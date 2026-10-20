// Voice leading (PLAN.md §4.2): with Smooth, each new chord takes the inversion and octave
// whose notes are closest to the chord played before it, so the voices move as little as
// possible from chord to chord.
//
// The candidates for a chord are its inversions: inversion i keeps the chord's notes from the
// (i+1)-th lowest up and moves each lower one up by whole octaves until it sits above that note
// (so inversion i has the chord's i-th note, counted from 0, in the bass). Each candidate is then
// moved by whole octaves so its bass lies in the window [A - 6, A + 6), where A is the tonic of
// the Chord Octave (chordBaseNote + key + 12 · Chord Octave): the chord's register is anchored
// to the Chord Octave and can't drift, whatever the progression. The window holds every pitch
// class once, so a chord with n distinct pitch classes has n candidates.
//
// The distance between two voicings P and C counts every note's move to its nearest partner,
// both ways: Σ_c min_p |c − p| + Σ_p min_c |p − c| (semitones), so a chord with fewer notes
// can't leave a voice stranded. The candidate nearest the previous chord wins; ties go to the
// one nearest the chord as written, then to the lower bass. With no previous chord, the chord
// as written is the target, so the first chord is the candidate nearest it.
//
// Plain C++, no allocation.
#pragma once

#include "ChordModel.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

namespace chordism
{
    // The order of the Voice leading parameter's choices: only ever append.
    enum class VoiceLeadingMode : uint8_t { Off, Smooth, Count };
    constexpr int numVoiceLeadingModes = (int) VoiceLeadingMode::Count;

    inline const char* voiceLeadingName (VoiceLeadingMode m) noexcept
    {
        static constexpr const char* names[numVoiceLeadingModes] = { "Off", "Smooth" };
        const int i = (int) m;
        return names[i >= 0 && i < numVoiceLeadingModes ? i : 0];
    }

    namespace voicelead
    {
        // The tonic of the Chord Octave: the window's centre.
        inline int anchor (const ChordContext& ctx) noexcept
        {
            return chordBaseNote + ((ctx.key % 12) + 12) % 12 + 12 * ctx.outputOctave;
        }

        inline int nearest (int note, const uint8_t* notes, int count) noexcept
        {
            int best = 1 << 20;
            for (int i = 0; i < count; ++i)
                best = std::min (best, std::abs (note - (int) notes[i]));
            return best;
        }

        // Σ_a min_b |a − b| + Σ_b min_a |b − a|.
        inline int distance (const ChordNotes& a, const ChordNotes& b) noexcept
        {
            if (a.size == 0 || b.size == 0)
                return 0;
            int d = 0;
            for (int i = 0; i < a.size; ++i)
                d += nearest (a.notes[(size_t) i], b.notes.data(), b.size);
            for (int i = 0; i < b.size; ++i)
                d += nearest (b.notes[(size_t) i], a.notes.data(), a.size);
            return d;
        }

        inline ChordNotes sorted (const ChordNotes& c) noexcept
        {
            ChordNotes s = c;
            std::sort (s.notes.begin(), s.notes.begin() + s.size);
            return s;
        }

        // Inversion i of `s` (sorted, distinct), with its bass moved into [lo, lo + 12). False if
        // a note would leave MIDI 0..127.
        inline bool candidate (const ChordNotes& s, int i, int lo, ChordNotes& out) noexcept
        {
            std::array<bool, 256> used {};
            std::array<int, maxChordNotes> notes {};
            const int bass = s.notes[(size_t) i];
            int n = 0;
            for (int k = i; k < s.size; ++k)
            {
                notes[(size_t) n++] = s.notes[(size_t) k];
                used[(size_t) s.notes[(size_t) k]] = true;
            }
            for (int k = 0; k < i; ++k)
            {
                int p = s.notes[(size_t) k] + 12;
                while (p <= bass || (p < 256 && used[(size_t) p]))
                    p += 12;
                if (p >= 256)
                    return false;
                used[(size_t) p] = true;
                notes[(size_t) n++] = p;
            }

            // Whole octaves that put the bass in the window.
            const int diff = lo - bass;
            const int shift = 12 * (diff >= 0 ? (diff + 11) / 12 : -((-diff) / 12));
            out.size = 0;
            for (int k = 0; k < n; ++k)
            {
                const int p = notes[(size_t) k] + shift;
                if (p < 0 || p > 127)
                    return false;
                out.notes[(size_t) out.size++] = (uint8_t) p;
            }
            std::sort (out.notes.begin(), out.notes.begin() + out.size);
            return true;
        }
    }

    // The voicing of `chord` (as chordNotes gives it, in context `ctx`) nearest `previous`
    // (nullptr or empty: nearest the chord as written), ascending. A chord no candidate fits
    // (MIDI range) plays as written.
    inline ChordNotes leadVoicing (const ChordNotes& chord, const ChordNotes* previous, const ChordContext& ctx) noexcept
    {
        if (chord.size == 0)
            return chord;
        const ChordNotes written = voicelead::sorted (chord);
        const ChordNotes& target = previous != nullptr && previous->size > 0 ? *previous : written;
        const int lo = voicelead::anchor (ctx) - 6;

        ChordNotes best = written;
        bool found = false;
        int bestDistance = 0, bestWritten = 0;
        for (int i = 0; i < written.size; ++i)
        {
            ChordNotes c;
            if (! voicelead::candidate (written, i, lo, c))
                continue;
            const int d = voicelead::distance (c, target);
            const int w = voicelead::distance (c, written);
            const bool better = ! found || d < bestDistance || (d == bestDistance && w < bestWritten)
                             || (d == bestDistance && w == bestWritten && c.notes[0] < best.notes[0]);
            if (better)
            {
                best = c;
                bestDistance = d;
                bestWritten = w;
                found = true;
            }
        }
        return best;
    }
}
