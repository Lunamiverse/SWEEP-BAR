#!/usr/bin/env bash
# Builds SweepbarSetup.exe (needs build/win/Sweepbar.vst3 from ../build_windows.sh).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/.."
MINGW_DIR="${MINGW_DIR:-$ROOT/../tools/llvm-mingw}"
OUT="$ROOT/build/installer"
mkdir -p "$OUT"

[[ -f "$ROOT/build/win/Sweepbar.vst3/Contents/x86_64-win/Sweepbar.vst3" ]] || { echo "Build the plugin first"; exit 1; }

python3 "$HERE/make_icon.py" "$HERE/sweepbar.ico"
# README as Windows text (CRLF)
sed 's/$/\r/' "$ROOT/README.md" > "$OUT/README.txt"

cd "$HERE"
"$MINGW_DIR/bin/x86_64-w64-mingw32-windres" -I "$HERE" installer.rc -O coff -o "$OUT/installer.res"
"$MINGW_DIR/bin/x86_64-w64-mingw32-clang++" -std=c++17 -O2 -municode -mwindows -static -s \
  -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
  installer.cpp "$OUT/installer.res" -o "$OUT/SweepbarSetup.exe" \
  -lcomctl32 -lshell32 -lole32 -luuid -ladvapi32
echo "Built: $OUT/SweepbarSetup.exe"
