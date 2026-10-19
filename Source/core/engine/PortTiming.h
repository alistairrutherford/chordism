// Timestamps for the CoreMIDI virtual port (PLAN.md §3).
//
// Live records a port event one audio buffer plus a fixed ~1.6 ms after its timestamp
// (measured in Live 12.4.6: +1.54 to +1.69 ms over 44.1/48/96 kHz and 128/512/1024-sample
// buffers). Only the fixed part can be compensated: Live honours a stamp at most ~1.6 ms
// before the event is sent, and clamps anything earlier to its arrival, which smears a block's
// events over a whole buffer. So events are stamped `portFixedLatencyMs` early (just under the
// smallest delay measured, so nothing is clamped), and the port route records one buffer late,
// like a hardware MIDI input. A user trim is added on top. The VST3 "MIDI From" route is
// sample-accurate and needs no correction.
#pragma once

#include <cstdint>

namespace chordism
{
    constexpr double portFixedLatencyMs = 1.5;

    struct PortTiming
    {
        double sampleRate = 44100.0;
        bool compensate = true;
        double trimMs = 0.0;

        // How much earlier than its place in the block an event is stamped, in ns
        // (negative = later).
        int64_t leadNs() const noexcept
        {
            return (int64_t) (((compensate ? portFixedLatencyMs : 0.0) - trimMs) * 1.0e6);
        }

        // The host-clock stamp for an event at `sample` in a block that starts at `blockNs`.
        int64_t stampNs (int64_t blockNs, int sample) const noexcept
        {
            const double nsPerSample = sampleRate > 0.0 ? 1.0e9 / sampleRate : 0.0;
            return blockNs + (int64_t) (sample * nsPerSample) - leadNs();
        }

        // How late Live records port events with compensation on: one buffer, plus whatever
        // part of the fixed delay the lead leaves (a fraction of a ms).
        static double expectedLateMs (double sampleRate, int bufferSize) noexcept
        {
            return sampleRate > 0.0 ? 1000.0 * bufferSize / sampleRate : 0.0;
        }
    };
}
