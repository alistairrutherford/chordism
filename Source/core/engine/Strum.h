// Strum (PLAN.md §4.5): the chord's tones start one after another instead of together, and
// are all held to the end of the chord.
//
//   Up           lowest tone first
//   Down         highest tone first
//   Alternating  up, down, up… one direction per strike, across all chords (like a guitar)
//   Random       a shuffled order per strike, from a seeded generator: the same playing
//                gives the same strums (it's reseeded on reset and when the transport starts)
//
// Strum Rate is the time between tones, as a note value while the host plays and in
// milliseconds while it's stopped. Plain C++, no allocation.
#pragma once

#include <cstdint>

namespace chordism
{
    // The order of the Strum parameter's choices: only ever append.
    enum class StrumMode : uint8_t { Off, Up, Down, Alternating, Random, Count };
    constexpr int numStrumModes = (int) StrumMode::Count;

    inline const char* strumModeName (StrumMode m) noexcept
    {
        static constexpr const char* names[numStrumModes] = { "Off", "Up", "Down", "Alternating", "Random" };
        const int i = (int) m;
        return names[i >= 0 && i < numStrumModes ? i : 0];
    }

    class Strummer
    {
    public:
        static constexpr uint32_t seed = 0x5eed1234u;

        void reset() noexcept
        {
            state = seed;
            nextDown = false;
        }

        // The order of the next strike's tones: out[k] = index (into the ascending tones) of the
        // k-th tone to start, for `n` tones.
        void order (StrumMode mode, int n, uint8_t* out) noexcept
        {
            bool down = mode == StrumMode::Down;
            if (mode == StrumMode::Alternating)
            {
                down = nextDown;
                nextDown = ! nextDown;
            }

            for (int k = 0; k < n; ++k)
                out[k] = (uint8_t) (down ? n - 1 - k : k);

            if (mode == StrumMode::Random)
                for (int k = n - 1; k > 0; --k)   // Fisher-Yates
                {
                    const int j = (int) (next() % (uint32_t) (k + 1));
                    const uint8_t t = out[k];
                    out[k] = out[j];
                    out[j] = t;
                }
        }

    private:
        uint32_t next() noexcept   // xorshift32
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        uint32_t state = seed;
        bool nextDown = false;
    };
}
