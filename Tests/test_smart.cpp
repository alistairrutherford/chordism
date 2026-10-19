// Phase 4 tests (PLAN.md §7 row 4, §8): the Smart Scale zone.
//
//   mappings     ScaleMapper against a reference written from §4.3 (and hand-written key
//                signatures for Absolute), in all 12 keys × 13 scales × 7 degrees, with the
//                Chord Editor accidentals rule, Inversions and the Smart Octave
//   engine       the zone in ChordismEngine: following the last chord slot, a Key/Scale shifter,
//                Chromatic, Mute SS, shared pitches between the zones, Stack, the test pattern
//   timing       patterns that follow chord changes without restarting, Note Repeat, the Smart
//                zone's Step Sequencer, Velocity, Stack and Hold, loop wraps and a stopped
//                transport, at 32-2048-sample blocks and 44.1/48/96 kHz (the fake host of
//                FakeHost.h): every note within one sample of its musical time
#include "FakeHost.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <set>
#include <sstream>
#include <vector>

using namespace chordism;
using namespace fakehost;
using testutil::check;

namespace
{
    constexpr int whiteOffs[7] = { 0, 2, 4, 5, 7, 9, 11 };

    int letterOf (int note)
    {
        for (int j = 0; j < 7; ++j)
            if (whiteOffs[j] == ((note % 12) + 12) % 12)
                return j;
        return -1;
    }

    // Scale index i (0 = the tonic at `tonic`) as a pitch, from the scale's semitone list.
    int scalePitch (Scale s, int tonic, int i)
    {
        const auto& st = scaleInfo (s).semitones;
        int octave = 0;
        while (i < 0) { i += 7; --octave; }
        octave += i / 7;
        return tonic + st[(size_t) (i % 7)] + 12 * octave;
    }

    // The chord step (0 = root … 6 = 13th) that lies `offset` scale steps above the root.
    int stepAt (int offset)
    {
        for (int k = 0; k < 7; ++k)
            if ((2 * k) % 7 == ((offset % 7) + 7) % 7)
                return k;
        return 0;
    }

    // §4.3, written out again: the pitch a white key plays (-1: none). Not for Absolute.
    int reference (Mapping m, bool inversions, int octave, int key, Scale s, const ChordSlot& chord, int note)
    {
        if (m == Mapping::Chromatic)
        {
            const int p = note + 12 * octave;
            return p >= 0 && p < 128 ? p : -1;
        }
        const int j = letterOf (note);
        if (j < 0)
            return -1;
        const int c = note - note % 12 + 12 * octave;
        const int tonic = c + key;
        auto acc = [&] (int offset)
        {
            const Step& st = chord.steps[(size_t) stepAt (offset)];
            return st.on ? st.accidental : 0;
        };
        int p = -1;
        if (m == Mapping::ChordTones)
            p = scalePitch (s, tonic, chord.degree + 2 * j) + acc (2 * j);
        else if (m == Mapping::Dynamic)
            p = scalePitch (s, tonic, chord.degree + j) + acc (j);
        else if (m == Mapping::Relative)
            p = scalePitch (s, tonic, j);
        if (inversions && (m == Mapping::ChordTones || m == Mapping::Dynamic))
        {
            while (p < tonic) p += 12;
            while (p >= tonic + 12) p -= 12;
        }
        return p >= 0 && p < 128 ? p : -1;
    }

    // Absolute: the pitch class each white letter (C … B) takes, in the major keys as the Key
    // menu spells them (Db, Eb, F#, Ab, Bb, B), from their key signatures.
    const int majorSignature[12][7] = {
        { 0, 2, 4, 5, 7, 9, 11 },    // C
        { 0, 1, 3, 5, 6, 8, 10 },    // Db: Bb Eb Ab Db Gb
        { 1, 2, 4, 6, 7, 9, 11 },    // D: F# C#
        { 0, 2, 3, 5, 7, 8, 10 },    // Eb: Bb Eb Ab
        { 1, 3, 4, 6, 8, 9, 11 },    // E: F# C# G# D#
        { 0, 2, 4, 5, 7, 9, 10 },    // F: Bb
        { 1, 3, 5, 6, 8, 10, 11 },   // F#: F# C# G# D# A# E#
        { 0, 2, 4, 6, 7, 9, 11 },    // G: F#
        { 0, 1, 3, 5, 7, 8, 10 },    // Ab: Bb Eb Ab Db
        { 1, 2, 4, 6, 8, 9, 11 },    // A: F# C# G#
        { 0, 2, 3, 5, 7, 9, 10 },    // Bb: Bb Eb
        { 1, 3, 4, 6, 8, 10, 11 },   // B: F# C# G# D# A#
    };
    const int gFlatMajor[7] = { 11, 1, 3, 5, 6, 8, 10 };   // Cb Db Eb F Gb Ab Bb (Eb minor)

    int nearest (int whitePitch, int pc)
    {
        const int d = ((pc - whitePitch % 12) % 12 + 18) % 12 - 6;   // -6..5
        return whitePitch + d;
    }

    // A few chord slots per degree: the group shapes, and edits with accidentals (some on
    // steps that are off, which must not count).
    std::vector<ChordSlot> chordsFor (Scale s, int degree, std::mt19937& rng)
    {
        std::vector<ChordSlot> v;
        for (auto g : { ChordGroup::Triads, ChordGroup::Thirteenths, ChordGroup::Power })
            v.push_back (makeGroupSlot (g, s, degree));
        for (int n = 0; n < 3; ++n)
        {
            ChordSlot c;
            c.degree = degree;
            for (auto& st : c.steps)
            {
                st.on = std::uniform_int_distribution<int> (0, 1) (rng) == 1;
                st.accidental = (int8_t) std::uniform_int_distribution<int> (-2, 2) (rng);
            }
            v.push_back (c);
        }
        return v;
    }

    // Notes on in the output, in order.
    std::vector<int> ons (const BlockEvents& out)
    {
        std::vector<int> v;
        for (const auto& e : out)
            if (e.isNoteOn())
                v.push_back (e.note());
        return v;
    }

    std::vector<int> offs (const BlockEvents& out)
    {
        std::vector<int> v;
        for (const auto& e : out)
            if (e.isNoteOff())
                v.push_back (e.note());
        return v;
    }

    // The engine, a block at a time, stopped (no timed effects unless asked).
    struct Rig
    {
        std::unique_ptr<ChordismEngine> engine = std::make_unique<ChordismEngine>();
        EngineSettings settings;
        Transport transport { 48000.0, false, 0.0, 120.0, 4, 4 };
        BlockEvents out;
        testutil::Receiver rx;

        BlockEvents& run (std::vector<MidiEvent> in, int n = 256)
        {
            out.clear();
            engine->process (settings, transport, in.data(), (int) in.size(), n, out);
            rx.feed (out, n);
            return out;
        }
        static MidiEvent on (int note, int vel = 100) { return MidiEvent::noteOn (0, note, vel, 0); }
        static MidiEvent off (int note) { return MidiEvent::noteOff (0, note, 0); }
    };
}

//==============================================================================
static void testMappingTables()
{
    std::cout << "== mapping tables (all keys, scales, degrees) ==\n";
    std::mt19937 rng (4);
    const Mapping maps[] = { Mapping::ChordTones, Mapping::Dynamic, Mapping::Relative, Mapping::Chromatic };
    long checked = 0;
    int bad = 0;
    std::string first;
    for (int key = 0; key < numKeys; ++key)
        for (int s = 0; s < numScales; ++s)
            for (int d = 0; d < 7; ++d)
                for (const auto& chord : chordsFor ((Scale) s, d, rng))
                    for (auto m : maps)
                        for (bool inv : { false, true })
                            for (int octave : { -1, 0, 2 })
                            {
                                ScaleMapper mapper;
                                mapper.update ({ m, inv, octave }, { key, (Scale) s, chord });
                                for (int note = 36; note < 96; ++note)
                                {
                                    if (m != Mapping::Chromatic && letterOf (note) < 0)
                                        continue;
                                    const int want = reference (m, inv, octave, key, (Scale) s, chord, note);
                                    const int got = mapper.tone (note);
                                    ++checked;
                                    if (got != want && bad++ == 0)
                                    {
                                        std::ostringstream o;
                                        o << " first: " << mappingName (m) << (inv ? " inv" : "") << " oct " << octave << " key " << key
                                          << " " << scaleInfo ((Scale) s).name << " degree " << d << " note " << note << ": got " << got
                                          << ", want " << want;
                                        first = o.str();
                                    }
                                }
                            }
    check (bad == 0, "Chord Tones, Dynamic, Relative, Chromatic × Inversions × octave -1/0/+2 in 12 keys × 13 scales × 7 degrees: "
                         + std::to_string (checked) + " keys, " + std::to_string (bad) + " wrong" + first);

    // Absolute against key signatures: major keys, and natural minor (its relative major's).
    bool majorOk = true, minorOk = true;
    for (int key = 0; key < numKeys; ++key)
    {
        ScaleMapper major, minor;
        major.update ({ Mapping::Absolute, false, 0 }, { key, Scale::Major, {} });
        minor.update ({ Mapping::Absolute, false, 0 }, { key, Scale::NaturalMinor, {} });
        const int* minorSig = key == 3 ? gFlatMajor : majorSignature[(key + 3) % 12];
        for (int j = 0; j < 7; ++j)
            for (int base : { 48, 60, 72 })
            {
                majorOk &= major.tone (base + whiteOffs[j]) == nearest (base + whiteOffs[j], majorSignature[key][j]);
                minorOk &= minor.tone (base + whiteOffs[j]) == nearest (base + whiteOffs[j], minorSig[j]);
            }
    }
    check (majorOk, "Absolute, 12 major keys: every letter plays its key-signature tone (D major F → F#, F major B → Bb)");
    check (minorOk, "Absolute, 12 natural minor keys: the relative major's signature (Eb minor: C → Cb = B below)");

    // Absolute in every key and scale: the seven letters give the scale's seven pitch classes,
    // each within two semitones of its key, rising from C to B.
    bool allScales = true;
    for (int key = 0; key < numKeys; ++key)
        for (int s = 0; s < numScales; ++s)
        {
            ScaleMapper m;
            m.update ({ Mapping::Absolute, true, 0 }, { key, (Scale) s, makeGroupSlot (ChordGroup::Sevenths, (Scale) s, 4) });
            std::set<int> pcs, want;
            int last = -100;
            for (int j = 0; j < 7; ++j)
            {
                const int p = m.tone (60 + whiteOffs[j]);
                pcs.insert (p % 12);
                want.insert ((key + scaleInfo ((Scale) s).semitones[(size_t) j]) % 12);
                allScales &= std::abs (p - (60 + whiteOffs[j])) <= 2 && p > last;
                last = p;
            }
            allScales &= pcs == want;
        }
    check (allScales, "Absolute, 12 keys × 13 scales: the scale's 7 tones, ±2 of each letter, rising (static: no Inversions, no chord)");

    // The worked examples (PLAN.md §4.3; C major, the vi chord, keys from 60).
    const ChordSlot vi = makeGroupSlot (ChordGroup::Triads, Scale::Major, 5);
    auto run = [&] (Mapping m, bool inv, int key, Scale s, const ChordSlot& c)
    {
        ScaleMapper mapper;
        mapper.update ({ m, inv, 0 }, { key, s, c });
        std::vector<int> v;
        for (int j = 0; j < 7; ++j)
            v.push_back (mapper.tone (60 + whiteOffs[j]));
        return v;
    };
    check (run (Mapping::ChordTones, false, 0, Scale::Major, vi) == std::vector<int> { 69, 72, 76, 79, 83, 86, 89 },
           "Chord Tones, vi in C: A C E G B D F (1-3-5-7-9-11-13 from A)");
    check (run (Mapping::ChordTones, true, 0, Scale::Major, vi) == std::vector<int> { 69, 60, 64, 67, 71, 62, 65 },
           "Chord Tones + Inversions: folded into C4-B4 (60-71)");
    check (run (Mapping::Dynamic, false, 0, Scale::Major, vi) == std::vector<int> { 69, 71, 72, 74, 76, 77, 79 },
           "Dynamic, vi in C: A Aeolian with A on C (69 … 79)");
    check (run (Mapping::Dynamic, true, 0, Scale::Major, vi) == std::vector<int> { 69, 71, 60, 62, 64, 65, 67 },
           "Dynamic + Inversions: E (C key) folds to C60, a lower pitch right of a higher one");
    check (run (Mapping::Relative, true, 2, Scale::Major, vi) == std::vector<int> { 62, 64, 66, 67, 69, 71, 73 },
           "Relative in D major: D on C, whatever the chord (Inversions ignored)");
    check (run (Mapping::Absolute, false, 2, Scale::Major, vi) == std::vector<int> { 61, 62, 64, 66, 67, 69, 71 },
           "Absolute in D major: C → C#, F → F#");

    // The Chord Editor accidentals rule: the raised 3rd of V in natural minor (a default
    // accidental) applies to the step that is on; a flattened 9th on an off step doesn't.
    ChordSlot v7 = makeGroupSlot (ChordGroup::Sevenths, Scale::NaturalMinor, 4);   // G B D F in C minor
    v7.steps[4].accidental = +1;                                                   // a natural 9th (A), but the step is off
    check (run (Mapping::ChordTones, false, 0, Scale::NaturalMinor, v7) == std::vector<int> { 67, 71, 74, 77, 80, 84, 87 },
           "C minor V7, Chord Tones: B natural (raised 3rd, on), Ab (the 9th's accidental is on an off step)");
    v7.steps[4].on = true;
    check (run (Mapping::ChordTones, false, 0, Scale::NaturalMinor, v7)[4] == 81 && run (Mapping::Dynamic, false, 0, Scale::NaturalMinor, v7)[1] == 69,
           "9th on: Chord Tones' 9th and Dynamic's 2nd become A");
    check (run (Mapping::Relative, false, 0, Scale::NaturalMinor, v7)[6] == 70 && run (Mapping::Absolute, false, 0, Scale::NaturalMinor, v7)[6] == 70,
           "Relative and Absolute are static: Bb stays Bb under V7");

    // Pattern chord tones: stacked thirds from R in the key's octave.
    ScaleMapper m;
    m.update ({ Mapping::Relative, false, 0 }, { 0, Scale::Major, vi });
    auto tones = [&] (int note, int count)
    {
        const auto t = m.chordTones (note, count);
        return std::vector<int> (t.begin(), t.end());
    };
    check (tones (61, 2) == std::vector<int> { 69, 76 } && tones (61, 3) == std::vector<int> { 69, 72, 76 }
               && tones (73, 7) == std::vector<int> { 81, 84, 88, 91, 95, 98, 101 },
           "pattern tones of vi: 5ths A E, Triads A C E; 13ths an octave up from C#5 (whatever the Mapping)");
    m.update ({ Mapping::Relative, false, -1 }, { 0, Scale::Major, {} });
    check (tones (66, 4) == std::vector<int> { 48, 52, 55, 59 }, "before any chord: the tonic (C E G B), Smart Octave -1");
}

//==============================================================================
static void testEngineZone()
{
    std::cout << "== Smart Scale zone in the engine ==\n";
    Rig r;   // Chord Tones, C major Triads, split 60

    check (ons (r.run ({ Rig::on (64) })) == std::vector<int> { 67 }, "before any chord: E key = the 5th of the tonic chord (G)");
    r.run ({ Rig::off (64) });

    r.run ({ Rig::on (50) });                                      // ii: D F A
    check (ons (r.run ({ Rig::on (60), Rig::on (64) })) == std::vector<int> { 62, 69 }, "after ii: C key = D, E key = A");
    auto& changed = r.run ({ Rig::off (50), Rig::on (53) });      // IV while the tones are held
    check (ons (changed) == std::vector<int> { 53, 57, 60 } && offs (changed) == std::vector<int> { 50, 53, 57 },
           "a chord change doesn't restrike held Smart Scale tones");
    check (offs (r.run ({ Rig::off (60), Rig::off (64) })) == std::vector<int> { 62, 69 }, "their key-ups end exactly what they started");
    check (ons (r.run ({ Rig::on (60) })) == std::vector<int> { 65 }, "the next press follows IV (C key = F)");
    r.run ({ Rig::off (60), Rig::off (53) });
    check (ons (r.run ({ Rig::on (62) })) == std::vector<int> { 69 }, "the chord stays current after its key is up (D key = A, IV's 3rd)");
    r.run ({ Rig::off (62) });

    // A Key/Scale shifter: the Smart Scale follows the chord it shifts to (shifter 4 = C minor).
    r.run ({ Rig::on (48), Rig::on (56) });
    check (ons (r.run ({ Rig::on (62) })) == std::vector<int> { 63 }, "with the parallel-minor shifter held: D key = Eb (Cm's 3rd)");
    r.run ({ Rig::off (62), Rig::off (56), Rig::off (48) });

    // Shared pitches: a chord's G and a Smart Scale G are counted, not cut.
    r.settings.chordOctave = 1;                                     // chords from C4
    r.run ({ Rig::on (48) });                                       // C E G = 60 64 67
    check (ons (r.run ({ Rig::on (64) })).empty() && r.engine->getVoices().ownerCount (0, 67) == 2,
           "Smart G on the chord's G: one note, two owners");
    check (offs (r.run ({ Rig::off (48) })) == std::vector<int> { 60, 64 }, "the chord's key-up keeps the Smart Scale's G");
    check (offs (r.run ({ Rig::off (64) })) == std::vector<int> { 67 } && r.engine->getVoices().isEmpty(), "and its own key-up ends it");
    r.settings = {};

    // Stack and Smart Octave.
    r.settings.smart.stack = StackMode::PlusOneAndTwo;
    r.settings.smart.map.octave = -1;
    check (ons (r.run ({ Rig::on (60) })) == std::vector<int> { 48, 60, 72 }, "Stack +1&2, SS octave -1: C3 C4 C5");
    r.run ({ Rig::off (60) });
    r.settings = {};

    // Chromatic: every key passes through, black keys too, still through the Smart chain.
    r.settings.smart.map.mapping = Mapping::Chromatic;
    r.settings.smart.stack = StackMode::PlusOne;
    check (ons (r.run ({ Rig::on (61) })) == std::vector<int> { 61, 73 }, "Chromatic: C# plays C# (no pattern), stacked");
    r.run ({ Rig::off (61) });
    r.settings = {};

    // Mute SS ends the zone's notes; nothing plays while muted.
    r.run ({ Rig::on (60) });
    r.settings.smart.noteRepeat = false;
    r.settings.muteScale = true;
    check (offs (r.run ({})) == std::vector<int> { 60 } && ons (r.run ({ Rig::on (62) })).empty(), "Mute SS ends the Smart Scale note");
    r.run ({ Rig::off (60), Rig::off (62) });
    r.settings = {};

    // Hold Latch: a pattern runs on after its key is up, until the next Smart Scale key.
    r.settings.smart.hold = HoldMode::Latch;
    r.run ({ Rig::on (61) }, 64);
    r.run ({ Rig::off (61) }, 64);
    check (r.engine->getSmartPerformer().isLatched (61), "Hold Latch: the pattern key's performance is held after key-up");
    r.run ({ Rig::on (62) }, 64);
    check (! r.engine->getSmartPerformer().isAlive (61), "a new Smart Scale key ends it");
    r.run ({ Rig::off (62) }, 64);
    r.settings = {};
    r.run ({});
    check (r.rx.clean() && r.rx.on.empty() && r.engine->getVoices().isEmpty(), "nothing stuck: " + r.rx.describe());
}

//==============================================================================
// Timing sweeps (every configuration of FakeHost.h).

static void testPatternTiming()
{
    std::cout << "== patterns ==\n";
    // Pattern 1 (Linear Up 1/16, Triads, gate 50%) held over C, F and Am (Am pressed off the grid).
    // It follows each change at its next step, and its step count runs on (index k mod 3).
    sweep ("Pattern 1 follows C → F → Am at its next step, without restarting", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.key (0.0, 48, true);
        h.key (0.0, 61, true);           // pattern 1
        h.key (1.0, 48, false);
        h.key (1.0, 53, true);
        h.key (2.37, 53, false);
        h.key (2.37, 57, true);
        h.key (3.0, 57, false);
        h.key (3.0, 61, false);
        h.run (h.at (3.5), c.block);

        Expect e (h.spq());
        for (int n : { 48, 52, 55 }) e.note (0.0, 1.0, n);
        for (int n : { 53, 57, 60 }) e.note (1.0, 2.37, n);
        for (int n : { 57, 60, 64 }) e.note (2.37, 3.0, n);
        const std::vector<int> C { 60, 64, 67 }, F { 65, 69, 72 }, Am { 69, 72, 76 };
        for (int k = 0; k < 12; ++k)
        {
            const double t = 0.25 * k;
            const auto& chord = t < 1.0 ? C : (t < 2.37 ? F : Am);
            e.note (t, t + 0.125, chord[(size_t) (k % 3)]);
        }
        return finish (h, e, worst);
    });

    // Pattern 2 (Linear Down 1/16, 7ths) through the Smart chain: Stack +1, the zone's Step
    // Sequencer (every other sixteenth, gate 100%) and Velocity Up (Peak 127, Range 100%).
    sweep ("Pattern 2 with SS Stack +1, step sequencer and velocity", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        auto& ss = h.settings.smart;
        ss.stack = StackMode::PlusOne;
        ss.seq.on = true;
        ss.seq.patterns[0] = 0x5555;
        ss.seq.gate = 1.0;
        ss.velocity = { VelocityCurve::Up, 127, 1.0 };
        h.key (0.0, 63, true);           // pattern 2
        h.key (2.0, 63, false);
        h.run (h.at (2.5), c.block);

        Expect e (h.spq());
        const std::vector<int> tones { 60, 64, 67, 71 };   // the tonic's 7th chord: no chord played yet
        for (int k = 0; k < 8; k += 2)
        {
            const double t = 0.25 * k;
            const int v = std::max (1, (int) std::lround (127.0 * (k % 16) / 15.0));
            const int n = tones[(size_t) (3 - k % 4)];
            e.note (t, t + 0.125, n, v);
            e.note (t, t + 0.125, n + 12, v);
        }
        return finish (h, e, worst);
    });

    for (auto style : { LoopStyle::Wrap, LoopStyle::Split, LoopStyle::SplitDropOvershoot })
    {
        const std::string styleName = style == LoopStyle::Wrap ? "host doesn't split" : style == LoopStyle::Split ? "host splits" : "host splits, drops the overshoot";
        sweep ("Pattern 1 at 1/4 across a 3.5-beat loop (" + styleName + ")", [style] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            h.looping = true;
            h.loopStart = 0.0;
            h.loopEnd = 3.5;
            h.style = style;
            h.settings.smart.patterns[0].rate = 1;
            h.key (0.0, 61, true);
            h.run (h.at (3 * 3.5 - 0.25), c.block);
            const int64_t stopAt = h.pos;
            h.stop();

            Expect e (h.spq());
            const std::vector<int> tones { 60, 64, 67 };
            int k = 0;
            for (int pass = 0; pass < 3; ++pass)
                for (int j = 0; j < 4; ++j)
                {
                    const double t = pass * 3.5 + j;
                    if (t >= 3 * 3.5 - 0.25)
                        break;
                    const int n = tones[(size_t) (k++ % 3)];
                    e.on (t, n);
                    const double end = std::min (t + 0.5, pass * 3.5 + 3.5);   // a step at 3 ends at the loop end
                    if (end < 3 * 3.5 - 0.25 + 1e-9) e.off (end, n); else e.offAt (stopAt, n);
                }
            return finish (h, e, worst, style == LoopStyle::SplitDropOvershoot ? 3 : 1);
        });
    }

    sweep ("Pattern 3 while stopped: Converge Up 1/8, range 2, from the key press", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.playing = false;
        const int64_t press = 555;
        h.keyAt (press, 66, true);       // pattern 3 (F#)
        h.keyAt (press + h.at (2.0), 66, false);
        h.run (press + h.at (2.5), c.block);

        Expect e (h.spq());
        const std::vector<int> tones { 60, 64, 67, 72, 76, 79 };
        for (int k = 0; k < 4; ++k)
        {
            e.onAt (press + h.at (0.5 * k), tones[(size_t) arpIndex (ArpPattern::ConvergeUp, 6, k)]);
            e.offAt (press + h.at (0.5 * k + 0.25), tones[(size_t) arpIndex (ArpPattern::ConvergeUp, 6, k)]);
        }
        return finish (h, e, worst);
    });

    sweep ("Hold Latch: pattern 1 runs on after its key-up until the next Smart Scale key", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.smart.hold = HoldMode::Latch;
        h.key (0.0, 61, true);
        h.key (0.6, 61, false);
        h.key (1.6, 62, true);           // D key: a new selection
        h.key (2.0, 62, false);          // held too, until Hold goes off
        h.run (h.at (2.5), c.block);
        const int64_t holdOff = h.pos;
        h.settings.smart.hold = HoldMode::Off;
        h.run (h.pos + c.block, c.block);

        Expect e (h.spq());
        const std::vector<int> tones { 60, 64, 67 };
        for (int k = 0; k < 7; ++k)
            e.note (0.25 * k, std::min (0.25 * k + 0.125, 1.6), tones[(size_t) (k % 3)]);
        e.on (1.6, 64);                  // Chord Tones, tonic chord: D key = the 3rd
        e.offAt (holdOff, 64);
        return finish (h, e, worst);
    });
}

static void testNoteRepeatTiming()
{
    std::cout << "== note repeat ==\n";
    // Note Repeat at pattern slot 1's Arp Rate and Gate (1/16, 50%), on the E key (Chord Tones:
    // the 5th), pressed off the grid; C changes to F at beat 1: the repeat there plays F's 5th.
    sweep ("Note Repeat 1/16 follows the chord at its next strike", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.smart.noteRepeat = true;
        h.key (0.0, 48, true);
        h.key (0.1, 64, true);
        h.key (1.0, 48, false);
        h.key (1.0, 53, true);
        h.key (2.0, 64, false);
        h.key (2.0, 53, false);
        h.run (h.at (2.5), c.block);

        Expect e (h.spq());
        for (int n : { 48, 52, 55 }) e.note (0.0, 1.0, n);
        for (int n : { 53, 57, 60 }) e.note (1.0, 2.0, n);
        e.note (0.1, 0.175, 67);
        for (int k = 1; k < 8; ++k)
            e.note (0.25 * k, 0.25 * k + 0.125, k < 4 ? 67 : 72);
        return finish (h, e, worst);
    });

    // The selected slot's rate: slot 3 (1/8, gate 50%), Relative.
    sweep ("Note Repeat uses the selected pattern slot (3: 1/8)", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.smart.noteRepeat = true;
        h.settings.smart.patternSlot = 2;
        h.settings.smart.map.mapping = Mapping::Relative;
        h.key (0.5, 65, true);           // F key: F
        h.key (2.0, 65, false);
        h.run (h.at (2.5), c.block);

        Expect e (h.spq());
        for (int k = 1; k < 4; ++k)
            e.note (0.5 * k, 0.5 * k + 0.25, 65);
        return finish (h, e, worst);
    });
}

static void testTestPattern()
{
    std::cout << "== test pattern (Test smart) ==\n";
    // Tones: on beat b the chord key of slot b mod 7 and the Smart Scale key 3b mod 7, both an
    // eighth long (what LiveBridge records).
    sweep ("Test smart Tones, Chord Tones, SS octave +1", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.testPattern = true;
        h.settings.testSmart = TestPattern::smartTones;
        h.settings.smart.map.octave = 1;
        h.run (h.at (8.0) - 1, c.block);
        h.settings.testPattern = false;
        h.run (h.pos + c.block, c.block);

        Expect e (h.spq());
        for (int b = 0; b < 8; ++b)
        {
            const int d = b % 7, j = (3 * b) % 7;
            for (int k = 0; k < 3; ++k)
                e.note (b, b + 0.5, 48 + scaleOffset (Scale::Major, d + 2 * k));
            e.note (b, b + 0.5, 72 + scaleOffset (Scale::Major, d + 2 * j));
        }
        return finish (h, e, worst);
    });

    sweep ("Test smart Pattern 1: held through the run, following every chord", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.testPattern = true;
        h.settings.testSmart = TestPattern::smartPattern;
        h.settings.smart.map.octave = 1;
        h.run (h.at (4.0) - 1, c.block);
        h.settings.testPattern = false;
        h.run (h.pos + c.block, c.block);

        Expect e (h.spq());
        for (int b = 0; b < 4; ++b)
            for (int k = 0; k < 3; ++k)
                e.note (b, b + 0.5, 48 + scaleOffset (Scale::Major, b + 2 * k));
        for (int k = 0; k < 16; ++k)
        {
            const int d = k / 4;
            const int n = 72 + scaleOffset (Scale::Major, d + 2 * (k % 3));
            e.note (0.25 * k, 0.25 * k + 0.125, n);
        }
        return finish (h, e, worst);
    });
}

int main()
{
    testMappingTables();
    testEngineZone();
    testPatternTiming();
    testNoteRepeatTiming();
    testTestPattern();
    return testutil::finish ("test_smart");
}
