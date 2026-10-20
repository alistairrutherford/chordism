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
//                 Enable or the test rig, a load ends every note); MIDI import from a file (the
//                 parameters, edits and chords it gives); Export under a playing transport (the
//                 handle's bar count and the file); QWERTY (both layouts, and a key press through
//                 the editor plays its chord)
//   5. Phase 8    hardening: no allocation in processBlock (a malloc hook counts every one on
//                 the audio thread) with everything on, two synced instances, bypass, transport
//                 and loop changes and on-screen keys; damaged saved states (garbage bytes, NaN
//                 and out-of-range values, nonsense Chord Editor edits) restore to sane
//                 parameters; zero-length and oversized blocks, mono output, bypass, a reset and
//                 a sample-rate change while notes sound, all-notes-off and SysEx from the host,
//                 an input flood (key-ups get through), and 64 instances made and destroyed
//
//   ChordismSnapshot <fixtures dir> <output dir> [--update | --no-pixels]
//   ChordismSnapshot --bench [seconds]
//   ChordismSnapshot --bench-ui [seconds [idle]]
//
// --update writes new reference images. --no-pixels renders the snapshots without comparing
// them (CI: the references are tied to this Mac's font rendering). The images of this run are
// always written to the output dir, for a look. --bench measures processBlock's CPU time
// (PLAN.md §7 row 8: 8 instances under 1% CPU each; Tests/run_bench.sh); --bench-ui the
// message thread's CPU with one editor open in a window while everything plays.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../../Source/plugin/ChordismProcessor.h"
#include "../../Source/ui/ChordismEditor.h"
#include "../../Source/ui/ChordEditorPanel.h"
#include "../../Source/core/midiio/MidiFile.h"
#include <chrono>
#include <iostream>
#include <pthread.h>
#include <random>
#include <thread>
#include <time.h>

using namespace chordism;

// Phase 8: every allocation the watched thread makes (malloc, realloc, new: libmalloc's logger
// hook sees them all). Only a pthread_t compare inside: the hook runs within malloc.
extern "C" void (*malloc_logger) (uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t);

namespace
{
    std::atomic<pthread_t> allocWatched { nullptr };
    void* volatile allocSink = nullptr;
    std::atomic<int> allocCount { 0 };

    void allocLogger (uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t)
    {
        if ((type & 2) != 0 && allocWatched.load (std::memory_order_relaxed) == pthread_self())   // 2: an allocation
            allocCount.fetch_add (1, std::memory_order_relaxed);
    }

    // Counts the allocations this thread makes while it lives.
    struct AllocWatch
    {
        AllocWatch() { malloc_logger = allocLogger; allocWatched = pthread_self(); }
        ~AllocWatch() { allocWatched = nullptr; }
    };
}

namespace
{
    int failures = 0;
    bool comparePixels = true;

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
        if (! comparePixels)
        {
            check (image.isValid() && image.getWidth() == editor.getWidth(), name + ": rendered (pixels not compared)");
            return;
        }
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

            // A user preset's state into a new instance; Enable and the test rig stay.
            set (*p, params::key, 5.0f);
            set (*p, params::scale, (float) (int) Scale::Dorian);
            set (*p, params::chordGroup, (float) (int) ChordGroup::Ninths);
            set (*p, params::patternArp (1), (float) (numArpPatterns - 1));
            set (*p, params::melodySeed, 321.0f);
            set (*p, params::seqSteps (2), (float) 0x1234);
            p->chordEditor.setAccidental (ChordGroup::Ninths, Scale::Dorian, 3, 2, 1);
            const auto tree = p->presetState();
            auto other = std::make_unique<ChordismProcessor>();
            set (*other, params::enable, 0.0f);
            set (*other, params::testPattern, 1.0f);
            other->applyPreset (tree, "User", "Mine");
            differ.clear();
            check (equalParams (*p, *other, differ, true) && other->chordEditor.getEdits().sameEdits (p->chordEditor.getEdits())
                       && get (*other, params::enable) == 0.0f && get (*other, params::testPattern) == 1.0f && other->getPresetName() == "Mine",
                   "a preset's state loads every preset parameter and the edits, and leaves Enable and the test pattern ("
                       + differ.joinIntoString (", ") + ")");
            check (! tree.getChildWithProperty ("id", params::enable).isValid() && ! tree.getChildWithProperty ("id", params::testPattern).isValid()
                       && ! tree.getChildWithName ("UI").isValid(),
                   "a preset doesn't hold Enable, the test rig or the editor's preferences");

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

    // Phase 7: two processors in one sync group (the process-wide SharedSession).
    void testPhase7()
    {
        std::cout << "== Phase 7: Instance Sync ==\n";
        auto pump = [] { for (int i = 0; i < 6; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20); };
        auto a = std::make_unique<ChordismProcessor>();
        auto b = std::make_unique<ChordismProcessor>();
        for (auto* p : { a.get(), b.get() })
            p->prepareToPlay (44100.0, 512);
        set (*a, params::key, 2.0f);
        set (*a, params::scale, (float) (int) Scale::Dorian);
        set (*a, params::syncGroup, 3.0f);      // C (A and B may be in use by other tests' processes)
        pump();
        set (*b, params::syncGroup, 3.0f);
        pump();
        check ((int) get (*b, params::key) == 2 && (int) get (*b, params::scale) == (int) Scale::Dorian,
               "joining a group takes its Key and Scale (D Dorian)");
        set (*b, params::key, 7.0f);
        pump();
        check ((int) get (*a, params::key) == 7 && (int) get (*a, params::scale) == (int) Scale::Dorian,
               "a Key change in one instance reaches the other (G)");

        // A plays chord key D (slot 2: iii in G Dorian, B minor's degree); B's Smart Scale follows.
        block (*a, { juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100) });
        block (*b, {});
        const auto& shown = b->readDisplay();
        check (shown.smartRemote && shown.hasSmart && shown.smart.slot.degree == 1 && shown.smart.key == 7,
               "B's Smart Scale (and its keyboard) follows A's chord");
        set (*b, params::muteChords, 1.0f);
        const auto out = noteOns (block (*b, { juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) }));
        check (out.size() == 1 && out[0] == 60 + 7 + scaleOffset (Scale::Dorian, 1),
               "B's Smart key C plays A's chord root (" + juce::String (out.empty() ? -1 : out[0]) + ")");
        block (*a, { juce::MidiMessage::noteOff (1, 50) });
        block (*b, { juce::MidiMessage::noteOff (1, 60) });

        // A preset leaves the Sync group alone.
        const auto tree = a->presetState();
        check (! tree.getChildWithProperty ("id", params::syncGroup).isValid(), "a preset doesn't hold the Sync group");
        set (*b, params::syncGroup, 0.0f);
        b->applyPreset (tree, "User", "Synced");
        check ((int) get (*b, params::syncGroup) == 0, "loading a preset keeps the instance's Sync group");
        set (*a, params::syncGroup, 0.0f);
        pump();
    }
}

namespace
{
    // A host transport with a loop and a tempo (Phase 8).
    struct LoopPlayHead : juce::AudioPlayHead
    {
        bool playing = true, looping = true;
        double ppq = 0.0, bpm = 140.0, loopStart = 0.0, loopEnd = 2.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            info.setIsLooping (looping);
            info.setLoopPoints (juce::AudioPlayHead::LoopPoints { loopStart, loopEnd });
            info.setPpqPosition (ppq);
            info.setBpm (bpm);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            return info;
        }
        void advance (int samples, double rate)
        {
            if (! playing)
                return;
            ppq += samples * bpm / (60.0 * rate);
            if (looping && ppq >= loopEnd)
                ppq = loopStart + (ppq - loopEnd);
        }
    };

    // Every chord and Smart Scale effect at once, the test rig playing both zones.
    void everythingOn (ChordismProcessor& p)
    {
        set (p, params::chordGroup, (float) (int) ChordGroup::Thirteenths);
        set (p, params::stack, (float) (int) StackMode::PlusOneAndTwo);
        set (p, params::arp, (float) (int) ArpPattern::BoomerangUp);
        set (p, params::arpRate, (float) (numPatternRates - 1));
        set (p, params::arpRange, 3.0f);
        set (p, params::repeat, 1.0f);
        set (p, params::seq, 1.0f);
        set (p, params::seqSustain, 1.0f);
        set (p, params::velocity, (float) (int) VelocityCurve::House);
        set (p, params::hold, (float) (int) HoldMode::Bar);
        set (p, params::voiceLeading, 1.0f);
        set (p, params::noteRepeat, 1.0f);
        set (p, params::smartStack, (float) (int) StackMode::PlusOneAndTwo);
        set (p, params::smartSeq, 1.0f);
        set (p, params::smartHold, (float) (int) HoldMode::Latch);
        set (p, params::patternArp (0), (float) (numArpPatterns - 1));   // slot 1: Melody
        set (p, params::patternNotes (0), 16.0f);
        set (p, params::lookAhead, (float) (numLookAheads - 1));
        set (p, params::testPattern, 1.0f);
        set (p, params::testShifter, 2.0f);
        set (p, params::testSmart, (float) (TestPattern::smartPattern + 2));
    }

    // A parameter is a sane value: finite, in 0..1.
    int insaneParameters (ChordismProcessor& p)
    {
        int bad = 0;
        for (auto* param : p.getParameters())
        {
            const float v = param->getValue();
            bad += std::isfinite (v) && v >= 0.0f && v <= 1.0f ? 0 : 1;
        }
        return bad;
    }

    // Plays and releases chord key C: the processor still works.
    bool playsAChord (ChordismProcessor& p)
    {
        set (p, params::enable, 1.0f);
        set (p, params::muteChords, 0.0f);
        set (p, params::testPattern, 0.0f);
        set (p, params::hold, 0.0f);
        set (p, params::arp, 0.0f);
        set (p, params::strum, 0.0f);
        set (p, params::seq, 0.0f);
        block (p, {});
        const auto on = noteOns (block (p, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) }));
        block (p, { juce::MidiMessage::noteOff (1, 48) });
        block (p, {});
        return ! on.empty() && p.soundingNotes.load() == 0;
    }

    void testPhase8()
    {
        std::cout << "== Phase 8: no allocation on the audio thread ==\n";
        {
            const int before = allocCount.load();
            {
                AllocWatch watch;
                allocSink = new int (8);          // stored through a volatile: the compiler can't elide it
                delete static_cast<int*> (allocSink);
                allocSink = std::malloc (1 << 20);
                std::free (allocSink);
            }
            check (allocCount.load() - before >= 2, "the malloc hook sees allocations (" + juce::String (allocCount.load() - before) + " counted)");
        }
        {
            // Two synced instances with everything on, under a looping transport; between blocks
            // (as the host and the message thread would) parameters change, the transport stops,
            // starts and moves its loop, on-screen keys go down, bypass toggles.
            auto a = std::make_unique<ChordismProcessor>();
            auto b = std::make_unique<ChordismProcessor>();
            LoopPlayHead head;
            for (auto* p : { a.get(), b.get() })
            {
                p->setPlayHead (&head);
                p->prepareToPlay (48000.0, 256);
                everythingOn (*p);
                set (*p, params::syncGroup, 4.0f);
            }
            juce::AudioBuffer<float> audio (2, 256);
            juce::MidiBuffer midi;
            midi.ensureSize (1 << 16);    // a host that reserves enough for any block
            std::mt19937 rng (8);
            int allocations = 0, blocks = 0, peak = 0, bypassed = 0;
            for (int i = 0; i < 4000; ++i)
            {
                if (rng() % 50 == 0) set (*a, params::key, (float) (rng() % 12));
                if (rng() % 70 == 0) set (*b, params::arp, (float) (rng() % numArpPatterns));
                if (rng() % 90 == 0) set (*a, params::chordGroup, (float) (rng() % numChordGroups));
                if (rng() % 200 == 0) head.playing = ! head.playing;
                if (rng() % 300 == 0) { head.loopStart = (double) (rng() % 4); head.loopEnd = head.loopStart + 0.5 + (rng() % 4); }
                if (rng() % 40 == 0) a->pushUiNote (48 + (int) (rng() % 30), 100, rng() % 2 == 0);
                if (rng() % 100 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);   // the timers
                for (auto* p : { a.get(), b.get() })
                {
                    midi.clear();
                    if (rng() % 8 == 0)
                        midi.addEvent (rng() % 2 == 0 ? juce::MidiMessage::noteOn (1, 36 + (int) (rng() % 48), (juce::uint8) 90)
                                                      : juce::MidiMessage::noteOff (1, 36 + (int) (rng() % 48)), (int) (rng() % 256));
                    const bool bypass = p == b.get() && i % 500 > 480;
                    const int before = allocCount.load();
                    {
                        AllocWatch watch;
                        if (bypass)
                            p->processBlockBypassed (audio, midi);
                        else
                            p->processBlock (audio, midi);
                    }
                    allocations += allocCount.load() - before;
                    bypassed += bypass ? 1 : 0;
                    peak = std::max (peak, midi.getNumEvents());
                    ++blocks;
                }
                head.advance (256, 48000.0);
            }
            check (allocations == 0, juce::String (blocks) + " blocks of two synced instances with everything on (" + juce::String (bypassed)
                                         + " bypassed, up to " + juce::String (peak) + " events out): " + juce::String (allocations)
                                         + " allocations in processBlock");

            // A host buffer the size JUCE's VST3 wrapper reserves (2048 bytes, ~227 events), and
            // stacked thirteenths without the arp (up to 128 notes a block, restruck): the host's
            // buffer grows in the first blocks with more output; after that it holds.
            set (*a, params::arp, 0.0f);
            set (*a, params::seq, 0.0f);
            set (*a, params::testPattern, 0.0f);
            set (*a, params::syncGroup, 0.0f);
            juce::MidiBuffer small;
            small.ensureSize (2048);
            int growth = 0, late = 0, most = 0;
            for (int i = 0; i < 1000; ++i)
            {
                small.clear();
                if (i % 2 == 0)
                    for (int n = 0; n < 60; ++n)
                        small.addEvent (juce::MidiMessage::noteOn (1, 36 + n % 24, (juce::uint8) 90), n);
                const int before = allocCount.load();
                {
                    AllocWatch watch;
                    a->processBlock (audio, small);
                }
                const int made = allocCount.load() - before;
                most = std::max (most, small.getNumEvents());
                growth += made;
                late += i >= 100 ? made : 0;
                head.advance (256, 48000.0);
            }
            check (late == 0, "with JUCE's VST3 host buffer (2048 bytes) and up to " + juce::String (most)
                                  + " events a block: the host's buffer grew " + juce::String (growth) + " time(s) in the first 100 blocks, never after");
            for (auto* p : { a.get(), b.get() })
            {
                set (*p, params::syncGroup, 0.0f);
                p->setPlayHead (nullptr);
            }
        }

        std::cout << "== Phase 8: damaged saved states ==\n";
        {
            auto p = std::make_unique<ChordismProcessor>();
            p->prepareToPlay (44100.0, 512);
            std::mt19937 rng (88);
            int insane = 0;
            for (int i = 0; i < 300; ++i)
            {
                juce::MemoryBlock junk ((size_t) (rng() % 4096));
                for (size_t k = 0; k < junk.getSize(); ++k)
                    junk[k] = (char) (rng() & 0xff);
                if (i % 3 == 0 && junk.getSize() > 8)
                    std::memcpy (junk.getData(), "VC2!", 4);   // JUCE's binary-XML header, garbage after
                p->setStateInformation (junk.getData(), (int) junk.getSize());
                insane += insaneParameters (*p);
            }
            p->setStateInformation (nullptr, 0);
            p->setStateInformation ("xx", -5);
            check (insane == 0 && playsAChord (*p), "300 blobs of random bytes (a third with a valid header) are ignored; the instance still plays");

            const char* values[] = { "nan", "inf", "-inf", "1e300", "-1e300", "abc", "", "0x7fffffff", "-0" };
            int worst = 0;
            for (const char* v : values)
            {
                juce::String xml ("<Chordism program=\"99999\" presetName=\"\">");
                for (auto* param : p->getParameters())
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
                        xml << "<PARAM id=\"" << r->paramID << "\" value=\"" << v << "\"/>";
                xml << "<PARAM id=\"noSuchParameter\" value=\"1\"/><PARAM id=\"key\" value=\"3\"/>"
                       "<ChordEditor group=\"999\"><Degree index=\"99\"/><Degree index=\"2\"><Step index=\"-1\" on=\"1\"/>"
                       "<Step index=\"3\" on=\"1\" acc=\"50\" oct=\"99999\"/></Degree></ChordEditor>"
                       "<UI page=\"nowhere\" keyboard=\"maybe\" width=\"-1\" height=\"1e9\"/></Chordism>";
                juce::MemoryBlock state;
                juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (xml), state);
                p->setStateInformation (state.getData(), (int) state.getSize());
                worst = std::max (worst, insaneParameters (*p));
                std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorIfNeeded());   // opens with the UI's garbage
                editor.reset();
                const auto& edits = p->chordEditor.getEdits();
                for (const auto& d : edits.degrees)
                    for (const auto& st : d.steps)
                        worst += st.accidental < -2 || st.accidental > 2 || (st.octaveMask & ~editor::allOctaves) != 0 ? 1 : 0;
                worst += p->getCurrentProgram() < 0 || p->getCurrentProgram() >= p->getNumPrograms() ? 1 : 0;
            }
            check (worst == 0 && playsAChord (*p), "states whose every value is nan, inf, -inf, +/-1e300, text or empty, with unknown IDs, "
                                                   "a nonsense Chord Editor and UI: every parameter stays finite and in range, the edits "
                                                   "in range, the editor opens, the instance plays");

            // A preset with a NaN gives that parameter its default.
            auto tree = p->presetState();
            tree.getChildWithProperty ("id", params::key).setProperty ("value", "nan", nullptr);
            set (*p, params::key, 5.0f);
            p->applyPreset (tree, "User", "Damaged");
            check ((int) get (*p, params::key) == 0, "a preset whose Key is NaN loads Key's default (C)");
        }

        std::cout << "== Phase 8: host edge cases ==\n";
        {
            auto p = std::make_unique<ChordismProcessor>();
            p->prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> none (2, 0), huge (2, 8192), mono (1, 512);
            auto run = [&p] (juce::AudioBuffer<float>& audio, std::initializer_list<juce::MidiMessage> events, bool bypass = false)
            {
                juce::MidiBuffer midi;
                for (const auto& e : events)
                    midi.addEvent (e, 0);
                if (bypass)
                    p->processBlockBypassed (audio, midi);
                else
                    p->processBlock (audio, midi);
                return midi;
            };

            const auto zero = run (none, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) });
            const auto big = run (huge, { juce::MidiMessage::noteOff (1, 48), juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100) });
            const auto monoOut = run (mono, { juce::MidiMessage::noteOff (1, 50) });
            check (noteOns (zero) == std::vector<int> { 48, 52, 55 } && noteOns (big) == std::vector<int> { 50, 53, 57 } && noteOffs (big) == 3
                       && noteOffs (monoOut) == 3 && p->soundingNotes.load() == 0,
                   "a zero-length block plays its key; an 8192-sample block after prepareToPlay (..., 512) and a mono block work");

            run (mono, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) });
            const auto bypassed = run (mono, { juce::MidiMessage::noteOn (1, 64, (juce::uint8) 90) }, true);
            check (noteOffs (bypassed) == 3 && noteOns (bypassed) == std::vector<int> { 64 }, "host bypass while a chord sounds ends it and passes input through");
            run (mono, { juce::MidiMessage::noteOff (1, 64), juce::MidiMessage::noteOff (1, 48) }, true);

            run (mono, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) });
            p->releaseResources();
            const auto afterRelease = run (mono, {});
            p->prepareToPlay (96000.0, 128);
            run (mono, { juce::MidiMessage::noteOff (1, 48), juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100) });
            p->prepareToPlay (44100.0, 2048);
            const auto afterRate = run (mono, {});
            run (mono, { juce::MidiMessage::noteOff (1, 52), juce::MidiMessage::noteOn (1, 53, (juce::uint8) 100) });
            const auto allOff = run (mono, { juce::MidiMessage::allNotesOff (1) });
            int cc = 0;
            for (const auto m : allOff)
                cc += m.getMessage().isAllNotesOff() ? 1 : 0;
            check (noteOffs (afterRelease) == 3 && noteOffs (afterRate) == 3 && noteOffs (allOff) == 3 && cc == 1 && p->soundingNotes.load() == 0,
                   "releaseResources, a sample-rate change and the host's all-notes-off each end a sounding chord (the CC passes on)");
            run (mono, { juce::MidiMessage::noteOff (1, 53) });

            const juce::uint8 sysex[] = { 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00 };
            const auto passed = run (mono, { juce::MidiMessage::createSysExMessage (sysex, (int) sizeof (sysex)) });
            bool sysexIntact = false;
            for (const auto m : passed)
                sysexIntact |= m.getMessage().isSysEx() && m.getMessage().getSysExDataSize() == (int) sizeof (sysex)
                            && std::memcmp (m.getMessage().getSysExData(), sysex, sizeof (sysex)) == 0;
            check (sysexIntact, "SysEx passes through unchanged");

            // A flood: 2900 note-ons, then every key up, in one block.
            {
                juce::MidiBuffer flood;
                for (int i = 0; i < 2900; ++i)
                    flood.addEvent (juce::MidiMessage::noteOn (1, 36 + i % 48, (juce::uint8) 100), i % 400);
                for (int n = 0; n < 128; ++n)
                    flood.addEvent (juce::MidiMessage::noteOff (1, n), 450);
                juce::AudioBuffer<float> audio (2, 512);
                p->processBlock (audio, flood);
                run (mono, {});
                check (p->soundingNotes.load() == 0, "2900 note-ons and 128 key-ups in one block (input holds 2048): the key-ups get in, nothing sounds after");
            }
        }

        std::cout << "== Phase 8: many instances ==\n";
        {
            std::vector<std::unique_ptr<ChordismProcessor>> all;
            for (int i = 0; i < 64; ++i)
            {
                all.push_back (std::make_unique<ChordismProcessor>());
                all.back()->prepareToPlay (44100.0, 256);
                set (*all.back(), params::syncGroup, (float) (i % 5));
            }
            int played = 0;
            for (auto& p : all)
                played += noteOns (block (*p, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) })).size() == 3 ? 1 : 0;
            std::shuffle (all.begin(), all.end(), std::mt19937 (64));
            while (all.size() > 2)
                all.pop_back();
            for (auto& p : all)
            {
                set (*p, params::syncGroup, 3.0f);
                block (*p, { juce::MidiMessage::noteOff (1, 48) });
            }
            block (*all[0], { juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100) });
            block (*all[1], {});
            check (played == 64 && all[1]->readDisplay().smartRemote,
                   "64 instances in all five sync groups play; destroyed in random order, the last two still sync");
            for (auto& p : all)
                set (*p, params::syncGroup, 0.0f);
        }
    }

    // processBlock's CPU time: `instances` processors one after another on one thread (as a
    // host's audio thread runs them), for `seconds` of audio at each block size.
    int runBench (double seconds)
    {
        constexpr int instances = 8;
        constexpr double rate = 44100.0;
        std::cout << "== Phase 8: CPU per instance (" << instances << " instances, " << seconds << " s of audio at 44.1 kHz, 140 BPM, 2-beat loop) ==\n"
                  << "   scenario         block   mean % of real time   99.9th percentile / worst block (% of a block's duration)\n";
        const char* names[] = { "idle", "test pattern", "everything on" };
        double worstMean = 0.0;
        for (int scenario = 0; scenario < 3; ++scenario)
            for (const int blockSize : { 32, 128, 512 })
            {
                std::vector<std::unique_ptr<ChordismProcessor>> procs;
                LoopPlayHead head;
                for (int i = 0; i < instances; ++i)
                {
                    procs.push_back (std::make_unique<ChordismProcessor>());
                    auto& p = *procs.back();
                    p.setPlayHead (&head);
                    p.prepareToPlay (rate, blockSize);
                    if (scenario == 1)
                        set (p, params::testPattern, 1.0f);
                    if (scenario == 2)
                    {
                        everythingOn (p);
                        set (p, params::syncGroup, (float) (1 + i % 4));
                    }
                }
                juce::AudioBuffer<float> audio (2, blockSize);
                juce::MidiBuffer midi;
                midi.ensureSize (1 << 16);
                const int numBlocks = (int) (seconds * rate / blockSize);
                double total = 0.0, worst = 0.0;
                std::vector<double> times;
                times.reserve ((size_t) numBlocks * instances);
                for (int n = 0; n < numBlocks; ++n)
                {
                    for (auto& p : procs)
                    {
                        midi.clear();
                        const auto t0 = std::chrono::steady_clock::now();
                        p->processBlock (audio, midi);
                        const double dt = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                        total += dt;
                        worst = std::max (worst, dt);
                        times.push_back (dt);
                    }
                    head.advance (blockSize, rate);
                }
                const double audioSeconds = numBlocks * blockSize / rate;
                const double mean = 100.0 * total / instances / audioSeconds;
                const double peak = 100.0 * worst / (blockSize / rate);
                std::nth_element (times.begin(), times.begin() + (long) (times.size() * 999 / 1000), times.end());
                const double p999 = 100.0 * times[times.size() * 999 / 1000] / (blockSize / rate);
                worstMean = std::max (worstMean, mean);
                std::printf ("   %-15s %6d   %10.4f%%            %8.3f%% / %.3f%%\n", names[scenario], blockSize, mean, p999, peak);
                for (auto& p : procs)
                {
                    set (*p, params::syncGroup, 0.0f);
                    p->setPlayHead (nullptr);
                }
            }
        check (worstMean < 1.0, "every scenario and block size: under 1% of one core per instance (worst mean "
                                    + juce::String (worstMean, 4) + "%)");
        return failures == 0 ? 0 : 1;
    }
}

namespace
{
    double threadCpuSeconds()
    {
        timespec t {};
        clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t);
        return (double) t.tv_sec + (double) t.tv_nsec * 1.0e-9;
    }

    // The editor's cost: one instance with everything on (or, `idle`, nothing playing and the
    // transport stopped) and its editor in a window, the audio
    // on its own thread in real time (512-sample blocks at 44.1 kHz), the message thread running
    // the editor's 30 Hz refresh and the painting for `seconds`. Prints each thread's CPU as a
    // share of one core.
    int runUiBench (double seconds, bool idle)
    {
        auto p = std::make_unique<ChordismProcessor>();
        LoopPlayHead head;
        head.loopEnd = 8.0;
        head.playing = ! idle;
        p->setPlayHead (&head);
        p->prepareToPlay (44100.0, 512);
        if (! idle)
        {
            everythingOn (*p);
            set (*p, params::testSmart, (float) TestPattern::smartTones);   // the Smart keys change on every beat too
        }
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorIfNeeded());
        editor->setTopLeftPosition (20, 60);
        editor->addToDesktop (juce::ComponentPeer::windowHasTitleBar);
        editor->setVisible (true);

        std::atomic<bool> stop { false };
        double audioCpu = 0.0;
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.ensureSize (1 << 16);
            const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration> (std::chrono::duration<double> (512.0 / 44100.0));
            auto next = std::chrono::steady_clock::now();
            const double c0 = threadCpuSeconds();
            while (! stop.load())
            {
                midi.clear();
                p->processBlock (buffer, midi);
                head.advance (512, 44100.0);
                next += period;
                std::this_thread::sleep_until (next);
            }
            audioCpu = threadCpuSeconds() - c0;
        });
        const double m0 = threadCpuSeconds();
        const auto t0 = std::chrono::steady_clock::now();
        juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) (seconds * 1000.0));
        const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double ui = threadCpuSeconds() - m0;
        stop = true;
        audio.join();
        editor.reset();
        p->setPlayHead (nullptr);
        std::printf ("== Phase 8: one editor open, %s (%.0f s) ==\n   message thread (refresh + paint): %.2f%% of a core\n"
                     "   audio thread: %.3f%% of a core\n", idle ? "transport stopped, nothing playing" : "everything playing",
                     wall, 100.0 * ui / wall, 100.0 * audioCpu / wall);
        return 0;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc >= 2 && juce::String (argv[1]) == "--bench")
        return runBench (argc >= 3 ? juce::String (argv[2]).getDoubleValue() : 20.0);
    if (argc >= 2 && juce::String (argv[1]) == "--bench-ui")
        return runUiBench (argc >= 3 ? juce::String (argv[2]).getDoubleValue() : 15.0, argc >= 4 && juce::String (argv[3]) == "idle");
    if (argc < 3)
    {
        std::cerr << "usage: ChordismSnapshot <fixtures dir> <output dir> [--update | --no-pixels]\n"
                     "       ChordismSnapshot --bench [seconds]\n";
        return 2;
    }
    const juce::File fixtures (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    const bool update = argc > 3 && juce::String (argv[3]) == "--update";
    comparePixels = ! (argc > 3 && juce::String (argv[3]) == "--no-pixels");
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
    testPhase7();
    testPhase8();

    std::cout << (failures == 0 ? "\nsnapshot: all passed\n" : "\n*** snapshot: " + std::to_string (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}
