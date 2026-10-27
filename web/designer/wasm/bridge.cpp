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

// The admission folder name. The browser has no folder; the designer's
// exported zip names it separately and checks it with its own rules.
const char* const DRAFT_FOLDER = "draft";

ui::Canvas g_canvas;

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
    pal::clearPacks();
    g_err[0] = 0;
}

// Parse and admit a draft pack, in sim/packload.h's order and with its rules:
// sizes -> parse -> emblem (only when the centrepiece is emblem) -> admit ->
// register. Returns the theme id (>= 0) or a negative UV_ERR_*.
EMSCRIPTEN_KEEPALIVE int uv_load_pack(const char* json, int json_len,
                                      const unsigned char* emblem, int emblem_len) {
    using namespace themepack;
    g_err[0] = 0;
    if (json_len < 0 || emblem_len < 0) return fail(UV_ERR_INPUT, "negative length");
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

    // The draft replaces whatever was loaded, so no other pack name is taken.
    const Admit a = admit(DRAFT_FOLDER, p, nullptr, 0);
    if (a != ADMIT_OK) return fail(UV_ERR_ADMIT, "%s", admitWhy(a));

    // Accepted: only now touch the live draft.
    const Emblem* em = nullptr;
    pal::clearPacks();
    if (p.centre == CENTRE_EMBLEM) {
        memcpy(g_emblemBuf, g_scratch, (size_t)emblem_len);
        char ewhy[40];
        // Re-parse in place so px points into the buffer that stays alive.
        if (!parseEmblem(g_emblemBuf, (size_t)emblem_len, g_emblem, ewhy, sizeof ewhy))
            return fail(UV_ERR_EMBLEM, "emblem.bin: %s", ewhy);   // unreachable: same bytes
        em = &g_emblem;
    }
    const int id = pal::addPack(p, em);
    if (id < 0) return fail(UV_ERR_FULL, "%s", admitWhy(ADMIT_FULL));
    return id;
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

} // extern "C"
