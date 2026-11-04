# General settings

The Waveshare native engine presents four directly selectable categories. Display,
Touch and Appearance remain on their separate screen. The legacy 160x120 renderer
is unchanged. Native layouts use the same geometry at 480x320 and 320x240.

| Category | Controls |
| --- | --- |
| Scanning | Discovery mode, coordinator dwell, BLE duty, Wi-Fi bands |
| Recording | Auto record, raw PCAP, alert threshold, saved files |
| Bridge | BLE enable, phone app, live link status, advertised identity |
| Nodes | Scan profile, requested node dwell, mesh screen, network ID |

Tapping an editable card opens its editor without changing its value. Minus and
plus use the existing `applyKnobStep` path; changes take effect live and retain
existing persistence rules (PCAP remains session-only). Default resets only the
selected setting. Done returns to the category. Changing phone app while enabled
reconfigures the bridge and can disconnect the phone; this is stated in the editor.

Link status, identity and network ID are read-only. The identity is the actual
bridge advertising name, or NOT STARTED before bridge initialization. Remote dwell
is a requested coordinator policy, not a claim that every node acknowledged it.
Saved Files and Mesh navigate to the existing screens. No global CLEAR action is
exposed here.

All actionable targets are at least 44px in both layouts. Wide cards include a
short explanation; compact cards retain it in the editor. Subpage changes cancel
queued input from the prior geometry, using the same input epoch mechanism as
Display controls. Rendering uses the existing bounded strip and dirty-frame path.

Host verification: `make -C sim test_settings_controls` and
`sim/test_settings_controls OUTPUT_DIRECTORY` for native PNG previews. Tests use
the production adjustment function, cover all categories/editors and both sizes,
check hit bounds/non-overlap, local defaults and bridge app synchronization, and
compare partial redraws with full renders. The full host suite remains required.
