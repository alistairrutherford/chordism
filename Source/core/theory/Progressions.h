// Chord Progressions (PLAN.md §2, §4.2): which scale degree each white key of the chord zone
// plays. A progression is seven degrees, one per white key (C = slot 0 … B = slot 6). The
// presets are permutations, so every chord of the scale stays on some key; Custom takes its
// seven degrees from the "Custom 1–7" parameters and may repeat a degree.
//
// The order is the order of the host parameter's choices (saved by index): only append.
#pragma once

#include "ChordModel.h"
#include <array>

namespace chordism
{
    enum class Progression : uint8_t
    {
        Diatonic,         // 1 2 3 4 5 6 7
        Custom,           // the seven custom degrees
        Pop,              // 1 5 6 4 …
        Fifties,          // 1 6 4 5 …
        TwoFiveOne,       // 2 5 1 …
        CircleOfFifths,   // 1 4 7 3 6 2 5
        Descending,       // 1 7 6 5 4 3 2
        Canon,            // 1 5 6 3 4 …
        Thirds,           // 1 3 5 7 2 4 6
        Count
    };

    constexpr int numProgressions = (int) Progression::Count;

    using DegreeOrder = std::array<int8_t, numChordSlots>;   // slot → degree 0..6

    struct ProgressionInfo
    {
        const char* name;
        DegreeOrder degrees;
    };

    // Degrees are written 1-based in the names (the case of a Roman numeral depends on the scale).
    inline const ProgressionInfo& progressionInfo (Progression p) noexcept
    {
        static constexpr ProgressionInfo table[numProgressions] = {
            { "Diatonic (1-7)",           { 0, 1, 2, 3, 4, 5, 6 } },
            { "Custom",                   { 0, 1, 2, 3, 4, 5, 6 } },
            { "Pop (1-5-6-4)",            { 0, 4, 5, 3, 1, 2, 6 } },
            { "Fifties (1-6-4-5)",        { 0, 5, 3, 4, 1, 2, 6 } },
            { "Two-Five-One (2-5-1)",     { 1, 4, 0, 5, 3, 2, 6 } },
            { "Circle of Fifths",         { 0, 3, 6, 2, 5, 1, 4 } },
            { "Descending (1-7-6-5)",     { 0, 6, 5, 4, 3, 2, 1 } },
            { "Canon (1-5-6-3-4)",        { 0, 4, 5, 2, 3, 1, 6 } },
            { "Thirds (1-3-5-7)",         { 0, 2, 4, 6, 1, 3, 5 } },
        };
        const int i = (int) p;
        return table[i >= 0 && i < numProgressions ? i : 0];
    }

    constexpr DegreeOrder identityOrder() noexcept { return { 0, 1, 2, 3, 4, 5, 6 }; }

    // The degree each slot plays: the preset's, or `custom` for Custom (clamped to 0..6).
    inline DegreeOrder progressionDegrees (Progression p, const DegreeOrder& custom) noexcept
    {
        if (p != Progression::Custom)
            return progressionInfo (p).degrees;

        DegreeOrder d {};
        for (size_t i = 0; i < d.size(); ++i)
            d[i] = (int8_t) (custom[i] < 0 ? 0 : (custom[i] > 6 ? 6 : custom[i]));
        return d;
    }
}
