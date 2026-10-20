// Screen selection and the gestures that drive it.
//
// Navigation is by tapping, not swiping: the tiles on UMBRAVOX are the
// buttons, and each opens the codex filtered to what that tile counts. A
// swipe is invisible until you already know it is there, and on a 2.8" panel
// a diagonal one fights the scroll.
//
// Lives in ui/ rather than device/ because it is interaction, not hardware.
// It takes a point and a button state, so the simulator can drive it with
// invented taps exactly as the panel drives it with real ones.
#pragma once
#include <stdint.h>
#include "model.h"

namespace ui {

enum Screen : uint8_t { SCR_SCAN = 0, SCR_CODEX, SCR_DETAIL, SCR_DOMAIN, SCR_MESH, SCR_OMENS, SCR_ECHOES, SCR_GRIMOIRE, SCR_ATTUNE, SCR_ASPECT, SCR_COUNT };
enum Filter : uint8_t { F_WIFI = 0, F_5G, F_BLE, F_COUNT };

struct Nav {
    uint8_t screen = SCR_SCAN;
    uint8_t filter = F_WIFI;
    int16_t scroll = 0;
    int16_t maxScroll = 0;      // set by the caller from the row count

    bool    tracking = false;
    bool    dragging = false;
    int16_t startX = 0, startY = 0;
    int16_t lastX = 0, lastY = 0;
    int16_t dragAccum = 0;      // leftover finger pixels, below one row
    uint16_t selected = 0;      // which codex row the detail screen is showing
    uint8_t  rangeIdx = 1;      // territory map scale

    // Set when the reset row is tapped; the caller clears it after acting.
    bool    resetRequested = false;

    // Set when the log control on the scan screen is tapped; the caller acts
    // on it (nav is pure UI and never touches the card) and clears it.
    bool    logToggleRequested = false;
};

// Feed one poll of the touch panel. Returns true if the screen changed, which
// the caller must treat as a full repaint -- the dirty regions of the screen
// being left behind say nothing about the one arriving.
// `set` is mutated directly when a settings control is tapped -- settings
// are UI state, and this is the UI's input handler.
bool navTouch(Nav& n, Settings& set, bool down, int16_t x, int16_t y);

// Force a Settings into range. Shared so that loading a stored blob and
// stepping a knob cannot disagree about what is valid.
void clampSettings(Settings& s);

// True when two Settings hold the same values.
//
// This lives beside the struct rather than in device/prefs.cpp, where the
// comparison used to be: prefs.cpp cannot be built on a host, and a
// comparator that forgets a field stops the save timer from ever firing --
// which looks exactly like "the setting does not persist" and nothing else.
bool settingsEqual(const Settings& a, const Settings& b);

} // namespace ui
