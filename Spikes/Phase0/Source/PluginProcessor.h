#pragma once

#include <JuceHeader.h>
#include "HostedInstrument.h"
#include "MidiExport.h"
#include "SharedSession.h"
#include "midi/VirtualMidiOut.h"
#include <array>
#include <atomic>
#include <limits>

// Chordism Phase 0 spike. A throwaway instrument plugin that answers four
// questions about Ableton Live 12 (see README.md):
//   1. Can another track take this plugin's VST3 MIDI output and record it on time?
//   2. Can the plugin host an AU/VST3 instrument and play it?
//   3. Does a .mid file dragged out of the plugin land in Live as a clip?
//   4. Do two instances share process-wide state (Instance Sync)?
//   1b. If Live won't route VST3 MIDI out: does a CoreMIDI virtual port
//       ("Chordism Spike Out n") record on time instead?
//
// MIDI behaviour: a white key below or above anything plays the diatonic triad of
// the current major key on that degree; black keys pass through. "Test pattern"
// plays C F G Am (in the current key) on every beat while the transport runs.
class SpikeProcessor : public juce::AudioProcessor,
                       private juce::Timer
{
public:
    SpikeProcessor();
    ~SpikeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- controls (message thread) ----
    void setKeyFromUi (int key);
    int getEffectiveKey() const noexcept;
    std::atomic<bool> syncKey { true };
    std::atomic<bool> testPattern { false };
    std::atomic<bool> usePort { true };          // CoreMIDI virtual output port
    std::atomic<float> portOffsetMs { 0.0f };    // timestamp calibration for the port

    // Tells the host the plugin state changed (so Live marks the set as modified
    // and asks to save it). Message thread.
    void markStateChanged (const juce::String& why);

    // ---- status for the UI and tests ----
    const int instanceId;
    std::atomic<bool> playing { false };
    std::atomic<double> currentBpm { 120.0 }, currentPpq { 0.0 };
    std::atomic<int> lastInputNote { -1 }, lastOutputNote { -1 };
    std::atomic<int> outputNoteOns { 0 };
    std::atomic<bool> usingHostClock { false };

    juce::String getPortStatus() const;
    uint32_t getPortEndpoint() const noexcept { return midiOut.getEndpoint(); }   // tests

    HostedInstrument hosted;
    ExportRecorder recorder;

    int soundingNoteCount() const noexcept;   // output notes currently on (tests)

private:
    void timerCallback() override;

    // Output helpers: reference-count each output pitch so two sources that share a
    // note never cut each other off, and log everything for MIDI export.
    void noteOn (juce::MidiBuffer& out, int note, int velocity, int sample);
    void noteOff (juce::MidiBuffer& out, int note, int sample);
    void scheduleOff (int note, int samplesFromNow);
    void playTriad (juce::MidiBuffer& out, int rootDegree, int octaveBase, int key, int velocity, int sample,
                    std::array<int, 3>* notesOut, int lengthSamples);
    void flushPendingOffs (juce::MidiBuffer& out, int sample);
    double sampleToPpq (int sample) const noexcept;

    std::atomic<int> localKey { 0 };

    VirtualMidiOut midiOut;
    int savedPortIndex = 0;
    SharedSession::Member* member = nullptr;
    bool loggedFormat = false;

    // audio-thread state
    double sampleRate = 44100.0;
    double blockPpq = 0.0, samplesPerBeat = 22050.0;
    bool blockPlaying = false, wasPlaying = false;
    int64_t lastBeatFired = std::numeric_limits<int64_t>::min();
    double lastBlockEndPpq = 0.0;

    std::array<uint8_t, 128> outCount {};
    std::array<std::array<int, 3>, 128> heldChords {};   // input note → output notes (-1 = none)
    std::array<bool, 128> inputHeld {};

    struct PendingOff { int note = -1; int samplesLeft = 0; };
    std::array<PendingOff, 128> pendingOffs {};

    juce::MidiBuffer generated;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SpikeProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpikeProcessor)
};
