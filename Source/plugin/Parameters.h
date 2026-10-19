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

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Lock-free reads of the current values, for the audio thread.
    class Snapshot
    {
    public:
        explicit Snapshot (juce::AudioProcessorValueTreeState& state);

        EngineSettings engineSettings() const noexcept;
        bool portEnabled() const noexcept { return port->load() > 0.5f; }
        bool portCompensation() const noexcept { return portCompensate->load() > 0.5f; }
        double portTrim() const noexcept { return (double) portTrimMs->load(); }

    private:
        std::atomic<float>* enable;
        std::atomic<float>* key;
        std::atomic<float>* scale;
        std::atomic<float>* chordGroup;
        std::atomic<float>* split;
        std::atomic<float>* chordOctave;
        std::atomic<float>* muteChords;
        std::atomic<float>* muteScale;
        std::atomic<float>* testPattern;
        std::atomic<float>* port;
        std::atomic<float>* portCompensate;
        std::atomic<float>* portTrimMs;
    };

    // MIDI note name as Live writes it (60 = C3).
    juce::String liveNoteName (int note);
}
