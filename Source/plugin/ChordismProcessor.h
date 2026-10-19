// The plug-in: a thin JUCE adapter around ChordismEngine (Source/core/engine).
//
// One source, three builds (PLAN.md §3): VST3 + AU instrument + Standalone from
// Chordism.jucer, and the AU MIDI effect (`aumi`, Logic) from MidiFX/ChordismMidiFX.jucer,
// where JucePlugin_IsMidiEffect is 1. Generated MIDI goes to the host's MIDI output and, when
// "Virtual port" is on, to the CoreMIDI port "Chordism Out <n>".
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/engine/ChordismEngine.h"
#include "../core/engine/PortTiming.h"
#include "Parameters.h"
#include "VirtualMidiOut.h"
#include <atomic>

namespace chordism
{
    class ChordismProcessor : public juce::AudioProcessor,
                              private juce::Timer
    {
    public:
        ChordismProcessor();
        ~ChordismProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;
        using AudioProcessor::processBlockBypassed;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return true; }
        bool isMidiEffect() const override { return JucePlugin_IsMidiEffect != 0; }
        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState state;

        // Status for the editor and tests (written by the audio thread).
        juce::String getPortStatus() const;
        std::atomic<int> lastChordSlot { -1 };
        std::atomic<int> soundingNotes { 0 };
        std::atomic<bool> transportPlaying { false };
        std::atomic<bool> usingHostClock { false };
        uint32_t getPortEndpoint() const noexcept { return port.getEndpoint(); }

    private:
        static BusesProperties makeBuses();
        void timerCallback() override;
        void run (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool bypassed);
        Transport readTransport (int64_t& blockNs);

        params::Snapshot snapshot;
        ChordismEngine engine;

        // Preallocated in the constructor / prepareToPlay; never resized in processBlock.
        EventBuffer<2048> input;
        BlockEvents output;
        juce::MidiBuffer generated;

        VirtualMidiOut port;
        int savedPortIndex = 0;
        std::atomic<double> preparedSampleRate { 44100.0 };
        std::atomic<int> preparedBlockSize { 512 };
        Transport lastTransport;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordismProcessor)
    };
}
