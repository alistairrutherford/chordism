#!/usr/bin/env bash
# Host validation (PLAN.md §8): auval for the AU instrument (aumu) and the AU MIDI effect
# (aumi), and pluginval at strictness 10 for the VST3 and both AUs.
#
# Needs Release builds of Chordism.jucer and MidiFX/ChordismMidiFX.jucer; their copy step
# puts the bundles in ~/Library/Audio/Plug-Ins. pluginval is found as $PLUGINVAL, on PATH,
# or in /Applications or ~/Applications.
#
#   Tests/run_validation.sh            # everything
#   Tests/run_validation.sh --auval    # auval only
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGINS="$HOME/Library/Audio/Plug-Ins"
VST3="$PLUGINS/VST3/Chordism.vst3"
AU="$PLUGINS/Components/Chordism.component"
AUFX="$PLUGINS/Components/ChordismMidiFX.component"
STRICTNESS="${PLUGINVAL_STRICTNESS:-10}"

for b in "$VST3" "$AU" "$AUFX"; do
    [[ -d "$b" ]] || { echo "Missing $b: build both projects (Release) first." >&2; exit 2; }
done

failed=()
run () {
    local name="$1"; shift
    echo ""
    echo "== $name =="
    if "$@"; then echo "-- $name: PASS"; else echo "-- $name: FAIL"; failed+=("$name"); fi
}

# A freshly built component isn't seen until the registrar rescans.
killall -9 AudioComponentRegistrar 2>/dev/null || true

auval_quiet () {
    local log; log="$(mktemp)"
    if auval -strict -v "$@" > "$log" 2>&1; then
        grep -E "AU VALIDATION SUCCEEDED|PASS" "$log" | tail -2
        rm -f "$log"
    else
        cat "$log"; rm -f "$log"; return 1
    fi
}
run "auval aumu Cdsm ARut (instrument)" auval_quiet aumu Cdsm ARut
run "auval aumi Cdsf ARut (MIDI effect)" auval_quiet aumi Cdsf ARut

if [[ "${1:-}" != "--auval" ]]; then
    PV="${PLUGINVAL:-}"
    if [[ -z "$PV" ]]; then
        for c in "$(command -v pluginval || true)" /Applications/pluginval.app/Contents/MacOS/pluginval \
                 "$HOME/Applications/pluginval.app/Contents/MacOS/pluginval"; do
            [[ -n "$c" && -x "$c" ]] && { PV="$c"; break; }
        done
    fi
    [[ -n "$PV" ]] || { echo "pluginval not found (set PLUGINVAL)." >&2; exit 2; }

    pv () {
        "$PV" --strictness-level "$STRICTNESS" --timeout-ms 600000 --validate-in-process --output-dir "$HERE/build/pluginval" --validate "$1"
    }
    mkdir -p "$HERE/build/pluginval"
    run "pluginval VST3 (strictness $STRICTNESS)" pv "$VST3"
    run "pluginval AU instrument (strictness $STRICTNESS)" pv "$AU"
    run "pluginval AU MIDI effect (strictness $STRICTNESS)" pv "$AUFX"
fi

echo ""
if (( ${#failed[@]} )); then
    printf 'FAILED: %s\n' "${failed[@]}"
    exit 1
fi
echo "All host validation passed."
