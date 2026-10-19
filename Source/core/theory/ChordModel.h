// The chord model of the Chord Generator and Chord Editor (PLAN.md §4.2). Plain C++, no JUCE.
//
// A chord is a stack of thirds on a scale degree. Each of the seven steps (1, 3, 5, 7, 9, 11,
// 13) can be on or off, carry an accidental, and sound in any of five octaves:
//
//   scaleIndex(k) = degree + 2k
//   pitch(k, o)   = base + keyRoot + scale[scaleIndex(k) mod 7] + 12·⌊scaleIndex(k)/7⌋
//                   + accidental[k] + 12·o + 12·outputOctave        for each o in octaveMask
#pragma once

#include "Scales.h"
#include <array>
#include <cstdint>

namespace chordism
{
    constexpr int numChordSteps = 7;      // 1, 3, 5, 7, 9, 11, 13
    constexpr int numChordSlots = 7;      // one per white key of the chord zone
    constexpr int chordBaseNote = 48;     // MIDI note of C in the chord output's default octave

    // Octave mask bit i stands for octave (i - 2), so bit 2 is the chord's own octave.
    constexpr uint8_t octaveBit (int octave) noexcept { return (uint8_t) (1u << (octave + 2)); }
    constexpr int minStepOctave = -2, maxStepOctave = 2;

    struct Step
    {
        bool on = false;
        int8_t accidental = 0;            // -2..+2 semitones
        uint8_t octaveMask = octaveBit (0);

        bool operator== (const Step& o) const noexcept
        {
            return on == o.on && accidental == o.accidental && octaveMask == o.octaveMask;
        }
    };

    struct ChordSlot
    {
        int degree = 0;                   // 0..6 (I..VII)
        std::array<Step, numChordSteps> steps {};

        bool operator== (const ChordSlot& o) const noexcept { return degree == o.degree && steps == o.steps; }
    };

    // Up to 7 steps × 5 octaves.
    constexpr int maxChordNotes = numChordSteps * (maxStepOctave - minStepOctave + 1);

    struct ChordNotes
    {
        std::array<uint8_t, maxChordNotes> notes {};
        int size = 0;

        const uint8_t* begin() const noexcept { return notes.data(); }
        const uint8_t* end() const noexcept { return notes.data() + size; }
    };

    struct ChordContext
    {
        int key = 0;                      // 0..11, C = 0
        Scale scale = Scale::Major;
        int outputOctave = 0;
    };

    // The MIDI notes a chord slot plays, lowest step first (not sorted by pitch). Notes outside
    // 0..127 are dropped; a pitch reached by two steps is listed once.
    inline ChordNotes chordNotes (const ChordSlot& slot, const ChordContext& ctx) noexcept
    {
        ChordNotes out;
        std::array<bool, 128> seen {};
        const int base = chordBaseNote + ((ctx.key % 12) + 12) % 12 + 12 * ctx.outputOctave;

        for (int k = 0; k < numChordSteps; ++k)
        {
            const Step& step = slot.steps[(size_t) k];
            if (! step.on)
                continue;

            const int stepPitch = base + scaleOffset (ctx.scale, slot.degree + 2 * k) + step.accidental;
            for (int o = minStepOctave; o <= maxStepOctave; ++o)
            {
                if ((step.octaveMask & octaveBit (o)) == 0)
                    continue;

                const int note = stepPitch + 12 * o;
                if (note < 0 || note > 127 || seen[(size_t) note])
                    continue;

                seen[(size_t) note] = true;
                out.notes[(size_t) out.size++] = (uint8_t) note;
            }
        }
        return out;
    }
}
