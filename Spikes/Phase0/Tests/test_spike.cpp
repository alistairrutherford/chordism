// Phase 0 spike tests. Part A drives SpikeProcessor directly with a fake Live
// transport. Part B loads the *built* VST3 and AU bundles the way a host would,
// to check what Live will actually see (MIDI output events, shared statics).
// Run via Tests/run_tests.sh after a Release build.
#include "PluginProcessor.h"
#include "TestUtil.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>
#include <mutex>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <new>
#include <set>
#include <vector>

using testutil::check;

// ---- heap allocation counter: counts operator new while `counting` is set ----
namespace alloccount
{
    thread_local bool counting = false;
    std::atomic<int> count { 0 };
}

void* operator new (std::size_t n)
{
    if (alloccount::counting) alloccount::count.fetch_add (1);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
    struct FakePlayHead : juce::AudioPlayHead
    {
        PositionInfo info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    };

    struct Event { int64_t sample; int note; bool on; };

    // Pumps the main run loop, where JUCE delivers async messages on macOS.
    bool pumpUntil (const std::function<bool()>& done, int timeoutMs = 5000)
    {
        for (int waited = 0; waited < timeoutMs; waited += 10)
        {
            if (done()) return true;
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false);
        }
        return done();
    }

    // Runs `processor` for `totalSamples` under a fake transport starting at `clock`.
    std::vector<Event> run (juce::AudioProcessor& processor, FakePlayHead& ph, double fs, double bpm, int block,
                            int64_t totalSamples, int64_t& clock, bool playing,
                            const std::function<void (int64_t blockStart, int n, juce::MidiBuffer&)>& inject = {},
                            float* peakOut = nullptr, bool countAllocs = false)
    {
        std::vector<Event> events;
        juce::AudioBuffer<float> buffer (juce::jmax (2, processor.getTotalNumOutputChannels()), block);
        juce::MidiBuffer midi;
        midi.ensureSize (8192);
        float peak = 0.0f;

        for (int64_t done = 0; done < totalSamples; done += block)
        {
            const int n = (int) std::min<int64_t> (block, totalSamples - done);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), n);

            ph.info.setIsPlaying (playing);
            ph.info.setBpm (bpm);
            ph.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            ph.info.setTimeInSamples (clock);
            ph.info.setPpqPosition ((double) clock * bpm / (60.0 * fs));

            midi.clear();
            if (inject) inject (clock, n, midi);

            alloccount::counting = countAllocs;
            processor.processBlock (view, midi);
            alloccount::counting = false;

            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn())       events.push_back ({ clock + meta.samplePosition, m.getNoteNumber(), true });
                else if (m.isNoteOff()) events.push_back ({ clock + meta.samplePosition, m.getNoteNumber(), false });
            }

            peak = juce::jmax (peak, view.getMagnitude (0, n));
            clock += n;
        }

        if (peakOut) *peakOut = peak;
        return events;
    }

    std::multiset<int> onsAt (const std::vector<Event>& events, int64_t sample)
    {
        std::multiset<int> notes;
        for (const auto& e : events)
            if (e.on && e.sample == sample) notes.insert (e.note);
        return notes;
    }

    int countOns (const std::vector<Event>& events)
    {
        int n = 0;
        for (const auto& e : events) n += e.on ? 1 : 0;
        return n;
    }

    juce::MidiBuffer noteAt (int note, int sample, bool on)
    {
        juce::MidiBuffer b;
        b.addEvent (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, note), sample);
        return b;
    }

    void prepare (juce::AudioProcessor& p, FakePlayHead& ph, double fs, int block)
    {
        p.setPlayHead (&ph);
        p.setRateAndBufferSizeDetails (fs, block);
        p.prepareToPlay (fs, block);
    }

    std::unique_ptr<juce::AudioPluginInstance> loadBuilt (juce::AudioPluginFormatManager& fm, const juce::String& formatName,
                                                          const juce::String& fileOrId, juce::String& error)
    {
        for (auto* f : fm.getFormats())
        {
            if (f->getName() != formatName)
                continue;

            juce::OwnedArray<juce::PluginDescription> found;
            f->findAllTypesForFile (found, fileOrId);
            if (found.isEmpty()) { error = "not found: " + fileOrId; return {}; }

            std::unique_ptr<juce::AudioPluginInstance> result;
            bool finished = false;
            fm.createPluginInstanceAsync (*found[0], 48000.0, 256, [&] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err)
            {
                result = std::move (inst);
                error = err;
                finished = true;
            });
            pumpUntil ([&] { return finished; });
            return result;
        }

        error = "no format " + formatName;
        return {};
    }

    juce::MemoryBlock stateWithKey (int key)
    {
        SpikeProcessor p;
        p.syncKey.store (true);
        p.setKeyFromUi (key);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        return mb;
    }

    // Listens to one CoreMIDI source endpoint and records note-ons with timestamps.
    struct PortListener
    {
        struct Note { int64_t ns; int note; };
        MIDIPortRef port = 0;
        std::mutex lock;
        std::vector<Note> notes;

        bool connect (uint32_t endpoint)
        {
            static MIDIClientRef client = [] { MIDIClientRef c = 0; MIDIClientCreateWithBlock (CFSTR ("SpikeTest"), &c, nullptr); return c; }();
            if (client == 0 || endpoint == 0) return false;
            auto receive = ^(const MIDIEventList* list, void*)
            {
                mach_timebase_info_data_t tb; mach_timebase_info (&tb);
                const MIDIEventPacket* p = &list->packet[0];
                std::lock_guard<std::mutex> g (lock);
                for (UInt32 i = 0; i < list->numPackets; ++i)
                {
                    for (UInt32 w = 0; w < p->wordCount; ++w)
                    {
                        const UInt32 word = p->words[w];
                        if ((word >> 28) == 0x2 && ((word >> 20) & 0xf) == 0x9 && (word & 0xff) > 0)
                            notes.push_back ({ (int64_t) ((__int128) p->timeStamp * tb.numer / tb.denom), (int) ((word >> 8) & 0x7f) });
                    }
                    p = MIDIEventPacketNext (p);
                }
            };
            if (MIDIInputPortCreateWithProtocol (client, CFSTR ("in"), kMIDIProtocol_1_0, &port, receive) != noErr) return false;
            return MIDIPortConnectSource (port, (MIDIEndpointRef) endpoint, nullptr) == noErr;
        }

        std::vector<Note> snapshot() { std::lock_guard<std::mutex> g (lock); return notes; }
        ~PortListener() { if (port) MIDIPortDispose (port); }
    };

    // Wraps a plugin state blob the way JUCE's VST3 host stores component state.
    juce::MemoryBlock asVst3HostState (const juce::MemoryBlock& pluginState)
    {
        juce::XmlElement xml ("VST3PluginState");
        xml.createNewChildElement ("IComponent")->addTextElement (pluginState.toBase64Encoding());
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (xml, mb);
        return mb;
    }

    // Plays a white key into a hosted copy of the plugin and returns the notes it sends back.
    std::multiset<int> chordFromBuilt (juce::AudioPluginInstance& inst, int note)
    {
        FakePlayHead ph;
        prepare (inst, ph, 48000.0, 256);
        int64_t clock = 0;
        auto events = run (inst, ph, 48000.0, 120.0, 256, 256, clock, false,
                           [note] (int64_t, int, juce::MidiBuffer& m) { m.addEvents (noteAt (note, 10, true), 0, -1, 0); });
        run (inst, ph, 48000.0, 120.0, 256, 256, clock, false,
             [note] (int64_t, int, juce::MidiBuffer& m) { m.addEvents (noteAt (note, 0, false), 0, -1, 0); });
        return onsAt (events, 10);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const std::multiset<int> cMajor { 60, 64, 67 }, dMinor { 62, 65, 69 }, fMajor { 65, 69, 72 }, dMajor { 62, 66, 69 };

    //==========================================================================
    testutil::section ("A1  White keys play diatonic triads; note-off releases them");
    {
        SpikeProcessor p;
        FakePlayHead ph;
        prepare (p, ph, 48000.0, 256);
        p.syncKey.store (false);
        p.setKeyFromUi (0);

        int64_t clock = 0;
        auto ev = run (p, ph, 48000.0, 120.0, 256, 256, clock, false, [] (int64_t, int, juce::MidiBuffer& m)
        {
            m.addEvents (noteAt (60, 5, true), 0, -1, 0);
            m.addEvents (noteAt (62, 9, true), 0, -1, 0);
        });
        check (onsAt (ev, 5) == cMajor, "C key → C E G at the same sample");
        check (onsAt (ev, 9) == dMinor, "D key → D F A");

        run (p, ph, 48000.0, 120.0, 256, 256, clock, false, [] (int64_t, int, juce::MidiBuffer& m)
        {
            m.addEvents (noteAt (60, 0, false), 0, -1, 0);
        });
        check (p.soundingNoteCount() == 3, "releasing C keeps D minor sounding (D F A)");

        run (p, ph, 48000.0, 120.0, 256, 256, clock, false, [] (int64_t, int, juce::MidiBuffer& m)
        {
            m.addEvents (noteAt (62, 0, false), 0, -1, 0);
        });
        check (p.soundingNoteCount() == 0, "no notes left after both keys are released");

        p.setKeyFromUi (2);
        ev = run (p, ph, 48000.0, 120.0, 256, 256, clock, false, [] (int64_t, int, juce::MidiBuffer& m)
        {
            m.addEvents (noteAt (60, 0, true), 0, -1, 0);
        });
        check (onsAt (ev, clock - 256) == dMajor, "key D: the C key plays D major");

        p.setKeyFromUi (5);   // change key while the chord is held
        run (p, ph, 48000.0, 120.0, 256, 256, clock, false, [] (int64_t, int, juce::MidiBuffer& m)
        {
            m.addEvents (noteAt (60, 0, false), 0, -1, 0);
        });
        check (p.soundingNoteCount() == 0, "changing key while held still releases the original notes");
    }

    //==========================================================================
    testutil::section ("A2  Test pattern lands on the beat (sample-accurate)");
    {
        struct Case { double fs, bpm; int block; };
        for (auto c : { Case { 48000.0, 120.0, 256 }, Case { 48000.0, 120.0, 333 }, Case { 44100.0, 97.0, 512 }, Case { 96000.0, 174.0, 64 } })
        {
            SpikeProcessor p;
            FakePlayHead ph;
            prepare (p, ph, c.fs, c.block);
            p.syncKey.store (false);
            p.setKeyFromUi (0);
            p.testPattern.store (true);

            const double spb = c.fs * 60.0 / c.bpm;
            int64_t clock = 0;
            auto ev = run (p, ph, c.fs, c.bpm, c.block, (int64_t) (spb * 16) - 1, clock, true);   // beats 0–15

            const std::multiset<int> expected[4] = { { 48, 52, 55 }, { 53, 57, 60 }, { 55, 59, 62 }, { 57, 60, 64 } };
            int good = 0, maxErr = 0;
            for (int beat = 0; beat < 16; ++beat)
            {
                const auto target = (int64_t) std::llround (beat * spb);
                std::multiset<int> got;
                for (const auto& e : ev)
                    if (e.on && std::llabs (e.sample - target) <= 1)
                    {
                        got.insert (e.note);
                        maxErr = juce::jmax (maxErr, (int) std::llabs (e.sample - target));
                    }
                good += got == expected[beat / 4] ? 1 : 0;
            }

            const auto label = juce::String (c.fs / 1000.0, 1) + " kHz, " + juce::String (c.bpm, 0) + " BPM, block " + juce::String (c.block);
            check (good == 16, (label + ": 16/16 beats carry the right chord (got " + juce::String (good) + ")").toStdString());
            check (maxErr <= 1, (label + ": every note-on within 1 sample of the beat").toStdString());
            check (countOns (ev) == 48, (label + ": exactly 48 note-ons, no doubles at block edges (got " + juce::String (countOns (ev)) + ")").toStdString());

            run (p, ph, c.fs, c.bpm, c.block, c.block, clock, false);
            check (p.soundingNoteCount() == 0, (label + ": stopping the transport leaves no notes on").toStdString());
        }
    }

    //==========================================================================
    testutil::section ("A3  Instance sync (two instances in one process)");
    {
        SpikeProcessor a, b, c;
        FakePlayHead ph;
        prepare (a, ph, 48000.0, 256);
        prepare (b, ph, 48000.0, 256);
        prepare (c, ph, 48000.0, 256);
        a.syncKey.store (true);
        b.syncKey.store (true);
        c.syncKey.store (false);
        c.setKeyFromUi (0);

        a.setKeyFromUi (5);
        int64_t clock = 0;
        auto inject = [] (int64_t, int, juce::MidiBuffer& m) { m.addEvents (noteAt (60, 0, true), 0, -1, 0); };
        auto evB = run (b, ph, 48000.0, 120.0, 256, 256, clock, false, inject);
        clock = 0;
        auto evC = run (c, ph, 48000.0, 120.0, 256, 256, clock, false, inject);

        check (onsAt (evB, 0) == fMajor, "key set on A (F) changes B's output: C key → F major");
        check (onsAt (evC, 0) == cMajor, "an unsynced instance keeps its own key");
        check (SharedSession::get().instanceCount.load() >= 3, "shared instance counter sees all instances");
        a.setKeyFromUi (0);
    }

    //==========================================================================
    testutil::section ("A4  Export: completed bars only, as a readable MIDI file");
    {
        SpikeProcessor p;
        FakePlayHead ph;
        prepare (p, ph, 48000.0, 256);
        p.syncKey.store (false);
        p.setKeyFromUi (0);
        p.testPattern.store (true);

        int64_t clock = 0;
        run (p, ph, 48000.0, 120.0, 256, 24000 * 18, clock, true);   // 4.5 bars
        p.recorder.drain();

        check (p.recorder.completedBars() == 4, "4.5 bars played → 4 completed bars");
        const auto result = p.recorder.build();
        check (result.bars == 4 && result.notes == 48, "export has 4 bars and 48 notes (got " + std::to_string (result.notes) + ")");

        const auto file = p.recorder.writeTempFile();
        juce::MidiFile readBack;
        juce::FileInputStream in (file);
        check (file.existsAsFile() && in.openedOk() && readBack.readFrom (in), "temp .mid file written and re-read");

        if (readBack.getNumTracks() == 1)
        {
            const auto* track = readBack.getTrack (0);
            std::vector<double> onTicks;
            double tempo = 0;
            for (const auto* e : *track)
            {
                if (e->message.isNoteOn()) onTicks.push_back (e->message.getTimeStamp());
                if (e->message.isTempoMetaEvent()) tempo = e->message.getTempoSecondsPerQuarterNote();
            }

            bool onGrid = onTicks.size() == 48;
            for (size_t i = 0; onGrid && i < onTicks.size(); ++i)
                onGrid = onTicks[i] == (double) ((i / 3) * 960);
            check (readBack.getTimeFormat() == 960, "960 ticks per quarter note");
            check (onGrid, "note-ons on every quarter note from tick 0");
            check (std::abs (tempo - 0.5) < 1.0e-6, "tempo meta event = 120 BPM");
        }
        else
        {
            check (false, "exported file has one track");
        }

        // Restart playback: the recording starts over and needs a full bar again.
        run (p, ph, 48000.0, 120.0, 256, 256, clock, false);
        clock = 0;
        run (p, ph, 48000.0, 120.0, 256, 24000 * 2, clock, true);
        p.recorder.drain();
        check (p.recorder.completedBars() == 0, "a new playback start resets the export (half a bar → nothing)");
    }

    //==========================================================================
    testutil::section ("A5  Real-time safety");
    {
        SpikeProcessor p;
        FakePlayHead ph;
        prepare (p, ph, 48000.0, 256);
        p.testPattern.store (true);
        int64_t clock = 0;
        run (p, ph, 48000.0, 120.0, 256, 48000, clock, true);   // warm up
        alloccount::count = 0;
        run (p, ph, 48000.0, 120.0, 256, 48000 * 4, clock, true, [] (int64_t start, int, juce::MidiBuffer& m)
        {
            if ((start / 256) % 50 == 0) m.addEvents (noteAt (64, 3, true), 0, -1, 0);
            if ((start / 256) % 50 == 25) m.addEvents (noteAt (64, 3, false), 0, -1, 0);
        }, nullptr, true);
        check (alloccount::count.load() == 0, "no heap allocations in processBlock over 4 s (" + std::to_string (alloccount::count.load()) + ")");
    }

    //==========================================================================
    testutil::section ("A6  Hosting an instrument (Apple DLSMusicDevice) inside the plugin");
    {
        SpikeProcessor p;
        FakePlayHead ph;
        prepare (p, ph, 48000.0, 256);

        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* f : p.hosted.getFormatManager().getFormats())
            if (f->getName() == "AudioUnit")
                f->findAllTypesForFile (found, "AudioUnit:Synths/aumu,dls ,appl");
        check (found.size() == 1, "found DLSMusicDevice");

        if (found.size() == 1)
        {
            juce::String error = "pending";
            p.hosted.load (*found[0], [&] (const juce::String& e) { error = e; });
            pumpUntil ([&] { return error != "pending"; });
            check (error.isEmpty() && p.hosted.isLoaded(), ("hosted instrument loaded " + error).toStdString());

            int64_t clock = 0;
            float silentPeak = 1.0f, playedPeak = 0.0f;
            run (p, ph, 48000.0, 120.0, 256, 256 * 10, clock, false, {}, &silentPeak);
            run (p, ph, 48000.0, 120.0, 256, 256 * 60, clock, false, [] (int64_t start, int, juce::MidiBuffer& m)
            {
                if (start == 256 * 10) m.addEvents (noteAt (60, 0, true), 0, -1, 0);
            }, &playedPeak);
            check (silentPeak < 1.0e-6, "silent before any note");
            check (playedPeak > 1.0e-3, ("a C chord is audible through the hosted synth (peak " + juce::String (playedPeak, 4) + ")").toStdString());

            juce::MemoryBlock state;
            p.getStateInformation (state);
            SpikeProcessor q;
            prepare (q, ph, 48000.0, 256);
            q.setStateInformation (state.getData(), (int) state.getSize());
            pumpUntil ([&] { return q.hosted.isLoaded(); });
            check (q.hosted.getLoadedName() == p.hosted.getLoadedName(), "hosted instrument restored from saved state");
            check (q.hosted.getRestoreInfo().startsWith ("restored from the saved set"), ("restore is reported: " + q.hosted.getRestoreInfo()).toStdString());

            // Play through the restored instance, then snapshot its window: the
            // restored instrument must be visible without scanning.
            int64_t qc = 0;
            run (q, ph, 48000.0, 120.0, 256, 256 * 20, qc, false, [] (int64_t start, int, juce::MidiBuffer& m)
            {
                if (start == 0) m.addEvents (noteAt (60, 0, true), 0, -1, 0);
            });
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor (q.createEditor());
                pumpUntil ([] { return false; }, 300);
                const auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds());
                const auto png = juce::File::getCurrentWorkingDirectory().getChildFile ("Tests/build/editor_restored.png");
                png.deleteFile();
                juce::FileOutputStream out (png);
                juce::PNGImageFormat().writeImageToStream (snapshot, out);
                std::cout << "  INFO  editor snapshot: " << png.getFullPathName() << "\n";
            }

            p.hosted.unload();
            check (! p.hosted.isLoaded(), "unload");
        }
    }

    //==========================================================================
    testutil::section ("A7  Hosted instrument settings survive save → restore (in-process)");
    {
        struct Target { const char* label; const char* format; const char* id; };
        const auto junoVst3 = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                  .getChildFile ("Library/Audio/Plug-Ins/VST3/Juno106.vst3").getFullPathName();
        for (auto t : { Target { "DLSMusicDevice (AU)", "AudioUnit", "AudioUnit:Synths/aumu,dls ,appl" },
                        Target { "Juno106 (AU)", "AudioUnit", "AudioUnit:Synths/aumu,Jn06,ARut" },
                        Target { "Juno106 (VST3)", "VST3", junoVst3.toRawUTF8() } })
        {
            SpikeProcessor p;
            FakePlayHead ph;
            prepare (p, ph, 48000.0, 256);

            juce::OwnedArray<juce::PluginDescription> found;
            for (auto* f : p.hosted.getFormatManager().getFormats())
                if (f->getName() == t.format)
                    f->findAllTypesForFile (found, t.id);
            if (found.isEmpty()) { std::cout << "  SKIP  " << t.label << " not installed\n"; continue; }

            juce::String error = "pending";
            p.hosted.load (*found[0], [&] (const juce::String& e) { error = e; });
            pumpUntil ([&] { return error != "pending"; });
            auto* inst = p.hosted.getInstance();
            if (inst == nullptr || inst->getParameters().isEmpty()) { check (false, std::string (t.label) + " loaded with parameters"); continue; }

            // Change every real parameter to a distinctive value, as a user editing a
            // patch would. (JUCE-built VST3s also expose ~2000 "MIDI CC" proxy
            // parameters that turn into controller messages, and a "Program" selector
            // that loads a factory preset; neither is part of the patch.)
            auto isProxy = [] (juce::AudioProcessorParameter* param)
            {
                const auto name = param->getName (64);
                return name.startsWith ("MIDI CC") || name.equalsIgnoreCase ("Bypass") || name.equalsIgnoreCase ("Program");
            };

            std::vector<int> indices;
            for (auto* param : inst->getParameters())
            {
                if (isProxy (param)) continue;
                param->setValueNotifyingHost (param->getValue() < 0.5f ? 0.8f : 0.2f);
                indices.push_back (param->getParameterIndex());
            }

            int64_t clk = 0;
            run (p, ph, 48000.0, 120.0, 256, 256 * 4, clk, false);   // let parameter changes reach the processor

            // Compare displayed values, so stepped parameters compare as the step they snapped to.
            std::vector<juce::String> wanted;
            for (int i : indices)
            {
                auto* param = inst->getParameters()[i];
                wanted.push_back (param->getText (param->getValue(), 64));
            }

            juce::MemoryBlock state;
            p.getStateInformation (state);

            SpikeProcessor q;
            prepare (q, ph, 48000.0, 256);
            q.setStateInformation (state.getData(), (int) state.getSize());
            pumpUntil ([&] { return q.hosted.isLoaded(); });

            auto* restored = q.hosted.getInstance();
            if (restored != nullptr)
            {
                int64_t c2 = 0;
                run (q, ph, 48000.0, 120.0, 256, 256 * 4, c2, false);
            }
            int same = 0, total = (int) wanted.size(), shown = 0;
            if (restored != nullptr)
                for (size_t k = 0; k < indices.size(); ++k)
                {
                    auto* rp = restored->getParameters()[indices[k]];
                    const auto got = rp->getText (rp->getValue(), 64);
                    const bool numeric = got.containsOnly ("-0123456789.") && wanted[k].containsOnly ("-0123456789.");
                    const bool ok = got == wanted[k]
                                 || (numeric && std::abs (got.getDoubleValue() - wanted[k].getDoubleValue()) < 1.0e-4);
                    same += ok ? 1 : 0;
                    if (! ok && shown++ < 8)
                        std::cout << "        " << rp->getName (40) << ": saved \"" << wanted[k] << "\" restored \"" << got << "\"\n";
                }

            check (restored != nullptr && same == total,
                   std::string (t.label) + ": " + std::to_string (same) + "/" + std::to_string (total) + " parameters restored");
        }
    }

    //==========================================================================
    testutil::section ("A8  CoreMIDI virtual port carries the same chords");
    {
        SpikeProcessor p;
        FakePlayHead ph;
        prepare (p, ph, 48000.0, 256);
        p.syncKey.store (false);
        p.setKeyFromUi (0);
        pumpUntil ([&] { return p.getPortEndpoint() != 0; }, 2000);
        check (p.getPortEndpoint() != 0, ("port opened: " + p.getPortStatus()).toStdString());

        PortListener listener;
        check (listener.connect (p.getPortEndpoint()), "test listener connected to the port");

        int64_t clock = 0;
        const auto before = (int64_t) ((__int128) mach_absolute_time() * [] { mach_timebase_info_data_t tb; mach_timebase_info (&tb); return tb; }().numer
                                       / [] { mach_timebase_info_data_t tb; mach_timebase_info (&tb); return tb; }().denom);
        run (p, ph, 48000.0, 120.0, 256, 256, clock, false, [] (int64_t, int, juce::MidiBuffer& m)
        {
            m.addEvents (noteAt (60, 128, true), 0, -1, 0);
        });
        pumpUntil ([&] { return listener.snapshot().size() >= 3; }, 2000);

        const auto got = listener.snapshot();
        std::multiset<int> notes;
        for (const auto& n : got) notes.insert (n.note);
        check (notes == cMajor, "port delivered C E G (" + std::to_string (got.size()) + " note-ons)");
        if (! got.empty())
        {
            const double offsetMs = (double) (got[0].ns - before) / 1.0e6;
            check (offsetMs > 0.0 && offsetMs < 50.0,
                   "port timestamp ≈ block time + 128 samples (" + std::to_string (offsetMs) + " ms after the block started)");
        }

        run (p, ph, 48000.0, 120.0, 256, 256, clock, false, [] (int64_t, int, juce::MidiBuffer& m)
        {
            m.addEvents (noteAt (60, 0, false), 0, -1, 0);
        });
    }

    //==========================================================================
    testutil::section ("A9  A hosted instrument that can't be restored is kept in the saved state");
    {
        SpikeProcessor p;
        FakePlayHead ph;
        prepare (p, ph, 48000.0, 256);

        juce::PluginDescription missing;
        missing.name = "Not Installed Synth";
        missing.pluginFormatName = "AudioUnit";
        missing.fileOrIdentifier = "AudioUnit:Synths/aumu,Nope,Nope";
        missing.isInstrument = true;

        juce::XmlElement saved ("ChordismSpike");
        auto* hostedXml = saved.createNewChildElement ("Hosted");
        hostedXml->addChildElement (missing.createXml().release());
        hostedXml->setAttribute ("state", juce::MemoryBlock ("abc", 3).toBase64Encoding());
        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary (saved, blob);

        p.setStateInformation (blob.getData(), (int) blob.getSize());
        pumpUntil ([&] { return p.hosted.getRestoreError().isNotEmpty(); }, 3000);
        check (p.hosted.getRestoreError().isNotEmpty(), ("restore reports an error: " + p.hosted.getRestoreError()).toStdString());

        juce::MemoryBlock resaved;
        p.getStateInformation (resaved);
        auto xml = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), (int) resaved.getSize());
        const auto* h = xml != nullptr ? xml->getChildByName ("Hosted") : nullptr;
        check (h != nullptr && h->getStringAttribute ("state") == hostedXml->getStringAttribute ("state"),
               "saving again keeps the hosted instrument and its state (nothing silently dropped)");
    }

    //==========================================================================
    testutil::section ("B1  Built VST3 bundle, loaded like a host (what Live sees)");
    {
        juce::AudioPluginFormatManager fm;
        juce::addDefaultFormatsToManager (fm);
        const auto vst3 = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                              .getChildFile ("Library/Audio/Plug-Ins/VST3/ChordismSpike.vst3").getFullPathName();

        juce::String error;
        auto inst1 = loadBuilt (fm, "VST3", vst3, error);
        auto inst2 = loadBuilt (fm, "VST3", vst3, error);
        check (inst1 != nullptr && inst2 != nullptr, ("two VST3 instances created " + error).toStdString());

        if (inst1 && inst2)
        {
            check (inst1->producesMidi() && inst1->getPluginDescription().isInstrument, "VST3 reports instrument + MIDI output");
            check (chordFromBuilt (*inst1, 60) == cMajor, "VST3 sends the C major triad as MIDI output events");

            const auto state = asVst3HostState (stateWithKey (7));
            inst1->setStateInformation (state.getData(), (int) state.getSize());
            check (chordFromBuilt (*inst2, 60) == std::multiset<int> { 67, 71, 74 },
                   "key G set on VST3 instance 1 reaches VST3 instance 2 (shared static across instances)");
            const auto reset = asVst3HostState (stateWithKey (0));
            inst1->setStateInformation (reset.getData(), (int) reset.getSize());
        }
    }

    testutil::section ("B3  Reopening a set: built VST3 restores its hosted instrument");
    {
        // A saved state with Apple's DLSMusicDevice hosted inside, made in-process.
        juce::MemoryBlock saved;
        {
            SpikeProcessor p;
            FakePlayHead ph;
            prepare (p, ph, 48000.0, 256);
            juce::OwnedArray<juce::PluginDescription> found;
            for (auto* f : p.hosted.getFormatManager().getFormats())
                if (f->getName() == "AudioUnit")
                    f->findAllTypesForFile (found, "AudioUnit:Synths/aumu,dls ,appl");
            juce::String error = "pending";
            if (found.size() == 1)
                p.hosted.load (*found[0], [&] (const juce::String& e) { error = e; });
            pumpUntil ([&] { return error != "pending"; });
            p.getStateInformation (saved);
        }

        std::vector<std::pair<juce::String, juce::MemoryBlock>> states { { "DLSMusicDevice state", saved } };
        if (auto* path = std::getenv ("SPIKE_STATE_FILE"))   // e.g. a state extracted from a Live set
        {
            juce::MemoryBlock fromFile;
            juce::File (path).loadFileAsData (fromFile);
            states.push_back ({ "state from " + juce::File (path).getFileName(), fromFile });
        }

        juce::AudioPluginFormatManager fm;
        juce::addDefaultFormatsToManager (fm);
        const auto vst3 = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                              .getChildFile ("Library/Audio/Plug-Ins/VST3/ChordismSpike.vst3").getFullPathName();

        for (const auto& [label, blob] : states)
        {
            for (bool stateBeforePrepare : { true, false })
            {
                juce::String error;
                auto inst = loadBuilt (fm, "VST3", vst3, error);
                if (inst == nullptr) { check (false, "VST3 created " + error.toStdString()); continue; }

                FakePlayHead ph;
                const auto hostState = asVst3HostState (blob);
                if (stateBeforePrepare)
                {
                    inst->setStateInformation (hostState.getData(), (int) hostState.getSize());
                    prepare (*inst, ph, 48000.0, 256);
                }
                else
                {
                    prepare (*inst, ph, 48000.0, 256);
                    inst->setStateInformation (hostState.getData(), (int) hostState.getSize());
                }

                // Give the async instrument load time to finish, then play a chord.
                float peak = 0.0f;
                int64_t clock = 0;
                for (int attempt = 0; attempt < 30 && peak < 1.0e-3f; ++attempt)
                {
                    pumpUntil ([] { return false; }, 100);
                    const auto first = clock;
                    run (*inst, ph, 48000.0, 120.0, 256, 256 * 40, clock, false, [first] (int64_t start, int, juce::MidiBuffer& m)
                    {
                        if (start == first) m.addEvents (noteAt (60, 0, true), 0, -1, 0);
                    }, &peak);
                    run (*inst, ph, 48000.0, 120.0, 256, 256 * 4, clock, false, [] (int64_t, int, juce::MidiBuffer& m)
                    {
                        m.addEvents (noteAt (60, 0, false), 0, -1, 0);
                    });
                }

                check (peak > 1.0e-3f, (label + (stateBeforePrepare ? ", state set before prepare (Live's order)" : ", state set after prepare")
                                        + ": hosted instrument plays after restore (peak " + juce::String (peak, 4) + ")").toStdString());
                inst->releaseResources();
            }
        }
    }

    testutil::section ("B4  Reopening a set: built AU restores its hosted instrument");
    {
        juce::AudioPluginFormatManager fm;
        juce::addDefaultFormatsToManager (fm);

        // An AU's saved state is its ClassInfo plist; JUCE keeps the plugin's own
        // state under "jucePluginState". Build one around an in-process state that
        // hosts DLSMusicDevice.
        juce::MemoryBlock inner;
        {
            SpikeProcessor p;
            FakePlayHead ph;
            prepare (p, ph, 48000.0, 256);
            juce::OwnedArray<juce::PluginDescription> found;
            for (auto* f : p.hosted.getFormatManager().getFormats())
                if (f->getName() == "AudioUnit")
                    f->findAllTypesForFile (found, "AudioUnit:Synths/aumu,dls ,appl");
            juce::String error = "pending";
            if (found.size() == 1)
                p.hosted.load (*found[0], [&] (const juce::String& e) { error = e; });
            pumpUntil ([&] { return error != "pending"; });
            p.getStateInformation (inner);
        }

        juce::MemoryBlock classInfo;
        {
            juce::String error;
            auto donor = loadBuilt (fm, "AudioUnit", "AudioUnit:Synths/aumu,CzSp,ARut", error);
            juce::MemoryBlock plist;
            if (donor) donor->getStateInformation (plist);

            CFDataRef data = CFDataCreate (nullptr, (const UInt8*) plist.getData(), (CFIndex) plist.getSize());
            auto dict = (CFMutableDictionaryRef) CFPropertyListCreateWithData (nullptr, data, kCFPropertyListMutableContainers, nullptr, nullptr);
            CFRelease (data);
            if (dict != nullptr)
            {
                CFDataRef state = CFDataCreate (nullptr, (const UInt8*) inner.getData(), (CFIndex) inner.getSize());
                CFDictionarySetValue (dict, CFSTR ("jucePluginState"), state);
                CFRelease (state);
                if (CFDataRef out = CFPropertyListCreateData (nullptr, dict, kCFPropertyListBinaryFormat_v1_0, 0, nullptr))
                {
                    classInfo.append (CFDataGetBytePtr (out), (size_t) CFDataGetLength (out));
                    CFRelease (out);
                }
                CFRelease (dict);
            }
        }
        check (classInfo.getSize() > 0, "built an AU ClassInfo state with a hosted DLSMusicDevice");

        std::vector<std::pair<juce::String, juce::MemoryBlock>> states { { "DLSMusicDevice ClassInfo", classInfo } };
        if (auto* path = std::getenv ("SPIKE_AU_CLASSINFO"))   // a ClassInfo plist taken from a Live set
        {
            juce::MemoryBlock fromFile;
            juce::File (path).loadFileAsData (fromFile);
            states.push_back ({ "ClassInfo from " + juce::File (path).getFileName(), fromFile });
        }

        for (const auto& [label, blob] : states)
        {
            for (bool stateBeforePrepare : { true, false })
            {
                juce::String error;
                auto inst = loadBuilt (fm, "AudioUnit", "AudioUnit:Synths/aumu,CzSp,ARut", error);
                if (inst == nullptr) { check (false, "AU created " + error.toStdString()); continue; }

                FakePlayHead ph;
                if (stateBeforePrepare)
                {
                    inst->setStateInformation (blob.getData(), (int) blob.getSize());
                    prepare (*inst, ph, 48000.0, 256);
                }
                else
                {
                    prepare (*inst, ph, 48000.0, 256);
                    inst->setStateInformation (blob.getData(), (int) blob.getSize());
                }

                float peak = 0.0f;
                int64_t clock = 0;
                for (int attempt = 0; attempt < 30 && peak < 1.0e-3f; ++attempt)
                {
                    pumpUntil ([] { return false; }, 100);
                    const auto first = clock;
                    run (*inst, ph, 48000.0, 120.0, 256, 256 * 40, clock, false, [first] (int64_t start, int, juce::MidiBuffer& m)
                    {
                        if (start == first) m.addEvents (noteAt (60, 0, true), 0, -1, 0);
                    }, &peak);
                    run (*inst, ph, 48000.0, 120.0, 256, 256 * 4, clock, false, [] (int64_t, int, juce::MidiBuffer& m)
                    {
                        m.addEvents (noteAt (60, 0, false), 0, -1, 0);
                    });
                }

                check (peak > 1.0e-3f, (label + (stateBeforePrepare ? ", state set before prepare (Live's order)" : ", state set after prepare")
                                        + ": hosted instrument plays after restore (peak " + juce::String (peak, 4) + ")").toStdString());
                inst->releaseResources();
            }
        }
    }

    testutil::section ("B2  Built AU (aumu), loaded like a host");
    {
        juce::AudioPluginFormatManager fm;
        juce::addDefaultFormatsToManager (fm);
        juce::String error;
        auto inst = loadBuilt (fm, "AudioUnit", "AudioUnit:Synths/aumu,CzSp,ARut", error);
        check (inst != nullptr, ("AU instance created " + error).toStdString());
        if (inst)
        {
            const auto notes = chordFromBuilt (*inst, 60);
            std::cout << "  INFO  AU MIDI output seen by a JUCE host: " << notes.size() << " note(s) (Live ignores AU MIDI output either way)\n";
        }
    }

    return testutil::finish ("Chordism Phase 0 spike");
}
