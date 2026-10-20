"""Phase 5 check in Live (PLAN.md §7, row 5): state restores after a Live set is reopened.

LiveBridge can't save a set (the Live Object Model has no save), so this runs in two stages
with a person in between:

  1. prepare   `uv run pytest -m live tests/test_phase5_live.py -k prepare`
               sets distinctive parameters on the Phase 1 rig's Chordism (D Dorian, Sevenths, the
               Pop progression, Absolute mapping, a sequencer pattern, a pattern slot, Smart
               Scale hold, Note repeat) and writes what Live shows to reports/phase5-state.json.
  2. by hand   open Chordism's editor, choose "Chord Editor", and in the first column (i, Dm7)
               click the cell "E / 9" to turn the 9th on (the column then reads Dm9); then save
               the set (Cmd+S) and note its path.
  3. reopen    `scripts/restart_live.sh "<path to the set>.als"`
  4. check     `uv run pytest -m live tests/test_phase5_live.py -k restored`
               every parameter shows what it showed before saving, and a recorded take plays the
               Pop progression in D Dorian Sevenths with the 9th (E) added to chord I. If another
               edit was saved, give chord I's notes in CHORDISM_CHORD_I (e.g. "53,57,60").
"""

import json
import os

import pytest

from test_phase1_live import (  # noqa: F401  (rig and reference are fixtures)
    REPORT_DIR, TEMPO, _configure, _record, _set, reference, rig)

pytestmark = pytest.mark.live

STATE = os.path.join(REPORT_DIR, "phase5-state.json")
POP = [0, 4, 5, 3, 1, 2, 6]          # Progressions.h: Pop (1-5-6-4), slot -> degree
KEY, SCALE, GROUP = 2, "Dorian", "Sevenths"
NINTH_OF_I = 48 + KEY + 14           # D + a 9th = E4 (64)
TOL_MS = 0.1

SETTINGS = {
    "Progression": (2, [""] * 9),
    "Mapping": (3, [""] * 5),
    "Seq 2 steps": (0x0F0F, (0, 0xFFFF)),
    "Pattern 3 arp": (6, [""] * 13),   # Stutter Up (13 choices since Phase 6 added Melody)
    "SS hold": (2, [""] * 3),          # Bar
    "Note repeat": (True, None),
    "Test shifter": (0, [""] * 6),
    "Test smart": (0, [""] * 7),
}


def _shown(live, rig):
    params = live.call("list_parameters", track=rig["src"], device=rig["device"])["parameters"]
    return {p["name"]: p["display"] for p in params if p["name"] != "Device On"}


def test_state_prepare(live, rig, reference):
    _configure(live, rig, reference, KEY, SCALE, GROUP)
    for name, (value, choices) in SETTINGS.items():
        _set(live, rig, name, value, choices)
    shown = _shown(live, rig)
    with open(STATE, "w") as f:
        json.dump(shown, f, indent=1, sort_keys=True)
    print("Saved %d parameter displays to %s. Now: Chord Editor -> first column (Dm7) -> click E / 9 "
          "(it becomes Dm9), save the set, and reopen it with scripts/restart_live.sh <set>." % (len(shown), STATE))
    assert shown["Key"] == "D" and shown["Mapping"] == "Absolute" and shown["Seq 2 steps"] == "xxxx....xxxx...."


def test_state_restored(live, rig, reference):
    if not os.path.exists(STATE):
        pytest.skip("run the prepare stage first")
    with open(STATE) as f:
        before = json.load(f)
    after = _shown(live, rig)
    # The rig fixture turns the Test pattern off when the prepare stage ends, before the save.
    changed = {k: (v, after.get(k)) for k, v in before.items() if after.get(k) != v and k != "Test pattern"}
    assert not changed, "parameters that didn't restore: %s" % changed
    print("%d parameters restored" % (len(before) - 1))

    _set(live, rig, "Test pattern", True)

    routing, audio, clip, notes, beats = _record(live, rig)
    chords = reference["table"][(KEY, SCALE, GROUP)]
    tol = TOL_MS / (60000.0 / TEMPO)
    wrong = []
    for b in range(int(beats)):
        degree = POP[b % 7]
        want = sorted(chords[degree] + ([NINTH_OF_I] if degree == 0 else []))
        if degree == 0 and os.environ.get("CHORDISM_CHORD_I"):   # another edit than the 9th was saved
            want = sorted(int(n) for n in os.environ["CHORDISM_CHORD_I"].split(","))
        got = sorted(n["pitch"] for n in notes if abs(n["song_time"] - b) < tol)
        if got != want:
            wrong.append((b, want, got))
    print("beats checked: %d, wrong: %s" % (int(beats), wrong))
    assert notes, "nothing recorded"
    assert not wrong, "chords that don't match (the Chord Editor edit didn't restore?): %s" % wrong
