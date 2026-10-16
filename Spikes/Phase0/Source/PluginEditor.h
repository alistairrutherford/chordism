#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// Drag source for spike (3): writes the recorded output to a temporary .mid file
// and starts a native file drag, so it can be dropped onto a Live track.
class ExportHandle : public juce::Component
{
public:
    explicit ExportHandle (SpikeProcessor& p) : processor (p) {}

    void setBars (int newBars);
    void paint (juce::Graphics& g) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

private:
    SpikeProcessor& processor;
    int bars = 0;
    bool dragging = false;
};

class SpikeEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit SpikeEditor (SpikeProcessor&);
    ~SpikeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPluginList();

    SpikeProcessor& processor;

    juce::Label title, status;
    juce::ToggleButton syncToggle { "Sync key with other instances" };
    juce::ComboBox keyBox;
    juce::ToggleButton testToggle { "Test pattern: I IV V vi on every beat while playing" };

    juce::ToggleButton portToggle { "CoreMIDI port" };
    juce::Label portStatus, offsetLabel;
    juce::Slider offsetSlider;

    juce::Label hostTitle, hostStatus;
    juce::TextButton scanButton { "Scan instruments" }, loadButton { "Load" },
                     unloadButton { "Unload" }, editorButton { "Show editor" };
    juce::ComboBox pluginBox;

    ExportHandle exportHandle;

    juce::String listedLoaded;   // hosted instrument the plugin list was last built around
    float shownPeak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpikeEditor)
};
