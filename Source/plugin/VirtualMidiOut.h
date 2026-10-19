// A CoreMIDI virtual source owned by one plug-in instance ("Chordism Out <n>").
//
// Live ignores MIDI output from AU plug-ins, but it lists virtual CoreMIDI sources as MIDI
// inputs, so a track can take this port as its input and record it (PLAN.md §3). The audio
// thread pushes time-stamped messages into a lock-free FIFO; a sender thread hands them to
// CoreMIDI. Closing the port releases every note the receiver still holds.
#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>

namespace chordism
{
    class VirtualMidiOut : private juce::Thread
    {
    public:
        VirtualMidiOut();
        ~VirtualMidiOut() override;

        // Opens "Chordism Out <n>" with the lowest free n >= 1, preferring `preferredIndex`.
        // Message thread. Returns false if CoreMIDI refused (see getLastError()).
        bool open (int preferredIndex = 0);
        void close();

        bool isOpen() const noexcept { return endpoint.load() != 0; }
        int getIndex() const noexcept { return index; }
        juce::String getName() const { return nameForIndex (index); }
        uint32_t getEndpoint() const noexcept { return endpoint.load(); }   // MIDIEndpointRef, for tests
        int32_t getLastError() const noexcept { return lastError.load(); }
        uint32_t getNumSent() const noexcept { return numSent.load(); }

        static juce::String nameForIndex (int i) { return "Chordism Out " + juce::String (i); }

        // Audio thread, real-time safe. `hostTimeNs` is on the mach host clock. Returns false
        // if the port is closed or the FIFO is full.
        bool push (const uint8_t* bytes, int numBytes, int64_t hostTimeNs) noexcept;

        // Mach host time in ns.
        static int64_t nowNs() noexcept;

    private:
        void run() override;
        void drain();
        void releaseHeldNotes();

        struct Message
        {
            int64_t hostTimeNs;
            uint8_t bytes[3];
            uint8_t size;
        };

        static constexpr int fifoSize = 2048;
        juce::AbstractFifo fifo { fifoSize };
        std::array<Message, fifoSize> messages {};

        std::atomic<uint32_t> endpoint { 0 };
        int index = 0;
        std::atomic<uint32_t> numSent { 0 };
        std::atomic<int32_t> lastError { 0 };

        // Notes the receiver holds, per channel. Consumer side only (the sender thread, or
        // close() after it stopped).
        std::array<std::bitset<128>, 16> heldNotes {};
        uint64_t latestStamp = 0;   // host ticks of the latest event handed to CoreMIDI

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualMidiOut)
    };
}
