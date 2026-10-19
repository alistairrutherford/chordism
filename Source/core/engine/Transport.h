// The host transport for one block, as the engine sees it. Plain C++.
#pragma once

namespace chordism
{
    struct Transport
    {
        double sampleRate = 44100.0;
        bool playing = false;
        double ppq = 0.0;                 // quarter notes at the first sample of the block
        double bpm = 120.0;
        int numerator = 4, denominator = 4;

        double samplesPerBeat() const noexcept { return sampleRate * 60.0 / (bpm > 0.0 ? bpm : 120.0); }
    };
}
