#include "HostedInstrument.h"

namespace
{
    struct HostedWindow : juce::DocumentWindow
    {
        HostedWindow (const juce::String& name, juce::AudioProcessorEditor* editor, std::function<void()> closed)
            : DocumentWindow (name, juce::Colours::black, DocumentWindow::closeButton),
              onClose (std::move (closed))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
            setAlwaysOnTop (true);   // otherwise it opens behind Live's window
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            if (onClose)
                onClose();
        }

        std::function<void()> onClose;
    };

    bool isOurself (const juce::String& fileOrIdentifier)
    {
        return fileOrIdentifier.contains ("CzSp") || fileOrIdentifier.containsIgnoreCase ("Chordism");
    }
}

HostedInstrument::HostedInstrument()
{
    juce::addDefaultFormatsToManager (formats);
}

HostedInstrument::~HostedInstrument()
{
    *alive = false;
    closeEditor();

    std::unique_ptr<Slot> old;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        std::swap (old, slot);
    }

    if (old != nullptr)
        old->instance->removeListener (&changeListener);
}

void HostedInstrument::scan()
{
    available.clear();

    for (auto* format : formats.getFormats())
    {
        const bool isAU = format->getName() == "AudioUnit";
        const auto ids = format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, false);

        for (const auto& id : ids)
        {
            // Only instruments, and never ourselves. AU identifiers carry their type,
            // so effects are skipped without instantiating them.
            if ((isAU && ! id.startsWith ("AudioUnit:Synths/")) || isOurself (id))
                continue;

            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile (found, id);

            for (auto* d : found)
                if (d->isInstrument && ! isOurself (d->name))
                    available.add (*d);
        }
    }

    std::sort (available.begin(), available.end(), [] (const auto& a, const auto& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    });
}

void HostedInstrument::load (const juce::PluginDescription& desc, std::function<void (const juce::String&)> done,
                             const juce::MemoryBlock* stateToRestore)
{
    std::shared_ptr<juce::MemoryBlock> state;
    if (stateToRestore != nullptr)
        state = std::make_shared<juce::MemoryBlock> (*stateToRestore);

    std::weak_ptr<bool> weakAlive = alive;

    formats.createPluginInstanceAsync (desc, sampleRate, blockSize,
        [this, weakAlive, desc, state, done] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            auto stillAlive = weakAlive.lock();
            if (stillAlive == nullptr || ! *stillAlive)
                return;

            if (instance == nullptr)
            {
                if (done) done (error.isNotEmpty() ? error : juce::String ("Could not create the plugin"));
                return;
            }

            auto newSlot = std::make_unique<Slot>();
            newSlot->instance = std::move (instance);
            newSlot->description = desc;

            if (state != nullptr && state->getSize() > 0)
                newSlot->instance->setStateInformation (state->getData(), (int) state->getSize());

            prepareSlot (*newSlot);
            swapIn (std::move (newSlot));

            if (done) done ({});
        });
}

void HostedInstrument::unload()
{
    pendingRestore.reset();
    restoreInfo.clear();
    swapIn (nullptr);
}

juce::String HostedInstrument::getLoadedName() const
{
    return slot != nullptr ? slot->description.name : juce::String();
}

juce::PluginDescription HostedInstrument::getLoadedDescription() const
{
    return slot != nullptr ? slot->description : juce::PluginDescription();
}

void HostedInstrument::addToAvailable (const juce::PluginDescription& desc)
{
    for (const auto& d : available)
        if (d.isDuplicateOf (desc))
            return;

    available.add (desc);
}

void HostedInstrument::prepareSlot (Slot& s) const
{
    auto& p = *s.instance;
    p.enableAllBuses();

    const int channels = juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    s.scratch.setSize (channels, blockSize);
    s.midi.ensureSize (4096);

    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
}

void HostedInstrument::swapIn (std::unique_ptr<Slot> newSlot)
{
    closeEditor();

    if (newSlot != nullptr)
        newSlot->instance->addListener (&changeListener);

    {
        const juce::SpinLock::ScopedLockType l (lock);
        std::swap (slot, newSlot);
    }

    if (newSlot != nullptr)     // the previous instrument, now off the audio thread
    {
        newSlot->instance->removeListener (&changeListener);
        newSlot->instance->releaseResources();
    }

    newSlot.reset();

    if (onChange)
        onChange();
}

void HostedInstrument::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    blockSize = juce::jmax (1, maxBlockSize);
    prepared = true;

    const juce::SpinLock::ScopedLockType l (lock);
    if (slot != nullptr)
    {
        slot->instance->releaseResources();
        prepareSlot (*slot);
    }
}

void HostedInstrument::release()
{
    prepared = false;

    const juce::SpinLock::ScopedLockType l (lock);
    if (slot != nullptr)
        slot->instance->releaseResources();
}

bool HostedInstrument::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept
{
    const juce::SpinLock::ScopedTryLockType l (lock);
    if (! l.isLocked() || slot == nullptr)
        return false;

    auto& s = *slot;
    const int numSamples = out.getNumSamples();
    if (numSamples > s.scratch.getNumSamples())
        return false;

    juce::AudioBuffer<float> view (s.scratch.getArrayOfWritePointers(), s.scratch.getNumChannels(), numSamples);
    view.clear();

    s.midi.clear();
    s.midi.addEvents (midi, 0, numSamples, 0);

    s.instance->processBlock (view, s.midi);

    const int hostedOuts = juce::jmax (1, s.instance->getTotalNumOutputChannels());
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        out.addFrom (ch, 0, view, juce::jmin (ch, hostedOuts - 1), 0, numSamples);

    const float peak = view.getMagnitude (0, numSamples);
    if (peak > outputPeak.load())
        outputPeak.store (peak);

    return true;
}

void HostedInstrument::showEditor()
{
    if (slot == nullptr)
        return;

    if (editorWindow != nullptr)
    {
        editorWindow->toFront (true);
        return;
    }

    auto& instance = *slot->instance;
    juce::AudioProcessorEditor* editor = instance.createEditorIfNeeded();
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (instance);

    std::weak_ptr<bool> weakAlive = alive;
    editorWindow = std::make_unique<HostedWindow> (slot->description.name, editor, [this, weakAlive]
    {
        juce::MessageManager::callAsync ([this, weakAlive]
        {
            if (auto a = weakAlive.lock(); a != nullptr && *a)
                closeEditor();
        });
    });
}

void HostedInstrument::closeEditor()
{
    editorWindow.reset();
}

std::unique_ptr<juce::XmlElement> HostedInstrument::saveState() const
{
    if (slot == nullptr)
        return pendingRestore != nullptr ? std::make_unique<juce::XmlElement> (*pendingRestore) : nullptr;

    auto xml = std::make_unique<juce::XmlElement> ("Hosted");
    xml->addChildElement (slot->description.createXml().release());

    juce::MemoryBlock state;
    slot->instance->getStateInformation (state);
    xml->setAttribute ("state", state.toBase64Encoding());
    return xml;
}

void HostedInstrument::restoreState (const juce::XmlElement& xml, std::function<void (const juce::String&)> done)
{
    const auto* descXml = xml.getChildByName ("PLUGIN");
    juce::PluginDescription desc;
    if (descXml == nullptr || ! desc.loadFromXml (*descXml))
    {
        if (done) done ("No plugin description in saved state");
        return;
    }

    juce::MemoryBlock state;
    state.fromBase64Encoding (xml.getStringAttribute ("state"));

    pendingRestore = std::make_unique<juce::XmlElement> (xml);
    restoreError.clear();
    if (log) log ("restore: loading " + desc.name + " (" + desc.pluginFormatName + "), state " + juce::String ((int) state.getSize()) + " bytes");

    std::weak_ptr<bool> weakAlive = alive;
    load (desc, [this, weakAlive, name = desc.name, done = std::move (done)] (const juce::String& error)
    {
        if (auto a = weakAlive.lock(); a == nullptr || ! *a)
            return;

        if (error.isEmpty())
        {
            pendingRestore.reset();
            restoreInfo = "restored from the saved set at " + juce::Time::getCurrentTime().formatted ("%H:%M:%S");
        }
        else
        {
            restoreError = error;
        }

        if (log) log ("restore: " + name + (error.isEmpty() ? juce::String (" loaded") : " FAILED: " + error));
        if (done) done (error);
    }, &state);
}
