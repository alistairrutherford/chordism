// Keys and seven-tone scales (PLAN.md §2, §4.2). Plain C++, no JUCE.
//
// The order of `Scale` and `Key` is the order of the host parameters' choices, which Live
// saves by index: only ever append new entries.
#pragma once

#include <array>
#include <cstdint>

namespace chordism
{
    constexpr int numKeys = 12;

    // Spelled the way the Key menu shows them. Index 0 = C.
    inline const char* keyName (int key) noexcept
    {
        static constexpr const char* names[numKeys] = { "C", "C#/Db", "D", "Eb", "E", "F",
                                                        "F#/Gb", "G", "Ab", "A", "Bb", "B" };
        return names[((key % numKeys) + numKeys) % numKeys];
    }

    enum class Scale : uint8_t
    {
        Major,
        NaturalMinor,
        HarmonicMinor,
        MelodicMinor,       // ascending form
        Dorian,
        Phrygian,
        Lydian,
        Mixolydian,
        Locrian,
        HarmonicMajor,
        PhrygianDominant,
        HungarianMinor,
        DoubleHarmonic,
        Count
    };

    constexpr int numScales = (int) Scale::Count;

    using ScaleSteps = std::array<int8_t, 7>;   // semitones above the tonic, degrees 1..7

    struct ScaleInfo
    {
        const char* name;
        ScaleSteps semitones;
    };

    inline const ScaleInfo& scaleInfo (Scale s) noexcept
    {
        static constexpr ScaleInfo table[numScales] = {
            { "Major",             { 0, 2, 4, 5, 7, 9, 11 } },
            { "Natural Minor",     { 0, 2, 3, 5, 7, 8, 10 } },
            { "Harmonic Minor",    { 0, 2, 3, 5, 7, 8, 11 } },
            { "Melodic Minor",     { 0, 2, 3, 5, 7, 9, 11 } },
            { "Dorian",            { 0, 2, 3, 5, 7, 9, 10 } },
            { "Phrygian",          { 0, 1, 3, 5, 7, 8, 10 } },
            { "Lydian",            { 0, 2, 4, 6, 7, 9, 11 } },
            { "Mixolydian",        { 0, 2, 4, 5, 7, 9, 10 } },
            { "Locrian",           { 0, 1, 3, 5, 6, 8, 10 } },
            { "Harmonic Major",    { 0, 2, 4, 5, 7, 8, 11 } },
            { "Phrygian Dominant", { 0, 1, 4, 5, 7, 8, 10 } },
            { "Hungarian Minor",   { 0, 2, 3, 6, 7, 8, 11 } },
            { "Double Harmonic",   { 0, 1, 4, 5, 7, 8, 11 } },
        };
        const int i = (int) s;
        return table[i >= 0 && i < numScales ? i : 0];
    }

    // Semitones from the tonic to scale index `i` (0 = tonic, 7 = the tonic an octave up,
    // negative = below the tonic).
    inline int scaleOffset (Scale s, int i) noexcept
    {
        const int octave = i >= 0 ? i / 7 : -((6 - i) / 7);
        return scaleInfo (s).semitones[(size_t) (i - 7 * octave)] + 12 * octave;
    }

    // Default accidentals (§4.2): per scale, per degree, per chord step, applied when a Chord
    // Group fills the editor. Only natural minor needs one: its V chord takes the leading tone
    // as its 3rd, so V and V7 come out major and dominant without editing.
    inline int defaultAccidental (Scale s, int degree, int step) noexcept
    {
        if (s == Scale::NaturalMinor && degree == 4 && step == 1)
            return +1;
        return 0;
    }
}
