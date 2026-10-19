# /// script
# requires-python = ">=3.10"
# dependencies = ["music21>=9"]
# ///
"""Writes Tests/fixtures/theory_reference.json: every chord the chord zone plays, for all
12 keys x all scales x all Chord Groups x 7 degrees, computed with music21.

This is the independent reference for Tests/test_theory.cpp (PLAN.md §8). It shares no code
with Source/core/theory: scales come from music21's scale classes (or, for the scales music21
has no class for, from their textbook interval names), and each group is written as scale
steps above the chord root rather than as Chord Editor steps and octave masks.

    uv run Tools/gen_theory_fixture.py
"""

import json
import os
import sys

import music21
from music21 import pitch, scale

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "Tests", "fixtures", "theory_reference.json")

BASE_MIDI = 48                       # C of the chord output's default octave
KEY_SPELLINGS = ["C", "C#", "D", "E-", "E", "F", "F#", "G", "A-", "A", "B-", "B"]


def from_intervals(*names):
    """A scale music21 has no class for, from the interval names between its degrees."""
    def build(tonic):
        pitches = [tonic]
        for n in names:
            pitches.append(pitches[-1].transpose(n))
        return scale.ConcreteScale(pitches=pitches)
    return build


# Same names and order as Source/core/theory/Scales.h.
SCALES = [
    ("Major", scale.MajorScale),
    ("Natural Minor", scale.MinorScale),
    ("Harmonic Minor", scale.HarmonicMinorScale),
    ("Melodic Minor", scale.MelodicMinorScale),      # walked upwards only: the ascending form
    ("Dorian", scale.DorianScale),
    ("Phrygian", scale.PhrygianScale),
    ("Lydian", scale.LydianScale),
    ("Mixolydian", scale.MixolydianScale),
    ("Locrian", scale.LocrianScale),
    ("Harmonic Major", from_intervals("M2", "M2", "m2", "M2", "m2", "A2", "m2")),
    ("Phrygian Dominant", from_intervals("m2", "A2", "m2", "M2", "m2", "M2", "M2")),
    ("Hungarian Minor", from_intervals("M2", "m2", "A2", "m2", "m2", "A2", "m2")),
    ("Double Harmonic", from_intervals("m2", "A2", "m2", "M2", "m2", "A2", "m2")),
]

# Each group as scale steps above the chord root (0 = root, 2 = third, 7 = root an octave up,
# negative = below the root). Same names and order as Source/core/theory/ChordGroups.h.
GROUPS = [
    ("Triads", [0, 2, 4]),
    ("Power (1-5)", [0, 4, 7]),
    ("Sus2", [0, 1, 4]),
    ("Sus4", [0, 3, 4]),
    ("6ths", [0, 2, 4, 5]),
    ("Sevenths", [0, 2, 4, 6]),
    ("Add9", [0, 2, 4, 8]),
    ("Ninths", [0, 2, 4, 6, 8]),
    ("Elevenths", [0, 2, 4, 6, 8, 10]),
    ("Thirteenths", [0, 2, 4, 6, 8, 12]),
    ("1st Inversion", [2, 4, 7]),
    ("2nd Inversion", [4, 7, 9]),
    ("Open Triads", [0, 4, 9]),
    ("Drop-2 Sevenths", [-3, 0, 2, 6]),
    ("Shell (1-3-7)", [0, 2, 6]),
    ("Neo-Soul", [0, 2, 6, 8]),
    ("Quartal", [0, 3, 6, 9]),
]

LOWEST, HIGHEST = -7, 27             # scale indices needed: degree 0..6 plus steps -3..12 (+ margin)


def ladder(scale_name, build, key):
    """MIDI note of every scale index from LOWEST to HIGHEST, walking music21's scale upwards."""
    tonic = pitch.Pitch(KEY_SPELLINGS[key])
    tonic.octave = 3                 # music21: C4 = MIDI 60, so C3 = 48
    assert tonic.midi == BASE_MIDI + key, (tonic, tonic.midi)
    sc = build(tonic)
    p = tonic.transpose("-P8")
    notes = {LOWEST: p.midi}
    for i in range(LOWEST + 1, HIGHEST + 1):
        p = sc.nextPitch(p, direction=scale.Direction.ASCENDING, stepSize=1)
        notes[i] = p.midi
    return notes


def default_accidental(scale_name, degree, steps_above_root):
    """The scale's default accidentals (PLAN.md §4.2): natural minor raises the 3rd of V
    (the leading tone), so V comes out major."""
    if scale_name == "Natural Minor" and degree == 4 and steps_above_root % 7 == 2:
        return 1
    return 0


def main():
    scales_out, chords_out = [], []
    for name, build in SCALES:
        lad = ladder(name, build, 0)
        scales_out.append({"name": name, "semitones": [lad[i] - BASE_MIDI for i in range(7)]})

    for key in range(12):
        for name, build in SCALES:
            lad = ladder(name, build, key)
            for group, shape in GROUPS:
                chords = []
                for degree in range(7):
                    notes = sorted(lad[degree + s] + default_accidental(name, degree, s) for s in shape)
                    chords.append(notes)
                chords_out.append({"key": key, "scale": name, "group": group, "chords": chords})

    doc = {
        "generator": "Tools/gen_theory_fixture.py, music21 %s" % music21.__version__,
        "base_midi": BASE_MIDI,
        "keys": KEY_SPELLINGS,
        "scales": scales_out,
        "groups": [g for g, _ in GROUPS],
        "chords": chords_out,
    }

    # One chord set per line keeps the file diffable.
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("{\n")
        for k in ("generator", "base_midi", "keys", "groups"):
            f.write('  %s: %s,\n' % (json.dumps(k), json.dumps(doc[k])))
        f.write('  "scales": [\n')
        f.write(",\n".join("    " + json.dumps(s) for s in scales_out))
        f.write('\n  ],\n  "chords": [\n')
        f.write(",\n".join("    " + json.dumps(c) for c in chords_out))
        f.write("\n  ]\n}\n")
    print("wrote %d chord sets (%d chords) to %s" % (len(chords_out), 7 * len(chords_out), os.path.relpath(OUT)),
          file=sys.stderr)


if __name__ == "__main__":
    main()
