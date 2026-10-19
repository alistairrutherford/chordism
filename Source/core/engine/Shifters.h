// Shifters (PLAN.md §4.2): the chord zone's five black keys (C# D# F# G# A#). Each holds a
// target: a Chord Group, or a Key/Scale ("same" keeps the current key or scale). While a
// shifter is active the chord zone plays with its target instead of the Chord Group or Key
// and Scale parameters.
//
//   Standard: active while held. With several held, the last one pressed wins; letting it go
//             hands over to the one pressed before it.
//   Toggle:   a press turns the shifter on (and any other off); pressing it again turns it off.
//
// Plain C++, fixed size, real-time safe.
#pragma once

#include "../theory/ChordGroups.h"
#include <array>
#include <cstdint>

namespace chordism
{
    constexpr int numShifters = 5;

    enum class ShifterMode : uint8_t { Standard, Toggle };
    enum class ShifterKind : uint8_t { ChordGroup, KeyScale };

    struct ShifterTarget
    {
        ShifterKind kind = ShifterKind::ChordGroup;
        ChordGroup group = ChordGroup::Sevenths;
        int key = -1;                     // -1 = the current key, else 0..11
        int scale = -1;                   // -1 = the current scale, else a Scale

        bool operator== (const ShifterTarget& o) const noexcept
        {
            return kind == o.kind && group == o.group && key == o.key && scale == o.scale;
        }
        bool operator!= (const ShifterTarget& o) const noexcept { return ! (*this == o); }
    };

    using ShifterTargets = std::array<ShifterTarget, numShifters>;

    // Factory targets: three chord-group colours, and the parallel minor and major.
    inline ShifterTargets defaultShifterTargets() noexcept
    {
        return { { { ShifterKind::ChordGroup, ChordGroup::Sevenths, -1, -1 },
                   { ShifterKind::ChordGroup, ChordGroup::Sus4, -1, -1 },
                   { ShifterKind::ChordGroup, ChordGroup::Ninths, -1, -1 },
                   { ShifterKind::KeyScale, ChordGroup::Triads, -1, (int) Scale::NaturalMinor },
                   { ShifterKind::KeyScale, ChordGroup::Triads, -1, (int) Scale::Major } } };
    }

    class Shifters
    {
    public:
        // A shifter key went down. Returns true if the active shifter changed.
        bool press (int note, int slot, ShifterMode mode) noexcept
        {
            if (note < 0 || note > 127 || slot < 0 || slot >= numShifters)
                return false;
            const int before = active (mode);
            pressStamp[(size_t) note] = ++counter;
            slotOf[(size_t) note] = (int8_t) slot;
            if (mode == ShifterMode::Toggle)
                toggled = toggled == slot ? -1 : slot;
            return active (mode) != before;
        }

        // A shifter key went up. Returns true if the active shifter changed.
        bool release (int note, ShifterMode mode) noexcept
        {
            if (note < 0 || note > 127 || pressStamp[(size_t) note] == 0)
                return false;
            const int before = active (mode);
            pressStamp[(size_t) note] = 0;
            return active (mode) != before;
        }

        // Forget the keys held (all notes off); a Toggle shifter stays on.
        void forgetHeld() noexcept { pressStamp.fill (0); }

        // Mode change: nothing stays active, and keys still held are ignored until pressed again.
        void reset() noexcept
        {
            pressStamp.fill (0);
            toggled = -1;
        }

        // The active shifter slot, or -1.
        int active (ShifterMode mode) const noexcept
        {
            if (mode == ShifterMode::Toggle)
                return toggled;
            int best = -1;
            uint32_t latest = 0;
            for (size_t n = 0; n < pressStamp.size(); ++n)
                if (pressStamp[n] > latest)
                {
                    latest = pressStamp[n];
                    best = slotOf[n];
                }
            return best;
        }

    private:
        std::array<uint32_t, 128> pressStamp {};   // 0 = not held, else the press order
        std::array<int8_t, 128> slotOf {};
        uint32_t counter = 0;
        int toggled = -1;
    };
}
