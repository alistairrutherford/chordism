// Octave and Stack, the first two stages of the chord-zone effect chain (PLAN.md §4.5).
//
// Octave is the Chord Octave parameter (Phase 1): it moves the chord's ChordContext, so the
// voicing already sounds in the chosen octave when it gets here. Stack then adds copies of
// every tone one octave up, two octaves up, or both. The result is the chord's tones in
// ascending pitch, each pitch once, within MIDI 0..127: the order Strum and Arp work in.
//
// Plain C++, fixed size.
#pragma once

#include "../theory/ChordModel.h"
#include <array>
#include <cstdint>

namespace chordism
{
    // The order of the Stack parameter's choices: only ever append.
    enum class StackMode : uint8_t { Off, PlusOne, PlusTwo, PlusOneAndTwo, Count };
    constexpr int numStackModes = (int) StackMode::Count;

    inline const char* stackModeName (StackMode m) noexcept
    {
        static constexpr const char* names[numStackModes] = { "Off", "+1", "+2", "+1&2" };
        const int i = (int) m;
        return names[i >= 0 && i < numStackModes ? i : 0];
    }

    // Up to 64 distinct pitches, ascending.
    struct VoicedNotes
    {
        static constexpr int capacity = 64;
        std::array<uint8_t, capacity> notes {};
        int size = 0;

        const uint8_t* begin() const noexcept { return notes.data(); }
        const uint8_t* end() const noexcept { return notes.data() + size; }
        uint8_t operator[] (int i) const noexcept { return notes[(size_t) i]; }

        bool operator== (const VoicedNotes& o) const noexcept
        {
            if (size != o.size)
                return false;
            for (int i = 0; i < size; ++i)
                if (notes[(size_t) i] != o.notes[(size_t) i])
                    return false;
            return true;
        }
        bool operator!= (const VoicedNotes& o) const noexcept { return ! (*this == o); }
    };

    // The chord's notes with `stack` applied, ascending and without duplicates.
    inline VoicedNotes stackNotes (const ChordNotes& chord, StackMode stack) noexcept
    {
        std::array<bool, 128> on {};
        auto add = [&on] (int n) noexcept { if (n >= 0 && n < 128) on[(size_t) n] = true; };

        const bool one = stack == StackMode::PlusOne || stack == StackMode::PlusOneAndTwo;
        const bool two = stack == StackMode::PlusTwo || stack == StackMode::PlusOneAndTwo;
        for (const auto n : chord)
        {
            add (n);
            if (one) add (n + 12);
            if (two) add (n + 24);
        }

        VoicedNotes v;
        for (int n = 0; n < 128 && v.size < VoicedNotes::capacity; ++n)
            if (on[(size_t) n])
                v.notes[(size_t) v.size++] = (uint8_t) n;
        return v;
    }
}
