// Hold (PLAN.md §4.5): the chord keeps playing (with its strum, arp, repeat and steps) after its
// key is released.
//
//   Latch  until a new selection: the next chord key pressed ends every held chord
//   Bar    the same, and at the latest the first bar line after the key is released
//
// Either way a transport stop ends it. While the transport is stopped, pressing the key of a
// held chord again just ends it, and Bar works like Latch (there's no bar to end). Turning
// Hold off ends the held chords. The engine side is in ChordFx.h. Plain C++.
#pragma once

#include <cstdint>

namespace chordism
{
    // The order of the Hold parameter's choices: only ever append.
    enum class HoldMode : uint8_t { Off, Latch, Bar, Count };
    constexpr int numHoldModes = (int) HoldMode::Count;

    inline const char* holdModeName (HoldMode m) noexcept
    {
        static constexpr const char* names[numHoldModes] = { "Off", "Latch", "Bar" };
        const int i = (int) m;
        return names[i >= 0 && i < numHoldModes ? i : 0];
    }
}
