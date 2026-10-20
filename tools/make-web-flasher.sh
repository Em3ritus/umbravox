#!/usr/bin/env bash
# Build all three firmwares, merge each into one browser-flashable image, and
# assemble the web flasher site (page + manifests + images) into web/dist/.
# Used both locally and by the GitHub Pages workflow (.github/workflows/pages.yml).
#
#   PIO=~/.platformio/penv/bin/pio ESPTOOL=~/.platformio/penv/bin/esptool \
#       tools/make-web-flasher.sh
#
# C5 flash map: bootloader 0x2000, partitions 0x8000, boot_app0 0xe000,
# app 0x10000. The merged image flashes at offset 0.
set -euo pipefail
cd "$(dirname "$0")/.."

PIO="${PIO:-pio}"
ESPTOOL="${ESPTOOL:-esptool}"
OUT=web/dist

BOOT_APP0=$(find "${HOME}/.platformio/packages" -name boot_app0.bin 2>/dev/null | head -1)
[ -n "$BOOT_APP0" ] || { echo "error: boot_app0.bin not found under ~/.platformio" >&2; exit 1; }

rm -rf "$OUT"; mkdir -p "$OUT"
cp web/index.html web/manifest-*.json "$OUT"/

merge() {  # <pio-env> <output-name>
  local env="$1" out="$2" d=".pio/build/$1"
  echo ">> building $env"
  "$PIO" run -e "$env"
  echo ">> merging $out"
  "$ESPTOOL" --chip esp32c5 merge-bin -o "$OUT/$out" \
    --flash-mode dio --flash-freq 80m --flash-size 16MB \
    0x2000  "$d/bootloader.bin" \
    0x8000  "$d/partitions.bin" \
    0xe000  "$BOOT_APP0" \
    0x10000 "$d/firmware.bin"
}

merge nm-cyd-c5         umbravox-cyd.bin
merge nm-c5-node        umbravox-node.bin
merge nm-c5-coordinator umbravox-coordinator.bin

echo
echo "web flasher assembled in $OUT/:"
ls -1 "$OUT"
