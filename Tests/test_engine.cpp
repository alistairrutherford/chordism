// Engine tests: InputRouter zones, VoiceTracker ownership, plain chord output, the test
// pattern under a fake transport, port timestamps, and stuck-note fuzzing (PLAN.md §8).
#include "TestUtil.h"
#include "core/engine/ChordismEngine.h"
#include "core/engine/PortTiming.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <vector>

using namespace chordism;
using testutil::check;

namespace
{
    // What an instrument receiving the output would hear: which notes are on, and whether the
    // stream ever doubled a note-on, sent a stray note-off, or went backwards in time.
    struct Receiver
    {
        std::set<std::pair<int, int>> on;     // (channel, note)
        int noteOns = 0, noteOffs = 0;
        int doubleOns = 0, strayOffs = 0, outOfOrder = 0, outOfBlock = 0;

        void feed (const BlockEvents& out, int numSamples)
        {
            int last = -1;
            for (const auto& e : out)
            {
                if (e.sample < last) ++outOfOrder;
                if (e.sample < 0 || e.sample >= std::max (1, numSamples)) ++outOfBlock;
                last = e.sample;

                const auto k = std::make_pair (e.channel(), e.note());
                if (e.isNoteOn())
                {
                    ++noteOns;
                    if (! on.insert (k).second) ++doubleOns;
                }
                else if (e.isNoteOff())
                {
                    ++noteOffs;
                    if (on.erase (k) == 0) ++strayOffs;
                }
                else if (e.isAllNotesOff())
                {
                    on.clear();
                }
            }
        }

        bool clean() const { return doubleOns == 0 && strayOffs == 0 && outOfOrder == 0 && outOfBlock == 0; }

        std::string describe() const
        {
            std::ostringstream o;
            o << on.size() << " sounding, " << noteOns << " on / " << noteOffs << " off, " << doubleOns
              << " double on, " << strayOffs << " stray off, " << outOfOrder << " out of order, " << outOfBlock
              << " outside the block";
            return o.str();
        }
    };

    std::vector<int> notesOn (const BlockEvents& out)
    {
        std::vector<int> v;
        for (const auto& e : out)
            if (e.isNoteOn()) v.push_back (e.note());
        std::sort (v.begin(), v.end());
        return v;
    }

    std::vector<int> notesOff (const BlockEvents& out)
    {
        std::vector<int> v;
        for (const auto& e : out)
            if (e.isNoteOff()) v.push_back (e.note());
        std::sort (v.begin(), v.end());
        return v;
    }

    // Runs one block with the given input events.
    struct Rig
    {
        ChordismEngine engine;
        EngineSettings settings;
        Transport transport;
        BlockEvents out;
        Receiver rx;

        BlockEvents& run (std::vector<MidiEvent> in, int numSamples = 512)
        {
            std::stable_sort (in.begin(), in.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.sample < b.sample; });
            out.clear();
            engine.process (settings, transport, in.data(), (int) in.size(), numSamples, out);
            rx.feed (out, numSamples);
            if (transport.playing)
                transport.ppq += numSamples / transport.samplesPerBeat();
            return out;
        }
    };
}

//==============================================================================
static void testRouter()
{
    std::cout << "== input router ==\n";
    InputRouter r;

    bool chordWhites = true, shifters = true;
    for (int oct = 0; oct < 5; ++oct)
    {
        for (int w = 0; w < 7; ++w)
            chordWhites &= r.route (oct * 12 + keys::whiteOffsets[w]) == RoutedKey { Zone::Chord, KeyRole::ChordSlot, w };
        const int blacks[5] = { 1, 3, 6, 8, 10 };
        for (int b = 0; b < 5; ++b)
            shifters &= r.route (oct * 12 + blacks[b]) == RoutedKey { Zone::Chord, KeyRole::Shifter, b };
    }
    check (chordWhites, "below the split: C..B are chord slots 0-6 in every octave");
    check (shifters, "below the split: C# D# F# G# A# are shifters 0-4");

    // The 25-key controller at its default octave: 48-59 chord zone, 60-72 Smart Scale.
    check (r.route (59) == RoutedKey { Zone::Chord, KeyRole::ChordSlot, 6 }, "59 (B) is the last chord key");
    check (r.route (60) == RoutedKey { Zone::Scale, KeyRole::ScaleTone, 0 }, "60 (split) is scale tone 0");
    check (r.route (72) == RoutedKey { Zone::Scale, KeyRole::ScaleTone, 7 }, "72 is scale tone 7 (next octave)");
    check (r.route (71) == RoutedKey { Zone::Scale, KeyRole::ScaleTone, 6 }, "71 (B) is scale tone 6");
    check (r.route (61) == RoutedKey { Zone::Scale, KeyRole::PatternSlot, 0 }
               && r.route (70) == RoutedKey { Zone::Scale, KeyRole::PatternSlot, 4 }
               && r.route (73) == RoutedKey { Zone::Scale, KeyRole::PatternSlot, 0 },
           "above the split: black keys are pattern slots 0-4, repeating each octave");

    r.split = 62;
    check (r.route (61) == RoutedKey { Zone::Chord, KeyRole::Shifter, 0 } && r.route (62) == RoutedKey { Zone::Scale, KeyRole::ScaleTone, 1 },
           "split at D: scale tones keep their letter (D = 1)");

    r.split = 60;
    r.chromatic = true;
    check (r.route (61) == RoutedKey { Zone::Scale, KeyRole::PassThrough, 61 } && r.route (48).role == KeyRole::ChordSlot,
           "Chromatic: the Smart Scale zone passes through and has no pattern keys");

    r.chromatic = false;
    check (r.chordKeyForSlot (0) == 48 && r.chordKeyForSlot (6) == 59, "test pattern keys at split 60: 48..59");
    r.split = 55;
    check (r.chordKeyForSlot (0) == 36 && r.chordKeyForSlot (6) == 47, "split 55: a whole octave below it, 36..47");
    r.split = 5;
    check (r.chordKeyForSlot (0) == -1, "no complete chord octave: no test pattern key");
}

//==============================================================================
static void testVoiceTracker()
{
    std::cout << "== voice tracker ==\n";
    VoiceTracker vt;
    BlockEvents out;

    vt.start (1, 0, 60, 100, 0, out);
    vt.start (2, 0, 60, 90, 5, out);
    check (out.size() == 1 && vt.ownerCount (0, 60) == 2, "a shared pitch sends one note-on and has two owners");

    vt.release (1, 10, out);
    check (out.size() == 1 && vt.soundingNotes() == 1, "releasing one owner doesn't cut the other's note");

    vt.release (2, 20, out);
    check (out.size() == 2 && out[1].isNoteOff() && out[1].sample == 20 && vt.isEmpty(), "the last owner sends the note-off");

    out.clear();
    vt.start (3, 0, 64, 100, 0, out);
    vt.start (3, 0, 64, 100, 0, out);
    check (out.size() == 1 && vt.ownerCount (0, 64) == 1, "a source owning a pitch twice owns it once");
    vt.release (3, 0, out);
    check (out.size() == 2 && vt.isEmpty(), "and releases it once");

    out.clear();
    vt.start (4, 0, 60, 100, 0, out);
    vt.start (4, 1, 60, 100, 0, out);
    check (out.size() == 2 && vt.soundingNotes() == 2, "the same note on two channels is two voices");
    vt.releaseAll (0, out);
    check (vt.isEmpty() && out.size() == 4, "releaseAll ends everything");

    // Full output buffer: note-ons are refused once half the buffer is used, note-offs always fit.
    EventBuffer<8> small;
    int started = 0;
    for (int n = 0; n < 8; ++n)
        started += vt.start (5, 0, n, 100, 0, small) ? 1 : 0;
    check (started == 4 && vt.notesOwnedBy (5) == 4, "note-ons stop at half the buffer");
    vt.release (5, 0, small);
    check (small.size() == 8 && vt.isEmpty(), "so every started note can still be released");

    out.clear();
    vt.start (VoiceTracker::maxSources, 0, 60, 100, 0, out);
    check (out.size() == 0, "an out-of-range source owns nothing");
}

//==============================================================================
static void testChords()
{
    std::cout << "== chord output ==\n";
    Rig rig;

    auto& o1 = rig.run ({ MidiEvent::noteOn (0, 48, 100, 10) });
    check (notesOn (o1) == std::vector<int> { 48, 52, 55 } && o1[0].sample == 10, "C key: C major triad at the key's sample");
    check (o1[0].velocity() == 100, "chord notes take the key's velocity");

    auto& o2 = rig.run ({ MidiEvent::noteOff (0, 48, 3) });
    check (notesOff (o2) == std::vector<int> { 48, 52, 55 } && rig.engine.getVoices().isEmpty(), "key up releases the chord");

    // Every white key in the default octave, C major: I ii iii IV V vi vii°.
    const std::vector<std::vector<int>> expected = { { 48, 52, 55 }, { 50, 53, 57 }, { 52, 55, 59 }, { 53, 57, 60 },
                                                     { 55, 59, 62 }, { 57, 60, 64 }, { 59, 62, 65 } };
    bool allSlots = true;
    for (int w = 0; w < 7; ++w)
    {
        allSlots &= notesOn (rig.run ({ MidiEvent::noteOn (0, 48 + keys::whiteOffsets[w], 90, 0) })) == expected[(size_t) w];
        rig.run ({ MidiEvent::noteOff (0, 48 + keys::whiteOffsets[w], 0) });
    }
    check (allSlots, "C major: the seven white keys play I ii iii IV V vi vii°");

    check (notesOn (rig.run ({ MidiEvent::noteOn (0, 36, 90, 0) })) == expected[0], "the chord-zone octave doesn't matter");
    rig.run ({ MidiEvent::noteOff (0, 36, 0) });

    // Shared tones: I (C E G) and iii (E G B).
    rig.run ({ MidiEvent::noteOn (0, 48, 90, 0) });
    auto& both = rig.run ({ MidiEvent::noteOn (0, 52, 90, 0) });
    check (notesOn (both) == std::vector<int> { 59 }, "iii while I is held: only B is new");
    auto& relI = rig.run ({ MidiEvent::noteOff (0, 48, 0) });
    check (notesOff (relI) == std::vector<int> { 48 }, "releasing I keeps E and G for iii");
    auto& reliii = rig.run ({ MidiEvent::noteOff (0, 52, 0) });
    check (notesOff (reliii) == std::vector<int> { 52, 55, 59 } && rig.engine.getVoices().isEmpty(), "releasing iii ends the rest");

    // Key change while held: the chord follows, and the key-up releases the new notes.
    rig.run ({ MidiEvent::noteOn (0, 48, 90, 0) });
    rig.settings.key = 2;
    auto& moved = rig.run ({});
    check (notesOff (moved) == std::vector<int> { 48, 52, 55 } && notesOn (moved) == std::vector<int> { 50, 54, 57 },
           "Key C→D while held: C E G off, D F# A on");
    auto& movedOff = rig.run ({ MidiEvent::noteOff (0, 48, 0) });
    check (notesOff (movedOff) == std::vector<int> { 50, 54, 57 } && rig.engine.getVoices().isEmpty(), "key up releases D F# A");
    rig.settings.key = 0;

    // Group, scale and octave.
    rig.settings.group = ChordGroup::Sevenths;
    rig.settings.scale = Scale::NaturalMinor;
    rig.settings.chordOctave = 1;
    check (notesOn (rig.run ({ MidiEvent::noteOn (0, 55, 90, 0) })) == std::vector<int> { 67, 71, 74, 77 },
           "C natural minor, Sevenths, octave +1: the G key plays G7 (raised 3rd)");
    rig.run ({ MidiEvent::noteOff (0, 55, 0) });
    rig.settings = EngineSettings {};

    // Split moves while a key is held: the key-up still releases what it started.
    rig.run ({ MidiEvent::noteOn (0, 62, 90, 0) });
    check (rig.engine.getVoices().ownerCount (0, 62) == 1, "62 above the split passes through (Phase 1 Smart Scale)");
    rig.settings.split = 64;
    rig.run ({});
    auto& splitOff = rig.run ({ MidiEvent::noteOff (0, 62, 0) });
    check (notesOff (splitOff) == std::vector<int> { 62 } && rig.engine.getVoices().isEmpty(),
           "split moved over a held key: its key-up releases the pass-through note, not a chord");
    rig.settings.split = 60;

    // Mutes.
    rig.run ({ MidiEvent::noteOn (0, 48, 90, 0), MidiEvent::noteOn (0, 64, 90, 0) });
    rig.settings.muteChords = true;
    auto& muted = rig.run ({});
    check (notesOff (muted) == std::vector<int> { 48, 52, 55 }, "Mute CG releases the held chord, keeps the Smart Scale note");
    check (notesOn (rig.run ({ MidiEvent::noteOn (0, 50, 90, 0) })).empty(), "Mute CG: chord keys play nothing");
    rig.settings.muteScale = true;
    check (notesOff (rig.run ({})) == std::vector<int> { 64 }, "Mute SS releases the Smart Scale note");
    rig.run ({ MidiEvent::noteOff (0, 48, 0), MidiEvent::noteOff (0, 64, 0), MidiEvent::noteOff (0, 50, 0) });
    rig.settings = EngineSettings {};
    check (rig.engine.getVoices().isEmpty(), "nothing left after the mutes");

    // Black keys in the chord zone are shifters (Phase 2): consumed, no output.
    check (rig.run ({ MidiEvent::noteOn (0, 49, 90, 0) }).size() == 0, "chord-zone black key plays nothing yet");
    rig.run ({ MidiEvent::noteOff (0, 49, 0) });

    // Enable off: input passes through unchanged, on its own channel.
    rig.run ({ MidiEvent::noteOn (0, 48, 90, 0) });
    rig.settings.enabled = false;
    auto& disabled = rig.run ({ MidiEvent::noteOn (3, 50, 77, 7) });
    check (notesOff (disabled) == std::vector<int> { 48, 52, 55 }, "Enable off releases the generated chord");
    check (notesOn (disabled) == std::vector<int> { 50 } && disabled[disabled.size() - 1].channel() == 3
               && disabled[disabled.size() - 1].velocity() == 77,
           "Enable off: a key passes through unchanged (note, channel, velocity)");
    rig.settings.enabled = true;
    auto& reenabled = rig.run ({});
    check (notesOff (reenabled) == std::vector<int> { 50 }, "Enable on again releases the passed-through note");
    rig.run ({ MidiEvent::noteOff (3, 50, 0), MidiEvent::noteOff (0, 48, 0) });
    check (rig.engine.getVoices().isEmpty(), "and the late key-ups send nothing stray");

    // Non-note messages pass through; All Notes Off releases everything and passes on.
    auto& cc = rig.run ({ MidiEvent::controller (0, 1, 64, 3) });
    check (cc.size() == 1 && cc[0].isController (1) && cc[0].sample == 3, "a CC passes through");
    rig.run ({ MidiEvent::noteOn (0, 48, 90, 0) });
    auto& ano = rig.run ({ MidiEvent::controller (0, 123, 0, 9) });
    check (notesOff (ano) == std::vector<int> { 48, 52, 55 } && ano[ano.size() - 1].isAllNotesOff(),
           "All Notes Off releases the chord and is passed on");

    // Retrigger: the same key twice releases then replays.
    rig.run ({ MidiEvent::noteOn (0, 48, 90, 0) });
    auto& again = rig.run ({ MidiEvent::noteOn (0, 48, 90, 0) });
    check (notesOff (again).size() == 3 && notesOn (again).size() == 3 && again[0].isNoteOff(), "a repeated key-down retriggers (offs first)");

    // requestReset (releaseResources / prepareToPlay): the next block releases everything.
    rig.engine.requestReset();
    check (notesOff (rig.run ({})).size() == 3 && rig.engine.getVoices().isEmpty(), "requestReset: next block releases all");
    rig.run ({ MidiEvent::noteOff (0, 48, 0) });

    check (rig.rx.clean() && rig.rx.on.empty(), "receiver stream clean: " + rig.rx.describe());
}

//==============================================================================
// The test pattern under a fake transport: a chord on every beat, sample-accurate, an
// eighth note long, slot = beat mod 7, across block edges, tempos and sample rates.
static void testPatternTiming (double sampleRate, double bpm, int blockSize, double startPpq = 0.0)
{
    Rig rig;
    rig.settings.testPattern = true;
    rig.transport = { sampleRate, true, startPpq, bpm, 4, 4 };

    const double spb = rig.transport.samplesPerBeat();
    const int beats = 16;
    const int64_t totalSamples = (int64_t) std::ceil (beats * spb) + blockSize;

    struct Hit { int64_t sample; int note; bool on; };
    std::vector<Hit> hits;
    int64_t pos = 0;
    while (pos < totalSamples)
    {
        auto& out = rig.run ({}, blockSize);
        for (const auto& e : out)
            hits.push_back ({ pos + e.sample, e.note(), e.isNoteOn() });
        pos += blockSize;
    }

    // Group note-ons by beat.
    std::map<int, std::vector<int>> onsByBeat;
    double worst = 0.0;
    int offTiming = 0;
    for (const auto& h : hits)
    {
        const double beat = startPpq + (double) h.sample / spb;
        const int nearest = (int) std::llround (beat * 2.0);   // half-beat grid
        worst = std::max (worst, std::abs (beat * 2.0 - nearest) * spb / 2.0);
        if (h.on)
        {
            if (nearest % 2 != 0) ++offTiming;
            onsByBeat[nearest / 2].push_back (h.note);
        }
        else if (nearest % 2 == 0)
        {
            ++offTiming;
        }
    }

    const ChordSet chords = makeGroupChords (ChordGroup::Triads, Scale::Major);
    int wrongChords = 0, beatsSeen = 0;
    const int firstBeat = (int) std::ceil (startPpq - 1e-9);
    for (auto& [beat, notes] : onsByBeat)
    {
        if (beat < firstBeat || beat >= firstBeat + beats)
            continue;
        ++beatsSeen;
        std::sort (notes.begin(), notes.end());
        const auto want = chordNotes (chords[(size_t) TestPattern::slotForBeat (beat)], {});
        std::vector<int> w (want.begin(), want.end());
        std::sort (w.begin(), w.end());
        wrongChords += notes == w ? 0 : 1;
    }

    std::ostringstream name;
    name << "test pattern at " << sampleRate / 1000.0 << " kHz, " << bpm << " BPM, block " << blockSize
         << (startPpq != 0.0 ? ", from ppq " + std::to_string (startPpq) : "") << ": ";
    check (beatsSeen == beats && wrongChords == 0,
           name.str() + std::to_string (beatsSeen) + " beats, " + std::to_string (wrongChords) + " wrong or doubled chords");
    check (worst <= 1.0 && offTiming == 0,
           name.str() + "every on/off within 1 sample of its beat / eighth (worst " + std::to_string (worst) + ")");

    // Stop: the held pattern chord ends.
    rig.transport.playing = false;
    rig.run ({}, blockSize);
    check (rig.engine.getVoices().isEmpty() && rig.rx.clean() && rig.rx.on.empty(), name.str() + "clean after stop: " + rig.rx.describe());
}

static void testPatternLoop()
{
    Rig rig;
    rig.settings.testPattern = true;
    rig.transport = { 48000.0, true, 0.0, 120.0, 4, 4 };
    const int block = 480;   // 1/50 beat at 120 BPM
    // Play into beat 3, past the key-down, then jump back to 0 before the key-up.
    while (rig.transport.ppq < 3.2)
        rig.run ({}, block);
    check (rig.engine.getVoices().soundingNotes() == 3, "pattern chord sounding at ppq 3.2");
    rig.transport.ppq = 0.0;
    auto& wrap = rig.run ({}, block);
    check (notesOff (wrap).size() == 3 && notesOn (wrap) == std::vector<int> { 48, 52, 55 },
           "loop wrap: the held chord is released and beat 0 plays again");

    rig.settings.testPattern = false;
    rig.run ({}, block);
    rig.run ({}, block);
    check (rig.engine.getVoices().isEmpty() && rig.rx.clean(), "test pattern off: released, " + rig.rx.describe());
}

//==============================================================================
static void testPortTiming()
{
    std::cout << "== port timestamps ==\n";
    // Raw port delay measured in Live 12.4.6 (compensation off), minus one buffer: the fixed
    // part, which is all a timestamp can remove (PLAN.md §3).
    const struct { double rate; int buffer; double measuredMs; } runs[] = {
        { 44100.0, 128, 4.44 }, { 44100.0, 512, 13.24 }, { 44100.0, 1024, 24.889 },
        { 48000.0, 1024, 22.990 }, { 96000.0, 1024, 12.357 } };
    bool underEvery = true, close = true;
    for (const auto& r : runs)
    {
        const double fixedMs = r.measuredMs - PortTiming::expectedLateMs (r.rate, r.buffer);
        const double leadMs = (double) PortTiming { r.rate, true, 0.0 }.leadNs() / 1.0e6;
        underEvery &= leadMs <= fixedMs;      // never stamped early enough to be clamped
        close &= fixedMs - leadMs < 0.25;     // and what's left is a fraction of a ms
        std::cout << "        " << r.rate / 1000.0 << " kHz / " << r.buffer << ": fixed part " << fixedMs
                  << " ms, lead " << leadMs << " ms, left " << fixedMs - leadMs << " ms\n";
    }
    check (underEvery, "the lead is under every measured fixed delay (Live would clamp an earlier stamp)");
    check (close, "the lead removes all but < 0.25 ms of the fixed delay");

    PortTiming t { 48000.0, true, 0.0 };
    const int64_t block = 1'000'000'000;
    check (t.stampNs (block, 48) - t.stampNs (block, 0) == 1'000'000, "48 samples at 48 kHz = 1 ms between stamps");
    check (block - t.stampNs (block, 0) == (int64_t) (portFixedLatencyMs * 1e6), "stamped earlier by the fixed latency only");
    check (std::abs (PortTiming::expectedLateMs (44100.0, 1024) - 23.2199) < 0.001, "port records one buffer late (1024 @ 44.1 kHz = 23.22 ms)");

    t.compensate = false;
    check (t.stampNs (block, 0) == block, "compensation off: stamped at the block time");
    t.trimMs = 2.0;
    check (t.stampNs (block, 0) == block + 2'000'000, "a positive trim stamps later");
}

//==============================================================================
// Stuck-note fuzzing: random keys, settings, transport moves and block sizes over thousands
// of blocks. The output must never double a note-on or send a stray note-off, and once every
// key is up and the transport stops, nothing may sound and VoiceTracker must be empty.
static void testFuzz (uint32_t seed, int blocks)
{
    std::mt19937 rng (seed);
    auto chance = [&] (double p) { return std::uniform_real_distribution<double> (0.0, 1.0) (rng) < p; };
    auto pick = [&] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); };

    Rig rig;
    rig.transport = { 44100.0, false, 0.0, 120.0, 4, 4 };
    std::set<std::pair<int, int>> keysDown;   // (channel, note) the "player" holds
    int maxSounding = 0;

    for (int b = 0; b < blocks; ++b)
    {
        const int numSamples = chance (0.1) ? pick (1, 2048) : (1 << pick (5, 11));
        std::vector<MidiEvent> in;
        const int numEvents = chance (0.5) ? pick (0, 12) : 0;
        // In sample order, so the player's idea of which keys are down matches the engine's.
        std::vector<int> samples;
        for (int i = 0; i < numEvents; ++i)
            samples.push_back (pick (0, numSamples - 1));
        std::sort (samples.begin(), samples.end());
        for (const int sample : samples)
        {
            const double r = std::uniform_real_distribution<double> (0.0, 1.0) (rng);
            if (r < 0.45)
            {
                const int ch = chance (0.9) ? 0 : pick (1, 15);
                const int note = chance (0.8) ? pick (36, 84) : pick (0, 127);
                in.push_back (MidiEvent::noteOn (ch, note, pick (1, 127), sample));
                keysDown.insert ({ ch, note });
            }
            else if (r < 0.9 && ! keysDown.empty())
            {
                auto it = keysDown.begin();
                std::advance (it, pick (0, (int) keysDown.size() - 1));
                in.push_back (chance (0.5) ? MidiEvent::noteOff (it->first, it->second, sample)
                                           : MidiEvent::noteOn (it->first, it->second, 0, sample));   // velocity-0 note-off
                keysDown.erase (it);
            }
            else if (r < 0.93)
            {
                in.push_back (MidiEvent::noteOff (0, pick (0, 127), sample));   // stray key-up
            }
            else if (r < 0.95)
            {
                in.push_back (MidiEvent::controller (0, 123, 0, sample));
            }
            else
            {
                in.push_back (MidiEvent::controller (0, pick (0, 119), pick (0, 127), sample));
            }
        }

        if (chance (0.05)) rig.settings.key = pick (0, 11);
        if (chance (0.03)) rig.settings.scale = (Scale) pick (0, numScales - 1);
        if (chance (0.03)) rig.settings.group = (ChordGroup) pick (0, numChordGroups - 1);
        if (chance (0.03)) rig.settings.split = chance (0.7) ? 60 : pick (0, 127);
        if (chance (0.03)) rig.settings.chordOctave = pick (-3, 3);
        if (chance (0.02)) rig.settings.muteChords = ! rig.settings.muteChords;
        if (chance (0.02)) rig.settings.muteScale = ! rig.settings.muteScale;
        if (chance (0.02)) rig.settings.enabled = ! rig.settings.enabled;
        if (chance (0.03)) rig.settings.testPattern = ! rig.settings.testPattern;
        if (chance (0.02)) rig.transport.playing = ! rig.transport.playing;
        if (chance (0.01)) rig.transport.ppq = std::uniform_real_distribution<double> (-1.0, 64.0) (rng);   // jump / loop wrap
        if (chance (0.01)) rig.transport.bpm = std::uniform_real_distribution<double> (40.0, 300.0) (rng);
        if (chance (0.005)) rig.transport.sampleRate = chance (0.5) ? 48000.0 : 96000.0;
        if (chance (0.003)) rig.engine.requestReset();

        rig.run (in, numSamples);
        maxSounding = std::max (maxSounding, (int) rig.rx.on.size());

        if ((int) rig.rx.on.size() != rig.engine.getVoices().soundingNotes() || ! rig.rx.clean())
        {
            check (false, "fuzz seed " + std::to_string (seed) + ", block " + std::to_string (b)
                              + ": receiver and VoiceTracker disagree: " + rig.rx.describe() + " vs tracker "
                              + std::to_string (rig.engine.getVoices().soundingNotes()));
            return;
        }
    }

    // Let go of everything and stop.
    std::vector<MidiEvent> ups;
    for (auto [ch, note] : keysDown)
        ups.push_back (MidiEvent::noteOff (ch, note, 0));
    rig.run (ups, 64);
    rig.transport.playing = false;
    rig.run ({}, 64);
    rig.settings.testPattern = false;
    rig.run ({}, 64);

    check (rig.rx.clean() && rig.rx.on.empty() && rig.engine.getVoices().isEmpty() && rig.rx.noteOns == rig.rx.noteOffs,
           "fuzz seed " + std::to_string (seed) + ", " + std::to_string (blocks) + " blocks (peak " + std::to_string (maxSounding)
               + " notes): " + rig.rx.describe());
}

int main()
{
    testRouter();
    testVoiceTracker();
    testChords();

    std::cout << "== test pattern ==\n";
    testPatternTiming (48000.0, 120.0, 256);
    testPatternTiming (48000.0, 120.0, 333);
    testPatternTiming (44100.0, 97.0, 512);
    testPatternTiming (96000.0, 174.0, 64);
    testPatternTiming (44100.0, 120.0, 2048);
    testPatternTiming (44100.0, 120.0, 32, 2.75);
    testPatternLoop();

    testPortTiming();

    std::cout << "== stuck-note fuzz ==\n";
    for (uint32_t seed = 1; seed <= 8; ++seed)
        testFuzz (seed, 5000);

    return testutil::finish ("test_engine");
}
