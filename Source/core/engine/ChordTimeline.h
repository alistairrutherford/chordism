// Look-Ahead Scale Transposing (PLAN.md §4.4): while the transport loops, the engine records
// when the chord the Smart Scale follows changes, as a map of the loop. On later passes a Smart
// Scale key pressed within the look-ahead window before a learned change plays the upcoming
// chord's tone, so a bass note played a little early (or a chord from a synced instance that
// arrives a block late, SharedSession.h) sounds right.
//
//   recording  every change while the transport plays inside the loop is kept with its place
//              in the loop (a remote chord at its stamped time). A pass that started at the loop
//              start (or entered it from before) becomes the learned map when the loop wraps. A
//              change within a hair of the loop end (a chord on the loop start whose sample
//              rounded down) belongs to the next pass, at the loop start.
//   look-ahead upcoming(T): the first learned change at or after T (in loop time, across the
//              wrap); if it is within the window and its chord isn't the current one, that
//              chord.
//   checking   a learned change must happen again within the tolerance of its place (the
//              window, at least 1/64, plus a block, since a synced chord can arrive a block
//              late): each change is checked once the playhead is past its place + tolerance,
//              and if it didn't happen the learned map is cleared. A locate skips the checks it
//              lands in the middle of.
//   clearing   when the loop points change or looping stops, everything is forgotten.
//
// Plain C++, fixed size.
#pragma once

#include "ScaleMapper.h"
#include <array>
#include <cmath>

namespace chordism
{
    // The Look-ahead parameter's choices: the window, in quarter notes. Only ever append.
    constexpr int numLookAheads = 6;

    inline const char* lookAheadName (int i) noexcept
    {
        static constexpr const char* names[numLookAheads] = { "Off", "1/128", "1/64", "1/32", "1/16", "1/8" };
        return names[i >= 0 && i < numLookAheads ? i : 0];
    }

    inline double lookAheadPpq (int i) noexcept
    {
        static constexpr double ppq[numLookAheads] = { 0.0, 1.0 / 32.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0 };
        return ppq[i >= 0 && i < numLookAheads ? i : 0];
    }

    constexpr int defaultLookAhead = 3;   // 1/32

    class ChordTimeline
    {
    public:
        static constexpr int capacity = 64;   // changes per pass

        struct Change
        {
            double ppq = 0.0;             // in [loopStart, loopEnd)
            SmartChord chord;
            bool seen = false;            // happened again this pass (checking)
        };

        void clear() noexcept
        {
            learnedCount = 0;
            passCount = 0;
            passComplete = false;
            overflow = false;
            haveLoop = false;
            haveCarry = false;
        }

        // Every block: the transport's loop, the window (0 = look-ahead off: nothing kept) and the
        // block's length in quarter notes.
        void setLoop (bool playing, bool looping, double start, double end, double window, double blockPpq) noexcept
        {
            const bool loop = window > 0.0 && looping && std::isfinite (start) && std::isfinite (end) && end - start > minLoop;
            if (! loop)
            {
                if (haveLoop || learnedCount > 0)
                    clear();
                return;
            }
            if (haveLoop && (std::abs (start - loopStart) > epsilon || std::abs (end - loopEnd) > epsilon))
                clear();
            if (! haveLoop)
            {
                haveLoop = true;
                passComplete = false;
                passCount = 0;
            }
            loopStart = start;
            loopEnd = end;
            tolerance = (window > minTolerance ? window : minTolerance) + (blockPpq > 0.0 ? blockPpq : 0.0);
            tolerance = tolerance < (end - start) * 0.25 ? tolerance : (end - start) * 0.25;
            if (! playing)
                passComplete = false;     // a stopped pass isn't a pass; playback starts a new one
        }

        // Playback started or jumped to `ppq` (not a wrap): a new pass starts there.
        void locate (double ppq) noexcept
        {
            if (! haveLoop)
                return;
            passCount = 0;
            overflow = false;
            haveCarry = false;
            passComplete = std::abs (ppq - loopStart) <= startSlack;
            const double at = phase (ppq);
            const bool inLoop = ppq >= loopStart - startSlack && ppq < loopEnd;
            for (int i = 0; i < learnedCount; ++i)
                learned[(size_t) i].seen = ! inLoop || near (learned[(size_t) i].ppq, at);
        }

        // The playhead wrapped from the loop end to the loop start.
        void wrap() noexcept
        {
            if (! haveLoop)
                return;
            if (passComplete && ! overflow)
            {
                learned = pass;
                learnedCount = passCount;
                for (int i = 0; i < learnedCount; ++i)
                    learned[(size_t) i].seen = near (learned[(size_t) i].ppq, loopStart);
            }
            passCount = 0;
            passComplete = true;
            overflow = false;
            if (haveCarry)
                pass[(size_t) passCount++] = carry;
            haveCarry = false;
        }

        // The playhead played [from, to) without a wrap: checks the learned changes due there,
        // and starts a pass when it enters the loop from before it.
        void advance (double from, double to) noexcept
        {
            if (! haveLoop || ! (to > from))
                return;
            if (from < loopStart - startSlack && to >= loopStart)
            {
                passCount = 0;
                passComplete = true;
                overflow = false;
            }
            const double a = from > loopStart + startSlack ? from : loopStart;   // a wrap lands just after the start
            const double b = to < loopEnd ? to : loopEnd;
            for (int i = 0; i < learnedCount; ++i)
            {
                Change& c = learned[(size_t) i];
                const double due = phase (c.ppq + tolerance);
                if (due < a || due >= b)
                    continue;
                if (! c.seen)
                {
                    learnedCount = 0;      // a learned change didn't happen: forget the map
                    return;
                }
                c.seen = false;
            }
        }

        // The chord changed to `chord` at `ppq` (while playing; `now` is the playhead, later than
        // `ppq` for a synced chord that arrived late).
        void record (double ppq, const SmartChord& chord, double now) noexcept
        {
            if (! haveLoop || ppq < loopStart - startSlack || ppq >= loopEnd + startSlack)
                return;
            const bool atEnd = phase (ppq) >= loopEnd - startSlack;
            const double at = atEnd ? loopStart : phase (ppq);
            for (int i = 0; i < learnedCount; ++i)
                if (learned[(size_t) i].chord == chord && near (learned[(size_t) i].ppq, at))
                    learned[(size_t) i].seen = true;
            if (atEnd && phase (now) > loopStart + 0.5 * (loopEnd - loopStart))
            {
                carry = { at, chord, false };      // the wrap hasn't come yet: the next pass's
                haveCarry = true;
                return;
            }
            if (passCount >= capacity)
            {
                overflow = true;
                return;
            }
            pass[(size_t) passCount++] = { at, chord, false };
        }

        // The chord a Smart Scale key pressed at `ppq` takes, if a learned change comes within
        // `window` and its chord isn't `current`. `slack` (a sample, in quarter notes) absorbs
        // the rounding of times to samples: a change up to that much before `ppq` is still ahead.
        bool upcoming (double ppq, double window, double slack, const SmartChord& current, SmartChord& out) const noexcept
        {
            if (! haveLoop || learnedCount == 0 || ppq < loopStart - startSlack || ppq >= loopEnd)
                return false;
            const double at = phase (ppq);
            const double length = loopEnd - loopStart;
            int best = -1;
            double bestAhead = 0.0;
            for (int i = 0; i < learnedCount; ++i)
            {
                double ahead = learned[(size_t) i].ppq - at;
                ahead -= length * std::floor (ahead / length);
                if (ahead > length - slack - epsilon)
                    ahead = 0.0;
                if (best < 0 || ahead < bestAhead)
                {
                    best = i;
                    bestAhead = ahead;
                }
            }
            if (best < 0 || bestAhead > window + slack + epsilon || learned[(size_t) best].chord == current)
                return false;
            out = learned[(size_t) best].chord;
            return true;
        }

        int learnedChanges() const noexcept { return learnedCount; }
        const Change& learnedChange (int i) const noexcept { return learned[(size_t) (i >= 0 && i < capacity ? i : 0)]; }
        bool hasLoop() const noexcept { return haveLoop; }

    private:
        static constexpr double epsilon = 1.0e-6;
        static constexpr double minLoop = 1.0e-3;
        static constexpr double minTolerance = 1.0 / 16.0;   // 1/64 note
        static constexpr double startSlack = 1.0e-3;         // ppq: "at the loop start"

        // A time's place in the loop, [loopStart, loopEnd).
        double phase (double ppq) const noexcept
        {
            const double length = loopEnd - loopStart;
            double x = ppq - loopStart;
            x -= length * std::floor (x / length);
            if (x >= length - epsilon)
                x = 0.0;
            return loopStart + x;
        }

        // Two places in the loop within the tolerance of each other (across the wrap).
        bool near (double a, double b) const noexcept
        {
            const double length = loopEnd - loopStart;
            double d = std::abs (a - b);
            d = d > length * 0.5 ? length - d : d;
            return d <= tolerance + epsilon;
        }

        std::array<Change, capacity> learned {}, pass {};
        Change carry;
        int learnedCount = 0, passCount = 0;
        bool passComplete = false, overflow = false, haveLoop = false, haveCarry = false;
        double loopStart = 0.0, loopEnd = 0.0, tolerance = minTolerance;
    };
}
