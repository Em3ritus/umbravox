// The wire protocol between a Core and its Nodes.
//
// ESP-NOW: connectionless, no AP, no association, and it reaches any peer in
// range. The catch that shapes everything below is that it transmits and
// receives on whatever channel the radio is currently parked on -- so a Node
// sweeping channels and a Core sweeping channels would only ever hear each
// other by coincidence.
//
// The arrangement that falls out of that:
//
//   * One fixed REPORT channel, which both ends agree on.
//   * The Core parks there permanently in mesh mode. That is not a sacrifice:
//     channel 1 is one of the three channels that actually carry traffic, so
//     the Core becomes its full-time camper while also being reachable.
//   * Nodes cover everything else, and hop to the report channel briefly to
//     deliver a batch before resuming.
//
// Camping beats splitting. Dividing 21 channels across N radios still means
// every channel is sampled; parking a radio on channel 1 means every beacon
// on channel 1 is heard. The busy channels deserve a dedicated listener.
#pragma once
#include <stdint.h>

namespace mesh {

constexpr uint8_t MAGIC       = 0xAE;
// Bump this whenever anything in Hello/Assign/Report changes shape. A node
// and a Core that disagree about the wire will otherwise just stop talking:
// the length check rejects every frame and both sides look fine in
// isolation, which cost a debugging session to notice.
constexpr uint8_t VERSION     = 4;            // was 3: added the network id

// Which mesh a device belongs to. 0 is the open/stock network every stock build
// shares (a lone setup works zero-config). To isolate your fleet when other
// setups share the room, give it a non-zero id -- two ways, both landing here:
//   * baked in at build time with -DMESH_NETID=<1..255> (inherited by the node
//     and coordinator envs), or
//   * paired over the air: press ENROLL on the CYD and an OPEN (id 0) node or
//     coordinator in range adopts the CYD's id and saves it to NVS.
// A device stamps `netId` on every frame and ignores -- and is ignored by --
// any frame carrying a different id (MSG_ENROLL is the one exception: it is how
// an open device crosses onto a private net). It is runtime so NVS can override
// the build default at boot. Not a secret (readable in the binary): it isolates
// co-located setups, it does not authenticate them. Encryption is the roadmap.
#ifndef MESH_NETID
#define MESH_NETID 0
#endif
inline uint8_t netId = (uint8_t)(MESH_NETID);
constexpr uint8_t REPORT_CHAN = 1;
// How often a node (and a coordinator) delivers its batch, and so the window
// that report-phase offsets are spread across.
constexpr uint32_t REPORT_MS  = 2500;

enum MsgKind : uint8_t {
    MSG_HELLO  = 1,   // node -> core: I exist, I am unassigned
    MSG_ASSIGN = 2,   // core -> node: cover these channels
    MSG_REPORT = 3,   // node -> core: here is what I heard
    MSG_CREG     = 4,   // coordinator -> CYD: I am a coordinator, unassigned
    MSG_CASSIGN  = 5,   // CYD -> coordinator: your cluster id / report chan / slice
    MSG_CBATCH   = 6,   // coordinator -> CYD: aggregated sightings + cluster stats
    MSG_ENROLL   = 7,   // CYD -> anyone: adopt my netId (open devices only)
};

// Every frame opens with this, so a stray packet from anything else on the
// air is rejected on the first four bytes.
struct __attribute__((packed)) Hdr {
    uint8_t magic;
    uint8_t ver;
    uint8_t netId;        // which mesh this belongs to (see MESH_NETID above)
    uint8_t kind;
    uint8_t mac[6];       // the node the message concerns
};

struct __attribute__((packed)) Hello {
    Hdr     h;
    uint8_t fw;           // firmware generation, so a Core can refuse a stranger
    uint8_t wants5;       // 1 if this node's radio can do 5GHz
};

// An invitation to join a private network, broadcast by the CYD while its
// enroll window is open. h.netId carries the id to adopt. An OPEN device (its
// own netId is 0) that hears one adopts it and locks; an enrolled device
// ignores it, so a neighbour cannot steal a node that is already yours.
constexpr uint16_t ENROLL_TOKEN = 0x5541;   // a sanity marker, not security
struct __attribute__((packed)) Enroll {
    Hdr      h;
    uint16_t token;       // == ENROLL_TOKEN
    // 0 = an invite: an OPEN device adopts h.netId. 1 = an un-enrol command:
    // a device already on h.netId goes back to open. The CYD sends the un-enrol
    // on its OWN (old) net, so only its own gear -- not a neighbour's -- resets.
    uint8_t  reset;
};

constexpr int MAX_ASSIGN = 21;
struct __attribute__((packed)) Assign {
    Hdr     h;
    // The Core's own address, so the node can stop broadcasting its reports.
    // Broadcast ESP-NOW is unacknowledged and never retried; unicast gets a
    // link-layer ACK and the radio retries for you. Measured loss on
    // broadcast was 41%.
    uint8_t coreMac[6];
    uint8_t reportChan;
    uint8_t nChan;
    uint8_t chans[MAX_ASSIGN];   // 0 terminated if short
    uint16_t phaseMs;     // report-time offset, so a cluster's nodes interleave
    uint8_t camp;         // 1 = sit still on chans[0], 0 = sweep them
};
static_assert(sizeof(Assign) <= 250, "ESP-NOW frames cap at 250 bytes");

// One sighting as it crosses the air. 34 bytes, so seven fit in a frame
// alongside the header and still clear ESP-NOW's 250-byte limit.
struct __attribute__((packed)) Rec {
    uint8_t  bssid[6];
    int8_t   rssi;
    uint8_t  chan;
    uint8_t  band;
    uint8_t  ssidLen;
    char     ssid[20];
    // Milliseconds between hearing this and sending it. The Core has no
    // other way to know how stale a batched sighting is, and without it
    // every record in a batch would be stamped with the same position.
    uint16_t ageMs;
    uint16_t auth;    // ui::AuthBits, so node findings export like local ones
};

constexpr int MAX_RECS = 7;
struct __attribute__((packed)) Report {
    Hdr     h;
    uint8_t seq;
    uint8_t count;
    Rec     recs[MAX_RECS];
};

static_assert(sizeof(Report) <= 250, "ESP-NOW frames cap at 250 bytes");

// CYD <-> coordinator. A coordinator announces with CoordReg (wants5 lets the
// CYD give a 5GHz-capable coordinator the 5GHz slice); the CYD answers with a
// CoordAssign carrying the cluster id, the channels this coordinator's nodes
// cover, its report channel, and the collection channel it delivers on.
struct __attribute__((packed)) CoordReg {
    Hdr     h;
    uint8_t wants5;       // 1 if this coordinator's radio can do 5GHz
};

constexpr int MAX_SLICE = 21;
struct __attribute__((packed)) CoordAssign {
    Hdr     h;
    uint8_t clusterId;
    uint8_t reportChan;       // nodes in this cluster report here
    uint8_t collectChan;      // this coordinator delivers to the CYD here
    uint8_t nChan;
    uint8_t chans[MAX_SLICE]; // the band slice this coordinator plans across
};
static_assert(sizeof(CoordAssign) <= 250, "ESP-NOW frames cap at 250 bytes");

// Coordinator -> CYD: a batch of already-deduplicated sightings plus the
// cluster's honest loss, so the CYD can surface which cluster needs splitting.
// MAX_BATCH is 6 (not 7): ClusterBatch carries two stat bytes Report does not
// (clusterId, lossPct), and 7 recs would be 251 bytes -- one over the cap.
// 9(Hdr)+4(ids/seq/loss/count)+6*34(Rec) = 217 bytes.
constexpr int MAX_BATCH = 6;
struct __attribute__((packed)) ClusterBatch {
    Hdr     h;
    uint8_t clusterId;
    uint8_t seq;
    uint8_t lossPct;      // this cluster's current report loss
    uint8_t count;
    Rec     recs[MAX_BATCH];
};

static_assert(sizeof(ClusterBatch) <= 250, "ESP-NOW frames cap at 250 bytes");

// True if a received buffer is plausibly ours and the right length.
inline bool valid(const uint8_t* d, int len, uint8_t kind, int expect) {
    if (!d || len < (int)sizeof(Hdr) || len < expect) return false;
    const Hdr* h = (const Hdr*)d;
    return h->magic == MAGIC && h->ver == VERSION &&
           h->netId == netId && h->kind == kind;
}

} // namespace mesh
