#!/usr/bin/env bash
# Host validation (PLAN.md §8): pluginval at strictness 10 for the VST3. (The AU builds, and
# auval with them, were removed on 2026-10-20.)
#
# Needs a Release build of Chordism.jucer; its copy step puts the bundle in
# ~/Library/Audio/Plug-Ins. pluginval is found as $PLUGINVAL, on PATH, or in /Applications or
# ~/Applications.
#
#   Tests/run_validation.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VST3="$HOME/Library/Audio/Plug-Ins/VST3/Chordism.vst3"
STRICTNESS="${PLUGINVAL_STRICTNESS:-10}"

[[ -d "$VST3" ]] || { echo "Missing $VST3: build Chordism.jucer (Release) first." >&2; exit 2; }

PV="${PLUGINVAL:-}"
if [[ -z "$PV" ]]; then
    for c in "$(command -v pluginval || true)" /Applications/pluginval.app/Contents/MacOS/pluginval \
             "$HOME/Applications/pluginval.app/Contents/MacOS/pluginval"; do
        [[ -n "$c" && -x "$c" ]] && { PV="$c"; break; }
    done
fi
[[ -n "$PV" ]] || { echo "pluginval not found (set PLUGINVAL)." >&2; exit 2; }

mkdir -p "$HERE/build/pluginval"
echo "== pluginval VST3 (strictness $STRICTNESS) =="
if "$PV" --strictness-level "$STRICTNESS" --timeout-ms 600000 --validate-in-process \
         --output-dir "$HERE/build/pluginval" --validate "$VST3"; then
    echo "-- pluginval VST3 (strictness $STRICTNESS): PASS"
else
    echo "-- pluginval VST3 (strictness $STRICTNESS): FAIL"
    exit 1
fi
