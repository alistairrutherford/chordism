// Step Sequencer (PLAN.md §4.5): a 16-step gate on the chord zone.
//
// Each bar is divided into sixteenth-note slots, counted from the bar line; slot j uses step
// j mod 16 of the active pattern (one of four pattern memories). While a chord is held, it
// sounds in the windows of the active steps and is silent in between; each window restrikes
// it (strummed, with Strum). A key pressed inside an open window sounds at once, to the end of
// that window (unless less than a quarter of the step is left); a key pressed in a closed one
// waits for the next active step.
//
//   Swing    pushes the even steps (2, 4, … 16) later, by up to half a sixteenth at 100%
//            (about 33% gives a triplet feel)
//   Gate     the part of the step the chord sounds, 0-100% (full = the whole sixteenth)
//   Sustain  a step sounds through the inactive steps after it, up to the sixteenth before the
//            next active step, and Gate applies to that last sixteenth
//
// With Repeat, the repeats sound only inside the windows; with Arp, the arp's tones sound only
// inside the windows (cut at a window's end). Plain C++.
#pragma once

#include "NoteValues.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace chordism
{
    constexpr int numSeqSteps = 16;
    constexpr int numSeqPatterns = 4;
    constexpr double seqSlotPpq = 0.25;           // a sixteenth note
    constexpr double maxSwingPpq = 0.5 * seqSlotPpq;

    using SeqPatterns = std::array<uint16_t, numSeqPatterns>;   // bit s = step s + 1 is on

    // Factory patterns for the four memories (Chordism's own).
    inline SeqPatterns defaultSeqPatterns() noexcept
    {
        return { {
            0x1111,   // 1: four on the floor (steps 1 5 9 13)
            0x4444,   // 2: offbeat stabs (3 7 11 15)
            0x1449,   // 3: tresillo-style 3+3+4 (1 4 7 11 13)
            0x4949,   // 4: syncopated dembow-style (1 4 7 9 12 15)
        } };
    }

    struct StepSeqSettings
    {
        bool on = false;
        int pattern = 0;                  // 0..3
        SeqPatterns patterns = defaultSeqPatterns();
        double swing = 0.0;               // 0..1
        double gate = 0.5;                // 0..1
        bool sustain = false;

        uint16_t steps() const noexcept { return patterns[(size_t) (pattern >= 0 && pattern < numSeqPatterns ? pattern : 0)]; }
        bool active (int slot) const noexcept { return ((steps() >> (slot % numSeqSteps)) & 1) != 0; }
    };

    struct SeqWindow
    {
        bool valid = false;
        double start = 0.0, end = 0.0;    // ppq
        double slotLength = seqSlotPpq;   // length of the window's own slot
    };

    // The sequencer's slots and windows for the bars a clock reports (bar lines at
    // origin + k · length).
    class StepGrid
    {
    public:
        StepGrid (const StepSeqSettings& s, double barOrigin, double barLength) noexcept
            : seq (s), origin (barOrigin), barLen (barLength > 0.0 ? barLength : 4.0)
        {
            const int n = (int) std::ceil (barLen / seqSlotPpq - grid::epsilon);
            slots = n < 1 ? 1 : (n > maxSlots ? maxSlots : n);
        }

        struct Slot
        {
            double bar = 0.0;             // ppq of the bar line
            int index = 0;                // 0..slots-1
        };

        double barStart (double ppq) const noexcept
        {
            return origin + std::floor ((ppq - origin) / barLen + 1.0e-9) * barLen;
        }

        double start (Slot s) const noexcept
        {
            double t = s.bar + s.index * seqSlotPpq + ((s.index & 1) != 0 ? clamp01 (seq.swing) * maxSwingPpq : 0.0);
            const double barEnd = s.bar + barLen;
            return t < barEnd ? t : barEnd;
        }

        Slot next (Slot s) const noexcept { return s.index + 1 < slots ? Slot { s.bar, s.index + 1 } : Slot { s.bar + barLen, 0 }; }
        Slot prev (Slot s) const noexcept { return s.index > 0 ? Slot { s.bar, s.index - 1 } : Slot { s.bar - barLen, slots - 1 }; }

        double end (Slot s) const noexcept { return start (next (s)); }   // the next slot's start (or the bar line)

        // The slot `ppq` falls in.
        Slot locate (double ppq) const noexcept
        {
            Slot s { barStart (ppq), 0 };
            int j = (int) std::floor ((ppq - s.bar) / seqSlotPpq + 1.0e-9);
            j = j < 0 ? 0 : (j >= slots ? slots - 1 : j);
            s.index = j;
            if (j > 0 && ppq < start (s) - grid::epsilon)
                s.index = j - 1;          // a swung slot hasn't started yet
            return s;
        }

        bool active (Slot s) const noexcept { return seq.active (s.index); }

        // The window of slot `s` (invalid if its step is off).
        SeqWindow window (Slot s) const noexcept
        {
            SeqWindow w;
            if (! active (s))
                return w;
            w.valid = true;
            w.start = start (s);
            w.slotLength = end (s) - w.start;
            const double gate = grid::clampGate (seq.gate);
            Slot last = s;                // the slot Gate applies to
            if (seq.sustain)
            {
                Slot n = next (s);
                for (int i = 0; i < searchLimit() && ! active (n); ++i)
                {
                    last = n;
                    n = next (n);
                }
                if (! active (n))
                    last = s;
            }
            w.end = start (last) + gate * (end (last) - start (last));
            return w;
        }

        // The open window `ppq` falls in, if any.
        SeqWindow windowAt (double ppq) const noexcept
        {
            Slot s = locate (ppq);
            for (int i = 0; i < searchLimit(); ++i)
            {
                if (active (s))
                {
                    const SeqWindow w = window (s);
                    if (w.start <= ppq + grid::epsilon && ppq < w.end - grid::epsilon)
                        return w;
                    return {};
                }
                s = prev (s);
            }
            return {};
        }

        // The first active step that starts after `ppq` (at or after it, if `inclusive`).
        // Returns false if no step is on.
        bool nextStart (double ppq, bool inclusive, double& out) const noexcept
        {
            Slot s = locate (ppq);
            for (int i = 0; i < searchLimit() + 1; ++i)
            {
                const double t = start (s);
                if (active (s) && (inclusive ? t >= ppq - grid::epsilon : t > ppq + grid::epsilon))
                {
                    out = t;
                    return true;
                }
                s = next (s);
            }
            return false;
        }

    private:
        static constexpr int maxSlots = 512;
        static double clamp01 (double x) noexcept { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }
        int searchLimit() const noexcept { return slots + numSeqSteps + 1; }

        const StepSeqSettings& seq;
        double origin, barLen;
        int slots = 16;
    };
}
