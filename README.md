# Chordism

A MIDI chord generator plug-in for Ableton Live on macOS: one key plays a full chord, and the
rest of the keyboard becomes a scale that follows it. A clean-room take on ChordPrism's
behaviour, for personal use. The plan, decisions and status are in [PLAN.md](PLAN.md).

## Build

Needs Xcode and JUCE 8.0.14 at `~/Development/JUCE` (with Projucer built).

```sh
PROJUCER=~/Development/JUCE/extras/Projucer/Builds/MacOSX/build/Release/Projucer.app/Contents/MacOS/Projucer
"$PROJUCER" --resave Chordism.jucer
"$PROJUCER" --resave MidiFX/ChordismMidiFX.jucer
xcodebuild -project Builds/MacOSX/Chordism.xcodeproj -target "Chordism - All" -configuration Release build
xcodebuild -project MidiFX/Builds/MacOSX/ChordismMidiFX.xcodeproj -target "ChordismMidiFX - All" -configuration Release build
```

The builds copy `Chordism.vst3`, `Chordism.component` (AU instrument) and
`ChordismMidiFX.component` (AU MIDI effect, for Logic) to `~/Library/Audio/Plug-Ins`.

## Test

```sh
Tests/run_tests.sh          # core: theory vs music21, router, VoiceTracker, test pattern, fuzz (no JUCE)
Tests/run_validation.sh     # auval (aumu + aumi) and pluginval strictness 10 (after a Release build)
uv run Tools/gen_theory_fixture.py   # regenerate Tests/fixtures/theory_reference.json (music21)
```

Live-in-the-loop tests are in [LiveBridge/](LiveBridge/README.md) (`tests/test_phase1_live.py`).

## In Live

- **VST3, recording:** Chordism on a MIDI track; an instrument on another track with
  *MIDI From: Chordism track → Chordism*. Sample-accurate.
- **AU instrument, or monitoring:** that track's MIDI From = **Chordism Out n** (switch its
  *Track* on in Settings → Link, Tempo & MIDI). Live records this route one audio buffer late.
- Keys below **Split** (default MIDI 60, Live's C3) play chords: C D E F G A B are degrees I–VII
  of the Key and Scale, shaped by the Chord Group.
