// Phase 8 hardening (PLAN.md §7 row 8): the engine under hosts outside the ranges the other
// suites sweep (R8 asks for 32-2048-sample blocks at 44.1/48/96 kHz; a host may do more).
//
//   extremes      the test pattern's chords on the beat (within 1 sample) at 8-384 kHz, 20-999
//                 BPM and 1-16384-sample blocks, and everything on at once in the same hosts
//   time          time signatures from 1/1 to 32/32 (and a nonsense 0/0), a count-in from
//                 negative song time, song time in the millions of beats, loops shorter than a
//                 sample, a loop whose end is before its start
//   hostile       a transport that jitters, scrubs backwards, changes tempo and sample rate on
//                 every block, and toggles play; zero-length blocks; events stamped outside
//                 their block
//   floods        2048 input events in a block, every key held with every effect stacking,
//                 the output buffer never overflowing a note-off
//
// Each one must keep the output clean (no double note-on, stray note-off or event outside its
// block), agree with VoiceTracker, and leave nothing sounding once the keys are up and the
// transport stops.
#include "FakeHost.h"
#include <chrono>
#include <random>

using namespace chordism;
using namespace fakehost;
using testutil::check;

namespace
{
    // Every on-beat check: each note-on within `tol` samples of a beat, each note-off of a half
    // beat (the test pattern's keys are an eighth long), 3 notes per beat (C major Triads) for
    // every beat the run covered. Musical time is the host's own (each block's ppq), so a host
    // whose position drifts in its last bits is measured against itself.
    std::string onTheBeat (const Host& h, int64_t tol, int64_t& worst)
    {
        const double spq = h.sampleRate * 60.0 / h.bpm;
        auto beatAt = [&] (int64_t sample)
        {
            auto it = std::upper_bound (h.blockStarts.begin(), h.blockStarts.end(), sample,
                                        [] (int64_t s, const auto& b) { return s < b.first; });
            const auto& b = *(it - 1);
            return b.second + (double) (sample - b.first) / spq;
        };
        int ons = 0;
        std::ostringstream why;
        for (const auto& hit : h.hits)
        {
            const double b = beatAt (hit.sample);
            const double grid = hit.on ? std::round (b) : std::round (b - 0.5) + 0.5;
            const auto d = (int64_t) std::llround (std::abs (b - grid) * spq);
            worst = std::max (worst, d);
            if (d > tol)
            {
                why << " " << (hit.on ? "on " : "off ") << hit.note << " " << d << " samples off the grid at beat " << b << ";";
                break;
            }
            ons += hit.on ? 1 : 0;
        }
        const double from = h.blockStarts.front().second, to = h.ppq;
        const auto beats = (int64_t) (std::ceil (to - 1.0e-9) - std::ceil (from - 1.0e-9));
        if (ons != 3 * beats)
            why << " " << ons << " note-ons for " << beats << " beats;";
        return why.str();
    }

    // Every chord and Smart Scale effect at once (the heaviest the engine gets).
    void everythingOn (EngineSettings& s)
    {
        s.testPattern = true;
        s.testShifter = 1;
        s.testSmart = TestPattern::smartPattern + 2;   // pattern key 3 held (slot 3: Converge Up ×2)
        s.group = ChordGroup::Thirteenths;
        s.fx.stack = StackMode::PlusOneAndTwo;
        s.fx.arp = ArpPattern::BoomerangUp;
        s.fx.arpRate = numPatternRates - 1;            // 1/32T
        s.fx.arpRange = 3;
        s.fx.repeat = { true, numPatternRates - 1, 0.7 };
        s.fx.seq.on = true;
        s.fx.seq.sustain = true;
        s.fx.seq.swing = 0.6;
        s.fx.velocity = { VelocityCurve::House, 120, 0.8 };
        s.fx.hold = HoldMode::Bar;
        s.fx.voiceLeading = VoiceLeadingMode::Smooth;
        s.smart.stack = StackMode::PlusOneAndTwo;
        s.smart.noteRepeat = true;
        s.smart.patterns[2].rate = numPatternRates - 1;
        s.smart.patterns[2].range = 3;
        s.smart.patterns[2].chord = ArpChord::Thirteenths;
        s.smart.patterns[0].melody = true;
        s.smart.patterns[0].notes = 16;
        s.smart.seq.on = true;
        s.smart.velocity = { VelocityCurve::Breakbeat, 110, 0.5 };
        s.smart.hold = HoldMode::Latch;
        s.lookAhead = numLookAheads - 1;
    }

    // Ends a run: the transport stops, then the test pattern and the holds go.
    bool endsClean (Host& h)
    {
        h.stop();
        h.settings.testPattern = false;
        h.settings.fx.hold = HoldMode::Off;
        h.settings.smart.hold = HoldMode::Off;
        h.run (h.pos + 64, 64);
        return h.clean();
    }

    struct Extreme
    {
        double rate, bpm;
        int block;
        double beats;
    };

    std::vector<Extreme> extremes()
    {
        std::vector<Extreme> c;
        for (double rate : { 8000.0, 22050.0, 192000.0, 384000.0 })
            for (double bpm : { 20.0, 300.0, 999.0 })
                for (int block : { 4096, 8192, 16384 })
                    c.push_back ({ rate, bpm, block, bpm < 100.0 ? 3.0 : 12.0 });
        // One- and three-sample blocks (a host splitting at every event).
        for (double rate : { 8000.0, 44100.0 })
            for (double bpm : { 120.0, 999.0 })
                for (int block : { 1, 3 })
                    c.push_back ({ rate, bpm, block, 4.0 });
        c.push_back ({ 384000.0, 20.0, 32, 2.0 });
        return c;
    }

    std::string describe (const Extreme& c)
    {
        std::ostringstream o;
        o << c.rate / 1000.0 << "k/" << c.bpm << "/" << c.block;
        return o.str();
    }
}

//==============================================================================
static void testExtremes()
{
    std::cout << "== extreme hosts ==\n";
    const auto all = extremes();
    int failed = 0, heavyFailed = 0;
    int64_t worst = 0;
    std::string first, heavyFirst;
    int maxSounding = 0;
    double slowest = 0.0;
    for (const auto& c : all)
    {
        Host h (c.rate, c.bpm);
        h.settings.testPattern = true;
        h.run (h.at (c.beats), c.block);
        int64_t w = 0;
        std::string why = onTheBeat (h, 1, w);
        worst = std::max (worst, w);
        if (! endsClean (h))
            why += " not clean: " + h.rx.describe();
        if (! why.empty() && failed++ == 0)
            first = " first failure " + describe (c) + ":" + why;

        // Everything on: clean, the queues in bound, nothing stuck.
        Host x (c.rate, c.bpm);
        everythingOn (x.settings);
        const auto t0 = std::chrono::steady_clock::now();
        for (int64_t until = x.at (c.beats), step = std::max<int64_t> (c.block, x.at (0.25)); x.pos < until;)
        {
            x.run (std::min (until, x.pos + step), c.block);
            maxSounding = std::max (maxSounding, (int) x.rx.on.size());
            const auto& p = x.engine->getPerformer();
            const auto& s = x.engine->getSmartPerformer();
            if (p.queued() > 4 * p.aliveCount() || s.queued() > 4 * s.aliveCount())
            {
                x.rx.doubleOns += 1000000;   // marks the run as failed
                break;
            }
        }
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        slowest = std::max (slowest, seconds / (c.beats * 60.0 / c.bpm));   // seconds of CPU per second of audio
        if (! endsClean (x) && heavyFailed++ == 0)
            heavyFirst = " first failure " + describe (c) + ": " + x.rx.describe();
    }
    check (failed == 0, "test pattern on the beat: " + std::to_string (all.size() - (size_t) failed) + "/" + std::to_string (all.size())
                            + " hosts (8-384 kHz, 20-999 BPM, 1-16384-sample blocks), worst " + std::to_string (worst) + " sample(s)" + first);
    check (heavyFailed == 0, "everything on: " + std::to_string (all.size() - (size_t) heavyFailed) + "/" + std::to_string (all.size())
                                 + " hosts clean, queues in bound, nothing left (peak " + std::to_string (maxSounding)
                                 + " notes)" + heavyFirst);
    check (slowest < 0.5, "everything on runs faster than real time in every host (slowest: "
                              + std::to_string (slowest * 100.0) + "% of real time)");
}

//==============================================================================
static void testTime()
{
    std::cout << "== musical time ==\n";

    // Time signatures, with and without the host's bar position.
    {
        int failed = 0;
        int64_t worst = 0;
        std::string first;
        const std::pair<int, int> sigs[] = { { 1, 1 }, { 1, 16 }, { 15, 16 }, { 32, 32 }, { 7, 64 }, { 99, 4 }, { 0, 0 }, { 5, 0 } };
        for (const auto& [num, den] : sigs)
            for (const bool bars : { false, true })
                for (const bool heavy : { false, true })
                {
                    Host h (44100.0, 120.0);
                    h.numerator = num;
                    h.denominator = den;
                    h.reportBarStart = bars;
                    h.settings.testPattern = true;
                    if (heavy)
                        everythingOn (h.settings);
                    h.run (h.at (16.0), 512);
                    int64_t w = 0;
                    std::string why = heavy ? std::string() : onTheBeat (h, 1, w);
                    worst = std::max (worst, w);
                    if (! endsClean (h))
                        why += " not clean: " + h.rx.describe();
                    if (! why.empty() && failed++ == 0)
                        first = " first failure " + std::to_string (num) + "/" + std::to_string (den) + (heavy ? " heavy" : "") + ":" + why;
                }
        check (failed == 0, "time signatures 1/1, 1/16, 15/16, 32/32, 7/64, 99/4, 0/0, 5/0 (with and without bar positions, "
                                "plain and everything on): worst " + std::to_string (worst) + " sample(s)" + first);
    }

    // A count-in: playback starts 4 beats before the song's start.
    {
        Host h (48000.0, 120.0);
        h.ppq = -4.0;
        h.reportBarStart = true;
        h.settings.testPattern = true;
        h.run (h.at (8.0), 333);
        int64_t w = 0;
        std::string why = onTheBeat (h, 1, w);
        if (! endsClean (h))
            why += " not clean: " + h.rx.describe();
        int firstSlot = -1;
        for (const auto& hit : h.hits)
            if (hit.on) { firstSlot = hit.note; break; }
        // Beat -4 plays slot 3 (-4 mod 7): F's chord, root F3 = 53 in C major.
        check (why.empty() && firstSlot == 53, "a count-in from beat -4 plays on every beat, slot -4 mod 7 first (F, "
                                                   + std::to_string (firstSlot) + ")" + why);

        Host x (48000.0, 120.0);
        x.ppq = -4.0;
        everythingOn (x.settings);
        x.run (x.at (8.0), 333);
        check (endsClean (x), "and with everything on: " + x.rx.describe());
    }

    // Song time far from zero: a host that's been playing for days.
    {
        int failed = 0;
        int64_t worst = 0;
        std::string first;
        for (const double start : { 1.0e6, 1.0e8, 4.0e9 })
            for (const int block : { 64, 1024 })
            {
                Host h (44100.0, 140.0);
                h.ppq = start;
                h.settings.testPattern = true;
                h.run (h.at (8.0), block);
                int64_t w = 0;
                std::string why = onTheBeat (h, 1, w);
                worst = std::max (worst, w);
                if (! endsClean (h))
                    why += " not clean: " + h.rx.describe();
                if (! why.empty() && failed++ == 0)
                    first = " first failure at " + std::to_string (start) + ":" + why;
            }
        check (failed == 0, "song time from 10^6 to 4·10^9 beats: on the beat, worst " + std::to_string (worst) + " sample(s)" + first);
    }

    // Loops shorter than a block (and than a sample), and backwards loop points.
    {
        int failed = 0;
        std::string first;
        const std::pair<double, double> loops[] = { { 0.0, 0.01 }, { 1.0, 1.0 + 1.0e-5 }, { 2.0, 2.0 + 1.0e-7 }, { 4.0, 4.0 },
                                                    { 8.0, 4.0 }, { 0.0, 0.25 } };
        for (const auto& [from, to] : loops)
            for (const auto style : { LoopStyle::Wrap, LoopStyle::Split, LoopStyle::SplitDropOvershoot })
                for (const int block : { 128, 8192 })
                {
                    Host h (44100.0, 999.0);
                    h.looping = true;
                    h.loopStart = from;
                    h.loopEnd = to;
                    h.style = style;
                    h.ppq = from;
                    everythingOn (h.settings);
                    // A host stuck in a sub-sample loop splits every sample: keep it short.
                    h.run (to - from < 0.001 ? 4096 : 44100, block);
                    if (! endsClean (h) && failed++ == 0)
                        first = " first failure loop " + std::to_string (from) + "-" + std::to_string (to) + ": " + h.rx.describe();
                }
        check (failed == 0, "loops of 0.01, 10^-5 and 10^-7 beats, empty and backwards, with three host loop styles: clean" + first);
    }
}

//==============================================================================
// A host that does everything a host shouldn't, block after block.
static void testHostile (uint32_t seed)
{
    std::mt19937 rng (seed);
    auto chance = [&] (double p) { return std::uniform_real_distribution<double> (0.0, 1.0) (rng) < p; };
    auto pick = [&] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); };
    auto real = [&] (double lo, double hi) { return std::uniform_real_distribution<double> (lo, hi) (rng); };

    ChordismEngine engine;
    EngineSettings s;
    everythingOn (s);
    Transport t { 44100.0, true, 0.0, 120.0, 4, 4 };
    BlockEvents out;
    testutil::Receiver rx;
    std::set<int> down;
    int zeroBlocks = 0, outside = 0;

    for (int b = 0; b < 6000; ++b)
    {
        const int n = chance (0.05) ? 0 : (chance (0.2) ? pick (1, 16384) : 1 << pick (0, 13));
        zeroBlocks += n == 0 ? 1 : 0;

        // Keys, some stamped before or after their block (hosts do), in sample order.
        std::vector<MidiEvent> in;
        const int events = chance (0.4) ? pick (1, 24) : 0;
        std::vector<int> at;
        for (int i = 0; i < events; ++i)
            at.push_back (chance (0.1) ? pick (-100, -1) : (chance (0.1) ? n + pick (0, 100) : pick (0, std::max (0, n - 1))));
        std::sort (at.begin(), at.end());
        for (const int sample : at)
        {
            outside += sample < 0 || sample >= n ? 1 : 0;
            if (chance (0.55) || down.empty())
            {
                const int note = pick (36, 84);
                in.push_back (MidiEvent::noteOn (pick (0, 1), note, pick (1, 127), sample));
                down.insert (note);
            }
            else
            {
                auto it = down.begin();
                std::advance (it, pick (0, (int) down.size() - 1));
                in.push_back (MidiEvent::noteOff (0, *it, sample));
                down.erase (it);
            }
        }

        // The transport: jitter, scrubbing, tempo and rate on every block, play toggles.
        if (t.playing)
        {
            t.ppq += n / t.samplesPerBeat();
            if (chance (0.3)) t.ppq += real (-0.5, 0.5) / t.samplesPerBeat();   // sub-sample jitter
            if (chance (0.05)) t.ppq -= real (0.0, 2.0);                         // scrub back
            if (chance (0.02)) t.ppq = real (-8.0, 1.0e5);
        }
        if (chance (0.2)) t.bpm = real (20.0, 999.0);
        if (chance (0.05)) t.sampleRate = std::array<double, 5> { 8000.0, 22050.0, 44100.0, 96000.0, 384000.0 }[(size_t) pick (0, 4)];
        if (chance (0.05)) t.playing = ! t.playing;
        if (chance (0.05))
        {
            t.looping = chance (0.7);
            t.loopStart = real (-2.0, 8.0);
            t.loopEnd = t.loopStart + (chance (0.2) ? real (-1.0, 0.01) : real (0.01, 4.0));
        }
        if (chance (0.05)) { t.numerator = pick (0, 32); t.denominator = 1 << pick (0, 6); }
        t.hasBarStart = chance (0.5);
        t.barStartPpq = std::floor (t.ppq / 4.0) * 4.0 + (chance (0.1) ? real (-3.0, 3.0) : 0.0);
        if (chance (0.02)) s.fx.hold = (HoldMode) pick (0, 2);
        if (chance (0.02)) s.testPattern = ! s.testPattern;
        if (chance (0.01)) engine.requestReset();

        out.clear();
        engine.process (s, t, in.data(), (int) in.size(), n, out);
        rx.feed (out, n);
        // VoiceTracker's per-note view (the editor's keyboard) agrees with what was sent.
        bool agree = true;
        for (int note = 0; note < 128 && agree; ++note)
        {
            bool sent = false;
            for (int ch = 0; ch < 16; ++ch)
                sent |= rx.on.count ({ ch, note }) != 0;
            agree = sent == engine.getVoices().isSounding (note);
        }
        if ((int) rx.on.size() != engine.getVoices().soundingNotes() || ! rx.clean() || ! agree)
        {
            rx.doubleOns += agree ? 0 : 1000000;   // marks the run as failed
            break;
        }
    }

    std::vector<MidiEvent> ups;
    for (const int note : down)
        ups.push_back (MidiEvent::noteOff (0, note, 0));
    s.fx.hold = HoldMode::Off;
    s.smart.hold = HoldMode::Off;
    s.testPattern = false;
    t.playing = false;
    for (int i = 0; i < 2; ++i)
    {
        out.clear();
        engine.process (s, t, i == 0 ? ups.data() : nullptr, i == 0 ? (int) ups.size() : 0, 64, out);
        rx.feed (out, 64);
    }
    check (rx.clean() && rx.on.empty() && engine.getVoices().isEmpty() && rx.noteOns == rx.noteOffs,
           "hostile host, seed " + std::to_string (seed) + " (6000 blocks, VoiceTracker agreeing note by note; " + std::to_string (zeroBlocks) + " of zero length, "
               + std::to_string (outside) + " events outside their block; jitter, scrubbing, tempo, rate and loop changes): "
               + rx.describe());
}

//==============================================================================
static void testFloods()
{
    std::cout << "== floods ==\n";

    // 2048 events in one block: every key down and up eight times over, then all down.
    // Twice: every effect (arps, one note at a time), and stacked chords restruck by Repeat
    // with Hold (as many notes as can sound).
    for (const bool arps : { true, false })
    {
        ChordismEngine engine;
        EngineSettings s;
        everythingOn (s);
        if (! arps)
        {
            s.fx.arp = ArpPattern::Off;
            s.fx.seq.on = false;
            s.smart.noteRepeat = false;
            s.testSmart = TestPattern::smartOff;
            s.fx.hold = HoldMode::Latch;
        }
        Transport t { 44100.0, true, 0.0, 120.0, 4, 4 };
        BlockEvents out;
        testutil::Receiver rx;
        int peakOut = 0, peakSounding = 0;
        for (int round = 0; round < 8; ++round)
        {
            std::vector<MidiEvent> in;
            for (int i = 0; i < 2048; ++i)
            {
                const int note = i % 128;
                in.push_back (i / 128 % 2 == 0 ? MidiEvent::noteOn (0, note, 100, i / 4) : MidiEvent::noteOff (0, note, i / 4));
            }
            if (round == 7)
                for (int i = 1920; i < 2048; ++i)
                    in[(size_t) i] = MidiEvent::noteOn (0, i % 128, 100, 511);   // the last 128 stay down
            out.clear();
            engine.process (s, t, in.data(), (int) in.size(), 512, out);
            rx.feed (out, 512);
            peakOut = std::max (peakOut, out.size());
            peakSounding = std::max (peakSounding, (int) rx.on.size());
            t.ppq += 512 / t.samplesPerBeat();
        }
        // Everything held for two bars of 1/32T arps on every key, stacked.
        for (int b = 0; b < 344; ++b)
        {
            out.clear();
            engine.process (s, t, nullptr, 0, 512, out);
            rx.feed (out, 512);
            peakOut = std::max (peakOut, out.size());
            peakSounding = std::max (peakSounding, (int) rx.on.size());
            t.ppq += 512 / t.samplesPerBeat();
        }
        const int sounding = (int) rx.on.size();
        std::vector<MidiEvent> ups;
        for (int note = 0; note < 128; ++note)
            ups.push_back (MidiEvent::noteOff (0, note, 0));
        s.fx.hold = HoldMode::Off;
        s.smart.hold = HoldMode::Off;
        s.testPattern = false;
        t.playing = false;
        out.clear();
        engine.process (s, t, ups.data(), (int) ups.size(), 512, out);
        rx.feed (out, 512);
        peakOut = std::max (peakOut, out.size());
        check (rx.clean() && rx.on.empty() && engine.getVoices().isEmpty() && peakOut < BlockEvents::capacity,
               std::string ("2048 events a block, then all 128 keys held, ") + (arps ? "every effect" : "stacked chords, Repeat 1/32T, Hold")
                   + " (peak " + std::to_string (peakSounding) + " notes sounding, " + std::to_string (sounding) + " at the end; peak "
                   + std::to_string (peakOut) + " events a block of " + std::to_string (BlockEvents::capacity) + "): " + rx.describe());
    }

    // The output can't take another note: VoiceTracker keeps half the buffer for note-offs,
    // so a release of everything always fits.
    {
        VoiceTracker v;
        EventBuffer<64> small;
        int started = 0;
        for (int note = 0; note < 128; ++note)
            started += v.start ((VoiceTracker::Source) note, 0, note, 100, 0, small) ? 1 : 0;
        const int ons = small.size();
        v.releaseAll (0, small);
        check (started == 32 && ons == 32 && small.size() == 64 && v.isEmpty(),
               "a full output refuses note-ons but always has room for their note-offs (" + std::to_string (started)
                   + " of 128 started in 64 slots, all released)");
    }
}

int main()
{
    testExtremes();
    testTime();
    std::cout << "== hostile host ==\n";
    for (uint32_t seed = 1; seed <= 6; ++seed)
        testHostile (seed);
    testFloods();
    return testutil::finish ("test_edge");
}
