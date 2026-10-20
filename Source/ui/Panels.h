// The editor's panels (PLAN.md §6):
//
//   TopBar         logo (opens Setup) · Enable · Preset browser · Key · Scale · Progression ·
//                  Chord Group · Mapping · Shifter mode · Export handle · keyboard show/hide
//   ChordFxPanel   the chord zone's effects: Stack, Octave, Hold, Mute; Strum; Arp; Repeat;
//                  Velocity; Step Sequencer and its steps
//   SmartPanel     the selected pattern slot (1-5) and its settings: an arp's, or in Melody mode
//                  the melody's (Notes, Rhythm; Swing and Seed for all slots beside the slot
//                  buttons); the Smart Scale's effects: Stack, Hold, Velocity, Step Sequencer
//                  and its steps
//   SetupPanel     everything else, as an overlay: Split, the custom progression, the five
//                  shifters' targets, the virtual port, the test pattern (LiveBridge), and the
//                  preferences (the QWERTY layout)
#pragma once

#include "Keyboards.h"
#include "PresetBar.h"

namespace chordism::ui
{
    // ParamControls in rows of `columns` cells; empty cells with addGap().
    class ParamGrid : public juce::Component
    {
    public:
        ParamGrid (Bindings& b, int columns, juce::Colour accent) : bindings (b), cols (columns), colour (accent) {}

        ParamControl& add (const juce::String& paramId, const juce::String& label = {});
        void addGap() { cells.add (nullptr); }
        void addSection (const juce::String& title);   // a caption row across the grid

        int rows() const noexcept;
        void resized() override;

        static constexpr int rowHeight = 42, sectionHeight = 20;

    private:
        Bindings& bindings;
        int cols;
        juce::Colour colour;
        juce::OwnedArray<ParamControl> owned;
        juce::OwnedArray<juce::Label> sections;
        juce::Array<juce::Component*> cells;      // nullptr = gap; a Label = section
    };

    class TopBar : public juce::Component
    {
    public:
        TopBar (ChordismProcessor&, Bindings&, std::function<void()> openSetup, std::function<void (bool)> showKeyboard,
                std::function<void (const juce::File&)> importMidi);
        void setKeyboardShown (bool shown);
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;

        PresetBar presets;
        ExportHandle exportHandle;

    private:
        juce::TextButton logo { "Chordism" };
        juce::TextButton keyboardButton { "Keys" };
        ParamControl enable, key, scale, progression, group, mapping, shifterMode;
    };

    class ChordFxPanel : public juce::Component
    {
    public:
        explicit ChordFxPanel (Bindings&);
        void refresh (const UiModel&);
        void resized() override;

    private:
        ParamGrid grid;
        StepSeqView steps;
        juce::Label stepsCaption;
    };

    class SmartPanel : public juce::Component
    {
    public:
        explicit SmartPanel (Bindings&);
        void refresh (const UiModel&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void bindSlot (int slot);

        Bindings& bindings;
        int slotShown = -1;
        bool melodyShown = false;
        juce::Label patternCaption;
        juce::OwnedArray<juce::TextButton> slotButtons;
        ParamControl arp, rate, range, chord, gate, notes, rhythm, swing, seed;
        ParamGrid fx;
        StepSeqView steps;
        juce::Label stepsCaption;
    };

    class SetupPanel : public juce::Component
    {
    public:
        SetupPanel (ChordismProcessor&, Bindings&, std::function<void()> close);
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        ChordismProcessor& processor;
        juce::TextButton closeButton { "Done" };
        juce::Label heading, portStatus, prefsCaption, qwertyCaption;
        juce::ComboBox qwerty;            // the QWERTY layout preference (ChordismEditor)
        ParamGrid left, shifters, right;
    };
}
