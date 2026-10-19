// Test pattern: while the transport runs, presses one chord-zone white key on every beat
// (C, D, E, F, G, A, B, C… so beat b plays chord slot b mod 7) and releases it an eighth
// note later. The key presses go through the same path as the user's, so a recording of the
// output checks routing, chord generation and timing (LiveBridge, PLAN.md §8).
//
// Plain C++. Stateless apart from duplicate suppression at block edges and the held key.
#pragma once

#include "InputRouter.h"
#include "MidiEvents.h"
#include "Transport.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace chordism
{
    class TestPattern
    {
    public:
        static constexpr int velocity = 100;

        static int slotForBeat (int64_t beat) noexcept { return (int) (((beat % 7) + 7) % 7); }

        // Adds this block's key presses to `out` (in sample order). `on` is the parameter.
        template <class Out>
        void generate (bool on, const Transport& t, int numSamples, const InputRouter& router, Out& out) noexcept
        {
            const double spb = t.samplesPerBeat();
            const double endPpq = t.ppq + numSamples / spb;
            const bool jumpedBack = t.ppq < lastEndPpq - 1.0e-6;

            if (heldKey >= 0 && (! on || ! t.playing || jumpedBack))
                releaseHeld (0, out);

            if (! on || ! t.playing)
            {
                lastHalfBeat = std::numeric_limits<int64_t>::min();
                lastEndPpq = t.ppq;
                return;
            }

            if (jumpedBack)
                lastHalfBeat = std::numeric_limits<int64_t>::min();

            // Half-beat grid: even = key down on the beat, odd = key up an eighth later.
            for (auto h = (int64_t) std::ceil (t.ppq * 2.0 - 1.0e-7); (double) h < endPpq * 2.0 - 1.0e-7; ++h)
            {
                if (h <= lastHalfBeat)
                    continue;
                lastHalfBeat = h;

                int sample = (int) std::llround (((double) h * 0.5 - t.ppq) * spb);
                sample = sample < 0 ? 0 : (sample >= numSamples ? numSamples - 1 : sample);

                if (heldKey >= 0)
                    releaseHeld (sample, out);

                if ((h & 1) == 0)
                {
                    const int key = router.chordKeyForSlot (slotForBeat (h / 2));
                    if (key >= 0 && out.push (MidiEvent::noteOn (0, key, velocity, sample)))
                        heldKey = key;
                }
            }
            lastEndPpq = endPpq;
        }

        // Key-up for a held pattern key, e.g. when the engine is reset.
        template <class Out>
        void releaseHeld (int sample, Out& out) noexcept
        {
            if (heldKey >= 0 && out.push (MidiEvent::noteOff (0, heldKey, sample)))
                heldKey = -1;
        }

        void reset() noexcept
        {
            heldKey = -1;
            lastHalfBeat = std::numeric_limits<int64_t>::min();
            lastEndPpq = 0.0;
        }

        int getHeldKey() const noexcept { return heldKey; }

    private:
        int heldKey = -1;
        int64_t lastHalfBeat = std::numeric_limits<int64_t>::min();
        double lastEndPpq = 0.0;
    };
}
