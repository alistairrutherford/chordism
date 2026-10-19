// Repeat (PLAN.md §4.5): restrikes the chord on every step of Repeat Rate while its key is held
// (or held by Hold). Repeat Gate (0-100%) is the part of each step the chord sounds.
//
// The first strike is the key press itself; the next ones fall on the host grid (multiples of
// the rate from ppq 0), so repeats stay in time with the song wherever the key went down. A
// press less than a quarter step before a grid line sounds through it (NoteValues.h).
// With Strum, every strike is strummed. Plain C++.
#pragma once

#include "NoteValues.h"

namespace chordism
{
    struct RepeatSettings
    {
        bool on = false;
        int rate = 3;                     // index into the pattern rates (1/8)
        double gate = 0.5;                // 0..1

        double step() const noexcept { return patternRatePpq (rate); }
    };

    struct RepeatStrike
    {
        double end = 0.0;                 // ppq the strike stops sounding
        double next = 0.0;                // ppq of the next strike
    };

    // The strike starting at `ppq`: by a key press, or at a grid line reached by repeating.
    inline RepeatStrike repeatStrike (const RepeatSettings& r, double ppq, bool press) noexcept
    {
        const double step = r.step();
        const double next = press ? grid::afterPress (ppq, step) : grid::after (ppq, step);
        return { ppq + grid::clampGate (r.gate) * (next - ppq), next };
    }
}
