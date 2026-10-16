# Chordism: Project Plan

A macOS MIDI-generating plugin, aimed first at Ableton Live, that reproduces the functionality of
**ChordPrism 2.1**. One key press plays a full chord. The rest of the keyboard turns into a
"smart" scale that follows the current chord, with arpeggiators, melody generators, step
sequencers and velocity grooves. Chordism makes no sound of its own. It sends MIDI to an
instrument, or plays an instrument plugin that it hosts.

Sources for the feature set: the ChordPrism setup page, the features page and the *ChordPrism 2.1
User's Guide* (12 pages). Chordism is a **clean-room implementation**. It copies behaviour only:
no code, artwork, preset files, manual text or product name come from ChordPrism.

---

## 1. Goals

| # | Requirement | Acceptance criterion |
|---|-------------|----------------------|
| R1 | Loads in Live (and other hosts) and sends MIDI to an instrument | Passes `auval` and `pluginval` (strictness 10). In Live, an instrument on another track plays Chordism's output and can record it. |
| R2 | **Chord Generator:** one key plays the diatonic chord for the chosen Key, Scale and Chord Group | Unit tests: every white key gives the correct chord in all 12 keys × all scales × all chord groups. |
| R3 | **Chord Editor:** each chord can be edited step by step (tone on/off, accidental, octave stacking) | Every chord in a reference set (triads to 13ths, altered chords, inversions, drop voicings) can be built in the editor, and the output matches. |
| R4 | **Chord effects:** strum, arp, repeat, hold, octave, stack, velocity algorithm, step sequencer | Events land sample-accurately on the host grid (fake-transport tests, including loops and tempo changes). No stuck notes in any test. |
| R5 | **Smart Scale + Pattern Generators:** five mappings, five arp/melody pattern slots, note repeat, look-ahead transposing | Mapping tables match the spec in §4.3. Patterns follow chord changes in real time. |
| R6 | **MIDI import/export:** drag a chord MIDI file in to make a preset; drag the generated MIDI out (up to 32 bars) | Round trip: export → import gives the same chords. Dropping the exported file into Live gives a clip that is identical to the live output. |
| R7 | **Presets, Instance Sync, VST/AU hosting, preferences** | Presets save and restore every setting. Two instances share Key, Scale and Chord. A hosted instrument plays and its state is saved with the project. |
| R8 | Real-time safe and robust | No allocations or locks on the audio thread. All-notes-off on stop, bypass, preset change and deletion. Works at 32–2048 sample buffers. |

**Out of scope for v1:** Windows, AAX/Pro Tools, iOS/AUv3, licensing and copy protection, and an
online preset store.

---

## 2. Feature map (ChordPrism → Chordism)

Priority: **P1** is the core instrument (Phases 1–5). **P2** finishes ChordPrism parity
(Phases 6–7). **P3** is beyond ChordPrism, or optional.

| Area | ChordPrism feature | Chordism plan | Pri |
|---|---|---|---|
| Global | Key (12), Scale (7-tone scales) | Same. A scale table with default accidentals for each scale (e.g. a raised 7th on V in minor) | P1 |
| Global | Chord Progression (orders the chords on the white keys) | Progression presets plus a custom order of 7 slots | P1 |
| Global | Chord Groups (chord-type families; reset editor edits) | Factory group table (§4.2) | P1 |
| Global | Mapping: Chord Tones, Dynamic, Relative, Absolute, Chromatic | Same five (§4.3) | P1 |
| Chord Gen | White keys = 7 chords, with Roman numeral and chord name display | Same, plus right-click menus to change the degree and the chord type | P1 |
| Chord Gen | Black keys = shifters: Chord Groups **or** Key/Scale; Toggle or Standard | Same. 5 shifter slots, each a target value chosen by right-click | P1 |
| Chord Gen | Strum (Up, Down, Alternating, Random) + Strum Rate | Same | P1 |
| Chord Gen | Arp: 12 patterns, Range 1–3, Gate 0–100% | Same | P1 |
| Chord Gen | Repeat (rate) + Repeat Gate | Same | P1 |
| Both | Octave, Stack (+1, +2, +1&2), Hold, Mute | Same | P1 |
| Both | Velocity Algorithm (13 curves) + Peak + Range | Same (curves defined in §4.5) | P1 |
| Both | Step Sequencer: 16 steps, 4 pattern slots, Swing, Gate, Sustain | Same, plus per-step velocity (P3) | P1 |
| Chord Editor | Per chord, per step: On/Off, Accidental, Octave (stackable) | 7 chords × 7 steps × {on, accidental, octave mask} | P1 |
| Chord Editor | Active Chord dropdown (choose the chord type for a root) | Same | P1 |
| Chord Gen | Voice leading (on the features page) | "Voice leading: Off / Smooth" (picks the inversion with the least movement) | P2 |
| Smart Scale | Scale-tone letters on the keys, chord tones highlighted | Same | P1 |
| Smart Scale | Inversions button (keep tones within one octave) | Same | P1 |
| Smart Scale | Note Repeat (right-click a white key) | Same, using Arp Rate and Arp Gate | P1 |
| Smart Scale | Look Ahead Scale Transposing (once a loop is recorded) | Learned chord timeline (§4.4) | P2 |
| Patterns | 5 black keys = pattern slots, each with its own settings | Same | P1 |
| Patterns | Arp: Pattern, Rate (9 values), Range 1–3, Arp Chord (5ths … 13ths), Gate | Same | P1 |
| Patterns | Melody: Notes per bar, Rhythm, Swing, Gate | Seeded generative melody (§4.4) | P2 |
| I/O | MIDI Chord Drag & Drop in | Chord recognition → Key/Scale + editor steps (§4.6) | P2 |
| I/O | Export: drag out the last ≤32 completed bars | Same | P2 |
| I/O | Enable/Disable (pass input MIDI through unchanged) | Same | P1 |
| I/O | MIDI CC control of parameters | All parameters are host parameters (APVTS) | P1 |
| I/O | Computer-keyboard (QWERTY) playing | On-screen keyboard plus a QWERTY map in the editor | P2 |
| Presets | Save, Open, Navigate; genre, artist, vibe and user presets | Same, with our own factory library (≥ 60 at launch) | P2 |
| Hosting | Host a VST instrument inside the plugin, a library, scanning, drag-to-library | AU + VST3 hosting, out-of-process scanning | P2 |
| Multi | Instance Sync (Key, Scale, Chord shared by all instances) | Process-wide shared session with sync groups | P2 |
| Prefs | Tooltips, keyboard display, colours, shifter mode, Toggle/Standard | Same, stored per user | P2 |
| Extra | — | Separate MIDI output channel for Chord and Smart Scale parts | P3 |
| Extra | — | CoreMIDI virtual output port (reusing NoteCap's `VirtualMidiOut`) | P3 |

---

## 3. Host constraints and plugin formats

NoteCap established that **Live does not host AU MIDI effects (`aumi`) and drops MIDI output from
AU plugins.** ChordPrism handles this by shipping a VST instrument and by hosting instruments
itself. Chordism does the same:

| Build | Type | Used in | How MIDI reaches the instrument |
|---|---|---|---|
| **VST3 instrument** (primary for Live) | Instrument with MIDI in/out, silent stereo out | Live, Bitwig, Cubase, Studio One, Reaper, FL | On a separate track. The destination track uses *MIDI From: Chordism track → Chordism*. Live records what arrives at the destination's input, which is the generated MIDI. |
| **AU instrument** (`aumu`) | Instrument with MIDI out, audio out from the hosted plugin | GarageBand, Live (hosting route), Maschine, etc. | Plays an instrument hosted inside Chordism. |
| **AU MIDI effect** (`aumi`) | MIDI effect | Logic Pro | Inserted in the MIDI FX slot, before the instrument. |
| Standalone | App | Development and debugging | Outputs to a hosted plugin or a MIDI device. |

JUCE needs a **separate Projucer target** for `aumi` (`isMidiEffect` is set at compile time).
It shares all of `Source/`, so this is a second `.jucer` file rather than separate code.

**Recording, as in ChordPrism.** By default, the Chordism track records the **trigger keys**,
so every parameter can still be changed after recording. The generated notes can be recorded
by (a) recording the destination track, or (b) Export drag-out.

### Phase 0 must confirm, in the user's Live version
1. VST3 MIDI output can be selected under "MIDI From" on another track, and it records sample-accurately.
2. JUCE's `AudioPluginFormatManager` can host AU and VST3 instruments inside the Chordism AU/VST3 in Live, and the hosted plugin's editor window works.
3. A file dragged out with `performExternalDragDropOfFiles` lands in Live as a MIDI clip at the drop point.
4. Two instances in one Live set share a process-wide static (Instance Sync). Live does not sandbox plugins by default, but Bitwig and Logic may run plugins in separate processes.

If (1) fails, the fallback is NoteCap's CoreMIDI virtual port (already built and tested), with
hosting as the recommended route.

---

## 4. Functional design

### 4.1 Keyboard layout

A **Split** parameter (default MIDI 60, which Live calls *C3*) divides incoming MIDI:

```
 ◄──────── Chord zone (below split) ───────►◄─────────── Smart Scale zone (split and above) ───────────►
  white C D E F G A B → progression slots 1–7    white keys → scale tones, depending on the Mapping (§4.3)
  black C# D# F# G# A# → shifter slots 1–5       black C# D# F# G# A# → pattern slots 1–5 (arp/melody)
  (the octave doesn't matter in this zone)        (white keys climb in octaves; black keys repeat the 5 slots)
```

- **Chromatic** mapping: the Smart Scale zone passes notes through unchanged and has no pattern keys.
- **Mute CG** and **Mute SS** silence each zone's output. This gives ChordPrism's multi-instance roles, e.g. instance 1 = chords, instance 2 = bass via the Smart Scale.
- Optional **output channel per zone** (P3), so one instance can drive two instruments.

### 4.2 Chord model (Chord Generator + Chord Editor)

Each chord is a **stack of thirds on a scale degree**, which is the same model as ChordPrism's
editor:

```
ChordSlot  { degree 0..6 ; Step steps[7] }               // steps = 1, 3, 5, 7, 9, 11, 13
Step       { bool on ; int8 accidental (-2..+2) ; uint8 octaveMask (bits for -2..+2) }

scaleIndex(k) = degree + 2k
pitch(k, o)   = keyRoot + scale[scaleIndex(k) mod 7] + 12·⌊scaleIndex(k)/7⌋
                + accidental[k] + 12·o + 12·outputOctave        for each o in octaveMask
```

- **Chord Groups** fill in `on` and `octaveMask` for all 7 slots. Factory groups (our own list):
  Triads, Power (1-5), Sus2, Sus4, 6ths, Sevenths, Add9, Ninths, Elevenths, Thirteenths,
  1st Inversion, 2nd Inversion, Open (spread) Triads, Drop-2 Sevenths, Shell (1-3-7), Neo-Soul
  (9ths without the 5th), Quartal. Changing the group resets the editor edits, as ChordPrism does.
- **Scale default accidentals:** a per-scale table, such as the harmonic-minor V and the melodic-minor IV, so that common chords in each scale come out right without editing.
- **Chord naming:** a pitch-class-set → name recogniser (root, quality, extensions, slash bass). The Roman numeral follows ChordPrism's style: upper case for major, lower case for minor, `*` for diminished, `+` for augmented.
- **Active Chord dropdown / right-click chord name:** a list of chord types for that root. Choosing one rewrites the steps (on and accidental) for that slot.
- **Voice leading (P2):** when Smooth is on, each new chord picks the inversion and octave whose notes are closest to the previous chord (minimum total semitone distance), anchored to the Output Octave.
- **Shifters:** five slots, each holding a target value (a Chord Group, or a Key/Scale). Standard = active while held. Toggle = stays active until another shifter is pressed. Shifting while a chord is held retriggers it.

### 4.3 Smart Scale mappings

Let *T* be the key tonic and *R* the root of the current chord (the most recently played chord slot).

| Mapping | White keys from the split upward (C, D, E, F, G, A, B, then the next octave) |
|---|---|
| **Chord Tones** | Scale tones counted from *R* in the order 1-3-5-7-2-4-6. Chord tones fall under the fingers and move with every chord change. |
| **Dynamic Scale** | Scale from *R*: 1-2-3-4-5-6-7 (the mode of the current chord), with *R* on C. |
| **Relative Scale** | Scale from *T*: 1-2-3-4-5-6-7, with *T* on C. Static. |
| **Absolute Scale** | Each white-key letter gives that letter's tone in the scale at its real pitch (e.g. in D major, F→F♯). Static. |
| **Chromatic** | Pass-through. |

- **Inversions button** (Chord Tones and Dynamic only): fold the tones into one octave above *R*, instead of stacking them upward. As ChordPrism notes, a lower pitch can then sit to the right of a higher one.
- Accidentals from the active chord's Chord Editor steps apply to the Smart Scale only when that step is on, as in ChordPrism.
- The UI shows the tone letter on each key, with chord tones marked by a coloured edge.

### 4.4 Pattern generators and look-ahead

Each of the five pattern slots stores `{mode: Arp | Melody, …settings}`. Pressing a pattern key
starts the pattern over the **current chord**. When the chord changes, the pattern follows it
at the next step without restarting.

- **Arp:** Pattern (the 12 directions, as in the chord arp: Linear, Converge, Diverge, Stutter, Zigzag, Boomerang × Up/Down), Rate (1/2, 1/4, 3/16, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T), Range 1–3 octaves, Arp Chord (5ths, Triads, 7ths, 9ths, 11ths, 13ths; this sets how many stacked chord tones are used), Gate 0–100%.
- **Melody (P2):** Notes per bar (1–16), Rhythm (a template from a factory table: straight, syncopated, dotted, clave, offbeat, …), Swing, Gate, and a Seed. Generation is deterministic for a given seed + chord + bar. Strong beats take chord tones and weak beats take scale passing tones, with stepwise motion favoured and a range limit.
- **Note Repeat:** right-clicking a Smart Scale white key turns on repeat for all Smart Scale tones, using Arp Rate and Arp Gate.
- **Look-Ahead Scale Transposing (P2):** while the transport loops, the processor records when each chord slot changes (in PPQ) into a `ChordTimeline`, building a map of the loop. On later passes, if a Smart Scale note arrives within the **look-ahead window** (default 1/32, adjustable) before a learned chord change, it uses the *upcoming* chord. The timeline is cleared when the loop points change, or when a learned change doesn't happen.

### 4.5 Timing and effects engine

All rhythmic effects run against the **host transport** (`AudioPlayHead`: PPQ position, BPM,
time signature, loop points, isPlaying). When the transport is stopped, an internal clock runs
at the last known tempo, so strum, arp and repeat still work while auditioning.

Order of the effects in each chain:

```
Chord zone:  trigger → ChordSlot → voicing (+voice leading) → Octave → Stack → [Strum | Arp] → Repeat → StepSeq gate → Velocity Algorithm → Hold → Mute → out
Smart zone:  trigger → Mapping → tone (+look-ahead)  → [Pattern | Note Repeat] → Octave → Stack → StepSeq gate → Velocity Algorithm → Hold → Mute → out
```

- **Strum:** Up, Down, Alternating, Random. Strum Rate is the time between tones (a set of fixed note values, e.g. 1/128 to 1/16, plus milliseconds when stopped). Strummed tones are held for the whole chord.
- **Repeat:** Rate (as for Arp Rate) and Gate 0–100%. Retriggers while the key is held.
- **Hold:** keeps the selection playing after the key is released. It ends on a new selection, at the end of the bar, or on transport stop. When stopped, pressing the same key again ends it.
- **Stack:** adds copies of all tones +1 octave, +2 octaves, or both.
- **Step Sequencer:** 16 steps of 1/16, 4 pattern memories, Swing (pushes the even steps later), Gate (full = one 1/16, or with **Sustain** on, up to the 1/16 before the next active step). Factory genre patterns.
- **Velocity Algorithm** (with the sequencer engaged, for the chord zone): replaces the input velocity with a curve value at the event's position in the bar. Curves are 16-step tables scaled by **Peak** and **Range**. Afrobeat, Breakbeat, Dancehall, House and Moombahton are our own accent patterns. Up, Down, Up½, Down½, Up¼, Down¼, Up/Down and Down/Up are linear.

**Note bookkeeping (most important for correctness):** a `VoiceTracker` records which output
notes each source owns. A source is an input key, a pattern slot, a held chord or a sequencer
step. Every note-off releases exactly the notes that source started, even if Key, Scale,
Mapping or Octave changed while it was held. Duplicate pitches from different sources are
reference-counted, so one source's note-off doesn't cut another source's note.

**Scheduling:** a fixed-capacity, sample-timed event queue (allocated in `prepareToPlay`) holds
future note-ons and note-offs across block boundaries. Loop wrap-around and tempo changes cause
the musical-time events in the queue to be recalculated.

### 4.6 MIDI import and export

- **Import (drag a `.mid` onto the UI):** group the notes into chord events (onsets within a tolerance). Keep up to 7 distinct chords in order of first appearance. Choose the Key and Scale whose diatonic chords explain the imported chords with the fewest accidentals. Then encode each chord as degree + steps + accidentals + octave masks, so the exact voicing is kept. The progression is set to match the import order. The white keys light up in sequence to confirm, as in ChordPrism.
- **Export (drag-out handle):** a recorder on the output side keeps the generated MIDI since playback started, in PPQ, in a preallocated buffer of 32 bars. Only **completed** bars are exported. The handle is disabled until the first bar has finished. Dragging writes a temporary `.mid` file (type 1, 960 PPQ, tempo and time-signature meta events) and calls `DragAndDropContainer::performExternalDragDropOfFiles`.

### 4.7 Presets, sync, hosting and preferences

- **Presets:** XML files in `~/Music/Chordism/Presets/<Category>/`, using the same ValueTree as the plugin state. Categories are Genre, Artist-style (described by style only, never by artist name), Vibe and User. The browser has previous/next buttons and a search box. Factory presets are installed by the `.pkg`.
- **Instance Sync:** a process-wide `SharedSession` holds `{key, scale, activeChordSlot, chordData, ppqStamp}` behind an atomic, versioned snapshot. Instances in the same **sync group** (Off, A, B, C, D) publish and subscribe. Each published chord carries its PPQ time, so an instance that processes earlier in the block can still apply the change at the right sample. If there is a one-block delay, the look-ahead logic covers it.
- **Hosting:** JUCE `AudioPluginFormatManager` with AU and VST3, a `KnownPluginList` that is saved to disk, and scanning in a **separate scanner process** so a crashing plugin doesn't take down the DAW (ChordPrism suggests drag-to-library as a workaround for this). Plugin files can be dropped onto the library. The hosted plugin's state is saved inside Chordism's state. A pop-up button opens its editor. Hosted-plugin parameters are reachable through 8 macro parameters (P3).
- **Preferences** (opened from the logo): tooltips, show/hide the keyboard display, colour theme, shifter target (Chord Groups or Key/Scale), Toggle/Standard, look-ahead window, and the QWERTY layout.

---

## 5. Architecture

Matches the sibling projects: **JUCE 8.0.14, C++17, Projucer + xcodebuild**, with JUCE checked
out at `~/Development/JUCE`. No CMake or Homebrew.

```
           MIDI in ─► InputRouter ─┬─► ChordZone ─► ChordEngine ─► ChordFxChain ──┐
 (host or QWERTY / on-screen key)  │      ▲ shifters                              ├─► VoiceTracker ─► EventQueue ─► MIDI out
                                   └─► SmartZone ─► ScaleMapper ─► PatternEngine ─┘                         │       └─► HostedInstrument ─► audio out
                                          ▲ current chord / look-ahead                                      └─► ExportRecorder
         Transport (AudioPlayHead) ─► Clock ─► all timed components         SharedSession ◄─► other instances
```

### Modules

| Module | Responsibility |
|---|---|
| `Source/core/theory/` (no JUCE) | `Scale`, `ScaleTable`, `ChordSlot`, `ChordGroups`, `Voicing`, `VoiceLeading`, `ChordNamer`, `RomanNumeral`, `Progressions` |
| `Source/core/engine/` (no JUCE) | `InputRouter`, `ChordEngine`, `Shifters`, `ScaleMapper`, `PatternEngine` (arp + melody), `Strum`, `Repeat`, `Arp`, `StepSequencer`, `VelocityAlgorithm`, `Hold`, `Stack`, `VoiceTracker`, `EventQueue`, `Clock`, `ChordTimeline` |
| `Source/core/midiio/` (no JUCE) | `MidiFileReader/Writer` wrappers, `ChordImporter` (chord recognition + key fit), `ExportRecorder` |
| `Source/plugin/` | `ChordismProcessor`, APVTS parameter layout, state (APVTS + ValueTree for the editor matrix, patterns and sequencer data), `SharedSession`, `PresetManager` |
| `Source/hosting/` | `PluginLibrary`, out-of-process `ScannerApp`, `HostedInstrument` (prepare, process, state, editor window) |
| `Source/ui/` | `ChordismEditor`, `KeyboardDisplay` (split, labels, highlights), `ChordEditorPanel` (7×7 grid), `ChordFxPanel`, `SmartScalePanel`, `PatternPanel`, `StepSeqView`, `PresetBar`, `HostBar`, `ExportHandle`, `PrefsOverlay`, `LookAndFeel` |
| `Tests/` | Plain C++ test executables run by `run_tests.sh` and `run_integration.sh`, as in NoteCap, plus an editor snapshot |

### Parameters vs. state
- **Host parameters (APVTS, automatable, MIDI-CC learnable):** Key, Scale, Progression, Chord Group, Mapping, Split, Mutes, Enable, every effect setting for both zones, and the active pattern's settings. About 80 parameters.
- **ValueTree state (not automated):** the 7×7 chord-editor matrix, 5 pattern slots × their settings, 2 × 4 sequencer patterns, shifter targets, hosted-plugin state, and UI preferences.

### Real-time safety rules
- `processBlock` never allocates, locks, logs or does file I/O. All buffers are allocated in `prepareToPlay`.
- Changes from the UI and from `SharedSession` reach the audio thread as lock-free snapshots (double-buffered and swapped atomically).
- Export, preset loading and MIDI import happen on the message thread. The results are handed over as new snapshots.
- All-notes-off (through `VoiceTracker`) on: transport stop, Enable off, preset load, Key/Scale change with notes held (retrigger), `releaseResources`, and destruction.

---

## 6. UI (v1)

Resizable, about 1100×640 by default. Our own visual design; ChordPrism's look is not copied.

- **Top bar:** logo (opens Preferences) · Enable · Preset browser · Key · Scale · Progression · Chord Group · Mapping · Sync group · Hosted instrument + pop-up · Export drag handle.
- **Left half, Chord Generator:** the chord-zone keyboard with the chord name and Roman numeral on each white key, a stripe on the last-played key, and shifter labels on the black keys. Below it, the **Chord FX** panel. A gear button swaps it for the **Chord Editor** grid (7 chords × 7 steps: on/off, accidental, octave stack).
- **Right half, Smart Scale:** the scale-zone keyboard with tone letters, chord-tone edges and pattern names on the black keys. Below it, the **Pattern** panel for the selected pattern slot, and the Smart Scale FX.
- **Bottom:** a full keyboard display showing played output notes (can be hidden). It also works as an on-screen keyboard and responds to the QWERTY keyboard.
- Right-click menus throughout: Roman numeral → degree; chord name → chord type; shifter key → target; pattern key → make active; white SS key → Note Repeat.

---

## 7. Milestones

| Phase | Duration | Deliverable | Exit criteria |
|---|---|---|---|
| **0. Feasibility spikes** | 3–4 days | Throwaway JUCE VST3/AU | The four checks in §3 pass or fail in the user's Live version. Routing approach chosen. |
| **1. Skeleton + theory core** | 1.5 weeks | `.jucer` targets (VST3, AU instrument, Standalone; `aumi` second project), `core/theory`, `InputRouter`, plain chord output, `VoiceTracker` | Live plays C-major-scale chords from the chord zone through the chosen route. Theory unit tests pass for all 12 keys × all scales × all groups. `auval` and `pluginval` pass. |
| **2. Chord Generator complete** | 1 week | Chord Editor model, Chord Groups, Progressions, Shifters (both modes), chord naming and Roman numerals | Reference-chord test set (R3) passes. Fuzzing parameters while notes are held gives no stuck notes. |
| **3. Timing engine + chord FX** | 2 weeks | `Clock`, `EventQueue`, Strum, Arp, Repeat, Hold, Octave, Stack, Velocity Algorithm, Step Sequencer | Fake-transport integration tests: sample-accurate at 32–2048 sample buffers and 44.1/48/96 kHz, through loop wraps and tempo changes. Recording test in Live passes. |
| **4. Smart Scale + patterns** | 2 weeks | 5 mappings, Inversions, Note Repeat, Pattern Engine (arp), SS FX chain | Mapping tables match §4.3 exactly (table tests). Patterns follow chord changes without restarting. |
| **5. UI v1** | 2–3 weeks | Editor from §6 | Every feature reachable. State restores after a Live set is reopened. Snapshot test. |
| **6. Melody, I/O, presets** | 2 weeks | Melody generator, MIDI import, Export drag-out, Preset manager + ≥ 60 factory presets, QWERTY | R6 round-trip test. Melody output is identical for the same seed. Export drops into Live as a correct clip. |
| **7. Sync, look-ahead, hosting** | 2 weeks | `SharedSession`, `ChordTimeline` look-ahead, voice leading, plugin hosting + out-of-process scanner | Two instances follow each other within one block. Look-ahead test on a looped transport. A hosted AU/VST3 synth plays and is restored with the project. |
| **8. Hardening + release** | 1 week | Edge cases, performance pass, universal binary, signed `.pkg` (adapt NoteCap's `Tools/package.sh`) | 8 instances under 1% CPU each (without hosted instruments). `pluginval` strictness 10 in CI. Clean install on a fresh Mac. |

**Total: about 14–17 weeks** for one developer. Phases 1–5 (about 9 weeks) give a usable
instrument that covers the core of ChordPrism.

---

## 8. Testing strategy

- **Theory tables:** every Key × Scale × Chord Group × degree is checked against a reference (generated independently, e.g. with `music21` offline into a JSON fixture).
- **Engine tests with a fake transport:** a scripted sequence of MIDI and transport events (play, stop, loop, tempo change, time-signature change) is checked against the expected output, sample-accurately. This covers every effect and all combinations of the effect chain.
- **Stuck-note fuzzing:** random notes, parameter changes, preset loads and transport changes over thousands of blocks. At the end, `VoiceTracker` must be empty and every note-on must have a matching note-off.
- **Determinism:** melody and random strum use a seeded PRNG, so the same input gives the same output.
- **MIDI I/O:** import ↔ export round trips, and import of hand-made files (inversions, slash chords, 13ths, non-diatonic chords).
- **Host validation:** `auval` (aumu + aumi), `pluginval` at strictness 10 for VST3 and AU, in GitHub Actions on macOS.
- **Manual Live checklist:** each routing route, recording trigger keys vs. output, export drag-out, two-instance sync, hosted instrument save/reload, freeze, deleting the device while notes are held, and the plugin rescan.

---

## 9. Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| VST3 MIDI output can't be routed in the user's Live version | Low–Medium | High | Phase 0. Fallbacks: hosted instrument, or the CoreMIDI virtual port from NoteCap |
| Stuck or orphaned notes when parameters change mid-note | High (if neglected) | High | `VoiceTracker` ownership model and fuzz tests from Phase 1 |
| Timing errors at loop wraps and tempo changes | Medium | Medium | Musical-time event queue that is recalculated, with fake-transport tests |
| Plugin scanning crashes the DAW | Medium | High | Out-of-process scanner, a blocklist, and drag-to-library |
| Instance Sync fails in sandboxed hosts (Bitwig, Logic AUv3) | Medium | Low | Document it. Later: shared memory or a local socket |
| Instances process in different orders, so sync lags by a block | Medium | Low | PPQ-stamped events plus look-ahead |
| Scope (the feature list is large) | High | Medium | P1/P2/P3 split. Phase 5 ends with a shippable core |
| Ambiguous ChordPrism behaviour (the manual is short) | Medium | Low | Choices are written down in this plan. Behaviour is checked against the demo videos where possible |
| IP / trademark | Low | High | Clean room: our own name, UI, presets and text. Copy behaviour only |
| Codesigning and notarization | Low | Medium | Reuse NoteCap's packaging. Get a Developer ID early |

---

## 10. Proposed repository layout

```
chordism/
├── Chordism.jucer              # VST3 + AU instrument + Standalone
├── ChordismMidiFX.jucer        # AU MIDI effect (aumi) for Logic, same Source/
├── Builds/MacOSX/              # generated by Projucer
├── JuceLibraryCode/            # generated by Projucer
├── Source/
│   ├── core/theory/            # pure C++ music theory (no JUCE)
│   ├── core/engine/            # pure C++ generators, effects, scheduler
│   ├── core/midiio/            # import / export logic
│   ├── plugin/                 # processor, parameters, state, presets, shared session
│   ├── hosting/                # plugin library, scanner, hosted instrument
│   └── ui/                     # editor + components
├── Presets/                    # factory presets (XML)
├── Tests/                      # run_tests.sh, run_integration.sh, fixtures/
├── Tools/                      # package.sh, preset/fixture generators
└── .github/workflows/          # build, auval, pluginval
```

---

## 11. Open questions

1. **Target DAW.** This plan assumes **Ableton Live on macOS**, like NoteCap. Is that right? Other hosts (Logic, Bitwig, FL) mainly affect which formats get tested first.
2. **Live version:** 11 or 12? This affects the VST3 MIDI routing spike.
3. **Personal use or distribution?** This decides Developer ID signing, the installer and the size of the factory preset library.
4. **Plugin hosting in v1?** It's the most complex P2 item. In Live it is optional, because VST3 MIDI routing covers the main workflow.
5. **Keyboard size:** what controller will be used (25, 49 or 61 keys)? This sets the default split point and how many octaves each zone gets.
6. **Name:** "Chordism" (the folder name) is the working title.

---

## 12. Status

| Phase | State | Notes |
|---|---|---|
| 0. Feasibility | **Hosting and drag-out passed in Live. MIDI routing (virtual port) and Instance Sync still to retest.** | `Spikes/Phase0/`: AU/VST3/Standalone, `auval` and `Tests/run_tests.sh` pass. Round 1 findings and the round 2 checklist are in `Spikes/Phase0/README.md`. |

**Round 1 in Live 12.4.6:**
- **Drag MIDI out: works.**
- **VST3 MIDI out → another track: not offered by Live.** Track 2 could only pick "Pre FX"/"Post FX" from the Chordism track. Vendor guidance (Scaler, Blue Cat) says Live takes plugin MIDI out from VST2, and we can't build VST2 (no SDK, and Steinberg no longer licenses it). Round 2 tests the **CoreMIDI virtual port** route (NoteCap's code). If that records on time, it becomes the primary route in §3, with in-plugin hosting as the second route.
- **Hosting: passed in round 2.** The hosted instrument and its settings are saved with the set and restored on reopen, in Live with the AU build. The spike now tells Live when its state changes, keeps a hosted instrument that hasn't finished restoring (or failed to) in the next save, and logs save/restore to `~/Library/Logs/ChordismSpike.log`. The Juno106 on its own also lost edits across a reopen, so that is being checked separately.
- **Instance Sync: works in principle.** Both instances share one process, but Sync was off on one of them. The UI now lists every instance with its Sync setting. Round 2 retests.

**Other findings:** a JUCE host gets no MIDI output from the AU *instrument* build. That doesn't matter for Live, but the Logic `aumi` target needs its own check in Phase 1.
`pluginval` isn't installed on this Mac. Set it up in Phase 1 (CI).
