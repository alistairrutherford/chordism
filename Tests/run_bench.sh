#!/usr/bin/env bash
# CPU benchmark (PLAN.md §7 row 8: 8 instances under 1% CPU each). Builds Tests/Snapshot (the
# real processor, Release) and times processBlock for 8 instances run one after another, as a
# host's audio thread runs them: idle, the test pattern, and every effect at once with the
# instances in sync groups, at 32-, 128- and 512-sample blocks (44.1 kHz). Prints each
# instance's mean CPU as a share of real time (100% = one core) and its worst block.
#
#   Tests/run_bench.sh [seconds of audio per case, default 20]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJUCER="${PROJUCER:-$HOME/Development/JUCE/extras/Projucer/Builds/MacOSX/build/Release/Projucer.app/Contents/MacOS/Projucer}"

"$PROJUCER" --resave "$HERE/Snapshot/ChordismSnapshot.jucer" > /dev/null
set -o pipefail
xcodebuild -project "$HERE/Snapshot/Builds/MacOSX/ChordismSnapshot.xcodeproj" -configuration Release build 2>&1 \
    | grep -E "error:|BUILD (SUCCEEDED|FAILED)" | sort -u

"$HERE/Snapshot/Builds/MacOSX/build/Release/ChordismSnapshot" --bench "${1:-20}"
