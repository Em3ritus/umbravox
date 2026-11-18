// Alert repeats: which sightings raise the alert takeover again (spec: docs/superpowers/specs/2026-11-18-alert-repeat-design.md).
//
// A device keeps an alert active while it is re-detected (a hit under 6 s old). When it drops out and comes back,
// that is a new "episode". Fixed things (cameras, plate readers, body cams...) alert once and then stay quiet for
// the ALERT REPEAT setting. A tracker travelling with you (TH_FOLLOW) re-alerts at most every 10 min and a drone
// every 20 min, even while it never leaves; neither follows the setting. A higher confidence than the device last
// alerted at always shows. The table only decides what is SHOWN: the history and the logs still get every sighting.
//
// Pure and host-tested (sim/test_alert_repeat.cpp); no allocation, no statics. Time: signed differences only.
#pragma once
#include "signatures.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace alertrepeat {

constexpr uint32_t EPISODE_GAP_MS = 6000;     // the device's ALERT_MS: no hit for this long ends an episode
constexpr uint32_t FOLLOW_MS = 600000;        // a tracker travelling with you: at most every 10 min (blescan's clock)
constexpr uint32_t DRONE_MS = 1200000;        // a drone: at most every 20 min
constexpr int SLOTS = 16;

// Settings.alertRepeat: 0 1 min, 1 10 min, 2 1 h, 3 until reboot (UINT32_MAX never elapses). Unknown values: 10 min.
inline uint32_t repeatMs(uint16_t setting) {
    switch (setting) { case 0: return 60000; case 2: return 3600000; case 3: return UINT32_MAX; default: return 600000; }
}

// TH_FOLLOW has no cooldown here: blescan raises it only every FOLLOW_MS (followDue), so every new follow episode is a
// due re-alert. A second clock here would drift from blescan's (a dismiss or a late first sighting moves it) and
// silently swallow the next re-alert.
inline uint32_t cooldown(uint8_t threat, uint16_t setting) {
    return threat == ui::TH_FOLLOW ? 0 : threat == ui::TH_DRONE ? DRONE_MS : repeatMs(setting);
}

// show: the takeover is on for this sighting. episode: changes exactly when a new shown episode starts (the tone and
// the screen wake key on it). firstMs: when this device first alerted in its current stay. repeat: a shown episode
// that is not the device's first in this stay.
struct Verdict { bool show = false; uint32_t episode = 0; uint32_t firstMs = 0; bool repeat = false; };

inline bool followDue(uint32_t lastFollowAlertMs, uint32_t nowMs) { return (uint32_t)(nowMs - lastFollowAlertMs) >= FOLLOW_MS; }

// "STILL HERE 40 MIN" (drone) or "WITH YOU 50 MIN" (follow), minutes clamped to 999, always NUL-terminated within n.
inline void reason(char* out, int n, uint8_t threat, uint32_t minutes) {
    if (!out || n < 1) return;
    if (minutes > 999) minutes = 999;
    std::snprintf(out, (size_t)n, threat == ui::TH_FOLLOW ? "WITH YOU %u MIN" : "STILL HERE %u MIN", (unsigned)minutes);
}

// The main loop's candidates (newest first) with any TH_FOLLOW moved to the front: a follow alert lasts only 6 s and
// must not be masked by a newer, continuously refreshed alert from the other radio. Otherwise the order is unchanged.
inline void prioritise(ui::AlertRec* c, int n) {
    for (int i = 1; i < n; i++)
        if (c[i].threat == ui::TH_FOLLOW && c[0].threat != ui::TH_FOLLOW) { const ui::AlertRec t = c[i]; for (int k = i; k > 0; k--) c[k] = c[k - 1]; c[0] = t; return; }
}

// blescan's single alert slot: may a hit (threat, conf) overwrite what it holds? As before, a hit of equal or higher
// confidence, or any hit once the held alert is over 6 s old; and a follow alert keeps the slot for its 6 s against
// every other kind (the tracker's own next advert would otherwise wipe it within a second).
inline bool replaces(const ui::AlertRec& held, uint8_t threat, uint8_t conf, uint32_t nowMs) {
    if (threat == ui::TH_NONE) return false;
    const bool old = (uint32_t)(nowMs - held.atMs) > EPISODE_GAP_MS;
    if (held.threat == ui::TH_FOLLOW && threat != ui::TH_FOLLOW && !old) return false;
    return conf >= held.conf || old;
}

struct Table {
    struct Entry {
        bool used = false, byName = false, showing = false, repeat = false;
        uint8_t threat = 0, shownConf = 0;
        uint8_t mac[6] = {0};
        char name[ui::ALERT_NAME] = {0};
        uint32_t lastSeenMs = 0, lastShownMs = 0, firstMs = 0, episode = 0;
    };
    Entry e_[SLOTS];
    uint32_t episodes_ = 0;
    int cur_ = -1;      // the entry last returned with show = true (what a tap dismisses)

    void clear() { *this = Table(); }

    // Call once per loop for each active alert candidate.
    Verdict see(const ui::AlertRec& a, uint32_t nowMs, uint16_t repeatSetting) {
        int i = find_(a);
        if (i < 0) {
            i = victim_(nowMs);
            Entry& e = e_[i];
            e = Entry();
            e.used = true; e.threat = a.threat;
            e.byName = a.address == ui::AlertAddress::Missing;
            if (e.byName) { int k = 0; for (; k < (int)sizeof e.name - 1 && a.name[k]; k++) e.name[k] = a.name[k]; e.name[k] = 0; }
            else std::memcpy(e.mac, a.mac, 6);
            e.lastSeenMs = nowMs; e.firstMs = nowMs;
            start_(e, a.conf, nowMs, false);
            return verdict_(i);
        }
        Entry& e = e_[i];
        const uint32_t gap = nowMs - e.lastSeenMs;
        e.lastSeenMs = nowMs;
        const uint32_t cd = cooldown(a.threat, repeatSetting);
        const bool elapsed = cd != UINT32_MAX && (uint32_t)(nowMs - e.lastShownMs) >= cd;
        if ((int32_t)gap > (int32_t)EPISODE_GAP_MS) {                      // it left and came back: a new episode
            const bool fresh = cd != UINT32_MAX && cd != 0 && gap > cd;   // no cooldown (follow): never a fresh stay
            if (fresh) e.firstMs = nowMs;
            if (a.conf > e.shownConf || elapsed) start_(e, a.conf, nowMs, !fresh);
            else e.showing = false;
        } else if (a.conf > e.shownConf) {
            start_(e, a.conf, nowMs, true);                                 // stronger evidence mid-episode
        } else if (a.threat == ui::TH_DRONE && (uint32_t)(nowMs - e.lastShownMs) >= DRONE_MS) {
            start_(e, a.conf, nowMs, true);                                 // a drone that never leaves
        }
        return verdict_(i);
    }

    // The tap: the shown alert stays hidden for the rest of its episode, and its cooldown starts now.
    void dismissCurrent(uint32_t nowMs) {
        if (cur_ < 0 || !e_[cur_].used) return;
        e_[cur_].showing = false;
        e_[cur_].lastShownMs = nowMs;
    }

private:
    int find_(const ui::AlertRec& a) const {
        const bool byName = a.address == ui::AlertAddress::Missing;
        for (int i = 0; i < SLOTS; i++) {
            const Entry& e = e_[i];
            if (!e.used || e.threat != a.threat || e.byName != byName) continue;
            if (byName ? std::strncmp(e.name, a.name, sizeof e.name - 1) == 0 : std::memcmp(e.mac, a.mac, 6) == 0) return i;
        }
        return -1;
    }
    int victim_(uint32_t nowMs) const {
        int best = 0; uint32_t oldest = 0;
        for (int i = 0; i < SLOTS; i++) {
            if (!e_[i].used) return i;
            const uint32_t age = nowMs - e_[i].lastSeenMs;
            if (age >= oldest) { oldest = age; best = i; }
        }
        return best;
    }
    void start_(Entry& e, uint8_t conf, uint32_t nowMs, bool repeat) {
        e.showing = true; e.repeat = repeat; e.shownConf = conf; e.lastShownMs = nowMs; e.episode = ++episodes_;
    }
    Verdict verdict_(int i) {
        const Entry& e = e_[i];
        if (e.showing) cur_ = i;
        Verdict v; v.show = e.showing; v.episode = e.episode; v.firstMs = e.firstMs; v.repeat = e.repeat;
        return v;
    }
};

}  // namespace alertrepeat
