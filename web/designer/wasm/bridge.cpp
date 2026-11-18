// The designer's live preview: a thin C surface over the device's own ui/
// drawing code, compiled to WebAssembly by tools/build-designer-wasm.sh.
//
// Nothing here draws. Every pixel comes from the same ui/*.cpp the firmware
// and the simulator compile; this file only does what sim/main_sim.cpp does
// around them (build a State, select the theme, call a screen) and what
// sim/packload.h does to admit a pack -- minus the files, because the browser
// hands us bytes. web/designer/test/wasm-parity.test.mjs holds it to that: its
// output must equal the simulator's PNGs byte for byte.
//
// One draft pack at a time: uv_load_pack validates the new draft completely
// first, and only then replaces the previous one (pal::clearPacks + addPack),
// so a rejected draft leaves the last good one loaded and drawable.
#include "../../../ui/screens.h"
#include "../../../ui/demo.h"
#include "../../../ui/nav.h"
#include "../../../ui/palette.h"
#include "../../../ui/signatures.h"
#include "../../../ui/themepack.h"
#include "../../../ui/themelayout.h"
#include "../../../ui/packassets.h"
#include <stdlib.h>
#include "../../../ui/preview_ws.h"
#include "../../../ui/meter.h"
#include <emscripten/emscripten.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

// Error codes (uv_load_pack returns one; uv_error() says why in words).
enum {
    UV_ERR_INPUT  = -1,   // missing, empty or over-size bytes
    UV_ERR_JSON   = -2,   // themepack::parse refused theme.json
    UV_ERR_EMBLEM = -3,   // emblem missing, over-size, or refused by parseEmblem
    UV_ERR_ADMIT  = -4,   // themepack::admit refused (reserved name, ...)
    UV_ERR_FULL   = -5,   // pal::addPack refused (cannot happen with one draft)
    UV_ERR_CLIP   = -6,   // radar.vid over-size or refused by parseClip
    UV_ERR_LAYOUT = -7,   // layout.json over-size or refused by themelayout::build (the device would skip the layout)
    UV_ERR_MEDIA  = -8,   // a font (.fnt) or picture (.art) over-size or refused by its parser
};

char g_err[128] = "";

int fail(int code, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
    return code;
}

// The registry keeps the emblem by POINTER, so the live draft's bytes live
// here for as long as it is loaded. A new emblem is parsed in the scratch
// buffer first and copied over only once the whole draft is accepted.
uint8_t g_scratch[themepack::MAX_EMBLEM];
uint8_t g_emblemBuf[themepack::MAX_EMBLEM];
themepack::Emblem g_emblem;
// The live draft's radar.vid, kept for as long as it is loaded (the registry holds a pointer into it).
uint8_t g_clipBuf[themepack::MAX_CLIP];
themepack::Clip g_clip;

// The admission folder name. The browser has no folder; the designer's
// exported zip names it separately and checks it with its own rules.
const char* const DRAFT_FOLDER = "draft";

ui::Canvas g_canvas;

// The draft's fonts and pictures (f0..f7.fnt, a0..a15.art), kept as the registry holds pointers into them.
uint8_t* g_mediaBytes[assets::MAX_FONTS + assets::MAX_ARTS];
fontpack::Font g_fonts[assets::MAX_FONTS];
artpack::Art g_arts[assets::MAX_ARTS];
bool g_fontOk[assets::MAX_FONTS], g_artOk[assets::MAX_ARTS];
int g_mediaOwner = -1;

void publishMedia() {
    const fontpack::Font* f[assets::MAX_FONTS] = {};
    const artpack::Art* a[assets::MAX_ARTS] = {};
    for (int i = 0; i < assets::MAX_FONTS; i++) if (g_fontOk[i]) f[i] = &g_fonts[i];
    for (int i = 0; i < assets::MAX_ARTS; i++) if (g_artOk[i]) a[i] = &g_arts[i];
    if (g_mediaOwner >= 0) assets::set((uint8_t)g_mediaOwner, f, a);
}
void clearMedia() {
    if (g_mediaOwner >= 0) assets::clear((uint8_t)g_mediaOwner);   // before the bytes go
    for (auto& b : g_mediaBytes) { free(b); b = nullptr; }
    for (bool& b : g_fontOk) b = false;
    for (bool& b : g_artOk) b = false;
    g_mediaOwner = -1;
}

// sim/png_writer.h's expansion, exactly: what makes byte equality possible.
inline void rgb565ToRgba(uint16_t c, unsigned char* o) {
    const int r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
    o[0] = (unsigned char)((r5 * 255 + 15) / 31);
    o[1] = (unsigned char)((g6 * 255 + 31) / 63);
    o[2] = (unsigned char)((b5 * 255 + 15) / 31);
    o[3] = 255;
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int uv_width() { return ui::W; }
EMSCRIPTEN_KEEPALIVE int uv_height() { return ui::H; }
EMSCRIPTEN_KEEPALIVE const char* uv_error() { return g_err; }
EMSCRIPTEN_KEEPALIVE int uv_theme_count() { return pal::themeCount(); }

EMSCRIPTEN_KEEPALIVE void uv_clear_packs() {
    clearMedia();
    pal::clearPacks();
    g_err[0] = 0;
}

// Parse and admit a draft pack, in sim/packload.h's order and with its rules:
// sizes -> parse -> emblem (only when the centrepiece is emblem) -> admit ->
// register. Returns the theme id (>= 0) or a negative UV_ERR_*.
// The draft's layout.json, as pal keeps it (a pointer): static, and rebuilt in place only once the draft is accepted.
themelayout::PackLayout g_layout, g_layoutProbe;

EMSCRIPTEN_KEEPALIVE int uv_load_pack(const char* json, int json_len,
                                      const unsigned char* emblem, int emblem_len,
                                      const unsigned char* clip, int clip_len,
                                      const char* layout, int layout_len) {
    using namespace themepack;
    g_err[0] = 0;
    if (json_len < 0 || emblem_len < 0 || clip_len < 0 || layout_len < 0) return fail(UV_ERR_INPUT, "negative length");
    if (!json || json_len == 0) return fail(UV_ERR_INPUT, "theme.json is empty");
    if ((size_t)json_len > MAX_JSON) return fail(UV_ERR_INPUT, "theme.json over 4096 bytes");

    Pack p;
    char pwhy[48];
    if (!parse(json, (size_t)json_len, p, pwhy, sizeof pwhy))
        return fail(UV_ERR_JSON, "theme.json: %s", pwhy);

    if (p.centre == CENTRE_EMBLEM) {
        if (!emblem || emblem_len == 0)
            return fail(UV_ERR_EMBLEM, "centre is emblem but emblem.bin is missing");
        if ((size_t)emblem_len > MAX_EMBLEM)
            return fail(UV_ERR_EMBLEM, "emblem.bin over 9728 bytes");
        memcpy(g_scratch, emblem, (size_t)emblem_len);
        Emblem e;
        char ewhy[40];
        if (!parseEmblem(g_scratch, (size_t)emblem_len, e, ewhy, sizeof ewhy))
            return fail(UV_ERR_EMBLEM, "emblem.bin: %s", ewhy);
    }

    // radar.vid is optional. Validated in place first (the bytes are the caller's), copied only once the draft is accepted.
    if (clip && clip_len > 0) {
        if ((size_t)clip_len > MAX_CLIP) return fail(UV_ERR_CLIP, "radar.vid over %u bytes", (unsigned)MAX_CLIP);
        Clip probe;
        char cwhy[48];
        if (!parseClip(clip, (size_t)clip_len, probe, cwhy, sizeof cwhy)) return fail(UV_ERR_CLIP, "%s", cwhy);
    }

    // layout.json is optional. The device would skip a bad one and keep the theme; the designer refuses the draft so the
    // author sees why (the same themelayout::build the device runs).
    if (layout && layout_len > 0) {
        char lwhy[96];
        if (!themelayout::build(layout, (size_t)layout_len, g_layoutProbe, lwhy, sizeof lwhy, p.color)) return fail(UV_ERR_LAYOUT, "%s", lwhy);
    }

    // The draft replaces whatever was loaded, so no other pack name is taken.
    const Admit a = admit(DRAFT_FOLDER, p, nullptr, 0);
    if (a != ADMIT_OK) return fail(UV_ERR_ADMIT, "%s", admitWhy(a));

    // Accepted: only now touch the live draft. Its fonts and pictures come after, through uv_load_media.
    const Emblem* em = nullptr;
    clearMedia();
    pal::clearPacks();
    if (p.centre == CENTRE_EMBLEM) {
        memcpy(g_emblemBuf, g_scratch, (size_t)emblem_len);
        char ewhy[40];
        // Re-parse in place so px points into the buffer that stays alive.
        if (!parseEmblem(g_emblemBuf, (size_t)emblem_len, g_emblem, ewhy, sizeof ewhy))
            return fail(UV_ERR_EMBLEM, "emblem.bin: %s", ewhy);   // unreachable: same bytes
        em = &g_emblem;
    }
    const Clip* cl = nullptr;
    if (clip && clip_len > 0) {
        memcpy(g_clipBuf, clip, (size_t)clip_len);
        char cwhy[48];
        if (!parseClip(g_clipBuf, (size_t)clip_len, g_clip, cwhy, sizeof cwhy)) return fail(UV_ERR_CLIP, "%s", cwhy);   // unreachable: same bytes
        cl = &g_clip;
    }
    const int id = pal::addPack(p, em, cl);
    if (id < 0) return fail(UV_ERR_FULL, "%s", admitWhy(ADMIT_FULL));
    if (layout && layout_len > 0) {
        char lwhy[96];
        if (!themelayout::build(layout, (size_t)layout_len, g_layout, lwhy, sizeof lwhy, p.color)) return fail(UV_ERR_LAYOUT, "%s", lwhy);   // unreachable: same bytes
        pal::setPackLayout(id, &g_layout.lo);
    }
    return id;
}

// One font (kind 0, slot 0..7 = fN.fnt) or picture (kind 1, slot 0..15 = aN.art) of the draft loaded as theme
// `theme`. The bytes are copied. 0, or UV_ERR_INPUT / UV_ERR_MEDIA with the reason in uv_error(); a refused file
// leaves that slot empty, as the device draws without it.
EMSCRIPTEN_KEEPALIVE int uv_load_media(int theme, int kind, int slot, const unsigned char* data, int len) {
    g_err[0] = 0;
    const bool isFont = kind == 0;
    if ((kind != 0 && kind != 1) || slot < 0 || slot >= (isFont ? assets::MAX_FONTS : assets::MAX_ARTS))
        return fail(UV_ERR_INPUT, "no such media slot");
    if (theme < 0 || theme >= pal::themeCount()) return fail(UV_ERR_INPUT, "no such theme");
    if (g_mediaOwner != theme) { clearMedia(); g_mediaOwner = theme; }
    const int k = isFont ? slot : assets::MAX_FONTS + slot;
    if (isFont) g_fontOk[slot] = false; else g_artOk[slot] = false;
    publishMedia();                                  // the old bytes are no longer reachable
    free(g_mediaBytes[k]); g_mediaBytes[k] = nullptr;
    const size_t cap = isFont ? fontpack::MAX_FONT : artpack::MAX_ART;
    if (!data || len <= 0) return fail(UV_ERR_INPUT, "empty file");
    if ((size_t)len > cap) return fail(UV_ERR_MEDIA, "%s%d over %u bytes", isFont ? "f" : "a", slot, (unsigned)cap);
    g_mediaBytes[k] = (uint8_t*)malloc((size_t)len);
    if (!g_mediaBytes[k]) return fail(UV_ERR_MEDIA, "out of memory");
    memcpy(g_mediaBytes[k], data, (size_t)len);
    const char* why = "";
    const bool ok = isFont ? fontpack::parse(g_mediaBytes[k], (size_t)len, g_fonts[slot], &why)
                           : artpack::parse(g_mediaBytes[k], (size_t)len, g_arts[slot], &why);
    if (!ok) { free(g_mediaBytes[k]); g_mediaBytes[k] = nullptr; return fail(UV_ERR_MEDIA, "%s%d: %s", isFont ? "f" : "a", slot, why); }
    if (isFont) g_fontOk[slot] = true; else g_artOk[slot] = true;
    publishMedia();
    return 0;
}

// 1 when the device would accept these bytes as a pack font (.fnt) or picture (.art); 0 with the reason in uv_error().
EMSCRIPTEN_KEEPALIVE int uv_font_check(const unsigned char* data, int len) {
    g_err[0] = 0;
    fontpack::Font f; const char* why = "";
    if (!data || len <= 0 || !fontpack::parse(data, (size_t)len, f, &why)) { fail(0, "%s", why[0] ? why : "empty file"); return 0; }
    return 1;
}
EMSCRIPTEN_KEEPALIVE int uv_art_check(const unsigned char* data, int len) {
    g_err[0] = 0;
    artpack::Art a; const char* why = "";
    if (!data || len <= 0 || !artpack::parse(data, (size_t)len, a, &why)) { fail(0, "%s", why[0] ? why : "empty file"); return 0; }
    return 1;
}

// The layout rules, for the designer's Layout step: the device's own names, defaults and verdict.
EMSCRIPTEN_KEEPALIVE int uv_piece_count() { return themelayout::P_N; }
EMSCRIPTEN_KEEPALIVE const char* uv_piece_name(int i) { return (i >= 0 && i < themelayout::P_N) ? themelayout::PIECE_NAME[i] : ""; }
// board 0 Waveshare, 1 CYD; writes x, y, w, h (int32) to out.
EMSCRIPTEN_KEEPALIVE void uv_layout_default(int board, int piece, int* out) {
    if (!out) return;
    if (board < 0 || board >= themelayout::B_N || piece < 0 || piece >= themelayout::P_N) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    const uieng::Rect r = themelayout::defaultRect((themelayout::Board)board, (themelayout::Piece)piece);
    out[0] = r.x; out[1] = r.y; out[2] = r.w; out[3] = r.h;
}
// Plan 5b: how many pieces may carry an alert effect on a board (0 Waveshare, 1 CYD); -1 for a bad board.
EMSCRIPTEN_KEEPALIVE int uv_fx_cap(int board) {
    return board >= 0 && board < themelayout::B_N ? themelayout::fxCap((themelayout::Board)board) : -1;
}
// The card a level shows when the theme gives none (ui::CONF_LOW 0, MED 1, HIGH 2): HIGH the whole screen, LOW and
// MED the hero of `theme`'s layout (scanr::alertCardRect without the theme's card; the hero moves with the radar
// group, ui/themelayout.cpp). x, y, w, h (int32) to out.
EMSCRIPTEN_KEEPALIVE void uv_card_default(int theme, int board, int conf, int* out) {
    if (!out) return;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (board < 0 || board >= themelayout::B_N || conf < 0 || conf > 2) return;
    const int W = board == themelayout::B_WAVESHARE ? 480 : 320, H = board == themelayout::B_WAVESHARE ? 320 : 240;
    pal::setTheme((uint8_t)theme);                 // installs that theme's layout as the active one, as rendering does
    const uieng::Rect r = conf == ui::CONF_HIGH ? uieng::Rect{0, 0, W, H} : uieng::scan(W, H).hero;
    out[0] = r.x; out[1] = r.y; out[2] = r.w; out[3] = r.h;
}
// 1 when the device would accept this layout.json on every board it gives; 0 with the reason in uv_error().
EMSCRIPTEN_KEEPALIVE int uv_layout_check(const char* json, int len) {
    g_err[0] = 0;
    if (!json || len <= 0) return fail(0, "layout.json is empty"), 0;
    char lwhy[96];
    if (!themelayout::build(json, (size_t)len, g_layoutProbe, lwhy, sizeof lwhy)) { fail(0, "%s", lwhy); return 0; }
    return 1;
}

// Draw one 160x120 frame as RGBA into out (W*H*4 bytes). Builds the State the
// way sim/main_sim.cpp does for the same arguments:
//   screen 0 scan, 1 mesh, 2 alert (the scan screen with an alert held up, as
//   `--alert`), 3 menu (the grimoire); anything else draws scan.
//   sweep < 0 is the simulator's default (RADAR); clampSettings then applies
//   the theme's offer, as on the device.
//   alert_style >= 0 holds an alert of that style up on any screen (the sim's
//   `--alert N` composes with `--screen` the same way); screen 2 with
//   alert_style < 0 uses style 0. The alert's age is frame * 20 ms.
EMSCRIPTEN_KEEPALIVE void uv_render(int theme, int screen, int frame, int sweep,
                                    int alert_style, unsigned char* out) {
    if (!out) return;
    const uint8_t themeId = (uint8_t)theme;
    const int alertSim = alert_style >= 0 ? alert_style : (screen == 2 ? 0 : -1);

    // Nothing may draw before the palette points somewhere.
    pal::setTheme(themeId);

    // The sim's canvas is a fresh zeroed static per run; match it per frame.
    memset(g_canvas.px, 0, sizeof g_canvas.px);

    const uint32_t f = (uint32_t)frame;
    ui::State s = ui::demoState(f);
    s.set.sweep = sweep >= 0 ? (uint8_t)sweep : (uint8_t)ui::SW_RADAR;
    s.set.theme = themeId;
    s.set.labels = 0;
    s.netId = 0;
    if (alertSim >= 0) {
        s.set.alertStyle = (uint8_t)alertSim;
        s.alertActive = true;
        s.alertThreat = ui::TH_DRONE;
        s.alertConf   = ui::CONF_HIGH;
        strcpy(s.alertWhy,  "DRONEID");
        strcpy(s.alertName, "60:60:1F:A2:4C:19");
        s.alertAge = (uint16_t)(f * 20);
    }
    ui::clampSettings(s.set);
    s.filter = ui::F_WIFI;
    switch (screen) {
    case 1:  s.screen = ui::SCR_MESH;     ui::drawMesh(g_canvas, s);     break;
    case 3:  s.screen = ui::SCR_GRIMOIRE; ui::drawGrimoire(g_canvas, s); break;
    default: s.screen = ui::SCR_SCAN;     ui::drawScan(g_canvas, s);     break;
    }

    for (int i = 0; i < ui::W * ui::H; i++) rgb565ToRgba(g_canvas.px[i], out + 4 * i);
}

// The Waveshare 3.5" engine UI (480x320), one frame as RGBA. Same arguments as uv_render plus the alert's confidence
// (ui::CONF_LOW 0, MED 1, HIGH 2); screen 4 is ASPECT. The drawing is ui/preview_ws.h, shared with the host reference.
// The engine UI at w x h (480x320 Waveshare or 320x240 CYD engine build): what layout.json changes.
EMSCRIPTEN_KEEPALIVE void uv_render_engine(int theme, int screen, int frame, int sweep,
                                           int alert_style, int alert_conf, int w, int h, unsigned char* out) {
    if (!out || w < 1 || h < 1 || w > prevws::W || h > prevws::H) return;
    static uint16_t px[prevws::W * prevws::H];
    prevws::render(theme, screen, frame, sweep, alert_style, alert_conf, px, w, h);
    for (int i = 0; i < w * h; i++) rgb565ToRgba(px[i], out + 4 * i);
}
EMSCRIPTEN_KEEPALIVE int uv_ws_width() { return prevws::W; }
EMSCRIPTEN_KEEPALIVE int uv_ws_height() { return prevws::H; }
EMSCRIPTEN_KEEPALIVE void uv_render_ws(int theme, int screen, int frame, int sweep,
                                       int alert_style, int alert_conf, unsigned char* out) {
    if (!out) return;
    static uint16_t px[prevws::W * prevws::H];
    prevws::render(theme, screen, frame, sweep, alert_style, alert_conf, px);
    for (int i = 0; i < prevws::W * prevws::H; i++) rgb565ToRgba(px[i], out + 4 * i);
}

// The speed meter (plan 5c): replay `frames` frames of the scan screen (mode -1) or an alert of LOW/MED/HIGH
// (mode 0/1/2) at w x h through the board's slice loop; out receives 2*frames values: drawn, pushed per frame.
// Returns the number of frames written (0 for a bad argument).
EMSCRIPTEN_KEEPALIVE int uv_meter(int theme, int w, int h, int mode, int frames, int frameMs, uint32_t* out) {
    if (!out || frames < 1 || frames > 600) return 0;
    std::vector<uimeter::Count> v((size_t)frames);
    const int n = meter::run(theme, w, h, mode, frames, frameMs, v.data());
    for (int i = 0; i < n; i++) { out[2 * i] = v[i].drawn; out[2 * i + 1] = v[i].pushed; }
    return n;
}

} // extern "C"
