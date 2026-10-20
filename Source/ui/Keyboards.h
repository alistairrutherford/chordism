// The editor's keyboards (PLAN.md §6). Each plays through the engine like host MIDI input
// (ChordismProcessor::pushUiNote), so clicking a key is the same as pressing it.
//
//   ChordKeyboard   the chord zone, one octave: each white key shows its Roman numeral and
//                   chord name, with a stripe on the last one played; the black keys show their
//                   shifter's target and light up while active. Right-click a white key for its
//                   degree (Custom progression), chord type (the Chord Editor) or a reset; a
//                   black key for its shifter's target. After a MIDI import the imported
//                   chords' keys light up in turn.
//   SmartKeyboard   the Smart Scale zone, two octaves from the split's C: each white key shows
//                   the tone it plays now, chord tones with a coloured edge; the black keys show
//                   their pattern slot. Right-click a white key for Note Repeat, a black key to
//                   select its pattern slot or change its pattern (an arp, or Melody).
//   OutputKeyboard  88 keys: the notes Chordism sends (red) and the keys held (outlined), with
//                   the split marked. Hideable.
#pragma once

#include "../plugin/ChordismProcessor.h"
#include "ParamControl.h"

namespace chordism::ui
{
    // What the editor shows, refreshed on its timer.
    struct UiModel
    {
        ChordDisplay display;             // the chords, their context, last slot, active shifter
        EngineSettings settings;          // the parameters now
        ChordEdits edits;                 // the Chord Editor's edits
    };

    // Piano geometry for notes lo..hi (both white keys) across a width.
    struct PianoLayout
    {
        int lo = 60, hi = 84;
        juce::Rectangle<float> area;

        static bool isBlack (int note) noexcept;
        int whiteCount() const noexcept;
        juce::Rectangle<float> keyBounds (int note, float blackHeight = 0.62f) const;
        int noteAt (juce::Point<float> p, float blackHeight = 0.62f) const;   // -1 if none
    };

    // Plays a key while the mouse holds it (and a drag moves along the keys).
    class PlayingKeys
    {
    public:
        explicit PlayingKeys (ChordismProcessor& p) : processor (p) {}
        ~PlayingKeys() { release(); }
        void press (int note, int velocity);
        void release();

    private:
        ChordismProcessor& processor;
        int held = -1;
    };

    class ChordKeyboard : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        ChordKeyboard (ChordismProcessor&, Bindings&);
        void refresh (const UiModel&);

        // Lights the first `count` white keys one after another (a MIDI import, PLAN.md §4.6).
        void flash (int count);
        bool isFlashing() const noexcept { return flashCount > 0; }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        int keyFor (int pc) const;        // the chord-zone note with pitch class pc
        void slotMenu (int slot);
        void shifterMenu (int shifter);

        ChordismProcessor& processor;
        Bindings& bindings;
        PlayingKeys playing;
        UiModel model;
        PianoLayout layout;
        std::array<bool, 12> active {};
        int flashCount = 0;
        double flashStart = 0.0;
        static constexpr double flashStepMs = 160.0, flashOnMs = 300.0;
    };

    class SmartKeyboard : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        SmartKeyboard (ChordismProcessor&, Bindings&);
        void refresh (const UiModel&);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        void patternMenu (int slot);

        ChordismProcessor& processor;
        Bindings& bindings;
        PlayingKeys playing;
        UiModel model;
        PianoLayout layout;
        std::array<bool, 128> active {};
    };

    class OutputKeyboard : public juce::Component,
                          public juce::SettableTooltipClient
    {
    public:
        explicit OutputKeyboard (ChordismProcessor&);
        void refresh (const UiModel&);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        ChordismProcessor& processor;
        PlayingKeys playing;
        PianoLayout layout;
        int split = 60;
        std::array<bool, 128> sounding {}, down {};
    };

    // A short name for an arp pattern ("Lin ↑").
    juce::String shortArpName (int pattern);
}
