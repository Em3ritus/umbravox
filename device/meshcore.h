// The Core's half of the mesh: hearing nodes, tracking them, assigning them
// channels.
#pragma once
#include <stdint.h>
#include "model.h"

namespace meshcore {

constexpr int MAX_NODES = 16;

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

// Restrict channel planning to a band slice and report on `reportChan`, which
// is what a coordinator does with the slice the CYD hands it: assignAll then
// plans over the slice (planCluster) instead of the whole band. UNSET (the
// default, and the only state on the CYD-as-Core) plans the whole band on
// mesh::REPORT_CHAN -- today's behaviour, byte for byte. n<=0 clears it.
void setSlice(const uint8_t* chans, int n, uint8_t reportChan);

// Where a node report's decoded sightings are folded. The CYD leaves this
// unset and the Core folds into cap::ingest, exactly as today. A coordinator
// installs its own sink so it keeps the sightings for a ClusterBatch to the
// CYD instead of displaying them -- "instead of the CYD's cap::ingest".
typedef void (*Sink)(const uint8_t* bssid, int8_t rssi, uint8_t chan,
                     uint8_t band, const char* ssid, uint32_t ageMs,
                     uint8_t src, uint16_t auth);
void setSink(Sink sink);

// Frames meshcore does not itself own -- the coordinator tier's CoordReg,
// CoordAssign and ClusterBatch -- are offered to a hook before node handling.
// It is called from the ESP-NOW receive context with the sender's address and
// the raw frame; returning true means the hook claimed it and meshcore leaves
// it alone, returning false lets the node path run unchanged. Unset (the
// default, and the only state on a node build or a zero-coordinator CYD's
// node path) keeps today's behaviour byte for byte.
typedef bool (*FrameHook)(const uint8_t* src, const uint8_t* data, int len);
void setCollector(FrameHook hook);

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
