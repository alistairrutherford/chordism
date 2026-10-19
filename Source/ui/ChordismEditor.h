// Interim editor: the generic parameter list plus status lines (port; the seven chords with
// their Roman numerals and names, the last one played in brackets; the active shifter).
// The real UI is Phase 5 (PLAN.md §6).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace chordism
{
    class ChordismProcessor;

    class ChordismEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
    {
    public:
        explicit ChordismEditor (ChordismProcessor&);
        ~ChordismEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;

        ChordismProcessor& processor;
        juce::GenericAudioProcessorEditor generic;
        juce::Label status;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordismEditor)
    };
}
