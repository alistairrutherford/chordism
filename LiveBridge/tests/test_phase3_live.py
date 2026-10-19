"""Phase 3 checks in Live (PLAN.md §7, row 3): the chord effects, recorded.

Uses the Phase 1 rig (tracks "Chordism P1 Src" / "Chordism P1 Rec", test_phase1_live.py) and
the VST3 "MIDI From" route, which is sample-accurate. The Test pattern presses chord slot
b mod 7 on beat b and releases it an eighth note later; the effects turn each press into a
strum, an arp or repeats (Source/core/engine/ChordFx.h). Everything is set through host
parameters, as LiveBridge requires.

Expected pitches come from the music21 fixture (Tests/fixtures/theory_reference.json); the
expected times and lengths from the effect definitions:

    strum-up        Strum Up, 1/32: tone i (ascending) at b + i/8, all to b + 1/2
    strum-down      Strum Down, 1/64: tone i (descending) at b + i/16, all to b + 1/2
    arp-up          Arp Linear Up, 1/32, gate 50%, range 1: steps at b + k/8 (k = 0..3), 1/64 long
    arp-converge    Arp Converge Down, 1/32, gate 50%, range 2 (six tones: 5 0 4 1 …)
    repeat-velocity Repeat 1/16, gate 50%, Velocity Up (Peak 127, Range 100%): strikes at b and
                    b + 1/4, 1/8 long, velocity 127·s/15 for sixteenth s of the bar (at least 1)

Every note must start and end within 0.1 ms of its time.

Chordism has more than 64 parameters, so Live needs `-_PluginAutoPopulateThreshold=128` in its
Options.txt to show them to the bridge (LiveBridge/README.md, Setup).
"""

import pytest

from test_phase1_live import (  # noqa: F401  (rig and reference are fixtures)
    BARS, PLUGIN, TEMPO, _configure, _record, _save, _set, reference, rig)

from livebridge import timing

# Plug-in choice lists (Source/core/engine/*.h, Source/plugin/Parameters.cpp).
STRUM = ["Off", "Up", "Down", "Alternating", "Random"]
STRUM_RATES = ["1/128", "1/64T", "1/64", "1/32T", "1/32", "1/16T", "1/16"]
ARP = ["Off", "Linear Up", "Linear Down", "Converge Up", "Converge Down", "Diverge Up", "Diverge Down",
       "Stutter Up", "Stutter Down", "Zigzag Up", "Zigzag Down", "Boomerang Up", "Boomerang Down"]
RATES = ["1/2", "1/4", "3/16", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"]
VELOCITY = ["Off", "Up", "Down", "Up 1/2", "Down 1/2", "Up 1/4", "Down 1/4", "Up/Down", "Down/Up",
            "Afrobeat", "Breakbeat", "Dancehall", "House", "Moombahton"]
STACK = ["Off", "+1", "+2", "+1&2"]
HOLD = ["Off", "Latch", "Bar"]

TOL_MS = 0.1
KEY_UP = 0.5      # the Test pattern holds each key an eighth note

pytestmark = pytest.mark.live


def _converge_down(n, k):
    c = k % n
    up = c // 2 if c % 2 == 0 else n - 1 - c // 2
    return n - 1 - up


def _strum(step, descending):
    def expect(beat, chord):
        tones = sorted(chord, reverse=descending)
        return [(beat + i * step, p, KEY_UP - i * step, None) for i, p in enumerate(tones) if i * step < KEY_UP]
    return expect


def _arp(index, octaves):
    def expect(beat, chord):
        tones = sorted({p + 12 * o for p in chord for o in range(octaves)})
        return [(beat + k / 8.0, tones[index(len(tones), k)], 1 / 16.0, None) for k in range(4)]
    return expect


def _repeat_velocity(beat, chord):
    out = []
    for t in (0.0, 0.25):
        s = int(round(((beat + t) % 4) * 4))
        v = max(1, int(round(127 * s / 15.0)))
        out += [(beat + t, p, 0.125, v) for p in sorted(chord)]
    return out


# (id, {parameter: (value, choices or (lo, hi))}, expect(beat, chord) -> [(start, pitch, length, velocity|None)])
CASES = [
    ("strum-up", {"Strum": ("Up", STRUM), "Strum rate": ("1/32", STRUM_RATES)}, _strum(0.125, False)),
    ("strum-down", {"Strum": ("Down", STRUM), "Strum rate": ("1/64", STRUM_RATES)}, _strum(0.0625, True)),
    ("arp-up", {"Arp": ("Linear Up", ARP), "Arp rate": ("1/32", RATES), "Arp gate": (50, (0, 100)),
                "Arp range": (1, (1, 3))}, _arp(lambda n, k: k % n, 1)),
    ("arp-converge", {"Arp": ("Converge Down", ARP), "Arp rate": ("1/32", RATES), "Arp gate": (50, (0, 100)),
                      "Arp range": (2, (1, 3))}, _arp(_converge_down, 2)),
    ("repeat-velocity", {"Repeat": (True, None), "Repeat rate": ("1/16", RATES), "Repeat gate": (50, (0, 100)),
                         "Velocity": ("Up", VELOCITY), "Velocity peak": (127, (1, 127)),
                         "Velocity range": (100, (0, 100))}, _repeat_velocity),
]

# The effects' defaults, restored before and after every case.
DEFAULTS = {"Stack": ("Off", STACK), "Strum": ("Off", STRUM), "Arp": ("Off", ARP), "Repeat": (False, None),
            "Step seq": (False, None), "Velocity": ("Off", VELOCITY), "Hold": ("Off", HOLD)}


def _apply(live, rig, settings):
    shown = {}
    for name, (value, choices) in settings.items():
        if isinstance(value, bool):
            r = _set(live, rig, name, value)
        elif isinstance(choices, list):
            r = _set(live, rig, name, choices.index(value), choices)
        else:
            r = _set(live, rig, name, value, choices)
        shown[name] = r.get("display")
    return shown


@pytest.mark.parametrize("case", CASES, ids=lambda c: c[0])
def test_chord_effects(live, rig, reference, report, case):
    cid, settings, expect = case
    key, scale, group = 0, "Major", "Triads"
    tempo_ms = 60000.0 / TEMPO
    tol_beats = TOL_MS / tempo_ms

    _configure(live, rig, reference, key, scale, group, compensate=True)
    _set(live, rig, "Progression", 0, [""] * 9)   # Diatonic
    _set(live, rig, "Test shifter", 0, [""] * 6)  # Off
    _apply(live, rig, DEFAULTS)
    shown = _apply(live, rig, settings)
    try:
        routing, audio, clip, notes, beats = _record(live, rig, "vst3")
    finally:
        _apply(live, rig, DEFAULTS)

    chords = reference["table"][(key, scale, group)]
    lines, wrong = [], []
    for b in range(int(beats)):
        want = sorted(expect(float(b), chords[b % 7]), key=lambda w: (w[0], w[1]))
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
            b, b % 7 + 1, [(round(w[0] - b, 4), w[1], w[3]) for w in want],
            [(round(g["song_time"] - b, 4), g["pitch"], g["velocity"]) for g in got], worst * tempo_ms,
            "WRONG" if bad else "ok"))

    rows = timing.note_offsets(notes, TEMPO, grid=1 / 64.0, sample_rate=audio.get("sample_rate"))
    summary = timing.summarize(rows, audio.get("output_buffer"))
    text = timing.format_table("%s Phase 3 %s (%d bars, grid 1/64)" % (PLUGIN, cid, BARS), rows, summary, TEMPO, audio)
    text += "\nplug-in shows: %s" % ", ".join("%s=%s" % kv for kv in shown.items())
    text += "\n" + "\n".join(lines)
    report(text)
    print(text)
    _save("phase3-%s" % cid, {"case": cid, "shown": shown, "audio": audio, "clip": clip, "rows": rows,
                              "wrong_beats": wrong})

    assert notes, "nothing recorded"
    assert not wrong, "beats with wrong notes, times or velocities: %s" % wrong
