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
| R1 | Loads in Live (and other hosts) and sends MIDI to an instrument | Passes `pluginval` (strictness 10) as a VST3 (`auval` until the AU builds were removed, 2026-10-20). In Live, an instrument on another track plays Chordism's output and can record it. |
| R2 | **Chord Generator:** one key plays the diatonic chord for the chosen Key, Scale and Chord Group | Unit tests: every white key gives the correct chord in all 12 keys × all scales × all chord groups. |
| R3 | **Chord Editor:** each chord can be edited step by step (tone on/off, accidental, octave stacking) | Every chord in a reference set (triads to 13ths, altered chords, inversions, drop voicings) can be built in the editor, and the output matches. |
| R4 | **Chord effects:** strum, arp, repeat, hold, octave, stack, velocity algorithm, step sequencer | Events land sample-accurately on the host grid (fake-transport tests, including loops and tempo changes). No stuck notes in any test. |
| R5 | **Smart Scale + Pattern Generators:** five mappings, five arp/melody pattern slots, note repeat, look-ahead transposing | Mapping tables match the spec in §4.3. Patterns follow chord changes in real time. |
| R6 | **MIDI import/export:** drag a chord MIDI file in to make a preset; drag the generated MIDI out (up to 32 bars) | Round trip: export → import gives the same chords. Dropping the exported file into Live gives a clip that is identical to the live output. |
| R7 | **Presets, Instance Sync, preferences** | Presets save and restore every setting. Two instances share Key, Scale and Chord. (Plugin hosting was dropped on 2026-10-19, see §11.) |
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
| Presets | Save, Open, Navigate; genre, artist, vibe and user presets | Same, with our own factory library (about 20 at launch; personal use) | P2 |
| Hosting | Host a VST instrument inside the plugin, a library, scanning, drag-to-library | **Dropped** (2026-10-19). VST3 MIDI From covers Live | — |
| Multi | Instance Sync (Key, Scale, Chord shared by all instances) | Process-wide shared session with sync groups | P2 |
| Prefs | Tooltips, keyboard display, colours, shifter mode, Toggle/Standard | Same, stored per user | P2 |
| Extra | — | Separate MIDI output channel for Chord and Smart Scale parts | P3 |
| Extra | — | ~~CoreMIDI virtual output port "Chordism Out n"~~ (built in Phase 1 for the AU instrument; **removed** with the AU builds, 2026-10-20) | — |

---

## 3. Host constraints and plugin formats

NoteCap established that **Live does not host AU MIDI effects (`aumi`) and drops MIDI output from
AU plugins.** ChordPrism handles this by shipping a VST instrument and by hosting instruments
itself. Chordism ships the VST instrument, but **does not host instruments** (dropped 2026-10-19).

**2026-10-20: Chordism is VST3 + Standalone only.** The user removed the AU instrument, the AU MIDI
effect (`MidiFX/`), the CoreMIDI virtual port with its three parameters (*Virtual port*, *Port
compensation*, *Port trim*), the port-timing tests and the Phase 0 spike (`Spikes/`). Reasons:
in Live the AU could only reach an instrument through the port, one buffer late, and the port
fed back into any Chordism track whose input was *All Ins* (each echoed chord note played
another chord). The tables below are kept as the record of what was built and measured.

| Build | Type | Used in | How MIDI reaches the instrument |
|---|---|---|---|
| **VST3 instrument** (primary for Live) | Instrument with MIDI in/out, silent stereo out | Live, Bitwig, Cubase, Studio One, Reaper, FL | On a separate track. The destination track uses *MIDI From: Chordism track → Chordism*. Live records what arrives at the destination's input, which is the generated MIDI. |
| **AU instrument** (`aumu`) | Instrument with MIDI out, silent stereo out | Live and other AU hosts | Through the **CoreMIDI virtual port**, because Live ignores AU MIDI output. |
| **AU MIDI effect** (`aumi`) | MIDI effect | Logic Pro | Inserted in the MIDI FX slot, before the instrument. |
| Standalone | App | Development and debugging | Outputs to a MIDI device (and, until 2026-10-20, the virtual port). |

The **AU instrument** (`aumu`) build stays even without hosting. It makes no sound of its own,
and Live ignores AU MIDI output, so in Live its MIDI reaches an instrument through the CoreMIDI
virtual port (verified in Phase 0). The port works from every build.

JUCE needs a **separate Projucer target** for `aumi` (`isMidiEffect` is set at compile time).
It shares all of `Source/`, so this is a second `.jucer` file rather than separate code. It lives in
`MidiFX/`, because Projucer always writes `JuceLibraryCode/` (with `JucePluginDefines.h`) next to
the `.jucer`, so two projects in one folder would overwrite each other's plug-in defines.

**Identifiers (fixed in Phase 1, 2026-10-19; never change them, or Live and Logic lose saved
instances):**

| Build | Bundle ID | Manufacturer | Plug-in code | AU type |
|---|---|---|---|---|
| `Chordism.jucer`: VST3, AU, Standalone | `com.alistairrutherford.chordism` | `ARut` (Alistair Rutherford) | `Cdsm` | `aumu` |
| ~~`MidiFX/ChordismMidiFX.jucer`: AU "Chordism MIDI FX"~~ (removed 2026-10-20) | `com.alistairrutherford.chordism.midifx` | `ARut` | `Cdsf` | `aumi` |

Host parameter IDs (`Source/plugin/Parameters.h`, version 1) and the order of the Key, Scale and
Chord Group choices are saved in sets too: only append.

**Recording, as in ChordPrism.** By default, the Chordism track records the **trigger keys**,
so every parameter can still be changed after recording. The generated notes can be recorded
by (a) recording the destination track, or (b) Export drag-out.

### Phase 0 must confirm, in the user's Live version
1. VST3 MIDI output can be selected under "MIDI From" on another track, and it records sample-accurately.
2. JUCE's `AudioPluginFormatManager` can host AU and VST3 instruments inside the Chordism AU/VST3 in Live, and the hosted plugin's editor window works.
3. A file dragged out with `performExternalDragDropOfFiles` lands in Live as a MIDI clip at the drop point.
4. Two instances in one Live set share a process-wide static (Instance Sync). Live does not sandbox plugins by default, but Bitwig and Logic may run plugins in separate processes.

**Phase 0 outcome (Live 12.4.6, 2026-10-16): all four confirmed.** Three ways to get MIDI to an
instrument in Live were verified. Chordism ships the first two; hosting was dropped (§11):

| Route | Use | Status |
|---|---|---|
| **VST3 → "MIDI From: Chordism track / Chordism"** on the instrument track | Default, as ChordPrism documents it | Live lists the plugin as a MIDI source (round 2). In round 1 the routing was left on "Post FX", which is why nothing played. |
| **CoreMIDI virtual port** ("Chordism Out n") as the track's MIDI input | Alternative; also works from the AU build | Records Chordism's chords in Live (round 2). Needs the port's *Track* switch on in Live's MIDI settings. |
| ~~**Hosted instrument inside Chordism**~~ (dropped) | One-track setups, and AU in Live | Plays, and the instrument and its settings are restored with the set (round 2). |

**Recording timing (measured 2026-10-19, LiveBridge `test_timing_live.py`).** The spike's test
pattern was recorded through both routes. Each run was 4 bars at 120 BPM, 48 notes, 44.1 kHz,
with Record Quantization off:

| Buffer | VST3 "MIDI From" | CoreMIDI virtual port |
|---|---|---|
| 128 | — | +4.44 ms (196 samples = 1 buffer + 68) |
| 512 | — | +13.24 ms (584 samples = 1 buffer + 72) |
| 1024 | **0.000 ms on every note** | +24.86 ms (1096 samples = 1 buffer + 72), spread 0.09 ms |

- **VST3 route:** sample-accurate. It needs no correction.
- **Port route:** late by **one audio block plus about 70 samples (~1.6 ms)**, and very
  consistent.

**Phase 1 re-measurement (2026-10-19, `LiveBridge/tests/test_phase1_live.py`, buffer 1024).**
The plan was to stamp port events earlier by `blockSize / sampleRate + ~1.6 ms`. **That doesn't
work in Live.** Live honours a timestamp at most ~1.6 ms before the event is sent. An earlier
stamp is clamped to the event's arrival, so all of a block's events land together and the
offsets smear over a whole buffer (mean +11.6 ms, spread 23 ms at 44.1 kHz). A trim sweep with
compensation off shows the clamp: the latest note never lands before +1 buffer. An event can't
arrive before Chordism has processed its block, so **the port route always records one buffer
late**, like a hardware MIDI input. Only the fixed part is compensated: port events are
stamped **1.5 ms** early, just under the smallest fixed delay measured, so nothing is clamped.
The *Port trim* parameter stays for fine-tuning.

| Rate | VST3 "MIDI From" | Port, raw (compensation off) | Port, Chordism (1.5 ms early) |
|---|---|---|---|
| 44.1 kHz | 0.000 ms on every note | buffer + 1.654 ms (72.9 samples) | buffer + 0.15 ms, spread 0.05–0.11 ms |
| 48 kHz | 0.000 ms | buffer + 1.656 ms (79.5 samples) | — (−1.6 ms trim: buffer + 0.04 ms) |
| 96 kHz | 0.000 ms | buffer + 1.690 ms (162 samples) | buffer + 0.18–0.20 ms, spread 0.13–0.17 ms |

The fixed part is a time (1.54–1.71 ms over every rate and buffer measured), not a number of
samples. The AU instrument through the port gives the same result (44.1 kHz: buffer + 0.17–0.19
ms). **For recording, use the VST3 route**; the port route is for the AU build and for live
monitoring.

---

## 4. Functional design

### 4.1 Keyboard layout

A **Split** parameter (default MIDI 60, which Live calls *C3*) divides incoming MIDI. The default
suits the main controller, a **25-key keyboard** at its default octave (MIDI 48–72): the chord
zone gets exactly one octave (48–59, all 7 chords and 5 shifters) and the Smart Scale gets 8
white keys plus the 5 pattern keys (60–72). The Smart Scale zone's Octave setting and the
controller's octave buttons reach the rest of the range.

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

- **Scales (Phase 1):** Major, Natural Minor, Harmonic Minor, Melodic Minor (ascending),
  Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic Major, Phrygian Dominant, Hungarian
  Minor, Double Harmonic. The chord zone's default octave puts C at MIDI 48, moved by *Chord Octave*.
- **Chord Groups** fill in `on` and `octaveMask` for all 7 slots. Factory groups (our own list):
  Triads, Power (1-5), Sus2, Sus4, 6ths, Sevenths, Add9, Ninths, Elevenths, Thirteenths,
  1st Inversion, 2nd Inversion, Open (spread) Triads, Drop-2 Sevenths, Shell (1-3-7), Neo-Soul
  (9ths without the 5th), Quartal. Changing the group resets the editor edits, as ChordPrism does.
- **Scale default accidentals:** a per-scale table, so that common chords in each scale come out right without editing. Phase 1 has one entry: natural minor V raises its 3rd (the leading tone). Harmonic and (ascending) melodic minor already give a major V and IV.
- **Chord naming:** a pitch-class-set → name recogniser (root, quality, extensions, slash bass). The Roman numeral follows ChordPrism's style: upper case for major, lower case for minor, `*` for diminished, `+` for augmented.
- **Active Chord dropdown / right-click chord name:** a list of chord types for that root. Choosing one rewrites the steps (on and accidental) for that slot.
- **Voice leading (P2):** when Smooth is on, each new chord picks the inversion and octave whose notes are closest to the previous chord (minimum total semitone distance), anchored to the Output Octave.
- **Shifters:** five slots, each holding a target value (a Chord Group, or a Key/Scale). Standard = active while held. Toggle = stays active until another shifter is pressed. Shifting while a chord is held retriggers it.
- **Decisions made in Phase 2 (2026-10-19):**
  - Editor edits belong to a **scale degree**, not a white key: a progression only reorders the chords, so the V chord keeps its edits wherever it lands (two keys on the same degree share them). Edits are relative to the scale's degrees, so they follow Key and Scale; accidentals are stored as shown, including the scale default at the time of the edit.
  - Edits are tied to the Chord Group they were made on. The engine stops applying them on the block the group changes and doesn't bring them back if the group returns; a newer snapshot (a new edit, or a state restore arriving with its group) applies again. The plug-in then clears its stored edits.
  - Progressions: 8 presets (each a permutation of the 7 degrees, named by degree numbers) plus **Custom**, whose 7 degrees are host parameters (*Custom 1–7*) and may repeat.
  - Shifters: per slot a type (Chord Group or Key/Scale) plus group, key and scale; key and scale can be *Same*. Standard: with several held, the last pressed wins and releasing it hands back to the one before. Toggle: pressing the active shifter again turns it off; a toggled shifter survives transport stop; changing the mode turns it off. A group shifter plays that group's own shapes (edits are ignored); a Key/Scale shifter keeps the edits. Retriggering only restrikes held keys whose chord changed, at the sample of the change.
  - Names are spelled in the key (the tonic spelling with the fewest accidentals; ties go to sharps with a major 3rd, flats with a minor 3rd); a slot's tones take their step's letter. Roman numerals come from the slot's own degree, 3rd and 5th, so inversions keep theirs; a root moved by its accidental gets `b`/`#` (`bII`); chords with no 3rd are upper case.

### 4.3 Smart Scale mappings

Let *T* be the key tonic and *R* the root of the current chord (the most recently played chord slot).

| Mapping | White keys from the split upward (C, D, E, F, G, A, B, then the next octave) |
|---|---|
| **Chord Tones** | Scale tones counted from *R* in the order 1-3-5-7-2-4-6. Chord tones fall under the fingers and move with every chord change. |
| **Dynamic Scale** | Scale from *R*: 1-2-3-4-5-6-7 (the mode of the current chord), with *R* on C. |
| **Relative Scale** | Scale from *T*: 1-2-3-4-5-6-7, with *T* on C. Static. |
| **Absolute Scale** | Each white-key letter gives that letter's tone in the scale at its real pitch (e.g. in D major, F→F♯). Static. |
| **Chromatic** | Pass-through. |

- **Inversions button** (Chord Tones and Dynamic only): fold the tones into one octave, instead of stacking them upward. As ChordPrism notes, a lower pitch can then sit to the right of a higher one. (Phase 4: the octave is the one from *T*, see below.)
- Accidentals from the active chord's Chord Editor steps apply to the Smart Scale only when that step is on, as in ChordPrism.
- The UI shows the tone letter on each key, with chord tones marked by a coloured edge.

**Decisions made in Phase 4 (2026-10-19):**
- **Register:** every key sounds in its own octave. *C_o* is the C at or below the key, moved by *SS octave* (−3..+3), and *T* sits on C_o + T, as in the chord zone. *R* sits at its place in the key (C_o + T + scale[d]), so Chord Tones stacks 1-3-5-7-9-11-13 upward from there (scale index d + 2j for letter j) and Dynamic plays scale index d + j. Each key octave repeats the same tones 12 semitones higher.
- **Inversions** (the user's choice) fold every tone into the octave from the tonic, [C_o + T, C_o + T + 12), whatever the chord, so the zone keeps its register. (Folding "above R" would leave Dynamic unchanged.)
- **Accidentals** apply in Chord Tones and Dynamic only, where a tone is a chord step (scale index d + 2k is step k; Dynamic's letter j is step 4j mod 7). Relative and Absolute are static and take none.
- **Absolute** spells the scale in the key the way the chord names do (`makeSpelling`: Db, not C#; Eb minor writes Cb) and plays the tone with the key's letter nearest the key, so it stays within ±2 semitones (Eb minor: the C key plays Cb = B below).
- **Current chord:** the chord slot played last, with the chord zone's effective key and scale (a Key/Scale shifter applies), and its chord as played (a group shifter's shape, edits). It stays current after its key is up. Before any chord is played: the tonic with no steps.
- **A held tone keeps its pitch** when the chord changes; the next note repeat (or the next press) takes the new one. Patterns take it at their next step.
- **Chromatic** passes every key through (black keys too; no pattern keys), still through the Smart chain (Octave, Stack, Note Repeat, StepSeq, Velocity, Hold).

### 4.4 Pattern generators and look-ahead

Each of the five pattern slots stores `{mode: Arp | Melody, …settings}`. Pressing a pattern key
starts the pattern over the **current chord**. When the chord changes, the pattern follows it
at the next step without restarting.

- **Arp:** Pattern (the 12 directions, as in the chord arp: Linear, Converge, Diverge, Stutter, Zigzag, Boomerang × Up/Down), Rate (1/2, 1/4, 3/16, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T), Range 1–3 octaves, Arp Chord (5ths, Triads, 7ths, 9ths, 11ths, 13ths; this sets how many stacked chord tones are used), Gate 0–100%.
- **Melody (P2):** Notes per bar (1–16), Rhythm (a template from a factory table: straight, syncopated, dotted, clave, offbeat, …), Swing, Gate, and a Seed. Generation is deterministic for a given seed + chord + bar. Strong beats take chord tones and weak beats take scale passing tones, with stepwise motion favoured and a range limit.
- **Note Repeat:** right-clicking a Smart Scale white key turns on repeat for all Smart Scale tones, using Arp Rate and Arp Gate. (Phase 4: the Rate and Gate of the *selected* pattern slot, the *Pattern slot* parameter that the Phase 5 pattern panel edits.)
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

**Decisions made in Phase 3 (2026-10-19):**
- **Octave** is the *Chord Octave* parameter (Phase 1); it moves the voicing, so no second Octave parameter. **Stack** adds +1, +2 or both octaves; the tones are then sorted, each pitch once.
- **Strum and Arp** are separate parameters; when both are on, Arp wins. Repeat doesn't apply to an arp. Strum Rate runs from 1/128 to 1/16 while playing; *Strum time* (ms) is used while stopped. Alternating flips direction on every strike, across all chords. Random uses a seeded generator, reseeded on reset and on transport start, so every take from the top strums the same.
- **Grid:** Arp and Repeat steps fall on multiples of the rate from ppq 0. The first step is the key press itself; the next grid line follows, unless it's less than a quarter step away (then the first step sounds through it). Gates are 0–100% of the step, with a floor of 1% so every note has a length.
- **Arp patterns** (our own definitions, `Source/core/engine/Arp.h`): Linear, Converge (outside in), Diverge (Converge reversed), Stutter (each tone twice), Zigzag (up two, back one), Boomerang (there and back), each Up or Down, over the stacked tones × Range octaves. A chord change takes effect on the next step.
- **Step Sequencer:** sixteenth slots counted from each bar line (the host's bar start); slot j plays step j mod 16, so a 3/4 bar uses steps 1–12. Swing pushes the even steps up to half a sixteenth later (100%). Each active step is a window in which the held chord sounds (restruck, and strummed with Strum); Sustain carries a step through the inactive ones, with Gate on the last sixteenth before the next active step. A key pressed inside an open window sounds at once (unless less than a quarter of the step is left); Repeat and Arp sound only inside windows. The four pattern memories start as factory patterns.
- **Velocity Algorithm** applies to every chord-zone note-on (not only with the sequencer): Peak · (1 − Range · (1 − curve)), curve from the note-on's sixteenth of the bar (a bar of another length is divided into 16).
- **Hold:** *Latch* (until the next chord key) or *Bar* (also ends at the first bar line after the release). A transport stop ends held chords. While stopped, pressing a held chord's key again only ends it, and Bar works like Latch. Turning Hold off ends them.
- **Chord changes** while a strike sounds restrike it at the change (all releases first), with Strum starting over; an arp takes the new chord at its next step.
- **Stopped transport:** an internal clock runs at the last tempo. A chord key pressed with nothing playing restarts it at bar 1 on that sample, so stopped patterns start on the press.
- **Loops and locates:** a block that crosses the loop end is processed in segments; a musical event at the loop end belongs after the wrap. On a wrap or a locate, queued note-offs and strum tones move with the music (keeping their lengths); each pattern's next step goes to the first grid line at the new position, so a pattern plays its step at the loop start. Tempo changes need nothing: musical events find their sample in the block they fall in.
- **Bookkeeping:** one performance per chord key, owning VoiceTracker sources 128 + 2k and 129 + 2k, used in turn per strike or arp step (VoiceTracker now has 384 sources of up to 64 notes). At one sample, queued note-offs run first, then input, then queued note-ons, so a key-up on a grid line never starts a step.

**Decisions made in Phase 4 (2026-10-19), the Smart zone:**
- **One engine for both zones.** The Smart zone runs a second `ChordPerformer` (ChordFx.h) on the same Clock, with `PatternEngine` as its *voicing* (the tones and the arp of each key). So it shares the scheduler, grid, StepSeq windows, Velocity Algorithm, Hold and the loop/locate handling. Smart performances own sources 384 + 2k and 385 + 2k (VoiceTracker: 640 sources). The engine runs the two queues in sample order (chord zone first at a tie). A stopped clock is re-anchored on a press only when neither zone is playing.
- **Pattern keys** (black, C# … A# = slots 1-5 in every octave) arp over the current chord's stacked thirds from R in the key's octave: Arp Chord 5ths = root and 5th, Triads … 13ths = 3 … 7 thirds, with the chord's accidentals (steps that are on). Pattern, Rate, Range and Gate come from the slot; the arp list has no Off. Factory slots: 1 Linear Up 1/16 Triads, 2 Linear Down 1/16 7ths, 3 Converge Up 1/8 Triads ×2, 4 Boomerang Up 1/16T 9ths, 5 Zigzag Up 1/8 7ths ×2.
- **Stack after the pattern:** an arp step sounds with its +1/+2 octave copies (the chord zone stacks before its arp). A tone's strike is the tone and its copies.
- **Smart zone settings** are its own: *SS octave*, *SS stack*, *SS step seq* (pattern, swing, gate, sustain and four patterns), *SS velocity* (peak, range), *SS hold*. Hold's "new selection" is the next Smart Scale key. Mute SS ends the zone's performances.
- **Test smart** (for LiveBridge): *Tones* presses Smart key letter 3b mod 7 with each beat's chord key; *Pattern n* holds pattern key n for the whole take.

**Decisions made in Phase 6 (2026-10-20), Melody (`Source/core/engine/Melody.h`):**
- **Melody is a pattern mode.** *Pattern n arp* gained a last choice, *Melody* (appended). A melody slot runs in the Smart zone's performer like an arp whose steps are the melody's notes, so it shares Stack (after the pattern), the SS step sequencer, velocity, Hold and the loop handling.
- **One bar at a time, a pure function** of (seed, slot, bar number, settings): bar number = the bar line's ppq ÷ bar length. The same seed plays the same melody at the same place in the song whatever the block size, and a loop repeats its bars. While stopped, the press re-anchors the clock, so the melody starts its bar 1 there.
- **Rhythm:** each sixteenth from the bar line is scored by the template's weight (Straight, Syncopated, Dotted, Clave, Offbeat, Free: our own tables) plus seeded jitter smaller than one weight step; the best *Notes per bar* sound (scaled for bars other than 4/4). So Straight 4 is the beats and 8 the eighths, Clave 5 the 3-2 son clave. **Swing** delays the even sixteenths up to half a sixteenth (as the step sequencer); **Gate** is the share of the time to the next note (or the bar line).
- **Pitch:** a seeded walk over scale indices 0 … 7 × Range from the tonic (steps of 1 or 2 three times in four, the odd repeat or leap of 3-4, leaning back to the middle); notes on a beat, and a bar's first, are *strong*. When a note plays, its index is made real in the current key and scale from the tonic in the pattern key's octave; a strong note moves to the nearest chord tone of the current chord (the slot's *Arp chord* sets how many stacked thirds count; ties go down); a tone that is one of the chord's steps takes that step's Chord Editor accidental if it's on. A chord change is taken at the next note; the rhythm doesn't restart.
- **Parameters:** *Pattern n notes* (1-16) and *Pattern n rhythm* per slot; *Melody swing* and *Melody seed* (1-999) shared by the five slots (each slot mixes its number into the seed). Range, Arp chord and Gate are the slot's own.

### 4.6 MIDI import and export

- **Import (drag a `.mid` onto the UI):** group the notes into chord events (onsets within a tolerance). Keep up to 7 distinct chords in order of first appearance. Choose the Key and Scale whose diatonic chords explain the imported chords with the fewest accidentals. Then encode each chord as degree + steps + accidentals + octave masks, so the exact voicing is kept. The progression is set to match the import order. The white keys light up in sequence to confirm, as in ChordPrism.
- **Export (drag-out handle):** a recorder on the output side keeps the generated MIDI since playback started, in PPQ, in a preallocated buffer of 32 bars. Only **completed** bars are exported. The handle is disabled until the first bar has finished. Dragging writes a temporary `.mid` file (type 1, 960 PPQ, tempo and time-signature meta events) and calls `DragAndDropContainer::performExternalDragDropOfFiles`.

**Decisions made in Phase 6 (2026-10-20), import and export (`Source/core/midiio`):**
- **The file code is our own plain C++** (`MidiFile.h`): type 0/1, running status, note-on velocity 0, the first tempo and time signature; SMPTE time is refused. Export writes type 1 at 960 PPQ: a track with the name, tempo and time signature, and one with the notes (note-offs first at a tick).
- **Chords in a file:** a note joins a chord if it starts within a sixteenth of the chord's last note *while the chord's first notes still sound*, so a strum is one chord and an arp isn't; two or more pitches make a chord. Distinct chords are exact pitch sets.
- **Key fit:** fewest pitch classes outside the scale, then the scale family (Major and Natural Minor, then Harmonic and Melodic Minor, then Dorian, Mixolydian, Lydian, Phrygian, then the rest), then where the tonic is a chord root (the first chord's, the last one's, any). C F G Am gives C major, Am F C G A minor; C Bb F Ab E7 picks F Melodic Minor (three tones outside, against four for C major).
- **Encoding:** every pitch class takes a step whose scale tone is within ±2 semitones (the step its interval above the root usually is, if it can: b9/9 → 9, 3rds → 3, 11, b5/5/#5 → 5, 6/13 → 13, 7ths → 7), every note an octave −2..+2 of it, by a small search for the fewest accidentals. **Edits belong to a degree** (Phase 2), so each imported chord needs its own degree: its root's (by the letter the key spells it with), or, if an earlier chord holds that, the free degree that encodes it best. C, C7 and Cmaj9 import on three degrees. **Chord Octave** is the one nearest 0 at which every note fits; notes that fit nowhere are left out and counted in the message. Progression becomes Custom (key n → chord n's degree; the rest play the unused degrees), the edits are made on the current Chord Group, and the chord keys light up in turn. A file that isn't MIDI changes nothing.
- **Export's take** starts at the bar line at or before the start of playback and runs on like a tape through loop wraps and locates (beats = samples played ÷ samples per beat). The stop's note-offs close it; the next start replaces it. A ring of 32,768 events (preallocated; overwritten oldest first) holds it; the message thread copies it out lock-free (a seqlock header and atomic slots stamped by a running count). The file has the last ≤ 32 *completed* bars, starting at a bar line; bar boundaries are decided in ticks, so a note a fraction of a sample before a bar line belongs to the bar it starts; notes begun before the first bar are left out and notes still sounding are cut at the last bar line. A drag writes `$TMPDIR/Chordism Export/Chordism <Key> <Scale> <n> bars.mid`.
- **For LiveBridge**, while the Test pattern is on each finished take is also written to `~/Music/Chordism/Exports/Test take.mid` (the processor's timer, after the stop).

### 4.7 Presets, sync and preferences

- **Presets:** XML files in `~/Music/Chordism/Presets/<Category>/`, using the same ValueTree as the plugin state. Categories are Genre, Artist-style (described by style only, never by artist name), Vibe and User. The browser has previous/next buttons and a search box. Factory presets are copied by the install script.
- **Instance Sync:** a process-wide `SharedSession` holds `{key, scale, activeChordSlot, chordData, ppqStamp}` behind an atomic, versioned snapshot. Instances in the same **sync group** (Off, A, B, C, D) publish and subscribe. Each published chord carries its PPQ time, so an instance that processes earlier in the block can still apply the change at the right sample. If there is a one-block delay, the look-ahead logic covers it.
- **Preferences** (opened from the logo): tooltips, show/hide the keyboard display, colour theme, shifter target (Chord Groups or Key/Scale), Toggle/Standard, look-ahead window, and the QWERTY layout.

**Decisions made in Phase 6 (2026-10-20), presets and QWERTY:**
- **Factory presets live in the code** (`Source/plugin/FactoryPresets.cpp`: 21, plus *Init*), as host-parameter values over the defaults, checked by the compiler against the engine's enums; no install step is needed and the browser always has them. Genre (10), Artist-style (5, by style only), Vibe (6); seven use Melody slots.
- **They are also the plug-in's programs** (program 0 = Init, every default, so a new instance is its program 0). The VST3 then has JUCE's *Program* parameter, which Live shows (Live lists no VST3 programs under the device's presets): LiveBridge loads presets through it. A program chosen off the message thread waits for the processor's timer; a state restore cancels a waiting one. The current program, preset name and category are saved with the plug-in, so a host restoring its program parameter doesn't reload over a restored state.
- **User presets** are XML files of the plug-in state's own tree in `~/Music/Chordism/Presets/<Category>/<Name>.xml` (categories Genre, Artist-style, Vibe, User; any folder is listed as a category). A preset holds every host parameter except Enable, the virtual port's three (removed since) and the test rig's three (`params::isPresetParameter`), and the Chord Editor's edits; not the editor's preferences. Loading sets each preset parameter (the preset's value, or the default if it has none), restores the edits on the preset's Chord Group, and ends every note.
- **QWERTY** (a preference in Setup, saved with the editor's): *Piano* (default): the A row from the C an octave below the split (A–J the chord zone's white keys, W E T Y U its shifters, K ' on the Smart Scale, O P its first pattern keys), Z / X an octave down / up; *Two rows*: Z–/ the octave below the split, Q–P the split's octave; or *Off*. Keys play through the same lock-free queue as on-screen clicks; the editor takes the keyboard focus when it opens and releases held keys when it loses it.

---

## 5. Architecture

Matches the sibling projects: **JUCE 8.0.14, C++17, Projucer + xcodebuild**, with JUCE checked
out at `~/Development/JUCE`. No CMake or Homebrew.

```
           MIDI in ─► InputRouter ─┬─► ChordZone ─► ChordEngine ─► ChordFxChain ──┐
 (host or QWERTY / on-screen key)  │      ▲ shifters                              ├─► VoiceTracker ─► EventQueue ─► MIDI out
                                   └─► SmartZone ─► ScaleMapper ─► PatternEngine ─┘                         │
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
| `Source/ui/` | `ChordismEditor`, `KeyboardDisplay` (split, labels, highlights), `ChordEditorPanel` (7×7 grid), `ChordFxPanel`, `SmartScalePanel`, `PatternPanel`, `StepSeqView`, `PresetBar`, `ExportHandle`, `PrefsOverlay`, `LookAndFeel` |
| `Tests/` | Plain C++ test executables run by `run_tests.sh` and `run_integration.sh`, as in NoteCap, plus an editor snapshot |

### Parameters vs. state
- **Host parameters (APVTS, automatable, MIDI-CC learnable):** Key, Scale, Progression, Chord Group, Mapping, Split, Mutes, Enable, every effect setting for both zones, and the active pattern's settings. About 80 parameters.
- **ValueTree state (not automated):** the 7×7 chord-editor matrix and UI preferences. (Phase 2 made the shifter targets host parameters; Phase 3 the sequencer steps; Phase 4 the five pattern slots' settings and the Smart Scale's sequencer, so LiveBridge and automation reach them.)
- **Live's limit:** Live shows a plug-in's parameters to LiveBridge only up to its auto-populate threshold, at most 128 (Options.txt `-_PluginAutoPopulateThreshold=128`); above it the panel is empty. Phase 4 packed the sequencer steps into one 16-bit mask per pattern (`Seq 1-4 steps`, shown as `x...x...`) to make room: **111 parameters after Phase 4, so 17 are left** for Phases 5-7. **Phase 6: 123 parameters, plus the VST3's *Program* (JUCE adds it with the programs), so Live shows 125 (and Device On): 3 are left for Phase 7** (Sync group, look-ahead window, voice leading). **2026-10-20: removing the virtual port's three leaves 120 + Program = 121 (Live: 122 with Device On), so 6 are left.**

### Real-time safety rules
- `processBlock` never allocates, locks, logs or does file I/O. All buffers are allocated in `prepareToPlay`.
- Changes from the UI and from `SharedSession` reach the audio thread as lock-free snapshots (double-buffered and swapped atomically; `Source/core/engine/SnapshotExchange.h` uses the three-buffer form, so the writer never touches the buffer the reader holds and neither side waits).
- Export, preset loading and MIDI import happen on the message thread. The results are handed over as new snapshots.
- All-notes-off (through `VoiceTracker`) on: transport stop, Enable off, preset load, Key/Scale change with notes held (retrigger), `releaseResources`, and destruction.

---

## 6. UI (v1)

Resizable, about 1100×640 by default. Our own visual design; ChordPrism's look is not copied.

- **Top bar:** logo (opens Preferences) · Enable · Preset browser · Key · Scale · Progression · Chord Group · Mapping · Sync group · Export drag handle.
- **Left half, Chord Generator:** the chord-zone keyboard with the chord name and Roman numeral on each white key, a stripe on the last-played key, and shifter labels on the black keys. Below it, the **Chord FX** panel. A gear button swaps it for the **Chord Editor** grid (7 chords × 7 steps: on/off, accidental, octave stack).
- **Right half, Smart Scale:** the scale-zone keyboard with tone letters, chord-tone edges and pattern names on the black keys. Below it, the **Pattern** panel for the selected pattern slot, and the Smart Scale FX.
- **Bottom:** a full keyboard display showing played output notes (can be hidden). It also works as an on-screen keyboard and responds to the QWERTY keyboard.
- Right-click menus throughout: Roman numeral → degree; chord name → chord type; shifter key → target; pattern key → make active; white SS key → Note Repeat.

**Decisions made in Phase 5 (2026-10-19):**
- **Look:** dark panels, amber for the Chord Generator, teal for the Smart Scale, red for sounding notes (`Source/ui/Theme.*`). Default 1100×640, resizable from 960×600 to 1800×1100; the size, the page shown and whether the keyboard is shown are saved in the plug-in state (a `UI` child next to the Chord Editor's).
- **Controls build themselves from the parameters** (`ParamControl`: a choice is a menu, a bool a toggle, a number a bar showing the parameter's own text). Every one records its parameter, so the snapshot test can prove every parameter is reachable.
- **Top bar:** logo (opens **Setup**) · On · Key · Scale · Progression · Chord Group · Mapping · Shifter mode · Keyboard. Preset browser, Sync group and Export come with their features (Phases 6–7). **Setup** holds what has no place on the main page: Split, the Custom degrees, the five shifters' targets, the virtual port and its status, and the test pattern controls.
- **Chord keys menu:** one right-click menu per white key: *Degree* (makes the progression Custom, starting from the current order), *Chord type* (27 types, `Source/core/theory/ChordTypes.h`; rewrites the degree's steps in the Chord Editor) and *Reset chord edits*. Black keys: the shifter's type, group, key and scale.
- **Smart keys** show the tone each key plays now, spelled in the key, with chord tones underlined and the root darker; black keys show their slot and arp. The keyboard shows the split's octave and the next (25 keys).
- **On-screen playing:** every keyboard plays through the engine like host MIDI (a lock-free queue into the next block), so clicks behave exactly like keys. QWERTY playing is Phase 6.
- **Change (2026-10-20): AU builds and virtual port removed** (the user's request, after an armed Chordism track with input *All Ins* fed the port's output back into itself):
- **Removed:** the AU instrument target (`Chordism.jucer` now builds VST3 + Standalone; `Info-AU.plist`, CoreMIDI linking), the `MidiFX/` project (AU MIDI effect for Logic), `Source/plugin/VirtualMidiOut.*` and `Source/core/engine/PortTiming.h`, the parameters *Virtual port*, *Port compensation* and *Port trim* (a saved state's values for them are ignored; the other IDs are unchanged, so sets keep their settings), the processor's port code and host-time reading, the Setup page's port section and status, `Spikes/` (the Phase 0 spike) and its bundles, `LiveBridge/tests/test_timing_live.py` (spike + port timing), and the port route and trim tests of `test_phase1_live.py`. The installed `Chordism.component`, `ChordismMidiFX.component`, `ChordismSpike.component` and `ChordismSpike.vst3` were deleted from `~/Library/Audio/Plug-Ins`. `Tests/run_validation.sh` now runs pluginval on the VST3 only; CI builds one project.
- **Parameters:** 120 (Live shows 122 with Program and Device On): 6 left for Phase 7.
- **Checks:** `Tests/run_tests.sh` passes (the port-timestamp test is gone); `Tests/run_snapshot.sh` passes (*120 parameters, all with a control*; the Setup reference image **updated** with `--update`, the port section being gone; the preset checks use Enable and the test rig as the parameters a preset leaves alone); the VST3 builds; `pluginval` strictness 10 passes. **Live** (restarted with the new build): `test_offline.py` 11 passed; Phases 1-4 and 6 18 passed (Phase 1 now records the 2 cases through VST3 MIDI From only, every note at 0.000 ms); Phase 5's *prepare* stage passes (Live shows 121 parameters, Program included; Device On aside).

**Phase 6 (2026-10-20):** the top bar gained the **preset browser** (◀ name ▶ · Save; the name opens a call-out with a search box over every preset, *Import MIDI chords…* and *Show folder*) and the **Export handle** (grip + "n bars"; dim until the first bar has finished; a red dot while recording; drag it into the host). Keyboard became *Keys*. The pattern panel shows *Melody swing* and *Seed* beside the slot buttons, and for a Melody slot *Notes/bar* and *Rhythm* instead of *Rate* (*Arp chord* reads *Chord tones*); a Melody slot's black keys read *Mel*. Dropping a .mid anywhere on the editor imports it (the editor is outlined while a file is over it; the result shows over the chord keys for 8 s). Setup has *Preferences: QWERTY keys*.
- **Telling the host about state changes:** the Chord Editor's edits and the UI's preferences aren't parameters, so the processor calls `updateHostDisplay (…withNonParameterStateChanged (true))` when they change (JUCE: VST3 `setDirty`), as the Phase 0 spike did; a state restore doesn't report a change. Not checked in Live: whether an edit alone marks the set as modified.

---

## 7. Milestones

| Phase | Duration | Deliverable | Exit criteria |
|---|---|---|---|
| **0. Feasibility spikes** | 3–4 days | Throwaway JUCE VST3/AU | The four checks in §3 pass or fail in the user's Live version. Routing approach chosen. |
| **1. Skeleton + theory core** | 1.5 weeks | `.jucer` targets (VST3, AU instrument, Standalone; `aumi` second project), `core/theory`, `InputRouter`, plain chord output, `VoiceTracker` | Live plays C-major-scale chords from the chord zone through the chosen route, checked by a LiveBridge test that records them and reads them back. Theory unit tests pass for all 12 keys × all scales × all groups. `auval` and `pluginval` pass. |
| **2. Chord Generator complete** | 1 week | Chord Editor model, Chord Groups, Progressions, Shifters (both modes), chord naming and Roman numerals | Reference-chord test set (R3) passes. Fuzzing parameters while notes are held gives no stuck notes. |
| **3. Timing engine + chord FX** | 2 weeks | `Clock`, `EventQueue`, Strum, Arp, Repeat, Hold, Octave, Stack, Velocity Algorithm, Step Sequencer | Fake-transport integration tests: sample-accurate at 32–2048 sample buffers and 44.1/48/96 kHz, through loop wraps and tempo changes. Recording test in Live passes (LiveBridge). |
| **4. Smart Scale + patterns** | 2 weeks | 5 mappings, Inversions, Note Repeat, Pattern Engine (arp), SS FX chain | Mapping tables match §4.3 exactly (table tests). Patterns follow chord changes without restarting. |
| **5. UI v1** | 2–3 weeks | Editor from §6 | Every feature reachable. State restores after a Live set is reopened. Snapshot test. |
| **6. Melody, I/O, presets** | 2 weeks | Melody generator, MIDI import, Export drag-out, Preset manager + about 20 factory presets, QWERTY | R6 round-trip test. Melody output is identical for the same seed. Export drops into Live as a correct clip. |
| **7. Sync, look-ahead** | 1 week | `SharedSession`, `ChordTimeline` look-ahead, voice leading | Two instances follow each other within one block (LiveBridge). Look-ahead test on a looped transport. |
| **8. Hardening + release** | 1 week | Edge cases, performance pass, ad-hoc signed build and an install script (personal use; no Developer ID or `.pkg`) | 8 instances under 1% CPU each. `pluginval` strictness 10 in CI. Clean install on a second user account. |

**Total: about 13–16 weeks** for one developer. Phases 1–5 (about 9 weeks) give a usable
instrument that covers the core of ChordPrism.

---

## 8. Testing strategy

- **Theory tables:** every Key × Scale × Chord Group × degree is checked against a reference (generated independently, e.g. with `music21` offline into a JSON fixture).
- **Engine tests with a fake transport:** a scripted sequence of MIDI and transport events (play, stop, loop, tempo change, time-signature change) is checked against the expected output, sample-accurately. This covers every effect and all combinations of the effect chain.
- **Stuck-note fuzzing:** random notes, parameter changes, preset loads and transport changes over thousands of blocks. At the end, `VoiceTracker` must be empty and every note-on must have a matching note-off.
- **Determinism:** melody and random strum use a seeded PRNG, so the same input gives the same output.
- **MIDI I/O:** import ↔ export round trips, and import of hand-made files (inversions, slash chords, 13ths, non-diatonic chords).
- **Host validation:** `pluginval` at strictness 10 for the VST3, in GitHub Actions on macOS (`auval` for the AU builds until they were removed, 2026-10-20).
- **Live-in-the-loop (`LiveBridge/`):** a Remote Script inside Live serves the Live Object Model over a local socket. pytest drives it, and so does an MCP server (`live-bridge` in `.mcp.json`) so Claude can drive Live interactively. It loads Chordism, sets routing and parameters, records N bars and reads the recorded notes back with exact start times. It automates **routing (VST3 and port), recording timing, two-instance sync, and output-level checks**. Every setting that a test needs to change must be a host parameter, because LiveBridge reaches the plugin only through Live's device parameters. Live must be running with its GUI, so these tests run on the development Mac, not in CI. Live has to be restarted to pick up a rebuilt plugin (`LiveBridge/scripts/restart_live.sh`).
- **Manual Live checklist:** whatever the LOM can't reach: saving and reopening a set (state restore), buffer-size changes, export drag-out, freeze, deleting the device while notes are held, and the plugin rescan.

---

## 9. Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| VST3 MIDI output can't be routed in the user's Live version | Low–Medium | High | Phase 0: VST3 routing works in Live 12.4.6 (the CoreMIDI virtual-port fallback was removed 2026-10-20) |
| Stuck or orphaned notes when parameters change mid-note | High (if neglected) | High | `VoiceTracker` ownership model and fuzz tests from Phase 1 |
| Timing errors at loop wraps and tempo changes | Medium | Medium | Musical-time event queue that is recalculated, with fake-transport tests |
| Plugin scanning crashes the DAW | Medium | High | Out-of-process scanner, a blocklist, and drag-to-library |
| Instance Sync fails in sandboxed hosts (Bitwig, Logic AUv3) | Medium | Low | Document it. Later: shared memory or a local socket |
| Instances process in different orders, so sync lags by a block | Medium | Low | PPQ-stamped events plus look-ahead |
| Scope (the feature list is large) | High | Medium | P1/P2/P3 split. Phase 5 ends with a shippable core |
| Ambiguous ChordPrism behaviour (the manual is short) | Medium | Low | Choices are written down in this plan. Behaviour is checked against the demo videos where possible |
| IP / trademark | Low | High | Clean room: our own name, UI, presets and text. Copy behaviour only |

---

## 10. Proposed repository layout

```
chordism/
├── Chordism.jucer              # VST3 + AU instrument + Standalone
├── Builds/MacOSX/              # generated by Projucer
├── JuceLibraryCode/            # generated by Projucer
├── Source/
│   ├── core/theory/            # pure C++ music theory (no JUCE)
│   ├── core/engine/            # pure C++ generators, effects, scheduler
│   ├── core/midiio/            # import / export logic
│   ├── plugin/                 # processor, parameters, state, presets, shared session
│   └── ui/                     # editor + components
├── Presets/                    # factory presets (XML)
├── Tests/                      # run_tests.sh, run_integration.sh, fixtures/
├── Tools/                      # install.sh, preset/fixture generators
├── LiveBridge/                 # Live-in-the-loop tests + MCP server (§8)
└── .github/workflows/          # build, core tests, pluginval
```

---

## 11. Open questions

1. ~~**Target DAW.**~~ Answered: **Ableton Live on macOS**.
2. ~~**Live version.**~~ Answered: **Live 12** (12.4.6 in Phase 0).
3. ~~**Personal use or distribution?**~~ Answered (2026-10-19): **personal use.** Ad-hoc signing, an install script, about 20 factory presets.
4. ~~**Plugin hosting in v1?**~~ Answered (2026-10-19): **dropped.** No hosting module and no scanner. The AU instrument build stayed, using the virtual port (§3), until both were removed on 2026-10-20.
5. ~~**Keyboard size.**~~ Answered (2026-10-19): **25 keys.** Split stays at MIDI 60 (§4.1).
6. ~~**Name.**~~ Answered (2026-10-19): **Chordism.** Phase 1 fixes the bundle ID and plugin codes, so they must not change afterwards.

---

## 12. Status

| Phase | State | Notes |
|---|---|---|
| 0. Feasibility | **Done (2026-10-16).** Routing, hosting, drag-out and Instance Sync all confirmed in Live 12.4.6. | `Spikes/Phase0/` (removed 2026-10-20; in git history): AU/VST3/Standalone, `auval` and `Tests/run_tests.sh` pass. Results by round were in `Spikes/Phase0/README.md`. Routing decision is in §3. |
| Phase 0 carry-over: recording timing | **Done (2026-10-19).** VST3 is sample-accurate; the port is one block + ~1.6 ms late. | Measured with `LiveBridge/` (§3, §8). |
| 1. Skeleton + theory core | **Done (2026-10-19).** Live plays C-major chords from the chord zone through VST3 MIDI From (sample-accurate) and the virtual port (one buffer late, §3), checked by LiveBridge. | Details below. |
| 2. Chord Generator complete | **Done (2026-10-19).** R3 reference set, naming and stuck-note fuzz pass; progressions and shifters checked in Live (LiveBridge). | Details below. |
| 3. Timing engine + chord FX | **Done (2026-10-19).** Fake-transport tests pass at 32–2048-sample blocks and 44.1/48/96 kHz, through loop wraps, locates and tempo changes; fuzz clean. Strum, arp and repeat + velocity recorded in Live (LiveBridge), sample-accurate. | Details below. |
| 4. Smart Scale + patterns | **Done (2026-10-19).** Mapping tables match §4.3 in all keys, scales and degrees; patterns follow chord changes without restarting; fake-transport tests pass at 32–2048-sample blocks and 44.1/48/96 kHz; fuzz clean; both builds and host validation pass. Five Smart Scale cases recorded in Live (LiveBridge), sample-accurate. | Details below. |
| 5. UI v1 | **Done (2026-10-19).** The editor of §6 is built; every parameter has a control; snapshot test and state round trip pass; both builds and host validation pass. A Live set saved with a Chord Editor edit reopens with every parameter and the edit (LiveBridge). | Details below. |
| 6. Melody, I/O, presets | **Done (2026-10-20).** R6 round trip passes (8 cases: export → file → import plays every chord of the take note for note); Melody is a pure function of the seed (identical across all block sizes and rates, and take after take in Live); Export's file equals the recorded take and Live's own import of it gives the same clip; 21 factory presets load in Live through the Program parameter; QWERTY plays. All core, snapshot and host-validation suites pass; the Phase 1-4 Live tests pass again. | Details below. |
| Change: AU builds and virtual port removed | **Done (2026-10-20).** Chordism is VST3 + Standalone only; the AU instrument, the AU MIDI effect, the CoreMIDI virtual port (3 parameters), the port-timing tests and the Phase 0 spike are gone. Core, snapshot and pluginval pass; the Live tests pass on the VST3. | Details below. |

**Phase 6 (2026-10-20):**
- **Core (plain C++):** `Source/core/engine/Melody.h` (rhythm templates, the seeded walk, timing), the melody steps in `ChordFx.h` (a melody slot is an arp whose steps are its notes; settings changes take the next note), `ScaleMapper::melodyTone`, and Melody settings in `PatternEngine.h`. `Source/core/midiio/`: `MidiFile.h` (read/write), `ChordImporter.h` (chords, key fit, encoding), `ExportRecorder.h` (the take and `exportBars`). Decisions are in §4.4, §4.6 and §4.7.
- **Plug-in and UI:** `PresetManager` (factory list + files, prev/next, save), `FactoryPresets` (21 + Init), programs; the processor records every block into the ExportRecorder, imports a .mid (parameters + `ChordEditorState::setImported`), writes Export's file and the test rig's take. `Source/ui/PresetBar` (browser call-out, Save dialog, Export handle); the editor takes .mid drops, plays QWERTY and flashes the imported keys; the pattern panel's Melody controls; the QWERTY preference in Setup. **A state restore now sets every saved parameter exactly**: APVTS skips a parameter whose value rounds to the saved one (a bool a host set to 0.35 is already "off"), which pluginval's state test caught once the Program parameter existed.
- **Host parameters (appended, IDs version 1):** *Pattern n arp* gained the choice *Melody*; *Pattern 1-5 notes*, *Pattern 1-5 rhythm*, *Melody swing*, *Melody seed*. **123 parameters**, plus the VST3's *Program*: Live shows 125 (§5).
- **Tests:** `Tests/test_melody.cpp` (new): the six rhythm templates put their notes where they should (40 seeds × 16 bars each), every bar has its Notes per bar, the walk stays in Range and steps by 1-2 three times in four (391,680 notes), the same seed/slot/bar gives the same bar and others differ; then 7 sweeps in the 26 FakeHost configurations, each note within 1 sample of the expected (an independent re-statement of §4.4): over C, following C → F pressed off the grid, Swing 60% + Gate 25% Dotted in two octaves, a one-bar loop (the same notes every pass), stopped (starts on the press), through SS Stack +1, and Test smart Pattern 1 (the LiveBridge case); and seed 4 vs 3. `Tests/test_midiio.cpp` (new): file round trip and hand-made files (type 0, running status, velocity-0 note-offs, SysEx, overlapping notes, SMPTE/garbage/truncated refused); the recorder (nothing before the first bar, 6.5 bars → bars 1-6 to the tick in all 26 configurations, 40 bars → the last 32, cut at the last bar line, a loop as a tape, a take started on beat 3, a new take); **R6: 8 round trips** (C major Triads; D Dorian Sevenths Pop with edits; Eb Harmonic Minor Ninths Circle; A minor with the raised V, Strum Up 1/64, Stack +1, Chord Octave −1; F# Lydian Drop-2 Custom with repeated degrees; Bb Mixolydian Thirteenths Stack +1&2 Chord Octave +1; E Phrygian Dominant Open Triads with edits on every degree; G Hungarian Minor Neo-Soul): the take is exported as played, written, read and imported, and the imported chord zone plays each distinct chord of the take, in order, note for note; hand-made imports (key fits, inversions and slash chords, bVII/bVI/V/vi, C/C7/Cmaj9 on three degrees, G13, chords three octaves up, nine chords → seven, strums, a melody refused) and 1,999 random chord sets (every imported note was in the chord, every other note counted as dropped; all 1,000 sets of stacked-third chords exact). `run_tests.sh` runs both. The fuzz also randomises Melody (slots, notes, rhythm, swing, seed): 16 seeds × 5000 blocks, nothing stuck. `Tests/run_snapshot.sh`: pixel references **updated** (`--update`) for the deliberate UI change (top bar, pattern panel, Setup); new checks: every factory program loads its values over the defaults, Init is the defaults, a preset's state round-trips without touching routing or the test rig and leaves out the editor's preferences, a load ends sounding notes, program and name restore with the state; a .mid import through the processor (parameters, edits, the chords played); Export under a playing host (2 bars → 8 chords in *Chordism C Major 2 bars.mid*); QWERTY in both layouts and a key press through the editor; **123 parameters, all with a control**.
- **Host validation:** both projects build (Release); `Tests/run_validation.sh` passes: `auval` aumu + aumi, `pluginval` 1.0.4 at strictness 10 on the VST3, the AU instrument and the AU MIDI effect (the VST3 also with seeds 0x2b9e82a, 0x1234, 0x777, which failed before the exact restore).
- **Live (`LiveBridge/tests/test_phase6_live.py`, VST3 MIDI From, 44.1 kHz, buffer 1024), all 3 pass:** *melody-seed*: Test smart Pattern 1 in Melody mode, seed 7: all 31 notes are the generator's (a Python port of Melody.h, checked against the C++ on 3,240 bars) to 0.1 ms; a second take is identical; seed 8 differs. *preset*: the 22 programs are listed by name through the Program parameter; House Stabs, Jazz Two-Five, Bright Melody and Init load and Live shows their parameters; Enable, the virtual port and the Test pattern stay. *export*: a 4-bar take (Pop, Sevenths, Strum Up 1/64): the file Chordism wrote (64 notes, 960 PPQ) equals the recorded clip within a tick, and Live's own import of it (the Remote Script's new `load_clip_file`, through the User Library) is a 16-beat clip with the same 64 notes. New bridge commands: `get_presets`, `set_preset`, `load_clip_file`. **The Phase 1-4 Live tests pass on this build** (Phase 1: 5 passed, the opt-in trim sweep skipped; Phases 2-4: 13 passed; `test_phase4_live.py` and `test_phase5_live.py` know *Melody* is the 13th arp choice now); Phase 5's *prepare* stage passes (Live shows 124 parameters, Program included).
- **Not done yet / by hand:** the Export *drag gesture* into Live and a .mid *dropped* on the editor in Live weren't tried by hand in this phase (LiveBridge can't drive a plug-in window; Phase 0 confirmed `performExternalDragDropOfFiles` lands in Live as a clip; the files and Live's import of them are tested). Phase 5's reopen stage (an edit by hand, save, reopen) wasn't repeated. The QWERTY keys weren't tried inside Live's plug-in window (Live may keep some keys). Only 3 parameters are left for Phase 7 (§5).

**Phase 5 (2026-10-19):**
- **UI (`Source/ui`):** `Theme` (colours, LookAndFeel), `ParamControl` (controls bound to parameters, the reachability record, `StepSeqView`), `Keyboards` (chord keys, Smart keys, output keyboard, on-screen playing), `ChordEditorPanel` (7 × 7 grid: on/off, ♭/♯ corners or the mouse wheel, five octave dots per step, Reset per chord and Reset all), `Panels` (top bar, Chord FX, pattern slot + Smart effects, Setup), `ChordismEditor` (layout, 30 Hz refresh, preferences). Decisions are in §6.
- **Core and plug-in:** `ChordTypes.h` (27 chord types for the Active Chord menu; offered only where no step needs more than a double accidental) and `ChordEditorState::setChordType`. The processor takes on-screen keys through a lock-free FIFO, publishes which output notes sound, which keys are down and which keys' performances play (atomic bit sets), and saves the UI's preferences.
- **Tests:** `Tests/test_chords.cpp`: every chord type on every degree of every scale, from three starting groups, is named as its type by ChordNamer (7,371 chords). `Tests/run_snapshot.sh` (new; `Tests/Snapshot/ChordismSnapshot.jucer`, a console app with the real processor and editor): four snapshots (main page, Chord Editor page, Setup, smallest size) against `Tests/fixtures/snapshot/*.png` (at most 0.5% of pixels may differ by more than 24 levels; `--update` after a deliberate change); all **111 parameters have a control**; a saved state restores every parameter, the edits and the UI preferences in a new instance; a pre-Phase 4 state's step bools become the packed patterns.
- **Host validation:** both projects build (Release); `Tests/run_validation.sh` passes (pluginval opens the editor at strictness 10 on the VST3 and both AUs).
- **Live (`LiveBridge/tests/test_phase5_live.py`, two stages around a save by hand):** *prepare* sets distinctive parameters (D Dorian, Sevenths, Pop, Absolute, a sequencer pattern, a pattern slot, SS hold, Note repeat; Live shows all 111); a person makes a Chord Editor edit and saves; Live reopens the set (`restart_live.sh <set>`); *restored* finds **every parameter as saved** (110, plus Test pattern, which the rig turns off before the save) and records the Pop progression with the edited chord i on all three of its beats. (The first saves held no edit because the edit hadn't been made yet; a temporary state log showed it.)
- **Not done yet:** the snapshot test isn't in CI (pixel references are tied to one macOS's font rendering); QWERTY playing, presets, Sync group and Export (Phases 6–7).

**Phase 4 (2026-10-19):**
- **Core (plain C++, `Source/core/engine`):** `ScaleMapper.h` (the five mappings, Inversions, the accidentals rule, Absolute spelling, and the chord tones a pattern key runs over), `PatternEngine.h` (Arp Chord, the five pattern slots and their factory settings, the Smart zone's settings, and the voicing that gives each Smart key its tones and arp). `ChordFx.h`'s `ChordPerformer` now also runs the Smart zone: a voicing, a source base, per-performance arp settings, stacking after the pattern, no restrike of sounding tones, `retuneAll`, and a sibling check before re-anchoring a stopped clock. `ChordismEngine` routes Smart keys to it, follows the current chord (chord press, shifter, settings) and runs both queues in sample order. `InputRouter` finds the Smart keys for a letter or pattern slot; `TestPattern` has *Test smart*; `VoiceTracker` has 640 sources. Decisions are in §4.3 and §4.5.
- **Host parameters:** the 64 *Seq n step m* bools became *Seq 1–4 steps* (16-bit masks, shown as `x...x...x...x...`); a saved state's old steps are folded into them on load (`ChordismProcessor::setStateInformation`). Appended: Mapping, Inversions, Note repeat, Pattern slot, SS octave, SS stack, SS step seq, SS seq pattern, SS seq swing, SS seq gate, SS seq sustain, SS velocity, SS velocity peak, SS velocity range, SS hold, SS seq 1–4 steps, Pattern 1–5 arp / rate / range / chord / gate, Test smart. **111 parameters in all** (Live's limit is 128, §5). The other IDs and their order are unchanged.
- **Tests:** `Tests/test_smart.cpp` (new, in `run_tests.sh`; the fake host moved from `test_fx.cpp` to `Tests/FakeHost.h`): Chord Tones, Dynamic, Relative and Chromatic × Inversions × three Smart octaves in 12 keys × 13 scales × 7 degrees × 6 chords each (group shapes and random edits with accidentals on steps on and off), checked against §4.3 written out again: 6,486,480 keys, all match. Absolute against hand-written key signatures for the 12 major and 12 natural minor keys, and in all 156 key/scale pairs it plays the scale's 7 tones within ±2 of each letter. Worked examples (vi in C in every mapping, D major Absolute, C minor V7's accidentals, pattern chord tones). The zone in the engine: following the last chord, held tones not restruck, a Key/Scale shifter, a pitch shared with a chord (two owners), Stack and SS octave, Chromatic, Mute SS, Hold Latch. 12 sweeps in the 26 configurations of Phase 3: Pattern 1 over C → F → Am (one change off the grid) following at its next step without restarting, Pattern 2 through SS Stack, step sequencer and velocity, Pattern 1 across 3.5-beat loops (three host loop styles), Pattern 3 while stopped, Hold Latch on a pattern, Note Repeat following a chord change and using the selected slot's rate, and the test pattern's Tones and Pattern 1. Every note within 1 sample (2 for a host that drops the wrap's overshoot). `Tests/test_engine.cpp`'s fuzz also randomises every Smart zone setting, Test smart, and checks both queues' bounds: 16 seeds × 5000 blocks, Smart performances in about a fifth of the blocks, nothing stuck. The Phase 1 pass-through checks now use Chromatic.
- **Host validation:** both projects build (Release); `Tests/run_validation.sh` passes: `auval` aumu + aumi, and `pluginval` 1.0.4 at strictness 10 on the VST3, the AU instrument and the AU MIDI effect.
- **Live (`LiveBridge/tests/test_phase4_live.py`, VST3 MIDI From):** five cases on the Test pattern with SS octave +1: Chord Tones, Dynamic + Inversions, Absolute in D major, Relative + Note repeat, and Pattern 1 held for the take. Each beat's chord and Smart notes are checked for pitch, start and end (0.1 ms). **All 5 pass** (44.1 kHz, buffer 1024): every note within ±0.5 sample (±0.011 ms) of its exact time, the rounding of sixteenth-note positions to whole samples (the three Tones cases land at 0.000 ms). The take keeps notes that start before its last half beat (`_record`), so the expectations do too. The Phase 1–3 Live tests, rerun on this build (packed sequencer steps, 111 parameters), pass: 13 passed, plus the opt-in trim sweep skipped; VST3 at 0.000 ms (±0.011 ms for arps and repeats), the port one buffer late as in §3.
- **Not done yet:** Melody mode (P2, Phase 6); look-ahead (Phase 7); the Smart Scale UI (Phase 5).

**Phase 3 (2026-10-19):**
- **Core (plain C++, `Source/core/engine`):** `Clock.h` (host transport or an internal clock at the last tempo; splits a block at each loop end; reports locates and transport starts as jumps), `EventQueue.h` (1024 events, musical or absolute-sample time, ties: note-offs, input, note-ons), `NoteValues.h` (rates and the grid), `Stack.h`, `Strum.h`, `Arp.h`, `Repeat.h`, `StepSequencer.h`, `VelocityAlgorithm.h`, `Hold.h`, and `ChordFx.h`: the chord-zone chain and its scheduler (one performance per chord key; at most 4 queued events each, so the queue can't overflow). `ChordismEngine` processes each block in the clock's segments and plays chord keys through the performer. The Test pattern runs per segment. Decisions are in §4.5.
- **Host parameters (appended after the 41, IDs version 1):** Stack, Strum, Strum rate, Strum time (ms), Arp, Arp rate, Arp range, Arp gate, Repeat, Repeat rate, Repeat gate, Step seq, Seq pattern, Seq swing, Seq gate, Seq sustain, Velocity, Velocity peak, Velocity range, Hold, and Seq 1–4 step 1–16 (64 bools, defaulting to the factory patterns). 126 parameters in all (Live lists 126 plus Device On; packed in Phase 4). The processor now passes the host's loop points and bar start to the engine.
- **Tests:** `Tests/test_fx.cpp` (new, in `run_tests.sh`): Clock (segments, jumps, tempo, anchor, bars), EventQueue, Stack, Strum orders, all 12 arp patterns against hand-written tables, step-grid windows (swing, sustain, 3/4), velocity curves; then 26 sweeps, each in 26 configurations (44.1/48/96 kHz × blocks 32, 64, 128, 333, 512, 1024, 2048 at 120 BPM, plus 97 and 174 BPM): strum (Up, Down, Alternating on repeats, cut short, in ms while stopped), arp (4 patterns, 1/8T off the grid, a shifter mid-arp, while stopped), repeat on and off the grid, step sequencer (press inside and late in a step, swing, sustain, with repeat and velocity, gating an arp), Hold Latch / Bar / while stopped, Stack and restrikes, loops of 4 and 3.5 beats with hosts that do and don't split blocks at the loop end, strummed repeats and steps across loops, tempo changes up and down, transport start under a stopped arp, a locate under a repeat, and determinism (Random strum identical across runs, block sizes and takes). Every note lands within 1 sample of its musical time (3 for a host that drops the wrap's overshoot, which loses up to a sample of song time per wrap). `Tests/test_engine.cpp`'s fuzz now also randomises every effect, Hold, loops that wrap inside blocks, and time signatures, and checks the queue bound: 16 seeds × 5000 blocks (8 of them effect-heavy and looping), nothing stuck.
- **Host validation:** both projects build (Release); `Tests/run_validation.sh` passes: `auval` aumu + aumi, and `pluginval` 1.0.4 at strictness 10 on the VST3, the AU instrument and the AU MIDI effect.
- **Live (`LiveBridge/tests/test_phase3_live.py`, VST3 MIDI From):** five cases on the Test pattern: Strum Up 1/32, Strum Down 1/64, Arp Linear Up 1/32 (gate 50%), Arp Converge Down 1/32 (range 2), Repeat 1/16 with Velocity Up. Each note's pitch, start, end (within 0.1 ms) and, for the last, velocity are checked. **All 5 pass** (44.1 kHz, buffer 1024): every note within ±0.5 sample (±0.011 ms) of its exact time, which is the rounding of 1/32- and 1/64-note positions to whole samples.
- **Live needs `-_PluginAutoPopulateThreshold=128` in Options.txt** (added 2026-10-19). Live puts a plug-in's parameters on its device panel by itself only up to 64, and LiveBridge reaches only the panel; 128 is Live's maximum. So **only the first 128 host parameters are reachable from LiveBridge** (and per Live's manual a plug-in over the threshold gets an empty panel). Phase 4 packed the steps (§5).
- **Not done yet:** a Live check of loop wraps (the LOM test records with the loop off; loops are covered by the fake-transport tests). The Step Sequencer's and the effects' UI (Phase 5).

**Phase 2 (2026-10-19):**
- **Core (plain C++):** `Source/core/theory`: `ChordEditor.h` (edits per degree on top of a group's shapes, and the editor operations: on/off, accidental, stacked octaves, reset), `Progressions.h`, `ChordNamer.h` (pitch-class set + bass → root, quality, extensions, alterations, slash bass; spelling in the key), `RomanNumeral.h`. `Source/core/engine`: `Shifters.h`, `SnapshotExchange.h`, and `ChordismEngine` now builds its chords from Progression + Chord Group + edits + the active shifter, and retriggers held chords whose chord changed (parameter, edit, progression or shifter). Design decisions are in §4.2.
- **Plug-in:** `Source/plugin/ChordEditorState` holds the edits as a ValueTree (`<ChordEditor group><Degree index><Step index on acc oct/>…`), saved inside the plug-in state next to the parameters, and publishes each change to the audio thread through a `SnapshotExchange` (no locks or allocation in `processBlock`). A 20 Hz timer clears the stored edits when the Chord Group changes. There is no editor UI for the matrix yet (Phase 5); the interim editor shows the seven chords with Roman numeral and name, the last one played and the active shifter.
- **Host parameters (appended, IDs version 1):** Progression, Custom 1–7, Shifter mode, Shifter 1–5 type / group / key / scale (key and scale lists start with *Same*), and Test shifter (Off, 1–5: the test pattern holds that shifter through every odd bar). 41 parameters in all.
- **Tests:** `Tests/test_chords.cpp`: the R3 reference set, 62 chords (triads, sus, power, 6ths, sevenths incl. dim7, m(maj7), 7#5, 7b5, 9ths, add9, 9sus4, 11ths, #11, 13ths, altered dominants b9 #9 b13 #5, Neapolitan bII, inversions, drop-2, drop-3, drop-2&4, open and stacked voicings, and other keys and scales), each built with the editor operations, played through the engine, and checked for exact MIDI notes, name (from the slot and from the bare pitch set) and, for 29, Roman numeral; naming and spelling tests; Roman numerals for whole scales. `Tests/test_engine.cpp`: edits (apply, follow while held, group reset, restore), progressions, shifters (both modes, hand-over, targets, edits), the test shifter in 3/4 and 4/4, a two-thread snapshot test (200,000 publishes, nothing torn), and the stuck-note fuzz now also edits chords, changes progressions, custom degrees, shifter mode and targets and presses shifters while notes are held: 8 seeds × 5000 blocks, nothing stuck.
- **Host validation:** both projects build (Release); `Tests/run_validation.sh` passes: `auval` aumu + aumi, and `pluginval` 1.0.4 at strictness 10 on the VST3, the AU instrument and the AU MIDI effect.
- **Live (`LiveBridge/tests/test_phase2_live.py`, 44.1 kHz, buffer 1024, VST3 MIDI From):** all 3 cases pass. The Pop progression plays its degrees on all 16 beats. Test shifter 1 (Sevenths) and 4 (parallel minor, Standard mode) give the shifted chords on the odd bars and the triads on the even bars, matching the music21 reference. Every note lands at 0.000 ms.
- **Not done yet:** the Active Chord / chord-type menus (they need the UI, Phase 5); voice leading (P2, Phase 7).

**Phase 1 (2026-10-19):**
- **Projects:** `Chordism.jucer` (VST3, AU `aumu`, Standalone) and `MidiFX/ChordismMidiFX.jucer`
  (AU `aumi`), both on `Source/`. Identifiers are in §3. Ad-hoc signed by Xcode's default (`-`).
- **Core (plain C++):** `Source/core/theory` (13 scales, 17 Chord Groups, `ChordSlot`/`Step`,
  default accidentals) and `Source/core/engine` (`InputRouter`, ref-counted `VoiceTracker`, plain
  chord output, `TestPattern`, `PortTiming`, `ChordismEngine`). The plug-in is a thin adapter.
  In Phase 1 the Smart Scale zone passes its notes through (mappings are Phase 4), and the chord
  zone's black keys are consumed (shifters are Phase 2).
- **Host parameters:** Enable, Key, Scale, Chord Group, Split, Chord Octave, Mute CG, Mute SS,
  Test pattern, Virtual port, Port compensation, Port trim. The Test pattern presses chord-zone
  key C, D, E… B, C… on each beat (slot = beat mod 7), an eighth note long, through the same path as the user's keys.
- **Tests:** `Tests/run_tests.sh`: all 18,564 chords (12 keys × 13 scales × 17 groups × 7)
  match the music21 reference `Tests/fixtures/theory_reference.json` (written by
  `Tools/gen_theory_fixture.py`, music21 10.5). Router, VoiceTracker, chord output, the test
  pattern under a fake transport (32–2048-sample blocks, 44.1/48/96 kHz), port stamps, and
  stuck-note fuzzing (8 seeds × 5000 blocks of random keys, settings, transport jumps and block
  sizes: no double note-on, no stray note-off, nothing left sounding).
- **Host validation:** `Tests/run_validation.sh`: `auval` aumu + aumi and `pluginval` 1.0.4 at
  strictness 10 on the VST3, the AU instrument and the AU MIDI effect: all pass.
  `.github/workflows/build.yml` runs the core tests, both builds, auval and pluginval on macOS;
  it hasn't run on GitHub yet (nothing pushed).
- **Live (`LiveBridge/tests/test_phase1_live.py`):** the recorded chords match the reference
  on all 16 beats for C major Triads and E♭ Dorian Sevenths, through VST3 MIDI From and the
  port, at 44.1 and 96 kHz, and through the AU instrument via the port. Timing is in §3.
- **Not checked yet:** the `aumi` build in Logic (auval and pluginval only); Live shows the AU's
  integer parameters (Split, Chord Octave) as normalised numbers (the VST3 shows C3 and 0).

**Phase 0 in Live 12.4.6:**
- **Drag MIDI out: passed** (round 1). Dropped clips are exactly on the grid.
- **MIDI to another track: passed** (round 2). Live lists the VST3 under "MIDI From", and the CoreMIDI virtual port records Chordism's chords. In round 1 the routing was left on "Post FX".
- **Hosting: passed** (round 2). The hosted instrument and its settings are saved and restored. Fixes along the way: tell the host when state changes, keep a pending or failed restore in the next save, and show the restored instrument in the UI.
- **Instance Sync: passed** (round 2), once Sync was on for both instances. Live runs all instances in one process.

**Other findings:** a JUCE host gets no MIDI output from the AU *instrument* build. That doesn't matter for Live (it uses the port), and the Logic `aumi` target passes auval and pluginval; it hasn't been tried in Logic.
`pluginval` 1.0.4 is installed in `~/Applications/pluginval.app` (Phase 1).
