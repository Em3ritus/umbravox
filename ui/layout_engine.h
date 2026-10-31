// layout_engine.h — resolution-independent UI geometry for umbravox.
//
// layout(W,H) rules compute every region as a pure function of the logical
// canvas size; hit regions come from the same geometry. One codebase lays out
// at any size; a scaling display adapter maps logical -> physical.
//
// Layout is pure geometry and state-INDEPENDENT. The ACTIVE HIT SET is
// state-AWARE: a threat target appears only in the threat state, and DOMAIN
// rows need a GPS fix and located records. Rects reproduce Codex's
// geometry.json EXACTLY for every state at both approved sizes (480x320,
// 320x240) -- the host oracle test asserts the complete set. No Arduino deps.
#pragma once
#include <algorithm>
#include <cmath>

namespace uieng {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const Rect& o) const { return x==o.x && y==o.y && w==o.w && h==o.h; }
};

inline int iround(double v) { return (int)std::lround(v); }

// --- shared chrome: the ONE design-1 header + nav bar every screen composes ----
// Single-sourced so a chrome fix lands once and every screen inherits it. The
// Header uses a theme title, GPS, optional MESH, BRIDGE toggle and battery;
// the nav bar is the four tabs + a STOP/REC record control at the far right:
//   statusCells[0..2] = GPS / MESH / POWER readout hits (cell 3 is the wordmark
//   area, NOT a control); navCells[0..3] = the four tabs; `stop` is the fifth
//   nav hit (the record control). `wordmark` is draw-only. `body` carries an 8px
//   margin (SCAN widens its own body to 16 at 480; the bands here are the same).
struct Chrome {
    Rect status, nav, body;
    Rect statusCells[4], navCells[4];
    Rect wordmark, stop, bridge;
};
inline Chrome chrome(int W, int H) {
    Chrome c;
    const bool wide = (W >= 400);
    c.status = {0, 0, W, 44};
    c.nav    = {0, H - 44, W, 44};
    c.body   = {8, 52, W - 16, H - 104};   // 52 = 44 status + 8 gap; 104 = that + 44 nav + 8 gap
    const int ny = H - 44;
    if (wide) {
        c.wordmark = {8, 0, 176, 44};
        c.statusCells[0] = {184, 0, 68, 44};
        c.statusCells[1] = {252, 0, 60, 44};
        c.bridge = {312, 0, 64, 44};
        c.statusCells[2] = {376, 0, W-384, 44};
        c.statusCells[3] = {0, 0, 184, 44};
        c.navCells[0] = {0,   ny, 99, 44}; c.navCells[1] = {99,  ny,  99, 44};
        c.navCells[2] = {198, ny, 99, 44}; c.navCells[3] = {297, ny, 101, 44};
        c.stop = {398, ny, 82, 44};
    } else {
        c.wordmark = {8, 0, 128, 44};
        c.statusCells[0] = {136, 0, 52, 44};
        c.statusCells[1] = {}; // Mesh remains available through MENU.
        c.bridge = {188, 0, 60, 44};
        c.statusCells[2] = {248, 0, W-256, 44};
        c.statusCells[3] = {0, 0, 136, 44};
        c.navCells[0] = {0,   ny, 65, 44}; c.navCells[1] = {65,  ny, 65, 44};
        c.navCells[2] = {130, ny, 65, 44}; c.navCells[3] = {195, ny, 65, 44};
        c.stop = {260, ny, 60, 44};
    }
    return c;
}

// --- SCAN: design #1 (WS35 full-design-scan) ----------------------------------
// This intentionally rewrites SCAN's geometry from design #2 (the old
// geometry.json oracle) to design #1 (the ratified proposal mockup): a wordmark
// header with GPS/MESH/BAT readouts, three count tiles, a LIVE FIELD hero, and a
// nav bar whose record control is a STOP button at the far right. Both approved
// profiles via one function; every hit >=44px (STOP draws ~56x30 but its touch
// rect is >=44). statusCells[0..2] = GPS/MESH/POWER readout hits (cell 3 is the
// wordmark area, not a control); navCells[0..3] = the four tabs; `stop` is the
// fifth nav hit. `wordmark` is draw-only.
struct ScanLayout {
    Rect status, nav, body, statusCells[4], navCells[4];
    Rect counts, hero, tiles[3], bridge;
    Rect wordmark, stop;
};
inline ScanLayout scan(int W, int H) {
    const Chrome c = chrome(W, H);               // design-1 bands, single-sourced
    ScanLayout s{};
    const bool wide = (W >= 400);
    const int M = wide ? 16 : 8;                 // design #1 margin: 16 @480, 8 @320
    s.status = c.status; s.nav = c.nav;
    s.body   = {M, 52, W - 2 * M, H - 104};      // SCAN widens the body to the 16px margin
    for (int i = 0; i < 4; i++) { s.statusCells[i] = c.statusCells[i]; s.navCells[i] = c.navCells[i]; }
    s.wordmark = c.wordmark; s.stop = c.stop;

    if (wide) {
        for (int i = 0; i < 3; i++) s.tiles[i] = {16, 52 + i * 70, 132, 62};
        s.hero = {156, 52, W - 156 - M, H - 104};
    } else {
        const int th = (H - 104) / 3;
        for (int i = 0; i < 3; i++) s.tiles[i] = {8, 52 + i * th, 88, (i < 2) ? th : (H - 104 - 2 * th)};
        s.hero = {104, 52, W - 104 - 8, H - 104};
    }
    s.bridge = c.bridge;
    s.counts = {s.tiles[0].x, s.tiles[0].y, s.tiles[0].w, H - 104};
    return s;
}

// --- DOMAIN: rectangular map (radar centered inside) + contacts + range -------
struct DomainLayout {
    Rect status, nav, body, statusCells[4], navCells[4];
    Rect map, contacts, range, minus, plus;
    bool wide = false;   // large names / 56px rows; compact uses 44px rows
};
inline DomainLayout domain(int W, int H) {
    const Chrome c = chrome(W, H);
    DomainLayout d;
    d.status = c.status; d.nav = c.nav; d.body = c.body;
    for (int i = 0; i < 4; i++) { d.statusCells[i] = c.statusCells[i]; d.navCells[i] = c.navCells[i]; }

    const int bw = c.body.w, bh = c.body.h;
    d.wide = bw >= 352;                                    // existing collapse threshold
    // Observations are primary at both sizes; map is a secondary overview.
    const int mapW = d.wide ? 144 : 104;
    const int contactsW = bw - mapW - 8;
    d.contacts = {c.body.x, c.body.y, contactsW, bh};
    d.map = {c.body.x + contactsW + 8, c.body.y, mapW, bh - 48};
    const int ry = c.body.y + bh - 44;
    d.range = {d.map.x, ry, mapW, 44};
    d.minus = {d.map.x, ry, 44, 44};
    d.plus = {d.map.x + mapW - 44, ry, 44, 44};
    return d;
}

// --- hit regions: slot id + rect + action + payload, from the same geometry ---
// The `desc` is documentation; `action`+`payload`/`key` drive runtime dispatch.
enum class Action : int { OpenGps, OpenMesh, PowerDetail, ToggleLogging,
                          Nav, CodexFilter, RangeStep,
                          OpenLocatedCodex, OpenDeviceDetail, OpenThreatDetail,
                          ToggleLabels, MeshEnroll, MeshResetNet,
                          SettingStep, SettingPage, SettingReset,
                          ThemeStep, OrientToggle, Back, CodexPage,
                          FileSelect, FileDelete, FileConfirmYes, FileConfirmNo, MarkPoi, DetailPage, MeshPage, ClientPage, EchoPage };
// Nav payload. The first four are the bottom nav-bar tabs (0..3, in bar order);
// the rest are the additional destinations the MENU hub launches into. Scan=0,
// Domain=1, Codex=2, Menu=3 are fixed (the oracle asserts those nav payloads).
enum class ScreenId : int { Scan, Domain, Codex, Menu,
                            Mesh, Omens, Echoes, Attune, Aspect };
enum class Band     : int { Wifi, FiveGhz, Ble };          // CodexFilter payload

struct Hit {
    const char* slot;    // distinct within a screen's hit list
    Rect        rect;
    Action      action;
    int         payload; // Nav->ScreenId, CodexFilter->Band, RangeStep->+/-1, row->index
    const char* key;     // stable record key for row/threat targets, else nullptr
    const char* desc;    // human-readable (docs only)
};
struct HitList {
    Hit items[22];
    int count = 0;
    void add(const char* slot, Rect r, Action a, int payload, const char* key, const char* desc) {
        items[count++] = {slot, r, a, payload, key, desc};
    }
};

// Presentation inputs the ACTIVE hit set depends on. All default to "nothing
// actionable": no matched device, no fix, no located records -- so the engine
// never fabricates a device-detail or threat target. `slot` is the positional
// UI identity; `key` is the record identity; they are kept separate. Row/match
// keys are a snapshot the CALLER owns and must hold stable from touch-down
// through gesture completion, so the row<->record binding cannot shift mid-tap.
struct HitCtx {
    const char*        matchKey = nullptr;  // matched-device key; threat target emitted iff non-null
    bool               hasFix   = false;    // GPS fix; DOMAIN maps rows only with a fix
    const char* const* rowKeys  = nullptr;  // located-device keys, by original row order
    int                nRowKeys = 0;        // a row is emitted ONLY for a valid supplied key
};

static const char* const kThreatDesc =
    "Open matching device detail; retain confidence and evidence; not a confirmed identity";

// A record/match key is usable only if non-null AND non-empty. An empty string
// is not a device identity and must not create an actionable target.
inline bool validKey(const char* k) { return k && k[0]; }

// The shared design-1 chrome hit set, derived from chrome(W,H) so EVERY screen
// binds the identical header + nav geometry: GPS/MESH/POWER readouts, the four
// nav tabs, and the STOP/REC record control (design #1 moved logging here from a
// status cell). The wordmark area (statusCells[3]) is draw-only, not a control.
inline void addChromeHits(HitList& h, int W, int H) {
    const Chrome c = chrome(W, H);
    h.add("gps",   c.statusCells[0], Action::OpenGps,     0, nullptr, "Open GPS diagnostics");
    if (c.statusCells[1].w) h.add("mesh",  c.statusCells[1], Action::OpenMesh,    0, nullptr, "Open Mesh");
    h.add("power", c.statusCells[2], Action::PowerDetail, 0, nullptr, "Power detail (NM: sensing unavailable)");
    h.add("bridge",c.bridge,Action::SettingStep,9,nullptr,"Toggle BLE bridge");
    h.add("nav-scan",c.statusCells[3],Action::Nav,0,nullptr,"Home: return to SCAN");
    static const char* const nslot[3] = {"nav-domain","nav-codex","nav-menu"};
    static const char* const ndesc[3] = {"Open DOMAIN","Open CODEX","Open MENU"};
    for (int i=0;i<3;++i) h.add(nslot[i],c.navCells[i],Action::Nav,i+1,nullptr,ndesc[i]);
    h.add("mark-poi",c.navCells[3],Action::MarkPoi,0,nullptr,"Save current GPS point of interest");
    h.add("stop", c.stop, Action::ToggleLogging, 0, nullptr, "Toggle logging; the REC/STOP record control");
}

inline HitList scanHits(int W, int H, const HitCtx& ctx) {
    const ScanLayout s = scan(W, H);
    HitList h;
    addChromeHits(h, W, H);              // shared design-1 header + nav + STOP
    // count tiles
    static const char* const tslot[3] = {"WIFI","5 GHZ","BLE"};
    static const char* const tdesc[3] = {"Open Codex filtered to WIFI",
                                         "Open Codex filtered to 5 GHZ",
                                         "Open Codex filtered to BLE"};
    for (int i = 0; i < 3; i++) h.add(tslot[i], s.tiles[i], Action::CodexFilter, i, nullptr, tdesc[i]);
    if (validKey(ctx.matchKey))   // threat target only for a real matched device
        h.add("threat", {s.hero.x, s.hero.y, s.hero.w, 44},
              Action::OpenThreatDetail, 0, ctx.matchKey, kThreatDesc);
    return h;
}

inline int domainRowHeight(const DomainLayout& d) { return d.wide ? 56 : 44; }
inline int domainRowBands(const DomainLayout& d) { return 1 + (d.contacts.h-44)/domainRowHeight(d); }
inline Rect domainRow(const DomainLayout& d,int index) {
    return {d.contacts.x,d.contacts.y+44+index*domainRowHeight(d),d.contacts.w,domainRowHeight(d)};
}

inline HitList domainHits(int W, int H, const HitCtx& ctx) {
    const DomainLayout d = domain(W, H);
    HitList h;
    addChromeHits(h, W, H);
    h.add("range-minus", d.minus, Action::RangeStep, -1, nullptr, "Select adjacent existing range; clamp at endpoint");
    h.add("range-plus",  d.plus,  Action::RangeStep, +1, nullptr, "Select adjacent existing range; clamp at endpoint");

    const int B = 44;
    const int cx = d.contacts.x, cy = d.contacts.y, cw = d.contacts.w;
    static const char* const SLOT[8] = {"contact-0","contact-1","contact-2","contact-3",
                                        "contact-4","contact-5","contact-6","contact-7"};
    // A located-device key for the row at band b, or nullptr (no actionable row):
    //   row at band b shows original record (b-1); never invented.
    auto rowKey = [&](int band) -> const char* {
        const int rec = band - 1;
        if (!ctx.hasFix || !ctx.rowKeys || rec < 0 || rec >= ctx.nRowKeys) return nullptr;
        return validKey(ctx.rowKeys[rec]) ? ctx.rowKeys[rec] : nullptr;   // empty key -> no row
    };

    {
        const int nbands = domainRowBands(d);                 // band 0 = header, 1.. = rows
        h.add("all-contacts", {cx, cy, cw, B}, Action::OpenLocatedCodex, 0, nullptr,
              "Open Codex with located scope");
        for (int b = 1; b < nbands; ++b) {
            const Rect r=domainRow(d,b-1);
            if (b == 1 && validKey(ctx.matchKey)) {   // threat REPLACES the first row; later rows retained
                h.add("threat", r, Action::OpenThreatDetail, 0, ctx.matchKey, kThreatDesc);
            } else if (const char* key = rowKey(b)) {
                h.add(SLOT[b & 7], r, Action::OpenDeviceDetail, b - 1, key,
                      "Open mapped device detail by stable identity");
            }  // else: empty band, no actionable hit
        }
    }
    return h;
}

// --- CODEX: filter bar (4 cells) + scrolling device list ----------------------
// The register of found devices. No delivered geometry.json oracle exists for
// CODEX (the oracle covers scan+domain only), so the regions are asserted
// against these rules + structural invariants. The filter bar divides the body
// into FOUR equal cells -- WIFI / 5 GHZ / BLE / LOCATED -- which is the 76px
// compact cell the contract's font note refers to (320 bodyW 304 / 4). LOCATED
// is the located scope DOMAIN's "all-contacts" opens into, reused here as a
// filter cell. Rows are a fixed 44px so every visible row is a touch target;
// the count that fits is size-dependent (3 @ 480, 2 @ 320), and the list drag /
// scrollbar page the rest.
struct CodexLayout {
    Rect status, nav, body, statusCells[4], navCells[4];
    Rect filterBar, filters[4];
    Rect list, rows[8], previous, next;
    int  nRows = 0;
};
inline CodexLayout codex(int W, int H) {
    const Chrome c = chrome(W, H);
    CodexLayout x;
    x.status = c.status; x.nav = c.nav; x.body = c.body;
    for (int i = 0; i < 4; i++) { x.statusCells[i] = c.statusCells[i]; x.navCells[i] = c.navCells[i]; }

    x.filterBar = {c.body.x, c.body.y, c.body.w, 44};
    const int fw = c.body.w / 4;
    for (int i = 0; i < 4; i++)                            // last cell takes the remainder -> tiles bodyW exactly
        x.filters[i] = {c.body.x + i * fw, c.body.y, i < 3 ? fw : c.body.w - 3 * fw, 44};

    const int RH = 44;
    x.list = {c.body.x, c.body.y + 44, c.body.w - 84, c.body.h - 44};
    x.previous = {x.list.x+x.list.w+8, x.list.y, 76, 44};
    x.next = {x.previous.x, x.list.y+x.list.h-44, 76, 44};
    x.nRows = x.list.h / RH; if (x.nRows > 6) x.nRows = 6; // 9 chrome + 4 filters + 2 pager + 6 rows fits HitList
    for (int i = 0; i < x.nRows; i++) x.rows[i] = {x.list.x, x.list.y + i * RH, x.list.w, RH};
    return x;
}

// CODEX hit set. Filters are always actionable; a row is emitted only for a
// valid supplied key (never an invented device). WIFI/5GHZ/BLE set the band
// filter; LOCATED selects the located scope (the same action DOMAIN uses to
// enter located Codex). Rows open device detail by stable identity.
inline HitList codexHits(int W, int H, const HitCtx& ctx) {
    const CodexLayout x = codex(W, H);
    HitList h;
    addChromeHits(h, W, H);
    static const char* const flab[4] = {"WIFI", "5 GHZ", "BLE", "LOCATED"};
    static const char* const fdesc[3] = {"Filter to WIFI (both Wi-Fi bands)",
                                         "Filter to 5 GHZ (a subset of WIFI)",
                                         "Filter to BLE"};
    for (int i = 0; i < 3; i++) h.add(flab[i], x.filters[i], Action::CodexFilter, i, nullptr, fdesc[i]);
    h.add(flab[3], x.filters[3], Action::OpenLocatedCodex, 0, nullptr, "Scope to located devices");

    h.add("previous",x.previous,Action::CodexPage,-1,nullptr,"Previous page");
    h.add("next",x.next,Action::CodexPage,1,nullptr,"Next page");
    static const char* const rslot[8] = {"codex-0","codex-1","codex-2","codex-3",
                                          "codex-4","codex-5","codex-6","codex-7"};
    for (int b = 0; b < x.nRows; ++b) {
        const char* key = (ctx.rowKeys && b < ctx.nRowKeys) ? ctx.rowKeys[b] : nullptr;
        if (validKey(key))
            h.add(rslot[b], x.rows[b], Action::OpenDeviceDetail, b, key,
                  "Open device detail by stable identity");
    }
    return h;
}

// --- MENU: the hub, a grid of launcher tiles (the old GRIMOIRE) ---------------
// No geometry.json oracle (scan+domain only), so regions are asserted against
// these rules + structural invariants. The body holds a resolution-independent
// 3x3 grid: eight screen launchers plus a LABELS tile that toggles brand/plain
// naming. Cells tile the body exactly (last col/row take the remainder) and
// stay >=44px at both approved profiles. The shared nav bar persists with MENU
// (index 3) lit, so the hub is a peer screen, reachable and leavable like the
// rest.
static constexpr int kMenuCols = 3, kMenuRows = 3, kMenuCells = 9;

struct MenuLayout {
    Rect status, nav, body, statusCells[4], navCells[4];
    Rect cells[kMenuCells];
    int  cols = kMenuCols, rows = kMenuRows, nCells = kMenuCells;
};
inline MenuLayout menu(int W, int H) {
    const Chrome c = chrome(W, H);
    MenuLayout m;
    m.status = c.status; m.nav = c.nav; m.body = c.body;
    for (int i = 0; i < 4; i++) { m.statusCells[i] = c.statusCells[i]; m.navCells[i] = c.navCells[i]; }

    const int cw = c.body.w / kMenuCols, rh = c.body.h / kMenuRows;
    for (int i = 0; i < kMenuCells; i++) {
        const int col = i % kMenuCols, row = i / kMenuCols;
        const int x = c.body.x + col * cw, y = c.body.y + row * rh;
        const int w = (col < kMenuCols - 1) ? cw : c.body.w - (kMenuCols - 1) * cw;
        const int hh = (row < kMenuRows - 1) ? rh : c.body.h - (kMenuRows - 1) * rh;
        m.cells[i] = {x, y, w, hh};
    }
    return m;
}

// MENU hit set. Eight tiles navigate (Nav payload = ScreenId); the ninth toggles
// the label mode. The tile order matches the delivered GRIMOIRE grid so the hit
// test and the drawing cannot disagree about which cell is which.
inline HitList menuHits(int W, int H) {
    const MenuLayout m = menu(W, H);
    HitList h;
    addChromeHits(h, W, H);
    struct ME { ScreenId id; const char* slot; const char* desc; };
    static const ME tiles[8] = {
        {ScreenId::Scan,   "menu-scan",   "Open SCAN"},
        {ScreenId::Codex,  "menu-codex",  "Open CODEX (FOUND)"},
        {ScreenId::Domain, "menu-domain", "Open DOMAIN (MAP)"},
        {ScreenId::Echoes, "menu-echoes", "Open ECHOES (PROBES)"},
        {ScreenId::Mesh,   "menu-mesh",   "Open MESH"},
        {ScreenId::Omens,  "menu-omens",  "Open OMENS (ALERTS)"},
        {ScreenId::Attune, "menu-attune", "Open ATTUNE (SETTINGS)"},
        {ScreenId::Aspect, "menu-aspect", "Open ASPECT (DISPLAY)"},
    };
    for (int i = 0; i < 8; i++) h.add(tiles[i].slot, m.cells[i], Action::Nav, (int)tiles[i].id, nullptr, tiles[i].desc);
    h.add("labels", m.cells[8], Action::ToggleLabels, 0, nullptr, "Toggle brand/plain labels");
    return h;
}

// --- deeper MENU destinations: shared body split for a titled sub-screen ------
// The hub launches into these; they carry the same chrome (status + nav bar,
// no tab lit) with a title band at the body top and the screen's own content
// below. No geometry.json oracle exists for any of them, so regions are
// asserted against these rules + structural invariants at both profiles.
struct SubChrome {
    Rect status, nav, body, statusCells[4], navCells[4];
    Rect title;   // body-top heading band
};
inline SubChrome subChrome(int W, int H) {
    const Chrome c = chrome(W, H);
    SubChrome s;
    s.status = c.status; s.nav = c.nav; s.body = c.body;
    for (int i = 0; i < 4; i++) { s.statusCells[i] = c.statusCells[i]; s.navCells[i] = c.navCells[i]; }
    s.title = {c.body.x, c.body.y, c.body.w, 20};
    return s;
}

// --- MESH: Core/loss header + paged diagnostics + ENROLL/PAGE/RESET -----------
struct MeshLayout {
    Rect status, nav, body, statusCells[4], navCells[4];
    Rect title, header, list, enroll, pager, reset;
};
inline MeshLayout mesh(int W, int H) {
    const SubChrome c = subChrome(W, H);
    MeshLayout m;
    m.status = c.status; m.nav = c.nav; m.body = c.body;
    for (int i = 0; i < 4; i++) { m.statusCells[i] = c.statusCells[i]; m.navCells[i] = c.navCells[i]; }
    m.title = c.title;
    const int ctrlH = 44, by = c.body.y, bh = c.body.h;
    m.header = {c.body.x, by + 20, c.body.w, 24};                  // CORE/COORD + LOSS
    m.list   = {c.body.x, by + 44, c.body.w, bh - 44 - ctrlH};     // node rows (display only)
    const int ctrlY = by + bh - ctrlH, cellW = (c.body.w - 16) / 3;
    m.enroll = {c.body.x, ctrlY, cellW, ctrlH};
    m.pager = {c.body.x+cellW+8, ctrlY, cellW, ctrlH};
    m.reset = {c.body.x+2*(cellW+8),ctrlY,c.body.w-2*(cellW+8),ctrlH};
    return m;
}
// MESH hits: two network-control buttons and a diagnostics pager. ENROLL
// opens the pairing window; RESET returns the fleet to the open network.
inline HitList meshHits(int W, int H) {
    const MeshLayout m = mesh(W, H);
    HitList h;
    addChromeHits(h, W, H);
    h.add("mesh-page",m.pager,Action::MeshPage,0,nullptr,"Next node diagnostics");
    h.add("enroll", m.enroll, Action::MeshEnroll,   0, nullptr, "Open a pairing window (adopt open nodes)");
    h.add("reset",  m.reset,  Action::MeshResetNet, 0, nullptr, "Return the fleet to the open network");
    return h;
}

// --- a read-only log body (OMENS / ECHOES): title + column header + list ------
// Both are records you read and scroll, not tap: an Omen carries no stable
// device key and a probe is a sighting, so neither row navigates. The whole
// list is one drag-to-scroll region (a gesture, not a discrete hit), so the hit
// set is the chrome only -- the same way SCAN/DOMAIN/CODEX never emitted a
// scroll hit. Compact rows (not 44px touch targets, since they are not hits).
struct LogLayout {
    Rect status, nav, body, statusCells[4], navCells[4];
    Rect title, header, list, prev, page, next;
    int  rowH = 14, nRows = 0;   // visible compact rows
};
inline LogLayout logBody(int W, int H) {
    const SubChrome c = subChrome(W, H);
    LogLayout g;
    g.status = c.status; g.nav = c.nav; g.body = c.body;
    for (int i = 0; i < 4; i++) { g.statusCells[i] = c.statusCells[i]; g.navCells[i] = c.navCells[i]; }
    g.title  = c.title;
    g.header = {c.body.x, c.body.y + 20, c.body.w, 12};
    g.list   = {c.body.x, c.body.y + 32, c.body.w, c.body.h - 32};
    g.rowH   = 14;
    g.nRows  = g.list.h / g.rowH;
    return g;
}
inline LogLayout omens(int W, int H)  { return logBody(W, H); }
inline LogLayout echoes(int W, int H) {
    auto g=logBody(W,H);const int y=g.body.y+g.body.h-44;
    g.list.h=y-g.list.y;g.nRows=W>=400?2:1;g.rowH=g.list.h/g.nRows;
    g.prev={g.body.x,y,44,44};g.next={g.body.x+g.body.w-44,y,44,44};g.page={g.body.x+48,y,g.body.w-96,44};return g;
}
inline HitList logHits(int W, int H) {        // chrome only; the list is a scroll gesture
    HitList h;
    addChromeHits(h, W, H);
    return h;
}
inline HitList omensHits(int W, int H)  { return logHits(W, H); }
inline HitList echoesHits(int W, int H) {
    auto h=logHits(W,H);const auto g=echoes(W,H);
    h.add("probe-prev",g.prev,Action::EchoPage,-1,nullptr,"Previous probe requests");
    h.add("probe-next",g.next,Action::EchoPage,1,nullptr,"Next probe requests");return h;
}

// --- ATTUNE: paginated knob rows + a pager/CLEAR footer ----------------------
// The shipped settings pack seven short rows, but the engine honours the >=44px
// touch rule: each knob row is 44px with a [-][+] stepper pair, so only
// floor((bodyH-44)/44) knobs fit per page (3 @480, 2 @320) above a 44px footer
// (PAGE n/m + CLEAR). The nine knobs paginate; the page COUNT therefore adapts
// to the profile. No geometry.json oracle; regions asserted vs these rules.
static constexpr int kAttuneKnobs = 13;   // ALERT DWELL BLE BANDS BRIGHT IDLE AUTOLOG ORIENT BRIDGE
inline int attuneRowsPerPage(int H) {
    const Chrome c = chrome(320, H);     // body height is width-independent
    int n = (c.body.h - 44) / 44; if (n < 1) n = 1; if (n > 4) n = 4;
    return n;
}
inline int attunePages(int H) {
    const int per = attuneRowsPerPage(H);
    return (kAttuneKnobs + per - 1) / per;
}
struct AttuneLayout {
    Rect status, nav, body, statusCells[4], navCells[4];
    Rect rows[4], minus[4], plus[4];     // per visible knob row
    Rect footer, pager, clear;
    int  nRows = 0;
};
inline AttuneLayout attune(int W, int H) {
    const Chrome c = chrome(W, H);
    AttuneLayout a;
    a.status = c.status; a.nav = c.nav; a.body = c.body;
    for (int i = 0; i < 4; i++) { a.statusCells[i] = c.statusCells[i]; a.navCells[i] = c.navCells[i]; }
    a.nRows = attuneRowsPerPage(H);
    for (int i = 0; i < a.nRows; i++) {
        const int y = c.body.y + i * 44;
        a.rows[i]  = {c.body.x, y, c.body.w, 44};
        a.minus[i] = {c.body.x + c.body.w - 92, y, 44, 44};
        a.plus[i]  = {c.body.x + c.body.w - 44, y, 44, 44};
    }
    const int fy = c.body.y + c.body.h - 44;
    a.footer = {c.body.x, fy, c.body.w, 44};
    a.pager  = {c.body.x, fy, 88, 44};
    a.clear  = {c.body.x + c.body.w - 88, fy, 88, 44};
    return a;
}
// ATTUNE hits for a given page. A stepper's payload encodes the GLOBAL knob
// index and direction as +/-(knob+1): dispatch reads dir = sign, knob =
// abs(payload)-1. The pager cycles pages (payload = next page); CLEAR resets.
inline HitList attuneHits(int W, int H, int page) {
    const AttuneLayout a = attune(W, H);
    const int per = a.nRows, pages = attunePages(H);
    if (page < 0) page = 0; else if (page >= pages) page = pages - 1;
    HitList h;
    addChromeHits(h, W, H);
    static const char* const mslot[4] = {"knob0-", "knob1-", "knob2-", "knob3-"};
    static const char* const pslot[4] = {"knob0+", "knob1+", "knob2+", "knob3+"};
    for (int i = 0; i < per; i++) {
        const int knob = page * per + i;
        if (knob >= kAttuneKnobs) break;
        const int setting = knob>=8 ? knob+1 : knob; // BRIDGE APP uses the remembered app selector
        h.add(mslot[i], a.minus[i], Action::SettingStep, -(setting + 1), nullptr, "Step this setting down");
        h.add(pslot[i], a.plus[i],  Action::SettingStep, +(setting + 1), nullptr, "Step this setting up");
    }
    h.add("pager", a.pager, Action::SettingPage, (page + 1) % pages, nullptr, "Cycle settings pages");
    h.add("clear", a.clear, Action::SettingReset, 0, nullptr, "Reset settings to defaults");
    return h;
}

// --- ASPECT: a themed dial preview + THEME / SWEEP / ALERT steppers -----------
// Appearance, not a capture trade-off, so it is its own screen. The preview is
// a left column; the three stepper rows stack in a right column so every
// [-][+] is a >=44px target at both profiles (a preview-on-top layout would
// crush the rows on the 136px body). The ALERT row is actionable only on a
// theme with a choice; otherwise its steppers are omitted (not a dead target).
struct AspectLayout {
    Rect status, nav, body, statusCells[4], navCells[4];
    Rect preview, rows[3], minus[3], plus[3];
    int  nRows = 3;
};
inline AspectLayout aspect(int W, int H) {
    const Chrome c = chrome(W, H);
    AspectLayout a;
    a.status = c.status; a.nav = c.nav; a.body = c.body;
    for (int i = 0; i < 4; i++) { a.statusCells[i] = c.statusCells[i]; a.navCells[i] = c.navCells[i]; }
    const int previewW = c.body.w * 2 / 5;
    a.preview = {c.body.x, c.body.y, previewW, c.body.h};
    const int rx = c.body.x + previewW + 8, rw = c.body.w - previewW - 8;
    for (int i = 0; i < 3; i++) {
        const int y = c.body.y + i * 44;
        a.rows[i]  = {rx, y, rw, 44};
        a.minus[i] = {rx + rw - 88, y, 44, 44};
        a.plus[i]  = {rx + rw - 44, y, 44, 44};
    }
    return a;
}
// ASPECT hits. THEME uses ThemeStep (a theme change forces a full repaint, so
// dispatch wants it distinct); SWEEP/ALERT use SettingStep with payload
// +/-(row+1) (row 1 SWEEP, 2 ALERT). The ALERT steppers are emitted only when
// alertLive -- a theme with an alert-style choice.
inline HitList aspectHits(int W, int H, bool alertLive) {
    const AspectLayout a = aspect(W, H);
    HitList h;
    addChromeHits(h, W, H);
    h.add("theme-", a.minus[0], Action::ThemeStep, -1, nullptr, "Previous theme");
    h.add("theme+", a.plus[0],  Action::ThemeStep, +1, nullptr, "Next theme");
    h.add("sweep-", a.minus[1], Action::SettingStep, -2, nullptr, "Previous sweep");
    h.add("sweep+", a.plus[1],  Action::SettingStep, +2, nullptr, "Next sweep");
    if (alertLive) {
        h.add("alert-", a.minus[2], Action::SettingStep, -3, nullptr, "Previous alert style");
        h.add("alert+", a.plus[2],  Action::SettingStep, +3, nullptr, "Next alert style");
    }
    return h;
}

// --- DETAIL: one device -- identity, signature, RSSI history, first/last ------
// Reached from a CODEX/DOMAIN row or a threat alert (OpenDeviceDetail /
// OpenThreatDetail). MORE switches facts pages; BACK (44px) is what the router
// turns into a return that preserves the caller's list scope + scroll. If the
// device vanishes the scene reads invalid and the body shows a graceful "GONE".
// No geometry.json oracle; regions asserted vs these rules.
struct DetailLayout {
    Rect status, nav, body, statusCells[4], navCells[4];
    Rect back, more, header, stats, spark;
};
inline DetailLayout detail(int W, int H) {
    const Chrome c = chrome(W, H);
    DetailLayout d;
    d.status = c.status; d.nav = c.nav; d.body = c.body;
    for (int i = 0; i < 4; i++) { d.statusCells[i] = c.statusCells[i]; d.navCells[i] = c.navCells[i]; }
    d.back   = {c.body.x, c.body.y, 44, 44};
    d.header = {c.body.x + 48, c.body.y, c.body.w - 48, 44};
    d.more = {c.body.x,c.body.y+44,44,44};
    const int sparkH = W>=400 ? 44 : 16;
    d.stats  = {c.body.x+48, c.body.y + 44, c.body.w-48, c.body.h - 44 - sparkH};
    d.spark  = {c.body.x, c.body.y + c.body.h - sparkH, c.body.w, sparkH};
    return d;
}
struct ClientLayout { Rect heading,rows[2],prev,page,next;int nRows=0; };
inline ClientLayout detailClients(int W,int H){
    const auto d=detail(W,H);ClientLayout c;
    c.heading={d.stats.x,d.stats.y,d.stats.w,12};
    const int bottom=d.body.y+d.body.h-44;
    c.nRows=W>=400?2:1;
    const int rowH=(bottom-c.heading.y-c.heading.h)/c.nRows;
    for(int i=0;i<c.nRows;i++)c.rows[i]={d.stats.x,c.heading.y+12+i*rowH,d.stats.w,rowH};
    c.prev={d.body.x,bottom,44,44};c.next={d.body.x+d.body.w-44,bottom,44,44};
    c.page={d.body.x+48,bottom,d.body.w-96,44};return c;
}
inline HitList detailHits(int W, int H,bool clients=false) {
    const DetailLayout d = detail(W, H);
    HitList h;
    addChromeHits(h, W, H);
    h.add("back", d.back, Action::Back, 0, nullptr, "Return to the list, preserving scope and scroll");
    h.add("detail-page",d.more,Action::DetailPage,0,nullptr,"Cycle signal/location, advertised AP information and observed clients");
    if(clients){const auto c=detailClients(W,H);h.add("client-prev",c.prev,Action::ClientPage,-1,nullptr,"Previous observed clients");h.add("client-next",c.next,Action::ClientPage,1,nullptr,"Next observed clients");}
    return h;
}

// --- FILES: the SD log list + a DELETE control with a YES/NO confirm modal ----
// Log rows are the touch targets, so they are 44px -- the list therefore shows
// floor((bodyH-44)/44) of them (3 @480, 2 @320) above a 44px action bar, and
// the rest page by scroll. DELETE opens a confirm modal; while it is up the row
// and DELETE hits are suppressed (input never reaches a control behind the
// modal) and the bar band carries YES / NO. No geometry.json oracle.
struct FilesLayout {
    Rect status, nav, body, statusCells[4], navCells[4];
    Rect list, rows[4], bar, del, yes, no;
    int  nRows = 0;
};
inline FilesLayout files(int W, int H) {
    const Chrome c = chrome(W, H);
    FilesLayout f;
    f.status = c.status; f.nav = c.nav; f.body = c.body;
    for (int i = 0; i < 4; i++) { f.statusCells[i] = c.statusCells[i]; f.navCells[i] = c.navCells[i]; }
    const int barY = c.body.y + c.body.h - 44;
    f.list = {c.body.x, c.body.y, c.body.w, c.body.h - 44};
    f.nRows = f.list.h / 44; if (f.nRows > 4) f.nRows = 4;
    for (int i = 0; i < f.nRows; i++) f.rows[i] = {c.body.x, c.body.y + i * 44, c.body.w, 44};
    f.bar = {c.body.x, barY, c.body.w, 44};
    f.del = {c.body.x + c.body.w - 120, barY, 120, 44};
    const int half = (c.body.w - 8) / 2;
    f.yes = {c.body.x, barY, half, 44};
    f.no  = {c.body.x + c.body.w - half, barY, half, 44};
    return f;
}
// FILES hits. In confirm mode: chrome + YES + NO only. Otherwise: chrome + a
// FileSelect per visible row (payload = visible index; caller adds scroll) +
// DELETE only when a deletable log is selected.
inline HitList filesHits(int W, int H, bool confirm, bool deletable, int nVisible) {
    const FilesLayout f = files(W, H);
    HitList h;
    addChromeHits(h, W, H);
    if (confirm) {
        h.add("yes", f.yes, Action::FileConfirmYes, 0, nullptr, "Confirm delete (cannot undo)");
        h.add("no",  f.no,  Action::FileConfirmNo,  0, nullptr, "Cancel the delete");
        return h;
    }
    if (nVisible > f.nRows) nVisible = f.nRows;
    static const char* const rslot[4] = {"file0", "file1", "file2", "file3"};
    for (int i = 0; i < nVisible; i++)
        h.add(rslot[i], f.rows[i], Action::FileSelect, i, nullptr, "Select this log");
    if (deletable) h.add("delete", f.del, Action::FileDelete, 0, nullptr, "Delete the selected log");
    return h;
}

}  // namespace uieng
