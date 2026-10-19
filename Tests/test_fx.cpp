// Phase 3 tests (PLAN.md §7 row 3, §8): the Clock, the EventQueue, the effect building blocks
// (Stack, Strum, Arp, Step Sequencer, Velocity Algorithm), and the chord-zone effect chain
// under a fake host transport. Every timing test runs at 32-2048-sample blocks and 44.1, 48 and
// 96 kHz, and must put each note-on and note-off within one sample of where its musical time
// falls, through loop wraps (hosts that split their blocks at the loop end and hosts that don't),
// locates and tempo changes, and while the transport is stopped.
#include "TestUtil.h"
#include "Receiver.h"
#include "core/engine/ChordismEngine.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

using namespace chordism;
using testutil::check;

namespace
{
    struct Hit
    {
        int64_t sample = 0;
        int note = 0;
        bool on = false;
        int velocity = 0;
    };

    enum class LoopStyle
    {
        Wrap,              // the host doesn't split blocks; its next block starts after the wrap
        Split,             // the host ends a block at the loop end
        SplitDropOvershoot // …and starts the next exactly at the loop start
    };

    // A fake host: plays the engine block by block with a transport that can loop, change tempo
    // at block boundaries and stop; feeds it key events at absolute sample positions.
    struct Host
    {
        std::unique_ptr<ChordismEngine> engine = std::make_unique<ChordismEngine>();
        EngineSettings settings;
        double sampleRate = 48000.0, bpm = 120.0;
        bool playing = true;
        double ppq = 0.0;
        int numerator = 4, denominator = 4;
        bool looping = false;
        double loopStart = 0.0, loopEnd = 0.0;
        LoopStyle style = LoopStyle::Wrap;
        bool reportBarStart = false;

        std::vector<std::pair<int64_t, MidiEvent>> input;
        std::vector<std::pair<int64_t, double>> tempo;   // (absolute sample, bpm), from the first block at or after it
        int64_t pos = 0;
        size_t nextInput = 0, nextTempo = 0;

        std::vector<Hit> hits;
        testutil::Receiver rx;
        std::vector<std::pair<int64_t, double>> blockStarts;   // (absolute sample, ppq) of each block

        Host (double rate, double tempoBpm) : sampleRate (rate), bpm (tempoBpm) {}

        double spq() const { return sampleRate * 60.0 / bpm; }
        int64_t at (double beats) const { return (int64_t) std::llround (beats * spq()); }

        void keyAt (int64_t sample, int note, bool down, int velocity = 100)
        {
            input.push_back ({ sample, down ? MidiEvent::noteOn (0, note, velocity, 0) : MidiEvent::noteOff (0, note, 0) });
        }

        // A key at `beats` (constant tempo from ppq 0 at sample 0).
        void key (double beats, int note, bool down, int velocity = 100) { keyAt (at (beats), note, down, velocity); }

        void run (int64_t until, int blockSize)
        {
            std::stable_sort (input.begin() + (long) nextInput, input.end(),
                              [] (const auto& a, const auto& b) { return a.first < b.first; });
            BlockEvents out;
            std::vector<MidiEvent> in;
            while (pos < until)
            {
                while (nextTempo < tempo.size() && tempo[nextTempo].first <= pos)
                    bpm = tempo[nextTempo++].second;

                int n = (int) std::min<int64_t> (blockSize, until - pos);
                const double samplesPerQuarter = spq();
                if (playing && looping && style != LoopStyle::Wrap && ppq < loopEnd - 1.0e-12)
                {
                    const int split = (int) std::ceil ((loopEnd - ppq) * samplesPerQuarter - 1.0e-6);
                    if (split >= 1 && split < n)
                        n = split;
                }

                Transport t { sampleRate, playing, ppq, bpm, numerator, denominator };
                t.looping = looping;
                t.loopStart = loopStart;
                t.loopEnd = loopEnd;
                if (reportBarStart)
                {
                    t.hasBarStart = true;
                    t.barStartPpq = std::floor (ppq / t.barLength() + 1.0e-9) * t.barLength();
                }

                in.clear();
                while (nextInput < input.size() && input[nextInput].first < pos + n)
                {
                    MidiEvent e = input[nextInput].second;
                    e.sample = (int) (input[nextInput].first - pos);
                    in.push_back (e);
                    ++nextInput;
                }

                blockStarts.push_back ({ pos, ppq });
                out.clear();
                engine->process (settings, t, in.data(), (int) in.size(), n, out);
                rx.feed (out, n);
                for (const auto& e : out)
                    if (e.isNoteOn() || e.isNoteOff())
                        hits.push_back ({ pos + e.sample, e.note(), e.isNoteOn(), e.isNoteOn() ? e.velocity() : 0 });

                if (playing)
                {
                    double next = ppq + n / samplesPerQuarter;
                    if (looping && ppq < loopEnd && next >= loopEnd - 1.0e-9)
                        next = style == LoopStyle::SplitDropOvershoot ? loopStart : next - (loopEnd - loopStart);
                    ppq = next;
                }
                pos += n;
            }
        }

        // Stops the transport for one block: everything still sounding ends at `pos`.
        void stop (int blockSize = 64)
        {
            playing = false;
            run (pos + blockSize, blockSize);
        }

        bool clean() const { return rx.clean() && rx.on.empty() && engine->getVoices().isEmpty() && rx.noteOns == rx.noteOffs; }
    };

    // The expected note list, from musical times through a beats → samples mapping.
    struct Expect
    {
        std::function<double (double)> toSample;
        std::vector<Hit> hits;

        explicit Expect (double spq) : toSample ([spq] (double beats) { return beats * spq; }) {}
        explicit Expect (std::function<double (double)> f) : toSample (std::move (f)) {}

        int64_t s (double beats) const { return (int64_t) std::llround (toSample (beats)); }

        void on (double beats, int note, int velocity = 100) { hits.push_back ({ s (beats), note, true, velocity }); }
        void off (double beats, int note) { hits.push_back ({ s (beats), note, false, 0 }); }
        void note (double from, double to, int note, int velocity = 100) { on (from, note, velocity); off (to, note); }
        void onAt (int64_t sample, int note, int velocity = 100) { hits.push_back ({ sample, note, true, velocity }); }
        void offAt (int64_t sample, int note) { hits.push_back ({ sample, note, false, 0 }); }
    };

    // Pairs every expected hit with a recorded one (same note, on/off and velocity, within
    // `tolerance` samples). Returns "" if all match, else what didn't.
    std::string compare (const std::vector<Hit>& got, const std::vector<Hit>& want, int64_t& worst, int64_t tolerance = 1)
    {
        std::vector<bool> used (got.size(), false);
        std::ostringstream why;
        int missing = 0;
        worst = 0;
        for (const auto& w : want)
        {
            int best = -1;
            int64_t bestD = tolerance + 1;
            for (size_t i = 0; i < got.size(); ++i)
            {
                const auto& g = got[i];
                if (used[i] || g.note != w.note || g.on != w.on || (w.on && g.velocity != w.velocity))
                    continue;
                const int64_t d = std::llabs (g.sample - w.sample);
                if (d < bestD)
                {
                    bestD = d;
                    best = (int) i;
                }
            }
            if (best < 0)
            {
                if (missing++ < 4)
                    why << " missing " << (w.on ? "on " : "off ") << w.note << " v" << w.velocity << " @" << w.sample << ";";
                continue;
            }
            used[(size_t) best] = true;
            worst = std::max (worst, bestD);
        }
        int extra = 0;
        for (size_t i = 0; i < got.size(); ++i)
            if (! used[i] && extra++ < 4)
                why << " extra " << (got[i].on ? "on " : "off ") << got[i].note << " v" << got[i].velocity << " @" << got[i].sample << ";";
        if (missing > 0 || extra > 0)
            why << " (" << missing << " missing, " << extra << " extra of " << want.size() << ")";
        return why.str();
    }

    struct Config
    {
        double rate, bpm;
        int block;
    };

    std::vector<Config> configs()
    {
        std::vector<Config> c;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 32, 64, 128, 333, 512, 1024, 2048 })
                c.push_back ({ rate, 120.0, block });
        for (int block : { 32, 333, 2048 })
            c.push_back ({ 44100.0, 97.0, block });
        for (int block : { 64, 512 })
            c.push_back ({ 96000.0, 174.0, block });
        return c;
    }

    std::string describe (const Config& c)
    {
        std::ostringstream o;
        o << c.rate / 1000.0 << "k/" << c.bpm << "/" << c.block;
        return o.str();
    }

    // Runs `scenario` in every configuration; one check line for all of them.
    void sweep (const std::string& name, const std::function<std::string (const Config&, int64_t& worst)>& scenario)
    {
        int failed = 0;
        int64_t worst = 0;
        std::string first;
        const auto all = configs();
        for (const auto& c : all)
        {
            int64_t w = 0;
            const std::string why = scenario (c, w);
            worst = std::max (worst, w);
            if (! why.empty())
                if (failed++ == 0)
                    first = " first failure " + describe (c) + ":" + why;
        }
        check (failed == 0, name + ": " + std::to_string (all.size() - (size_t) failed) + "/" + std::to_string (all.size())
                                + " configurations, worst " + std::to_string (worst) + " sample(s)" + first);
    }

    // Finishes a scenario: compare, and check the stream and the tracker are clean.
    std::string finish (Host& h, const Expect& e, int64_t& worst, int64_t tolerance = 1)
    {
        std::string why = compare (h.hits, e.hits, worst, tolerance);
        if (! h.clean())
            why += " not clean: " + h.rx.describe();
        return why;
    }

    constexpr int C = 48, D = 50, E = 52, G = 55, Csharp = 49;
    const std::vector<int> cMajor { 48, 52, 55 }, dMinor { 50, 53, 57 }, gMajor { 55, 59, 62 }, eMinor { 52, 55, 59 };
}

//==============================================================================
static void testClock()
{
    std::cout << "== clock ==\n";
    Clock c;
    Transport t { 48000.0, true, 1.0, 120.0, 4, 4 };
    c.beginBlock (t, 480);
    check (c.numSegments() == 1 && c.ppqAt (0, 240) == 1.0 + 240.0 / 24000.0 && ! c.jumpedAtStart(),
           "playing: one segment, ppq at a sample");
    t.ppq = 1.0 + 480.0 / 24000.0;
    c.beginBlock (t, 480);
    check (! c.jumpedAtStart(), "a block that carries on is no jump");
    t.ppq = 9.0;
    c.beginBlock (t, 480);
    check (c.jumpedAtStart() && std::abs (c.jumpDelta() - (9.0 - (1.0 + 960.0 / 24000.0))) < 1e-9, "a locate is a jump, with its distance");

    // A loop end inside the block: split at the first sample at or past it.
    t.looping = true;
    t.loopStart = 0.0;
    t.loopEnd = 4.0;
    t.ppq = 3.99;
    c.beginBlock (t, 1024);
    const auto& s1 = c.segment (1);
    check (c.numSegments() == 2 && c.segment (0).end == 240 && s1.start == 240 && std::abs (s1.ppq - 0.0) < 1e-9
               && std::abs (c.wrapDelta (1) + 4.0) < 1e-9,
           "loop wrap inside a block: two segments at sample 240, wrap delta -4");
    t.ppq = (1024 - 240) / 24000.0;
    c.beginBlock (t, 512);
    check (! c.jumpedAtStart(), "the host's next block after the wrap continues (no jump)");

    // Tiny loop, big block: several wraps.
    t.loopStart = 0.0;
    t.loopEnd = 0.01;   // 240 samples
    t.ppq = 0.0;
    c.beginBlock (t, 1000);
    check (c.numSegments() == 5 && c.segment (4).start == 960, "a 240-sample loop in a 1000-sample block: 5 segments");

    // Tempo change, then a stop: the internal clock runs on at the last tempo.
    t.looping = false;
    t.bpm = 90.0;
    t.ppq = 10.0;
    c.beginBlock (t, 100);
    check (c.tempoChanged() && std::abs (c.samplesPerQuarter() - 32000.0) < 1e-9, "tempo change seen; 90 BPM = 32000 samples a beat");
    t.playing = false;
    c.beginBlock (t, 100);
    const double stoppedAt = c.ppqAt (0, 0);
    check (! c.isPlaying() && std::abs (stoppedAt - (10.0 + 100.0 / 32000.0)) < 1e-9, "stopped: internal clock carries on from the host");
    c.anchor (40);
    check (std::abs (c.ppqAt (0, 40)) < 1e-12 && c.barStart (c.ppqAt (0, 99)) == 0.0, "anchor: bar 1 at the anchored sample");
    const int64_t before = c.blockStart();
    c.beginBlock (t, 100);
    check (std::abs (c.ppqAt (0, 0) - 60.0 / 32000.0) < 1e-12 && c.blockStart() == before + 100, "the internal clock runs on; absolute samples count");
    t.playing = true;
    t.ppq = 0.0;
    c.beginBlock (t, 100);
    check (c.jumpedAtStart() && std::abs (c.jumpDelta() + 160.0 / 32000.0) < 1e-12, "transport start: a jump from internal to host time");

    // Bars from the host's bar start (3/4).
    t.numerator = 3;
    t.hasBarStart = true;
    t.barStartPpq = 6.0;
    t.ppq = 7.5;
    c.beginBlock (t, 100);
    check (c.barLength() == 3.0 && c.barStart (7.5) == 6.0 && c.nextBarLine (7.5) == 9.0 && std::abs (c.barFraction (7.5) - 0.5) < 1e-12,
           "3/4 from the host's bar start: bar lines at 6, 9");
}

static void testEventQueue()
{
    std::cout << "== event queue ==\n";
    Clock c;
    Transport t { 48000.0, true, 0.0, 120.0, 4, 4 };
    c.beginBlock (t, 512);
    EventQueue<8> q;
    q.push (TimedEvent::musical (100.0 / 24000.0, 1, 2, 0, 0, 1));    // start at 100
    q.push (TimedEvent::musical (100.0 / 24000.0, 2, 0, 0, 0, 2));    // end at 100
    q.push (TimedEvent::atSample (50, 3, 2, 0, 0, 3));                // samples: 50
    q.push (TimedEvent::musical (1.0, 4, 0, 1, 0, 4));                // beat 1: not in this block
    q.push (TimedEvent::musical (100.0 / 24000.0, 5, 0, 0, 0, 5));    // end at 100, pushed later

    std::vector<uint32_t> order;
    int at = 0, i;
    while ((i = q.earliestDue (c, 0, 0, 512, at)) >= 0)
        order.push_back (q.take (i).arg);
    check (order == std::vector<uint32_t> { 3, 2, 5, 1 } && q.size() == 1, "by sample, then ends before starts, then push order");

    q.forEach ([] (TimedEvent& e) { e.ppq -= 0.99; });
    check (q.earliestDue (c, 0, 0, 512, at) == 0 && at == 240, "moved musical events find their new sample");
    q.removeIf ([] (const TimedEvent& e) { return e.owner == 1; });
    check (q.isEmpty(), "removeIf");
    for (int n = 0; n < 8; ++n)
        q.push (TimedEvent::musical (0.0, 0, 0, 0, 0, 0));
    check (! q.push (TimedEvent::musical (0.0, 0, 0, 0, 0, 0)) && q.size() == 8, "fixed capacity: a full queue refuses");
    q.clear();
    q.push (TimedEvent::musical (-1.0, 0, 0, 0, 0, 7));
    check (q.earliestDue (c, 0, 30, 512, at) == 0 && at == 30, "an event in the past comes out at the floor");
}

static void testBuildingBlocks()
{
    std::cout << "== stack, strum, arp, step grid, velocity ==\n";
    ChordNotes triad;
    triad.notes = { 48, 52, 55 };
    triad.size = 3;
    auto list = [] (const VoicedNotes& v) { return std::vector<int> (v.begin(), v.end()); };
    check (list (stackNotes (triad, StackMode::Off)) == std::vector<int> { 48, 52, 55 }, "Stack Off");
    check (list (stackNotes (triad, StackMode::PlusOne)) == std::vector<int> { 48, 52, 55, 60, 64, 67 }, "Stack +1");
    check (list (stackNotes (triad, StackMode::PlusTwo)) == std::vector<int> { 48, 52, 55, 72, 76, 79 }, "Stack +2");
    check (list (stackNotes (triad, StackMode::PlusOneAndTwo)) == std::vector<int> { 48, 52, 55, 60, 64, 67, 72, 76, 79 }, "Stack +1&2");
    ChordNotes high;
    high.notes = { 100, 112, 124 };
    high.size = 3;
    check (list (stackNotes (high, StackMode::PlusOneAndTwo)) == std::vector<int> { 100, 112, 124 }, "Stack drops pitches above 127 and duplicates");
    check (list (arpTones (stackNotes (triad, StackMode::Off), 3)) == std::vector<int> { 48, 52, 55, 60, 64, 67, 72, 76, 79 }, "Arp range 3");

    Strummer s;
    uint8_t o[4];
    auto ord = [&] (StrumMode m) { s.order (m, 4, o); return std::vector<int> (o, o + 4); };
    check (ord (StrumMode::Up) == std::vector<int> { 0, 1, 2, 3 } && ord (StrumMode::Down) == std::vector<int> { 3, 2, 1, 0 }, "Strum Up, Down");
    s.reset();
    check (ord (StrumMode::Alternating) == std::vector<int> { 0, 1, 2, 3 } && ord (StrumMode::Alternating) == std::vector<int> { 3, 2, 1, 0 }
               && ord (StrumMode::Alternating) == std::vector<int> { 0, 1, 2, 3 },
           "Strum Alternating: up, down, up");
    s.reset();
    std::vector<std::vector<int>> first, second;
    for (int n = 0; n < 6; ++n) first.push_back (ord (StrumMode::Random));
    s.reset();
    for (int n = 0; n < 6; ++n) second.push_back (ord (StrumMode::Random));
    bool perms = true, varied = false;
    for (auto v : first)
    {
        varied |= v != first[0];
        std::sort (v.begin(), v.end());
        perms &= v == std::vector<int> { 0, 1, 2, 3 };
    }
    check (perms && varied && first == second, "Strum Random: permutations, varied, the same after a reseed");

    // The twelve arp patterns over four tones (Arp.h).
    struct P { ArpPattern p; std::vector<int> seq; };
    const std::vector<P> patterns {
        { ArpPattern::LinearUp, { 0, 1, 2, 3, 0 } },          { ArpPattern::LinearDown, { 3, 2, 1, 0, 3 } },
        { ArpPattern::ConvergeUp, { 0, 3, 1, 2, 0 } },        { ArpPattern::ConvergeDown, { 3, 0, 2, 1, 3 } },
        { ArpPattern::DivergeUp, { 1, 2, 0, 3, 1 } },         { ArpPattern::DivergeDown, { 2, 1, 3, 0, 2 } },
        { ArpPattern::StutterUp, { 0, 0, 1, 1, 2, 2, 3, 3, 0 } }, { ArpPattern::StutterDown, { 3, 3, 2, 2, 1, 1, 0, 0, 3 } },
        { ArpPattern::ZigzagUp, { 0, 2, 1, 3, 0 } },          { ArpPattern::ZigzagDown, { 3, 1, 2, 0, 3 } },
        { ArpPattern::BoomerangUp, { 0, 1, 2, 3, 2, 1, 0 } }, { ArpPattern::BoomerangDown, { 3, 2, 1, 0, 1, 2, 3 } },
    };
    bool allPatterns = true;
    std::string bad;
    for (const auto& p : patterns)
    {
        std::vector<int> got;
        for (int k = 0; k < (int) p.seq.size(); ++k)
            got.push_back (arpIndex (p.p, 4, k));
        if (got != p.seq)
        {
            allPatterns = false;
            bad += std::string (" ") + arpPatternName (p.p);
        }
    }
    check (allPatterns, "the 12 arp patterns over 4 tones" + bad);
    bool inRange = true;
    for (int p = 1; p < numArpPatterns; ++p)
        for (int n = 1; n <= 12; ++n)
            for (int k = 0; k < 40; ++k)
            {
                const int i = arpIndex ((ArpPattern) p, n, k);
                inRange &= i >= 0 && i < n;
            }
    check (inRange && arpIndex (ArpPattern::LinearUp, 0, 0) == -1, "every pattern stays within 1-12 tones; no tones, no index");

    // Step grid: swing, gate, sustain.
    StepSeqSettings seq;
    seq.patterns[0] = 0x0003;   // steps 1, 2
    seq.swing = 1.0;
    seq.gate = 0.5;
    StepGrid g (seq, 0.0, 4.0);
    const auto w0 = g.windowAt (0.1), w1 = g.windowAt (0.4);
    check (w0.valid && w0.start == 0.0 && std::abs (w0.end - 0.1875) < 1e-12 && w1.valid && std::abs (w1.start - 0.375) < 1e-12
               && std::abs (w1.end - 0.4375) < 1e-12 && ! g.windowAt (0.3).valid,
           "swing 100%: step 2 at 0.375 (a dotted sixteenth), gate 50% of each slot");
    double next = 0.0;
    check (g.nextStart (0.0, false, next) && std::abs (next - 0.375) < 1e-12 && g.nextStart (0.5, true, next) && next == 4.0,
           "next active steps: 0.375, then the next bar");
    seq.swing = 0.0;
    seq.patterns[0] = 0x0101;   // steps 1, 9
    seq.sustain = true;
    seq.gate = 1.0;
    const auto sus = StepGrid (seq, 0.0, 4.0).windowAt (1.9);
    seq.gate = 0.5;
    const auto susHalf = StepGrid (seq, 0.0, 4.0).windowAt (1.0);
    check (sus.valid && sus.start == 0.0 && std::abs (sus.end - 2.0) < 1e-12 && susHalf.valid && std::abs (susHalf.end - 1.875) < 1e-12,
           "sustain: step 1 sounds to step 9 (gate on the last sixteenth)");
    seq.patterns[0] = 0x8000;   // step 16 only: never in a 3/4 bar
    check (! StepGrid (seq, 0.0, 3.0).nextStart (0.0, true, next), "3/4: step 16 never comes");

    VelocitySettings v;
    check (v.apply (77, 0.3) == 77, "Velocity Off keeps the key's velocity");
    v.curve = VelocityCurve::Up;
    v.peak = 100;
    v.range = 1.0;
    check (v.apply (77, 0.0) == 1 && v.apply (77, 0.999) == 100 && v.apply (77, 0.5) == 53, "Up, Range 100%: 1 .. Peak across the bar");
    v.range = 0.0;
    check (v.apply (77, 0.0) == 100, "Range 0%: always Peak");
    v.curve = VelocityCurve::DownUp;
    v.range = 0.5;
    check (v.apply (10, 0.5) == 50 && v.apply (10, 0.0) == 100, "Down/Up: a dip mid-bar");
    bool curvesOk = true;
    for (int c = 1; c < numVelocityCurves; ++c)
        for (int i = 0; i < 16; ++i)
        {
            const double x = velocityCurveValue ((VelocityCurve) c, i);
            curvesOk &= x >= 0.0 && x <= 1.0;
        }
    check (curvesOk, "all 13 curves stay within 0..1");
}

//==============================================================================
// Timing sweeps: each runs in every configuration (configs()).

static void testStrumTiming()
{
    std::cout << "== strum ==\n";
    sweep ("Strum Up 1/32 and Down 1/16, cut short by the key-up", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.strum = StrumMode::Up;
        h.settings.fx.strumRate = 4;   // 1/32
        h.key (1.0, C, true);
        h.key (2.0, C, false);
        h.run (h.at (2.5), c.block);
        h.settings.fx.strum = StrumMode::Down;
        h.settings.fx.strumRate = 6;   // 1/16
        h.key (3.0, D, true);
        h.key (4.0, D, false);
        h.key (5.0, G, true);
        h.key (5.3, G, false);         // before the third tone
        h.run (h.at (6.0), c.block);

        Expect e (h.spq());
        for (int i = 0; i < 3; ++i)
            e.note (1.0 + 0.125 * i, 2.0, cMajor[(size_t) i]);
        for (int i = 0; i < 3; ++i)
            e.note (3.0 + 0.25 * i, 4.0, dMinor[(size_t) (2 - i)]);
        e.note (5.0, 5.3, gMajor[2]);
        e.note (5.25, 5.3, gMajor[1]);
        return finish (h, e, worst);
    });

    sweep ("Strum Alternating 1/64 on Repeat 1/4 (gate 50%)", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.strum = StrumMode::Alternating;
        h.settings.fx.strumRate = 2;   // 1/64
        h.settings.fx.repeat = { true, 1, 0.5 };
        h.key (0.0, C, true);
        h.key (3.0, C, false);         // on the next strike's grid line: no strike there
        h.run (h.at (3.5), c.block);

        Expect e (h.spq());
        for (int strike = 0; strike < 3; ++strike)
            for (int i = 0; i < 3; ++i)
                e.note (strike + 0.0625 * i, strike + 0.5, cMajor[(size_t) (strike % 2 == 0 ? i : 2 - i)]);
        return finish (h, e, worst);
    });

    // Stopped: Strum Rate in ms, from the key press.
    sweep ("Strum Up while stopped: 30 ms between tones", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.playing = false;
        h.settings.fx.strum = StrumMode::Up;
        h.settings.fx.strumMs = 30.0;
        const int64_t press = 1000, up = 1000 + (int64_t) (0.5 * c.rate);
        h.keyAt (press, C, true);
        h.keyAt (up, C, false);
        h.run (up + 100, c.block);

        Expect e (h.spq());
        for (int i = 0; i < 3; ++i)
        {
            e.onAt (press + (int64_t) std::llround (i * 0.030 * c.rate), cMajor[(size_t) i]);
            e.offAt (up, cMajor[(size_t) i]);
        }
        return finish (h, e, worst);
    });
}

static void testArpTiming()
{
    std::cout << "== arp ==\n";
    for (auto p : { ArpPattern::LinearUp, ArpPattern::ConvergeDown, ArpPattern::BoomerangUp, ArpPattern::StutterDown })
        sweep (std::string ("Arp ") + arpPatternName (p) + " 1/16, gate 50%, range 2", [p] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            h.settings.fx.arp = p;
            h.settings.fx.arpRate = 5;
            h.settings.fx.arpGate = 0.5;
            h.settings.fx.arpRange = 2;
            h.key (0.0, C, true);
            h.key (3.0, C, false);
            h.run (h.at (3.5), c.block);

            const std::vector<int> tones { 48, 52, 55, 60, 64, 67 };
            Expect e (h.spq());
            for (int k = 0; k < 12; ++k)
                e.note (0.25 * k, 0.25 * k + 0.125, tones[(size_t) arpIndex (p, 6, k)]);
            return finish (h, e, worst);
        });

    sweep ("Arp 1/8T gate 100%, pressed off the grid", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.arp = ArpPattern::LinearUp;
        h.settings.fx.arpRate = 4;     // 1/8T
        h.settings.fx.arpGate = 1.0;
        h.key (1.1, G, true);
        h.key (2.6, G, false);
        h.run (h.at (3.0), c.block);

        Expect e (h.spq());
        const double steps[] = { 1.1, 4.0 / 3.0, 5.0 / 3.0, 2.0, 7.0 / 3.0, 2.6 };
        for (int k = 0; k < 5; ++k)
            e.note (steps[k], steps[k + 1], gMajor[(size_t) (k % 3)]);
        return finish (h, e, worst);
    });

    // A shifter mid-arp: the sounding tone isn't cut; the next step plays the new chord.
    sweep ("Arp follows a shifter at its next step", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.arp = ArpPattern::LinearUp;
        h.settings.fx.arpRate = 1;     // 1/4
        h.settings.fx.arpGate = 1.0;
        h.key (0.0, C, true);
        h.key (1.5, Csharp, true);     // shifter 1: Sevenths
        h.key (4.0, C, false);
        h.key (4.0, Csharp, false);
        h.run (h.at (4.5), c.block);

        Expect e (h.spq());
        const int notes[] = { 48, 52, 55, 59 };   // C, then C7's tones 2 and 3
        for (int k = 0; k < 4; ++k)
            e.note (k, k + 1, notes[k]);
        return finish (h, e, worst);
    });

    sweep ("Arp while stopped: 1/8 at the last tempo, from the key press", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.playing = false;
        h.settings.fx.arp = ArpPattern::LinearUp;
        h.settings.fx.arpRate = 3;     // 1/8
        h.settings.fx.arpGate = 0.5;
        const int64_t press = 777;
        h.keyAt (press, C, true);
        h.keyAt (press + h.at (2.0), C, false);
        h.run (press + h.at (2.5), c.block);

        Expect e (h.spq());
        for (int k = 0; k < 4; ++k)
        {
            e.onAt (press + h.at (0.5 * k), cMajor[(size_t) (k % 3)]);
            e.offAt (press + h.at (0.5 * k + 0.25), cMajor[(size_t) (k % 3)]);
        }
        return finish (h, e, worst);
    });
}

static void testRepeatAndSeqTiming()
{
    std::cout << "== repeat, step sequencer ==\n";
    sweep ("Repeat 1/8 gate 50%, on and off the grid", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.repeat = { true, 3, 0.5 };
        h.key (0.5, D, true);
        h.key (2.0, D, false);
        h.key (2.45, G, true);         // a twentieth of a beat before the grid: sounds through it
        h.key (3.25, G, false);
        h.run (h.at (3.5), c.block);

        Expect e (h.spq());
        for (double t : { 0.5, 1.0, 1.5 })
            for (int n : dMinor)
                e.note (t, t + 0.25, n);
        for (int n : gMajor)
        {
            e.note (2.45, 2.45 + 0.5 * 0.55, n);
            e.note (3.0, 3.25, n);     // the key-up cuts the second strike
        }
        return finish (h, e, worst);
    });

    sweep ("Step seq four-on-the-floor gate 100%, press inside and late in a step", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.seq.on = true;
        h.settings.fx.seq.pattern = 0;   // steps 1 5 9 13
        h.settings.fx.seq.gate = 1.0;
        h.key (0.0, C, true);
        h.key (8.0, C, false);
        h.key (9.1, D, true);          // inside step 5's window: sounds at once
        h.key (10.6, D, false);
        h.key (11.22, E, true);        // too little of step 13 left: waits for the next bar
        h.key (12.5, E, false);
        h.run (h.at (13.0), c.block);

        Expect e (h.spq());
        for (int b = 0; b < 8; ++b)
            for (int n : cMajor)
                e.note (b, b + 0.25, n);
        for (int n : dMinor)
        {
            e.note (9.1, 9.25, n);
            e.note (10.0, 10.25, n);
        }
        for (int n : eMinor)
            e.note (12.0, 12.25, n);
        return finish (h, e, worst);
    });

    sweep ("Step seq swing 100% gate 50%, and sustain", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        auto& seq = h.settings.fx.seq;
        seq.on = true;
        seq.pattern = 1;
        seq.patterns[1] = 0x0003;      // steps 1, 2
        seq.swing = 1.0;
        seq.gate = 0.5;
        h.key (0.0, C, true);
        h.key (8.0, C, false);
        h.run (h.at (8.0), c.block);   // two bars swung
        seq.swing = 0.0;
        seq.patterns[1] = 0x0101;      // steps 1, 9
        seq.sustain = true;
        h.key (8.0, D, true);
        h.key (16.0, D, false);
        h.run (h.at (16.5), c.block);

        Expect e (h.spq());
        for (int bar = 0; bar < 2; ++bar)
            for (int n : cMajor)
            {
                e.note (4.0 * bar, 4.0 * bar + 0.1875, n);
                e.note (4.0 * bar + 0.375, 4.0 * bar + 0.4375, n);
            }
        for (int half = 0; half < 4; ++half)
            for (int n : dMinor)
                e.note (8.0 + 2.0 * half, 8.0 + 2.0 * half + 1.875, n);
        return finish (h, e, worst);
    });

    sweep ("Step seq with Repeat 1/32 inside its windows, Velocity Up across the bar", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        auto& fx = h.settings.fx;
        fx.seq.on = true;
        fx.seq.pattern = 2;
        fx.seq.patterns[2] = 0x0101;   // steps 1, 9
        fx.seq.gate = 1.0;
        fx.repeat = { true, 7, 0.5 };  // 1/32
        fx.velocity = { VelocityCurve::Up, 120, 1.0 };
        h.key (0.0, G, true);
        h.key (5.0, G, false);
        h.run (h.at (5.5), c.block);

        Expect e (h.spq());
        for (double t : { 0.0, 0.125, 2.0, 2.125, 4.0, 4.125 })
        {
            const int step = (int) std::floor (std::fmod (t, 4.0) * 4.0 + 1e-9);
            const int vel = std::max (1, (int) std::lround (120.0 * step / 15.0));
            for (int n : gMajor)
                e.note (t, t + 0.0625, n, vel);
        }
        return finish (h, e, worst);
    });

    sweep ("Arp gated by the step sequencer", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        auto& fx = h.settings.fx;
        fx.seq.on = true;
        fx.seq.pattern = 0;            // steps 1 5 9 13
        fx.seq.gate = 0.5;             // windows [b, b + 0.125)
        fx.arp = ArpPattern::LinearUp;
        fx.arpRate = 7;                // 1/32: steps at b and b + 0.125 …
        fx.arpGate = 1.0;
        h.key (0.0, C, true);
        h.key (4.0, C, false);
        h.run (h.at (4.5), c.block);

        Expect e (h.spq());
        for (int b = 0; b < 4; ++b)    // only the step on each beat is inside a window; 8 arp steps a beat
            e.note (b, b + 0.125, cMajor[(size_t) ((8 * b) % 3)]);
        return finish (h, e, worst);
    });
}

static void testHoldAndStack()
{
    std::cout << "== hold, stack, restrike ==\n";
    sweep ("Hold Latch: until the next chord key; ends on stop", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.hold = HoldMode::Latch;
        h.key (0.0, C, true);
        h.key (0.5, C, false);
        h.key (2.0, D, true);
        h.key (2.5, D, false);
        h.key (3.0, G, true);
        h.key (3.1, G, false);
        h.run (h.at (4.0), c.block);
        const int64_t stopAt = h.pos;
        h.stop();

        Expect e (h.spq());
        for (int n : cMajor) e.note (0.0, 2.0, n);
        for (int n : dMinor) e.note (2.0, 3.0, n);
        for (int n : gMajor) { e.on (3.0, n); e.offAt (stopAt, n); }
        return finish (h, e, worst);
    });

    sweep ("Hold Bar: to the bar line after the release, or the next chord", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.hold = HoldMode::Bar;
        h.settings.fx.repeat = { true, 1, 0.5 };   // and the repeats carry on while held
        h.key (1.0, C, true);
        h.key (1.5, C, false);
        h.key (4.5, D, true);
        h.key (5.0, D, false);
        h.key (6.0, E, true);
        h.key (7.5, E, false);
        h.run (h.at (8.5), c.block);

        Expect e (h.spq());
        for (double t : { 1.0, 2.0, 3.0 }) for (int n : cMajor) e.note (t, t + 0.5, n);
        for (int n : dMinor) { e.note (4.5, 4.5 + 0.5 * 0.5, n); e.note (5.0, 5.5, n); }
        for (double t : { 6.0, 7.0 }) for (int n : eMinor) e.note (t, t + 0.5, n);
        return finish (h, e, worst);
    });

    sweep ("Hold while stopped: the same key again ends it", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.playing = false;
        h.settings.fx.hold = HoldMode::Bar;
        h.keyAt (1000, C, true);
        h.keyAt (2000, C, false);
        h.keyAt (50000, C, true);      // ends the held C; starts nothing
        h.keyAt (51000, C, false);
        h.keyAt (60000, D, true);
        h.keyAt (61000, D, false);
        h.keyAt (70000, D, true);
        h.run (80000, c.block);

        Expect e (h.spq());
        for (int n : cMajor) { e.onAt (1000, n); e.offAt (50000, n); }
        for (int n : dMinor) { e.onAt (60000, n); e.offAt (70000, n); }
        std::string why = compare (h.hits, e.hits, worst);
        if (h.engine->getPerformer().aliveCount() != 0)
            why += " a performance is still alive";
        return why;
    });

    sweep ("Stack +1&2, a Stack change and a shifter restrike a strummed chord", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.stack = StackMode::PlusOneAndTwo;
        h.key (0.0, C, true);
        h.key (1.0, C, false);
        h.run (h.at (2.0), c.block);
        h.settings.fx.stack = StackMode::Off;
        h.settings.fx.strum = StrumMode::Up;
        h.settings.fx.strumRate = 6;   // 1/16
        h.key (2.0, C, true);
        h.key (3.0, Csharp, true);     // shifter 1: Sevenths, restrums from beat 3
        h.key (4.5, C, false);
        h.key (4.5, Csharp, false);
        h.run (h.at (4.0), c.block);
        h.settings.fx.stack = StackMode::PlusOne;   // at the block that starts at beat 4
        const double change = h.ppq;
        h.run (h.at (5.0), c.block);

        Expect e (h.spq());
        for (int n : { 48, 52, 55, 60, 64, 67, 72, 76, 79 })
            e.note (0.0, 1.0, n);
        for (int i = 0; i < 3; ++i)
            e.note (2.0 + 0.25 * i, 3.0, cMajor[(size_t) i]);
        const int c7[] = { 48, 52, 55, 59 }, c7up[] = { 48, 52, 55, 59, 60, 64, 67, 71 };
        for (int i = 0; i < 4; ++i)
            e.note (3.0 + 0.25 * i, change, c7[i]);
        for (int i = 0; i < 8; ++i)
            if (change + 0.25 * i < 4.5 - 1e-9)
                e.note (change + 0.25 * i, 4.5, c7up[i]);
        return finish (h, e, worst);
    });
}

//==============================================================================
// Loop wraps, tempo changes, transport start.

static void testLoops()
{
    std::cout << "== loop wraps ==\n";
    for (auto style : { LoopStyle::Wrap, LoopStyle::Split, LoopStyle::SplitDropOvershoot })
    {
        const std::string styleName = style == LoopStyle::Wrap ? "host doesn't split" : style == LoopStyle::Split ? "host splits" : "host splits, drops the overshoot";
        for (double loopLen : { 4.0, 3.5 })
            sweep ("Arp 1/4 across a " + std::to_string (loopLen).substr (0, 3) + "-beat loop (" + styleName + ")",
                   [style, loopLen] (const Config& c, int64_t& worst)
            {
                Host h (c.rate, c.bpm);
                h.looping = true;
                h.loopStart = 0.0;
                h.loopEnd = loopLen;
                h.style = style;
                h.settings.fx.arp = ArpPattern::LinearUp;
                h.settings.fx.arpRate = 1;
                h.settings.fx.arpGate = 0.5;
                h.key (0.0, C, true);
                h.run (h.at (3.0 * loopLen - 0.25), c.block);
                const int64_t stopAt = h.pos;
                h.stop();

                // In song time each pass plays steps 0, 1, 2, 3 (< loop end); the pattern runs on.
                Expect e (h.spq());
                int k = 0;
                for (int pass = 0; pass < 3; ++pass)
                    for (int j = 0; j < 4 && j < loopLen; ++j)
                    {
                        const double t = pass * loopLen + j;
                        if (t >= 3.0 * loopLen - 0.25)
                            break;
                        const int note = cMajor[(size_t) (k++ % 3)];
                        e.on (t, note);
                        if (t + 0.5 < 3.0 * loopLen - 0.25 + 1e-9) e.off (t + 0.5, note); else e.offAt (stopAt, note);
                    }
                // A host that drops the overshoot loses up to a sample of song time per wrap.
                return finish (h, e, worst, style == LoopStyle::SplitDropOvershoot ? 3 : 1);
            });

        sweep ("Strummed repeats and a step sequencer across loops (" + styleName + ")", [style] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            h.looping = true;
            h.loopStart = 0.0;
            h.loopEnd = 2.0;
            h.style = style;
            h.settings.fx.strum = StrumMode::Up;
            h.settings.fx.strumRate = 2;   // 1/64
            h.settings.fx.repeat = { true, 3, 0.5 };   // 1/8
            h.key (0.0, C, true);
            h.key (5.75, C, false);        // absolute beats
            h.run (h.at (6.0), c.block);

            // Then a 3.5-beat loop with four-on-the-floor steps, from its start: the step on beat 1
            // plays at each wrap.
            h.loopEnd = 3.5;
            h.ppq = 0.0;
            h.settings.fx.strum = StrumMode::Off;
            h.settings.fx.repeat.on = false;
            h.settings.fx.seq.on = true;
            h.settings.fx.seq.gate = 1.0;
            const int64_t base = h.pos;
            h.keyAt (base, D, true);
            h.keyAt (base + h.at (3.5 * 2 + 3.25), D, false);
            h.run (base + h.at (3.5 * 2 + 3.5), c.block);

            Expect e (h.spq());
            for (int strike = 0; strike < 12; ++strike)
                for (int i = 0; i < 3; ++i)
                    e.note (0.5 * strike + 0.0625 * i, 0.5 * strike + 0.25, cMajor[(size_t) i]);
            for (int pass = 0; pass < 3; ++pass)
                for (int j = 0; j < 4; ++j)
                    for (int n : dMinor)
                    {
                        e.onAt (base + h.at (3.5 * pass + j), n);
                        e.offAt (base + h.at (3.5 * pass + j + 0.25), n);
                    }
            return finish (h, e, worst, style == LoopStyle::SplitDropOvershoot ? 6 : 1);
        });
    }
}

static void testTempoAndStart()
{
    std::cout << "== tempo changes, transport start ==\n";
    sweep ("Arp 1/16 through a tempo change (120 → 90 BPM, and 120 → 160)", [] (const Config& c, int64_t& worst)
    {
        std::string why;
        for (double to : { 90.0, 160.0 })
        {
            Host h (c.rate, 120.0);
            h.settings.fx.arp = ArpPattern::LinearUp;
            h.settings.fx.arpRate = 5;
            h.settings.fx.arpGate = 0.5;
            const double spq1 = h.spq();
            h.tempo.push_back ({ (int64_t) (2.1 * spq1), to });
            h.key (0.0, C, true);
            h.run ((int64_t) (2.1 * spq1) + 8 * 4096, c.block);   // well past the change
            // Where the change took effect: the first block at or after 2.1 beats.
            int64_t changeAt = 0;
            double changePpq = 0.0;
            for (const auto& [abs, ppq] : h.blockStarts)
                if (abs >= (int64_t) (2.1 * spq1)) { changeAt = abs; changePpq = ppq; break; }
            const double spq2 = c.rate * 60.0 / to;
            auto map = [=] (double beats) { return beats <= changePpq ? beats * spq1 : (double) changeAt + (beats - changePpq) * spq2; };
            const double endBeats = changePpq + (double) (h.pos - changeAt) / spq2;
            const int64_t stopAt = h.pos;
            h.stop();

            Expect e (map);
            for (int k = 0; 0.25 * k < endBeats - 1e-9; ++k)
            {
                const int note = cMajor[(size_t) (k % 3)];
                e.on (0.25 * k, note);
                if (0.25 * k + 0.125 < endBeats) e.off (0.25 * k + 0.125, note); else e.offAt (stopAt, note);
            }
            int64_t w = 0;
            why += finish (h, e, w);
            worst = std::max (worst, w);
        }
        return why;
    });

    sweep ("Arp started while stopped, then the transport starts: onto the host grid", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.playing = false;
        h.settings.fx.arp = ArpPattern::LinearUp;
        h.settings.fx.arpRate = 1;     // 1/4
        h.settings.fx.arpGate = 0.5;
        const int64_t press = 500;
        h.keyAt (press, C, true);
        h.run (press + h.at (1.7), c.block);
        const int64_t startAt = h.pos;
        h.playing = true;
        h.ppq = 0.0;
        h.run (startAt + h.at (2.75), c.block);
        const int64_t stopAt = h.pos;
        h.stop();

        // Stopped: steps at press + k beats. At the start a step on the host's beat 0, then each beat.
        Expect e (h.spq());
        int k = 0;
        for (; press + h.at (k) < startAt; ++k)
        {
            const int note = cMajor[(size_t) (k % 3)];
            e.onAt (press + h.at (k), note);
            e.offAt (std::min (press + h.at (k + 0.5), startAt), note);
        }
        for (int b = 0; b < 3; ++b, ++k)
        {
            const int note = cMajor[(size_t) (k % 3)];
            e.onAt (startAt + h.at (b), note);
            e.offAt (b + 0.5 < 2.75 ? startAt + h.at (b + 0.5) : stopAt, note);
        }
        return finish (h, e, worst);
    });

    sweep ("Locate while a repeat plays: the note-off keeps its length, repeats re-grid", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.repeat = { true, 1, 0.5 };   // 1/4, half a beat long
        h.key (0.0, C, true);
        h.run (h.at (1.2), c.block);   // strikes at 0 and 1; the second is sounding
        const int64_t jumpAt = h.pos;
        const double from = h.ppq;
        h.ppq = 10.6;                  // locate forward
        h.run (jumpAt + h.at (1.0), c.block);
        const int64_t stopAt = h.pos;
        h.stop();

        Expect e (h.spq());
        for (int n : cMajor)
        {
            e.note (0.0, 0.5, n);
            e.on (1.0, n);
            e.offAt (jumpAt + h.at (1.5 - from), n);        // 1.5 in the old time = 10.6 + 0.3 - …
            e.onAt (jumpAt + h.at (11.0 - 10.6), n);        // the next grid line after the locate
            e.offAt (std::min (jumpAt + h.at (11.5 - 10.6), stopAt), n);
        }
        return finish (h, e, worst);
    });
}

//==============================================================================
// Determinism: the same playing gives the same output, including Random strum.

static void testDeterminism()
{
    std::cout << "== determinism ==\n";
    auto play = [] (int block)
    {
        Host h (44100.0, 120.0);
        h.settings.fx.strum = StrumMode::Random;
        h.settings.fx.strumRate = 0;   // 1/128: all three tones inside each strike
        h.settings.fx.repeat = { true, 5, 0.75 };
        h.key (0.0, C, true);
        h.key (8.0, C, false);
        h.run (h.at (8.5), block);
        return h.hits;
    };
    const auto a = play (512), b = play (512), d = play (77);
    int64_t worst = 0;
    check (compare (a, b, worst, 0).empty() && compare (a, d, worst, 1).empty() && a.size() == 32 * 3 * 2,
           "Random strum on repeats: identical runs, and the same at another block size");

    // A Random strum is reseeded by a transport start: two takes from the top are the same.
    Host h (48000.0, 120.0);
    h.settings.fx.strum = StrumMode::Random;
    h.settings.fx.strumRate = 4;
    h.settings.fx.repeat = { true, 5, 0.5 };
    h.key (0.0, D, true);
    h.key (4.0, D, false);
    h.run (h.at (4.5), 256);
    const auto take1 = h.hits;
    h.hits.clear();
    h.stop();
    h.hits.clear();
    h.playing = true;
    h.ppq = 0.0;
    const int64_t base = h.pos;
    h.keyAt (base, D, true);
    h.keyAt (base + h.at (4.0), D, false);
    h.run (base + h.at (4.5), 256);
    std::vector<Hit> take2;
    for (auto x : h.hits) { x.sample -= base; take2.push_back (x); }
    check (compare (take1, take2, worst, 1).empty(), "Random strum: the same strums on every take from the top");
}

int main()
{
    testClock();
    testEventQueue();
    testBuildingBlocks();
    testStrumTiming();
    testArpTiming();
    testRepeatAndSeqTiming();
    testHoldAndStack();
    testLoops();
    testTempoAndStart();
    testDeterminism();
    return testutil::finish ("test_fx");
}
