// Host parameters (APVTS). Every setting a test needs is one of these, because LiveBridge
// reaches the plug-in only through Live's device parameters (PLAN.md §8).
//
// IDs, names and choice lists are saved in Live sets and automation: never rename or reorder
// them, only add new ones.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/engine/ChordismEngine.h"
#include "../core/engine/PortTiming.h"

namespace chordism::params
{
    inline constexpr const char* enable       = "enable";
    inline constexpr const char* key          = "key";
    inline constexpr const char* scale        = "scale";
    inline constexpr const char* chordGroup   = "chordGroup";
    inline constexpr const char* split        = "split";
    inline constexpr const char* chordOctave  = "chordOctave";
    inline constexpr const char* muteChords   = "muteChords";
    inline constexpr const char* muteScale    = "muteScale";
    inline constexpr const char* testPattern  = "testPattern";
    inline constexpr const char* port         = "port";
    inline constexpr const char* portCompensate = "portCompensate";
    inline constexpr const char* portTrimMs   = "portTrimMs";

    // Phase 2 (appended).
    inline constexpr const char* progression  = "progression";
    inline constexpr const char* shifterMode  = "shifterMode";
    inline constexpr const char* testShifter  = "testShifter";
    juce::String customDegree (int slot);     // "customDegree1".."customDegree7"
    juce::String shifterKind (int slot);      // "shifter1Kind".."shifter5Kind"
    juce::String shifterGroup (int slot);     // "shifter1Group"…
    juce::String shifterKey (int slot);       // "shifter1Key"… (0 = Same, then the keys)
    juce::String shifterScale (int slot);     // "shifter1Scale"… (0 = Same, then the scales)

    // Phase 3 (appended): the chord-zone effects.
    inline constexpr const char* stack        = "stack";
    inline constexpr const char* strum        = "strum";
    inline constexpr const char* strumRate    = "strumRate";
    inline constexpr const char* strumMs      = "strumMs";
    inline constexpr const char* arp          = "arp";
    inline constexpr const char* arpRate      = "arpRate";
    inline constexpr const char* arpRange     = "arpRange";
    inline constexpr const char* arpGate      = "arpGate";
    inline constexpr const char* repeat       = "repeat";
    inline constexpr const char* repeatRate   = "repeatRate";
    inline constexpr const char* repeatGate   = "repeatGate";
    inline constexpr const char* seq          = "seq";
    inline constexpr const char* seqPattern   = "seqPattern";
    inline constexpr const char* seqSwing     = "seqSwing";
    inline constexpr const char* seqGate      = "seqGate";
    inline constexpr const char* seqSustain   = "seqSustain";
    inline constexpr const char* velocity     = "velocity";
    inline constexpr const char* velocityPeak = "velocityPeak";
    inline constexpr const char* velocityRange = "velocityRange";
    inline constexpr const char* hold         = "hold";
    juce::String seqStep (int pattern, int step);   // "seq1Step1".."seq4Step16"

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Lock-free reads of the current values, for the audio thread.
    class Snapshot
    {
    public:
        explicit Snapshot (juce::AudioProcessorValueTreeState& state);

        EngineSettings engineSettings() const noexcept;
        ChordGroup chordGroup() const noexcept;
        Scale currentScale() const noexcept;
        ChordFxSettings chordFx() const noexcept;
        bool portEnabled() const noexcept { return port->load() > 0.5f; }
        bool portCompensation() const noexcept { return portCompensate->load() > 0.5f; }
        double portTrim() const noexcept { return (double) portTrimMs->load(); }

    private:
        std::atomic<float>* enable;
        std::atomic<float>* key;
        std::atomic<float>* scale;
        std::atomic<float>* group;
        std::atomic<float>* split;
        std::atomic<float>* chordOctave;
        std::atomic<float>* muteChords;
        std::atomic<float>* muteScale;
        std::atomic<float>* testPattern;
        std::atomic<float>* port;
        std::atomic<float>* portCompensate;
        std::atomic<float>* portTrimMs;
        std::atomic<float>* progression;
        std::atomic<float>* shifterMode;
        std::atomic<float>* testShifter;
        std::array<std::atomic<float>*, numChordSlots> customDegree {};
        std::array<std::atomic<float>*, numShifters> shifterKind {}, shifterGroup {}, shifterKey {}, shifterScale {};

        std::atomic<float>* fxStack;
        std::atomic<float>* fxStrum;
        std::atomic<float>* fxStrumRate;
        std::atomic<float>* fxStrumMs;
        std::atomic<float>* fxArp;
        std::atomic<float>* fxArpRate;
        std::atomic<float>* fxArpRange;
        std::atomic<float>* fxArpGate;
        std::atomic<float>* fxRepeat;
        std::atomic<float>* fxRepeatRate;
        std::atomic<float>* fxRepeatGate;
        std::atomic<float>* fxSeq;
        std::atomic<float>* fxSeqPattern;
        std::atomic<float>* fxSeqSwing;
        std::atomic<float>* fxSeqGate;
        std::atomic<float>* fxSeqSustain;
        std::atomic<float>* fxVelocity;
        std::atomic<float>* fxVelocityPeak;
        std::atomic<float>* fxVelocityRange;
        std::atomic<float>* fxHold;
        std::array<std::array<std::atomic<float>*, numSeqSteps>, numSeqPatterns> fxSeqSteps {};
    };

    // MIDI note name as Live writes it (60 = C3).
    juce::String liveNoteName (int note);
}
