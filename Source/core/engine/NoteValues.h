// Note values for the rhythmic effects, and the grid they run on (PLAN.md §4.4, §4.5).
//
// The order of each list is the order of a host parameter's choices: only ever append.
// Lengths are in quarter notes (PPQ). Plain C++.
#pragma once

#include <cmath>

namespace chordism
{
    // Arp Rate and Repeat Rate (the same nine values as the Smart Scale patterns).
    constexpr int numPatternRates = 9;

    inline const char* patternRateName (int i) noexcept
    {
        static constexpr const char* names[numPatternRates] = { "1/2", "1/4", "3/16", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" };
        return names[i >= 0 && i < numPatternRates ? i : 0];
    }

    inline double patternRatePpq (int i) noexcept
    {
        static constexpr double ppq[numPatternRates] = { 2.0, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0 };
        return ppq[i >= 0 && i < numPatternRates ? i : 0];
    }

    // Strum Rate: the time between strummed tones while the host plays.
    constexpr int numStrumRates = 7;

    inline const char* strumRateName (int i) noexcept
    {
        static constexpr const char* names[numStrumRates] = { "1/128", "1/64T", "1/64", "1/32T", "1/32", "1/16T", "1/16" };
        return names[i >= 0 && i < numStrumRates ? i : 0];
    }

    inline double strumRatePpq (int i) noexcept
    {
        static constexpr double ppq[numStrumRates] = { 1.0 / 32.0, 1.0 / 24.0, 1.0 / 16.0, 1.0 / 12.0, 0.125, 1.0 / 6.0, 0.25 };
        return ppq[i >= 0 && i < numStrumRates ? i : 0];
    }

    namespace grid
    {
        constexpr double epsilon = 1.0e-9;

        // A step shorter than this fraction of the rate isn't started: a key pressed just
        // before a grid line sounds through it instead of giving a blip.
        constexpr double minFirstStep = 0.25;

        // Gates below 1% of a step are played at 1%, so every note has a length.
        constexpr double minGate = 0.01;

        inline double clampGate (double g) noexcept { return g < minGate ? minGate : (g > 1.0 ? 1.0 : g); }

        // The first grid line (multiples of `step` from ppq 0) at or after `ppq`.
        inline double atOrAfter (double ppq, double step) noexcept
        {
            return std::ceil (ppq / step - epsilon) * step;
        }

        // The first grid line after `ppq`.
        inline double after (double ppq, double step) noexcept
        {
            return std::floor (ppq / step + epsilon) * step + step;
        }

        // The grid line that follows a step started at `ppq` by a key press: the next one, unless
        // it's less than minFirstStep of a step away.
        inline double afterPress (double ppq, double step) noexcept
        {
            const double next = after (ppq, step);
            return next - ppq < minFirstStep * step - epsilon ? next + step : next;
        }
    }
}
