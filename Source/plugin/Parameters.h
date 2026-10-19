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

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Lock-free reads of the current values, for the audio thread.
    class Snapshot
    {
    public:
        explicit Snapshot (juce::AudioProcessorValueTreeState& state);

        EngineSettings engineSettings() const noexcept;
        ChordGroup chordGroup() const noexcept;
        Scale currentScale() const noexcept;
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
    };

    // MIDI note name as Live writes it (60 = C3).
    juce::String liveNoteName (int note);
}
