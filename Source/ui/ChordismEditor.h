// The editor (PLAN.md §6), about 1100×640 and resizable:
//
//   top bar      logo (Setup) · Enable · Key · Scale · Progression · Chord Group · Mapping · …
//   left half    the Chord Generator: its keyboard, and below it the Chord FX panel or (gear)
//                the Chord Editor grid
//   right half   the Smart Scale: Inversions, Note repeat, octave, mute; its keyboard; the
//                selected pattern slot and the Smart Scale effects
//   bottom       the output keyboard (can be hidden), which also plays
//
// Everything here is a host parameter or the Chord Editor's state; the UI keeps only its own
// preferences (page, keyboard shown) in the plug-in state. Refreshed at 30 Hz from the
// processor's published display and note states.
#pragma once

#include "Panels.h"
#include "ChordEditorPanel.h"
#include "Theme.h"

namespace chordism
{
    class ChordismProcessor;

    class ChordismEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
    {
    public:
        explicit ChordismEditor (ChordismProcessor&);
        ~ChordismEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // For tests (Tests/Snapshot): read the processor now, without waiting for the timer;
        // the parameters some control is (or was) bound to; the pages.
        void refreshNow() { timerCallback(); }
        const std::set<juce::String>& boundParameters() const noexcept { return bindings.boundIds(); }
        void showEditorPage (bool chordEditorPage);
        void showSetup (bool shown);
        void showKeyboard (bool shown);

        static constexpr int defaultWidth = 1100, defaultHeight = 640;

    private:
        void timerCallback() override;

        ChordismProcessor& processor;
        ui::LookAndFeel lookAndFeel;
        ui::Bindings bindings;
        juce::TooltipWindow tooltips { this, 600 };

        ui::TopBar topBar;
        juce::Label chordTitle, smartTitle;
        juce::TextButton fxPage { "FX" }, editorPage { "Chord Editor" };
        ui::ChordKeyboard chordKeys;
        ui::ChordFxPanel chordFx;
        ui::ChordEditorPanel chordEditorPanel;
        ui::ParamControl inversions, noteRepeat, smartOctave, muteSmart;
        ui::SmartKeyboard smartKeys;
        ui::SmartPanel smartPanel;
        ui::OutputKeyboard outputKeys;
        ui::SetupPanel setup;

        juce::Rectangle<int> leftArea, rightArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordismEditor)
    };
}
