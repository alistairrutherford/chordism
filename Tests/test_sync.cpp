// Phase 7 tests (PLAN.md §7 row 7, §8): Instance Sync, look-ahead and voice leading.
//
//   voice leading  worked progressions (I IV V I, ii7 V7 Imaj7, a first chord, a retuned held
//                  chord, Stack), and over random chords in every key, scale and group: each led
//                  chord has the written chord's pitch classes and size, its bass in the window,
//                  is the nearest of its candidates (the inversions of VoiceLeading.h, built here
//                  again) and leads to itself; in the engine, Off is unchanged and a take from
//                  the top voices the same
//   session        the packed chord's round trip, the ring (a late reader, a joiner, its own
//                  chords skipped), Key/Scale versions, and many writer and reader threads at
//                  once (nothing torn, every sender's chords in order)
//   sync           two engines on one session under the fake host, in lockstep, with either one
//                  processing first or a random order: the follower's Smart Scale takes the
//                  leader's chords at their sample (leader first) or one block late (follower
//                  first), in every configuration; both ways round; Key/Scale shifters; groups;
//                  leaving a group; a muted chord zone; a stopped transport; a late joiner
//   look-ahead     a looped transport (three host loop styles): a Smart key pressed a 1/64 before
//                  each chord takes the upcoming chord from the second pass on (window 1/32 and
//                  1/64; not with 1/128 or Off); a follower processed first plays the leader's
//                  chord on time from the second pass; a learned change that doesn't happen
//                  clears the map; new loop points clear it; a locate doesn't; a pattern key;
//                  the Test pattern's Tones early (the LiveBridge case)
//   fuzz           two engines in random groups and processing order, with random keys, settings
//                  and transport moves: nothing stuck
#include "FakeHost.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

using namespace chordism;
using namespace fakehost;
using testutil::check;

namespace
{
    const InputRouter router;                 // split 60

    std::vector<int> notesOf (const ChordNotes& c)
    {
        std::vector<int> v (c.begin(), c.end());
        std::sort (v.begin(), v.end());
        return v;
    }

    std::string show (const std::vector<int>& v)
    {
        std::ostringstream o;
        o << "[";
        for (size_t i = 0; i < v.size(); ++i)
            o << (i ? " " : "") << v[i];
        o << "]";
        return o.str();
    }

    ChordNotes chordOf (ChordGroup g, int key, Scale s, int degree, int octave = 0)
    {
        return chordNotes (makeGroupSlot (g, s, degree), { key, s, octave });
    }

    // Σ_a min_b |a − b| + Σ_b min_a |b − a|, written out again.
    int refDistance (const std::vector<int>& a, const std::vector<int>& b)
    {
        int d = 0;
        for (int x : a)
        {
            int m = 1 << 20;
            for (int y : b) m = std::min (m, std::abs (x - y));
            d += m;
        }
        for (int y : b)
        {
            int m = 1 << 20;
            for (int x : a) m = std::min (m, std::abs (x - y));
            d += m;
        }
        return d;
    }

    // The Smart Scale tone of white-key letter j under a chord (Chord Tones, SS octave +1, keys
    // from the split's octave; no accidentals in these tests' chords).
    int toneOf (int key, Scale s, int degree, int j) { return 72 + key + scaleOffset (s, degree + 2 * j); }

    // Runs two hosts block by block until `until`; `aFirst (k)` says which processes block k first.
    void lockstep (Host& a, Host& b, int64_t until, int block, const std::function<bool (int)>& aFirst)
    {
        int k = 0;
        while (a.pos < until)
        {
            const int64_t end = std::min<int64_t> (until, a.pos + block);
            if (aFirst (k++))
            {
                a.run (end, block);
                b.run (end, block);
            }
            else
            {
                b.run (end, block);
                a.run (end, block);
            }
        }
    }

    std::vector<Hit> onsOf (const Host& h)
    {
        std::vector<Hit> v;
        for (const auto& x : h.hits)
            if (x.on)
                v.push_back (x);
        return v;
    }
}

//==============================================================================
static void testVoiceLeading()
{
    std::cout << "== voice leading ==\n";
    const ChordContext c0 { 0, Scale::Major, 0 };
    auto lead = [] (const std::vector<ChordNotes>& chords, const ChordContext& ctx)
    {
        std::vector<std::vector<int>> out;
        ChordNotes prev;
        bool have = false;
        for (const auto& c : chords)
        {
            prev = leadVoicing (c, have ? &prev : nullptr, ctx);
            have = true;
            out.push_back (notesOf (prev));
        }
        return out;
    };

    {
        const auto v = lead ({ chordOf (ChordGroup::Triads, 0, Scale::Major, 0), chordOf (ChordGroup::Triads, 0, Scale::Major, 3),
                               chordOf (ChordGroup::Triads, 0, Scale::Major, 4), chordOf (ChordGroup::Triads, 0, Scale::Major, 0) }, c0);
        check (v == std::vector<std::vector<int>> { { 48, 52, 55 }, { 48, 53, 57 }, { 47, 50, 55 }, { 48, 52, 55 } },
               "C F G C (triads) leads C-E-G, C-F-A, B-D-G, C-E-G: " + show (v[0]) + show (v[1]) + show (v[2]) + show (v[3]));
    }
    {
        // ii7 V7 Imaj7 in C.
        const auto v = lead ({ chordOf (ChordGroup::Sevenths, 0, Scale::Major, 1), chordOf (ChordGroup::Sevenths, 0, Scale::Major, 4),
                               chordOf (ChordGroup::Sevenths, 0, Scale::Major, 0) }, c0);
        check (v == std::vector<std::vector<int>> { { 50, 53, 57, 60 }, { 50, 53, 55, 59 }, { 52, 55, 59, 60 } },
               "ii7 V7 Imaj7 leads D-F-A-C, D-F-G-B, E-G-B-C: " + show (v[0]) + show (v[1]) + show (v[2]));
    }
    {
        // C → Am: the common tones stay (C E G → C E A).
        const auto v = lead ({ chordOf (ChordGroup::Triads, 0, Scale::Major, 0), chordOf (ChordGroup::Triads, 0, Scale::Major, 5) }, c0);
        check (v[1] == std::vector<int> { 48, 52, 57 }, "C → Am keeps the common tones C and E: " + show (v[1]));
    }
    {
        // The first chord: the candidate nearest the chord as written. vi in C written at A3
        // (57 60 64) has its bass outside [42, 54): it plays E-A-C.
        const auto v = lead ({ chordOf (ChordGroup::Triads, 0, Scale::Major, 5) }, c0);
        check (v[0] == std::vector<int> { 52, 57, 60 }, "a first chord takes the candidate nearest it as written (vi: E-A-C): " + show (v[0]));
    }
    {
        // Chord Octave +1 and key E: the window moves with the anchor.
        const ChordContext ctx { 4, Scale::Major, 1 };
        const auto v = lead ({ chordOf (ChordGroup::Triads, 4, Scale::Major, 0, 1), chordOf (ChordGroup::Triads, 4, Scale::Major, 4, 1) }, ctx);
        const int anchor = 48 + 4 + 12;
        check (v[0].front() >= anchor - 6 && v[0].front() < anchor + 6 && v[1].front() >= anchor - 6 && v[1].front() < anchor + 6,
               "the bass stays in the window around the Chord Octave's tonic (E, +1): " + show (v[0]) + show (v[1]));
    }

    // Random chords in every key, scale and group, chord octave -1..1.
    std::mt19937 rng (7);
    int chords = 0, bad = 0, notNearest = 0, notIdem = 0;
    std::string first;
    for (int run = 0; run < 400; ++run)
    {
        const int key = (int) (rng() % 12);
        const Scale s = (Scale) (rng() % (unsigned) numScales);
        const int octave = (int) (rng() % 3) - 1;
        const ChordContext ctx { key, s, octave };
        const int anchor = 48 + key + 12 * octave;
        ChordNotes prev;
        bool have = false;
        for (int n = 0; n < 12; ++n)
        {
            const auto g = (ChordGroup) (rng() % (unsigned) numChordGroups);
            const ChordNotes written = chordNotes (makeGroupSlot (g, s, (int) (rng() % 7)), ctx);
            const ChordNotes led = leadVoicing (written, have ? &prev : nullptr, ctx);
            const auto w = notesOf (written), l = notesOf (led);
            ++chords;

            std::set<int> pw, pl;
            for (int x : w) pw.insert (x % 12);
            for (int x : l) pl.insert (x % 12);
            const bool shapeOk = pw == pl && w.size() == l.size() && std::set<int> (l.begin(), l.end()).size() == l.size()
                              && l.front() >= anchor - 6 && l.front() < anchor + 6;
            if (! shapeOk)
            {
                if (bad++ == 0)
                    first = " e.g. " + show (w) + " → " + show (l);
            }

            // The candidates again: inversion b keeps the notes from w[b] up and lifts each lower
            // one by octaves until it's above w[b] (and on no other note), then the whole chord
            // goes by octaves until w[b] lies in the window. None is nearer the target.
            const auto target = have ? notesOf (prev) : w;
            const int dLed = refDistance (l, target);
            for (size_t b = 0; b < w.size(); ++b)
            {
                std::vector<int> c (w.begin() + (long) b, w.end());
                for (size_t k = 0; k < b; ++k)
                {
                    int p = w[k] + 12;
                    while (p <= w[b] || std::find (c.begin(), c.end(), p) != c.end())
                        p += 12;
                    c.push_back (p);
                }
                int shift = 0;
                while (w[b] + shift < anchor - 6) shift += 12;
                while (w[b] + shift >= anchor + 6) shift -= 12;
                for (int& x : c) x += shift;
                std::sort (c.begin(), c.end());
                if (c.front() >= 0 && c.back() <= 127 && refDistance (c, target) < dLed)
                    ++notNearest;
            }

            if (notesOf (leadVoicing (led, &led, ctx)) != l)
                ++notIdem;
            prev = led;
            have = true;
        }
    }
    check (bad == 0, "led chords keep the written pitch classes and size, bass in the window: "
                         + std::to_string (chords - bad) + "/" + std::to_string (chords) + first);
    check (notNearest == 0, "no candidate is nearer the previous chord: "
                                + std::to_string (notNearest) + " nearer of " + std::to_string (chords) + " chords");
    check (notIdem == 0, "a chord led from itself stays as it is: " + std::to_string (notIdem) + " moved");

    // In the engine: C F G C on the beats with Smooth; with Stack +1; Off as written.
    for (int mode = 0; mode < 3; ++mode)
    {
        sweep (mode == 0 ? "engine: Smooth plays C-E-G, C-F-A, B-D-G, C-E-G" : mode == 1 ? "engine: Smooth with Stack +1 (octaves of the led chord)"
                                                                                          : "engine: Off plays the chords as written",
               [mode] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            h.settings.fx.voiceLeading = mode == 2 ? VoiceLeadingMode::Off : VoiceLeadingMode::Smooth;
            h.settings.fx.stack = mode == 1 ? StackMode::PlusOne : StackMode::Off;
            const int slots[4] = { 0, 3, 4, 0 };
            for (int b = 0; b < 4; ++b)
            {
                h.key (b, router.chordKeyForSlot (slots[b]), true);
                h.key (b + 0.5, router.chordKeyForSlot (slots[b]), false);
            }
            h.run (h.at (4.5), c.block);
            Expect e (h.spq());
            const std::vector<std::vector<int>> led { { 48, 52, 55 }, { 48, 53, 57 }, { 47, 50, 55 }, { 48, 52, 55 } };
            for (int b = 0; b < 4; ++b)
            {
                std::vector<int> n = mode == 2 ? notesOf (chordOf (ChordGroup::Triads, 0, Scale::Major, slots[b])) : led[(size_t) b];
                if (mode == 1)
                {
                    const auto base = n;
                    for (int x : base) n.push_back (x + 12);
                    std::sort (n.begin(), n.end());
                    n.erase (std::unique (n.begin(), n.end()), n.end());
                }
                for (int x : n)
                    e.note (b, b + 0.5, x);
            }
            return finish (h, e, worst);
        });
    }

    // A held chord retuned by a Key/Scale shifter is led from itself: C held, shift to G major
    // (shifter 1 = Key G, so the window moves to G's tonic, [49, 61)): G-B-D nearest C-E-G is D-G-B.
    {
        Host h (48000.0, 120.0);
        h.settings.fx.voiceLeading = VoiceLeadingMode::Smooth;
        h.settings.shifters[0] = { ShifterKind::KeyScale, ChordGroup::Triads, 7, -1 };
        h.key (0.0, router.chordKeyForSlot (0), true);
        h.key (1.0, router.shifterKeyForSlot (0), true);
        h.run (h.at (1.5), 256);
        const auto now = h.engine->getPerformer().notesOf (router.chordKeyForSlot (0));
        std::vector<int> v (now.begin(), now.end());
        check (v == std::vector<int> { 50, 55, 59 }, "a shifter retunes a held chord to the voicing nearest it (C → G: D-G-B): " + show (v));
    }

    // A take from the top voices the same as the first: the transport start forgets the last chord.
    {
        auto take = [] (Host& h)
        {
            h.hits.clear();
            const int64_t t0 = h.pos;
            for (int b = 0; b < 3; ++b)
            {
                h.keyAt (t0 + h.at (b), router.chordKeyForSlot (b == 1 ? 4 : 5), true);
                h.keyAt (t0 + h.at (b + 0.5), router.chordKeyForSlot (b == 1 ? 4 : 5), false);
            }
            h.run (t0 + h.at (3.0), 512);
            std::vector<int> n;
            for (const auto& x : onsOf (h)) n.push_back (x.note);
            return n;
        };
        Host h (44100.0, 120.0);
        h.settings.fx.voiceLeading = VoiceLeadingMode::Smooth;
        const auto a = take (h);
        h.stop();
        h.playing = true;
        h.ppq = 3.0;
        const auto b = take (h);
        check (! a.empty() && a == b, "a second take from a transport start voices exactly as the first: " + show (a) + " / " + show (b));
    }
}

//==============================================================================
static void testSession()
{
    std::cout << "== shared session ==\n";
    {
        std::mt19937 rng (3);
        int ok = 0;
        for (int i = 0; i < 2000; ++i)
        {
            SharedSession s;
            SyncChord c;
            c.chord.key = (int) (rng() % 12);
            c.chord.scale = (Scale) (rng() % (unsigned) numScales);
            c.chord.slot.degree = (int) (rng() % 7);
            for (auto& st : c.chord.slot.steps)
            {
                st.on = (rng() & 1) != 0;
                st.accidental = (int8_t) ((int) (rng() % 5) - 2);
                st.octaveMask = (uint8_t) (rng() & 0x1f);
            }
            c.ppq = std::uniform_real_distribution<double> (-10.0, 10000.0) (rng);
            c.playing = (rng() & 1) != 0;
            c.sender = rng();
            s.publish (2, c);
            SyncChord back;
            ok += s.read (2, 0, back) == SharedSession::Read::Ready && back.chord == c.chord && back.ppq == c.ppq
                && back.playing == c.playing && back.sender == c.sender ? 1 : 0;
        }
        check (ok == 2000, "a published chord reads back exactly (key, scale, degree, steps, ppq, sender): " + std::to_string (ok) + "/2000");
    }
    {
        SharedSession s;
        const uint32_t me = s.join(), other = s.join();
        SyncReader reader;
        std::vector<double> got;
        reader.poll (s, 1, me, [&] (const SyncChord& c) { got.push_back (c.ppq); });
        for (int i = 0; i < 200; ++i)
            s.publish (1, { {}, (double) i, true, i % 2 == 0 ? other : me });
        reader.poll (s, 1, me, [&] (const SyncChord& c) { got.push_back (c.ppq); });
        const bool lastOnly = ! got.empty() && got.back() == 198.0 && got.size() <= SharedSession::ringSize / 2 + 1;
        bool ownSkipped = true;
        for (double p : got) ownSkipped &= ((int) p) % 2 == 0;
        check (lastOnly && ownSkipped, "a reader a ring behind gets the latest chords, never its own (" + std::to_string (got.size()) + " read)");

        SyncReader joiner;
        std::vector<double> j;
        joiner.poll (s, 1, s.join(), [&] (const SyncChord& c) { j.push_back (c.ppq); });
        joiner.poll (s, 1, 99, [&] (const SyncChord& c) { j.push_back (c.ppq); });
        check (j.size() == 1 && j[0] == 199.0, "a member joining a group gets its latest chord first");
        std::vector<double> none;
        joiner.poll (s, 2, 99, [&] (const SyncChord& c) { none.push_back (c.ppq); });
        check (none.empty(), "another group has none of them");

        check (! s.keyScale (3).valid, "Key/Scale: none before anyone publishes");
        const uint32_t v1 = s.publishKeyScale (3, 2, (int) Scale::Dorian);
        const auto k = s.keyScale (3);
        const uint32_t v2 = s.publishKeyScale (3, 9, (int) Scale::NaturalMinor);
        check (k.valid && k.key == 2 && k.scale == (int) Scale::Dorian && k.version == v1 && v2 == v1 + 1 && s.keyScale (3).key == 9,
               "Key/Scale: the last published wins, with a new version each time");
    }

    // Threads: 4 writers × 40,000 chords into one group, 3 readers polling all the while. Each
    // chord's steps and degree are a function of its (sender, count), so a torn read shows.
    {
        SharedSession s;
        std::atomic<bool> done { false };
        std::atomic<int> torn { 0 }, disorder { 0 }, readCount { 0 };
        auto chordFor = [] (uint32_t sender, int n)
        {
            SyncChord c;
            c.sender = sender;
            c.ppq = (double) n;
            c.chord.key = (int) ((sender * 7u + (uint32_t) n) % 12u);
            c.chord.scale = (Scale) ((uint32_t) n % (uint32_t) numScales);
            c.chord.slot.degree = n % 7;
            for (int k = 0; k < numChordSteps; ++k)
            {
                auto& st = c.chord.slot.steps[(size_t) k];
                st.on = ((n >> k) & 1) != 0;
                st.accidental = (int8_t) (((n + k) % 5) - 2);
                st.octaveMask = (uint8_t) ((n * 3 + k) & 0x1f);
            }
            c.playing = (n & 1) != 0;
            return c;
        };
        std::vector<std::thread> threads;
        for (int r = 0; r < 3; ++r)
            threads.emplace_back ([&, r]
            {
                SyncReader reader;
                std::array<int, 8> last;
                last.fill (-1);
                const uint32_t self = 1000u + (uint32_t) r;
                auto f = [&] (const SyncChord& c)
                {
                    readCount.fetch_add (1, std::memory_order_relaxed);
                    const SyncChord want = chordFor (c.sender, (int) c.ppq);
                    if (! (want.chord == c.chord) || want.playing != c.playing)
                        torn.fetch_add (1);
                    auto& l = last[(size_t) (c.sender % 8)];
                    if ((int) c.ppq <= l)
                        disorder.fetch_add (1);
                    l = (int) c.ppq;
                };
                while (! done.load())
                    reader.poll (s, 1, self, f);
                reader.poll (s, 1, self, f);
            });
        std::vector<std::thread> writers;
        for (uint32_t w = 1; w <= 4; ++w)
            writers.emplace_back ([&, w]
            {
                for (int n = 0; n < 40000; ++n)
                    s.publish (1, chordFor (w, n));
            });
        for (auto& t : writers)
            t.join();
        done = true;
        for (auto& t : threads)
            t.join();
        check (torn == 0 && disorder == 0 && readCount > 0,
               "4 writers × 40,000 chords, 3 readers at once: " + std::to_string (readCount.load()) + " read, "
                   + std::to_string (torn.load()) + " torn, " + std::to_string (disorder.load()) + " out of order");
    }
}

//==============================================================================
namespace
{
    // The two-instance rig of these tests: A plays chords in D Dorian (chord slot b on beat b,
    // an eighth long), B's chord zone is muted and it plays Smart Scale keys.
    struct Pair
    {
        SharedSession session;
        Host a, b;

        Pair (double rate, double bpm) : a (rate, bpm), b (rate, bpm)
        {
            a.engine->setSession (&session);
            b.engine->setSession (&session);
            a.settings.key = 2;
            a.settings.scale = Scale::Dorian;
            a.settings.syncGroup = 1;
            a.settings.smart.map.octave = 1;
            b.settings.key = 2;
            b.settings.scale = Scale::Dorian;
            b.settings.syncGroup = 1;
            b.settings.muteChords = true;
            b.settings.smart.map.octave = 1;
        }

        void chords (int beats, double start = 0.0)
        {
            for (int k = 0; k < beats; ++k)
            {
                a.key (start + k, router.chordKeyForSlot (k % 7), true);
                a.key (start + k + 0.5, router.chordKeyForSlot (k % 7), false);
            }
        }

        void loop (double end, LoopStyle style)
        {
            for (Host* h : { &a, &b })
            {
                h->looping = true;
                h->loopStart = 0.0;
                h->loopEnd = end;
                h->style = style;
            }
        }
    };

    std::function<bool (int)> orderOf (int order, uint32_t seed)
    {
        if (order == 0) return [] (int) { return true; };
        if (order == 1) return [] (int) { return false; };
        auto rng = std::make_shared<std::mt19937> (seed);
        return [rng] (int) { return ((*rng)() & 1) != 0; };
    }

    const char* orderName (int order) { return order == 0 ? "leader first" : order == 1 ? "follower first" : "random order"; }
}

static void testSync()
{
    std::cout << "== instance sync ==\n";

    // B presses a Smart key a sixteenth after each of A's chords: in every order it plays A's chord.
    for (int order = 0; order < 3; ++order)
        sweep (std::string ("follower's Smart Scale plays the leader's chords (") + orderName (order) + ")", [order] (const Config& c, int64_t& worst)
        {
            Pair p (c.rate, c.bpm);
            p.b.settings.key = 0;                      // B's own key doesn't matter: the chord carries A's
            p.b.settings.scale = Scale::Major;
            p.chords (8);
            for (int k = 0; k < 8; ++k)
            {
                const int key = router.scaleKeyForLetter ((3 * k) % 7);
                p.b.key (k + 0.25, key, true);
                p.b.key (k + 0.5, key, false);
            }
            lockstep (p.a, p.b, p.a.at (8.5), c.block, orderOf (order, (uint32_t) c.block));
            Expect e (p.b.spq());
            for (int k = 0; k < 8; ++k)
                e.note (k + 0.25, k + 0.5, toneOf (2, Scale::Dorian, k % 7, (3 * k) % 7));
            return finish (p.b, e, worst);
        });

    // B presses on the beat, with Note repeat 1/32: leader first, every strike is A's chord;
    // follower first, the strike on the beat still has the last chord (one block late) and the
    // next strike, more than a block later, the new one.
    for (int order = 0; order < 2; ++order)
        sweep (std::string ("within one block: Note repeat on the beat (") + orderName (order) + ")", [order] (const Config& c, int64_t& worst)
        {
            Pair p (c.rate, c.bpm);
            p.b.settings.smart.noteRepeat = true;
            p.b.settings.smart.patterns[0].rate = 7;   // 1/32
            p.b.settings.smart.patterns[0].gate = 0.5;
            p.b.settings.lookAhead = 0;
            p.chords (6);
            for (int k = 0; k < 6; ++k)
            {
                const int key = router.scaleKeyForLetter ((3 * k) % 7);
                p.b.key (k, key, true);
                p.b.key (k + 0.5, key, false);
            }
            lockstep (p.a, p.b, p.a.at (6.5), c.block, orderOf (order, 1));
            Expect e (p.b.spq());
            for (int k = 0; k < 6; ++k)
            {
                const int j = (3 * k) % 7;
                for (int s = 0; s < 4; ++s)
                {
                    // Before A's first chord B's own chord is current: D Dorian's tonic, no steps.
                    const int d = s == 0 && order == 1 ? (k == 0 ? 0 : (k - 1) % 7) : k % 7;
                    e.note (k + 0.125 * s, k + 0.125 * s + 0.0625, toneOf (2, Scale::Dorian, d, j));
                }
            }
            return finish (p.b, e, worst);
        });

    // Both ways round: B plays a chord, A's Smart Scale follows it; then A's chord again.
    {
        Pair p (48000.0, 120.0);
        p.b.settings.muteChords = false;
        p.a.key (0.0, router.chordKeyForSlot (1), true);         // A: ii (E minor in D Dorian)
        p.a.key (0.5, router.chordKeyForSlot (1), false);
        p.b.key (1.0, router.chordKeyForSlot (4), true);         // B: V (A minor)
        p.b.key (1.5, router.chordKeyForSlot (4), false);
        const int c = router.scaleKeyForLetter (0);
        p.a.key (1.25, c, true);
        p.a.key (1.5, c, false);
        p.b.key (0.25, c, true);
        p.b.key (0.5, c, false);
        lockstep (p.a, p.b, p.a.at (2.0), 256, orderOf (2, 5));
        auto smartOn = [] (const Host& h)
        {
            std::vector<int> v;
            for (const auto& x : onsOf (h))
                if (x.note >= 72)
                    v.push_back (x.note);
            return v;
        };
        check (smartOn (p.b) == std::vector<int> { toneOf (2, Scale::Dorian, 1, 0) } && smartOn (p.a) == std::vector<int> { toneOf (2, Scale::Dorian, 4, 0) },
               "they follow each other: B's Smart Scale takes A's chord, A's takes B's");
    }

    // A Key/Scale shifter on A: the chord B follows carries the shifted key (shifter 1 = Key F,
    // Major: ii is G minor).
    {
        Pair p (44100.0, 120.0);
        p.a.settings.shifters[0] = { ShifterKind::KeyScale, ChordGroup::Triads, 5, (int) Scale::Major };
        p.a.key (0.0, router.shifterKeyForSlot (0), true);
        p.a.key (0.1, router.chordKeyForSlot (1), true);
        const int key = router.scaleKeyForLetter (1);
        p.b.key (0.3, key, true);
        p.b.key (0.4, key, false);
        lockstep (p.a, p.b, p.a.at (0.5), 512, orderOf (1, 0));
        const auto on = onsOf (p.b);
        check (on.size() == 1 && on[0].note == toneOf (5, Scale::Major, 1, 1), "a Key/Scale shifter's key reaches the follower");
    }

    // Groups: B in another group, or Off, follows nothing; B leaving the group goes back to its own chord.
    for (int g : { 0, 2 })
    {
        Pair p (44100.0, 120.0);
        p.b.settings.syncGroup = g;
        p.chords (2);
        const int key = router.scaleKeyForLetter (1);
        p.b.key (1.25, key, true);
        p.b.key (1.5, key, false);
        lockstep (p.a, p.b, p.a.at (2.0), 256, orderOf (0, 0));
        const auto on = onsOf (p.b);
        check (on.size() == 1 && on[0].note == toneOf (2, Scale::Dorian, 0, 1),
               g == 0 ? "Sync Off: the Smart Scale follows only its own chords" : "another group (B): A's chords don't reach it");
    }
    {
        Pair p (44100.0, 120.0);
        p.b.settings.muteChords = false;
        p.b.key (0.0, router.chordKeyForSlot (3), true);   // B's own chord: IV
        p.b.key (0.2, router.chordKeyForSlot (3), false);
        p.a.key (0.5, router.chordKeyForSlot (5), true);   // A: vi
        p.a.key (0.7, router.chordKeyForSlot (5), false);
        lockstep (p.a, p.b, p.a.at (1.0), 256, orderOf (0, 0));
        const bool followed = p.b.engine->isSmartChordRemote() && p.b.engine->getSmartChord().slot.degree == 5;
        p.b.settings.syncGroup = 0;
        lockstep (p.a, p.b, p.a.at (1.5), 256, orderOf (0, 0));
        check (followed && ! p.b.engine->isSmartChordRemote() && p.b.engine->getSmartChord().slot.degree == 3,
               "leaving the group, the Smart Scale goes back to its own last chord");
    }

    // A muted chord zone publishes nothing: B's presses don't move A's Smart Scale.
    {
        Pair p (44100.0, 120.0);
        p.a.key (0.0, router.chordKeyForSlot (2), true);
        p.b.key (0.5, router.chordKeyForSlot (6), true);    // B is muted
        lockstep (p.a, p.b, p.a.at (1.0), 256, orderOf (2, 9));
        check (! p.a.engine->isSmartChordRemote() && p.a.engine->getSmartChord().slot.degree == 2 && p.session.head (1) == 1,
               "a muted chord zone's keys publish nothing (one chord in the group: A's)");
    }

    // Stopped transports: chords apply at the start of the next block.
    {
        Pair p (44100.0, 120.0);
        p.a.playing = p.b.playing = false;
        p.a.key (0.0, router.chordKeyForSlot (4), true);
        lockstep (p.a, p.b, 1024, 512, orderOf (1, 0));
        check (p.b.engine->getSmartChord().slot.degree == 4 && p.b.engine->getSmartChord().key == 2, "stopped: the follower takes the chord in the next block");
    }

    // A late joiner gets the group's current chord.
    {
        Pair p (44100.0, 120.0);
        p.b.settings.syncGroup = 0;
        p.a.key (0.0, router.chordKeyForSlot (3), true);
        lockstep (p.a, p.b, 4096, 512, orderOf (0, 0));
        p.b.settings.syncGroup = 1;
        lockstep (p.a, p.b, 8192, 512, orderOf (0, 0));
        check (p.b.engine->isSmartChordRemote() && p.b.engine->getSmartChord().slot.degree == 3, "joining a group takes its latest chord");
    }
}

//==============================================================================
static void testLookAhead()
{
    std::cout << "== look-ahead ==\n";
    const LoopStyle styles[] = { LoopStyle::Wrap, LoopStyle::Split, LoopStyle::SplitDropOvershoot };
    const char* styleNames[] = { "wrap", "split", "split, no overshoot" };

    // One bar looped, C major: chord slot b on beat b; a Smart key (letter 3b mod 7) pressed a
    // 1/64 note before each beat, held to b + 1/2. Pass 0 has nothing learned: the early tone
    // has the chord before; from pass 1 on it has the upcoming one.
    struct Window { int index; bool reaches; const char* name; };
    // (A host that drops the wrap's overshoot loses up to a sample of song time per wrap, so the
    // lead drifts past a window exactly its size: that case runs on the other two.)
    for (const Window w : { Window { 3, true, "1/32" }, Window { 2, true, "1/64 (exactly the lead)" }, Window { 1, false, "1/128" }, Window { 0, false, "Off" } })
        for (int st = 0; st < 3; ++st)
        {
            const LoopStyle style = styles[st];
            if (w.index == 2 && style == LoopStyle::SplitDropOvershoot)
                continue;
            sweep (std::string ("early Smart keys on a 1-bar loop, window ") + w.name + ", " + styleNames[st], [w, style] (const Config& c, int64_t& worst)
            {
                Host h (c.rate, c.bpm);
                h.looping = true;
                h.loopStart = 0.0;
                h.loopEnd = 4.0;
                h.style = style;
                h.settings.lookAhead = w.index;
                h.settings.smart.map.octave = 1;
                const int passes = 4;
                for (int t = 0; t < 4 * passes; ++t)
                {
                    const int b = t % 4;
                    h.key (t, router.chordKeyForSlot (b), true);
                    h.key (t + 0.5, router.chordKeyForSlot (b), false);
                    if (t > 0)
                    {
                        const int key = router.scaleKeyForLetter ((3 * b) % 7);
                        h.key (t - 0.0625, key, true);
                        h.key (t + 0.5, key, false);
                    }
                }
                h.run (h.at (4.0 * passes - 0.25), c.block);
                Expect e (h.spq());
                for (int t = 0; t < 4 * passes; ++t)
                {
                    const int b = t % 4;
                    for (int k = 0; k < 3; ++k)
                        e.note (t, t + 0.5, 48 + scaleOffset (Scale::Major, b + 2 * k));
                    if (t > 0)
                    {
                        const bool ahead = w.reaches && t >= 5;   // pass 1 on (t = 4 is pressed in pass 0)
                        e.note (t - 0.0625, t + 0.5, toneOf (0, Scale::Major, ahead ? b : (b + 3) % 4, (3 * b) % 7));
                    }
                }
                return finish (h, e, worst, style == LoopStyle::SplitDropOvershoot ? 4 : 2);
            });
        }

    // The follower processed first, with look-ahead on a loop: the strike on the beat is one block
    // late in pass 0, on time from pass 1 (the learned map has the leader's stamped chords).
    sweep ("follower first on a 1-bar loop: on time from the second pass", [] (const Config& c, int64_t& worst)
    {
        Pair p (c.rate, c.bpm);
        p.loop (4.0, LoopStyle::Wrap);
        const int passes = 3;
        for (int t = 0; t < 4 * passes; ++t)
        {
            p.a.key (t, router.chordKeyForSlot (t % 4), true);
            p.a.key (t + 0.5, router.chordKeyForSlot (t % 4), false);
            const int key = router.scaleKeyForLetter ((3 * (t % 4)) % 7);
            p.b.key (t, key, true);
            p.b.key (t + 0.25, key, false);
        }
        lockstep (p.a, p.b, p.a.at (4.0 * passes - 0.25), c.block, orderOf (1, 0));
        // The key on beat 4 is in pass 1 unless its sample rounds to just before the wrap.
        const bool fourInPass0 = p.b.at (4.0) < (int64_t) std::ceil (4.0 * p.b.spq() - 1.0e-6);
        Expect e (p.b.spq());
        for (int t = 0; t < 4 * passes; ++t)
        {
            const int b = t % 4;
            const bool onTime = t > 4 || (t == 4 && ! fourInPass0);
            const int d = onTime ? b : (t == 0 ? 0 : (b + 3) % 4);
            e.note (t, t + 0.25, toneOf (2, Scale::Dorian, d, (3 * b) % 7));
        }
        return finish (p.b, e, worst, 2);
    });

    // A learned change that doesn't happen clears the map: in pass 2 beat 2's chord isn't
    // played, so the early key before beat 3 has the chord that is current (beat 1's).
    {
        Host h (44100.0, 120.0);
        h.looping = true;
        h.loopEnd = 4.0;
        h.settings.smart.map.octave = 1;
        for (int t = 0; t < 12; ++t)
        {
            const int b = t % 4;
            if (t != 10)
            {
                h.key (t, router.chordKeyForSlot (b), true);
                h.key (t + 0.5, router.chordKeyForSlot (b), false);
            }
            if (t > 0)
            {
                const int key = router.scaleKeyForLetter ((3 * b) % 7);
                h.key (t - 0.0625, key, true);
                h.key (t + 0.5, key, false);
            }
        }
        h.run (h.at (12.25), 512);
        auto toneAt = [&h] (double beat)
        {
            for (const auto& x : h.hits)
                if (x.on && x.note >= 72 && std::llabs (x.sample - h.at (beat)) <= 2)
                    return x.note;
            return -1;
        };
        const int beforeMiss = toneAt (9.9375), afterMiss = toneAt (10.9375);
        check (beforeMiss == toneOf (0, Scale::Major, 2, 6) && afterMiss == toneOf (0, Scale::Major, 1, 2),
               "a learned change that doesn't happen clears the map (the next early key has the current chord)");
        check (h.engine->getTimeline().learnedChanges() == 3, "…and the pass without it becomes the map at the wrap (3 changes)");
    }

    // New loop points clear the map; a locate inside the loop keeps it.
    {
        Host h (44100.0, 120.0);
        h.looping = true;
        h.loopEnd = 4.0;
        for (int t = 0; t < 8; ++t)
        {
            h.key (t, router.chordKeyForSlot (t % 4), true);
            h.key (t + 0.5, router.chordKeyForSlot (t % 4), false);
        }
        h.run (h.at (6.2), 512);
        const int learned = h.engine->getTimeline().learnedChanges();
        h.ppq = 1.5;                      // a locate
        h.run (h.pos + 512, 512);
        const int afterLocate = h.engine->getTimeline().learnedChanges();
        h.loopEnd = 8.0;
        h.run (h.pos + 512, 512);
        check (learned == 4 && afterLocate == 4 && h.engine->getTimeline().learnedChanges() == 0,
               "a locate keeps the map (4 changes), new loop points clear it");
    }

    // A pattern key pressed early starts over the upcoming chord.
    {
        Host h (48000.0, 120.0);
        h.looping = true;
        h.loopEnd = 2.0;
        h.settings.smart.map.octave = 1;
        for (int t = 0; t < 4; ++t)
        {
            h.key (t, router.chordKeyForSlot (t % 2 == 0 ? 0 : 4), true);
            h.key (t + 0.5, router.chordKeyForSlot (t % 2 == 0 ? 0 : 4), false);
        }
        const int pk = router.patternKeyForSlot (0);   // Linear Up 1/16 Triads
        h.key (2.9375, pk, true);
        h.key (3.2, pk, false);
        h.run (h.at (3.5), 256);
        std::vector<int> v;
        for (const auto& x : h.hits)
            if (x.on && x.note >= 72)
                v.push_back (x.note);
        check (! v.empty() && v[0] == toneOf (0, Scale::Major, 4, 0), "a pattern key pressed early starts on the upcoming chord's root");
    }

    // The Test pattern's Tones early (the LiveBridge case), C major, SS octave +1, one bar looped.
    for (int st = 0; st < 3; ++st)
    {
        const LoopStyle style = styles[st];
        sweep (std::string ("Test smart Tones early on a 1-bar loop, ") + styleNames[st], [style] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            h.looping = true;
            h.loopEnd = 4.0;
            h.style = style;
            h.settings.testPattern = true;
            h.settings.testSmart = TestPattern::smartTonesEarly;
            h.settings.smart.map.octave = 1;
            h.run (h.at (16.0) - 1, c.block);
            h.settings.testPattern = false;
            h.run (h.pos + c.block, c.block);

            Expect e (h.spq());
            for (int t = 0; t < 16; ++t)
            {
                const int b = t % 4;
                for (int k = 0; k < 3; ++k)
                    e.note (t, t + 0.5, 48 + scaleOffset (Scale::Major, b + 2 * k));
                if (t > 0)
                    e.note (t - 0.0625, t + 0.5, toneOf (0, Scale::Major, t >= 5 ? b : (b + 3) % 4, (3 * b) % 7));
            }
            // The last early key (for the pass after) is cut by the stop.
            const int last = toneOf (0, Scale::Major, 0, 0);
            e.on (16.0 - 0.0625, last);
            e.offAt (h.at (16.0) - 1 + 0, last);
            std::string why = compare (h.hits, e.hits, worst, style == LoopStyle::SplitDropOvershoot ? 4 : 2);
            if (! h.clean())
                why += " not clean: " + h.rx.describe();
            return why;
        });
    }
}

//==============================================================================
// Two engines in random groups and order, random keys and settings: nothing stuck.
static void testSyncFuzz (uint32_t seed, int blocks)
{
    std::mt19937 rng (seed);
    auto chance = [&] (double p) { return std::uniform_real_distribution<double> (0.0, 1.0) (rng) < p; };
    auto pick = [&] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); };

    SharedSession session;
    Host hosts[2] { Host (44100.0, 120.0), Host (44100.0, 120.0) };
    std::set<int> down[2];
    for (auto& h : hosts)
        h.engine->setSession (&session);
    int remote = 0;
    for (int b = 0; b < blocks; ++b)
    {
        const int n = 1 << pick (5, 11);
        for (int k = 0; k < 2; ++k)
        {
            Host& h = hosts[k];
            for (int e = pick (0, 3); e > 0; --e)
            {
                const int note = pick (40, 90);
                const int64_t at = h.pos + pick (0, n - 1);
                if (down[k].count (note))
                {
                    h.keyAt (at, note, false);
                    down[k].erase (note);
                }
                else
                {
                    h.keyAt (at, note, true);
                    down[k].insert (note);
                }
            }
            auto& s = h.settings;
            if (chance (0.01)) s.syncGroup = pick (0, 2);
            if (chance (0.02)) s.lookAhead = pick (0, numLookAheads - 1);
            if (chance (0.02)) s.fx.voiceLeading = chance (0.5) ? VoiceLeadingMode::Smooth : VoiceLeadingMode::Off;
            if (chance (0.02)) s.key = pick (0, 11);
            if (chance (0.01)) s.muteChords = ! s.muteChords;
            if (chance (0.02)) s.smart.noteRepeat = ! s.smart.noteRepeat;
            if (chance (0.02)) s.smart.hold = (HoldMode) pick (0, numHoldModes - 1);
            if (chance (0.02)) s.fx.arp = chance (0.5) ? ArpPattern::Off : ArpPattern::LinearUp;
        }
        if (chance (0.01))
            for (auto& h : hosts)
            {
                h.looping = chance (0.7);
                h.loopStart = (double) pick (0, 4);
                h.loopEnd = h.loopStart + 0.5 * pick (1, 8);
                h.ppq = h.loopStart;
            }
        if (chance (0.005))
            for (auto& h : hosts)
                h.playing = ! h.playing;
        const bool firstA = chance (0.5);
        Host& x = hosts[firstA ? 0 : 1];
        Host& y = hosts[firstA ? 1 : 0];
        x.run (x.pos + n, n);
        y.run (y.pos + n, n);
        remote += hosts[0].engine->isSmartChordRemote() || hosts[1].engine->isSmartChordRemote() ? 1 : 0;
    }
    bool clean = true;
    for (int k = 0; k < 2; ++k)
    {
        Host& h = hosts[k];
        for (int note : down[k])
            h.keyAt (h.pos, note, false);
        h.settings.smart.hold = HoldMode::Off;
        h.settings.fx.hold = HoldMode::Off;
        h.run (h.pos + 64, 64);
        h.stop();
        clean &= h.clean();
    }
    check (clean, "sync fuzz seed " + std::to_string (seed) + ", " + std::to_string (blocks) + " blocks (" + std::to_string (remote)
                      + " following the other): nothing stuck, " + hosts[0].rx.describe() + " / " + hosts[1].rx.describe());
}

int main()
{
    testVoiceLeading();
    testSession();
    testSync();
    testLookAhead();
    std::cout << "== sync fuzz ==\n";
    for (uint32_t seed = 1; seed <= 6; ++seed)
        testSyncFuzz (seed, 4000);
    return testutil::finish ("test_sync");
}
