// The editor (PLAN.md §6), about 1100×640 and resizable:
//
//   top bar      logo (Setup) · Enable · Key · Scale · Progression · Chord Group · Mapping · …
//   left half    the Chord Generator: its keyboard, and below it the Chord FX panel or (gear)
//                the Chord Editor grid
//   right half   the Smart Scale: Inversions, Note repeat, octave, mute; its keyboard; the
//                selected pattern slot and the Smart Scale effects
//   bottom       the output keyboard (can be hidden), which also plays
//
// Phase 6: drop a .mid anywhere on the editor to import its chords (the chord keys light up in
// turn); the top bar has the preset browser and the Export handle; the computer keyboard plays
// (QWERTY, PLAN.md §4.7): with the Piano layout the A row plays from the C an octave below the
// split (A–J the chord zone, K on the Smart Scale; W E T Y U O P the black keys) and Z / X move
// it an octave; with Two rows, Z–/ play the octave below the split and Q–P the split's. The
// layout is a preference (Setup).
//
// Everything here is a host parameter or the Chord Editor's state; the UI keeps only its own
// preferences (page, keyboard shown) in the plug-in state. Refreshed at 30 Hz from the
// processor's published display and note states.
#pragma once

#include "Panels.h"
#include "ChordEditorPanel.h"
#include "Theme.h"
#include <map>

namespace chordism
{
    class ChordismProcessor;

    class ChordismEditor : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer
    {
    public:
        explicit ChordismEditor (ChordismProcessor&);
        ~ChordismEditor() override;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

        // For tests (Tests/Snapshot): read the processor now, without waiting for the timer;
        // the parameters some control is (or was) bound to; the pages.
        void refreshNow() { timerCallback(); }
        const std::set<juce::String>& boundParameters() const noexcept { return bindings.boundIds(); }
        void showEditorPage (bool chordEditorPage);
        void showSetup (bool shown);
        void showKeyboard (bool shown);

        // MIDI import (a drop, or the preset browser): the message, and the keys flash.
        void importMidi (const juce::File& file);
        const juce::String& getStatus() const noexcept { return statusText; }

        // QWERTY playing.
        enum class Qwerty { Off = 1, Piano, TwoRows };
        static int qwertyNote (Qwerty layout, juce::juce_wchar key, int split, int octave);   // -1 if none
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool isKeyDown) override;
        void focusLost (FocusChangeType) override { releaseQwerty(); }

        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
        void filesDropped (const juce::StringArray& files, int, int) override;

        static constexpr int defaultWidth = 1100, defaultHeight = 640;

    private:
        void timerCallback() override;
        void showStatus (const juce::String& text, bool error);
        void releaseQwerty();

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
        juce::Label status;
        juce::String statusText;
        double statusUntil = 0.0;
        bool dropHover = false, focusGrabbed = false;
        std::map<juce::juce_wchar, int> qwertyHeld;   // key → note

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordismEditor)
    };
}
