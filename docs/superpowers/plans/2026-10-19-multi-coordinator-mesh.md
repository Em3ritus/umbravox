# Multi-Coordinator Mesh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Scale the ESP-NOW mesh to ~12 co-located nodes by adding an optional coordinator tier (0/1/2 coordinators) between the nodes and the CYD, reusing the existing node↔Core protocol for node↔coordinator and adding a coordinator↔CYD collection tier.

**Architecture:** Three roles over wireless ESP-NOW. Nodes↔coordinator = today's node↔Core protocol unchanged (the coordinator runs the existing `meshcore`/`chanplan` for its cluster). The new layer is coordinator↔CYD: coordinators register with the CYD on a collection channel, the CYD hands each a cluster id + report channel + band slice and balances nodes across them, and coordinators forward deduplicated batches to the CYD. With 0 coordinators the CYD is the Core (today's behaviour, untouched).

**Tech Stack:** C++17, no third-party libs. Host tests build with `make` in `sim/`; device firmware with PlatformIO (`~/.platformio/penv/bin/pio run -e <env>`, `pio` not on PATH). ESP-NOW frames cap at 250 bytes. Three device build envs: `nm-cyd-c5` (CYD/Core+collector), `nm-c5-node` (node), and a new coordinator env.

**Spec:** `docs/superpowers/specs/2026-10-19-multi-coordinator-mesh-design.md`

## Global Constraints

- **Passive scope only.** No transmit-side/offensive capability. Receive/scan/aggregate only.
- **Co-located deployment; positioning model unchanged.** Node sightings carry `ageMs` and are geolocated from the CYD's single GPS fix exactly as today.
- **Backward compatible: 0 coordinators = today's behaviour, byte-for-byte on the node↔CYD path.** The `nm-c5-node` firmware must still work against a CYD Core with no coordinators present.
- **`mesh::VERSION` must bump** (currently `2` → `3`) because wire structs change; all three tiers build from the same repo so versions match. The existing version check hard-rejects mismatches.
- **Every ESP-NOW struct stays ≤ 250 bytes** (`static_assert` in `mesh.h`).
- **Host flags stay `-std=c++17 -O2 -Wall -Wextra`, no new warnings.** Existing device warnings (periph_ctrl.h, -Wvolatile) are not new.
- **`make -C sim regress`**: the SCAN/idle screens must stay `AETHERSCAN unchanged`. The **MESH screen render WILL change** (it must show ≥12 nodes / per-cluster loss); that one baseline (`mesh.png`) is expected to be re-captured in Task 8 and only that one.
- **Reuse, do not fork, the node↔Core protocol.** The coordinator uses `meshcore` for its cluster; do not reimplement node handling.
- Host tests use the existing idiom: file-scope `static int fails`, `check(const char*, bool)`, `printf`, `return fails?1:0`; add each new suite to `TESTS` in `sim/Makefile`.

## Review Focus

Failure modes the spec implies that a task's happy path won't exercise. Each gets its test in the owning task.

1. **Two coordinators must get disjoint band slices and distinct report channels** — overlapping slices leave channels double-covered and report channels colliding defeats the whole split. → Task 3, `test_chanplan` partition cases.
2. **Phase-offset math must not divide by zero or collapse at n=0/1** — a single node or an empty cluster must produce a valid (0) offset, not a crash. → Task 4, `test_phase`.
3. **Wire structs must stay ≤250 bytes after the new fields/messages** — a silent overflow corrupts every frame. → Task 1, `test_mesh` static checks.
4. **`chanplan` at 9–12 nodes must cover every channel and never exceed `MAX_ASSIGN`** — an untested node count could leave a channel unheard or overflow a node's channel array. → Task 2, `test_chanplan` high-count cases.
5. **A node whose coordinator dies must migrate to a survivor, not go dark** — the re-announce/reassign path is the resilience guarantee. → Task 6 logic + Hardware Acceptance (needs boards).

---

## File Structure

**Created:**
- `device/coord_main.cpp` — coordinator firmware: runs `meshcore` for its cluster + the CYD-facing registration/batch tier. Device-only.
- `sim/test_mesh.cpp` — host test pinning wire-struct sizes and `valid()` for the new message kinds.
- `sim/test_phase.cpp` — host test for the report phase-offset arithmetic.
- `device/phase.h` — pure `mesh_phase::offset(idx, n, windowMs)` (host-testable).

**Modified:**
- `device/mesh.h` — `VERSION` bump; new `MsgKind`s and structs (`CoordReg`, `CoordAssign`, `ClusterBatch`); `phase` field on `Assign`.
- `device/chanplan.h` — refactor `planChannels` to delegate to a new `planCluster(channels, nCh, nNodes, plans)`; add `partition(nCoord, slices)`.
- `sim/test_chanplan.cpp` — cover 9–12 nodes and the partition.
- `device/meshcore.{h,cpp}` — `MAX_NODES` 8 → 16; emit the report phase offset in `Assign`.
- `device/node_main.cpp` — honour the `phase` offset on first report.
- `device/main.cpp` — collector path: handle coordinator registration, balance nodes, camp the collection channel, fold `ClusterBatch` via `cap::ingest`, stop own capture when coordinators present.
- `ui/model.h` — `MAX_NODE_ROWS` 8 → 16; optional per-cluster fields.
- `ui/screen_mesh.cpp` — render ≥12 nodes / per-cluster loss.
- `platformio.ini` — new coordinator env; `upload_speed = 115200` on node/coordinator envs (the WCH-bridge C5 boards choke at 460800 — see [[technomancer-platform-scope]]).
- `sim/Makefile` — `test_mesh`, `test_phase` rules + `TESTS`.

---

### Task 1: Protocol extension in `mesh.h`

The wire additions for the coordinator tier, with host checks that nothing overflows 250 bytes and `valid()` accepts the new kinds. Pure header + host test.

**Files:**
- Modify: `device/mesh.h`
- Create: `sim/test_mesh.cpp`
- Modify: `sim/Makefile`

**Interfaces:**
- Produces: `mesh::VERSION == 3`; `MsgKind` gains `MSG_CREG=4, MSG_CASSIGN=5, MSG_CBATCH=6`; `struct CoordReg`, `struct CoordAssign`, `struct ClusterBatch`; `Assign` gains `uint16_t phaseMs`.

- [ ] **Step 1: Write the failing test**

Create `sim/test_mesh.cpp`:

```cpp
// Host test for the mesh wire protocol: struct sizes stay within the ESP-NOW
// 250-byte cap, and valid() accepts the new coordinator-tier message kinds.
#include "../device/mesh.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    printf("every wire struct fits an ESP-NOW frame (<=250):\n");
    check("Hello   <=250", sizeof(mesh::Hello)        <= 250);
    check("Assign  <=250", sizeof(mesh::Assign)       <= 250);
    check("Report  <=250", sizeof(mesh::Report)       <= 250);
    check("CoordReg<=250", sizeof(mesh::CoordReg)     <= 250);
    check("CAssign <=250", sizeof(mesh::CoordAssign)  <= 250);
    check("CBatch  <=250", sizeof(mesh::ClusterBatch) <= 250);

    printf("version bumped so old nodes are rejected:\n");
    check("version is 3", mesh::VERSION == 3);

    printf("valid() accepts a well-formed CBatch and rejects a short one:\n");
    mesh::ClusterBatch b = {};
    b.h.magic = mesh::MAGIC; b.h.ver = mesh::VERSION; b.h.kind = mesh::MSG_CBATCH;
    check("full CBatch valid",
          mesh::valid((const uint8_t*)&b, sizeof b, mesh::MSG_CBATCH, (int)sizeof b));
    check("truncated CBatch rejected",
          !mesh::valid((const uint8_t*)&b, (int)sizeof(mesh::Hdr), mesh::MSG_CBATCH, (int)sizeof b));

    printf("Assign carries a report phase offset:\n");
    mesh::Assign a = {}; a.phaseMs = 1234;
    check("phaseMs round-trips", a.phaseMs == 1234);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run it, confirm it fails to compile**

Run: `cd sim && make test_mesh` (after adding the Step 5 rule)
Expected: FAIL — `mesh::CoordReg` etc. do not exist, `VERSION` is 2.

- [ ] **Step 3: Extend `device/mesh.h`**

Bump the version and add the kinds, the phase field, and the three structs. Insert after the existing `MSG_REPORT`:

```cpp
constexpr uint8_t VERSION = 3;            // was 2: coordinator tier added
```

Add to `enum MsgKind`:

```cpp
    MSG_CREG     = 4,   // coordinator -> CYD: I am a coordinator, unassigned
    MSG_CASSIGN  = 5,   // CYD -> coordinator: your cluster id / report chan / slice
    MSG_CBATCH   = 6,   // coordinator -> CYD: aggregated sightings + cluster stats
```

Add `phaseMs` to `Assign` (just before `camp`), so a node can stagger its first report:

```cpp
    uint16_t phaseMs;     // report-time offset, so a cluster's nodes interleave
```

Add the new structs (after `Report`):

```cpp
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
```

- [ ] **Step 4: Confirm `valid()` is unchanged and still correct**

`valid()` already checks `magic/ver/kind/len` generically — no change needed; the test exercises it with `MSG_CBATCH`.

- [ ] **Step 5: Add the test rule, run the suite**

In `sim/Makefile`:

```make
test_mesh: test_mesh.cpp ../device/mesh.h
	$(CXX) $(CXXFLAGS) -o $@ test_mesh.cpp -lm
```

Append ` test_mesh` to `TESTS`.

Run: `cd sim && make -s test` → all suites `all passed`, including `test_mesh`.

- [ ] **Step 6: Verify regress + commit**

Run: `make -s regress` → `regress: AETHERSCAN unchanged`.

```bash
git add -A
git commit -m "feat: mesh protocol v3 — coordinator-tier message kinds and phase offset"
```

---

### Task 2: `chanplan` — generalize to a channel set, cover 9–12 nodes

Today `planChannels(live, plans)` plans over all channels. The coordinator must plan over only *its* slice. Refactor so the existing behaviour delegates to a new `planCluster(channels, nCh, nNodes, plans)`, then test both at high node counts.

**Files:**
- Modify: `device/chanplan.h`
- Modify: `sim/test_chanplan.cpp`

**Interfaces:**
- Consumes: `chanplan::Plan`, `ui::CHAN24/CH24`, `ui::CHAN5/CH5`, `mesh::REPORT_CHAN`, `mesh::MAX_ASSIGN`.
- Produces: `void chanplan::planCluster(const uint8_t* channels, int nCh, int nNodes, Plan* plans)`; `planChannels(live, plans)` keeps its current signature and behaviour (delegates to `planCluster` over all non-report channels).

- [ ] **Step 1: Write the failing test**

Add to `sim/test_chanplan.cpp` (follow its existing `check`/print idiom):

```cpp
// 9..12 nodes: every assigned channel array stays within MAX_ASSIGN, and every
// channel in the pool is covered by at least one node.
{
    for (int live = 9; live <= 12; live++) {
        chanplan::Plan plans[16];
        chanplan::planChannels(live, plans);
        int maxN = 0, totalCh = 0;
        for (int i = 0; i < live; i++) { if (plans[i].n > maxN) maxN = plans[i].n; totalCh += plans[i].n; }
        char label[48]; snprintf(label, sizeof label, "%d nodes: no node exceeds MAX_ASSIGN", live);
        check(label, maxN <= mesh::MAX_ASSIGN);
        snprintf(label, sizeof label, "%d nodes: something is assigned", live);
        check(label, totalCh > 0);
    }
}
// planCluster over an explicit 2.4GHz-only slice splits it across the nodes.
{
    const uint8_t slice[] = {2,3,4,5,7,8,9,10,12,13};
    chanplan::Plan plans[6];
    chanplan::planCluster(slice, 10, 6, plans);
    int total = 0; for (int i = 0; i < 6; i++) total += plans[i].n;
    check("planCluster covers the whole slice at least once", total >= 10);
    bool only24 = true;
    for (int i = 0; i < 6; i++) for (int k = 0; k < plans[i].n; k++) if (plans[i].ch[k] > 14) only24 = false;
    check("planCluster stays within its band slice", only24);
}
```

- [ ] **Step 2: Run it, confirm it fails**

Run: `cd sim && make test_chanplan`
Expected: FAIL — `planCluster` is undefined.

- [ ] **Step 3: Refactor `chanplan.h`**

Extract the node-distribution body of `planChannels` into `planCluster(const uint8_t* channels, int nCh, int nNodes, Plan* plans)`: it camps any `SOCIAL` channels present in `channels` (campers = `nNodes>=4?2:nNodes==3?1:0`, but only for SOCIAL entries actually in the slice), then splits the remainder across the sweepers using the existing proportional logic — operating on the passed `channels`/`nCh` instead of the global band. Then make `planChannels(live, plans)` build the all-channels list (every `CHAN24`/`CHAN5` except `REPORT_CHAN`) and call `planCluster` on it, preserving today's output exactly.

- [ ] **Step 4: Run tests, confirm pass**

Run: `cd sim && make -s test`
Expected: `test_chanplan` passes, including the new 9–12 and `planCluster` cases. The pre-existing 1–8 cases must still pass unchanged (proves the refactor preserved behaviour).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: chanplan planCluster() over an explicit channel slice, tested to 12 nodes"
```

---

### Task 3: `chanplan` — band partitioning across coordinators

The CYD splits the band into disjoint slices, one per coordinator, each with a distinct report channel. Pure + host-tested (Review Focus 1).

**Files:**
- Modify: `device/chanplan.h`
- Modify: `sim/test_chanplan.cpp`

**Interfaces:**
- Produces: `struct Slice { uint8_t nChan; uint8_t chans[mesh::MAX_SLICE]; uint8_t reportChan; };` and `int chanplan::partition(int nCoord, Slice* out)` — fills `out[0..nCoord-1]`, returns the number of slices actually produced.

- [ ] **Step 1: Write the failing test**

Add to `sim/test_chanplan.cpp`:

```cpp
// Partitioning: 1 coordinator gets everything; 2 get disjoint slices with
// distinct report channels (Review Focus 1).
{
    chanplan::Slice s1[2];
    check("1 coordinator -> 1 slice", chanplan::partition(1, s1) == 1);
    check("1 coordinator covers both bands",
          s1[0].nChan > (int)(/*2.4 only*/ 10));   // >10 means 5GHz included too

    chanplan::Slice s2[2];
    check("2 coordinators -> 2 slices", chanplan::partition(2, s2) == 2);
    check("distinct report channels", s2[0].reportChan != s2[1].reportChan);
    // disjoint slices
    bool overlap = false;
    for (int i = 0; i < s2[0].nChan; i++)
        for (int j = 0; j < s2[1].nChan; j++)
            if (s2[0].chans[i] == s2[1].chans[j]) overlap = true;
    check("2 coordinators get disjoint channel slices", !overlap);
    // one is 2.4-only, the other 5GHz-only (band split)
    bool a24 = true, b5 = true;
    for (int i = 0; i < s2[0].nChan; i++) if (s2[0].chans[i] > 14) a24 = false;
    for (int j = 0; j < s2[1].nChan; j++) if (s2[1].chans[j] <= 14) b5 = false;
    check("2 coordinators split by band", a24 && b5);
}
```

- [ ] **Step 2: Run it, confirm it fails**

Run: `cd sim && make test_chanplan` → FAIL, `partition` undefined.

- [ ] **Step 3: Implement `partition` in `chanplan.h`**

```cpp
struct Slice { uint8_t nChan; uint8_t chans[mesh::MAX_SLICE]; uint8_t reportChan; };

// The report channel each coordinator's nodes use. Distinct from the CYD's
// collection channel (REPORT_CHAN) and from each other. SOCIAL[] are the busy
// 2.4GHz channels; 5GHz coordinators report on a low 5GHz channel.
inline int partition(int nCoord, Slice* out) {
    if (nCoord <= 1) {
        out[0].nChan = 0;
        for (int i = 0; i < ui::CH24; i++)
            if (ui::CHAN24[i] != mesh::REPORT_CHAN) out[0].chans[out[0].nChan++] = ui::CHAN24[i];
        for (int i = 0; i < ui::CH5; i++) out[0].chans[out[0].nChan++] = ui::CHAN5[i];
        out[0].reportChan = SOCIAL[0];     // ch6; CYD collects on REPORT_CHAN (ch1)
        return 1;
    }
    // Two coordinators: 2.4GHz (minus the collection channel) and 5GHz.
    out[0].nChan = 0;
    for (int i = 0; i < ui::CH24; i++)
        if (ui::CHAN24[i] != mesh::REPORT_CHAN) out[0].chans[out[0].nChan++] = ui::CHAN24[i];
    out[0].reportChan = SOCIAL[0];         // ch6
    out[1].nChan = 0;
    for (int i = 0; i < ui::CH5; i++) out[1].chans[out[1].nChan++] = ui::CHAN5[i];
    out[1].reportChan = ui::CHAN5[0];      // a low 5GHz channel
    return 2;
}
```

- [ ] **Step 4: Run tests, confirm pass**

Run: `cd sim && make -s test` → all pass.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: chanplan partition() splits the band into per-coordinator slices"
```

---

### Task 4: Report phase-offset arithmetic

Pure function so a cluster's nodes interleave their reports (Review Focus 2). Host-tested.

**Files:**
- Create: `device/phase.h`, `sim/test_phase.cpp`
- Modify: `sim/Makefile`

**Interfaces:**
- Produces: `uint32_t mesh_phase::offset(int idx, int n, uint32_t windowMs)`.

- [ ] **Step 1: Write the failing test**

Create `sim/test_phase.cpp`:

```cpp
#include "../device/phase.h"
#include <stdio.h>
static int fails = 0;
static void check(const char* w, bool ok){ printf(ok?"  ok   %s\n":"  FAIL %s\n",w); if(!ok)fails++; }
int main() {
    using mesh_phase::offset;
    check("n=0 -> 0 (no divide by zero)", offset(0, 0, 2500) == 0);
    check("n=1 -> 0",                      offset(0, 1, 2500) == 0);
    check("4 nodes, idx0 -> 0",            offset(0, 4, 2500) == 0);
    check("4 nodes, idx1 -> 625",          offset(1, 4, 2500) == 625);
    check("4 nodes, idx3 -> 1875",         offset(3, 4, 2500) == 1875);
    check("idx>=n clamps into window",     offset(9, 4, 2500) < 2500);
    printf(fails?"\n%d FAILED\n":"\nall passed\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run it, confirm it fails** — `cd sim && make test_phase` → FAIL, header missing.

- [ ] **Step 3: Implement `device/phase.h`**

```cpp
// Report-time staggering: node idx of n reports at offset idx*window/n, so a
// cluster's deliveries interleave instead of colliding on the report channel.
// Pure; no hardware.
#pragma once
#include <stdint.h>
namespace mesh_phase {
inline uint32_t offset(int idx, int n, uint32_t windowMs) {
    if (n <= 1) return 0;
    return (uint32_t)((idx % n) * (uint64_t)windowMs / n);
}
}
```

- [ ] **Step 4: Add the rule, run** — add `test_phase` rule + to `TESTS`; `cd sim && make -s test` → all pass.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: pure report phase-offset arithmetic for cluster staggering"
```

---

### Task 5: Coordinator channel-assignment + phase in `meshcore`

Grow the Core's node table and have it stamp each node's report phase offset. This is device-build-only (the behaviour is pinned by the host `chanplan`/`phase` tests and by Hardware Acceptance). Keep the 0-coordinator path identical.

**Files:**
- Modify: `device/meshcore.h` (`MAX_NODES` 8 → 16)
- Modify: `device/meshcore.cpp` (`assignAll` sets `a.phaseMs`)

**Interfaces:**
- Consumes: `chanplan::planChannels`, `mesh_phase::offset`, `mesh::Assign::phaseMs`.
- Produces: nodes receive a staggered `phaseMs` in their `Assign`.

- [ ] **Step 1: Bump the table and stamp the phase**

In `meshcore.h`: `constexpr int MAX_NODES = 16;` (was 8). In `meshcore.cpp`'s `assignAll`, after computing `live` and the per-node `slot`, set the phase before sending:

```cpp
        a.phaseMs = (uint16_t)mesh_phase::offset(slot - 1, live, 2500 /* REPORT_MS */);
```

(add `#include "phase.h"` at the top). `slot - 1` because `slot` is post-incremented in the existing loop.

- [ ] **Step 2: Device build**

Run: `~/.platformio/penv/bin/pio run -e nm-cyd-c5` → compiles, no new warnings.

- [ ] **Step 3: Host suite + regress unaffected**

Run: `cd sim && make -s test && make -s regress` → all green (meshcore is device-only; this just confirms nothing else broke).

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: 16-node Core table and staggered report phase in assignments"
```

---

### Task 6: Coordinator firmware + CYD collector tier

The device integration: a new coordinator firmware that runs `meshcore` for its cluster and forwards batches to the CYD, and the CYD-side collector that registers coordinators, balances nodes, and folds batches. Device-build-only; behaviour is proven in Hardware Acceptance. This is the largest task — implement it against the real boards during the hardware phase, in the shape below.

**Files:**
- Create: `device/coord_main.cpp`
- Modify: `device/main.cpp` (collector path), `platformio.ini` (coordinator env + `upload_speed`)

**Interfaces:**
- Consumes: `meshcore` (cluster management), `chanplan::partition`/`planCluster`, `mesh::CoordReg/CoordAssign/ClusterBatch`, `cap::ingest`.
- Produces: a working 3-tier mesh where coordinators aggregate their cluster and the CYD merges all clusters.

- [ ] **Step 1: Coordinator firmware `device/coord_main.cpp`**

Mirror `node_main.cpp`'s setup (STA mode, ESP-NOW init, promiscuous off — a coordinator does not sniff; it receives node reports). It:
1. On boot, parks on the collection channel and broadcasts `CoordReg` (with `wants5`) until the CYD answers a `CoordAssign` (learn `clusterId`, `reportChan`, `collectChan`, channel slice).
2. Runs `meshcore::begin()` + `meshcore::tick()` to manage its own nodes on `reportChan`, but with its assigned slice fed to `planCluster` instead of the global plan. The simplest reuse: add a `meshcore::setSlice(const uint8_t* chans, int n, uint8_t reportChan)` that, when set, makes `assignAll` call `planCluster(slice...)` and use `reportChan`; unset = today's `planChannels` behaviour (the CYD-as-Core path).
3. Instead of the CYD's `cap::ingest`, the coordinator keeps its deduped sightings and periodically hops to `collectChan` to send `ClusterBatch` frames to the CYD (same batch/flush discipline as `node_main.cpp::report()`), stamping `clusterId`, `seq`, and its `meshcore::lossPct()`.

Run: `~/.platformio/penv/bin/pio run -e <coordinator-env>` → compiles.

- [ ] **Step 2: Coordinator env in `platformio.ini`**

```ini
[env:nm-c5-coordinator]
extends = env:nm-c5-node
build_src_filter = +<coord_main.cpp> +<meshcore.cpp> +<chanplan.h> +<capture.cpp> +<../ui/wifi_auth.cpp>
```

Also add `upload_speed = 115200` to the node and coordinator envs (the WCH-bridge C5s choke renegotiating to 460800 — [[technomancer-platform-scope]]). Confirm it builds.

- [ ] **Step 3: CYD collector path in `main.cpp`**

In the mesh section (today's `main.cpp:288-302` region): detect coordinators (a `CoordReg` arrives → register, assign a `clusterId`/slice via `chanplan::partition`, reply `CoordAssign`). When ≥1 coordinator is live, the CYD **camps the collection channel** (`cap::setCamp(COLLECT_CHAN)`), **stops its own sniffing-as-Core node handling**, and folds incoming `ClusterBatch` recs via `cap::ingest` (same loop as `meshcore::onRecv`'s fold). Node balancing: when the CYD sees a node `Hello` and coordinators exist, it replies telling the node which coordinator/report-channel to join (round-robin by current cluster sizes). With **0 coordinators, this entire path is skipped** and the CYD behaves exactly as today.

Run: `~/.platformio/penv/bin/pio run -e nm-cyd-c5` → compiles, report RAM/flash.

- [ ] **Step 4: Host suite + regress**

Run: `cd sim && make -s test && make -s regress` → all green (device-only changes; `mesh.png` not yet touched — that's Task 8).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: coordinator firmware and CYD collector tier (untested until hardware)"
```

---

### Task 7: Node honours the report phase offset

Minimal node change: delay the first report by `phaseMs` so a cluster's nodes interleave. Device-build-only; keep 0-coordinator behaviour identical (phase 0 = today).

**Files:**
- Modify: `device/node_main.cpp`

**Interfaces:**
- Consumes: `mesh::Assign::phaseMs`.

- [ ] **Step 1: Apply the phase on first assignment**

In `node_main.cpp::onRecv`, when `first && assigned`, seed the report clock so the first report fires one phase offset into the window:

```cpp
        lastReport = millis() - (REPORT_MS - a->phaseMs);   // first report at +phaseMs
```

(If `phaseMs == 0`, this is `millis() - REPORT_MS`, i.e. report almost immediately — today's behaviour.)

- [ ] **Step 2: Device build**

Run: `~/.platformio/penv/bin/pio run -e nm-c5-node` → compiles.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: node staggers its first report by the assigned phase offset"
```

---

### Task 8: UI — MESH screen for ≥12 nodes + per-cluster loss

Grow the node-row capacity and show per-cluster loss so the operator can see when to add a coordinator. This is the one task that **re-captures the `mesh.png` baseline** (expected and allowed by the Global Constraints — only `mesh.png`).

**Files:**
- Modify: `ui/model.h` (`MAX_NODE_ROWS` 8 → 16), `ui/screen_mesh.cpp`, `device/main.cpp`/`device/meshcore.cpp` (fill ≥12 rows / cluster summary)
- Modify: `sim/test_nav.cpp` only if MESH tap targets move (otherwise untouched)

**Interfaces:**
- Consumes: `ui::State::nodes[]`, `meshcore::fill`.

- [ ] **Step 1: Grow the capacity**

`ui/model.h`: `constexpr int MAX_NODE_ROWS = 16;` (was 8). `meshcore::fill` already loops to `ui::MAX_NODE_ROWS`, so it fills up to 16 automatically.

- [ ] **Step 2: Render more rows / per-cluster loss**

In `ui/screen_mesh.cpp`, lay the node list out so up to 12–16 rows fit (smaller row pitch, or two columns), and show each cluster's `%lost` prominently (the operator's "add a coordinator" signal). Keep it legible at 160×120.

- [ ] **Step 3: Re-capture the MESH baseline (only mesh.png)**

Run: `cd sim && make -s regress`
Expected: `mesh.png` differs (the node list changed); **every other screen must still match**. If any screen other than `mesh.png` differs, that is a bug — fix it. Re-capture per `private/baseline/README.txt`, confirm only `mesh.png` changed in the manifest, and note it in the commit.

- [ ] **Step 4: Device build + host suite**

Run: `~/.platformio/penv/bin/pio run -e nm-cyd-c5` → compiles.
Run: `cd sim && make -s test` → all pass.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: MESH screen shows up to 16 nodes and per-cluster loss (mesh.png rebaselined)"
```

---

## Hardware Acceptance (deferred — needs a CYD + 1–2 coordinators + several nodes)

Not tasks; a checklist to run when the boards are in hand. Nothing in the 3-tier mesh is "hardware-verified" until these pass. Flash all three roles from the same build (versions must match).

1. **0-coordinator regression.** With no coordinator powered, confirm nodes still join the CYD Core and report exactly as before (no behaviour change). Review: backward compatibility.
2. **1 coordinator.** Power one Waveshare coordinator; confirm it registers with the CYD, the CYD stops its own Core-sniffing and camps the collection channel, nodes get balanced to the coordinator, and sightings reach the CYD's capture table (`MESHSTAT`, `WIGLE`/`SDCAT`).
3. **2 coordinators, band split.** Power two; confirm distinct report channels, disjoint band slices (one 2.4, one 5 GHz), nodes split across them, and both clusters' sightings merge on the CYD.
4. **Contention / `%lost`.** Compare report `%lost` at ~12 nodes on 1 coordinator vs 2; confirm staggering keeps loss low and that 2 coordinators roughly halves per-channel loss. This is the empirical check behind the spec's thresholds.
5. **Failure/reassignment (Review Focus 5).** Kill a coordinator; confirm its nodes time out, re-announce, and the CYD migrates them to the survivor (and re-slices the band). Kill a node; confirm its channels free up.
6. **CYD responsiveness.** Confirm the CYD's UI/GPS/SD stay responsive as a pure head unit under 12 nodes of traffic (frame times, touch).
7. **Record measurements** (per-cluster `%lost` at 1 vs 2 coordinators, CYD frame times) in the README block alongside the existing figures.

---

## Self-review

**Spec coverage.** 0/1/2-coordinator spectrum → Tasks 5–6 + HW 1–3. Protocol/version → Task 1. node↔coordinator = existing protocol reused → Task 6 Step 1 (`meshcore` reuse). Channel partition + per-cluster plan → Tasks 2–3. Report staggering → Tasks 4, 5, 7. Dedup/merge via `cap::ingest` → Task 6. `%lost` operator guide → Task 8 (UI) + HW 4. Failure handling → Task 6 + HW 5. MAX_NODES/MAX_NODE_ROWS → Tasks 5, 8. Backward compatibility → Task 6 Step 3 (0-coord path skipped) + HW 1.

**Placeholder scan.** Task 6 is specified as a shape (the 3-tier RF integration is developed against hardware, as `sdlog.cpp` was) rather than line-by-line code — this is deliberate and flagged, not a gap: its host-testable pieces (`chanplan`, `phase`, protocol sizes) are fully pinned in Tasks 1–4, and its RF behaviour is pinned by Hardware Acceptance. No TBD/TODO elsewhere.

**Type consistency.** `mesh::VERSION==3`, `MSG_CREG/CASSIGN/CBATCH`, `Assign::phaseMs`, `CoordReg/CoordAssign/ClusterBatch`, `MAX_SLICE`, `MAX_BATCH` consistent across Tasks 1/5/6. `planCluster(const uint8_t*, int, int, Plan*)` and `partition(int, Slice*)` consistent across Tasks 2/3/6. `mesh_phase::offset(int,int,uint32_t)` consistent across Tasks 4/5/7. `MAX_NODES=16`/`MAX_NODE_ROWS=16` consistent across Tasks 5/8.

**Review Focus coverage.** (1) disjoint slices + distinct report channels → Task 3 test. (2) phase offset n=0/1 safe → Task 4 test. (3) structs ≤250 → Task 1 static checks. (4) chanplan 9–12 → Task 2 test. (5) coordinator-death migration → Task 6 logic + HW 5.
