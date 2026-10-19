#include "ChordEditorPanel.h"
#include "Theme.h"
#include "../core/theory/ChordNamer.h"
#include "../core/theory/RomanNumeral.h"

namespace chordism::ui
{
    namespace
    {
        constexpr float headerHeight = 34.0f, footerHeight = 26.0f;
        const char* stepNames[numChordSteps] = { "R", "3", "5", "7", "9", "11", "13" };
    }

    ChordEditorPanel::ChordEditorPanel (ChordismProcessor& p) : processor (p)
    {
        title.setFont (font (12.0f));
        title.setColour (juce::Label::textColourId, colours::dim);
        addAndMakeVisible (title);
        resetAll.onClick = [this] { processor.chordEditor.resetAll (model.settings.group); };
        addAndMakeVisible (resetAll);
        for (int s = 0; s < numChordSlots; ++s)
        {
            auto* b = resets.add (new juce::TextButton ("Reset"));
            b->onClick = [this, s] { processor.chordEditor.resetDegree (model.display.chords[(size_t) s].degree); };
            addAndMakeVisible (b);
        }
        setTooltip ("Click a step to turn it on or off; the flat and sharp corners (or the mouse wheel) change its accidental; "
                    "the dots stack octaves.");
    }

    void ChordEditorPanel::refresh (const UiModel& m)
    {
        const bool changed = ! (m.display == model.display) || ! m.edits.sameEdits (model.edits) || m.settings.group != model.settings.group
                          || m.settings.key != model.settings.key || m.settings.scale != model.settings.scale;
        model = m;
        if (! changed)
            return;
        title.setText ("Edits for " + juce::String (chordGroupInfo (m.settings.group).name) + " (choosing another group resets them)",
                       juce::dontSendNotification);
        for (int s = 0; s < numChordSlots; ++s)
            resets[s]->setEnabled (m.edits.group == m.settings.group && m.edits.degrees[(size_t) m.display.chords[(size_t) s].degree].edited);
        resetAll.setEnabled (m.edits.any());
        repaint();
    }

    void ChordEditorPanel::resized()
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (24);
        resetAll.setBounds (top.removeFromRight (84).reduced (0, 2));
        title.setBounds (top);
        grid = r.toFloat();
        const float w = grid.getWidth() / (float) numChordSlots;
        for (int s = 0; s < numChordSlots; ++s)
            resets[s]->setBounds (juce::Rectangle<float> (grid.getX() + w * (float) s, grid.getBottom() - footerHeight, w, footerHeight)
                                      .reduced (6.0f, 3.0f).toNearestInt());
    }

    ChordSlot ChordEditorPanel::slotSteps (int slot) const
    {
        const int degree = model.display.chords[(size_t) slot].degree;
        return editor::slotFor (model.edits, model.settings.group, model.settings.scale, degree);
    }

    juce::Rectangle<float> ChordEditorPanel::cellBounds (int slot, int step) const
    {
        const float w = grid.getWidth() / (float) numChordSlots;
        const float h = (grid.getHeight() - headerHeight - footerHeight) / (float) numChordSteps;
        const int row = numChordSteps - 1 - step;   // the 13th on top
        return { grid.getX() + w * (float) slot, grid.getY() + headerHeight + h * (float) row, w, h };
    }

    void ChordEditorPanel::paint (juce::Graphics& g)
    {
        const ChordContext ctx { model.settings.key, model.settings.scale, 0 };
        const Spelling sp = makeSpelling (ctx.key, ctx.scale);
        const float w = grid.getWidth() / (float) numChordSlots;

        for (int s = 0; s < numChordSlots; ++s)
        {
            const ChordSlot slot = slotSteps (s);
            auto head = juce::Rectangle<float> (grid.getX() + w * (float) s, grid.getY(), w, headerHeight).reduced (2.0f, 1.0f);
            g.setColour (s == model.display.lastSlot ? colours::chord : colours::text);
            g.setFont (font (14.0f, true));
            g.drawText (juce::String (romanNumeral (slot, ctx.scale)), head.removeFromTop (17.0f), juce::Justification::centred);
            g.setFont (font (11.5f));
            g.setColour (colours::dim);
            g.drawFittedText (juce::String (nameSlot (slot, ctx).text()), head.toNearestInt(), juce::Justification::centred, 1, 0.6f);

            for (int k = 0; k < numChordSteps; ++k)
            {
                const Step& st = slot.steps[(size_t) k];
                auto cell = cellBounds (s, k).reduced (2.5f, 2.0f);
                g.setColour (st.on ? colours::chord.withAlpha (0.9f) : colours::raised);
                g.fillRoundedRectangle (cell, 4.0f);
                if (st.accidental != 0)
                {
                    g.setColour (st.on ? colours::background : colours::chord);
                    g.drawRoundedRectangle (cell.reduced (0.5f), 4.0f, 1.0f);
                }

                auto body = cell;
                auto dots = body.removeFromBottom (juce::jmin (9.0f, body.getHeight() * 0.3f));
                const juce::Colour ink = st.on ? colours::background : colours::dim;

                // Corners: flat (left), sharp (right).
                g.setFont (font (11.0f));
                g.setColour (ink.withAlpha (0.55f));
                g.drawText (juce::String::fromUTF8 ("♭"), body.withWidth (12.0f).translated (1.0f, 0.0f), juce::Justification::centredLeft);
                g.drawText (juce::String::fromUTF8 ("♯"), body.withTrimmedLeft (body.getWidth() - 13.0f), juce::Justification::centredLeft);

                const int pc = (((ctx.key + scaleOffset (ctx.scale, slot.degree + 2 * k) + st.accidental) % 12) + 12) % 12;
                g.setColour (ink);
                g.setFont (font (12.5f, st.on));
                auto mid = body.reduced (12.0f, 0.0f);
                g.drawText (juce::String (sp.name (pc)), mid.removeFromTop (mid.getHeight() * 0.6f), juce::Justification::centredBottom);
                g.setFont (font (9.5f));
                g.setColour (ink.withAlpha (0.7f));
                g.drawText (stepNames[k], mid, juce::Justification::centredTop);

                // Octave dots: -2 … +2, the chord's own octave in the middle.
                const float dw = juce::jmin (9.0f, (dots.getWidth() - 12.0f) / 5.0f);
                const float x0 = dots.getCentreX() - dw * 2.5f;
                for (int o = minStepOctave; o <= maxStepOctave; ++o)
                {
                    auto dot = juce::Rectangle<float> (x0 + dw * (float) (o + 2), dots.getY(), dw, dots.getHeight()).reduced (1.5f, 1.0f);
                    const bool lit = (st.octaveMask & octaveBit (o)) != 0;
                    g.setColour (lit ? ink : ink.withAlpha (0.25f));
                    if (o == 0)
                        g.fillRect (dot);
                    else
                        g.fillEllipse (dot.withSizeKeepingCentre (juce::jmin (dot.getWidth(), dot.getHeight()), juce::jmin (dot.getWidth(), dot.getHeight())));
                }
            }
        }
    }

    ChordEditorPanel::Hit ChordEditorPanel::hitTest (juce::Point<float> p) const
    {
        Hit h;
        for (int s = 0; s < numChordSlots; ++s)
            for (int k = 0; k < numChordSteps; ++k)
            {
                const auto cell = cellBounds (s, k).reduced (2.5f, 2.0f);
                if (! cell.contains (p))
                    continue;
                h.slot = s;
                h.step = k;
                auto body = cell;
                auto dots = body.removeFromBottom (juce::jmin (9.0f, body.getHeight() * 0.3f));
                if (dots.expanded (0.0f, 2.0f).contains (p))
                {
                    const float dw = juce::jmin (9.0f, (dots.getWidth() - 12.0f) / 5.0f);
                    const float x0 = dots.getCentreX() - dw * 2.5f;
                    const int o = (int) std::floor ((p.x - x0) / dw) - 2;
                    if (o >= minStepOctave && o <= maxStepOctave)
                    {
                        h.part = Hit::Octave;
                        h.octave = o;
                        return h;
                    }
                }
                if (p.x < cell.getX() + 13.0f)
                    h.part = Hit::Flat;
                else if (p.x > cell.getRight() - 13.0f)
                    h.part = Hit::Sharp;
                else
                    h.part = Hit::Toggle;
                return h;
            }
        return h;
    }

    void ChordEditorPanel::changeAccidental (int slot, int step, int delta)
    {
        const ChordSlot s = slotSteps (slot);
        const int acc = juce::jlimit (-2, 2, (int) s.steps[(size_t) step].accidental + delta);
        processor.chordEditor.setAccidental (model.settings.group, model.settings.scale, s.degree, step, acc);
    }

    void ChordEditorPanel::mouseDown (const juce::MouseEvent& e)
    {
        const Hit h = hitTest (e.position);
        if (h.slot < 0)
            return;
        const ChordSlot s = slotSteps (h.slot);
        const Step& st = s.steps[(size_t) h.step];
        auto& ed = processor.chordEditor;
        switch (h.part)
        {
            case Hit::Toggle: ed.setOn (model.settings.group, model.settings.scale, s.degree, h.step, ! st.on); break;
            case Hit::Flat:   changeAccidental (h.slot, h.step, -1); break;
            case Hit::Sharp:  changeAccidental (h.slot, h.step, +1); break;
            case Hit::Octave:
                ed.setOctave (model.settings.group, model.settings.scale, s.degree, h.step, h.octave, (st.octaveMask & octaveBit (h.octave)) == 0);
                break;
            case Hit::None: default: break;
        }
    }

    void ChordEditorPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        const Hit h = hitTest (e.position);
        if (h.slot >= 0 && std::abs (w.deltaY) > 0.01f)
            changeAccidental (h.slot, h.step, w.deltaY > 0 ? 1 : -1);
    }
}
