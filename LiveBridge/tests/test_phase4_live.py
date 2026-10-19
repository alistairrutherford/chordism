"""Phase 4 checks in Live (PLAN.md §7, row 4): the Smart Scale zone, recorded.

Uses the Phase 1 rig (tracks "Chordism P1 Src" / "Chordism P1 Rec", test_phase1_live.py) and
the VST3 "MIDI From" route, which is sample-accurate. The Test pattern presses chord slot
b mod 7 on beat b for an eighth note; "Test smart" also plays the Smart Scale zone
(Source/core/engine/TestPattern.h):

    Tones      the Smart Scale white key with letter j = 3b mod 7 (C=0 … B=6) on beat b, an eighth
    Pattern 1  pattern key 1 held through the whole take, so the arp follows every chord

The Smart zone plays an octave up ("SS octave" +1, C5 = 72) so its notes never share a pitch
with the chords (C3-B4). Everything is set through host parameters, as LiveBridge requires.

    chord-tones         Chord Tones: scale index d + 2j from the chord's root (d = b mod 7)
    dynamic-inversions  Dynamic + Inversions: index d + j, folded into 72-83
    absolute-d-major    D major, Absolute: letter j's tone in the key signature (F → F#, C → C#)
    note-repeat         Relative + Note repeat (pattern slot 1: 1/16, gate 50%): strikes at b and
                        b + 1/4, an eighth of a beat long
    pattern-1           Pattern 1 (Linear Up 1/16, Triads, gate 50%): step k at k/4 plays tone k mod 3
                        of the chord of beat k // 4, without restarting at chord changes

Expected chord pitches come from the music21 fixture (Tests/fixtures/theory_reference.json).
Every note must start and end within 0.1 ms of its time.
"""

import pytest

from test_phase1_live import (  # noqa: F401  (rig and reference are fixtures)
    BARS, KEY_NAMES, PLUGIN, TEMPO, _configure, _record, _save, _set, reference, rig)
from test_phase3_live import ARP, DEFAULTS as CHORD_DEFAULTS, HOLD, RATES, STACK, VELOCITY, _apply

from livebridge import timing

# Plug-in choice lists (Source/core/engine/*.h, Source/plugin/Parameters.cpp).
MAPPING = ["Chord Tones", "Dynamic", "Relative", "Absolute", "Chromatic"]
TEST_SMART = ["Off", "Tones", "Pattern 1", "Pattern 2", "Pattern 3", "Pattern 4", "Pattern 5"]
SLOTS = ["1", "2", "3", "4", "5"]
PATTERN_ARP = ARP[1:]
ARP_CHORD = ["5ths", "Triads", "7ths", "9ths", "11ths", "13ths"]

MAJOR = [0, 2, 4, 5, 7, 9, 11]
WHITE = [0, 2, 4, 5, 7, 9, 11]
D_MAJOR = [1, 2, 4, 6, 7, 9, 11]   # pitch class of C … B in D major (C#, F#)
SS_C = 72                           # the Smart zone's C with SS octave +1 (keys from C3 = 60)
TOL_MS = 0.1
KEY_UP = 0.5

pytestmark = pytest.mark.live


def _major(i):
    return MAJOR[i % 7] + 12 * (i // 7)


def _letter(b):
    return (3 * b) % 7


def _chord_tones(beat, key):
    d, j = beat % 7, _letter(beat)
    return [(beat, SS_C + _major(d + 2 * j), KEY_UP, None)]


def _dynamic_inversions(beat, key):
    d, j = beat % 7, _letter(beat)
    p = SS_C + _major(d + j)
    while p >= SS_C + 12:
        p -= 12
    return [(beat, p, KEY_UP, None)]


def _absolute(beat, key):
    j = _letter(beat)
    white = SS_C + WHITE[j]
    d = (D_MAJOR[j] - WHITE[j] + 18) % 12 - 6
    return [(beat, white + d, KEY_UP, None)]


def _note_repeat(beat, key):
    p = SS_C + _major(_letter(beat))
    return [(beat + t, p, 0.125, None) for t in (0.0, 0.25)]


def _pattern_1(beat, key):
    out = []
    for i in range(4):
        k = 4 * beat + i
        out.append((beat + i / 4.0, SS_C + _major(beat % 7 + 2 * (k % 3)), 0.125, None))
    return out


# The Smart zone's defaults, restored before and after every case.
SMART_DEFAULTS = {"Mapping": ("Chord Tones", MAPPING), "Inversions": (False, None), "Note repeat": (False, None),
                  "Pattern slot": ("1", SLOTS), "SS octave": (1, (-3, 3)), "SS stack": ("Off", STACK),
                  "SS step seq": (False, None), "SS velocity": ("Off", VELOCITY), "SS hold": ("Off", HOLD),
                  "Pattern 1 arp": ("Linear Up", PATTERN_ARP), "Pattern 1 rate": ("1/16", RATES),
                  "Pattern 1 range": (1, (1, 3)), "Pattern 1 chord": ("Triads", ARP_CHORD), "Pattern 1 gate": (50, (0, 100)),
                  "Test smart": ("Off", TEST_SMART)}

# (id, key, {parameter: (value, choices or (lo, hi))}, expect(beat, key) -> [(start, pitch, length, velocity|None)])
CASES = [
    ("chord-tones", 0, {"Test smart": ("Tones", TEST_SMART)}, _chord_tones),
    ("dynamic-inversions", 0, {"Mapping": ("Dynamic", MAPPING), "Inversions": (True, None),
                               "Test smart": ("Tones", TEST_SMART)}, _dynamic_inversions),
    ("absolute-d-major", 2, {"Mapping": ("Absolute", MAPPING), "Test smart": ("Tones", TEST_SMART)}, _absolute),
    ("note-repeat", 0, {"Mapping": ("Relative", MAPPING), "Note repeat": (True, None),
                        "Test smart": ("Tones", TEST_SMART)}, _note_repeat),
    ("pattern-1", 0, {"Test smart": ("Pattern 1", TEST_SMART)}, _pattern_1),
]


@pytest.mark.parametrize("case", CASES, ids=lambda c: c[0])
def test_smart_scale(live, rig, reference, report, case):
    cid, key, settings, expect_smart = case
    scale, group = "Major", "Triads"
    tempo_ms = 60000.0 / TEMPO
    tol_beats = TOL_MS / tempo_ms

    _configure(live, rig, reference, key, scale, group, compensate=True)
    _set(live, rig, "Progression", 0, [""] * 9)   # Diatonic
    _set(live, rig, "Test shifter", 0, [""] * 6)  # Off
    _apply(live, rig, CHORD_DEFAULTS)
    _apply(live, rig, SMART_DEFAULTS)
    shown = _apply(live, rig, settings)
    try:
        routing, audio, clip, notes, beats = _record(live, rig, "vst3")
    finally:
        _apply(live, rig, SMART_DEFAULTS)

    chords = reference["table"][(key, scale, group)]
    lines, wrong = [], []
    for b in range(int(beats)):
        want = [(float(b), p, KEY_UP, None) for p in chords[b % 7]] + expect_smart(b, key)
        # _record keeps notes that start before the take's last half beat.
        want = sorted((w for w in want if w[0] < beats - 0.5), key=lambda w: (w[0], w[1]))
        got = sorted((n for n in notes if b - 0.01 <= n["song_time"] < b + 0.99), key=lambda n: (n["song_time"], n["pitch"]))
        bad = len(got) != len(want)
        worst = 0.0
        for g, (start, pitch, length, vel) in zip(got, want):
            d_on = abs(g["song_time"] - start)
            d_off = abs(g["song_time"] + g["duration"] - (start + length))
            worst = max(worst, d_on, d_off)
            if g["pitch"] != pitch or d_on > tol_beats or d_off > tol_beats or (vel is not None and g["velocity"] != vel):
                bad = True
        if bad:
            wrong.append(b)
        lines.append("beat %2d  slot %d  want %s\n                 got  %s  (worst %.3f ms) %s" % (
            b, b % 7 + 1, [(round(w[0] - b, 4), w[1]) for w in want],
            [(round(g["song_time"] - b, 4), g["pitch"]) for g in got], worst * tempo_ms,
            "WRONG" if bad else "ok"))

    rows = timing.note_offsets(notes, TEMPO, grid=1 / 64.0, sample_rate=audio.get("sample_rate"))
    summary = timing.summarize(rows, audio.get("output_buffer"))
    text = timing.format_table("%s Phase 4 %s, %s major (%d bars, grid 1/64)" % (PLUGIN, cid, KEY_NAMES[key], BARS),
                               rows, summary, TEMPO, audio)
    text += "\nplug-in shows: %s" % ", ".join("%s=%s" % kv for kv in shown.items())
    text += "\n" + "\n".join(lines)
    report(text)
    print(text)
    _save("phase4-%s" % cid, {"case": cid, "shown": shown, "audio": audio, "clip": clip, "rows": rows,
                              "wrong_beats": wrong})

    assert notes, "nothing recorded"
    assert not wrong, "beats with wrong notes or times: %s" % wrong
