#include "ChordismEditor.h"
#include "../plugin/ChordismProcessor.h"

namespace chordism
{
    namespace
    {
        const juce::Identifier pageId ("page"), keyboardId ("keyboard"), widthId ("width"), heightId ("height");

        void title (juce::Label& l, const juce::String& text, juce::Colour c)
        {
            l.setText (text, juce::dontSendNotification);
            l.setFont (ui::font (13.0f, true));
            l.setColour (juce::Label::textColourId, c);
        }
    }

    ChordismEditor::ChordismEditor (ChordismProcessor& p)
        : AudioProcessorEditor (p), processor (p), bindings (p.state),
          topBar (bindings, [this] { showSetup (true); }, [this] (bool shown) { showKeyboard (shown); }),
          chordKeys (p, bindings), chordFx (bindings), chordEditorPanel (p),
          inversions (bindings, params::inversions), noteRepeat (bindings, params::noteRepeat),
          smartOctave (bindings, params::smartOctave, "Octave"), muteSmart (bindings, params::muteScale, "Mute"),
          smartKeys (p, bindings), smartPanel (bindings), outputKeys (p),
          setup (p, bindings, [this] { showSetup (false); })
    {
        setLookAndFeel (&lookAndFeel);

        title (chordTitle, "CHORD GENERATOR", ui::colours::chord);
        title (smartTitle, "SMART SCALE", ui::colours::smart);
        for (auto* c : std::initializer_list<juce::Component*> { &topBar, &chordTitle, &smartTitle, &fxPage, &editorPage, &chordKeys, &chordFx,
                                                                 &chordEditorPanel, &inversions, &noteRepeat, &smartOctave, &muteSmart,
                                                                 &smartKeys, &smartPanel, &outputKeys })
            addAndMakeVisible (c);
        addChildComponent (setup);
        for (auto* c : { &inversions, &noteRepeat, &smartOctave, &muteSmart })
            c->setAccent (ui::colours::smart);
        smartOctave.setCaptionLeft (44);

        fxPage.setRadioGroupId (1);
        editorPage.setRadioGroupId (1);
        fxPage.setClickingTogglesState (true);
        editorPage.setClickingTogglesState (true);
        fxPage.onClick = [this] { showEditorPage (false); };
        editorPage.onClick = [this] { showEditorPage (true); };
        editorPage.setTooltip ("Edit the seven chords step by step");

        auto& ui = processor.uiState;
        showEditorPage (ui.getProperty (pageId, "fx").toString() == "editor");
        showKeyboard (ui.getProperty (keyboardId, true));

        setResizable (true, true);
        setResizeLimits (960, 600, 1800, 1100);
        setSize (ui.getProperty (widthId, defaultWidth), ui.getProperty (heightId, defaultHeight));
        timerCallback();
        startTimerHz (30);
    }

    ChordismEditor::~ChordismEditor()
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void ChordismEditor::showEditorPage (bool on)
    {
        chordFx.setVisible (! on);
        chordEditorPanel.setVisible (on);
        fxPage.setToggleState (! on, juce::dontSendNotification);
        editorPage.setToggleState (on, juce::dontSendNotification);
        processor.uiState.setProperty (pageId, on ? "editor" : "fx", nullptr);
    }

    void ChordismEditor::showSetup (bool shown)
    {
        setup.setVisible (shown);
        if (shown)
        {
            setup.refresh();
            setup.toFront (false);
        }
    }

    void ChordismEditor::showKeyboard (bool shown)
    {
        outputKeys.setVisible (shown);
        topBar.setKeyboardShown (shown);
        processor.uiState.setProperty (keyboardId, shown, nullptr);
        resized();
    }

    void ChordismEditor::paint (juce::Graphics& g)
    {
        g.fillAll (ui::colours::background);
        for (auto area : { leftArea, rightArea })
        {
            g.setColour (ui::colours::panel);
            g.fillRoundedRectangle (area.toFloat(), 6.0f);
        }
    }

    void ChordismEditor::resized()
    {
        auto r = getLocalBounds();
        topBar.setBounds (r.removeFromTop (48));
        setup.setBounds (r);
        if (outputKeys.isVisible())
            outputKeys.setBounds (r.removeFromBottom (64).reduced (8, 4));

        r.reduce (8, 8);
        leftArea = r.removeFromLeft ((r.getWidth() - 8) / 2);
        r.removeFromLeft (8);
        rightArea = r;

        const int keyboardHeight = juce::jlimit (120, 200, getHeight() / 4 + 8);

        // Chord Generator.
        auto a = leftArea.reduced (8, 6);
        auto head = a.removeFromTop (26);
        chordTitle.setBounds (head.removeFromLeft (150));
        editorPage.setBounds (head.removeFromRight (104).reduced (2, 1));
        fxPage.setBounds (head.removeFromRight (48).reduced (2, 1));
        chordKeys.setBounds (a.removeFromTop (keyboardHeight).reduced (0, 4));
        a.removeFromTop (6);
        chordFx.setBounds (a);
        chordEditorPanel.setBounds (a);

        // Smart Scale.
        auto b = rightArea.reduced (8, 6);
        head = b.removeFromTop (26);
        smartTitle.setBounds (head.removeFromLeft (110));
        muteSmart.setBounds (head.removeFromRight (56).reduced (2, 1));
        smartOctave.setBounds (head.removeFromRight (110).reduced (2, 1));
        noteRepeat.setBounds (head.removeFromRight (96).reduced (2, 1));
        inversions.setBounds (head.removeFromRight (90).reduced (2, 1));
        smartKeys.setBounds (b.removeFromTop (keyboardHeight).reduced (0, 4));
        b.removeFromTop (6);
        smartPanel.setBounds (b);

        processor.uiState.setProperty (widthId, getWidth(), nullptr);
        processor.uiState.setProperty (heightId, getHeight(), nullptr);
    }

    void ChordismEditor::timerCallback()
    {
        ui::UiModel m;
        m.display = processor.readDisplay();
        m.settings = processor.currentSettings();
        m.edits = processor.chordEditor.getEdits();

        chordKeys.refresh (m);
        smartKeys.refresh (m);
        outputKeys.refresh (m);
        chordFx.refresh (m);
        smartPanel.refresh (m);
        if (chordEditorPanel.isVisible())
            chordEditorPanel.refresh (m);
        if (setup.isVisible())
            setup.refresh();
    }
}
