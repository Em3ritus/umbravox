# Optional active discovery

Working baseline: 798f20e. Passive remains the default; no NVS reset is needed.

Native SETTINGS page 4 adds NODE PROFILE, NODE DWELL and DISCOVERY TX.
DISCOVERY TX toggles Wi-Fi probes and BLE scan requests. The console accepts
`DISCOVERY ACTIVE`, `DISCOVERY PASSIVE`, and `DISCOVERY` (scan counters).
The mode is persisted through the existing named-field preferences.

The handheld scans one channel at a time using its existing dwell and channel
plan. While holding the mesh report channel it does not initiate Wi-Fi scans.
Headless nodes scan Wi-Fi only, using their assigned channels and NODE DWELL.
They receive addressed config frames from their bound core; coordinators relay
settings to their nodes. Existing assignment/report layouts are unchanged.
Older node/coordinator firmware ignores configuration kind 8 and must be updated
to apply these settings. Settings describe requested policy, not acknowledgments;
new nodes print applied mode/dwell/profile on serial. Fleet telemetry is future work.
TRACKING preserves the existing camping policy; DISCOVERY distributes coverage
without dedicated campers. Dwell remains 220 ms by default; 150 ms is a trial.

The Wi-Fi event callback publishes completion through an atomic mailbox. Only
the loop starts scans or consumes results. There is no queue-capacity failure,
no fabricated scan ID, no concurrent driver scan owner, and no automatic abort/
restart that could confuse late completions. A missing completion after five
seconds logs once and holds ownership until completion; if it never arrives,
a restart is required. This deliberately does not claim successful recovery.
Results are bounded to 32 per scan; omitted records are counted. Promiscuous
capture remains available for frame details. Result security summaries use the
driver's advertised auth/cipher information, not a negotiated connection.

BLE uses the existing Arduino BLE stack, preserving bridge coexistence and the
configured duty (default 30/100 ms). It does not install a second Bluedroid stack.
The build-local NimBLE wrapper bounds unanswered active scan-response entries
to 16. Active scanning recycles every two seconds to release missing responses;
passive callback-only behavior stays unchanged. At capacity, additional devices
may wait for the next cycle. This is a memory limit, not a promise of exhaustive
capture. Neither active mode nor show_hidden guarantees hidden-name recovery.

Host tests exercise the real Wi-Fi adapter against driver mocks: single owner,
start rejection, delayed/missing completion, failed/empty retrieval, cleanup,
result bounds, config validation and planner mode. Hardware discovery yield,
bridge stability in active mode, and node fleet latency require measurements.
