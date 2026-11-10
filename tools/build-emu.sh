#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build Drelva Vave for the browser: the core as WebAssembly on the drawn FM-1.
#   tools/build-emu.sh           ->  build/emu/{index.html, worklet.js, drelva-vave.wasm}
#   tools/build-emu.sh --base    ->  the unmodified Melodee emulator in base/build/emu (reference)
# Needs Emscripten in %LOCALAPPDATA%\drelva-cc\emsdk and Python (a venv is created beside it).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC_DIR="$LOCALAPPDATA/drelva-cc"
VENV="$CC_DIR/vave-venv"
[ -d "$VENV" ] || python -m venv "$VENV"
PY="$VENV/Scripts/python.exe"
"$PY" -m pip install -q --disable-pip-version-check -r "$ROOT/base/tools/requirements-build.txt"
. "$CC_DIR/emsdk/emsdk_env.sh" >/dev/null 2>&1
# Upstream scripts read UTF-8 files with the platform default encoding; force UTF-8 on Windows.
export PYTHONUTF8=1

if [ "$1" = "--base" ]; then
    # Pillow's Raqm text layout needs fribidi-0.dll on Windows; keep a copy in $VENV/fribidi.
    [ -f "$VENV/fribidi/fribidi-0.dll" ] || { echo "missing $VENV/fribidi/fribidi-0.dll"; exit 1; }
    PATH="$VENV/fribidi:$PATH"; export PATH
    cd "$ROOT/base"
    PYTHON="$PY" sh web/emu/build.sh
    exit 0
fi

cd "$ROOT"
mkdir -p build/gen build/emu
"$PY" tools/gen_font.py build/gen/dv_font.h
emcc -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -Ibuild/gen -Isrc \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=262144 -sINITIAL_MEMORY=4194304 -sFILESYSTEM=0 \
    -o build/emu/drelva-vave.wasm emu/dv_web.c src/*.c src/machines/*.c
cp emu/index.html emu/worklet.js build/emu/
echo "emu: build/emu ($(wc -c < build/emu/drelva-vave.wasm | tr -d ' ') B wasm)"
