// Everything a screen is allowed to know about the world.
//
// The UI draws this struct and nothing else -- no WiFi handles, no SD handles,
// no globals. On the device a collector fills it once per frame; in the
// simulator main_sim.cpp fills it with invented numbers. That is the whole
// reason the UI can be developed on a PC: the seam is a plain struct.
#pragma once
#include <stdint.h>

namespace ui {

// Which radio saw it. The dial and the tiles share these colours, so the
// tiles double as the dial's legend and no separate key is needed.
enum Band : uint8_t { BAND_24 = 0, BAND_5 = 1, BAND_BLE = 2 };

struct Contact {
    uint16_t angle;  // 0..359, bearing on the dial
    uint8_t  dist;   // 0..100, from RSSI: 0 is on top of us
    uint8_t  band;   // Band
    uint8_t  age;    // 0 fresh .. 255 stale; drives the fade
};

constexpr int CH24 = 13;          // 2.4GHz channels 1..13
constexpr int CH5  = 8;           // the 5GHz subset we hop
constexpr int MAX_CONTACTS = 48;

// The channels, named rather than computed. 5GHz numbering is not a stride:
// UNII-1 is 36-48 and UNII-3 is 149-161, and the 52-64 block between them is
// DFS -- radar-detection channels we stay off deliberately.
inline constexpr uint8_t CHAN24[CH24] = {1,2,3,4,5,6,7,8,9,10,11,12,13};
inline constexpr uint8_t CHAN5 [CH5]  = {36,40,44,48,149,153,157,161};

// One row of the codex. SSIDs are truncated here rather than at draw time so
// the struct is a fixed size and State stays copyable.
constexpr int SSID_MAX   = 20;
constexpr int MAX_SIGHTS = 8;     // rows the codex can show at once

struct Sighting {
    char    ssid[SSID_MAX];       // "" for a hidden network
    uint8_t mac[6];
    int8_t  rssi;
    uint8_t band;
    uint8_t chan;
};

// How the dial searches the field. A sweep finds by bearing, a pulse by
// distance, and a candle not at all -- these are different instruments, not
// different decorations, so the id travels with the setting.
enum Sweep : uint8_t { SW_RADAR = 0, SW_GAZE, SW_PULSE, SW_STAR, SW_CANDLE, SW_COUNT };

// The adjustable things. Each of these is a knob we found a real reason for
// while building: the backlight because the panel is bright in the dark, and
// the other three because they are the capture trade-offs that were measured
// rather than guessed.
//
// Every field here must carry a default member initializer. The host test in
// sim/test_settings.cpp infers padding from the bytes the constructor leaves
// untouched, so a field without an initializer reads as padding and escapes
// the comparator check.
struct Settings {
    // Minimum confidence an alert must carry to interrupt. This is the knob
    // that makes signature matching usable: most evidence is weak, and an
    // alert that fires on every passing phone is an alert you learn to
    // ignore. CONF_OFF silences them entirely.
    uint8_t  alertMin = 1;    // Conf: 0 LOW, 1 MED, 2 HIGH, 3 OFF
    uint8_t  bright  = 80;    // 1..100 %
    uint16_t dwellMs = 220;   // 100..600, per channel
    uint8_t  bleDuty = 30;    // 0..100 % of the radio BLE may take
    uint8_t  bands   = 0;     // 0 both, 1 = 2.4 only, 2 = 5 only
    uint8_t  idleIdx = 2;     // index into IDLE_S; 0 = never dim
    // Appearance. Not a capture trade-off like the rest of this struct, but
    // it is stored the same way and clamped by the same function.
    uint8_t  theme   = 0;     // pal::ThemeId
    uint8_t  sweep   = 0;     // Sweep
    // Not a setting. It fills the byte the compiler would otherwise leave as
    // slack, so Settings has NO padding -- which is what lets the host test
    // treat "a byte the constructor never writes" as unambiguously a field
    // someone forgot to initialise. With slack present, such a field hides in
    // it: same sizeof, same padding count, every check green.
    //
    // If you need another setting, take this byte and the static_assert in
    // sim/test_settings.cpp will tell you to re-check the arithmetic.
    uint8_t  reserved = 0;
};

// Seconds of no touch before the backlight drops. 0 means never.
inline constexpr uint16_t IDLE_S[] = {0, 15, 30, 60, 120, 300};
constexpr int IDLE_N = (int)(sizeof(IDLE_S) / sizeof(IDLE_S[0]));

constexpr int SET_ROWS = 7;   // six knobs and a reset

// How many RSSI samples a device keeps. One per sighting rather than one
// per second: beacons arrive when the hop comes round, so a time-based
// series would be mostly gaps.
constexpr int HIST_N = 28;

// Everything known about one device, for the detail screen.
struct Detail {
    bool     valid = false;
    char     ssid[SSID_MAX] = {0};
    uint8_t  mac[6] = {0};
    int8_t   rssi = 0;
    uint8_t  band = 0, chan = 0;
    uint8_t  threat = 0, conf = 0;
    uint16_t auth = 0;        // ui::AuthBits
    char     why[20] = {0};
    uint32_t firstAgoS = 0, lastAgoS = 0;
    int8_t   hist[HIST_N] = {0};
    uint8_t  histN = 0;      // samples written, capped at HIST_N
};

// One row of the mesh screen. The UI never sees a meshcore::NodeRec --
// device types stop at the State boundary, same as everywhere else.
constexpr int MAX_NODE_ROWS = 16;
struct NodeRow {
    bool     live = false;
    uint8_t  id[3] = {0};     // last three octets, which is how nodes are named
    uint8_t  nChan = 0;
    uint8_t  chan0 = 0;
    bool     camp = false;
    uint32_t recs = 0;        // sightings relayed
    uint16_t found = 0;       // devices this one discovered first
    uint8_t  lossPct = 0;     // reports that never arrived
    uint32_t ageS = 0;        // since last heard from
};

// A point on the territory map, already reduced to canvas offsets from the
// centre. The trig lives on the device side so the screen stays arithmetic.
constexpr int MAX_PLOT  = 64;
constexpr int MAX_TRACK = 24;
struct Plot {
    int16_t dx, dy;       // pixels from centre; +y is south
    uint8_t band;
    uint8_t threat;
};

// One remembered alert. Position is kept because "a Flock camera fired at
// 19:42" is much less useful than knowing where you were when it did.
constexpr int MAX_OMEN_ROWS = 6;
struct Omen {
    uint8_t  threat = 0, conf = 0;
    char     name[SSID_MAX] = {0};
    uint32_t epoch = 0;        // UTC, 0 if there was no fix
    uint32_t agoS = 0;
    bool     located = false;
};

// A probe request: a client asking after a network by name. Directed probes
// are a record of where a device has previously connected, which is a
// different kind of finding from "an access point is here".
constexpr int MAX_ECHO_ROWS = 6;
struct Echo {
    char    ssid[SSID_MAX] = {0};
    uint8_t mac[6] = {0};
    int8_t  rssi = 0;
    uint8_t chan = 0;
    uint32_t agoS = 0;
    bool    randomised = false;   // locally-administered address
};

struct State {
    // counters
    uint16_t nets24 = 0, nets5 = 0, ble = 0;

    // sweep / capture
    uint8_t  chan = 1;            // channel number as labelled, not an index
    bool     on5 = false;         // currently parked in the 5GHz band
    uint8_t  load24[CH24] = {0};  // 0..100 activity per channel
    uint8_t  load5[CH5]  = {0};

    // fix
    bool     gpsFix = false;
    uint8_t  sats = 0;

    // storage
    bool     sdOk = false;
    char     logName[12] = {0};   // 8.3-ish, as it lands on the card
    uint8_t  logState = 0;        // 0 idle, 1 active, 2 no-card
    uint32_t logSecs = 0;         // elapsed seconds of the active session

    // chrome
    uint32_t uptimeS = 0;

    Contact  contacts[MAX_CONTACTS];
    uint8_t  nContacts = 0;

    // codex
    Sighting sights[MAX_SIGHTS];
    uint8_t  nSights = 0;
    uint16_t totalSights = 0;     // everything known, for the scrollbar
    int16_t  scroll = 0;          // first row shown
    uint8_t  screen = 0;          // which screen is up; see nav.h
    uint8_t  filter = 0;          // which band the codex is showing
    Settings set;
    Detail   detail;

    // omens: the alert record
    Omen     omens[MAX_OMEN_ROWS];
    uint8_t  nOmens = 0;
    uint16_t omenTotal = 0;
    int16_t  omenScroll = 0;

    // echoes: probe requests
    Echo     echoes[MAX_ECHO_ROWS];
    uint8_t  nEchoes = 0;
    uint16_t echoTotal = 0;
    uint16_t echoClients = 0;

    // territory
    Plot     plots[MAX_PLOT];
    uint8_t  nPlots = 0;
    Plot     track[MAX_TRACK];
    uint8_t  nTrack = 0;
    uint16_t plotTotal = 0;    // located APs that exist
    uint16_t plotBeyond = 0;   // ...of which fall outside the range
    uint16_t rangeM = 250;

    // mesh
    NodeRow  nodes[MAX_NODE_ROWS];
    uint8_t  nNodes = 0;
    bool     coreCamping = false;
    uint8_t  coreChan = 0;
    uint32_t coreFrames = 0;   // this radio only
    uint32_t meshHits = 0;     // every sighting, local and relayed
    uint8_t  meshLossPct = 0;  // reports lost, across every live node
    uint16_t coreFound = 0;    // devices this radio discovered first

    // The live alert, if any. `alertAtMs` is when it was last seen; the
    // screen fades it out rather than latching, so a card on the glass means
    // the thing is still in range.
    uint8_t  alertThreat = 0;
    uint8_t  alertConf = 0;
    char     alertWhy[20] = {0};
    char     alertName[SSID_MAX] = {0};
    bool     alertActive = false;
    uint16_t alertAge = 0;        // ms since this alert first appeared

    // Where the finger is, in canvas coordinates. -1 when up.
    int16_t  touchX = -1, touchY = -1;
    bool     touchDown = false;

    uint32_t frame = 0;           // animation clock; drives the sweep
};

} // namespace ui
