# Chordism

A MIDI chord generator plug-in for Ableton Live on macOS: one key plays a full chord, and the
rest of the keyboard becomes a scale that follows it. A clean-room take on ChordPrism's
behaviour, for personal use. The plan, decisions and status are in [PLAN.md](PLAN.md).

## Build

Needs Xcode and JUCE 8.0.14 at `~/Development/JUCE` (with Projucer built).

```sh
PROJUCER=~/Development/JUCE/extras/Projucer/Builds/MacOSX/build/Release/Projucer.app/Contents/MacOS/Projucer
"$PROJUCER" --resave Chordism.jucer
xcodebuild -project Builds/MacOSX/Chordism.xcodeproj -target "Chordism - All" -configuration Release build
```

The build copies `Chordism.vst3` to `~/Library/Audio/Plug-Ins/VST3` and makes the Standalone app
(development). There are no AU builds (removed 2026-10-20, PLAN.md §3).

## Test

```sh
Tests/run_tests.sh          # core: theory vs music21, router, VoiceTracker, test pattern, fuzz (no JUCE)
Tests/run_snapshot.sh       # editor snapshots, every parameter reachable, state, presets, import/export
Tests/run_validation.sh     # pluginval strictness 10 on the VST3 (after a Release build)
uv run Tools/gen_theory_fixture.py   # regenerate Tests/fixtures/theory_reference.json (music21)
```

Live-in-the-loop tests are in [LiveBridge/](LiveBridge/README.md) (`tests/test_phase1_live.py`).

## In Live

- Chordism (VST3) on a MIDI track, armed or monitoring *In*, with your keyboard as its MIDI
  From; an instrument on another track with *MIDI From: Chordism track → Chordism*.
  Sample-accurate, for playing and recording.
- Keys below **Split** (default MIDI 60, Live's C3) play chords: C D E F G A B are degrees I–VII
  of the Key and Scale, shaped by the Chord Group.
