// The Core's half of the mesh: hearing nodes, tracking them, assigning them
// channels.
#pragma once
#include <stdint.h>
#include "model.h"

namespace meshcore {

constexpr int MAX_NODES = 8;

struct NodeRec {
    uint8_t  mac[6];
    uint32_t lastMs;
    uint32_t hellos, reports, recs;
    // Link quality, from the sequence numbers the node stamps on each
    // report. Counting what arrived says nothing about what did not.
    uint32_t lost;
    uint8_t  lastSeq;
    bool     seqValid;
    uint8_t  nChan;
    uint8_t  chans[10];
    bool     camp;
    bool     assigned;
    bool     peered;
    bool     live;
};

void begin();
void tick(uint32_t nowMs);

// Publish node status into the State the screens draw.
void fill(ui::State& s, uint32_t nowMs);

int  count();                      // nodes heard from recently
const NodeRec* node(int i);
uint8_t  lossPct();      // across all live nodes
// Receive-path accounting: how many frames the callback saw, and how many
// each rejection branch discarded.
void rxStats(uint32_t& tot, uint32_t& magic, uint32_t& ver,
             uint32_t& len, uint32_t& noslot);
uint32_t hellosRx();
uint32_t reportsRx();

} // namespace meshcore
