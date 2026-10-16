# Chordism: Phase 0 spike

A throwaway instrument plugin (AU `aumu` / VST3 / Standalone) that tests the
four host behaviours the Chordism design depends on (PLAN.md §3). The code is
not meant to be reused as is. The real project starts clean in Phase 1.

| # | Question | What the spike does |
|---|---|---|
| 1 | Can another Live track take the plugin's **VST3 MIDI output** and record it on time? | White keys play the diatonic triad of the current major key. Black keys pass through. **Test pattern** plays I IV V vi (one chord per bar, struck on every beat, 1/8 long) while the transport runs. |
| 1b | If not: does a **CoreMIDI virtual port** ("Chordism Spike Out n") record on time instead? | The same MIDI is sent out of a virtual port, stamped with the time of each event. *Port offset* shifts the stamps to calibrate. This is NoteCap's port code. |
| 2 | Can the plugin **host an AU/VST3 instrument** inside Live, and get it back when the set reopens? | *Scan instruments* → pick one → *Load*. The generated MIDI plays the hosted instrument, whose audio is the plugin's output. *Show editor* opens its window. The instrument and its settings are saved with the set. |
| 3 | Does a **dragged-out .mid** land in Live as a clip? | Records the generated MIDI since the last transport start. The drag handle exports the completed bars (32 max) as `Chordism Export.mid`. |
| 4 | Do instances **share process-wide state** (Instance Sync)? | The status panel lists every instance in the process, with its key and Sync setting. With *Sync key* on for both, changing the key on one changes the other. |

## Automated results (already run)

`Tests/run_tests.sh` drives the real processor under a fake transport, and
loads the built bundles the way a host does. **All checks pass:**

- Triads, ref-counted note-offs, and key changes while a chord is held: no stuck notes.
- Test pattern lands **within 1 sample of every beat**, with no double triggers at block edges. Tested at 48 kHz/120 BPM (blocks 256 and 333), 44.1 kHz/97 BPM (512) and 96 kHz/174 BPM (64).
- Sync between instances in one process. An unsynced instance keeps its own key.
- Export: 4.5 bars played → 4 bars, 48 notes on the quarter-note grid, 960 PPQ, tempo meta event. A new transport start resets it.
- **No heap allocations** in `processBlock`.
- Hosting: Apple's DLSMusicDevice plays, and is restored from saved state.
- **Hosted settings round-trip:** every parameter of Juno106 (AU and VST3) is changed, saved and restored into a new instance, and all 38 come back.
- **Reopening a set, as Live does it:** the built VST3 is given its saved state *before* being prepared. The hosted instrument comes back and plays. This was also run with the exact state from your `spike1_test` set, with the hosted Juno.
- A hosted instrument that **fails to restore is kept** in the saved state, so the next save doesn't drop it.
- **The virtual port** delivers the same chords, with timestamps at the block time plus the event's offset.
- **The built VST3 sends its chords as MIDI output events** to a host. Two VST3 instances share the static session.
- `auval -v aumu CzSp ARut` passes.
- *Info:* a JUCE host sees **no MIDI output from the AU instrument** build. Live ignores AU MIDI output anyway. It only matters for the Logic `aumi` target.

## Round 1 in Live 12.4.6 (2026-10-16): what the set and Live's log show

| # | Reported | Cause |
|---|---|---|
| 1 | Track 2 didn't play triads | Track 2's input was saved as **MIDI From "1-Chordism Spike" / "Post FX"**, not "/ Chordism Spike". Live never offered the plugin as a MIDI source, so track 2 got the keys from *before* the plugin. This matches vendor guidance that Live takes plugin MIDI out from **VST2** builds, not VST3 or AU. We can't build VST2 (no SDK). Next: **try the virtual port (1b)**. |
| 2 | Hosting works, settings not persisted | The hosted Juno **was** written into every save (AU and VST3, with your edits at 13:01), and restoring from those saves works outside Live. Two gaps were fixed: the plugin never told Live its state had changed, so Live didn't know the set needed saving; and a save made while the restore was still loading could drop the instrument. *Also seen:* the Juno on track 2 (a plain Live device, not hosted) had your edits in the 12:58 save but defaults again after you reopened at 12:58:34. That points at the Juno or Live, not Chordism. Round 2 tests it separately. |
| 4 | Key change not synced | In the saved set, the instance on **track 1 had Sync key *off*** (key C#), and track 2 had it on (key D). Live runs both instances in one process, so the shared state works. Each window now lists every instance with its Sync setting. |

## Round 2 in Live (2026-10-16): hosting

You reported: *the loaded instrument is not saved when the track is saved and
reloaded.* What the plugin's log and the saved `Untitled.als` show:

- 21:20:47: Live saved the set with **Juno 106-ARP (AU) hosted** in Chordism Spike (AU).
- 21:21:04: on reopen, the new instance got that state and logged *restore: Juno 106-ARP loaded*.
- 21:21:19: a Juno was loaded by hand again.

Fed the exact state Live saved, the built AU restores the Juno and it plays
(test B4). So the instrument **was** saved and reloaded. **The window just
didn't show it:** the instrument dropdown is empty after a reopen (the scan
list isn't saved), and the only sign was a small "Playing: …" label.

Fixed: after a restore, the dropdown lists and selects the restored instrument,
and the status panel has a **Hosted:** line, e.g. *Juno 106-ARP (AudioUnit),
restored from the saved set at 21:21:04, output -18.0 dB*. The output level
shows whether sound is actually coming through.

**Retest 2: passed (2026-10-16 21:42 and 21:44).** The hosted Juno 106-ARP (AU)
is saved and loaded correctly on reopen, and the log shows both restores.
Live hadn't been restarted, so the new dropdown and *Hosted:* line haven't
been seen in Live yet. They're covered by test A6 and the editor snapshot.

## Result: Phase 0 passed (2026-10-16)

Round 2 results: **1a pass** (Live lists Chordism Spike VST3 under MIDI From),
**1b pass** (the virtual port records Chordism's chords), **2 pass** (hosted
instrument saved and restored), **3 pass** (round 1), and **4 pass** (key
follows with Sync on in both).

Not yet measured: the saved recordings through the port were played by hand,
so timing against the beat wasn't checked. This moves to Phase 1 (PLAN.md §3).

## Live 12 checklist, round 2

**First, quit and restart Live:** it keeps the previous build loaded until it
restarts. Every save, restore, hosting and port event is now written to
`~/Library/Logs/ChordismSpike.log`, so I can read what happened afterwards.

### 1. MIDI to another track
- [ ] **1a, VST3 (recheck):** Track 1: **Chordism Spike (VST3)**, Monitor *In*. Track 2: an instrument. Open track 2's **MIDI From** second dropdown (below "1-Chordism Spike"). *Does it list "Chordism Spike", or only "Pre FX" / "Post FX"?*
- [ ] **1b, virtual port:** Chordism's window shows *CoreMIDI port: Chordism Spike Out 1*. In Live → Settings → **Link, Tempo & MIDI**, set the **Track** switch to On for the input **Chordism Spike Out 1**. On track 2, set **MIDI From → Chordism Spike Out 1**, Monitor *In*. Play keys: does track 2 play triads?
- [ ] Turn on **Test pattern**. Arm track 2 (Record Quantization **off**) and record 4 bars. Zoom in: is each chord exactly on the beat? If they're consistently early or late, set **Port offset** to the opposite amount, record again, and note the value that lines them up.
- [ ] Repeat at buffer sizes 128 and 1024. Does the offset change?

### 2. Hosting: does it come back?
- [ ] Chordism Spike (VST3) on a track → **Scan** → load **Juno106 (VST3)**. In the Juno window, change something you'll recognise (Chorus I on, a long attack).
- [ ] Is the set now marked as changed (asterisk on the set name / Save prompt on close)?
- [ ] **Save**, close the set (File → New), then reopen it. Does Chordism show *Playing: Juno 106-ARP*, and does the Juno still have your edits?
- [ ] Repeat with **Juno106 (AU)** hosted.
- [ ] **Juno on its own:** put Juno106 (AU) directly on a track (no Chordism), change a knob, save, reopen. Did the knob survive? (This separates a Juno issue from a Chordism one.)

### 3. Drag MIDI out: passed in round 1. Nothing to do.

### 4. Instance Sync
- [ ] Chordism Spike **VST3** on two tracks. In each window, the instance list should show both, with **Sync ON**.
- [ ] Change the key in one window: does the other window's key follow, and its output change?

### Report back
Pass or fail for each item, plus anything odd. I'll also read
`~/Library/Logs/ChordismSpike.log` and the saved set.

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
