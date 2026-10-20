// Phase 6: MIDI import and export (PLAN.md §4.6, R6).
//
//   file        writeMidiFile → readMidiFile keeps every note, the tempo and the time signature;
//               hand-made files (type 0, running status, note-on velocity 0, other messages)
//               read right; files Chordism can't read are refused
//   recorder    Export keeps only completed bars, the last 32, from the take's first bar line;
//               notes still sounding are cut at the last bar line; loops run on like a tape; a
//               new take replaces the old; the file's notes are the engine's output, to the tick
//   round trip  (R6) a take of the test pattern is exported, written, read and imported, and
//               the imported chord zone plays every chord of the take again, note for note
//   import      hand-made chord files: the key fit, inversions, slash chords, 13ths,
//               non-diatonic chords, two chords on one root, register, more than seven chords;
//               and 2,000 random chords: every note is either played back or counted as dropped
#include "FakeHost.h"
#include "core/midiio/ChordImporter.h"
#include "core/midiio/ExportRecorder.h"
#include "core/midiio/MidiFile.h"
#include <map>
#include <random>

using namespace chordism;
using namespace fakehost;
using testutil::check;

namespace
{
    constexpr int whiteOffset[7] = { 0, 2, 4, 5, 7, 9, 11 };

    std::string text (const std::vector<int>& v)
    {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); ++i)
            s += (i > 0 ? " " : "") + std::to_string (v[i]);
        return s + "]";
    }

    std::string text (const std::vector<std::vector<int>>& v)
    {
        std::string s;
        for (const auto& c : v)
            s += text (c);
        return s;
    }

    bool sameNotes (const MidiNote& a, const MidiNote& b, int64_t tolerance = 0)
    {
        return a.pitch == b.pitch && a.velocity == b.velocity && a.channel == b.channel
            && std::llabs (a.start - b.start) <= tolerance && std::llabs (a.end - b.end) <= tolerance;
    }

    //==========================================================================
    // Export through the fake host.

    struct Take
    {
        std::unique_ptr<ExportRecorder> recorder = std::make_unique<ExportRecorder>();

        void attach (Host& h)
        {
            h.observe = [this] (const Transport& t, int n, const BlockEvents& out) { recorder->record (t, n, out.begin(), out.size()); };
        }

        MidiSong song (int maxBars = ExportRecorder::maxBars) const
        {
            TakeInfo info;
            std::vector<RecordedEvent> events;
            recorder->snapshot (info, events);
            return exportBars (info, events, maxBars);
        }
    };

    // The host's notes (paired in order per pitch) in beats [from, to), as file notes from `from`
    // (ticks at 960), cut at `to`. Constant tempo; ppq `startPpq` at sample 0.
    std::vector<MidiNote> hostNotes (const Host& h, double from, double to, double startPpq = 0.0)
    {
        std::map<int, std::vector<std::pair<double, int>>> open;
        std::vector<MidiNote> notes;
        const auto tick = [&] (double beat) { return (int64_t) std::llround ((beat - from) * 960.0); };
        for (const auto& hit : h.hits)
        {
            const double beat = startPpq + (double) hit.sample / h.spq();
            if (hit.on)
            {
                open[hit.note].push_back ({ beat, hit.velocity });
                continue;
            }
            auto& o = open[hit.note];
            if (o.empty())
                continue;
            const auto [start, velocity] = o.front();
            o.erase (o.begin());
            if (tick (start) < 0 || tick (start) >= tick (to))
                continue;
            notes.push_back ({ tick (start), std::min (tick (beat), tick (to)), (uint8_t) hit.note, (uint8_t) velocity, 0 });
        }
        for (auto& [note, o] : open)
            for (const auto& [start, velocity] : o)
                if (tick (start) >= 0 && tick (start) < tick (to))
                    notes.push_back ({ tick (start), tick (to), (uint8_t) note, (uint8_t) velocity, 0 });
        std::stable_sort (notes.begin(), notes.end(), [] (const MidiNote& a, const MidiNote& b)
        {
            return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
        });
        return notes;
    }

    // "" if the song's notes are `want` (within `tolerance` ticks), else the first difference.
    std::string compareNotes (const std::vector<MidiNote>& got, const std::vector<MidiNote>& want, int64_t tolerance = 1)
    {
        if (got.size() != want.size())
            return " " + std::to_string (got.size()) + " notes, want " + std::to_string (want.size());
        for (size_t i = 0; i < got.size(); ++i)
            if (! sameNotes (got[i], want[i], tolerance))
                return " note " + std::to_string (i) + ": pitch " + std::to_string (got[i].pitch) + " " + std::to_string (got[i].start) + "-"
                     + std::to_string (got[i].end) + ", want " + std::to_string (want[i].pitch) + " " + std::to_string (want[i].start) + "-"
                     + std::to_string (want[i].end);
        return {};
    }

    //==========================================================================
    void testMidiFile()
    {
        std::cout << "== MIDI files ==\n";
        MidiSong a;
        a.ppq = 960;
        a.bpm = 97.5;
        a.numerator = 3;
        a.denominator = 4;
        a.name = "test";
        a.notes = { { 0, 480, 60, 100, 0 }, { 0, 480, 64, 90, 0 }, { 480, 1920, 67, 80, 1 }, { 1920, 1921, 127, 1, 15 }, { 5000, 9000, 0, 127, 3 } };
        const auto bytes = writeMidiFile (a);
        MidiSong b;
        std::string error;
        const bool read = readMidiFile (bytes.data(), bytes.size(), b, error);
        bool same = read && b.ppq == 960 && std::abs (b.bpm - 97.5) < 0.01 && b.numerator == 3 && b.denominator == 4 && b.name == "test"
                 && b.notes.size() == a.notes.size();
        for (size_t i = 0; same && i < a.notes.size(); ++i)
            same = sameNotes (a.notes[i], b.notes[i]);
        check (same, "write → read keeps every note (pitch, velocity, channel, ticks), 960 PPQ, the tempo and 3/4");
        check (bytes.size() > 22 && std::string ((const char*) bytes.data(), 4) == "MThd" && bytes[9] == 1 && bytes[11] == 2
                   && bytes[12] == 0x03 && bytes[13] == 0xc0,
               "the file is type 1 with two tracks at 960 ticks per quarter");

        // A hand-made type 0 file at 480 PPQ: running status, note-on velocity 0 as note-off,
        // a program change (one data byte), a controller and a SysEx in between.
        const std::vector<uint8_t> type0 = {
            'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xe0,
            'M', 'T', 'r', 'k', 0, 0, 0, 38,
            0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20,      // 120 BPM
            0x00, 0x90, 0x3c, 0x64,                        // C4 on
            0x00, 0x40, 0x50,                              // E4 on (running status)
            0x83, 0x60, 0x3c, 0x00,                        // 480 ticks later: C4 off (velocity 0)
            0x00, 0x40, 0x00,                              // E4 off
            0x00, 0xc0, 0x05,                              // program change
            0x00, 0xb0, 0x07, 0x64,                        // volume
            0x00, 0xf0, 0x03, 0x01, 0x02, 0xf7,            // SysEx
            0x00, 0xff, 0x2f, 0x00
        };
        MidiSong c;
        const bool ok0 = readMidiFile (type0.data(), type0.size(), c, error);
        check (ok0 && c.ppq == 480 && std::abs (c.bpm - 120.0) < 1.0e-6 && c.notes.size() == 2 && c.notes[0].pitch == 60 && c.notes[0].end == 480
                   && c.notes[1].pitch == 64 && c.notes[1].velocity == 0x50 && c.notes[1].end == 480,
               "a type 0 file with running status, velocity-0 note-offs and other messages reads right" + (ok0 ? std::string() : ": " + error));

        // The same pitch struck twice before either ends: the first on ends first.
        MidiSong overlap;
        overlap.notes = { { 0, 960, 60, 100, 0 }, { 240, 480, 60, 90, 0 } };
        const auto ob = writeMidiFile (overlap);
        MidiSong od;
        readMidiFile (ob.data(), ob.size(), od, error);
        check (od.notes.size() == 2 && od.notes[0].end == 480 && od.notes[1].end == 960,
               "overlapping notes on one pitch pair in order (first on, first off)");

        std::vector<uint8_t> smpte (type0);
        smpte[12] = 0xe7;
        smpte[13] = 0x28;
        MidiSong x;
        check (! readMidiFile (smpte.data(), smpte.size(), x, error), "an SMPTE-timed file is refused (" + error + ")");
        const std::vector<uint8_t> riff = { 'R', 'I', 'F', 'F', 0, 0, 0, 4, 'W', 'A', 'V', 'E', 0, 0 };
        check (! readMidiFile (riff.data(), riff.size(), x, error), "a file that isn't MIDI is refused (" + error + ")");
        std::vector<uint8_t> truncated (type0.begin(), type0.begin() + 30);
        check (! readMidiFile (truncated.data(), truncated.size(), x, error), "a truncated file is refused (" + error + ")");
    }

    //==========================================================================
    EngineSettings testPatternSettings()
    {
        EngineSettings s;
        s.testPattern = true;
        s.smart.map.mapping = Mapping::ChordTones;
        return s;
    }

    void testRecorder()
    {
        std::cout << "== Export recorder ==\n";

        // Nothing to export until the first bar has finished.
        {
            Host h (48000.0, 120.0);
            h.settings = testPatternSettings();
            Take take;
            take.attach (h);
            h.run (h.at (3.9), 512);
            const int before = take.recorder->info().completedBars();
            const bool emptyBefore = take.song().notes.empty();
            h.run (h.at (4.05), 512);
            check (before == 0 && emptyBefore && take.recorder->info().completedBars() == 1 && take.song().notes.size() == 12,
                   "the handle has nothing until the first bar has finished, then one bar (4 chords × 3 notes)");
        }

        // 6.5 bars, then stop: six bars, every note as the engine sent it.
        for (const auto& c : configs())
        {
            Host h (c.rate, c.bpm);
            h.settings = testPatternSettings();
            Take take;
            take.attach (h);
            h.run (h.at (26.0), c.block);
            h.stop (c.block);
            const auto song = take.song();
            const std::string why = compareNotes (song.notes, hostNotes (h, 0.0, 24.0));
            if (! why.empty() || song.notes.size() != 72 || song.ppq != 960 || std::abs (song.bpm - c.bpm) > 1.0e-9)
            {
                check (false, "6.5 bars, stopped: the file holds bars 1-6 exactly (" + describe (c) + "):" + why + " "
                                  + std::to_string (song.notes.size()) + " notes at " + std::to_string (song.bpm) + " BPM");
                break;
            }
            if (&c == &configs().back())
                check (true, "6.5 bars, stopped: the file holds bars 1-6, every note to the tick, in all configurations");
        }

        // 40 bars: the last 32.
        {
            Host h (44100.0, 120.0);
            h.settings = testPatternSettings();
            Take take;
            take.attach (h);
            h.run (h.at (161.0), 512);
            const auto song = take.song();
            check (compareNotes (song.notes, hostNotes (h, 32.0, 160.0)).empty() && song.notes.front().start == 0,
                   "40 bars: the file holds the last 32 (bars 9-40), starting at its first bar line");
        }

        // A chord held across the end is cut at the last bar line; one started before the first
        // exported bar is left out.
        {
            Host h (48000.0, 120.0);
            Take take;
            take.attach (h);
            h.key (2.0, 48, true);        // C (I) from beat 2…
            h.key (9.5, 48, false);
            h.key (5.0, 50, true);        // …and D (ii) from beat 5
            h.key (5.5, 50, false);
            h.run (h.at (9.75), 256);
            const auto two = take.song();
            bool cut = two.notes.size() == 6;
            for (const auto& n : two.notes)
                cut &= n.pitch >= 60 ? true : (n.start == 2 * 960 ? n.end == 8 * 960 : (n.start == 5 * 960 && n.end == 5 * 960 + 480));
            const auto one = take.song (1);
            check (cut && one.notes.size() == 3 && one.notes[0].start == 960,
                   "a chord held past the last bar line is cut there; with one bar, the chord begun before it is left out");
        }

        // A one-bar loop played three times: three bars of the same notes.
        {
            Host h (48000.0, 120.0);
            h.settings = testPatternSettings();
            h.looping = true;
            h.loopStart = 0.0;
            h.loopEnd = 4.0;
            Take take;
            take.attach (h);
            h.run (h.at (12.0) + 10, 333);
            h.stop();
            const auto song = take.song();
            bool same = song.notes.size() == 36;
            for (size_t i = 0; same && i < 12; ++i)
                for (int pass = 1; pass < 3; ++pass)
                {
                    MidiNote moved = song.notes[i];
                    moved.start += pass * 4 * 960;
                    moved.end += pass * 4 * 960;
                    same &= sameNotes (song.notes[i + 12 * (size_t) pass], moved, 1);
                }
            check (same, "a loop runs on like a tape: three passes of a one-bar loop are three equal bars");
        }

        // Playback started on beat 3: the take starts at the bar line before it.
        {
            Host h (48000.0, 120.0);
            h.settings = testPatternSettings();
            h.ppq = 6.0;
            Take take;
            take.attach (h);
            h.run (h.at (6.2), 512);
            const auto info = take.recorder->info();
            const auto song = take.song();
            check (info.startBeat == 2.0 && info.completedBars() == 2 && compareNotes (song.notes, hostNotes (h, 4.0, 12.0, 6.0)).empty()
                       && song.notes.front().start == 2 * 960,
                   "a take started on beat 3 of bar 2 begins at bar 2's line: its first chord is at beat 3 of the file");
        }

        // A new take replaces the old one.
        {
            Host h (48000.0, 120.0);
            h.settings = testPatternSettings();
            Take take;
            take.attach (h);
            h.run (h.at (8.5), 512);
            h.stop (512);
            const auto first = take.recorder->info().take;
            h.playing = true;
            h.ppq = 0.0;
            const int64_t start = h.pos;
            h.settings.key = 2;           // D major: other notes
            h.run (start + h.at (4.5), 512);
            const auto song = take.song();
            bool fromSecond = take.recorder->info().take == first + 1 && song.notes.size() == 12;
            for (const auto& n : song.notes)
                fromSecond &= n.pitch % 12 == 2 || n.pitch % 12 == 6 || n.pitch % 12 == 9 || n.pitch % 12 == 4 || n.pitch % 12 == 7
                           || n.pitch % 12 == 11 || n.pitch % 12 == 1;
            check (fromSecond && song.notes[0].pitch == 50, "playing again starts a new take: only its notes are exported");
        }
    }

    //==========================================================================
    // R6: export → import gives the same chords.

    // The pitches each white key of the chord zone plays (pressed one after another, playing).
    std::vector<std::vector<int>> playKeys (const EngineSettings& s, const ChordEdits* edits)
    {
        Host h (48000.0, 120.0);
        h.settings = s;
        h.settings.testPattern = false;
        h.edits = edits;
        for (int n = 0; n < 7; ++n)
        {
            h.key (2.0 * n, 48 + whiteOffset[n], true);
            h.key (2.0 * n + 1.0, 48 + whiteOffset[n], false);
        }
        h.run (h.at (14.0), 256);
        std::vector<std::vector<int>> chords (7);
        for (const auto& hit : h.hits)
            if (hit.on)
            {
                const int n = (int) (hit.sample / h.at (2.0));
                if (n >= 0 && n < 7)
                    chords[(size_t) n].push_back (hit.note);
            }
        for (auto& c : chords)
        {
            std::sort (c.begin(), c.end());
            c.erase (std::unique (c.begin(), c.end()), c.end());
        }
        return chords;
    }

    struct RoundTrip
    {
        std::string name;
        EngineSettings settings;
        bool edited = false;
        ChordEdits edits;
    };

    void roundTrip (const RoundTrip& c)
    {
        // 1. The take: the test pattern plays slot b mod 7 on beat b, for four bars.
        Host h (44100.0, 120.0);
        h.settings = c.settings;
        h.settings.testPattern = true;
        h.edits = c.edited ? &c.edits : nullptr;
        Take take;
        take.attach (h);
        h.run (h.at (16.0) + 100, 512);
        h.stop (512);
        const auto song = take.song();
        const bool asPlayed = compareNotes (song.notes, hostNotes (h, 0.0, 16.0)).empty();

        // 2. Through a file and back.
        const auto bytes = writeMidiFile (song);
        MidiSong back;
        std::string error;
        readMidiFile (bytes.data(), bytes.size(), back, error);
        const ChordImport imp = importMidi (back);

        // 3. The imported chord zone, played key by key.
        EngineSettings s;
        s.group = c.settings.group;
        s.key = imp.key;
        s.scale = imp.scale;
        s.chordOctave = imp.chordOctave;
        s.progression = Progression::Custom;
        s.customOrder = imp.order;
        ChordEdits edits;
        edits.group = s.group;
        edits.degrees = imp.degrees;
        edits.version = 1;
        const auto played = playKeys (s, &edits);

        // The take's distinct chords, in order of first appearance (slot 0, 1, … skipping repeats).
        const auto original = playKeys (c.settings, c.edited ? &c.edits : nullptr);
        std::vector<std::vector<int>> distinct;
        for (const auto& chord : original)
            if (std::find (distinct.begin(), distinct.end(), chord) == distinct.end())
                distinct.push_back (chord);
        const std::vector<std::vector<int>> got (played.begin(), played.begin() + (long) std::min<size_t> (distinct.size(), played.size()));

        check (asPlayed && imp.ok && imp.droppedNotes == 0 && imp.count == (int) distinct.size() && got == distinct,
               "round trip, " + c.name + ": " + std::to_string (song.notes.size()) + " notes exported as played, " + imp.message
                   + (got == distinct ? "" : "\n          want " + text (distinct) + "\n          got  " + text (got)));
    }

    void testRoundTrip()
    {
        std::cout << "== R6 round trip: export → import gives the same chords ==\n";
        std::vector<RoundTrip> cases;

        RoundTrip c { "C Major, Triads, Diatonic", {}, false, {} };
        cases.push_back (c);

        c = { "D Dorian, Sevenths, Pop, edits (V: b9; I: 7th an octave up too)", {}, true, {} };
        c.settings.key = 2;
        c.settings.scale = Scale::Dorian;
        c.settings.group = ChordGroup::Sevenths;
        c.settings.progression = Progression::Pop;
        c.edits.group = ChordGroup::Sevenths;
        editor::setOn (c.edits, ChordGroup::Sevenths, Scale::Dorian, 4, 4, true);
        editor::setAccidental (c.edits, ChordGroup::Sevenths, Scale::Dorian, 4, 4, -1);
        editor::setOctave (c.edits, ChordGroup::Sevenths, Scale::Dorian, 0, 3, 1, true);
        c.edits.version = 7;
        cases.push_back (c);

        c = { "Eb Harmonic Minor, Ninths, Circle of Fifths", {}, false, {} };
        c.settings.key = 3;
        c.settings.scale = Scale::HarmonicMinor;
        c.settings.group = ChordGroup::Ninths;
        c.settings.progression = Progression::CircleOfFifths;
        cases.push_back (c);

        c = { "A Natural Minor (raised V), Triads, Strum Up 1/64, Stack +1, Chord Octave -1", {}, false, {} };
        c.settings.key = 9;
        c.settings.scale = Scale::NaturalMinor;
        c.settings.chordOctave = -1;
        c.settings.fx.strum = StrumMode::Up;
        c.settings.fx.strumRate = 2;
        c.settings.fx.stack = StackMode::PlusOne;
        cases.push_back (c);

        c = { "F# Lydian, Drop-2 Sevenths, Custom 1-4-1-5-6-4-7 (repeats)", {}, false, {} };
        c.settings.key = 6;
        c.settings.scale = Scale::Lydian;
        c.settings.group = ChordGroup::Drop2Sevenths;
        c.settings.progression = Progression::Custom;
        c.settings.customOrder = { 0, 3, 0, 4, 5, 3, 6 };
        cases.push_back (c);

        c = { "Bb Mixolydian, Thirteenths, Stack +1&2, Chord Octave +1", {}, false, {} };
        c.settings.key = 10;
        c.settings.scale = Scale::Mixolydian;
        c.settings.group = ChordGroup::Thirteenths;
        c.settings.chordOctave = 1;
        c.settings.fx.stack = StackMode::PlusOneAndTwo;
        cases.push_back (c);

        c = { "E Phrygian Dominant, Open Triads, Quartal shapes, edits on every degree", {}, true, {} };
        c.settings.key = 4;
        c.settings.scale = Scale::PhrygianDominant;
        c.settings.group = ChordGroup::OpenTriads;
        c.edits.group = ChordGroup::OpenTriads;
        for (int d = 0; d < 7; ++d)
        {
            editor::setOn (c.edits, ChordGroup::OpenTriads, Scale::PhrygianDominant, d, 3, d % 2 == 0);
            editor::setOctave (c.edits, ChordGroup::OpenTriads, Scale::PhrygianDominant, d, 0, -1, true);
            editor::setAccidental (c.edits, ChordGroup::OpenTriads, Scale::PhrygianDominant, d, 2, d == 3 ? 1 : 0);
        }
        c.edits.version = 3;
        cases.push_back (c);

        c = { "G Hungarian Minor, Neo-Soul, Two-Five-One", {}, false, {} };
        c.settings.key = 7;
        c.settings.scale = Scale::HungarianMinor;
        c.settings.group = ChordGroup::NeoSoul;
        c.settings.progression = Progression::TwoFiveOne;
        cases.push_back (c);

        for (const auto& rt : cases)
            roundTrip (rt);
    }

    //==========================================================================
    // Import of hand-made chord lists.

    // What the imported chord zone plays on each key (the first `count`).
    std::vector<std::vector<int>> playImport (const ChordImport& imp, ChordGroup group = ChordGroup::Triads)
    {
        EngineSettings s;
        s.group = group;
        s.key = imp.key;
        s.scale = imp.scale;
        s.chordOctave = imp.chordOctave;
        s.progression = Progression::Custom;
        s.customOrder = imp.order;
        ChordEdits edits;
        edits.group = group;
        edits.degrees = imp.degrees;
        edits.version = 1;
        const ChordSet set = makeChords (group, s.scale, imp.order, &edits);
        std::vector<std::vector<int>> out;
        for (int n = 0; n < imp.count; ++n)
        {
            const auto notes = chordNotes (set[(size_t) n], { s.key, s.scale, s.chordOctave });
            std::vector<int> v (notes.begin(), notes.end());
            std::sort (v.begin(), v.end());
            out.push_back (v);
        }
        return out;
    }

    void importCase (const std::string& name, const std::vector<std::vector<int>>& chords, int key, Scale scale,
                     const std::vector<int>& degrees = {})
    {
        const auto imp = importChords (chords);
        std::vector<std::vector<int>> want;
        for (auto c : chords)
        {
            std::sort (c.begin(), c.end());
            want.push_back (c);
        }
        if (want.size() > 7)
            want.resize (7);
        bool degreesOk = true;
        for (size_t n = 0; n < degrees.size(); ++n)
            degreesOk &= imp.order[n] == degrees[n];
        const auto got = playImport (imp);
        check (imp.ok && (key < 0 || (imp.key == key && imp.scale == scale)) && degreesOk && got == want && imp.droppedNotes == 0,
               name + ": " + imp.message + (got == want ? "" : "\n          want " + text (want) + "\n          got  " + text (got)));
    }

    void testImport()
    {
        std::cout << "== import ==\n";
        importCase ("C F G Am (triads)", { { 48, 52, 55 }, { 53, 57, 60 }, { 55, 59, 62 }, { 57, 60, 64 } }, 0, Scale::Major, { 0, 3, 4, 5 });
        importCase ("Am F C G: A minor", { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } }, 9, Scale::NaturalMinor, { 0, 5, 2, 6 });
        importCase ("Dm7 G7 Cmaj7: C major", { { 50, 53, 57, 60 }, { 43, 47, 50, 53 }, { 48, 52, 55, 59 } }, 0, Scale::Major, { 1, 4, 0 });
        importCase ("inversions and slash chords: C/E, F/C, G/B, Am/G", { { 52, 55, 60 }, { 48, 53, 57 }, { 47, 50, 55 }, { 43, 45, 48, 52 } },
                    0, Scale::Major, { 0, 3, 4, 5 });
        importCase ("non-diatonic: C Bb F Ab (bVII, bVI) and E7 (V/vi), whatever key fits best",
                    { { 48, 52, 55 }, { 46, 50, 53 }, { 53, 57, 60 }, { 44, 48, 51 }, { 52, 56, 59, 62 } }, -1, Scale::Major);
        importCase ("two chords on one root (C, C7, Cmaj9) take three degrees",
                    { { 48, 52, 55 }, { 48, 52, 55, 58 }, { 48, 52, 55, 59, 62 } }, 0, Scale::Major);
        importCase ("G13 (all seven steps) and a spread voicing", { { 43, 53, 59, 64, 69, 72, 74 }, { 36, 55, 64, 72, 79 } }, 0, Scale::Major);
        importCase ("Dm Gm A7: D harmonic minor", { { 50, 53, 57 }, { 55, 58, 62 }, { 57, 61, 64, 67 } }, 2, Scale::HarmonicMinor, { 0, 3, 4 });

        // High chords: Chord Octave moves so every note fits.
        {
            const std::vector<std::vector<int>> high = { { 96, 100, 103 }, { 101, 105, 108 } };
            const auto imp = importChords (high);
            check (imp.ok && imp.chordOctave > 0 && playImport (imp) == high && imp.droppedNotes == 0,
                   "chords three octaves up: Chord Octave " + std::to_string (imp.chordOctave) + ", every note kept");
        }

        // More than seven chords: the first seven.
        {
            std::vector<std::vector<int>> nine;
            for (int r = 0; r < 9; ++r)
                nine.push_back ({ 48 + r, 52 + r, 55 + r });
            const auto imp = importChords (nine);
            check (imp.ok && imp.count == 7 && imp.extraChords == 2 && playImport (imp) == std::vector<std::vector<int>> (nine.begin(), nine.begin() + 7),
                   "nine chromatic triads: the first seven, two left out");
        }

        // Single notes aren't chords; a file with none imports nothing.
        {
            MidiSong melody;
            for (int i = 0; i < 8; ++i)
                melody.notes.push_back ({ i * 480, i * 480 + 400, (uint8_t) (60 + i), 100, 0 });
            check (! importMidi (melody).ok, "a melody (no two notes together) has no chords to import");
            MidiSong strummed;
            strummed.ppq = 960;
            strummed.notes = { { 0, 960, 48, 100, 0 }, { 60, 960, 52, 100, 0 }, { 120, 960, 55, 100, 0 }, { 960, 1900, 53, 100, 0 },
                               { 1000, 1900, 57, 100, 0 }, { 1100, 1900, 60, 100, 0 }, { 1920, 2800, 48, 100, 0 }, { 1920, 2800, 52, 100, 0 },
                               { 1920, 2800, 55, 100, 0 } };
            const auto imp = importMidi (strummed);
            check (imp.ok && imp.count == 2, "strummed chords (onsets within a sixteenth) are one chord each; a repeat isn't a new chord");
        }

        // Random chords: every note is played back, or counted as dropped; tertian ones are exact.
        {
            std::mt19937 rng (6);
            int exact = 0, accounted = 0, tertian = 0, tertianExact = 0, processed = 0;
            const int runs = 2000;
            for (int run = 0; run < runs; ++run)
            {
                std::vector<std::vector<int>> chords;
                const bool stacked = run % 2 == 0;
                const int n = 1 + (int) (rng() % 7);
                for (int i = 0; i < n; ++i)
                {
                    std::vector<int> c;
                    const int root = 36 + (int) (rng() % 36);
                    if (stacked)
                    {
                        // A root, a 3rd and a 5th of some kind, maybe a 7th and extensions, maybe
                        // inverted or doubled.
                        static const int thirds[2] = { 3, 4 }, fifths[3] = { 6, 7, 8 }, sevenths[3] = { 9, 10, 11 };
                        c = { root, root + thirds[rng() % 2], root + fifths[rng() % 3] };
                        if (rng() % 2) c.push_back (root + sevenths[rng() % 3]);
                        if (rng() % 3 == 0) c.push_back (root + 13 + (int) (rng() % 3) - 1);
                        if (rng() % 2) c[0] += 12;
                        if (rng() % 3 == 0) c.push_back (root - 12);
                    }
                    else
                    {
                        const int size = 2 + (int) (rng() % 5);
                        for (int k = 0; k < size; ++k)
                            c.push_back (root + (int) (rng() % 30));
                    }
                    std::sort (c.begin(), c.end());
                    c.erase (std::unique (c.begin(), c.end()), c.end());
                    if (c.size() < 2 || std::find (chords.begin(), chords.end(), c) != chords.end())
                        continue;
                    chords.push_back (c);
                }
                if (chords.empty())
                    continue;
                ++processed;
                const auto imp = importChords (chords);
                const auto got = playImport (imp);
                bool ok = imp.ok && got.size() == chords.size();
                int missing = 0;
                for (size_t i = 0; ok && i < chords.size(); ++i)
                {
                    for (int p : got[i])
                        ok &= std::find (chords[i].begin(), chords[i].end(), p) != chords[i].end();
                    missing += (int) chords[i].size() - (int) got[i].size();
                }
                ok &= missing == imp.droppedNotes;
                accounted += ok ? 1 : 0;
                exact += ok && missing == 0 ? 1 : 0;
                tertian += stacked ? 1 : 0;
                tertianExact += stacked && ok && missing == 0 ? 1 : 0;
            }
            check (accounted == processed, std::to_string (accounted) + "/" + std::to_string (processed)
                                               + " random chord sets: every imported note was in the chord and every other note is counted as dropped");
            check (tertianExact == tertian, std::to_string (tertianExact) + "/" + std::to_string (tertian)
                                                + " sets of stacked-third chords (any kind of 3rd, 5th, 7th, a 9th, inversions, doublings) import exactly; "
                                                + std::to_string (exact) + " of all " + std::to_string (processed) + " sets are exact");
        }
    }
}

int main()
{
    testMidiFile();
    testRecorder();
    testRoundTrip();
    testImport();
    return testutil::finish ("test_midiio");
}
