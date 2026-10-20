"""Phase 2 checks in Live (PLAN.md §7, row 2): Progressions and Shifters, recorded.

Uses the Phase 1 rig (tracks "Chordism P1 Src" / "Chordism P1 Rec", test_phase1_live.py) and
the VST3 "MIDI From" route, which is sample-accurate. The Test pattern presses chord slot
b mod 7 on beat b. With "Test shifter" set, it also holds that shifter's black key through
every odd bar (bars 1, 3 …), so those bars play the shifted chords (Source/core/engine/
TestPattern.h). Everything is set through host parameters, as LiveBridge requires.

Expected chords come from the music21 fixture (Tests/fixtures/theory_reference.json).
"""

import pytest

from test_phase1_live import (  # noqa: F401  (rig and reference are fixtures)
    BARS, PLUGIN, TEMPO, _configure, _record, _save, _set, reference, rig)

from livebridge import timing

# Plug-in choice lists (Source/core/theory/Progressions.h, Source/plugin/Parameters.cpp).
PROGRESSIONS = ["Diatonic (1-7)", "Custom", "Pop (1-5-6-4)", "Fifties (1-6-4-5)", "Two-Five-One (2-5-1)",
                "Circle of Fifths", "Descending (1-7-6-5)", "Canon (1-5-6-3-4)", "Thirds (1-3-5-7)"]
PROGRESSION_DEGREES = {"Pop (1-5-6-4)": [0, 4, 5, 3, 1, 2, 6], "Diatonic (1-7)": [0, 1, 2, 3, 4, 5, 6]}
SHIFTER_MODES = ["Standard", "Toggle"]
SHIFTER_KINDS = ["Chord Group", "Key/Scale"]
TEST_SHIFTER = ["Off", "1", "2", "3", "4", "5"]

pytestmark = pytest.mark.live

# (id, progression, test shifter slot or None, shifter target: ("group", name) | ("scale", name))
CASES = [
    ("pop", "Pop (1-5-6-4)", None, None),
    ("shift-sevenths", "Diatonic (1-7)", 1, ("group", "Sevenths")),
    ("shift-parallel-minor", "Diatonic (1-7)", 4, ("scale", "Natural Minor")),
]


def _set_choice(live, rig, name, value, choices):
    return _set(live, rig, name, choices.index(value), choices)


@pytest.mark.parametrize("case", CASES, ids=lambda c: c[0])
def test_progressions_and_shifters(live, rig, reference, report, case):
    cid, progression, shifter, target = case
    key, scale, group = 0, "Major", "Triads"
    beats_per_bar = 4

    _configure(live, rig, reference, key, scale, group)
    _set_choice(live, rig, "Progression", progression, PROGRESSIONS)
    _set_choice(live, rig, "Shifter mode", "Standard", SHIFTER_MODES)
    shifted_table = None
    if shifter is not None:
        n = "Shifter %d" % shifter
        kind, value = target
        if kind == "group":
            _set_choice(live, rig, n + " type", "Chord Group", SHIFTER_KINDS)
            _set_choice(live, rig, n + " group", value, reference["groups"])
            shifted_table = reference["table"][(key, scale, value)]
        else:
            _set_choice(live, rig, n + " type", "Key/Scale", SHIFTER_KINDS)
            _set(live, rig, n + " key", 0, ["Same"] + [""] * 12)
            _set(live, rig, n + " scale", reference["scales"].index(value) + 1, ["Same"] + reference["scales"])
            shifted_table = reference["table"][(key, value, group)]
    _set_choice(live, rig, "Test shifter", str(shifter) if shifter else "Off", TEST_SHIFTER)

    try:
        routing, audio, clip, notes, beats = _record(live, rig)
    finally:
        _set_choice(live, rig, "Test shifter", "Off", TEST_SHIFTER)
        _set_choice(live, rig, "Progression", "Diatonic (1-7)", PROGRESSIONS)

    base = reference["table"][(key, scale, group)]
    degrees = PROGRESSION_DEGREES[progression]
    by_beat = {}
    for n in notes:
        by_beat.setdefault(int(round(n["song_time"])), []).append(n["pitch"])
    lines, wrong = [], []
    for b in range(int(beats)):
        shifted = shifted_table is not None and (b // beats_per_bar) % 2 == 1
        want = (shifted_table if shifted else base)[degrees[b % 7]]
        got = sorted(by_beat.get(b, []))
        if got != want:
            wrong.append(b)
        lines.append("beat %2d  slot %d%s  want %-22s got %-22s %s" % (
            b, b % 7 + 1, " shifted" if shifted else "        ", want, got, "ok" if got == want else "WRONG"))

    rows = timing.note_offsets(notes, TEMPO, grid=1.0, sample_rate=audio.get("sample_rate"))
    summary = timing.summarize(rows, audio.get("output_buffer"))
    text = timing.format_table("%s Phase 2 %s (%d bars)" % (PLUGIN, cid, BARS), rows, summary, TEMPO, audio)
    text += "\n" + "\n".join(lines)
    report(text)
    print(text)
    _save("phase2-%s" % cid, {"case": case, "audio": audio, "clip": clip, "rows": rows, "wrong_beats": wrong})

    assert notes, "nothing recorded"
    assert not wrong, "beats with the wrong chord: %s" % wrong
    assert max(abs(r["offset_ms"]) for r in rows) < 0.1, "VST3 route not sample-accurate"
