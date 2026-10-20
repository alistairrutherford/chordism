// Chordism's factory presets (PLAN.md §4.7): about 20, our own, in three categories: Genre,
// Artist-style (described by style only, never by an artist's name) and Vibe. Each is a list of
// host parameter values on top of the defaults (plain values: a choice's index, a bool's 0/1,
// a percentage 0-100), so the list is checked by the compiler against the engine's enums.
// Program 0 of the plug-in is "Init" (every default), so a new instance is its program 0.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <utility>
#include <vector>

namespace chordism
{
    struct FactoryPreset
    {
        juce::String category, name;
        std::vector<std::pair<juce::String, float>> values;   // parameter ID → plain value
    };

    // "Init" first, then the factory presets in browser order.
    const std::vector<FactoryPreset>& factoryPresets();
}
