#!/usr/bin/env bash
# Phase 0 spike tests: drives the real SpikeProcessor under a fake transport, and
# loads the built VST3/AU bundles the way a host would.
#
# Needs a Release build first (it links the shared-code library and loads the
# bundles that Xcode copied to ~/Library/Audio/Plug-Ins):
#     xcodebuild -project Builds/MacOSX/ChordismSpike.xcodeproj -target "ChordismSpike - All" \
#                -configuration Release build
#     Tests/run_tests.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="$HERE/.."
JUCE_DIR="${JUCE_DIR:-$HOME/Development/JUCE}"
LIB="$PROJ/Builds/MacOSX/build/Release/libChordismSpike.a"
XCPROJ="$PROJ/Builds/MacOSX/ChordismSpike.xcodeproj"

[[ -f "$LIB" ]] || { echo "Build the plugin first (Release). Missing $LIB" >&2; exit 2; }

BUILD="$HERE/build"; mkdir -p "$BUILD"

# The sources use JucePlugin_* macros that Xcode passes on the command line.
# Reproduce them as a defines header from the target's build settings.
xcodebuild -project "$XCPROJ" -target "ChordismSpike - Shared Code" \
           -configuration Release -showBuildSettings -json 2>/dev/null > "$BUILD/settings.json"
/usr/bin/python3 -I - "$BUILD/settings.json" "$BUILD/defs.h" <<'PY'
import json, shlex, sys
data = json.load(open(sys.argv[1]))
defs = next(e["buildSettings"]["GCC_PREPROCESSOR_DEFINITIONS"]
            for e in data if "GCC_PREPROCESSOR_DEFINITIONS" in e.get("buildSettings", {}))
with open(sys.argv[2], "w") as f:
    for t in shlex.split(defs):
        k, _, v = t.partition("=")
        f.write(f"#define {k} {v}\n" if _ else f"#define {k}\n")
PY

if ! clang++ -std=c++17 -x objective-c++ -O1 -include "$BUILD/defs.h" \
    -I "$PROJ/JuceLibraryCode" -I "$JUCE_DIR/modules" -I "$JUCE_DIR/modules/juce_audio_processors_headless/format_types/VST3_SDK" \
    -I "$PROJ/Source" -I "$HERE" \
    -c "$HERE/test_spike.cpp" -o "$BUILD/test_spike.o" 2> "$BUILD/test_spike.err"; then
    cat "$BUILD/test_spike.err" >&2; exit 1
fi

clang++ -O1 "$BUILD/test_spike.o" "$LIB" \
    -framework Cocoa -framework Foundation -framework CoreFoundation -framework Security \
    -framework IOKit -framework CoreAudio -framework CoreMIDI -framework CoreAudioKit \
    -framework AudioToolbox -framework Accelerate -framework CoreServices -framework AudioUnit \
    -framework Carbon -framework QuartzCore -framework Metal -framework WebKit \
    -framework UniformTypeIdentifiers \
    -o "$BUILD/test_spike"

"$BUILD/test_spike"
