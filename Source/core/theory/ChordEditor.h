// The Chord Editor's data (PLAN.md §4.2): edits on top of the Chord Group's shapes.
// Plain C++, no JUCE, no allocation.
//
// Edits belong to a scale degree, not to a white key: a progression only reorders the chords,
// so "the V chord" keeps its edits wherever the progression puts it (two keys playing the same
// degree share them). A degree that isn't edited plays the group's shape. Edits are made on one
// Chord Group and only apply while that group is chosen: choosing a group resets them, as
// ChordPrism does (the plug-in clears them in its state; the engine ignores them from the
// moment the group changes, see ChordismEngine).
//
// Accidentals are stored as shown in the editor, i.e. including the scale's default accidental
// at the time of the edit. Edits are relative to the scale's degrees, so they follow Key and
// Scale changes.
#pragma once

#include "ChordGroups.h"
#include "Progressions.h"

namespace chordism
{
    struct DegreeEdit
    {
        bool edited = false;
        std::array<Step, numChordSteps> steps {};

        bool operator== (const DegreeEdit& o) const noexcept { return edited == o.edited && (! edited || steps == o.steps); }
        bool operator!= (const DegreeEdit& o) const noexcept { return ! (*this == o); }
    };

    struct ChordEdits
    {
        ChordGroup group = ChordGroup::Triads;          // the group the edits were made on
        std::array<DegreeEdit, numChordSlots> degrees {};
        uint32_t version = 0;                           // bumped by the publisher on every change

        bool any() const noexcept
        {
            for (const auto& d : degrees)
                if (d.edited)
                    return true;
            return false;
        }

        // Same edits, whatever the version.
        bool sameEdits (const ChordEdits& o) const noexcept { return group == o.group && degrees == o.degrees; }
    };

    namespace editor
    {
        constexpr int clampAccidental (int a) noexcept { return a < -2 ? -2 : (a > 2 ? 2 : a); }
        constexpr uint8_t allOctaves = (uint8_t) ((1u << (maxStepOctave - minStepOctave + 1)) - 1);

        inline bool validDegree (int d) noexcept { return d >= 0 && d < numChordSlots; }
        inline bool validStep (int k) noexcept { return k >= 0 && k < numChordSteps; }

        // Forgets every edit and ties the (empty) edits to `g`.
        inline void reset (ChordEdits& e, ChordGroup g) noexcept
        {
            e.group = g;
            for (auto& d : e.degrees)
                d = {};
        }

        // The steps a degree plays under group `g` and scale `s`: its edits, if they belong to `g`.
        inline ChordSlot slotFor (const ChordEdits& e, ChordGroup g, Scale s, int degree) noexcept
        {
            if (e.group == g && validDegree (degree) && e.degrees[(size_t) degree].edited)
            {
                ChordSlot slot;
                slot.degree = degree;
                slot.steps = e.degrees[(size_t) degree].steps;
                return slot;
            }
            return makeGroupSlot (g, s, degree);
        }

        // Makes `degree` editable: edits for another group are dropped first, and a degree's
        // first edit starts from the group's shape (with the scale's default accidentals).
        inline DegreeEdit& begin (ChordEdits& e, ChordGroup g, Scale s, int degree) noexcept
        {
            if (e.group != g)
                reset (e, g);
            auto& d = e.degrees[(size_t) degree];
            if (! d.edited)
            {
                d.steps = makeGroupSlot (g, s, degree).steps;
                d.edited = true;
            }
            return d;
        }

        inline void setStep (ChordEdits& e, ChordGroup g, Scale s, int degree, int k, Step step) noexcept
        {
            if (! validDegree (degree) || ! validStep (k))
                return;
            step.accidental = (int8_t) clampAccidental (step.accidental);
            step.octaveMask = (uint8_t) (step.octaveMask & allOctaves);
            begin (e, g, s, degree).steps[(size_t) k] = step;
        }

        inline void setOn (ChordEdits& e, ChordGroup g, Scale s, int degree, int k, bool on) noexcept
        {
            if (! validDegree (degree) || ! validStep (k))
                return;
            auto step = slotFor (e, g, s, degree).steps[(size_t) k];
            step.on = on;
            setStep (e, g, s, degree, k, step);
        }

        inline void setAccidental (ChordEdits& e, ChordGroup g, Scale s, int degree, int k, int accidental) noexcept
        {
            if (! validDegree (degree) || ! validStep (k))
                return;
            auto step = slotFor (e, g, s, degree).steps[(size_t) k];
            step.accidental = (int8_t) clampAccidental (accidental);
            setStep (e, g, s, degree, k, step);
        }

        // Adds or removes one octave (-2..+2) of a step; the editor's octave buttons stack.
        inline void setOctave (ChordEdits& e, ChordGroup g, Scale s, int degree, int k, int octave, bool on) noexcept
        {
            if (! validDegree (degree) || ! validStep (k) || octave < minStepOctave || octave > maxStepOctave)
                return;
            auto step = slotFor (e, g, s, degree).steps[(size_t) k];
            step.octaveMask = (uint8_t) (on ? (step.octaveMask | octaveBit (octave)) : (step.octaveMask & ~octaveBit (octave)));
            setStep (e, g, s, degree, k, step);
        }

        // Back to the group's shape for one degree.
        inline void resetDegree (ChordEdits& e, int degree) noexcept
        {
            if (validDegree (degree))
                e.degrees[(size_t) degree] = {};
        }
    }

    // The seven chords of the chord zone: slot n plays degree order[n], with the degree's edits
    // when `edits` is given and belongs to `g`.
    inline ChordSet makeChords (ChordGroup g, Scale s, const DegreeOrder& order, const ChordEdits* edits) noexcept
    {
        ChordSet set;
        for (size_t n = 0; n < set.size(); ++n)
        {
            const int degree = order[n];
            set[n] = edits != nullptr ? editor::slotFor (*edits, g, s, degree) : makeGroupSlot (g, s, degree);
        }
        return set;
    }
}
