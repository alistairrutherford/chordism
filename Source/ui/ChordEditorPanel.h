// The Chord Editor (PLAN.md §4.2, §6): seven columns, one per chord key, each its degree's
// steps from the 13th (top) down to the root. A step shows its note, with:
//
//   click            turn the step on or off
//   ♭ / ♯ corners    lower or raise it (-2..+2), or use the mouse wheel
//   five dots        the octaves it sounds in (-2..+2), stackable
//
// Edits belong to the degree (a progression only moves chords between keys) and to the Chord
// Group they were made on (choosing another group resets them). Reset clears one chord, Reset
// all every chord.
#pragma once

#include "Keyboards.h"

namespace chordism::ui
{
    class ChordEditorPanel : public juce::Component,
                             public juce::SettableTooltipClient
    {
    public:
        ChordEditorPanel (ChordismProcessor&);
        void refresh (const UiModel&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        // Where key `slot`'s step `step` (0 = root … 6 = 13th) is drawn (for tests).
        juce::Rectangle<float> stepBounds (int slot, int step) const { return cellBounds (slot, step); }

    private:
        struct Hit
        {
            int slot = -1, step = -1;
            enum Part { None, Toggle, Flat, Sharp, Octave } part = None;
            int octave = 0;
        };

        juce::Rectangle<float> cellBounds (int slot, int step) const;
        Hit hitTest (juce::Point<float>) const;
        ChordSlot slotSteps (int slot) const;   // the steps the editor shows for a key
        void changeAccidental (int slot, int step, int delta);

        ChordismProcessor& processor;
        UiModel model;
        juce::Rectangle<float> grid;
        juce::TextButton resetAll { "Reset all" };
        juce::OwnedArray<juce::TextButton> resets;
        juce::Label title;
    };
}
