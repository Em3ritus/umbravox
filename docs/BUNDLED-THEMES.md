# Bundled themes

Every firmware target includes the same eight approved themes:
UMBRAVOX, CVLT OF BAPH0MAU5, COSMIC MUFFIN, SLICK, BAPH0MAU5,
EM3RITUS, EMBER, and SKULD. No SD card is needed for their palettes or logos.
The native UI adapts to panel size; the legacy NM renderer retains its own layout.

The original three theme IDs stay unchanged. Crew themes retain IDs 3–7,
matching the prior alphabetical SD pack order. Saved crew names resolve even
when their SD pack is absent. New custom themes follow the bundled set.

An SD pack with a bundled crew name overrides that entry instead of adding a
duplicate. Removing the SD override restores the firmware version on next boot.
The original three reserved names remain protected. Existing pack validation,
status-color sanitization, and loader crash guards still apply.

## Source and verification

The five files under `packs/*/theme.json` and their existing `emblem.bin` files
are the approved source. Run `python3 tools/generate_bundled_themes.py` after
changing them. It generates firmware rows, four-bit indexed emblems, and a
losslessly packed copy of the existing Cosmic Muffin logo. Emblems remain in
flash; drawing does not allocate or decompress a full bitmap.

`tools/gen_crew_art.cpp` generates compact three-byte artwork spans. Splitting
long spans changes storage only, not pixels. Cvlt uses the same sigil at 35%
intensity on the compact native screen; its wide rendering remains unchanged.

`sim/test_bundled_themes` checks all palette roles, metadata, every emblem pixel,
saved IDs, override replacement, and rendered equality against the original
packs at both native sizes. The existing wide render goldens remain unchanged.

## Flash capacity

Check the actual `firmware.bin` length against its app partition, not only the
PlatformIO ELF size summary. The bundled 3.5-inch image currently fits very
closely within the 3 MiB application partition. Future additions require a
fresh size check; this change does not alter partition tables or flash devices.
