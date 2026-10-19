// Factory Chord Groups (PLAN.md §4.2): families of chord shapes that fill in every slot's
// steps at once. Choosing a group resets the Chord Editor's edits, as ChordPrism does.
//
// The order is the order of the host parameter's choices (saved by index): only append.
#pragma once

#include "ChordModel.h"
#include <array>

namespace chordism
{
    enum class ChordGroup : uint8_t
    {
        Triads,
        Power,
        Sus2,
        Sus4,
        Sixths,
        Sevenths,
        Add9,
        Ninths,
        Elevenths,
        Thirteenths,
        FirstInversion,
        SecondInversion,
        OpenTriads,
        Drop2Sevenths,
        Shell,
        NeoSoul,
        Quartal,
        Count
    };

    constexpr int numChordGroups = (int) ChordGroup::Count;

    // One step of a group shape: which chord step (0 = root, 1 = 3rd, … 6 = 13th) and the
    // octaves it sounds in.
    struct GroupTone
    {
        int8_t step;
        uint8_t octaveMask;
    };

    struct ChordGroupInfo
    {
        const char* name;
        std::array<GroupTone, numChordSteps> tones;
        int numTones;
    };

    inline const ChordGroupInfo& chordGroupInfo (ChordGroup g) noexcept
    {
        constexpr uint8_t o0 = octaveBit (0), up = octaveBit (1), down = octaveBit (-1);
        constexpr int8_t R = 0, T3 = 1, T5 = 2, T7 = 3, T9 = 4, T11 = 5, T13 = 6;

        static constexpr ChordGroupInfo table[numChordGroups] = {
            { "Triads",          { { { R, o0 }, { T3, o0 }, { T5, o0 } } }, 3 },
            { "Power (1-5)",     { { { R, (uint8_t) (o0 | up) }, { T5, o0 } } }, 2 },
            { "Sus2",            { { { R, o0 }, { T9, down }, { T5, o0 } } }, 3 },
            { "Sus4",            { { { R, o0 }, { T11, down }, { T5, o0 } } }, 3 },
            { "6ths",            { { { R, o0 }, { T3, o0 }, { T5, o0 }, { T13, down } } }, 4 },
            { "Sevenths",        { { { R, o0 }, { T3, o0 }, { T5, o0 }, { T7, o0 } } }, 4 },
            { "Add9",            { { { R, o0 }, { T3, o0 }, { T5, o0 }, { T9, o0 } } }, 4 },
            { "Ninths",          { { { R, o0 }, { T3, o0 }, { T5, o0 }, { T7, o0 }, { T9, o0 } } }, 5 },
            { "Elevenths",       { { { R, o0 }, { T3, o0 }, { T5, o0 }, { T7, o0 }, { T9, o0 }, { T11, o0 } } }, 6 },
            // The 11th clashes with the 3rd in a 13th chord, so it's left out, as is usual.
            { "Thirteenths",     { { { R, o0 }, { T3, o0 }, { T5, o0 }, { T7, o0 }, { T9, o0 }, { T13, o0 } } }, 6 },
            { "1st Inversion",   { { { R, up }, { T3, o0 }, { T5, o0 } } }, 3 },
            { "2nd Inversion",   { { { R, up }, { T3, up }, { T5, o0 } } }, 3 },
            { "Open Triads",     { { { R, o0 }, { T5, o0 }, { T3, up } } }, 3 },
            { "Drop-2 Sevenths", { { { R, o0 }, { T3, o0 }, { T5, down }, { T7, o0 } } }, 4 },
            { "Shell (1-3-7)",   { { { R, o0 }, { T3, o0 }, { T7, o0 } } }, 3 },
            { "Neo-Soul",        { { { R, o0 }, { T3, o0 }, { T7, o0 }, { T9, o0 } } }, 4 },
            // Stacked diatonic 4ths: root, 4th, 7th, 10th.
            { "Quartal",         { { { R, o0 }, { T11, down }, { T7, o0 }, { T3, up } } }, 4 },
        };
        const int i = (int) g;
        return table[i >= 0 && i < numChordGroups ? i : 0];
    }

    // The slot a group gives a degree in a scale: the group's steps and octaves, with the
    // scale's default accidentals.
    inline ChordSlot makeGroupSlot (ChordGroup g, Scale s, int degree) noexcept
    {
        ChordSlot slot;
        slot.degree = degree;
        const auto& info = chordGroupInfo (g);
        for (int i = 0; i < info.numTones; ++i)
        {
            const auto& tone = info.tones[(size_t) i];
            auto& step = slot.steps[(size_t) tone.step];
            step.on = true;
            step.octaveMask = tone.octaveMask;
            step.accidental = (int8_t) defaultAccidental (s, degree, tone.step);
        }
        return slot;
    }

    // The seven slots of the chord zone. In Phase 1 the progression is the identity:
    // white key n (C = 0) plays degree n.
    using ChordSet = std::array<ChordSlot, numChordSlots>;

    inline ChordSet makeGroupChords (ChordGroup g, Scale s) noexcept
    {
        ChordSet set;
        for (int d = 0; d < numChordSlots; ++d)
            set[(size_t) d] = makeGroupSlot (g, s, d);
        return set;
    }
}
