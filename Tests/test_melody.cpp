// Phase 6: the Melody generator (PLAN.md §4.4, Melody.h).
//
//   generator   the Rhythm templates put Notes per bar where they should (Straight 4 = the
//               beats, Offbeat 4 = the "and"s, Dotted 6, Clave 5, all 16), bars of other
//               lengths scale, the walk stays in Range and moves mostly by step, strong notes
//               are on the beats; the same seed, slot and bar always give the same bar, and
//               other seeds or slots other bars
//   engine      a pattern key in Melody mode, in every FakeHost configuration: each note on its
//               sixteenth (with Swing), Gate × the time to the next note, the walk's scale tone
//               from the tonic in the key's octave, strong notes moved to the nearest chord tone
//               of the chord played last; it follows chord changes at its next note without
//               restarting, repeats across a loop, starts on its own bar while stopped, stacks,
//               and plays the same under Test smart (the LiveBridge case); a different seed
//               plays something else
#include "FakeHost.h"
#include <set>

using namespace chordism;
using namespace fakehost;
using testutil::check;

namespace
{
    constexpr int major[7] = { 0, 2, 4, 5, 7, 9, 11 };

    int majorOffset (int i) { return major[((i % 7) + 7) % 7] + 12 * (i >= 0 ? i / 7 : -((6 - i) / 7)); }

    std::vector<int> positions (const MelodyBar& b)
    {
        std::vector<int> p;
        for (int i = 0; i < b.count; ++i)
            p.push_back (b.notes[(size_t) i].pos);
        return p;
    }

    MelodySettings melodyOf (int notes, MelodyRhythm rhythm, uint32_t seed = 1, int range = 1, double gate = 0.5, double swing = 0.0, int slot = 0)
    {
        MelodySettings m;
        m.on = true;
        m.notes = notes;
        m.rhythm = rhythm;
        m.seed = seed;
        m.range = range;
        m.gate = gate;
        m.swing = swing;
        m.slot = slot;
        return m;
    }

    void testGenerator()
    {
        std::cout << "== generator ==\n";
        struct Template { MelodyRhythm r; int notes; std::vector<int> want; const char* what; };
        const Template templates[] = {
            { MelodyRhythm::Straight, 4, { 0, 4, 8, 12 }, "Straight, 4 notes: the beats" },
            { MelodyRhythm::Straight, 8, { 0, 2, 4, 6, 8, 10, 12, 14 }, "Straight, 8 notes: the eighths" },
            { MelodyRhythm::Offbeat, 4, { 2, 6, 10, 14 }, "Offbeat, 4 notes: the \"and\"s" },
            { MelodyRhythm::Dotted, 6, { 0, 3, 6, 9, 12, 15 }, "Dotted, 6 notes: every dotted eighth" },
            { MelodyRhythm::Clave, 5, { 0, 3, 6, 10, 12 }, "Clave, 5 notes: 3-2 son clave" },
            { MelodyRhythm::Free, 16, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, "Free, 16 notes: every sixteenth" },
        };
        for (const auto& t : templates)
        {
            bool ok = true;
            for (uint32_t seed = 1; seed <= 40 && ok; ++seed)
                for (int bar = 0; bar < 16 && ok; ++bar)
                    ok = positions (melody::generate (melodyOf (t.notes, t.r, seed), bar, 16)) == t.want;
            check (ok, std::string (t.what) + " (40 seeds × 16 bars)");
        }

        // Notes per bar, range, strong notes, steps.
        int wrongCount = 0, outOfRange = 0, wrongStrong = 0, steps = 0, moves = 0, total = 0;
        for (int n = minMelodyNotes; n <= maxMelodyNotes; ++n)
            for (int range = 1; range <= 3; ++range)
                for (int r = 0; r < numMelodyRhythms; ++r)
                    for (uint32_t seed = 1; seed <= 20; ++seed)
                        for (int bar = -2; bar < 6; ++bar)
                        {
                            const auto b = melody::generate (melodyOf (n, (MelodyRhythm) r, seed, range), bar, 16);
                            wrongCount += b.count != n ? 1 : 0;
                            for (int i = 0; i < b.count; ++i)
                            {
                                const auto& note = b.notes[(size_t) i];
                                outOfRange += note.index < 0 || note.index > 7 * range ? 1 : 0;
                                wrongStrong += note.strong != (note.pos % 4 == 0 || i == 0) ? 1 : 0;
                                if (i > 0)
                                {
                                    const int d = std::abs (note.index - b.notes[(size_t) i - 1].index);
                                    steps += d >= 1 && d <= 2 ? 1 : 0;
                                    moves += 1;
                                }
                                ++total;
                            }
                        }
        check (wrongCount == 0, "every bar has Notes per bar notes (1-16 × 3 ranges × 6 rhythms × 20 seeds × 8 bars)");
        check (outOfRange == 0 && wrongStrong == 0, "every note is within Range (0 .. 7 × range scale steps) and strong exactly on a beat or first ("
                                                    + std::to_string (total) + " notes)");
        check (steps * 100 >= moves * 70, "the walk moves by a step or two " + std::to_string (100 * steps / std::max (1, moves)) + "% of the time");

        const auto three = melody::generate (melodyOf (8, MelodyRhythm::Straight), 0, 12);
        bool short3 = three.count == 6;
        for (int i = 0; i < three.count; ++i)
            short3 &= three.notes[(size_t) i].pos < 12;
        check (short3, "a 3/4 bar (12 sixteenths) with 8 notes per 4/4 bar has 6, all inside it");

        // Same seed, same bar; other seeds and slots differ.
        auto same = [] (const MelodyBar& a, const MelodyBar& b)
        {
            if (a.count != b.count)
                return false;
            for (int i = 0; i < a.count; ++i)
                if (a.notes[(size_t) i].pos != b.notes[(size_t) i].pos || a.notes[(size_t) i].index != b.notes[(size_t) i].index)
                    return false;
            return true;
        };
        int repeat = 0, otherSeed = 0, otherSlot = 0, otherBar = 0;
        for (int bar = 0; bar < 100; ++bar)
        {
            const auto m = melodyOf (7, MelodyRhythm::Syncopated, 42, 2);
            auto m2 = m;
            m2.seed = 43;
            auto m3 = m;
            m3.slot = 3;
            repeat += same (melody::generate (m, bar, 16), melody::generate (m, bar, 16)) ? 1 : 0;
            otherSeed += same (melody::generate (m, bar, 16), melody::generate (m2, bar, 16)) ? 0 : 1;
            otherSlot += same (melody::generate (m, bar, 16), melody::generate (m3, bar, 16)) ? 0 : 1;
            otherBar += same (melody::generate (m, bar, 16), melody::generate (m, bar + 1, 16)) ? 0 : 1;
        }
        check (repeat == 100, "the same seed, slot and bar give the same bar (100/100)");
        check (otherSeed >= 95 && otherSlot >= 95 && otherBar >= 95,
               "another seed (" + std::to_string (otherSeed) + "/100), slot (" + std::to_string (otherSlot) + "/100) or bar ("
                   + std::to_string (otherBar) + "/100) gives another bar");
    }

    //==========================================================================
    // The expected notes of melody slot `m` from `from` to `to` (beats; key held throughout,
    // released at `to`), with the chord degree at each beat from `degreeAt`, in C major from
    // tonic `tonic`. Strong notes take the nearest chord tone (`count` stacked thirds).
    void expectMelody (Expect& e, const MelodySettings& m, double from, double to, const std::function<int (double)>& degreeAt,
                       int tonic = 60, int count = 3, int stack = 0)
    {
        const int firstBar = (int) std::floor (from / 4.0);
        for (int bar = firstBar; bar * 4.0 < to; ++bar)
        {
            const auto b = melody::generate (m, bar, 16);
            for (int i = 0; i < b.count; ++i)
            {
                const auto start = [&] (int k) { return k < b.count ? bar * 4.0 + b.notes[(size_t) k].pos * 0.25 + ((b.notes[(size_t) k].pos & 1) ? m.swing * 0.125 : 0.0) : bar * 4.0 + 4.0; };
                const double s = start (i);
                const double end = s + std::max (0.01, m.gate) * (start (i + 1) - s);
                if (s < from - 1.0e-9 || s >= to - 1.0e-9)
                    continue;
                int a = b.notes[(size_t) i].index;
                const int d = degreeAt (s);
                if (b.notes[(size_t) i].strong)
                    for (int delta : { 0, -1, 1, -2, 2, -3, 3 })
                    {
                        const int rel = (((a + delta - d) % 7) + 7) % 7;
                        bool tone = false;
                        for (int k = 0; k < count; ++k)
                            tone |= (2 * k) % 7 == rel;
                        if (tone)
                        {
                            a += delta;
                            break;
                        }
                    }
                const int pitch = tonic + majorOffset (a);
                for (int o = 0; o <= stack; ++o)
                    e.note (s, std::min (end, to), pitch + 12 * o);
            }
        }
    }

    void testEngine()
    {
        std::cout << "== the Melody in the engine ==\n";

        // Slot 1: Straight, 8 notes, gate 50%, seed 1, over C; two bars.
        sweep ("Melody (Straight, 8 notes) over C: every note on its sixteenth, its length and its pitch", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            auto& slot = h.settings.smart.patterns[0];
            slot.melody = true;
            slot.notes = 8;
            slot.rhythm = MelodyRhythm::Straight;
            h.key (0.0, 48, true);
            h.key (0.0, 61, true);
            h.key (8.0, 48, false);
            h.key (8.0, 61, false);
            h.run (h.at (8.5), c.block);
            Expect e (h.spq());
            for (int n : { 48, 52, 55 }) e.note (0.0, 8.0, n);
            expectMelody (e, melodyOf (8, MelodyRhythm::Straight, 1, 1, 0.5), 0.0, 8.0, [] (double) { return 0; });
            return finish (h, e, worst);
        });

        // A chord change off the grid: the next note takes the new chord; the rhythm runs on.
        sweep ("Melody follows C → F (pressed off the grid) at its next note, without restarting", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            auto& slot = h.settings.smart.patterns[1];
            slot.melody = true;
            slot.notes = 12;
            slot.rhythm = MelodyRhythm::Syncopated;
            slot.range = 2;
            slot.gate = 0.8;
            slot.chord = ArpChord::Sevenths;
            h.settings.smart.melodySeed = 77;
            h.key (0.0, 48, true);
            h.key (0.0, 63, true);        // pattern 2
            h.key (5.3, 48, false);
            h.key (5.3, 53, true);
            h.key (8.0, 53, false);
            h.key (8.0, 63, false);
            h.run (h.at (8.5), c.block);
            Expect e (h.spq());
            for (int n : { 48, 52, 55 }) e.note (0.0, 5.3, n);
            for (int n : { 53, 57, 60 }) e.note (5.3, 8.0, n);
            auto m = melodyOf (12, MelodyRhythm::Syncopated, 77, 2, 0.8, 0.0, 1);
            expectMelody (e, m, 0.0, 8.0, [] (double t) { return t < 5.3 ? 0 : 3; }, 60, 4);
            return finish (h, e, worst);
        });

        // Swing and gate; Dotted rhythm in two octaves.
        sweep ("Melody with Swing 60% and Gate 25% (Dotted, 6 notes, range 2)", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            auto& slot = h.settings.smart.patterns[2];
            slot.melody = true;
            slot.notes = 6;
            slot.rhythm = MelodyRhythm::Dotted;
            slot.range = 2;
            slot.gate = 0.25;
            h.settings.smart.melodySwing = 0.6;
            h.settings.smart.melodySeed = 5;
            h.key (0.0, 50, true);        // ii
            h.key (0.0, 66, true);        // pattern 3
            h.key (8.0, 50, false);
            h.key (8.0, 66, false);
            h.run (h.at (8.5), c.block);
            Expect e (h.spq());
            for (int n : { 50, 53, 57 }) e.note (0.0, 8.0, n);
            expectMelody (e, melodyOf (6, MelodyRhythm::Dotted, 5, 2, 0.25, 0.6, 2), 0.0, 8.0, [] (double) { return 1; }, 60, 3);
            return finish (h, e, worst);
        });

        // A one-bar loop: the same bar on every pass.
        sweep ("Melody across a one-bar loop (host doesn't split): the same notes every pass", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            h.settings.smart.patterns[0].melody = true;
            h.settings.smart.patterns[0].notes = 5;
            h.settings.smart.patterns[0].rhythm = MelodyRhythm::Clave;
            h.looping = true;
            h.loopStart = 0.0;
            h.loopEnd = 4.0;
            h.settings.smart.map.octave = 1;   // clear of the chord's notes
            h.key (0.0, 57, true);        // vi
            h.key (0.0, 61, true);
            h.run (h.at (12.0), c.block);
            h.stop();
            Expect e (h.spq());
            for (int n : { 57, 60, 64 }) e.note (0.0, 12.0, n);
            for (int pass = 0; pass < 3; ++pass)
            {
                Expect one (h.spq());
                expectMelody (one, melodyOf (5, MelodyRhythm::Clave), 0.0, 4.0, [] (double) { return 5; }, 72);
                for (auto hit : one.hits)
                {
                    hit.sample += h.at (4.0 * pass);
                    if (! hit.on && hit.sample >= h.at (12.0))
                        hit.sample = h.at (12.0);
                    e.hits.push_back (hit);
                }
            }
            return finish (h, e, worst, 2);
        });

        // Stopped: the melody starts its own bar at the key press.
        sweep ("Melody while stopped starts on the key press, as bar 1 (Straight, 4 notes)", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            h.playing = false;
            h.settings.smart.patterns[3].melody = true;
            h.settings.smart.patterns[3].notes = 4;
            const int64_t press = 1000;
            h.keyAt (press, 68, true);    // pattern 4 (G#)
            h.keyAt (press + h.at (6.0), 68, false);
            h.run (press + h.at (6.5), c.block);
            const double spq = h.spq();
            Expect e ([spq] (double beats) { return 1000.0 + beats * spq; });
            // Slot 4's factory settings: range 1, 9ths, gate 75%.
            expectMelody (e, melodyOf (4, MelodyRhythm::Straight, 1, 1, 0.75, 0.0, 3), 0.0, 6.0, [] (double) { return 0; }, 60, 5);
            return finish (h, e, worst);
        });

        // Stack after the pattern: every note with its octave.
        sweep ("Melody through SS Stack +1: every note with its octave", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            h.settings.smart.patterns[0].melody = true;
            h.settings.smart.stack = StackMode::PlusOne;
            h.key (0.0, 61, true);
            h.key (4.0, 61, false);
            h.run (h.at (4.5), c.block);
            Expect e (h.spq());
            expectMelody (e, melodyOf (8, MelodyRhythm::Straight), 0.0, 4.0, [] (double) { return 0; }, 60, 3, 1);
            return finish (h, e, worst);
        });

        // Test smart Pattern 1 with slot 1 in Melody mode (the LiveBridge case): the chord of
        // each beat is slot b mod 7, pressed just before the melody's note on that beat.
        sweep ("Test smart Pattern 1 in Melody mode, SS octave +1: every note from the seed, over each beat's chord", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            h.settings.testPattern = true;
            h.settings.testSmart = TestPattern::smartPattern;
            h.settings.smart.map.octave = 1;
            h.settings.smart.patterns[0].melody = true;
            h.settings.smart.melodySeed = 7;
            h.run (h.at (7.75), c.block);
            h.stop();
            Expect e (h.spq());
            for (int b = 0; b < 8; ++b)
            {
                const auto notes = chordNotes (makeGroupSlot (ChordGroup::Triads, Scale::Major, b % 7), {});
                for (auto n : notes)
                    e.note (b, b + 0.5, n);
            }
            expectMelody (e, melodyOf (8, MelodyRhythm::Straight, 7), 0.0, (double) h.at (7.75) / h.spq(),
                          [] (double t) { return (int) std::floor (t + 1.0e-9) % 7; }, 72);
            return finish (h, e, worst);
        });

        // Another seed plays another melody; the same seed the same, take after take.
        {
            auto take = [] (uint32_t seed)
            {
                Host h (44100.0, 120.0);
                h.settings.testPattern = true;
                h.settings.testSmart = TestPattern::smartPattern;
                h.settings.smart.patterns[0].melody = true;
                h.settings.smart.melodySeed = seed;
                h.run (h.at (16.0), 512);
                std::vector<std::pair<int64_t, int>> ons;
                for (const auto& hit : h.hits)
                    if (hit.on && hit.note >= 60)
                        ons.push_back ({ hit.sample, hit.note });
                return ons;
            };
            const auto a = take (3), b = take (3), other = take (4);
            check (a == b && ! a.empty() && a != other, "the same seed plays the same melody take after take; seed 4 plays another ("
                                                          + std::to_string (a.size()) + " notes)");
        }
    }
}

int main()
{
    testGenerator();
    testEngine();
    return testutil::finish ("test_melody");
}
