#!/usr/bin/env bash
# Builds tests/plugin_harness natively (Linux) against the real plugin sources.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
JUCE_DIR="${JUCE_DIR:-$HERE/../tools/JUCE}"
MODS="$JUCE_DIR/modules"
OUT="$HERE/build/native"
mkdir -p "$OUT"
FLAGS=(-std=c++17 -O2 -g0 -DJUCE_DEBUG=0 -DNDEBUG=1 -include "$HERE/Source/JuceConfig.h"
       -I "$MODS" -I "$HERE/Source" -I /usr/include/freetype2 -w
       -DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XRENDER=0 -DJUCE_USE_XCURSOR=0)

srcs=(juce_core/juce_core.cpp juce_events/juce_events.cpp juce_data_structures/juce_data_structures.cpp
      juce_graphics/juce_graphics.cpp juce_gui_basics/juce_gui_basics.cpp juce_gui_extra/juce_gui_extra.cpp
      juce_audio_basics/juce_audio_basics.cpp juce_audio_processors/juce_audio_processors.cpp)
pids=()
for s in "${srcs[@]}"; do
  o="$OUT/$(basename "${s%.cpp}").o"
  [[ -f "$o" ]] || { g++ "${FLAGS[@]}" -c "$MODS/$s" -o "$o" & pids+=($!); }
done
for p in "${pids[@]}"; do wait "$p"; done

for s in "$HERE"/Source/*.cpp "$HERE/tests/plugin_harness.cpp"; do
  g++ "${FLAGS[@]}" -c "$s" -o "$OUT/$(basename "${s%.cpp}").o"
done
g++ -o "$OUT/plugin_harness" "$OUT"/*.o -lfreetype -lpthread -ldl -lrt -pthread
echo "built $OUT/plugin_harness"
