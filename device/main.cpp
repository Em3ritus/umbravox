// NM-CYD-C5 bring-up: draw AETHERSCAN on the real panel.
//
// This is the whole device side. Everything visual lives in ui/, compiled
// unchanged from what the simulator builds -- the only job here is to own the
// panel, own the backlight, and blit.
#include <Arduino.h>
#include <stdarg.h>
#include <Arduino_GFX_Library.h>

#include "screens.h"
#include "gnss.h"
#include "gpstrack.h"
#include <math.h>
#include "capture.h"
#include "blescan.h"
#include "touch.h"
#include "prefs.h"
#include "alertlog.h"
#include "meshcore.h"
#include "mesh.h"
#include "signatures.h"
#include "nav.h"
#include "layout.h"
#include "palette.h"
#include "wigle.h"

// ---- pins (RockBase NM-CYD-C5, confirmed against Documention/2-sch) -------
// LCD, touch and the SD card all share one SPI bus.
static constexpr int8_t PIN_SCK  = 6;
static constexpr int8_t PIN_MOSI = 7;
static constexpr int8_t PIN_MISO = 2;
static constexpr int8_t PIN_CS   = 23;
static constexpr int8_t PIN_DC   = 24;
static constexpr int8_t PIN_RST  = -1;   // tied to the C5's own reset
static constexpr int8_t PIN_BL   = 25;   // NOT 27 -- 27 is the WS2812
static constexpr int    PANEL_W  = 320;
static constexpr int    PANEL_H  = 240;

// GPS on header P5 (NOT P1 -- GPIO 4 appears on both, and only P5 carries the
// receiver's TX). Confirmed on the board: ~9600 baud, measured from a 98us
// minimum pulse width rather than guessed.
static constexpr int8_t PIN_GPS_RX = 4;    // we receive on 4
static constexpr int8_t PIN_GPS_TX = 5;
static constexpr int    GPS_BAUD   = 9600;

// 40MHz is the ceiling on this board, not a choice: measured 35ms/frame at
// both 40 and 80MHz, and 112ms at 10MHz, so the parameter works and 80 is
// being clamped. SPI above 40MHz needs the dedicated IOMUX pins, and SCK 6 /
// MOSI 7 here route through the GPIO matrix. The transfer cannot be made
// faster -- only smaller, which is what the dirty rectangles are for.
static constexpr int32_t SPI_HZ  = 40000000;

static Arduino_DataBus* bus = new Arduino_ESP32SPI(PIN_DC, PIN_CS, PIN_SCK, PIN_MOSI, PIN_MISO);
// Native panel is 240x320 portrait; rotation 3 lands it landscape, which is
// how the board is cased and how the UI is laid out.
static Arduino_TFT* gfx = new Arduino_ST7789(bus, PIN_RST, 3 /*rotation*/, false /*IPS*/,
                                             240, 320);

static ui::Canvas canvas;          // 38,400 bytes, internal SRAM by design
static uint16_t   lineBuf[PANEL_W];
static HardwareSerial gpsPort(1);

// One canvas rect -> the panel at 2x, nearest neighbour. Doubling into a line
// buffer and pushing each row twice keeps the transfer sequential, which is
// what matters: on a single-core C5 every millisecond in here is a
// millisecond not spent capturing.
static void blitRect(const uint16_t* src, const ui::Rect& r) {
    const int x0 = r.x < 0 ? 0 : r.x;
    const int y0 = r.y < 0 ? 0 : r.y;
    const int x1 = (r.x + r.w) > ui::W ? ui::W : (r.x + r.w);
    const int y1 = (r.y + r.h) > ui::H ? ui::H : (r.y + r.h);
    if (x1 <= x0 || y1 <= y0) return;
    const int w = x1 - x0;

    gfx->startWrite();
    gfx->writeAddrWindow(x0 * 2, y0 * 2, w * 2, (y1 - y0) * 2);
    for (int y = y0; y < y1; y++) {
        const uint16_t* s = src + (size_t)y * ui::W + x0;
        for (int x = 0; x < w; x++) {
            lineBuf[x * 2]     = s[x];
            lineBuf[x * 2 + 1] = s[x];
        }
        gfx->writePixels(lineBuf, w * 2);
        gfx->writePixels(lineBuf, w * 2);
    }
    gfx->endWrite();
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("AETHERSCAN bring-up");
    Serial.printf("chip  : %s rev %d, %d core(s)\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
    Serial.printf("flash : %u bytes\n", (unsigned)ESP.getFlashChipSize());
    Serial.printf("heap  : %u free\n", (unsigned)ESP.getFreeHeap());
    Serial.printf("canvas: %u bytes\n", (unsigned)sizeof(canvas));
    Serial.printf("psram : %u bytes (%u free)\n",
                  (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());

    if (!gfx->begin(SPI_HZ)) {
        Serial.println("gfx->begin() FAILED");
        for (;;) delay(1000);
    }
    gfx->fillScreen(RGB565_BLACK);

    // Arduino core 3.x LEDC. The old ledcSetup()/ledcAttachPin() pair is gone,
    // and ledcWrite() now takes the pin rather than a channel -- this is the
    // API break that makes porting core-2 firmware to the C5 expensive.
    ledcAttach(PIN_BL, 5000, 8);
    ledcWrite(PIN_BL, 204);     // 80%, matching Settings' default

    gpsPort.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    cap::begin();
    blescan::begin();
    meshcore::begin();
    touch::begin();
    Serial.printf("panel up @ %d MHz SPI, GPS on GPIO %d @ %d\n",
                  (int)(SPI_HZ / 1000000), (int)PIN_GPS_RX, GPS_BAUD);
}

// Console logging that the compiler checks.
//
// Serial.printf() has no printf format attribute, so passing an int to a %s
// compiles clean and then dereferences it as a pointer. That crashed the
// board once already: an edit changed the arguments of the status line but
// its format string did not match, and `%s` got an integer 0.
__attribute__((format(printf, 1, 2)))
static void logf(const char* fmt, ...) {
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    Serial.print(buf);
}

// --- test alerts --------------------------------------------------------
// Send "TEST" over the console to walk the alert card through all three
// confidence levels. Kept rather than scaffolded out: there is no other way
// to see what a HIGH-confidence Flock alert looks like without standing in
// front of a Flock camera, and the card's colours are the thing most likely
// to need adjusting.
namespace {

struct TestStep { uint8_t threat, conf; const char* why; const char* name; };
const TestStep TEST_SEQ[] = {
    {ui::TH_FLOCK,   ui::CONF_HIGH, "FLOCK-MA-L",      "B4:1E:52"},
    {ui::TH_GLASSES, ui::CONF_MED,  "RAYBANMETA",      "4F:2A:08"},
    {ui::TH_CAMERA,  ui::CONF_LOW,  "TUYA",            "A4:C1:38"},
    // The two behavioural findings: measured, not matched.
    {ui::TH_DEAUTH,  ui::CONF_HIGH, "FLOOD",           "3C:A7:19 92/S"},
    {ui::TH_ROGUE,   ui::CONF_MED,  "SSID AUTH CLASH", "HOLLOWBROOK"},
    {ui::TH_FOLLOW,  ui::CONF_HIGH, "TRAVELLED WITH YOU", "A4:2C:1E 1.9KM"},
};
constexpr int  TEST_N  = (int)(sizeof(TEST_SEQ) / sizeof(TEST_SEQ[0]));
constexpr uint32_t TEST_STEP_MS = 5000;

int      testStep  = -1;          // -1 = not running
uint32_t testAt    = 0;

// Bridges the pure header sink to a Print, then appends both exporters.
struct PrintSink : ui_wigle::Sink {
    Print& p;
    PrintSink(Print& pp) : p(pp) {}
    void put(const char* s) override { p.print(s); }
};

static uint32_t writeWigle(Print& out, uint32_t& skipped) {
    PrintSink sink(out);
    ui_wigle::header(sink);
    const uint32_t w = cap::exportRows(out, skipped);
    const uint32_t b = blescan::exportRows(out, skipped);
    return w + b;
}

void pollConsole() {
    static char line[32];
    static int  n = 0;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (!n) continue;
            line[n] = 0;
            n = 0;
            if (!strncmp(line, "TEST", 4)) {
                testStep = 0;
                testAt   = millis();
                Serial.printf("test alert: %d steps, %d ms each\n", TEST_N, (int)TEST_STEP_MS);
            } else if (!strncmp(line, "MESHSTAT", 8)) {
                uint32_t tot, mg, vr, ln, ns;
                meshcore::rxStats(tot, mg, vr, ln, ns);
                logf("meshstat: callbacks=%lu  rejected: magic=%lu ver=%lu "
                     "len=%lu noslot=%lu  accepted reports=%lu\n",
                     (unsigned long)tot, (unsigned long)mg, (unsigned long)vr,
                     (unsigned long)ln, (unsigned long)ns,
                     (unsigned long)meshcore::reportsRx());
            } else if (!strncmp(line, "WIGLE", 5)) {
                // One file, two sources. Main owns the markers; the header
                // lives in wigle.h so Serial and the SD file cannot disagree.
                Serial.println("=== WIGLE BEGIN ===");
                uint32_t skipped = 0;
                const uint32_t n = writeWigle(Serial, skipped);
                logf("=== WIGLE END === %lu rows, %lu skipped "
                     "(no position or no UTC)\n",
                     (unsigned long)n, (unsigned long)skipped);
            } else {
                Serial.printf("unknown command '%s' (try TEST or WIGLE)\n", line);
            }
        } else if (n < 31) {
            line[n++] = c;
        }
    }
}

// Fills `s` from the test sequence if one is running. Deliberately ignores
// the ALERT floor -- the point is to see the card, including the LOW one the
// floor would normally hide.
bool testAlert(ui::State& s, uint32_t nowMs) {
    if (testStep < 0) return false;
    if (nowMs - testAt >= TEST_STEP_MS) {
        testAt = nowMs;
        if (++testStep >= TEST_N) { testStep = -1; return false; }
    }
    const TestStep& st = TEST_SEQ[testStep];
    s.alertActive = true;
    s.alertThreat = st.threat;
    s.alertConf   = st.conf;
    snprintf(s.alertWhy,  sizeof(s.alertWhy),  "%s", st.why);
    snprintf(s.alertName, sizeof(s.alertName), "%s", st.name);
    return true;
}

} // namespace

void loop() {
    static uint32_t frame = 0;
    static ui::Dirty dirty;
    const uint32_t t0 = millis();

    // Drain the receiver every frame. At 9600 baud a frame's worth is well
    // under 100 bytes, so this never blocks long enough to matter.
    while (gpsPort.available()) ui::Gnss::feed((char)gpsPort.read(), millis());

    cap::tick(millis());
    meshcore::tick(millis());
    blescan::tick(millis());

    // With nodes in the mesh the Core stops sweeping and holds the report
    // channel. Measured: a sweeping Core hears only 4% of what a node sends,
    // because it is on channel 1 for one dwell in twenty-one. Holding it
    // costs the Core's own sweep and buys every node's entire catch.
    {
        static bool camped = false;
        const bool want = meshcore::count() > 0;
        if (want != camped) {
            camped = want;
            cap::setCamp(want ? mesh::REPORT_CHAN : 0);
            Serial.printf("mesh: %s\n", want
                ? "nodes present -- core holding the report channel"
                : "no nodes -- core sweeping again");
        }
    }

    // A default-constructed State, filled only from real sources. It used to
    // start from demoState(), which quietly left four invented values on the
    // glass -- uptime, battery, and an SD status claiming a card was present
    // with a log file name to match. Anything not filled below now shows its
    // honest default instead.
    ui::State s;
    s.uptimeS = millis() / 1000;

    // The animation clock. Driven by time, not by the frame counter: the
    // frame rate is not constant (an alert animating costs 32ms against a
    // steady 13ms), so a per-frame tick made the sweep visibly slow down
    // exactly when something interesting was on screen.
    //
    // Losing this is silent -- the dial simply freezes, with nothing logged
    // and nothing failing. It went missing when the device stopped borrowing
    // demoState(), which had been setting it.
    s.frame = millis() / 16;
    s.sdOk    = false;              // no card support yet; the footer says so
    cap::fill(s, millis());
    blescan::fill(s, millis());
    meshcore::fill(s, millis());
    s.coreCamping = cap::camping();
    s.coreChan    = s.chan;
    s.coreFrames  = cap::localFrames();
    s.meshHits    = cap::frames();
    s.coreFound   = cap::foundBy(0);

    // Declared before the dimmer, which needs the brightness and timeout
    // settings to decide anything.
    static ui::Nav nav;
    static ui::Settings set;
    static bool loaded = false;
    if (!loaded) {
        prefs::load(set);
        loaded = true;
        // Touch hit-testing reads the theme's geometry (dial, spectrum), and
        // the first navTouch runs before the draw-time apply below. Same
        // task, before any draw, so this is safe.
        pal::setTheme(set.theme);
    }

    const touch::Point tp = touch::read();

    // --- idle dimming ---------------------------------------------------
    //
    // Worth more than comfort on a night drive: with the panel dark there is
    // no reason to draw or blit, and that is ~13ms of every frame handed
    // back to packet capture.
    //
    // The touch that wakes the screen is swallowed rather than passed on. A
    // panel you cannot see is a panel you cannot aim at, so the first
    // contact should only ever mean "wake" -- otherwise reaching for a dark
    // screen navigates somewhere at random.
    static uint32_t lastTouchMs = 0;
    static bool     dimmed = false;
    static bool     wakeScreen = false;
    static uint8_t  pwmNow = 0;
    static bool     pwmInit = false;

    if (wakeScreen) {
        wakeScreen = false;
        lastTouchMs = millis();
        dimmed = false;
    }

    bool tpDown = tp.down;
    if (tpDown) {
        if (dimmed) { tpDown = false; }        // this one only wakes
        lastTouchMs = millis();
        dimmed = false;
    }
    const uint16_t idleS = ui::IDLE_S[set.idleIdx % ui::IDLE_N];
    if (idleS && !dimmed && (millis() - lastTouchMs) > (uint32_t)idleS * 1000)
        dimmed = true;

    // Dim to a fraction of the chosen brightness rather than a fixed level,
    // so someone running at 3% does not get *brighter* when it dims.
    const uint8_t wantPct = dimmed ? (set.bright > 16 ? set.bright / 8 : 2)
                                   : set.bright;
    const uint8_t wantPwm = (uint8_t)((uint32_t)wantPct * 255 / 100);
    if (!pwmInit) { pwmNow = wantPwm; pwmInit = true; }
    if (pwmNow != wantPwm) {
        // Ramp rather than snap: a backlight that steps is startling in the
        // dark, and the fade costs nothing.
        const int step = 6;
        if (pwmNow < wantPwm) pwmNow = (uint8_t)(pwmNow + step > wantPwm ? wantPwm : pwmNow + step);
        else                  pwmNow = (uint8_t)(pwmNow - step < wantPwm ? wantPwm : pwmNow - step);
        ledcWrite(PIN_BL, pwmNow);
    }
    const bool screenOff = dimmed && pwmNow == wantPwm;

    // Nav consumes the touch before the screen sees it, so a swipe that
    // changes screens does not also land as a tap on the one arriving.
    const uint16_t rows = nav.screen == ui::SCR_ECHOES ? cap::echoCount()
                        : nav.screen == ui::SCR_OMENS ? alertlog::count()
                        : nav.filter == ui::F_BLE ? blescan::sightCount()
                        : cap::sightCount(nav.filter == ui::F_5G);
    const int visible = nav.screen == ui::SCR_OMENS  ? ui::MAX_OMEN_ROWS
                      : nav.screen == ui::SCR_ECHOES ? ui::MAX_ECHO_ROWS : ui::ROWS;
    nav.maxScroll = (int16_t)(rows > visible ? rows - visible : 0);
    const bool switched = ui::navTouch(nav, set, tpDown, tp.x, tp.y);

    // Push settings to the hardware only when they actually move: changing
    // the BLE duty restarts the scanner, so doing it every frame would mean
    // never scanning at all.
    static ui::Settings applied;
    if (set.dwellMs != applied.dwellMs) cap::setDwell(set.dwellMs);
    if (set.bands   != applied.bands)   cap::setBands(set.bands);
    if (set.bleDuty != applied.bleDuty) blescan::setDuty(set.bleDuty);
    applied = set;
    prefs::tick(set, millis());

    if (nav.resetRequested) {
        nav.resetRequested = false;
        cap::reset();
        blescan::reset();
        alertlog::clear();
        Serial.println("counters cleared");
    }

    // Alerts: take the strongest recent hit from either radio that clears
    // the confidence floor. The card fades rather than latching, so one on
    // the glass means the thing is still in range.
    pollConsole();

    constexpr uint32_t ALERT_MS = 6000;
    s.alertActive = false;
    if (testAlert(s, millis())) {
        // a test alert is up; leave it alone
    } else if (set.alertMin != ui::CONF_OFF) {
        ui::AlertRec w, b, best;
        bool have = false;
        if (cap::lastAlert(w) && w.conf >= set.alertMin &&
            (millis() - w.atMs) < ALERT_MS) { best = w; have = true; }
        if (blescan::lastAlert(b) && b.conf >= set.alertMin &&
            (millis() - b.atMs) < ALERT_MS &&
            (!have || b.atMs > best.atMs))  { best = b; have = true; }
        if (have) {
            s.alertActive = true;
            s.alertThreat = best.threat;
            s.alertConf   = best.conf;
            for (int k = 0; k < 19 && best.why[k]; k++) s.alertWhy[k] = best.why[k];
            for (int k = 0; k < ui::SSID_MAX; k++)      s.alertName[k] = best.name[k];
        }
    }

    // Age the alert so the card can animate. The clock restarts only when
    // the alert is actually a different one -- a device that keeps being
    // seen holds its card steady rather than re-striking every frame.
    {
        static uint8_t  lastThreat = 0;
        static char     lastName[ui::SSID_MAX] = {0};
        static uint32_t startedAt = 0;
        if (s.alertActive) {
            if (s.alertThreat != lastThreat || strncmp(s.alertName, lastName, sizeof lastName)) {
                startedAt  = millis();
                lastThreat = s.alertThreat;
                snprintf(lastName, sizeof lastName, "%s", s.alertName);

                // Wake the panel. Dim-on-idle suspends the whole UI, which
                // meant an alert raised while the screen was dark was drawn
                // nowhere and seen by nobody -- the feature silently undid
                // the one thing the device exists to do.
                //
                // Only a *new* alert wakes it: the same camera re-detected
                // every few seconds would otherwise hold the screen on for
                // as long as you were parked beside it.
                wakeScreen = true;
            }
            const uint32_t a = millis() - startedAt;
            s.alertAge = (uint16_t)(a > 60000 ? 60000 : a);
        } else {
            lastThreat = 0;
            lastName[0] = 0;
        }
    }

    if (s.alertActive && testStep < 0) {
        ui::AlertRec rec;
        rec.threat = s.alertThreat;
        rec.conf   = s.alertConf;
        for (int k = 0; k < ui::SSID_MAX; k++) rec.name[k] = s.alertName[k];
        const ui::Gnss::Fix& fx = ui::Gnss::fix();
        alertlog::push(rec, millis(), ui::Gnss::utcEpoch(millis()),
                       ui::Gnss::fresh(millis()), fx.lat7, fx.lon7);
    }

    s.set    = set;
    s.screen = nav.screen;
    s.filter = nav.filter;
    s.scroll = nav.scroll;
    if (nav.screen == ui::SCR_CODEX) {
        if (nav.filter == ui::F_BLE) blescan::fillSights(s, nav.scroll);
        else                         cap::fillSights(s, nav.scroll, nav.filter == ui::F_5G);
    } else if (nav.screen == ui::SCR_ECHOES) {
        cap::fillEchoes(s, nav.scroll, millis());
    } else if (nav.screen == ui::SCR_OMENS) {
        alertlog::fill(s, nav.scroll, millis());
        s.omenScroll = nav.scroll;
    } else if (nav.screen == ui::SCR_DOMAIN) {
        const ui::Gnss::Fix& fx = ui::Gnss::fix();
        cap::fillPlots(s, ui::RANGES[nav.rangeIdx % ui::RANGE_N],
                       fx.lat7, fx.lon7, ui::Gnss::fresh(millis()));
        if (ui::Gnss::fresh(millis())) {
            const float latDeg = (float)fx.lat7 / 1e7f;
            const float mLat = 111320.0f;
            const float mLon = 111320.0f * cosf(latDeg * 3.14159265f / 180.0f);
            const float pxPerM = (float)ui::MAP_R / (float)s.rangeM;
            int n = 0;
            for (int i = 0; i < gpstrack::count() && n < ui::MAX_TRACK; i++) {
                int32_t la, lo;
                if (!gpstrack::sample(i, la, lo)) continue;
                const int px = (int)lrintf(((float)(lo - fx.lon7) / 1e7f) * mLon * pxPerM);
                const int py = (int)lrintf(-((float)(la - fx.lat7) / 1e7f) * mLat * pxPerM);
                if (px * px + py * py > ui::MAP_R * ui::MAP_R) continue;
                s.track[n].dx = (int16_t)px;
                s.track[n].dy = (int16_t)py;
                n++;
            }
            s.nTrack = (uint8_t)n;
        }
    } else if (nav.screen == ui::SCR_DETAIL) {
        if (nav.filter == ui::F_BLE) blescan::fillDetail(s, nav.selected, millis());
        else cap::fillDetail(s, nav.selected, nav.filter == ui::F_5G, millis());
    }

    s.touchDown = tpDown;
    s.touchX    = tp.x;
    s.touchY    = tp.y;

    // Report every press once, with the raw values -- the mapping below is a
    // first guess from the board's calibration constants and needs a finger
    // to confirm the axis order and inversion.
    static bool wasDown = false;
    if (tp.down && !wasDown)
        Serial.printf("TOUCH raw x=%4u y=%4u z=%4u  ->  canvas %3d,%3d\n",
                      tp.rawX, tp.rawY, tp.z, tp.x, tp.y);
    wasDown = tp.down;

    const bool live = ui::Gnss::fresh(millis());
    s.gpsFix = live;
    s.sats   = live ? ui::Gnss::fix().used : 0;

    // Feed the position history that dates node reports.
    gpstrack::push(millis(), live, ui::Gnss::fix().lat7, ui::Gnss::fix().lon7);

    // Nothing to look at: skip draw and blit entirely and give the time to
    // the radio. The next wake forces a full repaint, because every screen's
    // dirty tracking has been standing still.
    if (screenOff) {
        static bool     said = false;
        static uint32_t lastLog = 0;
        if (!said) { said = true; Serial.println("screen: dark, UI suspended"); }

        // Yield. Without the draw and blit this loop runs about 5700 times a
        // second, which is 80x more often than anything here needs and
        // starves the WiFi task of scheduling. 5ms is still 200Hz of touch
        // polling -- far faster than a finger.
        delay(5);

        // Log on a clock, not a frame count: frame counts mean something
        // different on each side of this branch.
        if (millis() - lastLog >= 10000) {
            lastLog = millis();
            logf("[dark] wifi %4u  hits %u  deauth %u  heap %u\n",
                 (unsigned)cap::uniques(), (unsigned)cap::frames(),
                 (unsigned)cap::deauths(), (unsigned)ESP.getFreeHeap());
        }
        frame++;
        return;
    }
    { static bool said = false; (void)said; }

    // The palette and the dial geometry are the theme. Apply it before
    // anything draws, and repaint everything when it changes -- the dirty
    // regions of the old theme say nothing about the new one.
    //
    // This runs here, in the task that draws, and nowhere else: the palette
    // globals are plain uint16_t with no atomics, so setTheme must never be
    // called from a touch handler or another thread mid-draw. navTouch only
    // edits `set`; the change takes effect here, at the top of the next draw.
    static uint8_t lastTheme = 0xFF;
    bool themeChanged = false;
    if (lastTheme != s.set.theme) {
        pal::setTheme(s.set.theme);
        lastTheme = s.set.theme;
        themeChanged = true;
    }
    if      (nav.screen == ui::SCR_CODEX)  ui::drawCodex(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_DETAIL) ui::drawDetail(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_ECHOES) ui::drawEchoes(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_GRIMOIRE) ui::drawGrimoire(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_OMENS)  ui::drawOmens(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_DOMAIN) ui::drawDomain(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_MESH)   ui::drawMesh(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_ATTUNE) ui::drawAttune(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_ASPECT) ui::drawAspect(canvas, s, &dirty);
    else                                   ui::drawScan(canvas, s, &dirty);

    // A screen change invalidates every region the outgoing screen reported.
    // The incoming one has its own first-frame logic, but it has already run
    // for this frame, so force the repaint here.
    static bool justWoke = true;
    // Load-bearing coupling: every screen keeps its own `static State prev` /
    // `static bool first` for dirty tracking, and those mean nothing across a
    // theme change. That is safe only because the theme can change solely
    // while ASPECT is on screen, so the user always leaves via a screen
    // switch, and `switched` forces this full blit, which clears the other
    // screens' stale dirty state. A future navigation path that changes the
    // theme or leaves ASPECT without setting `switched` will show leftover
    // pixels from the old theme.
    if (switched || justWoke || themeChanged) {
        dirty.clear();
        dirty.add(0, 0, ui::W, ui::H);
        justWoke = false;
    }
    if (screenOff) justWoke = true;
    const uint32_t tDraw = millis();

    uint32_t pushed = 0;
    for (int i = 0; i < dirty.n; i++) {
        blitRect(canvas.px, dirty.r[i]);
        pushed += (uint32_t)dirty.r[i].w * dirty.r[i].h;
    }
    const uint32_t tBlit = millis();

    if ((frame % 30) == 0) {
        logf("f%-5u [%s] draw %2u blit %2u ms | wifi %4u/%-4u (2.4 %u, 5 %u) "
             "%u fr ch%-3d %s | ble %4u/%-6u | gps %s %2d sv | touch %s | "
             "alert %s %s | mesh %dn %ur %u%%lost | hits %u rem %u | heap %u\n",
             (unsigned)frame,
             nav.screen == ui::SCR_CODEX   ? "codex"
             : nav.screen == ui::SCR_DETAIL ? "detal"
             : nav.screen == ui::SCR_ECHOES ? "echos"
             : nav.screen == ui::SCR_GRIMOIRE ? "grim "
             : nav.screen == ui::SCR_OMENS  ? "omens"
             : nav.screen == ui::SCR_DOMAIN ? "domn "
             : nav.screen == ui::SCR_MESH   ? "mesh "
             : nav.screen == ui::SCR_ATTUNE ? "tune "
            : nav.screen == ui::SCR_ASPECT ? "aspct" : "scan ",
             (unsigned)(tDraw - t0), (unsigned)(tBlit - tDraw),
             (unsigned)cap::uniques(), (unsigned)cap::tableCap(),
             (unsigned)s.nets24, (unsigned)s.nets5,
             (unsigned)cap::frames(), (int)s.chan,
             s.on5 ? "5G " : "2.4",
             (unsigned)blescan::uniques(), (unsigned)blescan::adverts(),
             live ? "FIX" : "---", (int)s.sats,
             tp.down ? "DOWN" : "up  ",
             s.alertActive ? ui::threatName(s.alertThreat) : "-",
             s.alertActive ? ui::confName(s.alertConf) : "",
             meshcore::count(), (unsigned)meshcore::reportsRx(),
             (unsigned)meshcore::lossPct(),
             (unsigned)cap::frames(),
             (unsigned)(cap::frames() - cap::localFrames()),
             (unsigned)ESP.getFreeHeap());
    }
    frame++;
}
