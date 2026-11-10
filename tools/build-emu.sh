#!/bin/sh
# Build the browser emulator (firmware UI + sound as WebAssembly) from a firmware tree.
#   tools/build-emu.sh [TREE]     default TREE: _ref/melodee
# Needs Emscripten in %LOCALAPPDATA%\drelva-cc\emsdk; creates a Python venv beside it.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TREE="${1:-$ROOT/_ref/melodee}"
CC_DIR="$LOCALAPPDATA/drelva-cc"
VENV="$CC_DIR/vave-venv"
[ -d "$VENV" ] || python -m venv "$VENV"
"$VENV/Scripts/python.exe" -m pip install -q -r "$TREE/tools/requirements-build.txt"
. "$CC_DIR/emsdk/emsdk_env.sh" >/dev/null 2>&1
emcc --version | head -n 1
cd "$TREE"
# Upstream scripts read UTF-8 files with the platform default encoding; force UTF-8 on Windows.
export PYTHONUTF8=1
# Pillow's Raqm text layout needs fribidi-0.dll on Windows; keep a copy in $VENV/fribidi.
[ -f "$VENV/fribidi/fribidi-0.dll" ] || { echo "missing $VENV/fribidi/fribidi-0.dll"; exit 1; }
PATH="$VENV/fribidi:$PATH"; export PATH
PYTHON="$VENV/Scripts/python.exe" sh web/emu/build.sh
echo "Serve it with: cd \"$TREE/build/emu\" && python -m http.server 8000"
