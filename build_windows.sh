#!/usr/bin/env bash
# Cross-compiles Sweepbar.vst3 for Windows x64 from Linux using llvm-mingw.
# Usage: ./build_windows.sh            (expects JUCE and llvm-mingw paths below)
#        JUCE_DIR=... MINGW_DIR=... ./build_windows.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
JUCE_DIR="${JUCE_DIR:-$HERE/../tools/JUCE}"
MINGW_DIR="${MINGW_DIR:-$HERE/../tools/llvm-mingw}"
CXX="$MINGW_DIR/bin/x86_64-w64-mingw32-clang++"
OUT="$HERE/build/win"
JOBS="${JOBS:-$(nproc)}"

MODS="$JUCE_DIR/modules"
CXXFLAGS=(
  -std=c++17 -O2 -DNDEBUG=1 -DJUCE_DEBUG=0
  -include "$HERE/Source/JuceConfig.h"
  -I "$MODS"
  -I "$MODS/juce_audio_processors/format_types/VST3_SDK"
  -I "$HERE/Source"
  -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DUNICODE -D_UNICODE
  -Wno-deprecated-declarations -Wno-unused-command-line-argument
)

mkdir -p "$OUT/modules" "$OUT/plugin"

compile() { # src obj
  local src="$1" obj="$2"
  if [[ ! -f "$obj" || "$src" -nt "$obj" || "$HERE/Source/JuceConfig.h" -nt "$obj" ]]; then
    echo "  CXX $(basename "$src")"
    "$CXX" "${CXXFLAGS[@]}" -c "$src" -o "$obj"
  fi
}
export -f compile

MODULE_SRCS=(
  juce_core/juce_core.cpp
  juce_events/juce_events.cpp
  juce_data_structures/juce_data_structures.cpp
  juce_graphics/juce_graphics.cpp
  juce_gui_basics/juce_gui_basics.cpp
  juce_gui_extra/juce_gui_extra.cpp
  juce_audio_basics/juce_audio_basics.cpp
  juce_audio_processors/juce_audio_processors.cpp
  juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp
)

echo "== JUCE modules"
pids=()
for s in "${MODULE_SRCS[@]}"; do
  compile "$MODS/$s" "$OUT/modules/$(basename "${s%.cpp}").o" &
  pids+=($!)
  if (( ${#pids[@]} >= JOBS )); then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
done
for p in "${pids[@]}"; do wait "$p"; done

echo "== Plugin sources"
for s in "$HERE"/Source/*.cpp; do
  compile "$s" "$OUT/plugin/$(basename "${s%.cpp}").o"
done

echo "== Link"
BUNDLE="$OUT/Sweepbar.vst3"
mkdir -p "$BUNDLE/Contents/x86_64-win" "$BUNDLE/Contents/Resources"
"$CXX" -shared -static -s -o "$BUNDLE/Contents/x86_64-win/Sweepbar.vst3" \
  "$OUT"/plugin/*.o "$OUT"/modules/*.o \
  -lole32 -loleaut32 -limm32 -lcomdlg32 -lshlwapi -lrpcrt4 -lwinmm -lws2_32 \
  -lwininet -lversion -lshell32 -luuid -lgdi32 -luser32 -ladvapi32 -lcomctl32 \
  -luxtheme -ldwmapi -ld2d1 -ldwrite -ldxgi -ld3d11 -lsetupapi -lwsock32 -lnetapi32 -liphlpapi

echo "Built: $BUNDLE"
