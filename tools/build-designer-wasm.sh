#!/usr/bin/env bash
# Build the designer's live preview: the device's own ui/ drawing code plus
# web/designer/wasm/bridge.cpp, compiled to WebAssembly with emscripten.
#
#   tools/build-designer-wasm.sh          -> web/designer/wasm/uv_preview.{js,wasm}
#
# The preview is the real renderer, never a re-implementation: the source list
# is read out of sim/Makefile (its SRC minus main_sim.cpp), so a ui/ file the
# simulator gains is a file the preview gains.
#
# Toolchain: emcc on PATH, else ~/emsdk (or $EMSDK) sourced inside THIS
# process only -- nothing global is touched. The pinned version is
# tools/emsdk-version.txt; a different emcc version is refused, because the
# parity test's byte equality is only promised for the pinned one.
#
# Incremental: objects live in web/designer/wasm/build/ with header
# dependencies (-MMD), so a re-run with nothing changed only checks
# timestamps. Outputs and objects are build products and are gitignored.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WANT="$(tr -d '[:space:]' < "$ROOT/tools/emsdk-version.txt")"

if ! command -v emcc >/dev/null 2>&1; then
    EMSDK_DIR="${EMSDK:-$HOME/emsdk}"
    if [ ! -f "$EMSDK_DIR/emsdk_env.sh" ]; then
        echo "build-designer-wasm: emcc is not on PATH and $EMSDK_DIR/emsdk_env.sh does not exist." >&2
        echo "  install emscripten $WANT:" >&2
        echo "    git clone https://github.com/emscripten-core/emsdk.git ~/emsdk" >&2
        echo "    cd ~/emsdk && ./emsdk install $WANT && ./emsdk activate $WANT" >&2
        exit 1
    fi
    # shellcheck disable=SC1091
    source "$EMSDK_DIR/emsdk_env.sh" >/dev/null 2>&1
    command -v emcc >/dev/null 2>&1 || { echo "build-designer-wasm: sourcing $EMSDK_DIR/emsdk_env.sh did not put emcc on PATH" >&2; exit 1; }
fi

HAVE="$(emcc --version 2>/dev/null | head -1 | sed -n 's/.* \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\) .*/\1/p')"
if [ "$HAVE" != "$WANT" ]; then
    echo "build-designer-wasm: emcc is $HAVE, tools/emsdk-version.txt pins $WANT" >&2
    exit 1
fi

# The simulator's sources, from the simulator's Makefile (paths relative to sim/).
SIM_SRC="$(make -s -C "$ROOT/sim" --no-print-directory --eval 'print-uv-src: ; @echo $(SRC_NOMAIN)' print-uv-src)"
[ -n "$SIM_SRC" ] || { echo "build-designer-wasm: could not read SRC_NOMAIN from sim/Makefile" >&2; exit 1; }
SRC=""
for s in $SIM_SRC; do SRC="$SRC $(cd "$ROOT/sim" && realpath "$s")"; done
SRC="$SRC $ROOT/web/designer/wasm/bridge.cpp"

# The exported C surface, and the two runtime helpers the JS side needs
# (HEAPU8 to move bytes in and out, UTF8ToString for uv_error()).
EXPORTS='_uv_load_pack,_uv_clear_packs,_uv_theme_count,_uv_render,_uv_width,_uv_height,_uv_error,_malloc,_free'

exec make -s --no-print-directory -f - \
    ROOT="$ROOT" SRC="$SRC" EXPORTS="$EXPORTS" <<'MAKEFILE'
OUT   := $(ROOT)/web/designer/wasm
OBJD  := $(OUT)/build
# Same language level and optimisation as the simulator (sim/Makefile).
# Exceptions stay off (emscripten's default): ui/ throws none.
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
LDFLAGS  := -O2 -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createUvPreview \
            -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 \
            -sEXPORTED_FUNCTIONS=$(EXPORTS) \
            -sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString

obj = $(OBJD)/$(subst /,_,$(patsubst $(ROOT)/%,%,$(1:.cpp=.o)))
OBJS := $(foreach s,$(SRC),$(call obj,$(s)))

all: $(OUT)/uv_preview.js

$(OUT)/uv_preview.js: $(OBJS) $(ROOT)/tools/build-designer-wasm.sh
	@echo "link  $(notdir $@) + uv_preview.wasm"
	@em++ $(LDFLAGS) -o $@ $(OBJS)

define compile
$(call obj,$(1)): $(1) $(ROOT)/tools/build-designer-wasm.sh | $(OBJD)
	@echo "em++  $(patsubst $(ROOT)/%,%,$(1))"
	@em++ $(CXXFLAGS) -MMD -MP -c -o $$@ $(1)
endef
$(foreach s,$(SRC),$(eval $(call compile,$(s))))

$(OBJD):
	@mkdir -p $@

-include $(OBJS:.o=.d)
MAKEFILE
