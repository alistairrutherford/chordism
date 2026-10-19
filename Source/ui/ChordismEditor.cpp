#include "ChordismEditor.h"
#include "../plugin/ChordismProcessor.h"
#include "../core/theory/ChordNamer.h"
#include "../core/theory/RomanNumeral.h"

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
        setSize (640, 520);
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
        status.setBounds (r.removeFromBottom (76).reduced (8, 4));
        generic.setBounds (r);
    }

    void ChordismEditor::timerCallback()
    {
        const auto& d = processor.readDisplay();

        // The seven white keys: Roman numeral and chord name, the last one played marked.
        juce::String chords;
        for (int n = 0; n < numChordSlots; ++n)
        {
            const auto& slot = d.chords[(size_t) n];
            chords << (n ? "   " : "") << (n == d.lastSlot ? "[" : "") << juce::String (romanNumeral (slot, d.context.scale)) << " "
                   << juce::String (nameSlot (slot, d.context).text()) << (n == d.lastSlot ? "]" : "");
        }

        status.setText ("Port: " + processor.getPortStatus() + "\nChords: " + chords
                            + "\nShifter: " + (d.activeShifter >= 0 ? juce::String (d.activeShifter + 1) : juce::String ("-"))
                            + "   Sounding: " + juce::String (processor.soundingNotes.load())
                            + (processor.transportPlaying.load() ? "   (playing)" : ""),
                        juce::dontSendNotification);
    }
}
