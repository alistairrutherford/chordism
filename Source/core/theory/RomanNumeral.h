// Roman numerals in ChordPrism's style (PLAN.md §4.2): upper case for a major chord, lower
// case for minor, "*" after a diminished chord and "+" after an augmented one ("I", "ii",
// "vii*", "III+"). A root moved by the editor's accidental is marked "b" or "#" ("bII").
// Chords without a 3rd (power, sus) are written in upper case.
//
// The numeral is the slot's own: its degree, and the quality of its own 3rd and 5th steps, so
// an inversion keeps its numeral. Plain C++, no JUCE; std::string, so not for the audio thread.
#pragma once

#include "ChordModel.h"
#include <string>

namespace chordism
{
    enum class TriadQuality : uint8_t { Major, Minor, Diminished, Augmented, NoThird };

    // The quality of a slot's triad from its 3rd and 5th steps (a step that is off counts as
    // missing; with no 5th, a 3rd alone decides).
    inline TriadQuality slotTriadQuality (const ChordSlot& slot, Scale scale) noexcept
    {
        const int root = scaleOffset (scale, slot.degree) + slot.steps[0].accidental;
        auto interval = [&] (int k) {
            const auto& st = slot.steps[(size_t) k];
            return st.on ? (((scaleOffset (scale, slot.degree + 2 * k) + st.accidental - root) % 12) + 12) % 12 : -1;
        };
        const int third = interval (1), fifth = interval (2);
        if (third == 4)
            return fifth == 8 ? TriadQuality::Augmented : TriadQuality::Major;
        if (third == 3)
            return fifth == 6 ? TriadQuality::Diminished : TriadQuality::Minor;
        return TriadQuality::NoThird;
    }

    inline std::string romanNumeral (const ChordSlot& slot, Scale scale)
    {
        static constexpr const char* upper[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
        static constexpr const char* lower[7] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };
        const int d = ((slot.degree % 7) + 7) % 7;
        const int acc = slot.steps[0].accidental;

        std::string s ((size_t) (acc < 0 ? -acc : acc), acc < 0 ? 'b' : '#');
        switch (slotTriadQuality (slot, scale))
        {
            case TriadQuality::Major:      s += upper[d]; break;
            case TriadQuality::Augmented:  s += upper[d]; s += '+'; break;
            case TriadQuality::Minor:      s += lower[d]; break;
            case TriadQuality::Diminished: s += lower[d]; s += '*'; break;
            case TriadQuality::NoThird:    s += upper[d]; break;
        }
        return s;
    }
}
