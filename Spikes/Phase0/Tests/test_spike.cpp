// Phase 0 spike tests. Part A drives SpikeProcessor directly with a fake Live
// transport. Part B loads the *built* VST3 and AU bundles the way a host would,
// to check what Live will actually see (MIDI output events, shared statics).
// Run via Tests/run_tests.sh after a Release build.
#include "PluginProcessor.h"
#include "TestUtil.h"
#include <CoreFoundation/CoreFoundation.h>
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

            p.hosted.unload();
            check (! p.hosted.isLoaded(), "unload");
        }
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
