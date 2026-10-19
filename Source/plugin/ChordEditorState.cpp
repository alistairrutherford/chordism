#include "ChordEditorState.h"

namespace chordism
{
    const juce::Identifier ChordEditorState::treeType ("ChordEditor");

    namespace
    {
        const juce::Identifier degreeType ("Degree"), stepType ("Step");
        const juce::Identifier groupId ("group"), indexId ("index"), onId ("on"), accId ("acc"), octId ("oct");
    }

    ChordEditorState::ChordEditorState()
    {
        const juce::ScopedLock sl (lock);
        commit();
    }

    juce::ValueTree ChordEditorState::toTree (const ChordEdits& edits)
    {
        juce::ValueTree t (treeType);
        t.setProperty (groupId, (int) edits.group, nullptr);
        for (int d = 0; d < numChordSlots; ++d)
        {
            const auto& de = edits.degrees[(size_t) d];
            if (! de.edited)
                continue;
            juce::ValueTree degree (degreeType);
            degree.setProperty (indexId, d, nullptr);
            for (int k = 0; k < numChordSteps; ++k)
            {
                const auto& st = de.steps[(size_t) k];
                juce::ValueTree step (stepType);
                step.setProperty (indexId, k, nullptr);
                step.setProperty (onId, st.on, nullptr);
                step.setProperty (accId, (int) st.accidental, nullptr);
                step.setProperty (octId, (int) st.octaveMask, nullptr);
                degree.appendChild (step, nullptr);
            }
            t.appendChild (degree, nullptr);
        }
        return t;
    }

    ChordEdits ChordEditorState::fromTree (const juce::ValueTree& t)
    {
        ChordEdits e;
        if (! t.hasType (treeType))
            return e;

        e.group = (ChordGroup) juce::jlimit (0, numChordGroups - 1, (int) t.getProperty (groupId, 0));
        for (const auto& degree : t)
        {
            const int d = degree.getProperty (indexId, -1);
            if (! degree.hasType (degreeType) || ! editor::validDegree (d))
                continue;
            auto& de = e.degrees[(size_t) d];
            de.edited = true;
            de.steps = {};
            for (const auto& step : degree)
            {
                const int k = step.getProperty (indexId, -1);
                if (! step.hasType (stepType) || ! editor::validStep (k))
                    continue;
                auto& st = de.steps[(size_t) k];
                st.on = step.getProperty (onId, false);
                st.accidental = (int8_t) editor::clampAccidental (step.getProperty (accId, 0));
                st.octaveMask = (uint8_t) ((int) step.getProperty (octId, (int) octaveBit (0)) & editor::allOctaves);
            }
        }
        return e;
    }

    void ChordEditorState::commit()
    {
        tree = toTree (model);
        model.version = ++version;
        exchange.publish (model);
    }

    void ChordEditorState::setStep (ChordGroup group, Scale scale, int degree, int step, Step value)
    {
        const juce::ScopedLock sl (lock);
        editor::setStep (model, group, scale, degree, step, value);
        commit();
    }

    void ChordEditorState::setOn (ChordGroup group, Scale scale, int degree, int step, bool on)
    {
        const juce::ScopedLock sl (lock);
        editor::setOn (model, group, scale, degree, step, on);
        commit();
    }

    void ChordEditorState::setAccidental (ChordGroup group, Scale scale, int degree, int step, int accidental)
    {
        const juce::ScopedLock sl (lock);
        editor::setAccidental (model, group, scale, degree, step, accidental);
        commit();
    }

    void ChordEditorState::setOctave (ChordGroup group, Scale scale, int degree, int step, int octave, bool on)
    {
        const juce::ScopedLock sl (lock);
        editor::setOctave (model, group, scale, degree, step, octave, on);
        commit();
    }

    void ChordEditorState::setChordType (ChordGroup group, Scale scale, int degree, int type)
    {
        if (! editor::validDegree (degree))
            return;
        const juce::ScopedLock sl (lock);
        auto& d = editor::begin (model, group, scale, degree);
        ChordSlot slot;
        slot.degree = degree;
        slot.steps = d.steps;
        if (withChordType (slot, scale, type, slot))
            d.steps = slot.steps;
        commit();
    }

    void ChordEditorState::resetDegree (int degree)
    {
        const juce::ScopedLock sl (lock);
        editor::resetDegree (model, degree);
        commit();
    }

    void ChordEditorState::resetAll (ChordGroup group)
    {
        const juce::ScopedLock sl (lock);
        editor::reset (model, group);
        commit();
    }

    uint32_t ChordEditorState::getVersion() const
    {
        const juce::ScopedLock sl (lock);
        return version;
    }

    ChordEdits ChordEditorState::getEdits() const
    {
        const juce::ScopedLock sl (lock);
        return model;
    }

    juce::ValueTree ChordEditorState::copyTree() const
    {
        const juce::ScopedLock sl (lock);
        return tree.createCopy();
    }

    void ChordEditorState::restore (const juce::ValueTree& t, ChordGroup group)
    {
        const juce::ScopedLock sl (lock);
        model = fromTree (t);
        if (! t.isValid() || (model.group != group && model.any()))
            editor::reset (model, group);   // no edits saved, or saved for another group
        commit();
    }

    void ChordEditorState::sync (ChordGroup group, uint32_t engineDiscards, uint32_t discardedVersion)
    {
        const juce::ScopedLock sl (lock);
        bool clear = model.group != group;
        if (engineDiscards != seenDiscards)
        {
            seenDiscards = engineDiscards;
            // The engine dropped the snapshot it had: clear it here too, unless a newer one was
            // published since (an edit made on the new group).
            clear |= discardedVersion == version;
        }
        if (clear && (model.any() || model.group != group))
        {
            editor::reset (model, group);
            commit();
        }
    }
}
