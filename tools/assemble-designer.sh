#!/usr/bin/env bash
# Copy the theme designer's runtime files into the published site.
#
#   tools/assemble-designer.sh [OUT]      OUT defaults to web/dist
#
# Copies web/designer/{index.html,designer.css,js/} and the built wasm
# (wasm/uv_preview.js + .wasm) into OUT/designer/. Tests, the wasm sources and
# object files, and node_modules are NOT copied. Run tools/build-designer-wasm.sh
# first; this refuses to publish a designer without its preview.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:-web/dist}/designer"
SRC=web/designer

for f in "$SRC/index.html" "$SRC/designer.css" "$SRC/js/limits.json" \
         "$SRC/wasm/uv_preview.js" "$SRC/wasm/uv_preview.wasm"; do
    [ -f "$f" ] || { echo "assemble-designer: missing $f (run tools/build-designer-wasm.sh)" >&2; exit 1; }
done

rm -rf "$OUT"
mkdir -p "$OUT/js" "$OUT/wasm"
cp "$SRC/index.html" "$SRC/designer.css" "$OUT"/
cp "$SRC"/js/* "$OUT/js/"
cp "$SRC/wasm/uv_preview.js" "$SRC/wasm/uv_preview.wasm" "$OUT/wasm/"
echo ">> designer assembled in $OUT/"
