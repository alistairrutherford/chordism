#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>

// Spike (2): host an AU or VST3 instrument inside the plugin, feed it the
// generated MIDI and output its audio. Scanning is done in-process here (the real
// product scans in a separate process so a crashing plugin can't take Live down).
class HostedInstrument
{
public:
    HostedInstrument();
    ~HostedInstrument();

    // ---- message thread ----
    void scan();
    const juce::Array<juce::PluginDescription>& getAvailable() const { return available; }

    // Loads asynchronously; `done` gets an empty string on success.
    void load (const juce::PluginDescription& desc, std::function<void (const juce::String& error)> done = {},
               const juce::MemoryBlock* stateToRestore = nullptr);
    void unload();

    juce::String getLoadedName() const;
    bool isLoaded() const { return slot != nullptr; }
    void showEditor();

    std::unique_ptr<juce::XmlElement> saveState() const;
    void restoreState (const juce::XmlElement& xml, std::function<void (const juce::String&)> done = {});

    juce::AudioPluginFormatManager& getFormatManager() { return formats; }

    // Called from the outer plugin's prepareToPlay / releaseResources.
    void prepare (double sampleRate, int maxBlockSize);
    void release();

    // ---- audio thread ----
    // Renders the hosted instrument into `out` (adds nothing if none is loaded).
    // Returns true if a hosted instrument processed this block.
    bool process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept;

    std::function<void()> onChange;     // message thread: a plugin was loaded or unloaded

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::PluginDescription description;
        juce::AudioBuffer<float> scratch;
        juce::MidiBuffer midi;
    };

    void prepareSlot (Slot& s) const;
    void swapIn (std::unique_ptr<Slot> newSlot);
    void closeEditor();

    juce::AudioPluginFormatManager formats;
    juce::Array<juce::PluginDescription> available;

    std::unique_ptr<Slot> slot;         // guarded by `lock` against the audio thread
    juce::SpinLock lock;

    std::unique_ptr<juce::DocumentWindow> editorWindow;
    double sampleRate = 44100.0;
    int blockSize = 512;
    bool prepared = false;

    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};
