// Chordism's own look (PLAN.md §6): a dark panel UI, amber for the Chord Generator, teal for
// the Smart Scale. Colours, fonts and a LookAndFeel used by every component in Source/ui.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace chordism::ui
{
    namespace colours
    {
        const juce::Colour background   { 0xff15171c };
        const juce::Colour panel        { 0xff1e2128 };
        const juce::Colour raised       { 0xff2a2e37 };
        const juce::Colour outline      { 0xff383d48 };
        const juce::Colour text         { 0xffe6e8ec };
        const juce::Colour dim          { 0xff9aa1ad };
        const juce::Colour faint        { 0xff5d6472 };
        const juce::Colour chord        { 0xfff2a541 };   // Chord Generator accent
        const juce::Colour smart        { 0xff3cc9b0 };   // Smart Scale accent
        const juce::Colour sounding     { 0xfff25f5c };   // notes playing
        const juce::Colour whiteKey     { 0xffe9e6df };
        const juce::Colour whiteKeyText { 0xff2b2d33 };
        const juce::Colour blackKey     { 0xff2b2c31 };
    }

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (12.5f, true); }
        juce::Font getLabelFont (juce::Label&) override { return font (12.0f); }
        juce::Font getPopupMenuFont() override { return font (14.0f); }
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
    };
}
