#pragma once
// Pure decisions behind collector-mode node migration, lifted out so they can
// be host-tested without a radio (same pattern as chanplan.h / balance.h).
#include <stdint.h>
#include <string.h>

namespace rehome {

// What a node should do with the Core named in an incoming Assign.
enum Action : uint8_t { BIND, REHOME, UPDATE };

// A node with no Core binds to the first one it is told about. Once it has a
// Core, an Assign naming a DIFFERENT Core means it has been handed off (the
// collector moving it from the CYD onto a coordinator), so it must re-peer; the
// same Core is just a refreshed assignment (new channels or phase). Fixing the
// REHOME case is the whole bug: the node used to latch its Core on the first
// Assign and ignore every later one, so the collector's redirect never took.
inline Action decide(bool haveCore, const uint8_t* cur, const uint8_t* next) {
    if (!haveCore) return BIND;
    return memcmp(cur, next, 6) == 0 ? UPDATE : REHOME;
}

} // namespace rehome

namespace straggler {

// In collector mode, a node still reporting straight to the CYD has not been
// moved onto a coordinator yet (it bound to the CYD before any coordinator
// existed, or across a CYD reboot). Redirect it -- but no more than once per
// cooldown, so a chatty node cannot trigger a redirect storm while it converges
// over the next report cycle.
inline bool shouldRedirect(bool collectorActive, uint32_t now,
                           uint32_t lastRedirectMs, uint32_t cooldownMs) {
    if (!collectorActive) return false;
    return (now - lastRedirectMs) >= cooldownMs;
}

} // namespace straggler
