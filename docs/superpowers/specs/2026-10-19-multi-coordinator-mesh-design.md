# Multi-Coordinator Mesh — Design

**Date:** 2026-10-19
**Status:** Design (awaiting review → implementation plan)
**Scope:** Passive wardriving only. Receive/scan/aggregate side. No transmit-side tooling. See [[technomancer-platform-scope]].

## Intent

Scale the UMBRAVOX ESP-NOW mesh from its current ~8-node ceiling to **~12 co-located scanning nodes** reporting to one CYD, by inserting an optional **coordinator tier** (a Waveshare C5) between the nodes and the CYD. The design must support **0, 1, or 2 coordinators** from the same mechanism, so an operator can run today's flat setup, offload to one coordinator, or split 12 nodes 6/6 across two coordinators.

**Agreed context (from brainstorming):**
- **Co-located.** All nodes + coordinators + the CYD move together in one rig. They largely hear the *same* APs, and the CYD's single GPS fix is correct for every sighting. **The positioning model is unchanged** — this is purely a scaling + coordination change, not a per-node-location change.
- **Goal of many radios = depth, not discovery.** Co-located radios saturate AP *discovery* by ~4–5 radios; the payoff of 12 is camping more channels (a full-time listener hears every beacon on a channel vs ~1-in-N while sweeping) and headroom.
- **Transport: wireless ESP-NOW, 2-tier.** No inter-board wiring; boards share only power. Coordinators absorb node contention on their own report channels and deliver aggregated batches to the CYD on a collection channel.
- **Waveshare C5 = coordinator** (the more capable/available board).

## Goals

1. Support 1–12 co-located nodes reliably, with report loss staying low (operator target: a report channel's `%lost` under ~10–15%).
2. One **coordinator firmware role** that runs the existing Core aggregation logic for a subset of nodes, deployable as 0/1/2 instances.
3. Backward compatibility: **0 coordinators = today's behaviour**, unchanged.
4. Keep the channel-planning, band-partitioning, and merge logic **host-testable** (pure), as the current `chanplan` already is.
5. Report-time **staggering** built in from the start (the cheap contention win).

## Non-goals

- Distributed (non-co-located) nodes with per-node GPS — explicitly out of scope; flagged for a future design if the rig ever spreads out.
- Wired coordinator↔CYD transport — considered and set aside; wireless chosen. May return if link robustness ever demands it.
- Any transmit-side / offensive capability.
- Scaling past ~12 nodes / 2 coordinators now. The design should not *preclude* a 3rd coordinator, but 3+ is not a target.

## Architecture

Three roles. The **node↔coordinator link is exactly today's node↔Core protocol** — this is the single most important design decision, because it means the node firmware barely changes and the coordinator runs the existing `meshcore` + `chanplan` code for its own cluster. The genuinely new work is the **coordinator↔CYD tier**.

### The 0/1/2-coordinator spectrum

The coordinator runs the same aggregation logic the CYD runs today. So the topology degrades gracefully:

| Coordinators | Who is "Core" | Nodes | Notes |
|---|---|---|---|
| **0** | CYD itself (today) | ~1–5 | Unchanged. No new hardware. |
| **1** | one Waveshare | ~5–10 | CYD becomes a collector / head unit; the coordinator does RF aggregation. |
| **2** | two Waveshares | ~10–12+ | Nodes split across two report channels; each coordinator owns one band. |

Thresholds are starting points, not laws — see **Capacity & the `%lost` guide**.

### Roles

- **CYD — head unit + brain + collector.** Owns the UI, touch, GPS, SD logging, and the final merged capture table. With coordinators present it **stops sniffing** and instead: (a) during formation, assigns coordinators their channel slice and balances nodes across them; (b) in steady state, camps a **collection channel** and folds each coordinator's aggregated batches into its table via the existing `cap::ingest`. Losing the CYD's one capture radio is negligible against 12 node radios.
- **Coordinator (Waveshare C5) — cluster manager.** Runs today's Core logic (`meshcore` + `chanplan`) for *its* subset of nodes: assigns them channels, camps *its* report channel to hear them, dedups their sightings into a local table, and periodically hops to the collection channel to deliver an aggregated batch to the CYD. One coordinator → all nodes on one report channel; two → each ~6 nodes on a different report channel.
- **Node (Xiao or Waveshare C5) — sniffer.** Essentially unchanged: sniffs assigned channels, hops to a report channel to deliver a batch of `mesh::Rec`. It just reports to a coordinator instead of the CYD.

## Topology formation

The CYD is the single brain for formation; all discovery happens on the **collection channel** — a fixed, agreed channel the CYD camps (ch1 is the natural pick: it is reliable and lets the CYD also overhear the busiest channel). **Invariant: the collection channel is distinct from every coordinator's report channel**, so coordinator→CYD deliveries never collide with node→coordinator reports.

1. **Coordinator registration.** A coordinator boots, announces itself on the collection channel. The CYD replies with: a **cluster ID**, a **report channel** for its nodes (distinct from the collection channel and from the other coordinator's report channel), a **band/channel slice** to cover, and the collection channel. Example with collection = ch1: coord-0 → 2.4 GHz slice, report ch6; coord-1 → 5 GHz slice, report on a 5 GHz channel.
2. **Node attachment & balancing.** A node boots and announces on the collection channel. The CYD, knowing every coordinator's current load, **balances nodes round-robin** across coordinators and tells the node which coordinator (cluster ID + report channel) to join. The node then switches to that report channel and behaves exactly as today.
3. **Steady state.** Nodes ↔ their coordinator on its report channel (so the 12-sender contention is physically split across the coordinator radios); each coordinator dedups its cluster and hops to the collection channel to hand the CYD aggregated batches. The CYD merges by MAC (dedup already exists) and logs to SD with the GPS position.

With **0 coordinators**, the CYD *is* the coordinator: nodes attach to it directly on the report channel — today's flow, untouched.

## Channel partitioning

- The CYD splits the usable band across the live coordinators and gives each a disjoint slice. Natural split at 2 coordinators: **one owns 2.4 GHz, the other 5 GHz** — this also groups each node's channels within one band, avoiding the per-wrap band-change cost `chanplan` already documents.
- Within its slice, each coordinator runs the **existing `chanplan`** for its own node count (camp the busy channels first once it has ≥3 radios, sweep the rest). `chanplan` currently covers 1–8 nodes and is tested only to 8; it must be **extended and re-tested to at least 12** (per cluster this is usually ≤6, but the single-coordinator case needs all 12).
- Partitioning logic (how the band divides across N coordinators) is new, pure, and host-testable alongside `chanplan`.

## Report staggering

Co-located nodes hear the same APs, so after dedup each report is small — contention is dominated by report-*timing* collisions, not bulk data. The cheap, essential mitigation:

- When a coordinator (or the CYD at 0 coordinators) assigns a node, it also hands the node a **report phase offset** spread evenly across the `REPORT_MS` (2500 ms) window — e.g. node *k* of *n* reports at offset `k · REPORT_MS / n`. Nodes keep their current `REPORT_MS` cadence but start out of phase, so deliveries interleave instead of colliding.
- Coordinators similarly stagger their collection-channel deliveries to the CYD.
- Staggering is pure arithmetic → host-testable.

## Dedup / merge

- **Within a cluster:** the coordinator folds its nodes' `mesh::Rec` sightings into a local table via `cap::ingest` (the existing dedup-by-MAC path that already serves local and relayed sightings).
- **Across clusters:** disjoint channel slices mean two coordinators rarely hear the same AP, so cross-cluster overlap is minimal. The CYD still merges every coordinator batch through `cap::ingest`, which dedups by MAC — so an AP heard by two clusters collapses correctly regardless.
- Sightings carry their `ageMs` (already in `mesh::Rec`) so the CYD back-dates each to the right GPS position, exactly as today.

## Capacity & the `%lost` guide

Capacity is bounded by (1) CYD CPU — only in the 0-coordinator case — and (2) report contention on one report channel. UMBRAVOX already reports **`%lost` per node/cluster** honestly (from report sequence gaps). This is the operator's instrument:

- **Rule:** if a report channel's `%lost` climbs past ~10–15%, add or split a coordinator.
- Starting points: 0 coordinators ≈ 1–5 nodes; 1 coordinator ≈ 5–10; 2 coordinators ≈ 10–12+.
- Staggering and small post-dedup batches extend each tier's reach; RF environment moves the numbers, so measure rather than trust the table.

The UI must surface per-cluster `%lost` so the operator can see when to add a coordinator. The MESH screen (`MAX_NODE_ROWS = 8`) must be reworked to show ≥12 nodes grouped by coordinator, or summarised per cluster.

## Failure handling

- **Coordinator dies:** its nodes stop receiving assignment refreshes, time out (`NODE_TIMEOUT_MS`, 20 s today), and re-announce on the collection channel; the CYD reassigns them to a surviving coordinator and re-slices the band. The CYD drops the dead coordinator after its own timeout.
- **Node dies:** its channels free up; its coordinator re-plans the cluster (existing `assignAll` behaviour on shape change).
- **No coordinators:** CYD runs as Core (today).

## Protocol changes

- **`mesh::VERSION` bump** (currently 2) — any struct or flow change on the wire requires it, and the version check already hard-rejects mismatches, so all three tiers must be flashed from the same build.
- New message kinds for the coordinator↔CYD tier: **coordinator-register / coordinator-assign** (CYD ↔ coordinator) and **cluster-batch** (coordinator → CYD, carrying aggregated `mesh::Rec`s plus the cluster's `%lost`/stats). The existing `MSG_HELLO`/`MSG_ASSIGN`/`MSG_REPORT` are reused unchanged for node↔coordinator.
- `mesh::Assign` gains a **report phase offset** field for staggering, and `Hello` already carries `wants5` (5 GHz capability) which the band-split uses.
- Frames stay within the 250-byte ESP-NOW cap (the `static_assert` in `mesh.h` guards this).

## Code impact

- **New:** a coordinator firmware target (`[env:*-coordinator]`) running `meshcore` + `chanplan` for its cluster plus the new CYD-facing tier. Likely a new `device/coord_main.cpp` mirroring `node_main.cpp`'s structure.
- **`chanplan`** — extend + re-test to ≥12 nodes; add pure band-partitioning across N coordinators.
- **`meshcore` / `mesh.h`** — new message kinds, version bump, phase-offset field; `MAX_NODES` 8 → ≥12 (per coordinator) and the CYD's coordinator table.
- **CYD (`main.cpp`)** — a collector path (camp collection channel, fold coordinator batches via `cap::ingest`), coordinator registration + node balancing, and it stops its own capture when coordinators are present.
- **UI (`model.h` / MESH screen)** — `MAX_NODE_ROWS` 8 → ≥12 or per-cluster summary; surface per-cluster `%lost`.
- **Node (`node_main.cpp`)** — minimal: honour a report phase offset; otherwise unchanged.

## Testability

- **Host-testable (pure):** band partitioning across N coordinators, per-cluster `chanplan` at up to 12 nodes, report-phase-offset arithmetic, and the merge/dedup behaviour (extend `sim/test_chanplan.cpp` and add partitioning/staggering tests). The existing `cap::ingest` dedup is already exercised.
- **Hardware acceptance (needs boards):** 3-tier formation (coordinators register, nodes balance across 2 coordinators), steady-state `%lost` at 12 nodes vs 1 vs 2 coordinators, band-split coverage, failure/reassignment (kill a coordinator, confirm its nodes migrate), and that the CYD's UI/GPS/SD stay responsive as a pure head unit. The `MESHSTAT` command and per-cluster `%lost` are the instruments.

## Open questions / risks

1. **Collection channel choice** — reuse ch1 (busy, good for the CYD to also overhear) vs a dedicated quiet channel. Settle in the plan.
2. **One coordinator for all 12** — is it good enough with staggering, or is 2 always wanted at 12? Measure `%lost`; the design supports both, so this is operational, not structural.
3. **ESP-NOW peer limits** — 20 unencrypted peers per device is fine for 12 nodes + coordinators; encryption (not in scope now) would tighten this.
4. **MESH screen at 12+ nodes** — exact UI treatment (per-node rows vs per-cluster summary) needs a small UI design pass within the plan.
