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
#include "../core/engine/SnapshotExchange.h"
#include "ChordEditorState.h"
#include "Parameters.h"
#include "VirtualMidiOut.h"
#include <atomic>

namespace chordism
{
    // What the chord zone plays now, for the editor (published by the audio thread).
    struct ChordDisplay
    {
        ChordSet chords {};
        ChordContext context;
        int lastSlot = -1;
        int activeShifter = -1;

        bool operator== (const ChordDisplay& o) const noexcept
        {
            return chords == o.chords && context.key == o.context.key && context.scale == o.context.scale
                && context.outputOctave == o.context.outputOctave && lastSlot == o.lastSlot && activeShifter == o.activeShifter;
        }
    };

    class ChordismProcessor : public juce::AudioProcessor,
                              private juce::Timer,
                              private juce::ValueTree::Listener
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
        ChordEditorState chordEditor;     // the Chord Editor's edits (not host parameters)

        // Message thread, one reader (the editor).
        const ChordDisplay& readDisplay() noexcept { return display.read(); }

        // The engine settings the parameters give now (any thread; lock-free reads).
        EngineSettings currentSettings() const noexcept { return snapshot.engineSettings(); }

        // On-screen keys (Phase 5): the editor's keyboards play through the engine like host
        // MIDI input. Message thread only; lock-free, read by the next audio block.
        void pushUiNote (int note, int velocity, bool on) noexcept;

        // Key and note states for the editor (written by the audio thread each block): an
        // output note sounds; an input key is down; a key's performance plays (held or latched).
        bool isNoteSounding (int note) const noexcept { return testBit (sounding, note); }
        bool isKeyDown (int note) const noexcept { return testBit (keysDown, note); }
        bool isPerforming (int note) const noexcept { return testBit (performing, note); }

        // Editor preferences saved with the plug-in (page shown, keyboard shown). Message thread.
        juce::ValueTree uiState { "UI" };

        // Status for the editor and tests (written by the audio thread).
        juce::String getPortStatus() const;
        std::atomic<int> lastChordSlot { -1 };
        std::atomic<int> soundingNotes { 0 };
        std::atomic<bool> transportPlaying { false };
        std::atomic<bool> usingHostClock { false };
        uint32_t getPortEndpoint() const noexcept { return port.getEndpoint(); }

    private:
        using NoteBits = std::array<std::atomic<uint64_t>, 2>;
        static bool testBit (const NoteBits& b, int note) noexcept
        {
            return note >= 0 && note < 128 && ((b[(size_t) (note >> 6)].load (std::memory_order_relaxed) >> (note & 63)) & 1u) != 0;
        }
        void publishNoteStates() noexcept;

        // The Chord Editor's edits and the UI's preferences aren't parameters: Live only saves
        // them in a set if the plug-in says its state changed (and not for a restore).
        void notifyStateChanged();
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { notifyStateChanged(); }
        std::atomic<uint32_t> notifiedEditsVersion { 0 };
        std::atomic<bool> restoringState { false };

        static BusesProperties makeBuses();
        void timerCallback() override;
        void run (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool bypassed);
        Transport readTransport (int64_t& blockNs);

        params::Snapshot snapshot;
        ChordismEngine engine;

        // Preallocated in the constructor / prepareToPlay; never resized in processBlock.
        EventBuffer<2048> input;
        std::array<MidiEvent, 256> uiNotes {};
        juce::AbstractFifo uiFifo { 256 };
        NoteBits sounding {}, keysDown {}, performing {};
        BlockEvents output;
        juce::MidiBuffer generated;

        VirtualMidiOut port;
        int savedPortIndex = 0;
        std::atomic<double> preparedSampleRate { 44100.0 };
        std::atomic<int> preparedBlockSize { 512 };
        Transport lastTransport;
        SnapshotExchange<ChordDisplay> display;
        ChordDisplay lastDisplay;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordismProcessor)
    };
}
