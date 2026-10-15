# Chordism: Phase 0 spike

A throwaway instrument plugin (AU `aumu` / VST3 / Standalone) that tests the
four host behaviours the Chordism design depends on (PLAN.md §3). The code is
not meant to be reused as is. The real project starts clean in Phase 1.

| # | Question | What the spike does |
|---|---|---|
| 1 | Can another Live track take the plugin's **VST3 MIDI output** and record it on time? | White keys play the diatonic triad of the current major key. Black keys pass through. **Test pattern** plays I IV V vi (one chord per bar, struck on every beat, 1/8 long) while the transport runs. |
| 2 | Can the plugin **host an AU/VST3 instrument** inside Live? | *Scan instruments* → pick one → *Load*. The generated MIDI plays the hosted instrument, whose audio is the plugin's output. *Show editor* opens its window. The choice is saved with the set. |
| 3 | Does a **dragged-out .mid** land in Live as a clip? | Records the generated MIDI since the last transport start. The drag handle exports the completed bars (32 max) as `Chordism Export.mid`. |
| 4 | Do instances **share process-wide state** (Instance Sync)? | The status panel shows the process ID and how many instances of this binary are in the process. With *Sync key* on, changing the key on one instance changes it on all of them. |

## Automated results (already run)

`Tests/run_tests.sh` drives the real processor under a fake transport, and
loads the built bundles the way a host does. **All checks pass:**

- Triads, ref-counted note-offs, and key changes while a chord is held: no stuck notes.
- Test pattern lands **within 1 sample of every beat**, with no double triggers at block edges. Tested at 48 kHz/120 BPM (blocks 256 and 333), 44.1 kHz/97 BPM (512) and 96 kHz/174 BPM (64).
- Sync between instances in one process. An unsynced instance keeps its own key.
- Export: 4.5 bars played → 4 bars, 48 notes on the quarter-note grid, 960 PPQ, tempo meta event. A new transport start resets it.
- **No heap allocations** in `processBlock`.
- Hosting Apple's DLSMusicDevice: silent with no notes, audible with a chord, restored from saved state.
- **The built VST3 sends its chords as MIDI output events** to a host. This is what Live's "MIDI From" routing reads. Two VST3 instances share the static session.
- `auval -v aumu CzSp ARut` passes.
- *Info:* a JUCE host sees **no MIDI output from the AU instrument** build. Live ignores AU MIDI output anyway. It only matters for the Logic `aumi` target, which gets its own check in Phase 1.

## Live 12 checklist (only Live can answer these)

Before you start: Live → Settings → **Plug-Ins**: turn on *Use Audio Units v2*
and *Use VST3 Plug-In System Folders*, then **Rescan**. The plugin appears
under Plug-Ins → *Alistair Rutherford* → **Chordism Spike**. If it's missing,
run `killall -9 AudioComponentRegistrar` and rescan.

### 1. VST3 MIDI output → another track (the main routing route)
- [ ] Track 1 (MIDI): load **Chordism Spike (VST3)**. Set Monitor to *In* so it receives your controller.
- [ ] Track 2 (MIDI): load any instrument (e.g. Drift). Set **MIDI From → "1-MIDI" → "Chordism Spike"**. *Pass if the plugin is offered in the second dropdown.* Set Monitor to *In*.
- [ ] Play white keys on the controller: track 2 plays triads.
- [ ] Turn on **Test pattern**. Arm track 2 (Record Quantization **off**), then record 4 bars. *Pass if* the clip has C F G Am, one chord per bar, with **every chord starting exactly on the beat** (zoom in) and 1/8 long.
- [ ] Repeat with a different buffer size (Settings → Audio → Buffer Size, e.g. 128 and 1024). Is it still exactly on the beat?
- [ ] Record on **track 1** instead: you should get only the keys you pressed (the trigger notes), not the chords. That's the expected ChordPrism-style behaviour.

### 2. Hosting an instrument inside the plugin
- [ ] On a new MIDI track, load **Chordism Spike (AU)**. Click **Scan instruments**. *Note:* how long it took, and whether anything crashed.
- [ ] Choose **Juno106 (AudioUnit)** → **Load**. Play keys: you should hear triads through the Juno.
- [ ] **Show editor**: does the Juno window open, appear in front of Live, and work?
- [ ] Save the set, quit Live, reopen it. Is the Juno still loaded, with the same sound settings?
- [ ] Repeat with **Chordism Spike (VST3)** hosting **Juno106 (VST3)**.

### 3. Drag MIDI out
- [ ] With Test pattern on, play at least 2 full bars, then stop. The handle reads *Drag MIDI out (N completed bars)*.
- [ ] Drag it onto an empty MIDI track in **Arrangement view**. *Pass if* a clip appears at the drop point with chords on the beats and the right length.
- [ ] Drag it into a **Session view** clip slot as well.

### 4. Instance Sync
- [ ] Load Chordism Spike **VST3** on two tracks and open both windows. Are the **PIDs the same**, and does each show *instances … in this process: 2*?
- [ ] Change the key on one: does the other's key follow, and does its output change?
- [ ] Mixed formats (one AU, one VST3): expected **not** to sync, because they are separate binaries. Note what the status shows.

### Report back
For each section: pass or fail, plus anything odd (e.g. "chords 3 ms late at
buffer 1024", "Juno editor opens behind Live"). Those results decide the
routing approach and update PLAN.md §3.

## Building

Needs Xcode and a JUCE 8 checkout at `~/Development/JUCE`.

```sh
~/Development/JUCE/extras/Projucer/Builds/MacOSX/build/Release/Projucer.app/Contents/MacOS/Projucer \
    --resave ChordismSpike.jucer
xcodebuild -project Builds/MacOSX/ChordismSpike.xcodeproj -target "ChordismSpike - All" -configuration Release build
auval -v aumu CzSp ARut
Tests/run_tests.sh
```

The build copies the plugins to `~/Library/Audio/Plug-Ins/{Components,VST3}`.
Delete `ChordismSpike.component` and `ChordismSpike.vst3` from there once
Phase 0 is done.
