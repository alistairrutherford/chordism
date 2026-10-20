// Editor snapshot test (PLAN.md §7 row 5, §8). Runs the real processor and editor without a
// host and checks:
//
//   1. snapshots  the editor in a fixed state (C major, the Pop progression, edits, effects, keys
//                 held), its Chord Editor page and its Setup overlay at 1100×640, and the editor
//                 at its smallest size (960×600, keyboard hidden), each
//                 compared with Tests/fixtures/snapshot/*.png (at most 0.5% of pixels may differ
//                 by more than 24 levels: font rendering moves a little between macOS versions)
//   2. reachable  every host parameter is bound to a control of the editor (going through the
//                 five pattern slots and the four sequencer patterns, as a user would)
//   3. state      a saved state restores every parameter, the Chord Editor's edits and the UI's
//                 preferences into a new instance; a state from before Phase 4 (64 step bools)
//                 restores the same sequencer patterns
//   4. Phase 6    presets (every factory program loads its values over the defaults, Init is
//                 the defaults, a preset's state round-trips into a new instance without touching
//                 routing or the test rig, a load ends every note); MIDI import from a file (the
//                 parameters, edits and chords it gives); Export under a playing transport (the
//                 handle's bar count and the file); QWERTY (both layouts, and a key press through
//                 the editor plays its chord)
//
//   ChordismSnapshot <fixtures dir> <output dir> [--update]
//
// --update writes new reference images. The images of this run are always written to the
// output dir, for a look.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../../Source/plugin/ChordismProcessor.h"
#include "../../Source/ui/ChordismEditor.h"
#include "../../Source/ui/ChordEditorPanel.h"
#include "../../Source/core/midiio/MidiFile.h"
#include <iostream>

using namespace chordism;

namespace
{
    int failures = 0;

    // Records whether the processor told the host its non-parameter state changed.
    struct StateWatch : juce::AudioProcessorListener
    {
        bool changed = false;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override { changed |= d.nonParameterStateChanged; }
    };


    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
        failures += ok ? 0 : 1;
    }

    void set (ChordismProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.state.getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
    }

    float get (ChordismProcessor& p, const juce::String& id) { return p.state.getRawParameterValue (id)->load(); }

    // A block of the processor with these MIDI events at sample 0; returns its output.
    juce::MidiBuffer block (ChordismProcessor& p, std::initializer_list<juce::MidiMessage> events)
    {
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        for (const auto& e : events)
            midi.addEvent (e, 0);
        p.processBlock (audio, midi);
        return midi;
    }

    std::vector<int> noteOns (const juce::MidiBuffer& midi)
    {
        std::vector<int> n;
        for (const auto m : midi)
            if (m.getMessage().isNoteOn())
                n.push_back (m.getMessage().getNoteNumber());
        std::sort (n.begin(), n.end());
        return n;
    }

    int noteOffs (const juce::MidiBuffer& midi)
    {
        int n = 0;
        for (const auto m : midi)
            n += m.getMessage().isNoteOff() ? 1 : 0;
        return n;
    }

    // A host transport for the processor (Export records only while it plays).
    struct PlayHead : juce::AudioPlayHead
    {
        bool playing = false;
        double ppq = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            info.setPpqPosition (ppq);
            info.setBpm (120.0);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            return info;
        }
    };

    // Fraction of pixels that differ by more than `tolerance` in any channel (1 if sizes differ).
    double difference (const juce::Image& a, const juce::Image& b, int tolerance)
    {
        if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
            return 1.0;
        const juce::Image::BitmapData da (a, juce::Image::BitmapData::readOnly), db (b, juce::Image::BitmapData::readOnly);
        long differing = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
            {
                const auto ca = da.getPixelColour (x, y), cb = db.getPixelColour (x, y);
                const int d = juce::jmax (std::abs (ca.getRed() - cb.getRed()), std::abs (ca.getGreen() - cb.getGreen()),
                                          std::abs (ca.getBlue() - cb.getBlue()));
                differing += d > tolerance ? 1 : 0;
            }
        return (double) differing / ((double) a.getWidth() * a.getHeight());
    }

    void snapshot (ChordismEditor& editor, const juce::String& name, const juce::File& fixtures, const juce::File& out, bool update)
    {
        editor.refreshNow();
        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        const auto write = [&image] (const juce::File& f)
        {
            f.deleteFile();
            juce::FileOutputStream s (f);
            juce::PNGImageFormat().writeImageToStream (image, s);
        };
        write (out.getChildFile (name + ".png"));

        const auto reference = fixtures.getChildFile (name + ".png");
        if (update || ! reference.existsAsFile())
        {
            write (reference);
            check (true, name + ": reference written (" + reference.getFullPathName() + ")");
            return;
        }
        const double d = difference (image, juce::ImageFileFormat::loadFrom (reference), 24);
        check (d <= 0.005, name + ": " + juce::String (d * 100.0, 3) + "% of pixels differ from the reference");
    }

    // The fixed state the snapshots show.
    void arrange (ChordismProcessor& p)
    {
        set (p, params::progression, (float) (int) Progression::Pop);
        set (p, params::chordGroup, (float) (int) ChordGroup::Sevenths);
        set (p, params::arp, (float) (int) ArpPattern::ConvergeUp);
        set (p, params::velocity, (float) (int) VelocityCurve::House);
        set (p, params::seq, 1.0f);
        set (p, params::seqPattern, 2.0f);
        set (p, params::mapping, (float) (int) Mapping::Dynamic);
        set (p, params::patternSlot, 2.0f);
        set (p, params::smartSeqPattern, 1.0f);
        set (p, params::patternArp (2), (float) (numArpPatterns - 1));   // slot 3 in Melody mode (Phase 6)
        set (p, params::patternRhythm (2), (float) (int) MelodyRhythm::Clave);
        set (p, params::port, 0.0f);
        // Edits on degree 5 (V): a flat 9th, the 7th an octave up as well.
        p.chordEditor.setOn (ChordGroup::Sevenths, Scale::Major, 4, 4, true);
        p.chordEditor.setAccidental (ChordGroup::Sevenths, Scale::Major, 4, 4, -1);
        p.chordEditor.setOctave (ChordGroup::Sevenths, Scale::Major, 4, 3, 1, true);
        p.prepareToPlay (48000.0, 512);
        block (p, {});
        // Hold the E key (Pop: vi), a Smart Scale key and pattern key 2.
        block (p, { juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100), juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100),
                    juce::MidiMessage::noteOn (1, 63, (juce::uint8) 100) });
        block (p, {});
    }
}

namespace
{
    bool equalParams (ChordismProcessor& a, ChordismProcessor& b, juce::StringArray& differ, bool presetOnly)
    {
        for (auto* param : a.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                if ((! presetOnly || params::isPresetParameter (withId->paramID)) && get (a, withId->paramID) != get (b, withId->paramID))
                    differ.add (withId->paramID);
        return differ.isEmpty();
    }

    void testPhase6()
    {
        std::cout << "== presets ==\n";
        {
            auto p = std::make_unique<ChordismProcessor>();
            auto fresh = std::make_unique<ChordismProcessor>();
            p->prepareToPlay (48000.0, 512);
            const auto& factory = factoryPresets();
            check (p->getNumPrograms() == (int) factory.size() && factory.size() >= 21 && p->getProgramName (0) == "Init",
                   juce::String (factory.size() - 1) + " factory presets, plus Init as program 0");
            int wrong = 0;
            juce::String firstWrong;
            for (int i = 1; i < (int) factory.size(); ++i)
            {
                p->setCurrentProgram (i);
                for (const auto& [id, value] : factory[(size_t) i].values)
                    if (std::abs (get (*p, id) - value) > 1.0e-3f)
                    {
                        if (wrong++ == 0)
                            firstWrong = factory[(size_t) i].name + ": " + id + " = " + juce::String (get (*p, id)) + ", want " + juce::String (value);
                    }
                // Everything it doesn't set is the default.
                for (auto* param : p->getParameters())
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
                    {
                        bool listed = false;
                        for (const auto& kv : factory[(size_t) i].values)
                            listed |= kv.first == r->paramID;
                        if (! listed && std::abs (r->getValue() - r->getDefaultValue()) > 1.0e-6f && params::isPresetParameter (r->paramID))
                            if (wrong++ == 0)
                                firstWrong = factory[(size_t) i].name + ": " + r->paramID + " isn't its default";
                    }
                if (p->getCurrentProgram() != i || p->getPresetName() != factory[(size_t) i].name)
                    if (wrong++ == 0)
                        firstWrong = factory[(size_t) i].name + ": program " + juce::String (p->getCurrentProgram());
            }
            check (wrong == 0, "every factory program sets its values over the defaults" + (firstWrong.isEmpty() ? juce::String() : " (" + firstWrong + ")"));
            p->setCurrentProgram (0);
            juce::StringArray differ;
            check (equalParams (*p, *fresh, differ, false) && p->getPresetName() == "Init", "Init is the defaults (" + differ.joinIntoString (", ") + ")");

            // A user preset's state into a new instance; routing and the test rig stay.
            set (*p, params::key, 5.0f);
            set (*p, params::scale, (float) (int) Scale::Dorian);
            set (*p, params::chordGroup, (float) (int) ChordGroup::Ninths);
            set (*p, params::patternArp (1), (float) (numArpPatterns - 1));
            set (*p, params::melodySeed, 321.0f);
            set (*p, params::seqSteps (2), (float) 0x1234);
            p->chordEditor.setAccidental (ChordGroup::Ninths, Scale::Dorian, 3, 2, 1);
            const auto tree = p->presetState();
            auto other = std::make_unique<ChordismProcessor>();
            set (*other, params::port, 0.0f);
            set (*other, params::testPattern, 1.0f);
            other->applyPreset (tree, "User", "Mine");
            differ.clear();
            check (equalParams (*p, *other, differ, true) && other->chordEditor.getEdits().sameEdits (p->chordEditor.getEdits())
                       && get (*other, params::port) == 0.0f && get (*other, params::testPattern) == 1.0f && other->getPresetName() == "Mine",
                   "a preset's state loads every preset parameter and the edits, and leaves the port and the test pattern ("
                       + differ.joinIntoString (", ") + ")");
            check (! tree.getChildWithProperty ("id", params::port).isValid() && ! tree.getChildWithProperty ("id", params::testPattern).isValid()
                       && ! tree.getChildWithName ("UI").isValid(),
                   "a preset doesn't hold the routing, the test rig or the editor's preferences");

            // A load ends every note.
            block (*p, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) });
            p->setCurrentProgram (3);
            const auto out = block (*p, {});
            check (noteOffs (out) >= 3 && p->getPresetName() == factory[3].name, "loading a preset while a chord sounds ends its notes ("
                                                                                     + juce::String (noteOffs (out)) + " note-offs)");

            // The program and the name are saved with the plug-in.
            juce::MemoryBlock saved;
            p->getStateInformation (saved);
            auto restored = std::make_unique<ChordismProcessor>();
            restored->setStateInformation (saved.getData(), (int) saved.getSize());
            check (restored->getCurrentProgram() == 3 && restored->getPresetName() == factory[3].name,
                   "the current program and preset name restore with the plug-in's state");
        }

        std::cout << "== MIDI import ==\n";
        {
            auto p = std::make_unique<ChordismProcessor>();
            p->prepareToPlay (48000.0, 512);
            set (*p, params::chordGroup, (float) (int) ChordGroup::Sevenths);
            MidiSong song;
            const std::vector<std::vector<int>> chords = { { 45, 48, 52, 55 }, { 50, 53, 57, 60 }, { 43, 47, 50, 53, 57 }, { 48, 52, 55, 59 } };
            int64_t t = 0;
            for (const auto& c : chords)
            {
                for (int n : c)
                    song.notes.push_back ({ t, t + 900, (uint8_t) n, 100, 0 });
                t += 960;
            }
            const auto bytes = writeMidiFile (song);
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("chordism-snapshot-import.mid");
            file.replaceWithData (bytes.data(), bytes.size());
            const auto result = p->importMidiFile (file);
            file.deleteFile();
            std::vector<std::vector<int>> played;
            for (int n = 0; n < 4; ++n)
            {
                const int key = 48 + std::array<int, 4> { 0, 2, 4, 5 }[(size_t) n];
                played.push_back (noteOns (block (*p, { juce::MidiMessage::noteOn (1, key, (juce::uint8) 100) })));
                block (*p, { juce::MidiMessage::noteOff (1, key) });
            }
            check (result.ok && (int) get (*p, params::progression) == (int) Progression::Custom && played == chords
                       && p->chordEditor.getEdits().group == ChordGroup::Sevenths,
                   "a dropped .mid sets Key, Scale, Custom progression and edits; keys C D E F play its chords (" + juce::String (result.message) + ")");
            juce::MemoryBlock garbage ("not midi", 8);
            const auto bad = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("chordism-snapshot-bad.mid");
            bad.replaceWithData (garbage.getData(), garbage.getSize());
            const auto refused = p->importMidiFile (bad);
            bad.deleteFile();
            check (! refused.ok && (int) get (*p, params::progression) == (int) Progression::Custom, "a file that isn't MIDI is refused, nothing changes ("
                                                                                                      + juce::String (refused.message) + ")");
        }

        std::cout << "== Export ==\n";
        {
            auto p = std::make_unique<ChordismProcessor>();
            PlayHead head;
            p->setPlayHead (&head);
            p->prepareToPlay (48000.0, 512);
            set (*p, params::testPattern, 1.0f);
            set (*p, params::port, 0.0f);
            std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorIfNeeded());
            auto* editor = dynamic_cast<ChordismEditor*> (base.get());
            juce::String error;
            const bool emptyAtFirst = ! p->writeExport (error).existsAsFile();
            head.playing = true;
            while (head.ppq < 9.0)
            {
                block (*p, {});
                head.ppq += 512.0 / 24000.0;
            }
            editor->refreshNow();
            const auto file = p->writeExport (error);
            MidiSong song;
            std::string readError;
            juce::MemoryBlock data;
            file.loadFileAsData (data);
            const bool read = readMidiFile ((const uint8_t*) data.getData(), data.getSize(), song, readError);
            check (emptyAtFirst && file.existsAsFile() && read && song.notes.size() == 24 && song.ppq == 960 && song.notes.front().start == 0
                       && song.notes.back().start == 7 * 960 && file.getFileName() == "Chordism C Major 2 bars.mid",
                   "after 2.25 bars playing, the handle exports 2 bars (8 chords) as " + file.getFileName());
            head.playing = false;
            block (*p, {});
            editor = nullptr;
            base.reset();
            p->setPlayHead (nullptr);
        }

        std::cout << "== QWERTY ==\n";
        {
            using Q = ChordismEditor::Qwerty;
            check (ChordismEditor::qwertyNote (Q::Piano, 'a', 60, 0) == 48 && ChordismEditor::qwertyNote (Q::Piano, 'w', 60, 0) == 49
                       && ChordismEditor::qwertyNote (Q::Piano, 'j', 60, 0) == 59 && ChordismEditor::qwertyNote (Q::Piano, 'k', 60, 0) == 60
                       && ChordismEditor::qwertyNote (Q::Piano, '\'', 60, 0) == 65 && ChordismEditor::qwertyNote (Q::Piano, 'A', 60, 1) == 60
                       && ChordismEditor::qwertyNote (Q::Piano, 'q', 60, 0) == -1 && ChordismEditor::qwertyNote (Q::Off, 'a', 60, 0) == -1,
                   "Piano layout: A-J the chord zone from the C below the split, K the split's C, W the first shifter; an octave up moves it");
            check (ChordismEditor::qwertyNote (Q::TwoRows, 'z', 60, 0) == 48 && ChordismEditor::qwertyNote (Q::TwoRows, 'q', 60, 0) == 60
                       && ChordismEditor::qwertyNote (Q::TwoRows, '2', 60, 0) == 61 && ChordismEditor::qwertyNote (Q::TwoRows, 'm', 60, 0) == 59
                       && ChordismEditor::qwertyNote (Q::TwoRows, 'p', 64, 0) == 76,
                   "Two rows: Z-M the octave below the split, Q-P the split's (from its C)");

            auto p = std::make_unique<ChordismProcessor>();
            p->prepareToPlay (48000.0, 512);
            std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditorIfNeeded());
            auto* editor = dynamic_cast<ChordismEditor*> (base.get());
            editor->keyPressed (juce::KeyPress ('d', 0, 'd'));
            const auto chord = noteOns (block (*p, {}));
            editor->keyStateChanged (false);   // the key isn't down: its note-off
            const auto release = block (*p, {});
            check (chord == std::vector<int> { 52, 55, 59 } && noteOffs (release) == 3, "pressing D on the computer keyboard plays chord 3 (Em) and releasing it ends it");
        }
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3)
    {
        std::cerr << "usage: ChordismSnapshot <fixtures dir> <output dir> [--update]\n";
        return 2;
    }
    const juce::File fixtures (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    const bool update = argc > 3 && juce::String (argv[3]) == "--update";
    fixtures.createDirectory();
    out.createDirectory();

    std::cout << "== snapshots ==\n";
    auto processor = std::make_unique<ChordismProcessor>();
    arrange (*processor);
    {
        std::unique_ptr<juce::AudioProcessorEditor> base (processor->createEditorIfNeeded());
        auto* editor = dynamic_cast<ChordismEditor*> (base.get());
        check (editor != nullptr, "the editor opens");
        if (editor == nullptr)
            return 1;
        editor->setSize (ChordismEditor::defaultWidth, ChordismEditor::defaultHeight);
        editor->showKeyboard (true);
        editor->showEditorPage (false);
        snapshot (*editor, "editor", fixtures, out, update);
        editor->showEditorPage (true);
        snapshot (*editor, "editor-chord-editor", fixtures, out, update);
        editor->showEditorPage (false);
        editor->showSetup (true);
        snapshot (*editor, "editor-setup", fixtures, out, update);
        editor->showSetup (false);
        editor->setSize (960, 600);   // the smallest size, keyboard hidden
        editor->showKeyboard (false);
        snapshot (*editor, "editor-small", fixtures, out, update);
        editor->showKeyboard (true);
        editor->setSize (ChordismEditor::defaultWidth, ChordismEditor::defaultHeight);

        std::cout << "== Chord Editor clicks ==\n";
        {
            editor->setSize (960, 600);   // the size it opened at in Live
            editor->showEditorPage (true);
            editor->refreshNow();
            ui::ChordEditorPanel* panel = nullptr;
            for (auto* c : editor->getChildren())
                if (auto* p = dynamic_cast<ui::ChordEditorPanel*> (c))
                    panel = p;
            check (panel != nullptr && panel->isVisible(), "the Chord Editor page is shown");
            if (panel != nullptr)
            {
                // Click the middle of key 1's 9th (its degree under Pop is I): it turns on.
                const auto before = processor->chordEditor.getEdits();
                StateWatch watch;
                processor->addListener (&watch);
                const auto at = panel->stepBounds (0, 4).getCentre();
                auto source = juce::Desktop::getInstance().getMainMouseSource();
                const auto now = juce::Time::getCurrentTime();
                const juce::MouseEvent e (source, at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, panel, panel, now, at, now, 1, false);
                panel->mouseDown (e);
                // The host keeps processing and the processor's timer runs (it clears edits after a
                // Chord Group change): the edit must survive both.
                for (int i = 0; i < 20; ++i)
                {
                    block (*processor, {});
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                }
                processor->removeListener (&watch);
                check (watch.changed, "the host is told the plug-in's state changed (so Live saves the edit)");
                const auto after = processor->chordEditor.getEdits();
                check (! before.degrees[0].edited && after.degrees[0].edited && after.degrees[0].steps[4].on && after.group == ChordGroup::Sevenths,
                       "clicking a step turns it on in the Chord Editor's state");
                juce::MemoryBlock withEdit;
                processor->getStateInformation (withEdit);
                const auto xml = juce::AudioProcessor::getXmlFromBinary (withEdit.getData(), (int) withEdit.getSize());
                check (xml != nullptr && xml->getChildByName ("ChordEditor") != nullptr
                           && xml->getChildByName ("ChordEditor")->getNumChildElements() == 2,
                       "and the saved state has the edited degrees");
                processor->chordEditor.resetDegree (0);
            }
            editor->showEditorPage (false);
            editor->setSize (ChordismEditor::defaultWidth, ChordismEditor::defaultHeight);
        }

        std::cout << "== every parameter reachable ==\n";
        for (int slot = 0; slot < numPatternSlots; ++slot)
        {
            set (*processor, params::patternSlot, (float) slot);
            editor->refreshNow();
        }
        for (int pattern = 0; pattern < numSeqPatterns; ++pattern)
        {
            set (*processor, params::seqPattern, (float) pattern);
            set (*processor, params::smartSeqPattern, (float) pattern);
            editor->refreshNow();
        }
        juce::StringArray missing;
        int total = 0;
        for (auto* param : processor->getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            {
                ++total;
                if (editor->boundParameters().count (withId->paramID) == 0)
                    missing.add (withId->paramID);
            }
        check (missing.isEmpty(), juce::String (total) + " parameters, " + juce::String (missing.size()) + " without a control"
                                      + (missing.isEmpty() ? juce::String() : ": " + missing.joinIntoString (", ")));
    }

    std::cout << "== state ==\n";
    processor->uiState.setProperty ("page", "editor", nullptr);
    juce::MemoryBlock saved;
    processor->getStateInformation (saved);
    auto restored = std::make_unique<ChordismProcessor>();
    StateWatch restoreWatch;
    restored->addListener (&restoreWatch);
    restored->setStateInformation (saved.getData(), (int) saved.getSize());
    for (int i = 0; i < 10; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    restored->removeListener (&restoreWatch);
    check (! restoreWatch.changed, "a restore doesn't report a change (the set isn't marked as edited when it opens)");
    int differing = 0;
    for (auto* param : processor->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            differing += get (*processor, withId->paramID) != get (*restored, withId->paramID) ? 1 : 0;
    check (differing == 0, "every parameter restores (" + juce::String (differing) + " differ)");
    check (restored->chordEditor.getEdits().sameEdits (processor->chordEditor.getEdits()) && restored->chordEditor.getEdits().any(),
           "the Chord Editor's edits restore");
    check (restored->uiState.getProperty ("page").toString() == "editor", "the editor's preferences restore");

    // A state saved before Phase 4: the sequencer steps as 64 bools.
    auto xml = juce::parseXML ("<Chordism><PARAM id=\"seq\" value=\"1.0\"/><PARAM id=\"seq2Step1\" value=\"1.0\"/>"
                               "<PARAM id=\"seq2Step2\" value=\"1.0\"/><PARAM id=\"seq2Step5\" value=\"0.0\"/>"
                               "<PARAM id=\"seq3Step16\" value=\"1.0\"/></Chordism>");
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    auto migrated = std::make_unique<ChordismProcessor>();
    migrated->setStateInformation (old.getData(), (int) old.getSize());
    check ((int) get (*migrated, params::seqSteps (1)) == 0x0003 && (int) get (*migrated, params::seqSteps (2)) == 0x8000
               && (int) get (*migrated, params::seqSteps (0)) == (int) defaultSeqPatterns()[0],
           "a pre-Phase 4 state's step bools become the packed patterns (missing patterns keep their defaults)");

    testPhase6();

    std::cout << (failures == 0 ? "\nsnapshot: all passed\n" : "\n*** snapshot: " + std::to_string (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}
