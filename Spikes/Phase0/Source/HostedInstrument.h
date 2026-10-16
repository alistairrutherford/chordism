#pragma once

#include <JuceHeader.h>
#include <atomic>
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
    juce::PluginDescription getLoadedDescription() const;
    juce::String getRestoreInfo() const { return restoreInfo; }   // e.g. "restored from the saved set at 21:21:04"

    // Adds a description to the list shown in the UI (e.g. a restored instrument
    // that hasn't been scanned in this session).
    void addToAvailable (const juce::PluginDescription& desc);

    // Peak level of the hosted instrument's output since the last call (UI meter).
    float takeOutputPeak() noexcept { return outputPeak.exchange (0.0f); }
    bool isLoaded() const { return slot != nullptr; }
    void showEditor();

    // While a restore is still loading (or if it failed), saveState() returns the
    // saved XML unchanged, so a save in between never drops the hosted instrument.
    std::unique_ptr<juce::XmlElement> saveState() const;
    void restoreState (const juce::XmlElement& xml, std::function<void (const juce::String&)> done = {});
    juce::String getRestoreError() const { return restoreError; }

    // True (once) if the hosted instrument's parameters or state changed since the
    // last call, so the outer plugin can tell the host its state is dirty.
    bool consumeDirty() noexcept { return dirty.exchange (false); }

    juce::AudioPluginFormatManager& getFormatManager() { return formats; }
    juce::AudioPluginInstance* getInstance() const { return slot != nullptr ? slot->instance.get() : nullptr; }

    // Called from the outer plugin's prepareToPlay / releaseResources.
    void prepare (double sampleRate, int maxBlockSize);
    void release();

    // ---- audio thread ----
    // Renders the hosted instrument into `out` (adds nothing if none is loaded).
    // Returns true if a hosted instrument processed this block.
    bool process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept;

    std::function<void()> onChange;     // message thread: a plugin was loaded or unloaded
    std::function<void (const juce::String&)> log;   // message thread diagnostics

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

    struct ChangeListener : juce::AudioProcessorListener
    {
        explicit ChangeListener (std::atomic<bool>& f) : flag (f) {}
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { flag.store (true); }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override { flag.store (true); }
        std::atomic<bool>& flag;
    };

    std::atomic<bool> dirty { false };
    ChangeListener changeListener { dirty };
    std::unique_ptr<juce::XmlElement> pendingRestore;
    juce::String restoreError, restoreInfo;
    std::atomic<float> outputPeak { 0.0f };

    std::unique_ptr<juce::DocumentWindow> editorWindow;
    double sampleRate = 44100.0;
    int blockSize = 512;
    bool prepared = false;

    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};
