# NM-CYD-C5 native UI engine

Build target: `nm-cyd-c5-engine`. This target runs the same adaptive UI engine
as the Waveshare targets at native 320x240 in landscape, including SCAN,
DOMAIN, CODEX/details, Menu, settings, Files, Alerts and bundled themes.
It enables the native bridge and capture/export paths. The existing
`nm-cyd-c5` target retains the legacy 160x120 canvas at 2x: that is the original UI, and it is what releases and the
browser installer ship for the CYD. The engine target is opt-in (build it yourself); it costs about 9 KB of boot heap
against the original UI.

## Board adapter

NM retains its ST7789 display, XPT2046 resistive touch and UART GPS wiring.
Native touch uses full-panel coordinates and the shared queued sampler.
Display, touch and SD use the same Arduino SPI transaction owner. The NM
has no battery sensing: the header reports unavailable, never a made-up level.
Waveshare I2C touch, PMU and I2C GPS initialization are not used on NM.
Native portrait is not enabled in this target. Resistive pressure calibration
is retained; the capacitive touch sensitivity setting is unavailable.

## First installation: partition migration required

Requires the configured 16 MiB flash and 8 MiB PSRAM NM-CYD-C5 board.
The native application exceeds the old 3 MiB slot. Its separate
`partitions/nm-c5-native-16mb.csv` gives app0 and app1 6 MiB each.
The second slot reserves space; an OTA update workflow is not implemented.

The first native installation requires the matching bootloader at 0x2000,
partition table at 0x8000, boot_app0 at 0xe000 and firmware at 0x10000.
An app-only upload into the old 3 MiB layout is not sufficient.
Verify board identity and physical flash capacity first. Preserve a full-flash
backup privately before migration. NVS remains at 0x9000 and these writes do
not touch external SD. Internal filesystem locations change; preserve any
needed internal files before migration. Rollback requires matching old boot
and partition artifacts plus the old app, or the full-flash backup.

## Validation scope

Host tests and compilation do not validate the physical NM display or touch.
Before hardware acceptance, verify boot stability, colors/orientation, corner
and center taps, repeated navigation, SD recording/export, UART GPS, themes,
bridge and node behavior. The sampler shares SPI with display/SD; a long SPI
transaction can delay sampling, so this is not a hard real-time guarantee.
