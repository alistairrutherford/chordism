#include "ChordismEditor.h"
#include "../plugin/ChordismProcessor.h"

namespace chordism
{
    namespace
    {
        const juce::Identifier pageId ("page"), keyboardId ("keyboard"), widthId ("width"), heightId ("height");
        const juce::Identifier qwertyId ("qwerty"), qwertyOctaveId ("qwertyOctave");

        void title (juce::Label& l, const juce::String& text, juce::Colour c)
        {
            l.setText (text, juce::dontSendNotification);
            l.setFont (ui::font (13.0f, true));
            l.setColour (juce::Label::textColourId, c);
        }
    }

    ChordismEditor::ChordismEditor (ChordismProcessor& p)
        : AudioProcessorEditor (p), processor (p), bindings (p.state),
          topBar (p, bindings, [this] { showSetup (true); }, [this] (bool shown) { showKeyboard (shown); },
                  [this] (const juce::File& f) { importMidi (f); }),
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
        status.setFont (ui::font (12.0f));
        status.setJustificationType (juce::Justification::centredLeft);
        status.setMinimumHorizontalScale (0.6f);
        status.setColour (juce::Label::backgroundColourId, ui::colours::background.withAlpha (0.92f));
        addChildComponent (status);
        setWantsKeyboardFocus (true);
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
        releaseQwerty();
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

    void ChordismEditor::paintOverChildren (juce::Graphics& g)
    {
        if (! dropHover)
            return;
        g.setColour (ui::colours::chord.withAlpha (0.12f));
        g.fillRect (getLocalBounds());
        g.setColour (ui::colours::chord);
        g.drawRect (getLocalBounds(), 3);
        g.setFont (ui::font (18.0f, true));
        g.drawText ("Drop a MIDI file to import its chords", getLocalBounds(), juce::Justification::centred);
    }

    //==========================================================================
    bool ChordismEditor::isInterestedInFileDrag (const juce::StringArray& files)
    {
        for (const auto& f : files)
            if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
                return true;
        return false;
    }

    void ChordismEditor::filesDropped (const juce::StringArray& files, int, int)
    {
        dropHover = false;
        repaint();
        for (const auto& f : files)
            if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
            {
                importMidi (juce::File (f));
                return;
            }
    }

    void ChordismEditor::importMidi (const juce::File& file)
    {
        const auto result = processor.importMidiFile (file);
        showStatus (result.message, ! result.ok);
        if (result.ok)
        {
            showEditorPage (chordEditorPanel.isVisible());
            chordKeys.flash (result.count);
        }
    }

    void ChordismEditor::showStatus (const juce::String& text, bool error)
    {
        statusText = text;
        status.setText (text, juce::dontSendNotification);
        status.setTooltip (text);
        status.setColour (juce::Label::textColourId, error ? ui::colours::sounding : ui::colours::chord);
        status.setVisible (text.isNotEmpty());
        status.toFront (false);
        statusUntil = juce::Time::getMillisecondCounterHiRes() + 8000.0;
    }

    //==========================================================================
    int ChordismEditor::qwertyNote (Qwerty layout, juce::juce_wchar key, int split, int octave)
    {
        key = juce::CharacterFunctions::toLowerCase (key);
        const auto indexIn = [key] (const char* row) { for (int i = 0; row[i] != 0; ++i) if ((juce::juce_wchar) row[i] == key) return i; return -1; };
        const int below = split - 12 * (split >= 12 ? 1 : 0);
        int note = -1;
        if (layout == Qwerty::Piano)
        {
            if (const int i = indexIn ("awsedftgyhujkolp;'"); i >= 0)
                note = below + 12 * octave + i;
        }
        else if (layout == Qwerty::TwoRows)
        {
            if (const int i = indexIn ("zsxdcvgbhnjm,l.;/"); i >= 0)
                note = below + 12 * octave + i;
            else if (const int j = indexIn ("q2w3er5t6y7ui9o0p"); j >= 0)
                note = split + 12 * octave + j;
        }
        // From the C at or below: the rows start on a C.
        if (note >= 0)
            note -= ((split % 12) + 12) % 12;
        return note >= 0 && note < 128 ? note : -1;
    }

    bool ChordismEditor::keyPressed (const juce::KeyPress& key)
    {
        const auto layout = (Qwerty) (int) processor.uiState.getProperty (qwertyId, (int) Qwerty::Piano);
        if (layout == Qwerty::Off || key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown())
            return false;
        const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter() != 0 ? key.getTextCharacter() : (juce::juce_wchar) key.getKeyCode());
        int octave = processor.uiState.getProperty (qwertyOctaveId, 0);
        if (layout == Qwerty::Piano && (c == 'z' || c == 'x'))
        {
            releaseQwerty();
            octave = juce::jlimit (-3, 3, octave + (c == 'x' ? 1 : -1));
            processor.uiState.setProperty (qwertyOctaveId, octave, nullptr);
            showStatus ("QWERTY octave " + juce::String (octave > 0 ? "+" : "") + juce::String (octave), false);
            return true;
        }
        const int note = qwertyNote (layout, c, bindings.state.getRawParameterValue (params::split) != nullptr
                                                    ? (int) bindings.value (params::split) : 60, octave);
        if (note < 0)
            return false;
        if (qwertyHeld.count (c) == 0)
        {
            qwertyHeld[c] = note;
            processor.pushUiNote (note, 100, true);
        }
        return true;
    }

    bool ChordismEditor::keyStateChanged (bool)
    {
        bool any = false;
        for (auto it = qwertyHeld.begin(); it != qwertyHeld.end();)
        {
            if (juce::KeyPress::isKeyCurrentlyDown ((int) it->first))
            {
                ++it;
                continue;
            }
            processor.pushUiNote (it->second, 0, false);
            it = qwertyHeld.erase (it);
            any = true;
        }
        return any;
    }

    void ChordismEditor::releaseQwerty()
    {
        for (const auto& [key, note] : qwertyHeld)
            processor.pushUiNote (note, 0, false);
        qwertyHeld.clear();
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
        status.setBounds (a.getX(), a.getY() + 2, a.getWidth(), 20);   // a toast over the top of the chord keys
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
        if (! focusGrabbed && isShowing())
        {
            focusGrabbed = true;
            grabKeyboardFocus();          // so the computer keyboard plays at once
        }
        if (status.isVisible() && juce::Time::getMillisecondCounterHiRes() > statusUntil)
            status.setVisible (false);
        topBar.refresh();

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
