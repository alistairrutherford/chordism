#include "Panels.h"
#include "Theme.h"

namespace chordism::ui
{
    namespace
    {
        void caption (juce::Label& l, const juce::String& text, juce::Colour c = colours::dim)
        {
            l.setText (text, juce::dontSendNotification);
            l.setFont (font (11.5f, true));
            l.setColour (juce::Label::textColourId, c);
        }
    }

    //==========================================================================
    ParamControl& ParamGrid::add (const juce::String& paramId, const juce::String& label)
    {
        auto* c = owned.add (new ParamControl (bindings, paramId, label));
        c->setAccent (colour);
        addAndMakeVisible (c);
        cells.add (c);
        return *c;
    }

    void ParamGrid::addSection (const juce::String& title)
    {
        while (cells.size() % cols != 0)
            cells.add (nullptr);
        auto* l = sections.add (new juce::Label());
        caption (*l, title.toUpperCase(), colours::faint);
        addAndMakeVisible (l);
        cells.add (l);
        for (int i = 1; i < cols; ++i)
            cells.add (nullptr);
    }

    int ParamGrid::rows() const noexcept { return (cells.size() + cols - 1) / cols; }

    void ParamGrid::resized()
    {
        const float w = (float) getWidth() / (float) cols;
        int y = 0;
        for (int row = 0; row < rows(); ++row)
        {
            auto* first = cells[row * cols];
            const bool section = dynamic_cast<juce::Label*> (first) != nullptr;
            const int h = section ? sectionHeight : rowHeight;
            if (section)
                first->setBounds (0, y, getWidth(), h);
            else
                for (int c = 0; c < cols; ++c)
                    if (auto* cell = cells[row * cols + c])
                        cell->setBounds (juce::Rectangle<float> (w * (float) c, (float) y, w, (float) h).reduced (4.0f, 2.0f).toNearestInt());
            y += h;
        }
    }

    //==========================================================================
    TopBar::TopBar (ChordismProcessor& p, Bindings& b, std::function<void()> openSetup, std::function<void (bool)> showKeyboard,
                    std::function<void (const juce::File&)> importMidi)
        : presets (p, std::move (importMidi)), exportHandle (p),
          enable (b, params::enable, "On"), key (b, params::key), scale (b, params::scale), progression (b, params::progression),
          group (b, params::chordGroup), mapping (b, params::mapping), shifterMode (b, params::shifterMode, "Shifters")
    {
        logo.setTooltip ("Setup: split, custom progression, shifter targets, test pattern, preferences");
        logo.onClick = std::move (openSetup);
        logo.setColour (juce::TextButton::buttonColourId, colours::background);
        logo.setColour (juce::TextButton::textColourOffId, colours::text);
        addAndMakeVisible (logo);
        keyboardButton.setClickingTogglesState (true);
        keyboardButton.setTooltip ("Show or hide the keyboard at the bottom");
        keyboardButton.onClick = [this, showKeyboard] { showKeyboard (keyboardButton.getToggleState()); };
        addAndMakeVisible (keyboardButton);
        for (auto* c : { &enable, &key, &scale, &progression, &group, &mapping, &shifterMode })
            addAndMakeVisible (c);
        mapping.setAccent (colours::smart);
        addAndMakeVisible (presets);
        addAndMakeVisible (exportHandle);
    }

    void TopBar::setKeyboardShown (bool shown) { keyboardButton.setToggleState (shown, juce::dontSendNotification); }

    void TopBar::refresh()
    {
        presets.refresh();
        exportHandle.refresh();
    }

    void TopBar::paint (juce::Graphics& g)
    {
        g.setColour (colours::panel);
        g.fillRect (getLocalBounds());
        g.setColour (colours::outline);
        g.fillRect (getLocalBounds().removeFromBottom (1));
    }

    void TopBar::resized()
    {
        auto r = getLocalBounds().reduced (8, 4);
        logo.setBounds (r.removeFromLeft (84).withSizeKeepingCentre (84, 30));
        logo.setButtonText ("Chordism");
        r.removeFromLeft (6);
        enable.setBounds (r.removeFromLeft (40).withTrimmedTop (14));
        r.removeFromLeft (4);
        presets.setBounds (r.removeFromLeft (juce::jlimit (170, 250, r.getWidth() / 4)));
        r.removeFromLeft (4);
        keyboardButton.setBounds (r.removeFromRight (52).withTrimmedTop (14).withTrimmedBottom (0));
        r.removeFromRight (6);
        exportHandle.setBounds (r.removeFromRight (78));
        r.removeFromRight (4);
        const struct { ParamControl* c; int w; } cols[] = { { &key, 66 }, { &scale, 118 }, { &progression, 150 }, { &group, 118 },
                                                            { &mapping, 104 }, { &shifterMode, 90 } };
        int total = 0;
        for (const auto& c : cols)
            total += c.w;
        const float k = juce::jmin (1.4f, (float) r.getWidth() / (float) total);
        for (const auto& c : cols)
            c.c->setBounds (r.removeFromLeft ((int) ((float) c.w * k)).reduced (3, 0));
    }

    //==========================================================================
    ChordFxPanel::ChordFxPanel (Bindings& b) : grid (b, 5, colours::chord), steps (b, colours::chord)
    {
        grid.add (params::stack);
        grid.add (params::chordOctave, "Octave");
        grid.add (params::hold);
        grid.add (params::velocity);
        grid.add (params::muteChords, "Mute chords");
        grid.add (params::strum);
        grid.add (params::strumRate, "Strum rate");
        grid.add (params::strumMs, "Strum time (stopped)");
        grid.add (params::velocityPeak, "Velocity peak");
        grid.add (params::velocityRange, "Velocity range");
        grid.add (params::arp);
        grid.add (params::arpRate, "Arp rate");
        grid.add (params::arpRange, "Arp range");
        grid.add (params::arpGate, "Arp gate");
        grid.addGap();
        grid.add (params::repeat);
        grid.add (params::repeatRate, "Repeat rate");
        grid.add (params::repeatGate, "Repeat gate");
        grid.addGap();
        grid.addGap();
        grid.add (params::seq, "Step seq");
        grid.add (params::seqPattern, "Pattern");
        grid.add (params::seqSwing, "Swing");
        grid.add (params::seqGate, "Gate");
        grid.add (params::seqSustain, "Sustain");
        addAndMakeVisible (grid);
        caption (stepsCaption, "STEPS");
        addAndMakeVisible (stepsCaption);
        addAndMakeVisible (steps);
        steps.bind (params::seqSteps (0));
    }

    void ChordFxPanel::refresh (const UiModel& m) { steps.bind (params::seqSteps (m.settings.fx.seq.pattern)); }

    void ChordFxPanel::resized()
    {
        auto r = getLocalBounds();
        grid.setBounds (r.removeFromTop (grid.rows() * ParamGrid::rowHeight));
        auto s = r.removeFromTop (30).reduced (4, 2);
        stepsCaption.setBounds (s.removeFromLeft (52));
        steps.setBounds (s);
    }

    //==========================================================================
    SmartPanel::SmartPanel (Bindings& b)
        : bindings (b), arp (b, params::patternArp (0), "Pattern"), rate (b, params::patternRate (0), "Rate"),
          range (b, params::patternRange (0), "Range"), chord (b, params::patternChord (0), "Arp chord"),
          gate (b, params::patternGate (0), "Gate"), notes (b, params::patternNotes (0), "Notes/bar"),
          rhythm (b, params::patternRhythm (0), "Rhythm"), swing (b, params::melodySwing, "Melody swing"),
          seed (b, params::melodySeed, "Seed"), fx (b, 5, colours::smart), steps (b, colours::smart)
    {
        caption (patternCaption, "PATTERN", colours::faint);
        addAndMakeVisible (patternCaption);
        for (int s = 0; s < numPatternSlots; ++s)
        {
            auto* button = slotButtons.add (new juce::TextButton (juce::String (s + 1)));
            button->setColour (juce::TextButton::buttonOnColourId, colours::smart);
            button->setTooltip ("Edit pattern slot " + juce::String (s + 1) + " (the black key " + juce::String ("C# D# F# G# A#").substring (s * 3, s * 3 + 2)
                                + "). Note Repeat uses this slot's rate and gate.");
            button->onClick = [this, s] { bindings.set (params::patternSlot, (float) s); };
            addAndMakeVisible (button);
        }
        bindings.bound (params::patternSlot);
        for (auto* c : { &arp, &rate, &range, &chord, &gate, &notes, &rhythm, &swing, &seed })
        {
            c->setAccent (colours::smart);
            addAndMakeVisible (c);
        }
        notes.setVisible (false);
        rhythm.setVisible (false);
        arp.setTooltip ("The slot's arp pattern, or Melody: a seeded melody over the current chord");
        swing.setTooltip ("Melody mode: pushes the even sixteenths later (all slots)");
        seed.setTooltip ("Melody mode: the same seed always plays the same melody at the same place (all slots; each slot varies it)");

        fx.addSection ("Smart Scale effects");
        fx.add (params::smartStack, "Stack");
        fx.add (params::smartHold, "Hold");
        fx.add (params::smartVelocity, "Velocity");
        fx.add (params::smartVelocityPeak, "Velocity peak");
        fx.add (params::smartVelocityRange, "Velocity range");
        fx.add (params::smartSeq, "Step seq");
        fx.add (params::smartSeqPattern, "Pattern");
        fx.add (params::smartSeqSwing, "Swing");
        fx.add (params::smartSeqGate, "Gate");
        fx.add (params::smartSeqSustain, "Sustain");
        addAndMakeVisible (fx);
        caption (stepsCaption, "STEPS");
        addAndMakeVisible (stepsCaption);
        addAndMakeVisible (steps);
        steps.bind (params::smartSeqSteps (0));
        bindSlot (0);
    }

    void SmartPanel::bindSlot (int slot)
    {
        if (slot == slotShown)
            return;
        slotShown = slot;
        arp.bind (params::patternArp (slot), "Pattern");
        rate.bind (params::patternRate (slot), "Rate");
        range.bind (params::patternRange (slot), "Range");
        chord.bind (params::patternChord (slot), melodyShown ? "Chord tones" : "Arp chord");
        gate.bind (params::patternGate (slot), "Gate");
        notes.bind (params::patternNotes (slot), "Notes/bar");
        rhythm.bind (params::patternRhythm (slot), "Rhythm");
        for (int s = 0; s < numPatternSlots; ++s)
            slotButtons[s]->setToggleState (s == slot, juce::dontSendNotification);
    }

    void SmartPanel::refresh (const UiModel& m)
    {
        bindSlot (m.settings.smart.patternSlot);
        const bool melody = m.settings.smart.patterns[(size_t) juce::jlimit (0, numPatternSlots - 1, slotShown)].melody;
        if (melody != melodyShown)
        {
            melodyShown = melody;
            rate.setVisible (! melody);
            notes.setVisible (melody);
            rhythm.setVisible (melody);
            chord.bind (chord.getParamId(), melody ? "Chord tones" : "Arp chord");
            resized();
        }
        steps.bind (params::smartSeqSteps (m.settings.smart.seq.pattern));
    }

    void SmartPanel::paint (juce::Graphics& g)
    {
        g.setColour (colours::outline);
        g.fillRect (0, ParamGrid::sectionHeight + 2 * ParamGrid::rowHeight + 2, getWidth(), 1);
    }

    void SmartPanel::resized()
    {
        auto r = getLocalBounds();
        auto head = r.removeFromTop (ParamGrid::sectionHeight);
        patternCaption.setBounds (head.removeFromLeft (70));
        auto top = r.removeFromTop (ParamGrid::rowHeight);
        const int globalsWidth = top.getWidth() * 2 / 5;
        auto globals = top.removeFromRight (globalsWidth);
        swing.setBounds (globals.removeFromLeft (globalsWidth / 2).reduced (4, 2));
        seed.setBounds (globals.reduced (4, 2));
        auto buttons = top.reduced (4, 8);
        const int bw = buttons.getWidth() / numPatternSlots;
        for (int s = 0; s < numPatternSlots; ++s)
            slotButtons[s]->setBounds (buttons.removeFromLeft (bw).reduced (3, 0));
        auto row = r.removeFromTop (ParamGrid::rowHeight);
        if (melodyShown)
        {
            const int w = row.getWidth() / 6;
            for (auto* c : { &arp, &notes, &rhythm, &range, &chord, &gate })
                c->setBounds (row.removeFromLeft (w).reduced (4, 2));
        }
        else
        {
            const int w = row.getWidth() / 5;
            for (auto* c : { &arp, &rate, &range, &chord, &gate })
                c->setBounds (row.removeFromLeft (w).reduced (4, 2));
        }
        r.removeFromTop (6);
        fx.setBounds (r.removeFromTop (ParamGrid::sectionHeight + 2 * ParamGrid::rowHeight));
        auto s = r.removeFromTop (30).reduced (4, 2);
        stepsCaption.setBounds (s.removeFromLeft (52));
        steps.setBounds (s);
    }

    //==========================================================================
    SetupPanel::SetupPanel (ChordismProcessor& p, Bindings& b, std::function<void()> close)
        : processor (p), left (b, 2, colours::chord), shifters (b, 4, colours::chord), right (b, 2, colours::chord)
    {
        heading.setText ("Setup", juce::dontSendNotification);
        heading.setFont (font (18.0f, true));
        heading.setColour (juce::Label::textColourId, colours::text);
        addAndMakeVisible (heading);
        closeButton.onClick = std::move (close);
        addAndMakeVisible (closeButton);

        left.addSection ("Keyboard");
        left.add (params::split);
        left.addGap();
        left.addSection ("Custom progression (degree per white key)");
        for (int n = 0; n < numChordSlots; ++n)
            left.add (params::customDegree (n), "Key " + juce::String::charToString ("CDEFGAB"[n]));
        addAndMakeVisible (left);

        shifters.addSection ("Shifters (black keys of the chord zone)");
        for (int n = 0; n < numShifters; ++n)
        {
            const juce::String s = "Shifter " + juce::String (n + 1) + " ";
            shifters.add (params::shifterKind (n), s + "type");
            shifters.add (params::shifterGroup (n), "group");
            shifters.add (params::shifterKey (n), "key");
            shifters.add (params::shifterScale (n), "scale");
        }
        addAndMakeVisible (shifters);

        right.addSection ("Test pattern (LiveBridge)");
        right.add (params::testPattern);
        right.add (params::testShifter);
        right.add (params::testSmart);
        addAndMakeVisible (right);

        caption (prefsCaption, "PREFERENCES", colours::faint);
        addAndMakeVisible (prefsCaption);
        caption (qwertyCaption, "QWERTY keys");
        addAndMakeVisible (qwertyCaption);
        qwerty.addItem ("Off", 1);
        qwerty.addItem ("Piano: A row (Z / X octave)", 2);
        qwerty.addItem ("Two rows: Z row and Q row", 3);
        qwerty.setSelectedId (juce::jlimit (1, 3, (int) processor.uiState.getProperty ("qwerty", 2)), juce::dontSendNotification);
        qwerty.onChange = [this] { processor.uiState.setProperty ("qwerty", qwerty.getSelectedId(), nullptr); };
        qwerty.setTooltip ("Play Chordism from the computer keyboard while its window has the focus");
        addAndMakeVisible (qwerty);
    }

    void SetupPanel::paint (juce::Graphics& g)
    {
        g.setColour (colours::background);
        g.fillRect (getLocalBounds());
    }

    void SetupPanel::resized()
    {
        auto r = getLocalBounds().reduced (16, 10);
        auto top = r.removeFromTop (32);
        closeButton.setBounds (top.removeFromRight (80).reduced (0, 3));
        heading.setBounds (top);
        const int w = r.getWidth() / 3;
        auto a = r.removeFromLeft (w - 8);
        r.removeFromLeft (8);
        auto c = r.removeFromRight (w - 8);
        r.removeFromRight (8);
        left.setBounds (a);
        shifters.setBounds (r);
        right.setBounds (c.removeFromTop (ParamGrid::sectionHeight + 2 * ParamGrid::rowHeight));
        prefsCaption.setBounds (c.removeFromTop (ParamGrid::sectionHeight).reduced (4, 0));
        auto q = c.removeFromTop (ParamGrid::rowHeight).reduced (4, 2);
        qwertyCaption.setBounds (q.removeFromTop (14));
        qwerty.setBounds (q);
    }
}
