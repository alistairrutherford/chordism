// Test pattern: while the transport runs, presses one chord-zone white key on every beat
// (C, D, E, F, G, A, B, C… so beat b plays chord slot b mod 7) and releases it an eighth
// note later. The key presses go through the same path as the user's, so a recording of the
// output checks routing, chord generation and timing (LiveBridge, PLAN.md §8).
//
// With a test shifter (0..4) it also holds that shifter's black key through every odd bar
// (bars 1, 3, 5… counted from 0 at ppq 0): pressed on the bar line just before that beat's
// chord key, released on the next bar line. So odd bars play shifted chords.
//
// Test smart (Phase 4) drives the Smart Scale zone too, just after each beat's chord key:
//   Tones      presses the Smart Scale white key with letter 3b mod 7 on beat b (C, F, B, E, A,
//              D, G, C…, in the first octave at or above the split), released with the chord key
//   Pattern n  holds pattern key n (the first at or above the split) for as long as the test
//              runs, so the pattern follows every chord change
//   Tones early (Phase 7) as Tones, but each beat's Smart Scale key goes down a 1/64 note (1/16
//              of a beat) before the beat, ahead of its chord: on a loop, look-ahead
//              (ChordTimeline.h) gives it the upcoming chord. Before a loop wrap it anticipates
//              the beat at the loop start, and stays down through the wrap.
//
// Plain C++. Stateless apart from duplicate suppression at block edges and the held keys.
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

        // Test smart: 0 = Off, 1 = Tones, 2 + n = Pattern n, then Tones early (Phase 7, appended):
        // the parameter's choices.
        static constexpr int smartOff = 0, smartTones = 1, smartPattern = 2, smartTonesEarly = smartPattern + 5;
        static constexpr int numSmartTests = smartTonesEarly + 1;

        // Tones early: how far before its beat the Smart Scale key goes down (quarter notes).
        static constexpr double earlyPpq = 1.0 / 16.0;

        // The Smart Scale white key letter (C = 0) Tones presses on beat `beat`.
        static int letterForBeat (int64_t beat) noexcept { return (int) (((3 * beat) % 7 + 7) % 7); }

        // Quarter notes per bar.
        static double barLength (const Transport& t) noexcept
        {
            return t.numerator > 0 && t.denominator > 0 ? t.numerator * 4.0 / t.denominator : 4.0;
        }

        // True if the test shifter is held on beat `beat` (ppq) with time signature `t`.
        static bool shiftedAt (double beat, const Transport& t) noexcept
        {
            const auto bar = (int64_t) std::floor (beat / barLength (t) + 1.0e-9);
            return (bar & 1) != 0;
        }

        // Adds this block's key presses to `out` (in sample order). `on` is the parameter;
        // `shifter` is the test shifter slot, or -1. For one segment of a block (Clock.h), `t`
        // holds the segment's start and `offset` its first sample: events are stamped from it.
        // `smart` is Test smart (smartOff, smartTones, smartPattern + n).
        template <class Out>
        void generate (bool on, int shifter, const Transport& t, int numSamples, const InputRouter& router, Out& out,
                       int offset = 0, int smart = smartOff) noexcept
        {
            const double spb = t.samplesPerBeat();
            // A segment that ends at a loop wrap stops short of the loop end in musical time (a
            // key there belongs after the wrap, Clock.h), and a segment starting just after the
            // wrap (by part of a sample) still has the key on the loop start.
            double endPpq = t.ppq + numSamples / spb;
            if (t.looping && t.loopEnd > t.loopStart && t.ppq < t.loopEnd && endPpq > t.loopEnd)
                endPpq = t.loopEnd;
            const double fromPpq = t.ppq - 1.0 / spb;
            const bool jumpedBack = t.ppq < lastEndPpq - 1.0e-6;
            // A loop wrap (rather than a locate): Tones early keeps its key down through it.
            const bool wrapped = jumpedBack && t.looping && std::abs (t.ppq - t.loopStart) < 1.0e-3
                              && std::abs (lastEndPpq - t.loopEnd) < 1.0e-3;
            const int shifterKey = on ? router.shifterKeyForSlot (shifter) : -1;
            const bool early = on && smart == smartTonesEarly;
            const bool tones = on && (smart == smartTones || early);
            const int patternKey = on && smart >= smartPattern && smart < smartPattern + 5
                                 ? router.patternKeyForSlot (smart - smartPattern) : -1;

            if (heldKey >= 0 && (! on || ! t.playing || jumpedBack))
                releaseHeld (offset, out);
            if (heldScaleKey >= 0 && (! tones || ! t.playing || (jumpedBack && ! (early && wrapped))))
                releaseScale (offset, out);
            if (heldShifter >= 0 && (! on || ! t.playing || jumpedBack || heldShifter != shifterKey))
                releaseShifter (offset, out);
            if (heldPattern >= 0 && (! t.playing || jumpedBack || heldPattern != patternKey))
                releasePattern (offset, out);

            if (! on || ! t.playing)
            {
                lastTick = std::numeric_limits<int64_t>::min();
                lastEndPpq = t.ppq;
                return;
            }

            if (jumpedBack)
                lastTick = std::numeric_limits<int64_t>::min();

            // A grid of 1/16 beats: tick 8h is half beat h (even = key down on the beat, odd = key
            // up an eighth later); with Tones early, tick 16b - 1 presses beat b's Smart key.
            for (auto k = (int64_t) std::ceil (fromPpq * ticksPerBeat - 8.0e-7); (double) k < endPpq * ticksPerBeat - 8.0e-7; ++k)
            {
                if (k <= lastTick)
                    continue;
                lastTick = k;
                const bool halfBeat = k % 8 == 0;
                const bool earlyPress = early && ((k % 16) + 16) % 16 == 15;
                if (! halfBeat && ! earlyPress)
                    continue;

                int sample = (int) std::llround (((double) k / ticksPerBeat - t.ppq) * spb);
                sample = offset + (sample < 0 ? 0 : (sample >= numSamples ? numSamples - 1 : sample));

                if (earlyPress)
                {
                    // The beat it's for: across a loop wrap, the beat at the loop start.
                    double beat = (double) (k + 1) / ticksPerBeat;
                    if (t.looping && t.loopEnd > t.loopStart && beat >= t.loopEnd - 1.0e-9)
                        beat = t.loopStart + (beat - t.loopEnd);
                    if (heldScaleKey >= 0)
                        releaseScale (sample, out);
                    const int key = router.scaleKeyForLetter (letterForBeat ((int64_t) std::floor (beat + 1.0e-9)));
                    if (key >= 0 && out.push (MidiEvent::noteOn (0, key, velocity, sample)))
                        heldScaleKey = key;
                    continue;
                }

                const int64_t h = k / 8;
                const bool down = (h & 1) == 0;
                if (heldKey >= 0)
                    releaseHeld (sample, out);
                if (heldScaleKey >= 0 && (! early || ! down))
                    releaseScale (sample, out);

                if (down)
                {
                    if (shifterKey >= 0)
                    {
                        const bool shift = shiftedAt ((double) h * 0.5, t);
                        if (! shift && heldShifter >= 0)
                            releaseShifter (sample, out);
                        else if (shift && heldShifter < 0 && out.push (MidiEvent::noteOn (0, shifterKey, velocity, sample)))
                            heldShifter = shifterKey;
                    }

                    const int key = router.chordKeyForSlot (slotForBeat (h / 2));
                    if (key >= 0 && out.push (MidiEvent::noteOn (0, key, velocity, sample)))
                        heldKey = key;

                    if (tones && ! early)
                    {
                        const int k2 = router.scaleKeyForLetter (letterForBeat (h / 2));
                        if (k2 >= 0 && out.push (MidiEvent::noteOn (0, k2, velocity, sample)))
                            heldScaleKey = k2;
                    }
                    if (patternKey >= 0 && heldPattern < 0 && out.push (MidiEvent::noteOn (0, patternKey, velocity, sample)))
                        heldPattern = patternKey;
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

        template <class Out>
        void releaseShifter (int sample, Out& out) noexcept
        {
            if (heldShifter >= 0 && out.push (MidiEvent::noteOff (0, heldShifter, sample)))
                heldShifter = -1;
        }

        template <class Out>
        void releaseScale (int sample, Out& out) noexcept
        {
            if (heldScaleKey >= 0 && out.push (MidiEvent::noteOff (0, heldScaleKey, sample)))
                heldScaleKey = -1;
        }

        template <class Out>
        void releasePattern (int sample, Out& out) noexcept
        {
            if (heldPattern >= 0 && out.push (MidiEvent::noteOff (0, heldPattern, sample)))
                heldPattern = -1;
        }

        void reset() noexcept
        {
            heldKey = -1;
            heldShifter = -1;
            heldScaleKey = -1;
            heldPattern = -1;
            lastTick = std::numeric_limits<int64_t>::min();
            lastEndPpq = 0.0;
        }

        int getHeldKey() const noexcept { return heldKey; }
        int getHeldShifter() const noexcept { return heldShifter; }
        int getHeldScaleKey() const noexcept { return heldScaleKey; }
        int getHeldPattern() const noexcept { return heldPattern; }

    private:
        int heldKey = -1;
        int heldShifter = -1;
        int heldScaleKey = -1;
        int heldPattern = -1;
        static constexpr double ticksPerBeat = 16.0;
        int64_t lastTick = std::numeric_limits<int64_t>::min();
        double lastEndPpq = 0.0;
    };
}
