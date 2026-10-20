// The Melody generator (PLAN.md §4.4): a pattern slot in Melody mode plays a seeded, generated
// melody over the current chord instead of an arp.
//
// A melody is made one bar at a time, as a pure function of (seed, pattern slot, bar number,
// settings), so the same seed always plays the same melody at the same place in the song,
// whatever the block size or how often it's played:
//
//   rhythm   the bar's sixteenths (counted from the bar line) are scored by the Rhythm
//            template's weights plus a little seeded jitter, and the `notes` best ones sound
//            (Notes per bar, 1-16, for a 4/4 bar; scaled for other bar lengths)
//   pitches  a seeded walk over scale indices from the tonic, within Range octaves: steps of
//            1 and 2 are favoured, with the odd repeat or leap, and it leans back toward the
//            middle of the range. Notes on a beat (and a bar's first note) are *strong*.
//   swing    pushes the even sixteenths (2, 4, …) later, by up to half a sixteenth (as the
//            Step Sequencer's Swing)
//   gate     each note lasts Gate × the time to the next note (or the bar line)
//
// The pitches are made real when a note plays (ScaleMapper::melodyTone): the walk's scale index
// in the current key and scale, from the tonic in the pattern key's octave; a strong note moves
// to the nearest chord tone of the current chord (Arp Chord sets how many stacked thirds count);
// a tone that is one of the chord's steps takes its Chord Editor accidental. So the melody
// follows chord changes at its next note, like an arp. Plain C++, no allocation.
#pragma once

#include "StepSequencer.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace chordism
{
    // The order of the Rhythm parameter's choices: only ever append.
    enum class MelodyRhythm : uint8_t { Straight, Syncopated, Dotted, Clave, Offbeat, Free, Count };
    constexpr int numMelodyRhythms = (int) MelodyRhythm::Count;

    inline const char* melodyRhythmName (MelodyRhythm r) noexcept
    {
        static constexpr const char* names[numMelodyRhythms] = { "Straight", "Syncopated", "Dotted", "Clave", "Offbeat", "Free" };
        const int i = (int) r;
        return names[i >= 0 && i < numMelodyRhythms ? i : 0];
    }

    // How likely each sixteenth of a 4/4 bar is to sound (0-9), per template (Chordism's own).
    inline const std::array<uint8_t, 16>& melodyRhythmWeights (MelodyRhythm r) noexcept
    {
        static constexpr std::array<uint8_t, 16> table[numMelodyRhythms] = {
            { 9, 1, 4, 1, 7, 1, 4, 1, 8, 1, 4, 1, 7, 1, 4, 1 },   // Straight: beats, then eighths
            { 8, 2, 3, 7, 2, 1, 7, 2, 3, 2, 7, 1, 2, 7, 3, 5 },   // Syncopated: anticipations
            { 9, 1, 1, 8, 1, 1, 8, 1, 1, 7, 1, 1, 7, 1, 1, 6 },   // Dotted: every dotted eighth
            { 9, 1, 2, 8, 1, 2, 8, 1, 2, 2, 8, 1, 8, 1, 2, 1 },   // Clave: 3-2 son (1, 4, 7, 11, 13)
            { 3, 1, 9, 1, 3, 1, 8, 1, 3, 1, 9, 1, 3, 1, 8, 2 },   // Offbeat: the "and"s
            { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 },   // Free: the seed decides
        };
        const int i = (int) r;
        return table[i >= 0 && i < numMelodyRhythms ? i : 0];
    }

    constexpr int minMelodyNotes = 1, maxMelodyNotes = 16;
    constexpr int minMelodySeed = 1, maxMelodySeed = 999;

    struct MelodySettings
    {
        bool on = false;                  // the slot plays a melody (else its arp)
        int notes = 8;                    // per 4/4 bar, 1..16
        MelodyRhythm rhythm = MelodyRhythm::Straight;
        double swing = 0.0;               // 0..1
        uint32_t seed = 1;
        int range = 1;                    // octaves, 1..3
        int slot = 0;                     // the pattern slot (part of the seed)
        double gate = 0.5;                // 0..1

        bool operator== (const MelodySettings& o) const noexcept
        {
            return on == o.on && notes == o.notes && rhythm == o.rhythm && swing == o.swing && seed == o.seed
                && range == o.range && slot == o.slot && gate == o.gate;
        }
        bool operator!= (const MelodySettings& o) const noexcept { return ! (*this == o); }
    };

    struct MelodyNote
    {
        int8_t pos = 0;                   // the sixteenth of the bar it starts on
        int8_t index = 0;                 // scale index from the tonic (0 = tonic)
        bool strong = false;              // on a beat, or the bar's first note: a chord tone
    };

    struct MelodyBar
    {
        static constexpr int maxSlots = 64;   // sixteenths in the longest bar (16/4)

        int slots = 16;                   // sixteenths in the bar
        int count = 0;
        std::array<MelodyNote, maxSlots> notes {};
    };

    namespace melody
    {
        // SplitMix64: small, fast, and the same everywhere.
        struct Random
        {
            uint64_t state;
            uint32_t next() noexcept
            {
                uint64_t z = (state += 0x9e3779b97f4a7c15ull);
                z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
                z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
                return (uint32_t) ((z ^ (z >> 31)) >> 32);
            }
            int below (int n) noexcept { return n > 0 ? (int) (next() % (uint32_t) n) : 0; }
        };

        inline Random randomFor (const MelodySettings& s, int64_t bar) noexcept
        {
            Random r { (uint64_t) s.seed * 0x100000001b3ull + (uint64_t) (s.slot + 1) * 0x9e3779b1ull };
            r.state ^= (uint64_t) bar * 0xc2b2ae3d27d4eb4full;
            r.next();
            return r;
        }

        // Sixteenths in a bar of `barLength` quarter notes (at least 1, at most 64).
        inline int slotsIn (double barLength) noexcept
        {
            const int n = (int) std::ceil (barLength / seqSlotPpq - 1.0e-9);
            return n < 1 ? 1 : (n > MelodyBar::maxSlots ? MelodyBar::maxSlots : n);
        }

        inline int clampInt (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

        // The melody of bar number `bar` (a bar of `slots` sixteenths).
        inline MelodyBar generate (const MelodySettings& s, int64_t bar, int slots) noexcept
        {
            MelodyBar m;
            m.slots = clampInt (slots, 1, MelodyBar::maxSlots);
            Random rng = randomFor (s, bar);

            // Rhythm: the best-scoring sixteenths sound.
            const int notes = clampInt (s.notes, minMelodyNotes, maxMelodyNotes);
            const int wanted = clampInt ((int) std::lround (notes * m.slots / 16.0), 1, m.slots);
            const auto& w = melodyRhythmWeights (s.rhythm);
            std::array<int, MelodyBar::maxSlots> score {};
            for (int j = 0; j < m.slots; ++j)
                score[(size_t) j] = 8 * w[(size_t) (j % 16)] + rng.below (12);
            std::array<bool, MelodyBar::maxSlots> chosen {};
            for (int n = 0; n < wanted; ++n)
            {
                int best = -1;
                for (int j = 0; j < m.slots; ++j)
                    if (! chosen[(size_t) j] && (best < 0 || score[(size_t) j] > score[(size_t) best]))
                        best = j;
                chosen[(size_t) best] = true;
            }

            // Pitches: a walk over scale indices 0 .. 7·range, leaning toward the middle.
            const int span = 7 * clampInt (s.range, 1, 3);
            const int mid = span / 2;
            int a = (3 * span) / 8 + rng.below (span / 4 + 1);
            for (int j = 0; j < m.slots; ++j)
            {
                if (! chosen[(size_t) j])
                    continue;
                if (m.count > 0)
                {
                    const int r = rng.below (100);
                    const int size = r < 50 ? 1 : (r < 75 ? 2 : (r < 87 ? 0 : 3 + rng.below (2)));
                    if (size > 0)
                    {
                        const int downChance = a > mid ? 65 : (a < mid ? 35 : 50);
                        a += rng.below (100) < downChance ? -size : size;
                        if (a < 0)
                            a = -a;
                        if (a > span)
                            a = 2 * span - a;
                        a = clampInt (a, 0, span);
                    }
                }
                auto& note = m.notes[(size_t) m.count];
                note.pos = (int8_t) j;
                note.index = (int8_t) a;
                note.strong = j % 4 == 0 || m.count == 0;
                ++m.count;
            }
            return m;
        }

        // When sixteenth `pos` of the bar starting at `barStart` starts, with swing.
        inline double slotStart (const MelodySettings& s, double barStart, double barLength, int pos) noexcept
        {
            const double swing = s.swing < 0.0 ? 0.0 : (s.swing > 1.0 ? 1.0 : s.swing);
            const double t = barStart + pos * seqSlotPpq + ((pos & 1) != 0 ? swing * maxSwingPpq : 0.0);
            return t < barStart + barLength ? t : barStart + barLength;
        }

        // Note i of bar `m`: its start and end (Gate × the time to the next note, or the bar line).
        inline double noteStart (const MelodySettings& s, const MelodyBar& m, double barStart, double barLength, int i) noexcept
        {
            return slotStart (s, barStart, barLength, m.notes[(size_t) i].pos);
        }

        inline double noteEnd (const MelodySettings& s, const MelodyBar& m, double barStart, double barLength, int i) noexcept
        {
            const double start = noteStart (s, m, barStart, barLength, i);
            const double next = i + 1 < m.count ? noteStart (s, m, barStart, barLength, i + 1) : barStart + barLength;
            return start + grid::clampGate (s.gate) * (next - start);
        }

        // The bar number of the bar starting at `barStart`, for the seed.
        inline int64_t barNumber (double barStart, double barLength) noexcept
        {
            return (int64_t) std::floor (barStart / (barLength > 0.0 ? barLength : 4.0) + 0.5);
        }
    }
}
