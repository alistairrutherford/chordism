#include "PluginEditor.h"
#include <unistd.h>

namespace
{
    const juce::StringArray keyNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    juce::String liveNoteName (int note)
    {
        // Live names MIDI 60 "C3".
        return note < 0 ? juce::String ("-") : juce::MidiMessage::getMidiNoteName (note, true, true, 3);
    }
}

//==============================================================================
void ExportHandle::setBars (int newBars)
{
    if (newBars != bars)
    {
        bars = newBars;
        repaint();
    }
}

void ExportHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool ready = bars > 0;

    g.setColour (ready ? juce::Colour (0xff2d6a4f) : juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (ready ? 1.0f : 0.5f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (ready ? "Drag MIDI out  (" + juce::String (bars) + " completed bar" + (bars == 1 ? ")" : "s)")
                      : juce::String ("Export: play at least one full bar first"),
                getLocalBounds(), juce::Justification::centred);
}

void ExportHandle::mouseDrag (const juce::MouseEvent&)
{
    if (dragging || bars == 0)
        return;

    dragging = true;
    const auto file = processor.recorder.writeTempFile();
    if (! file.existsAsFile())
    {
        dragging = false;
        return;
    }

    juce::Component::SafePointer<ExportHandle> safe (this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this, [safe]
    {
        if (safe != nullptr)
            safe->dragging = false;
    });
}

//==============================================================================
SpikeEditor::SpikeEditor (SpikeProcessor& p)
    : AudioProcessorEditor (&p), processor (p), exportHandle (p)
{
    title.setText ("Chordism  -  Phase 0 spike", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    addAndMakeVisible (title);

    status.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    status.setJustificationType (juce::Justification::topLeft);
    status.setColour (juce::Label::backgroundColourId, juce::Colour (0xff1e1e1e));
    addAndMakeVisible (status);

    syncToggle.setToggleState (processor.syncKey.load(), juce::dontSendNotification);
    syncToggle.onClick = [this]
    {
        const int key = keyBox.getSelectedItemIndex();
        processor.syncKey.store (syncToggle.getToggleState());
        processor.setKeyFromUi (juce::jmax (0, key));
    };
    addAndMakeVisible (syncToggle);

    keyBox.addItemList (keyNames, 1);
    keyBox.setSelectedItemIndex (processor.getEffectiveKey(), juce::dontSendNotification);
    keyBox.onChange = [this] { processor.setKeyFromUi (keyBox.getSelectedItemIndex()); };
    addAndMakeVisible (keyBox);

    testToggle.setToggleState (processor.testPattern.load(), juce::dontSendNotification);
    testToggle.onClick = [this] { processor.testPattern.store (testToggle.getToggleState()); };
    addAndMakeVisible (testToggle);

    hostTitle.setText ("Hosted instrument", juce::dontSendNotification);
    hostTitle.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (hostTitle);
    addAndMakeVisible (hostStatus);

    scanButton.onClick = [this]
    {
        hostStatus.setText ("Scanning...", juce::dontSendNotification);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SpikeEditor> (this)]
        {
            if (safe == nullptr) return;
            safe->processor.hosted.scan();
            safe->refreshPluginList();
            safe->hostStatus.setText (juce::String (safe->processor.hosted.getAvailable().size()) + " instruments found",
                                      juce::dontSendNotification);
        });
    };
    addAndMakeVisible (scanButton);
    addAndMakeVisible (pluginBox);

    loadButton.onClick = [this]
    {
        const int index = pluginBox.getSelectedItemIndex();
        const auto& list = processor.hosted.getAvailable();
        if (! juce::isPositiveAndBelow (index, list.size()))
            return;

        hostStatus.setText ("Loading " + list[index].name + "...", juce::dontSendNotification);
        processor.hosted.load (list[index], [safe = juce::Component::SafePointer<SpikeEditor> (this)] (const juce::String& error)
        {
            if (safe != nullptr)
                safe->hostStatus.setText (error.isEmpty() ? "Loaded" : "Error: " + error, juce::dontSendNotification);
        });
    };
    addAndMakeVisible (loadButton);

    unloadButton.onClick = [this] { processor.hosted.unload(); };
    addAndMakeVisible (unloadButton);

    editorButton.onClick = [this] { processor.hosted.showEditor(); };
    addAndMakeVisible (editorButton);

    addAndMakeVisible (exportHandle);

    refreshPluginList();
    setSize (680, 470);
    startTimerHz (10);
    timerCallback();
}

SpikeEditor::~SpikeEditor()
{
    stopTimer();
}

void SpikeEditor::refreshPluginList()
{
    pluginBox.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& d : processor.hosted.getAvailable())
        pluginBox.addItem (d.name + "  (" + d.pluginFormatName + ", " + d.manufacturerName + ")", id++);

    if (pluginBox.getNumItems() > 0)
        pluginBox.setSelectedItemIndex (0, juce::dontSendNotification);
}

void SpikeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2a2a));
}

void SpikeEditor::resized()
{
    auto r = getLocalBounds().reduced (14);

    title.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    status.setBounds (r.removeFromTop (92));
    r.removeFromTop (10);

    auto row = r.removeFromTop (26);
    syncToggle.setBounds (row.removeFromLeft (260));
    keyBox.setBounds (row.removeFromLeft (90));
    r.removeFromTop (6);
    testToggle.setBounds (r.removeFromTop (26));
    r.removeFromTop (14);

    hostTitle.setBounds (r.removeFromTop (22));
    row = r.removeFromTop (26);
    scanButton.setBounds (row.removeFromLeft (140));
    row.removeFromLeft (8);
    pluginBox.setBounds (row);
    r.removeFromTop (6);
    row = r.removeFromTop (26);
    loadButton.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (8);
    unloadButton.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (8);
    editorButton.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (12);
    hostStatus.setBounds (row);

    r.removeFromTop (18);
    exportHandle.setBounds (r.removeFromTop (48));
}

void SpikeEditor::timerCallback()
{
    auto& session = SharedSession::get();

    const double ppq = processor.currentPpq.load();
    const auto beat = (int64_t) std::floor (ppq + 1.0e-9);

    juce::String text;
    text << "Format: " << juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType)
         << "   Host: " << juce::PluginHostType().getHostDescription()
         << "   PID " << (int) getpid() << "\n"
         << "Instance #" << processor.instanceId << "   instances of this binary in this process: "
         << session.instanceCount.load() << "\n"
         << "Shared key: " << keyNames[session.key.load()] << "  (version " << (int) session.version.load()
         << ", last set by #" << session.lastWriterId.load() << ")\n"
         << "Transport: " << (processor.playing.load() ? "playing" : "stopped")
         << "   " << juce::String (processor.currentBpm.load(), 1) << " BPM"
         << "   bar " << (beat / 4 + 1) << " beat " << (beat % 4 + 1) << " (4/4)\n"
         << "MIDI: last in " << liveNoteName (processor.lastInputNote.load())
         << "   last out " << liveNoteName (processor.lastOutputNote.load())
         << "   note-ons sent " << processor.outputNoteOns.load();
    status.setText (text, juce::dontSendNotification);

    if (! keyBox.isPopupActive())
        keyBox.setSelectedItemIndex (processor.getEffectiveKey(), juce::dontSendNotification);

    if (processor.hosted.isLoaded() && ! hostStatus.getText().startsWith ("Error"))
        hostStatus.setText ("Playing: " + processor.hosted.getLoadedName(), juce::dontSendNotification);
    else if (! processor.hosted.isLoaded() && hostStatus.getText().startsWith ("Playing"))
        hostStatus.setText ("Nothing loaded", juce::dontSendNotification);

    exportHandle.setBars (processor.recorder.completedBars());
}
