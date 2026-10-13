#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Melodee in the browser: the firmware's sources, as the host tests build them, compiled to WebAssembly
# with Emscripten, plus the page. After Felucca's web/emu/build.sh (Hügelton Instruments, GPL-3.0).
#   web/emu/build.sh  ->  build/emu/{index.html, worklet.js, melodee.wasm}
# Run tools/build.py once first (build/gen: the tables, melodee_wt.h).
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/melodee_tables.h ] || { echo "emu: run python3 tools/build.py once first (build/gen)"; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten); source emsdk_env.sh"; exit 1; }
VERSION=$(sed -n 's/^#define MELODEE_VERSION "\([^"]*\)".*/\1/p' firmware/src/melodee.c)
[ -n "$VERSION" ] || VERSION=WEB
mkdir -p build/emu
# single core, no USB/UART/OTA/CDC: the browser has none of them. -ffp-contract=off as the host build.
# shellcheck disable=SC2086
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w \
    "-DMELODEE_VERSION=\"$VERSION\"" -DMELODEE_DUAL_CORE=0 -DMELODEE_USB_AUDIO=0 -DMELODEE_UART=0 \
    -DMELODEE_OTA=0 -DMELODEE_CDC=0 -DMELODEE_FLASH=1 -DMELODEE_SLICE=0 \
    -Ibuild/gen -Ifirmware/src \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=2097152 -sINITIAL_MEMORY=67108864 -sFILESYSTEM=0 \
    -o build/emu/melodee.wasm web/emu/melodee_web.c
sed -e "s/@VERSION@/$VERSION/g" web/emu/index.html > build/emu/index.html
cp web/emu/worklet.js build/emu/
echo "emu: build/emu ($(wc -c < build/emu/melodee.wasm | tr -d ' ') B wasm, $VERSION)"
