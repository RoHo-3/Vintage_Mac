#!/bin/sh
# Build Mac MD Write locally with Retro68.
# Set RETRO68 if your build directory is somewhere else.
set -e
RETRO68="${RETRO68:-$HOME/Developer/Retro68-build}"
cd "$(dirname "$0")"
mkdir -p build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="$RETRO68/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake"
make
echo
echo "Built: $(pwd)/MacMDWrite.dsk"
