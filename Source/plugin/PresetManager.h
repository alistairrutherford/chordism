// Presets (PLAN.md §4.7): the factory presets (FactoryPresets.h) and the user's preset files,
// XML in ~/Music/Chordism/Presets/<Category>/<Name>.xml, each the plug-in state's own ValueTree
// (the host parameters and the Chord Editor's edits) without what a preset doesn't touch: the
// routing (Enable, the virtual port), the keyboard's Split and the test rig
// (params::isPresetParameter), and the editor's own preferences. A file in any category folder
// is listed under that category.
//
// The browser lists the factory presets first (Init, then Genre, Artist-style, Vibe), then the
// files by category and name. Message thread only.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "FactoryPresets.h"

namespace chordism
{
    class ChordismProcessor;

    class PresetManager
    {
    public:
        struct Entry
        {
            juce::String category, name;
            int factory = -1;             // index into factoryPresets(), or -1 for a file
            juce::File file;

            juce::String label() const { return category == name ? name : category + " / " + name; }
        };

        explicit PresetManager (ChordismProcessor& p) : processor (p) {}

        static juce::StringArray categories() { return { "Genre", "Artist-style", "Vibe", "User" }; }
        static juce::File userFolder();
        static juce::String fileExtension() { return ".xml"; }

        // Lists the factory presets and the files on disk again.
        void rescan();
        const juce::Array<Entry>& getEntries();

        // The entry of the preset loaded last (by category and name), or -1.
        int current();

        bool load (int index);
        bool step (int delta);            // the previous or next preset, wrapping

        // Saves the current state as a preset file (a new one, or over the one with that name).
        juce::Result save (const juce::String& name, const juce::String& category);

        // A preset file's tree, or an invalid tree.
        static juce::ValueTree readFile (const juce::File& file);

        // The tree of factory preset `index`: its values over the defaults.
        static juce::ValueTree factoryTree (int index);

    private:
        ChordismProcessor& processor;
        juce::Array<Entry> entries;
        bool scanned = false;
    };
}
