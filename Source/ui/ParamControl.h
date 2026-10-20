// Controls bound to host parameters (PLAN.md §6). A ParamControl builds itself from the
// parameter's type: a choice is a menu, a bool a toggle button, a number a bar slider showing
// the parameter's own text (Split shows "C3"). It can be re-bound to another parameter (the
// pattern panel shows the selected slot's settings).
//
// Bindings records every parameter some control has been bound to, so a test can check that
// every feature is reachable from the editor (Tests/Snapshot). StepSeqView edits a sequencer
// pattern's 16 steps (one packed parameter, Parameters.h).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <set>

namespace chordism::ui
{
    class Bindings
    {
    public:
        explicit Bindings (juce::AudioProcessorValueTreeState& s) : state (s) {}

        juce::AudioProcessorValueTreeState& state;

        void bound (const juce::String& paramId) { ids.insert (paramId); }
        const std::set<juce::String>& boundIds() const noexcept { return ids; }

        juce::RangedAudioParameter* param (const juce::String& id) const { return state.getParameter (id); }
        float value (const juce::String& id) const
        {
            auto* p = state.getRawParameterValue (id);
            return p != nullptr ? p->load() : 0.0f;
        }
        // Sets a parameter from the UI as one host gesture.
        void set (const juce::String& id, float plainValue) const;

    private:
        std::set<juce::String> ids;
    };

    class ParamControl : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        // `label` replaces the parameter's name as the caption; `showLabel` false for a control
        // with its caption elsewhere. A bool's caption is the button text.
        ParamControl (Bindings&, const juce::String& paramId, const juce::String& label = {}, bool showLabel = true);
        ~ParamControl() override;

        void bind (const juce::String& paramId, const juce::String& label = {});
        const juce::String& getParamId() const noexcept { return paramId; }
        void setAccent (juce::Colour c);
        void setCaptionLeft (int width) { captionLeft = width; resized(); }   // caption beside the control

        void resized() override;

    private:
        Bindings& bindings;
        juce::String paramId;
        bool showLabel = true;
        int captionLeft = 0;
        juce::Colour accent;
        juce::Label caption;
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    };

    // The 16 steps of a sequencer pattern: one packed parameter (seqNSteps / smartSeqNSteps).
    class StepSeqView : public juce::Component,
                        public juce::SettableTooltipClient,
                        private juce::AudioProcessorParameter::Listener,
                        private juce::AsyncUpdater
    {
    public:
        StepSeqView (Bindings&, juce::Colour accent);
        ~StepSeqView() override;

        void bind (const juce::String& paramId);
        void setPlayingStep (int step);   // -1 for none

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        void parameterValueChanged (int, float) override { triggerAsyncUpdate(); }
        void parameterGestureChanged (int, bool) override {}
        void handleAsyncUpdate() override { repaint(); }
        int mask() const;
        int stepAt (juce::Point<float>) const;
        juce::Rectangle<float> stepBounds (int step) const;

        Bindings& bindings;
        juce::Colour accent;
        juce::Font numberFont;
        juce::String paramId;
        juce::RangedAudioParameter* param = nullptr;
        int playing = -1;
        bool dragValue = true;
        int lastDragStep = -1;
    };
}
