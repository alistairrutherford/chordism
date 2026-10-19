#include "Theme.h"

namespace chordism::ui
{
    LookAndFeel::LookAndFeel()
    {
        setColourScheme ({ colours::background, colours::panel, colours::raised, colours::outline, colours::text,
                           colours::chord, colours::background, colours::raised, colours::text });
        setColour (juce::ComboBox::backgroundColourId, colours::raised);
        setColour (juce::ComboBox::outlineColourId, colours::outline);
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::ComboBox::arrowColourId, colours::dim);
        setColour (juce::PopupMenu::backgroundColourId, colours::panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::raised);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
        setColour (juce::TextButton::buttonColourId, colours::raised);
        setColour (juce::TextButton::buttonOnColourId, colours::chord);
        setColour (juce::TextButton::textColourOffId, colours::dim);
        setColour (juce::TextButton::textColourOnId, colours::background);
        setColour (juce::Label::textColourId, colours::dim);
        setColour (juce::Slider::backgroundColourId, colours::raised);
        setColour (juce::Slider::trackColourId, colours::chord.withAlpha (0.55f));
        setColour (juce::Slider::textBoxTextColourId, colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TooltipWindow::backgroundColourId, colours::raised);
        setColour (juce::TooltipWindow::textColourId, colours::text);
    }

    void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool highlighted, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : base;
        if (! b.isEnabled())
            c = c.withAlpha (0.4f);
        else if (down)
            c = c.brighter (0.15f);
        else if (highlighted)
            c = c.brighter (0.07f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (colours::outline.withAlpha (b.getToggleState() ? 0.0f : 1.0f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                         .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
    }

    void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
    {
        const auto r = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        juce::Path arrow;
        const float ax = (float) width - 12.0f, ay = (float) height * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
        g.fillPath (arrow);
    }

    void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (4, 1, box.getWidth() - 20, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                        juce::Slider::SliderStyle style, juce::Slider& s)
    {
        if (style != juce::Slider::LinearBar)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0.0f, 0.0f, style, s);
            return;
        }
        const auto r = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (0.5f);
        g.setColour (s.findColour (juce::Slider::backgroundColourId));
        g.fillRoundedRectangle (r, 4.0f);
        auto fill = r.withRight (juce::jmax (r.getX(), pos));
        g.setColour (s.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (fill, 4.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (font (12.5f));
        l->setColour (juce::Label::textColourId, colours::text);
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return l;
    }
}
