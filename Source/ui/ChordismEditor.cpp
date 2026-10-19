#include "ChordismEditor.h"
#include "../plugin/ChordismProcessor.h"

namespace chordism
{
    ChordismEditor::ChordismEditor (ChordismProcessor& p)
        : AudioProcessorEditor (p), processor (p), generic (p)
    {
        addAndMakeVisible (generic);
        status.setJustificationType (juce::Justification::centredLeft);
        status.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (status);

        setResizable (true, false);
        setSize (520, 460);
        timerCallback();
        startTimerHz (10);
    }

    ChordismEditor::~ChordismEditor()
    {
        stopTimer();
    }

    void ChordismEditor::paint (juce::Graphics& g)
    {
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    }

    void ChordismEditor::resized()
    {
        auto r = getLocalBounds();
        status.setBounds (r.removeFromBottom (44).reduced (8, 4));
        generic.setBounds (r);
    }

    void ChordismEditor::timerCallback()
    {
        static const char* numerals[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
        const int slot = processor.lastChordSlot.load();
        status.setText ("Port: " + processor.getPortStatus() + "\nLast chord key: "
                            + (slot >= 0 && slot < 7 ? juce::String (numerals[slot]) : juce::String ("-"))
                            + "   Sounding: " + juce::String (processor.soundingNotes.load())
                            + (processor.transportPlaying.load() ? "   (playing)" : ""),
                        juce::dontSendNotification);
    }
}
