#include "Parameters.h"

namespace chordism::params
{
    namespace
    {
        juce::ParameterID id (const char* name) { return { name, 1 }; }

        juce::StringArray keyNames()
        {
            juce::StringArray a;
            for (int k = 0; k < numKeys; ++k)
                a.add (keyName (k));
            return a;
        }

        juce::StringArray scaleNames()
        {
            juce::StringArray a;
            for (int s = 0; s < numScales; ++s)
                a.add (scaleInfo ((Scale) s).name);
            return a;
        }

        juce::StringArray groupNames()
        {
            juce::StringArray a;
            for (int g = 0; g < numChordGroups; ++g)
                a.add (chordGroupInfo ((ChordGroup) g).name);
            return a;
        }

        juce::ParameterID id (const juce::String& name) { return { name, 1 }; }

        juce::StringArray progressionNames()
        {
            juce::StringArray a;
            for (int p = 0; p < numProgressions; ++p)
                a.add (progressionInfo ((Progression) p).name);
            return a;
        }

        juce::StringArray degreeNames() { return { "I", "II", "III", "IV", "V", "VI", "VII" }; }

        juce::StringArray withSame (juce::StringArray a)
        {
            a.insert (0, "Same");
            return a;
        }

        juce::String numbered (const char* prefix, int slot, const char* suffix)
        {
            return juce::String (prefix) + juce::String (slot + 1) + suffix;
        }

        bool on (const std::atomic<float>* p) noexcept { return p->load() > 0.5f; }
        int whole (const std::atomic<float>* p) noexcept { return (int) std::lround (p->load()); }
    }

    juce::String customDegree (int slot) { return numbered ("customDegree", slot, ""); }
    juce::String shifterKind (int slot)  { return numbered ("shifter", slot, "Kind"); }
    juce::String shifterGroup (int slot) { return numbered ("shifter", slot, "Group"); }
    juce::String shifterKey (int slot)   { return numbered ("shifter", slot, "Key"); }
    juce::String shifterScale (int slot) { return numbered ("shifter", slot, "Scale"); }

    juce::String liveNoteName (int note)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 2);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<AudioParameterBool> (id (enable), "Enable", true));
        layout.add (std::make_unique<AudioParameterChoice> (id (key), "Key", keyNames(), 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (scale), "Scale", scaleNames(), 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (chordGroup), "Chord Group", groupNames(), 0));
        layout.add (std::make_unique<AudioParameterInt> (
            id (split), "Split", 0, 127, InputRouter::defaultSplit,
            AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return liveNoteName (v); })));
        layout.add (std::make_unique<AudioParameterInt> (id (chordOctave), "Chord Octave", -3, 3, 0));
        layout.add (std::make_unique<AudioParameterBool> (id (muteChords), "Mute CG", false));
        layout.add (std::make_unique<AudioParameterBool> (id (muteScale), "Mute SS", false));
        layout.add (std::make_unique<AudioParameterBool> (id (testPattern), "Test pattern", false));

        // The virtual port is the only route out of the AU instrument in Live, and isn't
        // needed by the Logic MIDI effect.
        layout.add (std::make_unique<AudioParameterBool> (id (port), "Virtual port", ! JucePlugin_IsMidiEffect));
        layout.add (std::make_unique<AudioParameterBool> (id (portCompensate), "Port compensation", true));
        layout.add (std::make_unique<AudioParameterFloat> (
            id (portTrimMs), "Port trim", NormalisableRange<float> (-20.0f, 20.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("ms")));

        // Phase 2: Progression, its custom order, and the shifters. Appended, never reordered.
        layout.add (std::make_unique<AudioParameterChoice> (id (progression), "Progression", progressionNames(), 0));
        for (int n = 0; n < numChordSlots; ++n)
            layout.add (std::make_unique<AudioParameterChoice> (id (customDegree (n)), "Custom " + String (n + 1), degreeNames(), n));

        layout.add (std::make_unique<AudioParameterChoice> (id (shifterMode), "Shifter mode", StringArray { "Standard", "Toggle" }, 0));
        const auto targets = defaultShifterTargets();
        for (int n = 0; n < numShifters; ++n)
        {
            const auto& t = targets[(size_t) n];
            const String name = "Shifter " + String (n + 1);
            layout.add (std::make_unique<AudioParameterChoice> (id (shifterKind (n)), name + " type",
                                                                StringArray { "Chord Group", "Key/Scale" }, (int) t.kind));
            layout.add (std::make_unique<AudioParameterChoice> (id (shifterGroup (n)), name + " group", groupNames(), (int) t.group));
            layout.add (std::make_unique<AudioParameterChoice> (id (shifterKey (n)), name + " key", withSame (keyNames()), t.key + 1));
            layout.add (std::make_unique<AudioParameterChoice> (id (shifterScale (n)), name + " scale", withSame (scaleNames()), t.scale + 1));
        }

        // The test pattern holds this shifter through every odd bar (TestPattern.h).
        layout.add (std::make_unique<AudioParameterChoice> (id (testShifter), "Test shifter",
                                                            StringArray { "Off", "1", "2", "3", "4", "5" }, 0));
        return layout;
    }

    Snapshot::Snapshot (juce::AudioProcessorValueTreeState& state)
        : enable (state.getRawParameterValue (params::enable)),
          key (state.getRawParameterValue (params::key)),
          scale (state.getRawParameterValue (params::scale)),
          group (state.getRawParameterValue (params::chordGroup)),
          split (state.getRawParameterValue (params::split)),
          chordOctave (state.getRawParameterValue (params::chordOctave)),
          muteChords (state.getRawParameterValue (params::muteChords)),
          muteScale (state.getRawParameterValue (params::muteScale)),
          testPattern (state.getRawParameterValue (params::testPattern)),
          port (state.getRawParameterValue (params::port)),
          portCompensate (state.getRawParameterValue (params::portCompensate)),
          portTrimMs (state.getRawParameterValue (params::portTrimMs)),
          progression (state.getRawParameterValue (params::progression)),
          shifterMode (state.getRawParameterValue (params::shifterMode)),
          testShifter (state.getRawParameterValue (params::testShifter))
    {
        jassert (enable && key && scale && group && split && chordOctave && muteChords && muteScale
                 && testPattern && port && portCompensate && portTrimMs && progression && shifterMode && testShifter);

        for (int n = 0; n < numChordSlots; ++n)
        {
            customDegree[(size_t) n] = state.getRawParameterValue (params::customDegree (n));
            jassert (customDegree[(size_t) n] != nullptr);
        }
        for (int n = 0; n < numShifters; ++n)
        {
            shifterKind[(size_t) n] = state.getRawParameterValue (params::shifterKind (n));
            shifterGroup[(size_t) n] = state.getRawParameterValue (params::shifterGroup (n));
            shifterKey[(size_t) n] = state.getRawParameterValue (params::shifterKey (n));
            shifterScale[(size_t) n] = state.getRawParameterValue (params::shifterScale (n));
            jassert (shifterKind[(size_t) n] && shifterGroup[(size_t) n] && shifterKey[(size_t) n] && shifterScale[(size_t) n]);
        }
    }

    ChordGroup Snapshot::chordGroup() const noexcept
    {
        return (ChordGroup) juce::jlimit (0, numChordGroups - 1, whole (group));
    }

    Scale Snapshot::currentScale() const noexcept
    {
        return (Scale) juce::jlimit (0, numScales - 1, whole (scale));
    }

    EngineSettings Snapshot::engineSettings() const noexcept
    {
        EngineSettings s;
        s.enabled = on (enable);
        s.key = juce::jlimit (0, numKeys - 1, whole (key));
        s.scale = (Scale) juce::jlimit (0, numScales - 1, whole (scale));
        s.group = chordGroup();
        s.split = juce::jlimit (0, 127, whole (split));
        s.chordOctave = juce::jlimit (-3, 3, whole (chordOctave));
        s.muteChords = on (muteChords);
        s.muteScale = on (muteScale);
        s.testPattern = on (testPattern);

        s.progression = (Progression) juce::jlimit (0, numProgressions - 1, whole (progression));
        for (int n = 0; n < numChordSlots; ++n)
            s.customOrder[(size_t) n] = (int8_t) juce::jlimit (0, numChordSlots - 1, whole (customDegree[(size_t) n]));
        s.shifterMode = whole (shifterMode) == 1 ? ShifterMode::Toggle : ShifterMode::Standard;
        for (int n = 0; n < numShifters; ++n)
        {
            auto& t = s.shifters[(size_t) n];
            t.kind = whole (shifterKind[(size_t) n]) == 1 ? ShifterKind::KeyScale : ShifterKind::ChordGroup;
            t.group = (ChordGroup) juce::jlimit (0, numChordGroups - 1, whole (shifterGroup[(size_t) n]));
            t.key = juce::jlimit (0, numKeys, whole (shifterKey[(size_t) n])) - 1;
            t.scale = juce::jlimit (0, numScales, whole (shifterScale[(size_t) n])) - 1;
        }
        s.testShifter = juce::jlimit (0, numShifters, whole (testShifter)) - 1;
        return s;
    }
}
