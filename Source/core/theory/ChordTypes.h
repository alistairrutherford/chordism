// Chord types for the Active Chord menu (PLAN.md §4.2): choosing a type for a slot rewrites its
// steps (on, accidental) so the chord on that root has the type's intervals, as ChordPrism's
// "Active Chord" dropdown does. The root step keeps its accidental (the type is "for that
// root"). Each type lists, for the 3rd … 13th steps, the interval above the root it needs (in
// semitones, mod 12) or none; a step's accidental is whatever moves its scale tone there. A type
// whose tones would need more than a double accidental on that degree isn't offered.
//
// Steps a type turns on keep their octaves if they were on; otherwise they sound in their own
// octave, except the 9th of sus2, the 11th of sus4 and the 13th of a 6th chord, which sound an
// octave down (inside the triad), as the Chord Groups place them. Plain C++, no JUCE.
#pragma once

#include "ChordModel.h"
#include <array>

namespace chordism
{
    struct ChordType
    {
        const char* name;                 // menu text
        std::array<int8_t, 6> interval;   // steps 3, 5, 7, 9, 11, 13: semitones above the root (mod 12), -1 = off
        std::array<int8_t, 6> octave;     // the octave a step sounds in when the type turns it on
        const char* quality;              // the chord name's suffix (ChordNamer) on a root position
    };

    constexpr int numChordTypes = 27;

    inline const ChordType& chordType (int i) noexcept
    {
        constexpr int8_t _ = -1;
        static constexpr ChordType table[numChordTypes] = {
            { "Major",         { 4, 7, _, _, _, _ },    { 0, 0, 0, 0, 0, 0 },   "" },
            { "Minor",         { 3, 7, _, _, _, _ },    { 0, 0, 0, 0, 0, 0 },   "m" },
            { "Diminished",    { 3, 6, _, _, _, _ },    { 0, 0, 0, 0, 0, 0 },   "dim" },
            { "Augmented",     { 4, 8, _, _, _, _ },    { 0, 0, 0, 0, 0, 0 },   "aug" },
            { "Sus2",          { _, 7, _, 2, _, _ },    { 0, 0, 0, -1, 0, 0 },  "sus2" },
            { "Sus4",          { _, 7, _, _, 5, _ },    { 0, 0, 0, 0, -1, 0 },  "sus4" },
            { "Power (5)",     { _, 7, _, _, _, _ },    { 0, 0, 0, 0, 0, 0 },   "5" },
            { "6",             { 4, 7, _, _, _, 9 },    { 0, 0, 0, 0, 0, -1 },  "6" },
            { "m6",            { 3, 7, _, _, _, 9 },    { 0, 0, 0, 0, 0, -1 },  "m6" },
            { "7",             { 4, 7, 10, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "7" },
            { "maj7",          { 4, 7, 11, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "maj7" },
            { "m7",            { 3, 7, 10, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "m7" },
            { "m(maj7)",       { 3, 7, 11, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "m(maj7)" },
            { "m7b5",          { 3, 6, 10, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "m7b5" },
            { "dim7",          { 3, 6, 9, _, _, _ },    { 0, 0, 0, 0, 0, 0 },   "dim7" },
            { "7sus4",         { _, 7, 10, _, 5, _ },   { 0, 0, 0, 0, -1, 0 },  "7sus4" },
            { "7b5",           { 4, 6, 10, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "7b5" },
            { "7#5",           { 4, 8, 10, _, _, _ },   { 0, 0, 0, 0, 0, 0 },   "7#5" },
            { "add9",          { 4, 7, _, 2, _, _ },    { 0, 0, 0, 0, 0, 0 },   "add9" },
            { "m(add9)",       { 3, 7, _, 2, _, _ },    { 0, 0, 0, 0, 0, 0 },   "madd9" },
            { "9",             { 4, 7, 10, 2, _, _ },   { 0, 0, 0, 0, 0, 0 },   "9" },
            { "maj9",          { 4, 7, 11, 2, _, _ },   { 0, 0, 0, 0, 0, 0 },   "maj9" },
            { "m9",            { 3, 7, 10, 2, _, _ },   { 0, 0, 0, 0, 0, 0 },   "m9" },
            { "7b9",           { 4, 7, 10, 1, _, _ },   { 0, 0, 0, 0, 0, 0 },   "7b9" },
            { "7#9",           { 4, 7, 10, 3, _, _ },   { 0, 0, 0, 0, 0, 0 },   "7#9" },
            { "m11",           { 3, 7, 10, 2, 5, _ },   { 0, 0, 0, 0, 0, 0 },   "m11" },
            { "13",            { 4, 7, 10, 2, _, 9 },   { 0, 0, 0, 0, 0, 0 },   "13" },
        };
        return table[i >= 0 && i < numChordTypes ? i : 0];
    }

    // The steps `slot` gets with chord type `type`, in `scale`. False (and `out` unchanged) if
    // the type needs more than a double accidental on that degree.
    inline bool withChordType (const ChordSlot& slot, Scale scale, int type, ChordSlot& out) noexcept
    {
        const ChordType& t = chordType (type);
        ChordSlot s = slot;
        s.steps[0].on = true;
        const int root = scaleOffset (scale, slot.degree) + slot.steps[0].accidental;
        for (int k = 1; k < numChordSteps; ++k)
        {
            Step& st = s.steps[(size_t) k];
            const int want = t.interval[(size_t) (k - 1)];
            if (want < 0)
            {
                st.on = false;
                st.accidental = 0;
                continue;
            }
            const int natural = (((scaleOffset (scale, slot.degree + 2 * k) - root) % 12) + 12) % 12;
            const int acc = (((want - natural) % 12) + 18) % 12 - 6;   // -6..5
            if (acc < -2 || acc > 2)
                return false;
            if (! st.on)
                st.octaveMask = octaveBit (t.octave[(size_t) (k - 1)]);
            st.on = true;
            st.accidental = (int8_t) acc;
        }
        out = s;
        return true;
    }
}
