// Node liveness and the Core's camp decision, pure so they can be host-tested
// (sim/test_meshlive.cpp).
//
// Field logs (Task 1 report, section 3): a node with nothing queued sends
// nothing at all, and the Core expired it after 20 s -- 21% of 10 s rows in
// quiet areas lost a node. When the last one went, the Core went back to
// sweeping, sat on the report channel ~5% of the time, and the nodes' reports
// stopped landing: recovery took 30-310 s. Receive-only fixes (no keepalive:
// that would add transmissions):
//  - a node is gone after 75 s of silence, not 20 s;
//  - the Core keeps camping the report channel for 120 s after the last node.
#pragma once
#include <stdint.h>

namespace meshlive {

constexpr uint32_t NODE_TIMEOUT_MS = 75000;    // silent this long => gone
constexpr uint32_t CAMP_HOLD_MS    = 120000;   // camp on after the last node
// "Still answering" for an enrol reset (enrollwin.h's boardsHeard): heard in
// the last 20 s, the old node timeout, so the longer table timeout does not
// stretch a reset by up to 55 s.
constexpr uint32_t BOARD_QUIET_MS  = 20000;

// Silent for more than `timeoutMs`. Signed, so a lastMs stamped by the
// receive callback just after the loop sampled `nowMs` reads as fresh rather
// than wrapping to ~49 days old; and right across the millis() wrap.
inline bool expired(uint32_t nowMs, uint32_t lastMs, uint32_t timeoutMs) {
    return (int32_t)(nowMs - lastMs) > (int32_t)timeoutMs;
}

// How long since `lastMs`, for display: signed, clamped at 0, so a report
// stamped just after the loop read nowMs shows 0 s, not ~49 days.
inline uint32_t ageMs(uint32_t nowMs, uint32_t lastMs) {
    const int32_t d = (int32_t)(nowMs - lastMs);
    return d > 0 ? (uint32_t)d : 0;
}

struct CampHold {
    bool     held        = false;   // nodes were here, the hold is running
    uint32_t lastNodesMs = 0;       // last time any node was live
};

// Camp the report channel? Exactly the old rule (nodes in the table, a live
// coordinator, or an open enrol window) plus the hold: CAMP_HOLD_MS after the
// last node was seen. There is no hold at boot before any node, and a spent
// hold is cleared so it cannot come back when millis() wraps.
inline bool wantCamp(CampHold& h, int nodes, bool coordLive, bool enrolOpen,
                     uint32_t nowMs) {
    if (nodes > 0) { h.held = true; h.lastNodesMs = nowMs; }
    if (h.held && nowMs - h.lastNodesMs >= CAMP_HOLD_MS) h.held = false;
    return nodes > 0 || coordLive || enrolOpen || h.held;
}

} // namespace meshlive
