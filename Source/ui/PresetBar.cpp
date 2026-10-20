#include "PresetBar.h"
#include "../plugin/ChordismProcessor.h"

namespace chordism::ui
{
    namespace
    {
        // The browser in the call-out: a search box, the presets that match, and Import MIDI.
        class PresetBrowser : public juce::Component,
                              private juce::ListBoxModel
        {
        public:
            PresetBrowser (ChordismProcessor& p, std::function<void()> importMidi) : processor (p), onImport (std::move (importMidi))
            {
                processor.presets.rescan();
                search.setTextToShowWhenEmpty ("Search presets", colours::faint);
                search.setColour (juce::TextEditor::backgroundColourId, colours::raised);
                search.setColour (juce::TextEditor::textColourId, colours::text);
                search.setColour (juce::TextEditor::outlineColourId, colours::outline);
                search.onTextChange = [this] { filter(); };
                search.onReturnKey = [this] { load (list.getSelectedRow() >= 0 ? list.getSelectedRow() : 0); };
                addAndMakeVisible (search);

                list.setModel (this);
                list.setRowHeight (22);
                list.setColour (juce::ListBox::backgroundColourId, colours::panel);
                list.setColour (juce::ListBox::outlineColourId, colours::outline);
                addAndMakeVisible (list);

                importButton.onClick = [this]
                {
                    auto import = onImport;
                    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                        box->dismiss();
                    if (import)
                        import();
                };
                importButton.setTooltip ("Make the chord zone's chords from a MIDI file's chords (you can also drop a .mid on Chordism)");
                addAndMakeVisible (importButton);
                folderButton.onClick = [] { PresetManager::userFolder().createDirectory(); PresetManager::userFolder().revealToUser(); };
                folderButton.setTooltip (PresetManager::userFolder().getFullPathName());
                addAndMakeVisible (folderButton);

                filter();
                setSize (320, 420);
            }

            void resized() override
            {
                auto r = getLocalBounds().reduced (6);
                search.setBounds (r.removeFromTop (26));
                r.removeFromTop (6);
                auto buttons = r.removeFromBottom (26);
                importButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (0, 0).withTrimmedRight (3));
                folderButton.setBounds (buttons.withTrimmedLeft (3));
                r.removeFromBottom (6);
                list.setBounds (r);
            }

            void visibilityChanged() override
            {
                if (isShowing())
                    search.grabKeyboardFocus();
            }

        private:
            void filter()
            {
                rows.clear();
                const auto words = juce::StringArray::fromTokens (search.getText(), true);
                const auto& entries = processor.presets.getEntries();
                for (int i = 0; i < entries.size(); ++i)
                {
                    const auto label = entries[i].label();
                    bool match = true;
                    for (const auto& w : words)
                        match &= label.containsIgnoreCase (w);
                    if (match)
                        rows.add (i);
                }
                list.updateContent();
                const int current = processor.presets.current();
                list.selectRow (rows.indexOf (current), true);
                list.repaint();
            }

            void load (int row)
            {
                if (row < 0 || row >= rows.size())
                    return;
                processor.presets.load (rows[row]);
                if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                    box->dismiss();
            }

            int getNumRows() override { return rows.size(); }

            void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
            {
                if (row < 0 || row >= rows.size())
                    return;
                const auto& e = processor.presets.getEntries().getReference (rows[row]);
                if (selected)
                    g.fillAll (colours::chord.withAlpha (0.25f));
                g.setFont (font (12.0f));
                g.setColour (colours::faint);
                g.drawText (e.category, 8, 0, 92, height, juce::Justification::centredLeft, true);
                g.setFont (font (13.5f, selected));
                g.setColour (colours::text);
                g.drawText (e.name, 104, 0, width - 110, height, juce::Justification::centredLeft, true);
            }

            void listBoxItemClicked (int row, const juce::MouseEvent&) override { load (row); }
            void returnKeyPressed (int row) override { load (row); }

            ChordismProcessor& processor;
            std::function<void()> onImport;
            juce::TextEditor search;
            juce::ListBox list;
            juce::TextButton importButton { "Import MIDI chords..." }, folderButton { "Show folder" };
            juce::Array<int> rows;
        };
    }

    //==========================================================================
    PresetBar::PresetBar (ChordismProcessor& p, std::function<void (const juce::File&)> import)
        : processor (p), importMidi (std::move (import))
    {
        previous.setTooltip ("Previous preset");
        next.setTooltip ("Next preset");
        name.setTooltip ("Presets: click to browse and search, or import a MIDI file's chords");
        save.setTooltip ("Save the current settings as a preset");
        previous.onClick = [this] { processor.presets.step (-1); refresh(); };
        next.onClick = [this] { processor.presets.step (1); refresh(); };
        name.onClick = [this] { openBrowser(); };
        save.onClick = [this] { askToSave(); };
        for (auto* b : { &previous, &next, &name, &save })
        {
            b->setColour (juce::TextButton::buttonColourId, colours::raised);
            addAndMakeVisible (b);
        }
        refresh();
    }

    PresetBar::~PresetBar() = default;

    void PresetBar::refresh()
    {
        const auto& category = processor.getPresetCategory();
        const auto& presetName = processor.getPresetName();
        const auto text = category.isEmpty() || category == presetName ? presetName : category + " / " + presetName;
        if (text != shown)
        {
            shown = text;
            name.setButtonText (text);
        }
    }

    void PresetBar::paint (juce::Graphics& g)
    {
        g.setColour (colours::dim);
        g.setFont (font (11.0f, true));
        g.drawText ("PRESET", getLocalBounds().removeFromTop (14), juce::Justification::centredLeft);
    }

    void PresetBar::resized()
    {
        auto r = getLocalBounds().withTrimmedTop (14);
        previous.setBounds (r.removeFromLeft (22));
        save.setBounds (r.removeFromRight (42).withTrimmedLeft (3));
        next.setBounds (r.removeFromRight (22));
        name.setBounds (r.reduced (2, 0));
    }

    void PresetBar::openBrowser()
    {
        auto content = std::make_unique<PresetBrowser> (processor, [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Import a MIDI file's chords", juce::File(), "*.mid;*.midi");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult().existsAsFile() && importMidi)
                                          importMidi (fc.getResult());
                                  });
        });
        auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
        juce::CallOutBox::launchAsynchronously (std::move (content),
                                                editor != nullptr ? editor->getLocalArea (this, name.getBounds()) : name.getScreenBounds(), editor);
    }

    void PresetBar::askToSave()
    {
        saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "Saved in " + PresetManager::userFolder().getFullPathName(),
                                                          juce::MessageBoxIconType::NoIcon, this);
        const auto& current = processor.getPresetName();
        saveWindow->addTextEditor ("name", current == "Init" ? juce::String() : current, "Name");
        auto categories = PresetManager::categories();
        saveWindow->addComboBox ("category", categories, "Category");
        if (auto* box = saveWindow->getComboBoxComponent ("category"))
            box->setSelectedItemIndex (juce::jmax (0, categories.indexOf (processor.getPresetCategory() == "Init" ? juce::String ("User")
                                                                                                                   : processor.getPresetCategory())));
        saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<PresetBar> (this)] (int result)
        {
            auto* self = safe.getComponent();
            if (self == nullptr)
                return;
            auto& saveWindow = self->saveWindow;
            auto& processor = self->processor;
            if (result == 1 && saveWindow != nullptr)
            {
                const auto text = saveWindow->getTextEditorContents ("name");
                auto* box = saveWindow->getComboBoxComponent ("category");
                const auto category = box != nullptr ? box->getText() : juce::String ("User");
                const auto saved = processor.presets.save (text, category);
                if (saved.failed())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset not saved", saved.getErrorMessage(), {}, self);
                self->refresh();
            }
            saveWindow.reset();
        }), false);
    }

    //==========================================================================
    ExportHandle::ExportHandle (ChordismProcessor& p) : processor (p)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        refresh();
    }

    void ExportHandle::refresh()
    {
        const auto info = processor.exportInfo();
        const int bars = juce::jmin (info.completedBars(), ExportRecorder::maxBars);
        if (bars != barCount || info.recording != recording)
        {
            barCount = bars;
            recording = info.recording;
            setTooltip (barCount > 0 ? "Drag into the host: the last " + juce::String (barCount) + (barCount == 1 ? " bar" : " bars")
                                           + " Chordism played (up to 32 completed bars since playback started), as a MIDI clip"
                                     : "Export: play at least one whole bar with the host's transport running, then drag this into a track");
            repaint();
        }
    }

    void ExportHandle::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().withTrimmedTop (14.0f);
        g.setColour (colours::dim);
        g.setFont (font (11.0f, true));
        g.drawText ("EXPORT", getLocalBounds().removeFromTop (14), juce::Justification::centredLeft);

        const bool enabled = barCount > 0;
        g.setColour (enabled ? colours::raised : colours::panel);
        g.fillRoundedRectangle (r.reduced (1.0f), 4.0f);
        g.setColour (enabled ? colours::chord : colours::outline);
        g.drawRoundedRectangle (r.reduced (1.0f), 4.0f, dragging ? 2.0f : 1.0f);

        // Grip dots, then the bar count.
        auto grip = r.removeFromLeft (16.0f).reduced (5.0f, 7.0f);
        g.setColour (enabled ? colours::chord : colours::faint);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                g.fillEllipse (grip.getX() + (float) col * 4.0f, grip.getY() + (float) row * grip.getHeight() / 2.5f, 2.5f, 2.5f);
        g.setFont (font (12.5f, true));
        g.setColour (enabled ? colours::text : colours::faint);
        g.drawFittedText (enabled ? juce::String (barCount) + (barCount == 1 ? " bar" : " bars") : juce::String ("no bars"),
                          r.toNearestInt().withTrimmedRight (4), juce::Justification::centred, 1);
        if (recording)
        {
            g.setColour (colours::sounding);
            g.fillEllipse (r.getRight() - 9.0f, r.getY() + 4.0f, 5.0f, 5.0f);
        }
    }

    void ExportHandle::mouseDown (const juce::MouseEvent&)
    {
        dragging = false;
        refresh();
    }

    void ExportHandle::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging || barCount <= 0 || e.getDistanceFromDragStart() < 4)
            return;
        juce::String error;
        const auto file = processor.writeExport (error);
        if (! file.existsAsFile())
        {
            setTooltip (error);
            return;
        }
        dragging = true;
        repaint();
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                    [safe = juce::Component::SafePointer<ExportHandle> (this)]
        {
            if (safe != nullptr)
            {
                safe->dragging = false;
                safe->repaint();
            }
        });
    }

    void ExportHandle::mouseUp (const juce::MouseEvent&)
    {
        dragging = false;
        repaint();
    }
}
