#pragma once
// Pure decisions behind collector-mode node migration, lifted out so they can
// be host-tested without a radio (same pattern as chanplan.h / balance.h).
#include <stdint.h>
#include <string.h>

namespace rehome {

// What a node should do with the Core named in an incoming Assign.
//   BIND   first Core, from an unassigned node
//   UPDATE same Core, refreshed channels/phase
//   REHOME handed off to a different Core (CYD -> coordinator) -- re-peer
//   IGNORE a different Core named by someone other than our current Core: do
//          nothing (see the security note below)
enum Action : uint8_t { BIND, REHOME, UPDATE, IGNORE };

// A node with no Core binds to the first one it is told about. Once it has a
// Core, an Assign naming a DIFFERENT Core means it has been handed off (the
// collector moving it from the CYD onto a coordinator), so it must re-peer; the
// same Core is just a refreshed assignment. Fixing the REHOME case is the whole
// bug: the node used to latch its Core on the first Assign and ignore every
// later one, so the collector's redirect never took.
//
// Security: a hand-off is only honoured when the frame comes from the node's
// CURRENT Core (sender == cur) -- the one it already trusts. The legitimate
// redirect is issued BY the CYD (the node's Core while it is stranded) naming a
// coordinator as the new Core, so this passes. A different Core named by any
// other sender is IGNORED, so a stray or hostile same-protocol device in range
// cannot yank an assigned node off onto an arbitrary Core just by naming one.
// The mesh is still unauthenticated by design (an attacker who spoofs the
// Core's MAC defeats this; encryption is the documented roadmap fix), but this
// closes the opportunistic re-bind the re-home path would otherwise open.
inline Action decide(bool haveCore, const uint8_t* cur, const uint8_t* next,
                     const uint8_t* sender) {
    if (!haveCore) return BIND;
    if (memcmp(cur, next, 6) == 0) return UPDATE;
    if (sender && memcmp(cur, sender, 6) == 0) return REHOME;
    return IGNORE;
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
