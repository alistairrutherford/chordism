#!/usr/bin/env bash
# The editor snapshot test (PLAN.md §7 row 5): builds Tests/Snapshot (a console app with the
# processor and editor), renders the editor and compares it with Tests/fixtures/snapshot, and
# checks that every parameter has a control and that state restores.
#
#   Tests/run_snapshot.sh            # compare
#   Tests/run_snapshot.sh --update   # write new reference images (after a deliberate UI change)
#
# The images of each run are in Tests/build/snapshot.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJUCER="${PROJUCER:-$HOME/Development/JUCE/extras/Projucer/Builds/MacOSX/build/Release/Projucer.app/Contents/MacOS/Projucer}"

"$PROJUCER" --resave "$HERE/Snapshot/ChordismSnapshot.jucer" > /dev/null
set -o pipefail
xcodebuild -project "$HERE/Snapshot/Builds/MacOSX/ChordismSnapshot.xcodeproj" -configuration Release build 2>&1 \
    | grep -E "error:|BUILD (SUCCEEDED|FAILED)" | sort -u

cd "$HERE"
"$HERE/Snapshot/Builds/MacOSX/build/Release/ChordismSnapshot" fixtures/snapshot build/snapshot "$@"
