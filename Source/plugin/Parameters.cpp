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

        bool on (const std::atomic<float>* p) noexcept { return p->load() > 0.5f; }
        int whole (const std::atomic<float>* p) noexcept { return (int) std::lround (p->load()); }
    }

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

        return layout;
    }

    Snapshot::Snapshot (juce::AudioProcessorValueTreeState& state)
        : enable (state.getRawParameterValue (params::enable)),
          key (state.getRawParameterValue (params::key)),
          scale (state.getRawParameterValue (params::scale)),
          chordGroup (state.getRawParameterValue (params::chordGroup)),
          split (state.getRawParameterValue (params::split)),
          chordOctave (state.getRawParameterValue (params::chordOctave)),
          muteChords (state.getRawParameterValue (params::muteChords)),
          muteScale (state.getRawParameterValue (params::muteScale)),
          testPattern (state.getRawParameterValue (params::testPattern)),
          port (state.getRawParameterValue (params::port)),
          portCompensate (state.getRawParameterValue (params::portCompensate)),
          portTrimMs (state.getRawParameterValue (params::portTrimMs))
    {
        jassert (enable && key && scale && chordGroup && split && chordOctave && muteChords && muteScale
                 && testPattern && port && portCompensate && portTrimMs);
    }

    EngineSettings Snapshot::engineSettings() const noexcept
    {
        EngineSettings s;
        s.enabled = on (enable);
        s.key = juce::jlimit (0, numKeys - 1, whole (key));
        s.scale = (Scale) juce::jlimit (0, numScales - 1, whole (scale));
        s.group = (ChordGroup) juce::jlimit (0, numChordGroups - 1, whole (chordGroup));
        s.split = juce::jlimit (0, 127, whole (split));
        s.chordOctave = juce::jlimit (-3, 3, whole (chordOctave));
        s.muteChords = on (muteChords);
        s.muteScale = on (muteScale);
        s.testPattern = on (testPattern);
        return s;
    }
}
