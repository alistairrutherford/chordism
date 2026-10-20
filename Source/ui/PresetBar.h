// The top bar's preset browser and Export handle (PLAN.md §4.6, §4.7, §6).
//
//   PresetBar      ◀ preset name ▶ · Save. The name opens the browser: a search box over every
//                  preset (factory, then the user's files by category), click to load; and
//                  "Import MIDI chords…" (as dropping a .mid on the editor). Save asks for a name
//                  and a category and writes ~/Music/Chordism/Presets/<Category>/<Name>.xml.
//   ExportHandle   drag it into the host: the last ≤ 32 completed bars of what Chordism sent
//                  since playback started, as a .mid (disabled until the first bar has
//                  finished; its tooltip says why).
#pragma once

#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace chordism
{
    class ChordismProcessor;
}

namespace chordism::ui
{
    class PresetBar : public juce::Component
    {
    public:
        // `importMidi` runs a MIDI import the way a drop on the editor does.
        PresetBar (ChordismProcessor&, std::function<void (const juce::File&)> importMidi);
        ~PresetBar() override;

        void refresh();                   // the shown name, from the processor
        void resized() override;
        void paint (juce::Graphics&) override;

        void openBrowser();
        void askToSave();

    private:
        ChordismProcessor& processor;
        std::function<void (const juce::File&)> importMidi;
        juce::TextButton previous { juce::String::fromUTF8 ("\xe2\x97\x80") }, next { juce::String::fromUTF8 ("\xe2\x96\xb6") };
        juce::TextButton name, save { "Save" };
        std::unique_ptr<juce::FileChooser> chooser;
        std::unique_ptr<juce::AlertWindow> saveWindow;
        juce::String shown;
    };

    class ExportHandle : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        explicit ExportHandle (ChordismProcessor&);

        void refresh();                   // the bar count, from the recorder
        int bars() const noexcept { return barCount; }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        ChordismProcessor& processor;
        int barCount = 0;
        bool recording = false, dragging = false;
    };
}
