#include "Keyboards.h"
#include "Theme.h"
#include "../core/theory/ChordNamer.h"
#include "../core/theory/RomanNumeral.h"

namespace chordism::ui
{
    namespace
    {
        const char* shortGroupName (ChordGroup g)
        {
            static constexpr const char* names[numChordGroups] = {
                "Triads", "5", "Sus2", "Sus4", "6ths", "7ths", "Add9", "9ths", "11ths", "13ths",
                "Inv 1", "Inv 2", "Open", "Drop 2", "Shell", "Neo", "4ths"
            };
            const int i = (int) g;
            return names[i >= 0 && i < numChordGroups ? i : 0];
        }

        const char* shortScaleName (int s)
        {
            static constexpr const char* names[numScales] = {
                "Major", "Minor", "Harm m", "Mel m", "Dorian", "Phryg", "Lydian", "Mixo", "Locrian",
                "Harm M", "Phr Dom", "Hung m", "Dbl Hm"
            };
            return names[s >= 0 && s < numScales ? s : 0];
        }

        juce::String shifterLabel (const ShifterTarget& t)
        {
            if (t.kind == ShifterKind::ChordGroup)
                return shortGroupName (t.group);
            juce::String s;
            if (t.key >= 0)
                s << keyName (t.key) << "\n";
            s << (t.scale >= 0 ? shortScaleName (t.scale) : "Scale");
            return s;
        }

        void addTicked (juce::PopupMenu& m, int id, const juce::String& text, bool ticked)
        {
            m.addItem (id, text, true, ticked);
        }
    }

    juce::String shortArpName (int pattern)
    {
        static constexpr const char* names[numArpPatterns] = {
            "Off", "Lin ↑", "Lin ↓", "Conv ↑", "Conv ↓", "Div ↑", "Div ↓", "Stut ↑", "Stut ↓",
            "Zig ↑", "Zig ↓", "Boom ↑", "Boom ↓"
        };
        return juce::String::fromUTF8 (names[pattern >= 0 && pattern < numArpPatterns ? pattern : 0]);
    }

    //==========================================================================
    bool PianoLayout::isBlack (int note) noexcept { return keys::blackIndex (((note % 12) + 12) % 12) >= 0; }

    int PianoLayout::whiteCount() const noexcept
    {
        int n = 0;
        for (int k = lo; k <= hi; ++k)
            n += isBlack (k) ? 0 : 1;
        return juce::jmax (1, n);
    }

    juce::Rectangle<float> PianoLayout::keyBounds (int note, float blackHeight) const
    {
        const float ww = area.getWidth() / (float) whiteCount();
        int whitesBefore = 0;
        for (int k = lo; k < note; ++k)
            whitesBefore += isBlack (k) ? 0 : 1;
        if (! isBlack (note))
            return { area.getX() + ww * (float) whitesBefore, area.getY(), ww, area.getHeight() };
        const float bw = ww * 0.62f;
        return { area.getX() + ww * (float) whitesBefore - bw * 0.5f, area.getY(), bw, area.getHeight() * blackHeight };
    }

    int PianoLayout::noteAt (juce::Point<float> p, float blackHeight) const
    {
        for (int k = lo; k <= hi; ++k)
            if (isBlack (k) && keyBounds (k, blackHeight).contains (p))
                return k;
        for (int k = lo; k <= hi; ++k)
            if (! isBlack (k) && keyBounds (k, blackHeight).contains (p))
                return k;
        return -1;
    }

    //==========================================================================
    void PlayingKeys::press (int note, int velocity)
    {
        if (note == held)
            return;
        release();
        if (note < 0 || note > 127)
            return;
        processor.pushUiNote (note, velocity, true);
        held = note;
    }

    void PlayingKeys::release()
    {
        if (held >= 0)
            processor.pushUiNote (held, 0, false);
        held = -1;
    }

    //==========================================================================
    namespace
    {
        constexpr float chordBlackHeight = 0.5f, smartBlackHeight = 0.58f;

        void drawWhiteKey (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint, bool lit)
        {
            r = r.reduced (1.0f, 0.0f);
            g.setColour (lit ? colours::whiteKey.interpolatedWith (tint, 0.45f) : colours::whiteKey);
            g.fillRoundedRectangle (r, 4.0f);
        }

        void drawBlackKey (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint, bool lit, bool outlined)
        {
            g.setColour (lit ? tint.darker (0.15f) : colours::blackKey);
            g.fillRoundedRectangle (r, 3.0f);
            if (outlined)
            {
                g.setColour (tint);
                g.drawRoundedRectangle (r.reduced (1.0f), 3.0f, 2.0f);
            }
        }
    }

    ChordKeyboard::ChordKeyboard (ChordismProcessor& p, Bindings& b) : processor (p), bindings (b), playing (p)
    {
        layout.lo = 0;
        layout.hi = 11;
        setTooltip ("Chord keys: click to play. Right-click a white key for its degree, chord type or a reset; "
                    "a black key for its shifter's target.");
    }

    int ChordKeyboard::keyFor (int pc) const
    {
        const int split = model.settings.split;
        if (split < 12)
            return -1;
        return ((split - 12) / 12) * 12 + pc;
    }

    void ChordKeyboard::refresh (const UiModel& m)
    {
        std::array<bool, 12> now {};
        for (int n = 0; n < m.settings.split && n < 128; ++n)
            if (processor.isKeyDown (n) || processor.isPerforming (n))
                now[(size_t) (n % 12)] = true;

        const bool changed = now != active || ! (m.display == model.display) || m.settings.split != model.settings.split
                          || m.settings.shifters != model.settings.shifters;
        model = m;
        active = now;
        if (flashCount > 0 && juce::Time::getMillisecondCounterHiRes() - flashStart > flashStepMs * (flashCount - 1) + flashOnMs)
            flashCount = 0;
        if (changed || flashCount > 0)
            repaint();
    }

    void ChordKeyboard::flash (int count)
    {
        flashCount = juce::jlimit (0, numChordSlots, count);
        flashStart = juce::Time::getMillisecondCounterHiRes();
        repaint();
    }

    void ChordKeyboard::paint (juce::Graphics& g)
    {
        layout.area = getLocalBounds().toFloat();
        const auto& d = model.display;
        for (int pc = 0; pc < 12; ++pc)
        {
            if (PianoLayout::isBlack (pc))
                continue;
            const int slot = keys::whiteLetter (pc);
            auto r = layout.keyBounds (pc, chordBlackHeight);
            drawWhiteKey (g, r, colours::chord, active[(size_t) pc]);

            auto text = r.reduced (3.0f, 6.0f).withTrimmedTop (r.getHeight() * chordBlackHeight);
            const auto& slotData = d.chords[(size_t) slot];
            g.setColour (colours::whiteKeyText);
            g.setFont (font (19.0f, true));
            g.drawFittedText (juce::String (romanNumeral (slotData, d.context.scale)), text.removeFromTop (text.getHeight() * 0.5f).toNearestInt(),
                              juce::Justification::centredBottom, 1);
            g.setFont (font (13.0f));
            g.setColour (colours::whiteKeyText.withAlpha (0.8f));
            g.drawFittedText (juce::String (nameSlot (slotData, d.context).text()), text.toNearestInt(), juce::Justification::centredTop, 1, 0.7f);

            if (slot == d.lastSlot)
            {
                g.setColour (colours::chord);
                g.fillRoundedRectangle (r.reduced (5.0f, 0.0f).removeFromBottom (5.0f).translated (0.0f, -4.0f), 2.0f);
            }
            if (slot < flashCount)
            {
                const double t = juce::Time::getMillisecondCounterHiRes() - flashStart - flashStepMs * slot;
                if (t >= 0.0 && t < flashOnMs)
                {
                    g.setColour (colours::chord.withAlpha ((float) (0.6 * (1.0 - t / flashOnMs))));
                    g.fillRoundedRectangle (r.reduced (1.0f), 3.0f);
                }
            }
        }
        for (int pc = 0; pc < 12; ++pc)
        {
            if (! PianoLayout::isBlack (pc))
                continue;
            const int shifter = keys::blackIndex (pc);
            const auto r = layout.keyBounds (pc, chordBlackHeight);
            const bool on = shifter == d.activeShifter;
            drawBlackKey (g, r, colours::chord, on || active[(size_t) pc], false);
            g.setColour (on ? colours::background : colours::dim);
            g.setFont (font (10.5f));
            g.drawFittedText (shifterLabel (model.settings.shifters[(size_t) shifter]), r.reduced (2.0f, 4.0f).toNearestInt(),
                              juce::Justification::centredBottom, 2, 0.8f);
        }
    }

    void ChordKeyboard::mouseDown (const juce::MouseEvent& e)
    {
        layout.area = getLocalBounds().toFloat();
        const int pc = layout.noteAt (e.position, chordBlackHeight);
        if (pc < 0)
            return;
        if (e.mods.isPopupMenu())
        {
            if (PianoLayout::isBlack (pc))
                shifterMenu (keys::blackIndex (pc));
            else
                slotMenu (keys::whiteLetter (pc));
            return;
        }
        playing.press (keyFor (pc), 100);
    }

    void ChordKeyboard::mouseUp (const juce::MouseEvent&) { playing.release(); }

    void ChordKeyboard::slotMenu (int slot)
    {
        const auto& s = model.settings;
        const int degree = model.display.chords[(size_t) slot].degree;
        static const char* numerals[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };

        juce::PopupMenu degrees, types;
        for (int dgr = 0; dgr < 7; ++dgr)
            addTicked (degrees, 100 + dgr, numerals[dgr], dgr == degree);
        for (int t = 0; t < numChordTypes; ++t)
        {
            ChordSlot preview;
            const bool ok = withChordType (editor::slotFor (model.edits, s.group, s.scale, degree), s.scale, t, preview);
            types.addItem (200 + t, chordType (t).name, ok);
        }

        juce::PopupMenu m;
        m.addSectionHeader ("Key " + juce::String (slot + 1) + ": " + juce::String (nameSlot (model.display.chords[(size_t) slot], model.display.context).text()));
        m.addSubMenu ("Degree", degrees);
        m.addSubMenu ("Chord type", types);
        m.addItem (300, "Reset chord edits", model.edits.degrees[(size_t) degree].edited);

        juce::Component::SafePointer<ChordKeyboard> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [self, slot, degree, s] (int r)
        {
            if (self == nullptr || r == 0)
                return;
            auto& b = self->bindings;
            if (r >= 100 && r < 107)
            {
                // A degree for one key makes the progression Custom, starting from the current order.
                if (s.progression != Progression::Custom)
                {
                    const auto order = progressionDegrees (s.progression, s.customOrder);
                    for (int n = 0; n < numChordSlots; ++n)
                        b.set (params::customDegree (n), (float) order[(size_t) n]);
                    b.set (params::progression, (float) (int) Progression::Custom);
                }
                b.set (params::customDegree (slot), (float) (r - 100));
            }
            else if (r >= 200 && r < 200 + numChordTypes)
            {
                self->processor.chordEditor.setChordType (s.group, s.scale, degree, r - 200);
            }
            else if (r == 300)
            {
                self->processor.chordEditor.resetDegree (degree);
            }
        });
    }

    void ChordKeyboard::shifterMenu (int n)
    {
        const auto& t = model.settings.shifters[(size_t) n];
        juce::PopupMenu kind, group, key, scale;
        addTicked (kind, 1, "Chord Group", t.kind == ShifterKind::ChordGroup);
        addTicked (kind, 2, "Key/Scale", t.kind == ShifterKind::KeyScale);
        for (int g = 0; g < numChordGroups; ++g)
            addTicked (group, 100 + g, chordGroupInfo ((ChordGroup) g).name, t.group == (ChordGroup) g);
        addTicked (key, 200, "Same", t.key < 0);
        for (int k = 0; k < numKeys; ++k)
            addTicked (key, 201 + k, keyName (k), t.key == k);
        addTicked (scale, 300, "Same", t.scale < 0);
        for (int sc = 0; sc < numScales; ++sc)
            addTicked (scale, 301 + sc, scaleInfo ((Scale) sc).name, t.scale == sc);

        juce::PopupMenu m;
        m.addSectionHeader ("Shifter " + juce::String (n + 1));
        m.addSubMenu ("Type", kind);
        m.addSubMenu ("Chord Group", group, t.kind == ShifterKind::ChordGroup);
        m.addSubMenu ("Key", key, t.kind == ShifterKind::KeyScale);
        m.addSubMenu ("Scale", scale, t.kind == ShifterKind::KeyScale);

        juce::Component::SafePointer<ChordKeyboard> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [self, n] (int r)
        {
            if (self == nullptr || r == 0)
                return;
            auto& b = self->bindings;
            if (r == 1 || r == 2)                  b.set (params::shifterKind (n), (float) (r - 1));
            else if (r >= 100 && r < 200)          b.set (params::shifterGroup (n), (float) (r - 100));
            else if (r >= 200 && r < 300)          b.set (params::shifterKey (n), (float) (r - 200));
            else if (r >= 300)                     b.set (params::shifterScale (n), (float) (r - 300));
        });
    }

    //==========================================================================
    SmartKeyboard::SmartKeyboard (ChordismProcessor& p, Bindings& b) : processor (p), bindings (b), playing (p)
    {
        setTooltip ("Smart Scale keys: click to play. Right-click a white key for Note Repeat, a black key for its pattern slot.");
    }

    void SmartKeyboard::refresh (const UiModel& m)
    {
        std::array<bool, 128> now {};
        for (int n = 0; n < 128; ++n)
            now[(size_t) n] = processor.isKeyDown (n) || processor.isPerforming (n);

        const int start = m.settings.split - m.settings.split % 12;
        const bool changed = now != active || ! (m.display == model.display) || m.settings.split != model.settings.split
                          || ! (m.settings.smart.map == model.settings.smart.map) || m.settings.smart.patternSlot != model.settings.smart.patternSlot
                          || m.settings.smart.noteRepeat != model.settings.smart.noteRepeat;
        bool patternsChanged = false;
        for (int s = 0; s < numPatternSlots; ++s)
            patternsChanged |= m.settings.smart.patterns[(size_t) s].pattern != model.settings.smart.patterns[(size_t) s].pattern;
        model = m;
        active = now;
        layout.lo = juce::jlimit (0, 103, start);
        layout.hi = layout.lo + 24;
        if (changed || patternsChanged)
            repaint();
    }

    void SmartKeyboard::paint (juce::Graphics& g)
    {
        layout.area = getLocalBounds().toFloat();
        const auto& d = model.display;
        const auto& ss = model.settings.smart;
        const bool chromatic = ss.map.mapping == Mapping::Chromatic;

        SmartChord chord;
        chord.key = d.context.key;
        chord.scale = d.context.scale;
        if (d.lastSlot >= 0 && d.lastSlot < numChordSlots)
            chord.slot = d.chords[(size_t) d.lastSlot];
        ScaleMapper mapper;
        mapper.update (ss.map, chord);
        const Spelling spelling = makeSpelling (d.context.key, d.context.scale);

        // The current chord's pitch classes (its notes as played) and root.
        std::array<bool, 12> chordPc {};
        int rootPc = -1;
        if (d.lastSlot >= 0)
        {
            for (const auto n : chordNotes (chord.slot, d.context))
                chordPc[(size_t) (n % 12)] = true;
            rootPc = slotRootPc (chord.slot, d.context);
        }

        for (int n = layout.lo; n <= layout.hi; ++n)
        {
            if (PianoLayout::isBlack (n))
                continue;
            auto r = layout.keyBounds (n, smartBlackHeight);
            const bool inZone = n >= model.settings.split;
            drawWhiteKey (g, r, colours::smart, active[(size_t) n] && inZone);
            if (! inZone)
            {
                g.setColour (colours::background.withAlpha (0.55f));
                g.fillRoundedRectangle (r.reduced (1.0f, 0.0f), 4.0f);
                continue;
            }
            const int t = mapper.tone (n);
            if (t < 0)
                continue;
            const bool isChordTone = chordPc[(size_t) (t % 12)];
            if (isChordTone)
            {
                g.setColour (colours::smart);
                g.fillRoundedRectangle (r.reduced (3.0f, 0.0f).removeFromBottom (5.0f).translated (0.0f, -3.0f), 2.0f);
            }
            auto text = r.reduced (1.0f, 10.0f).withTrimmedTop (r.getHeight() * smartBlackHeight);
            g.setColour (t % 12 == rootPc ? colours::smart.darker (0.6f) : colours::whiteKeyText);
            g.setFont (font (13.0f, isChordTone));
            g.drawFittedText (juce::String (spelling.name (t)), text.removeFromTop (text.getHeight() * 0.6f).toNearestInt(),
                              juce::Justification::centredBottom, 1, 0.6f);
            g.setFont (font (9.5f));
            g.setColour (colours::whiteKeyText.withAlpha (0.5f));
            g.drawFittedText (juce::String (t / 12 - 2), text.toNearestInt(), juce::Justification::centredTop, 1);
        }

        for (int n = layout.lo; n <= layout.hi; ++n)
        {
            if (! PianoLayout::isBlack (n))
                continue;
            const auto r = layout.keyBounds (n, smartBlackHeight);
            const bool inZone = n >= model.settings.split;
            const int slot = keys::blackIndex (n % 12);
            const bool selected = ! chromatic && inZone && slot == ss.patternSlot;
            drawBlackKey (g, r, colours::smart, inZone && active[(size_t) n], selected);
            if (! inZone || chromatic)
                continue;
            g.setColour (active[(size_t) n] ? colours::background : colours::dim);
            g.setFont (font (11.0f, true));
            auto tr = r.reduced (1.0f, 4.0f);
            g.drawText (juce::String (slot + 1), tr.removeFromBottom (14.0f), juce::Justification::centred);
            g.setFont (font (8.5f));
            g.drawFittedText (ss.patterns[(size_t) slot].melody ? juce::String ("Mel") : shortArpName ((int) ss.patterns[(size_t) slot].pattern),
                              tr.removeFromBottom (12.0f).toNearestInt(),
                              juce::Justification::centred, 1, 0.5f);
        }
    }

    void SmartKeyboard::mouseDown (const juce::MouseEvent& e)
    {
        layout.area = getLocalBounds().toFloat();
        const int n = layout.noteAt (e.position, smartBlackHeight);
        if (n < 0 || n < model.settings.split)
            return;
        const bool chromatic = model.settings.smart.map.mapping == Mapping::Chromatic;
        if (e.mods.isPopupMenu())
        {
            if (PianoLayout::isBlack (n) && ! chromatic)
            {
                patternMenu (keys::blackIndex (n % 12));
                return;
            }
            juce::PopupMenu m;
            m.addItem (1, "Note Repeat (all Smart Scale keys)", true, model.settings.smart.noteRepeat);
            juce::Component::SafePointer<SmartKeyboard> self (this);
            const bool on = model.settings.smart.noteRepeat;
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [self, on] (int r)
            {
                if (self != nullptr && r == 1)
                    self->bindings.set (params::noteRepeat, on ? 0.0f : 1.0f);
            });
            return;
        }
        playing.press (n, 100);
    }

    void SmartKeyboard::mouseUp (const juce::MouseEvent&) { playing.release(); }

    void SmartKeyboard::patternMenu (int slot)
    {
        const auto& p = model.settings.smart.patterns[(size_t) slot];
        juce::PopupMenu arps, rates, chords;
        for (int a = 1; a < numArpPatterns; ++a)
            addTicked (arps, 100 + a, arpPatternName ((ArpPattern) a), ! p.melody && p.pattern == (ArpPattern) a);
        arps.addSeparator();
        addTicked (arps, 100 + numArpPatterns, "Melody", p.melody);   // the parameter's last choice
        for (int r = 0; r < numPatternRates; ++r)
            addTicked (rates, 200 + r, patternRateName (r), p.rate == r);
        for (int c = 0; c < numArpChords; ++c)
            addTicked (chords, 300 + c, arpChordName ((ArpChord) c), p.chord == (ArpChord) c);

        juce::PopupMenu m;
        m.addSectionHeader ("Pattern " + juce::String (slot + 1));
        m.addItem (1, "Edit this pattern", true, model.settings.smart.patternSlot == slot);
        m.addSubMenu ("Pattern", arps);
        m.addSubMenu ("Rate", rates);
        m.addSubMenu ("Arp chord", chords);

        juce::Component::SafePointer<SmartKeyboard> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [self, slot] (int r)
        {
            if (self == nullptr || r == 0)
                return;
            auto& b = self->bindings;
            if (r == 1)                    b.set (params::patternSlot, (float) slot);
            else if (r >= 100 && r < 200)  b.set (params::patternArp (slot), (float) (r - 101));
            else if (r >= 200 && r < 300)  b.set (params::patternRate (slot), (float) (r - 200));
            else if (r >= 300)             b.set (params::patternChord (slot), (float) (r - 300));
        });
    }

    //==========================================================================
    OutputKeyboard::OutputKeyboard (ChordismProcessor& p) : processor (p), playing (p)
    {
        layout.lo = 21;
        layout.hi = 108;
        setTooltip ("Output: the notes Chordism plays (red) and the keys held (outlined). Click to play.");
    }

    void OutputKeyboard::refresh (const UiModel& m)
    {
        std::array<bool, 128> s {}, k {};
        for (int n = 0; n < 128; ++n)
        {
            s[(size_t) n] = processor.isNoteSounding (n);
            k[(size_t) n] = processor.isKeyDown (n);
        }
        if (s != sounding || k != down || m.settings.split != split)
        {
            sounding = s;
            down = k;
            split = m.settings.split;
            repaint();
        }
    }

    void OutputKeyboard::paint (juce::Graphics& g)
    {
        layout.area = getLocalBounds().toFloat();
        for (int pass = 0; pass < 2; ++pass)
            for (int n = layout.lo; n <= layout.hi; ++n)
            {
                const bool black = PianoLayout::isBlack (n);
                if (black != (pass == 1))
                    continue;
                auto r = layout.keyBounds (n);
                if (black)
                {
                    g.setColour (sounding[(size_t) n] ? colours::sounding : colours::blackKey);
                    g.fillRect (r);
                }
                else
                {
                    g.setColour (sounding[(size_t) n] ? colours::sounding : colours::whiteKey);
                    g.fillRect (r.reduced (0.5f, 0.0f));
                    if (n % 12 == 0)
                    {
                        g.setColour (colours::whiteKeyText.withAlpha (0.5f));
                        g.setFont (font (9.0f));
                        g.drawText ("C" + juce::String (n / 12 - 2), r.removeFromBottom (12.0f), juce::Justification::centred);
                    }
                }
                if (down[(size_t) n])
                {
                    g.setColour (n < split ? colours::chord : colours::smart);
                    g.drawRect (layout.keyBounds (n).reduced (1.0f), 2.0f);
                }
            }

        if (split > layout.lo && split <= layout.hi)
        {
            const float x = layout.keyBounds (split).getX() + (PianoLayout::isBlack (split) ? layout.keyBounds (split).getWidth() * 0.5f : 0.0f);
            g.setColour (colours::text);
            g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight());
        }
    }

    void OutputKeyboard::mouseDown (const juce::MouseEvent& e) { mouseDrag (e); }

    void OutputKeyboard::mouseDrag (const juce::MouseEvent& e)
    {
        layout.area = getLocalBounds().toFloat();
        const int n = layout.noteAt (e.position);
        const int velocity = juce::jlimit (1, 127, 40 + (int) (87.0f * e.position.y / (float) juce::jmax (1, getHeight())));
        if (n >= 0)
            playing.press (n, velocity);
    }

    void OutputKeyboard::mouseUp (const juce::MouseEvent&) { playing.release(); }
}
