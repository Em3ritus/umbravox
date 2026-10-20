# Multi-Coordinator Mesh — Hardware Acceptance Runbook

Step-by-step to validate the coordinator-tier mesh (merged at `cdf5a68`) on real boards.
Spec: `docs/superpowers/specs/2026-10-19-multi-coordinator-mesh-design.md`.
Plan's checklist: the "Hardware Acceptance" section of `docs/superpowers/plans/2026-10-19-multi-coordinator-mesh.md`.

**This whole run can be done INDOORS.** The mesh test exercises formation / channel-split / reporting / failure-recovery — none of it needs a GPS fix. (GPS only matters for *geolocating* sightings into the SD/WiGLE log, which is the separate SD-logging acceptance.)

## 0. Roles, boards, ports

| Role | Board | Port | Env |
|------|-------|------|-----|
| CYD = Core/collector + head unit | NM-CYD-C5 | `/dev/ttyUSB0` (CH340) | `nm-cyd-c5` |
| Coordinator (1–2) | Waveshare ESP32-C5 | `/dev/ttyACM*` (WCH 1a86:55d3) | `nm-c5-coordinator` |
| Node (1–12) | Waveshare / Xiao C5 | `/dev/ttyACM*` | `nm-c5-node` |

Map each ACM board to a physical unit by serial number (ports re-enumerate between plug-ins):
```
for d in /dev/ttyACM*; do sn=$(udevadm info -q property -n "$d" | grep ID_SERIAL_SHORT | cut -d= -f2); echo "$d $sn"; done
```

**Flash all three roles from the SAME build** — the mesh protocol is `VERSION=3`; a board on a different version is hard-rejected (you'll see `speaks protocol vN, we speak v3 -- reflash it` on the Core console).

## 1. Flashing

```
~/.platformio/penv/bin/pio run -e nm-cyd-c5       -t upload                      # CYD on ttyUSB0
~/.platformio/penv/bin/pio run -e nm-c5-node       -t upload --upload-port /dev/ttyACMx
~/.platformio/penv/bin/pio run -e nm-c5-coordinator -t upload --upload-port /dev/ttyACMx
```

**WCH-bridge C5 flashing quirks (learned this session — see memory `technomancer-platform-scope`):**
- The node/coordinator envs already pin `upload_speed = 115200` (these boards choke renegotiating to 460800: `Changing baud rate to 460800... failed` / `Invalid head of packet`).
- A newer C5 silicon revision (e.g. **v1.2**) is **incompatible with esptool's stub flasher** — it syncs, loads the stub, then dies at the first flash access (`read_reg(SPI_USR_REG)` → `chip stopped responding`, right after `Stub flasher running`). Flash that board **manually with `--no-stub`**:
  ```
  # get the exact offsets/files:
  ~/.platformio/penv/bin/pio run -e nm-c5-coordinator -t upload -v   # read the write-flash line
  # then (adjust port; write the bootloader in ITS OWN call — esptool silently skips 0x2000 when batched):
  ~/.platformio/penv/bin/esptool --chip esp32c5 --port /dev/ttyACMx --baud 115200 \
    --before default-reset --after hard-reset --no-stub write-flash -z \
    --flash-mode dio --flash-freq 80m --flash-size detect \
    0x2000 .pio/build/nm-c5-coordinator/bootloader.bin
  ~/.platformio/penv/bin/esptool --chip esp32c5 --port /dev/ttyACMx --baud 115200 \
    --before default-reset --after hard-reset --no-stub write-flash -z \
    --flash-mode dio --flash-freq 80m --flash-size detect \
    0x8000 .pio/build/nm-c5-coordinator/partitions.bin \
    0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
    0x10000 .pio/build/nm-c5-coordinator/firmware.bin
  ```
- If an upload fails with `chip stopped responding` at the *start* (not after the stub), the board keeps dropping off USB — swap to a known-good DATA cable, plug direct (not a hub), and re-check the port stays present in `dmesg`.

## 2. Observing the mesh (serial)

- **CYD console (`/dev/ttyUSB0`)** carries the Core/collector logs + the per-frame status line's `mesh Nn Nr N%lost` field, and the `MESHSTAT` command (callbacks + per-reason rejects + accepted reports). `pio device monitor` needs a real TTY; from an agent/script use pyserial: open `/dev/ttyUSB0` @115200, reset with `dtr=False; rts=True; sleep 0.15; rts=False`, then read. Send `MESHSTAT\n` / `WIGLE\n` / `SDLS\n` / `SDCAT /file\n` as needed.
- **Coordinator / node Serial** should be readable over their `ttyACM` via the WCH bridge (Serial is on UART0, which the bridge carries). **Verify this early**: open a coordinator's ttyACM and look for `AETHERSCAN node` / mesh lines. If it's blank at runtime, that board's Serial isn't on the bridge → rebuild that env with `-DARDUINO_USB_CDC_ON_BOOT=1` and reflash to get Serial over USB-CDC.

## 3. Acceptance steps (in order)

Boot order matters: **start coordinators BEFORE nodes** — a node that binds to the CYD before a coordinator exists only migrates on coordinator loss.

0. **Backward-compat (0 coordinators).** No coordinator powered. Flash CYD + 1 node. Confirm the node joins the CYD *Core* and reports exactly as before: Core console shows `mesh: N node(s), core camping ch1`, `MESHSTAT` accepted reports climb, status line `mesh 1n Nr 0%lost`. This proves the merge didn't regress the flat setup.
1. **1 coordinator.** Power CYD + 1 coordinator + 1–2 nodes. Expect: coordinator registers with the CYD (collector), the CYD camps the collection channel, nodes get balanced to the coordinator, MESH screen header reads **"1 COORD …"** and shows a loss figure. Confirm node-found APs reach the CYD table (`WIGLE` / `SDCAT` show WIFI rows with node-attributed finds).
2. **2 coordinators.** Power a 2nd coordinator. Expect distinct report channels + a band split (coord-0 → 2.4 GHz, coord-1 → 5 GHz), nodes split across the two, both clusters' sightings merging on the CYD, MESH header **"2 COORD"**.
3. **`%lost` at scale.** With ~12 nodes, compare report `%lost` on 1 coordinator vs 2. Confirm report staggering keeps loss low and that 2 coordinators roughly halves per-channel loss. **This empirically sets the loss thresholds** (the UI's `NEAR LIMIT` 10% / `ADD COORD` 20% are provisional).
4. **Failure / reassignment.** Kill a coordinator (unplug). Expect: its nodes' unicast fail-streak trips (~8 consecutive fails), they re-Hello, and the CYD migrates them to the surviving coordinator and re-slices the band. Then kill a node → its channels free up and the coordinator re-plans. (The dead coordinator's own re-registration — re-sends `CoordReg` after ~10 s without an assign — is also exercised if you re-power it or reboot the CYD.)
5. **CYD responsiveness.** Confirm the CYD's UI/touch/GPS/SD stay responsive as a pure head unit under 12 nodes of traffic (watch frame times in the status line).
6. **Record** per-cluster `%lost` (1 vs 2 coordinators) and CYD frame times in the README block alongside the existing figures.

## Known limitations to keep in mind during the run
- The mesh is **unauthenticated** (magic + version only) — a same-protocol device in RF range could spoof a coordinator or inject sightings. Accepted for a co-located owner-operated rig; a future encrypted-cluster mode is the roadmap fix.
- UI loss is **mesh-wide / max-cluster**, not yet per-coordinator rows — enough to decide *whether* to add a coordinator, not yet *which* cluster is hottest.

## Bonus (hardware is out anyway): SD logging leftovers
- SD wardrive logging acceptance item **#7** (flush-duration measurement) is still open — with a card in, start a session, observe the per-flush stall, and record it in the README.
- A real geolocated wardrive upload still wants a stable outdoor GPS fix (the mesh run above does not).

## Results — 2026-10-20 overnight run (0/1-coordinator path validated)

Run against merged master `cdf5a68`. Rig: CYD (ttyUSB0) + 1 Waveshare coordinator
(ttyACM0, eco3/v1.2, `--no-stub`) + 1 Xiao C5 node (ttyACM1, native USB). Indoors.
Full evidence log: `private/hardware-acceptance-results-2026-10-20.md`.

- **Step 0 (backward-compat): PASS.** Flat CYD Core adopted a v3 node, accepted reports
  climbed continuously (16→77→83); a stale-v2 board was hard-rejected throughout
  (`speaks protocol v2, we speak v3`) and never entered the table. No flat-path regression;
  version gate confirmed.
- **Step 1 (1 coordinator + 1 node): PASS.** Coordinator registered and was assigned
  (`cluster 0, report ch6, collect ch1`); CYD logged `collector: coordinator … -> cluster 0`;
  node migrated to the coordinator; steady state `nodes=1 loss=0% folded=… relayed=… drop=0
  fail=0`. After `GPSFIX`, `WIGLE` exported **7 geolocated rows** across both bands — the
  full node→coordinator→CYD→export chain. (Pre-fix: rows skipped for no position — expected indoors.)
- **CYD-reboot reconvergence (part of step 4): PASS.** Rebooting the CYD self-healed back to
  node-on-coordinator within ~10s (coordinator `REREG_MS`), one transient unicast fail, recovered.

Flashing notes confirmed on hardware: the Waveshare is **eco3 (v1.2)** → `--no-stub` required,
bootloader at 0x2000 written in its own esptool call. The Xiao's app `Serial` is **not** on its
USB-CDC (only the ROM boot log appears over ttyACM1) — it meshes fine over RF; rebuild
`nm-c5-node` with `-DARDUINO_USB_CDC_ON_BOOT=1` to get node logs over USB.

**Still open at that point (need the owner + more boards):** step 2 (2 coordinators — only 1
Waveshare on hand), step 3 (~12-node scale + loss-threshold tuning), step 4 full (physical
coordinator unplug → node fail-streak → migration/re-slice), step 5 (CYD responsiveness under
12 nodes), and SD flush-duration (#7, needs a recording session started by a footer tap).
Step 2 was validated the same day — see below.

## Results — 2026-10-20 (step 2: two coordinators + node-migration fix)

Rig: CYD (ttyUSB0) + **two** Waveshare coordinators — one on `ttyACM1`, one on a USB
charger (boards mesh over RF; USB is only power/flash/serial, so a coordinator needs no
laptop port once flashed) — + 1 Xiao C5 node (ttyACM0). A coordinator runs on any C5, not
just a Waveshare (the `nm-c5-coordinator` env is board-agnostic). The eco2 Waveshare
(SN 5C37182998) flashes with the normal stub; only the eco3/v1.2 board needs `--no-stub`.

- **Step 2 (2 coordinators + band split): PASS.** Both registered on the CYD
  (`collector: coordinator … -> cluster 0 (1 live)` then `… -> cluster 1 (2 live)`) and got
  **distinct report channels with a clean band split** — cluster 0 → ch6 (2.4 GHz, 12-ch
  slice), cluster 1 → ch36 (5 GHz, 8-ch slice). This is `chanplan::partition(2)` on real
  radios, matching `sim/test_cluster`. (A 3rd coordinator is correctly refused — partition
  caps at two slices.) Node *splitting* across the two needs ≥2 nodes (only 1 on hand); that
  allocation is covered by `test_cluster` (12 nodes → 6/6).

- **Node-migration bug found and fixed (merged to master, 93dc79c + fcf4255).** Step 2
  surfaced a real defect: in collector mode a node that bound to the CYD Core *before* a
  coordinator registered (boot order, or across a CYD reboot) stayed stuck on the CYD forever
  and never offloaded — defeating the coordinators. Root cause: the node latched its Core on
  the first `Assign` and never re-homed, while the CYD's Core kept re-asserting itself. Fix:
  the node re-homes when an `Assign` names a different Core; the CYD's `meshcore` stops
  assigning while a coordinator is live; the collector redirects a straggler on its direct
  `Report`; and the node holds the report channel ~60 ms after reporting so the redirect lands
  promptly. A security guard only honours a re-home from the node's **current** Core (the mesh
  is still unauthenticated by design). **Verified on hardware:** a boot-order straggler now
  migrates off the CYD within ~1 report cycle (CYD direct `reportsRx` plateaued at 2, versus
  climbing 22→28 over ~48 s before the fix), lands on a coordinator, and stays put (no
  flapping). Host-tested in `sim/test_rehome.cpp`.

**Still open:** step 3 (~12-node scale + loss-threshold tuning) and step 5 (CYD
responsiveness under 12 nodes) — both need more nodes than on hand; the allocation/balancing
logic is host-covered by `test_cluster`. Step 4 full (physical coordinator unplug → fail-streak
→ reassignment) still wants a hands-on run; the CYD-reboot half of it is already PASS above.
SD flush-duration (#7) unchanged.
