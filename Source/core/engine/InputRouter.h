// Splits incoming notes into the two zones (PLAN.md §4.1).
//
//   below the split:  white C D E F G A B → chord slots 0–6 (the octave doesn't matter)
//                     black C# D# F# G# A# → shifter slots 0–4
//   split and above:  white keys → scale tones 0, 1, 2… climbing through the octaves,
//                     counted from the C at or below the split (so the letter is kept)
//                     black keys → pattern slots 0–4 (repeating every octave)
//                     Chromatic mapping: every key passes through, no pattern keys
#pragma once

#include <cstdint>

namespace chordism
{
    enum class Zone : uint8_t { Chord, Scale };

    enum class KeyRole : uint8_t
    {
        ChordSlot,      // index 0..6
        Shifter,        // index 0..4
        ScaleTone,      // index 0, 1, 2… (7 per octave)
        PatternSlot,    // index 0..4
        PassThrough     // index = the note itself
    };

    struct RoutedKey
    {
        Zone zone = Zone::Chord;
        KeyRole role = KeyRole::PassThrough;
        int index = 0;

        bool operator== (const RoutedKey& o) const noexcept { return zone == o.zone && role == o.role && index == o.index; }
    };

    namespace keys
    {
        // Letter of a white key (C = 0 … B = 6), or -1.
        constexpr int whiteLetter (int pitchClass) noexcept
        {
            constexpr int t[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
            return t[((pitchClass % 12) + 12) % 12];
        }

        // Black key C# = 0, D# = 1, F# = 2, G# = 3, A# = 4, or -1.
        constexpr int blackIndex (int pitchClass) noexcept
        {
            constexpr int t[12] = { -1, 0, -1, 1, -1, -1, 2, -1, 3, -1, 4, -1 };
            return t[((pitchClass % 12) + 12) % 12];
        }

        constexpr int whiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
        constexpr int blackOffsets[5] = { 1, 3, 6, 8, 10 };
    }

    class InputRouter
    {
    public:
        static constexpr int defaultSplit = 60;    // Live's "C3"

        int split = defaultSplit;                  // first note of the Smart Scale zone
        bool chromatic = false;                    // Smart Scale mapping is Chromatic

        RoutedKey route (int note) const noexcept
        {
            const int pc = note % 12;

            if (note < split)
            {
                if (const int w = keys::whiteLetter (pc); w >= 0)
                    return { Zone::Chord, KeyRole::ChordSlot, w };
                return { Zone::Chord, KeyRole::Shifter, keys::blackIndex (pc) };
            }

            if (chromatic)
                return { Zone::Scale, KeyRole::PassThrough, note };

            if (const int w = keys::whiteLetter (pc); w >= 0)
            {
                const int cBelowSplit = split - split % 12;
                return { Zone::Scale, KeyRole::ScaleTone, (note / 12 - cBelowSplit / 12) * 7 + w };
            }
            return { Zone::Scale, KeyRole::PatternSlot, keys::blackIndex (pc) };
        }

        // The chord-zone key that plays `slot` in the octave just below the split, or -1 if the
        // chord zone has no complete octave.
        int chordKeyForSlot (int slot) const noexcept
        {
            if (split < 12 || slot < 0 || slot > 6)
                return -1;
            const int c = ((split - 12) / 12) * 12;
            return c + keys::whiteOffsets[slot];
        }

        // The chord-zone black key of shifter `slot` in the same octave, or -1.
        int shifterKeyForSlot (int slot) const noexcept
        {
            if (split < 12 || slot < 0 || slot > 4)
                return -1;
            const int c = ((split - 12) / 12) * 12;
            return c + keys::blackOffsets[slot];
        }
    };
}
