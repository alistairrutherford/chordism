// Arp (PLAN.md §4.4, §4.5): plays the chord's tones one per step, on the host grid.
//
// The tones are the chord after Octave and Stack, ascending, repeated an octave up for each
// extra octave of Range (1-3). Step k plays tone arpIndex(pattern, n, k) of those n tones.
// Chordism's own definitions of the twelve patterns, shown for n = 4 (tones 0 1 2 3):
//
//   Linear     Up 0 1 2 3            Down 3 2 1 0            once through, again and again
//   Converge   Up 0 3 1 2            Down 3 0 2 1            outside in
//   Diverge    Up 1 2 0 3            Down 2 1 3 0            inside out (Converge reversed)
//   Stutter    Up 0 0 1 1 2 2 3 3    Down 3 3 2 2 1 1 0 0    each tone twice
//   Zigzag     Up 0 2 1 3            Down 3 1 2 0            up two, back one (n ≥ 3)
//   Boomerang  Up 0 1 2 3 2 1        Down 3 2 1 0 1 2        there and back
//
// Gate (0-100%) is the part of each step a tone sounds. A chord change takes effect on the
// next step, without restarting the pattern. Plain C++.
#pragma once

#include "Stack.h"
#include <cstdint>

namespace chordism
{
    // The order of the Arp parameter's choices: only ever append.
    enum class ArpPattern : uint8_t
    {
        Off,
        LinearUp, LinearDown,
        ConvergeUp, ConvergeDown,
        DivergeUp, DivergeDown,
        StutterUp, StutterDown,
        ZigzagUp, ZigzagDown,
        BoomerangUp, BoomerangDown,
        Count
    };
    constexpr int numArpPatterns = (int) ArpPattern::Count;   // Off + 12

    inline const char* arpPatternName (ArpPattern p) noexcept
    {
        static constexpr const char* names[numArpPatterns] = {
            "Off", "Linear Up", "Linear Down", "Converge Up", "Converge Down", "Diverge Up", "Diverge Down",
            "Stutter Up", "Stutter Down", "Zigzag Up", "Zigzag Down", "Boomerang Up", "Boomerang Down"
        };
        const int i = (int) p;
        return names[i >= 0 && i < numArpPatterns ? i : 0];
    }

    constexpr int minArpRange = 1, maxArpRange = 3;

    namespace arp
    {
        inline int64_t wrap (int64_t k, int64_t len) noexcept { return ((k % len) + len) % len; }

        inline int convergeUp (int n, int64_t k) noexcept
        {
            const auto c = (int) wrap (k, n);
            return (c & 1) == 0 ? c / 2 : n - 1 - c / 2;
        }

        inline int divergeUp (int n, int64_t k) noexcept
        {
            return n - 1 - convergeUp (n, n - 1 - wrap (k, n));
        }

        inline int up (ArpPattern p, int n, int64_t k) noexcept
        {
            switch (p)
            {
                case ArpPattern::ConvergeUp: case ArpPattern::ConvergeDown: return convergeUp (n, k);
                case ArpPattern::DivergeUp: case ArpPattern::DivergeDown:   return divergeUp (n, k);
                case ArpPattern::StutterUp: case ArpPattern::StutterDown:   return (int) (wrap (k, 2 * n) / 2);
                case ArpPattern::ZigzagUp: case ArpPattern::ZigzagDown:
                {
                    if (n < 3)
                        return (int) wrap (k, n);
                    const auto c = (int) wrap (k, 2 * (n - 2));
                    return c / 2 + ((c & 1) == 0 ? 0 : 2);
                }
                case ArpPattern::BoomerangUp: case ArpPattern::BoomerangDown:
                {
                    if (n < 2)
                        return 0;
                    const auto c = (int) wrap (k, 2 * (n - 1));
                    return c < n ? c : 2 * (n - 1) - c;
                }
                case ArpPattern::LinearUp: case ArpPattern::LinearDown:
                case ArpPattern::Off: case ArpPattern::Count:
                default:
                    return (int) wrap (k, n);
            }
        }

        inline bool isDown (ArpPattern p) noexcept
        {
            return p == ArpPattern::LinearDown || p == ArpPattern::ConvergeDown || p == ArpPattern::DivergeDown
                || p == ArpPattern::StutterDown || p == ArpPattern::ZigzagDown || p == ArpPattern::BoomerangDown;
        }
    }

    // The tone (0..n-1, into the ascending tones) that step k plays. -1 if n is 0.
    inline int arpIndex (ArpPattern p, int n, int64_t k) noexcept
    {
        if (n <= 0)
            return -1;
        const int i = arp::up (p, n, k);
        return arp::isDown (p) ? n - 1 - i : i;
    }

    // The tones an arp runs over: `chord` repeated an octave up for each extra octave of range.
    inline VoicedNotes arpTones (const VoicedNotes& chord, int range) noexcept
    {
        range = range < minArpRange ? minArpRange : (range > maxArpRange ? maxArpRange : range);
        std::array<bool, 128> on {};
        for (int r = 0; r < range; ++r)
            for (const auto n : chord)
                if (n + 12 * r < 128)
                    on[(size_t) (n + 12 * r)] = true;

        VoicedNotes v;
        for (int n = 0; n < 128 && v.size < VoicedNotes::capacity; ++n)
            if (on[(size_t) n])
                v.notes[(size_t) v.size++] = (uint8_t) n;
        return v;
    }
}
