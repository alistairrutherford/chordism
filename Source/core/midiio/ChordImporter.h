// MIDI chord import (PLAN.md §4.6): drag a .mid onto the editor and its chords become the
// chord zone's seven chords, with their exact voicings.
//
//   1. chords   notes that start within a sixteenth of each other, while the first still sound,
//               are one chord (so strummed chords count); a chord needs two or more pitches. The distinct chords (exact
//               pitch sets) are kept in order of first appearance, up to seven.
//   2. key      every Key × Scale is scored: pitch classes outside the scale (fewest first),
//               then the scale (Major and Natural Minor before the minor variants, then the
//               modes, then the rest), then where the tonic is a chord's root (the first
//               chord's, the last one's, any, none).
//   3. encode   each chord becomes a degree with seven steps (on, accidental, octave mask): every
//               pitch class takes a step whose scale tone is within two semitones (the step its
//               interval above the root usually is, if it can), and every note an octave of that
//               step (-2..+2). A chord goes on its root's degree, or, if an earlier chord holds
//               that degree, on the free degree that encodes it best (edits belong to a degree,
//               so two chords can't share one). Chord Octave is chosen so that every note fits
//               (the nearest to 0); notes that fit nowhere are dropped and counted.
//   4. order    the progression becomes Custom: white key n plays the degree of chord n; keys
//               beyond the imported chords play the unused degrees.
//
// So playing white key n gives exactly the pitches of imported chord n. Plain C++, no JUCE;
// message thread only (std::vector, std::string).
#pragma once

#include "../theory/ChordEditor.h"
#include "../theory/ChordNamer.h"
#include "MidiFile.h"
#include <algorithm>
#include <string>
#include <vector>

namespace chordism
{
    struct ChordImport
    {
        bool ok = false;
        std::string message;              // what was imported, or why not

        int key = 0;
        Scale scale = Scale::Major;
        int chordOctave = 0;
        int count = 0;                    // chords imported (1..7)
        DegreeOrder order = identityOrder();                  // white key → degree (Custom)
        std::array<DegreeEdit, numChordSlots> degrees {};     // the chords' steps, by degree
        std::vector<std::vector<int>> chords;                 // the imported chords, by white key
        int extraChords = 0;              // distinct chords after the seventh, left out
        int droppedNotes = 0;             // notes no step could play
    };

    namespace importing
    {
        inline int mod12 (int v) noexcept { return ((v % 12) + 12) % 12; }

        inline unsigned pcMask (const std::vector<int>& pitches) noexcept
        {
            unsigned m = 0;
            for (int p : pitches)
                m |= 1u << mod12 (p);
            return m;
        }

        // The chord's root pitch class (ChordNamer's reading).
        inline int rootOf (const std::vector<int>& pitches)
        {
            if (pitches.empty())
                return 0;
            const int bass = *std::min_element (pitches.begin(), pitches.end());
            return nameChord (pcMask (pitches), mod12 (bass), makeSpelling (0, Scale::Major)).root;
        }

        // The step a tone `interval` semitones above the root usually is: 1, b9/9 → 9, m3/M3 → 3,
        // 11, b5/5/#5 → 5, 6/13 → 13, b7/7 → 7.
        inline int usualStep (int interval) noexcept
        {
            static constexpr int step[12] = { 0, 4, 4, 1, 1, 5, 2, 2, 2, 6, 3, 3 };
            return step[mod12 (interval)];
        }

        struct Encoding
        {
            int cost = 1 << 30;
            int dropped = 0;
            std::array<Step, numChordSteps> steps {};

            bool betterThan (const Encoding& o) const noexcept
            {
                return dropped != o.dropped ? dropped < o.dropped : cost < o.cost;
            }
        };

        struct PitchClass
        {
            int pc = 0, interval = 0;
            std::vector<int> notes;
        };

        class Encoder
        {
        public:
            Encoder (const std::vector<int>& pitches, int keyIn, Scale scaleIn, int degreeIn, int chordOctave)
                : key (keyIn), scale (scaleIn), degree (degreeIn),
                  base (chordBaseNote + keyIn + 12 * chordOctave)
            {
                const int root = rootOf (pitches);
                for (int p : pitches)
                {
                    auto it = std::find_if (pcs.begin(), pcs.end(), [p] (const PitchClass& c) { return c.pc == mod12 (p); });
                    if (it == pcs.end())
                        pcs.push_back ({ mod12 (p), mod12 (p - root), { p } });
                    else
                        it->notes.push_back (p);
                }
                std::sort (pcs.begin(), pcs.end(), [] (const PitchClass& a, const PitchClass& b) { return a.interval < b.interval; });
                for (auto& s : current.steps)
                    s = {};
            }

            Encoding run()
            {
                search (0, 0u, 0, 0);
                return best;
            }

        private:
            static constexpr int dropCost = 100;

            void search (size_t i, unsigned used, int cost, int dropped)
            {
                if (cost >= best.cost)    // a dropped note costs more than any accidentals
                    return;
                if (i == pcs.size())
                {
                    best = current;
                    best.cost = cost;
                    best.dropped = dropped;
                    return;
                }
                const PitchClass& c = pcs[i];
                // The usual step first, so the first answer is a good one and the rest prune.
                const int usual = usualStep (c.interval);
                for (int n = 0; n < numChordSteps; ++n)
                {
                    const int k = (usual + n) % numChordSteps;
                    if ((used >> k) & 1u)
                        continue;
                    const int offset = scaleOffset (scale, degree + 2 * k);
                    const int a = naming::wrap (c.pc - mod12 (key + offset));
                    if (a < -2 || a > 2)
                        continue;
                    const int stepPitch = base + offset + a;
                    int mask = 0, lost = 0;
                    for (int p : c.notes)
                    {
                        const int o = (p - stepPitch) / 12;
                        if (o >= minStepOctave && o <= maxStepOctave && p - stepPitch == 12 * o)
                            mask |= octaveBit (o);
                        else
                            ++lost;
                    }
                    if (mask == 0)
                        continue;
                    current.steps[(size_t) k] = { true, (int8_t) a, (uint8_t) mask };
                    search (i + 1, used | (1u << k), cost + 3 * (a < 0 ? -a : a) + (k != usual ? 1 : 0) + dropCost * lost,
                            dropped + lost);
                    current.steps[(size_t) k] = {};
                }
                // Or leave the pitch class out.
                search (i + 1, used, cost + dropCost * (int) c.notes.size(), dropped + (int) c.notes.size());
            }

            int key;
            Scale scale;
            int degree, base;
            std::vector<PitchClass> pcs;
            Encoding current, best;
        };

        // Scale preference for the key fit (lower is preferred).
        inline int scaleFamily (Scale s) noexcept
        {
            switch (s)
            {
                case Scale::Major: case Scale::NaturalMinor:                        return 0;
                case Scale::HarmonicMinor: case Scale::MelodicMinor:                return 1;
                case Scale::Dorian: case Scale::Mixolydian: case Scale::Lydian:
                case Scale::Phrygian:                                                return 2;
                case Scale::Locrian: case Scale::HarmonicMajor: case Scale::PhrygianDominant:
                case Scale::HungarianMinor: case Scale::DoubleHarmonic: case Scale::Count:
                default:                                                             return 3;
            }
        }

        // The degree whose tone has the letter `pc` is written with in the key.
        inline int degreeOfLetter (int pc, int key, Scale scale) noexcept
        {
            const Spelling sp = makeSpelling (key, scale);
            const int letter = sp.letter[(size_t) mod12 (pc)];
            for (int d = 0; d < 7; ++d)
                if (sp.letter[(size_t) mod12 (key + scaleInfo (scale).semitones[(size_t) d])] == letter)
                    return d;
            return 0;
        }
    }

    // The distinct chords of a song, in order of first appearance (all of them; the importer
    // keeps seven). A note joins a chord if it starts within `tolerance` ticks of the chord's
    // last note (default: a sixteenth) and while all of the chord's notes still sound, so a
    // strummed chord is one chord and an arp isn't.
    inline std::vector<std::vector<int>> findChords (const MidiSong& song, int64_t tolerance = -1)
    {
        if (tolerance < 0)
            tolerance = song.ppq / 4;
        std::vector<std::vector<int>> distinct;
        size_t i = 0;
        while (i < song.notes.size())
        {
            int64_t last = song.notes[i].start, firstEnd = song.notes[i].end;
            std::vector<int> chord;
            for (; i < song.notes.size() && song.notes[i].start - last <= tolerance && (chord.empty() || song.notes[i].start < firstEnd); ++i)
            {
                chord.push_back (song.notes[i].pitch);
                last = song.notes[i].start;
                firstEnd = std::min (firstEnd, song.notes[i].end);
            }
            std::sort (chord.begin(), chord.end());
            chord.erase (std::unique (chord.begin(), chord.end()), chord.end());
            if (chord.size() >= 2 && std::find (distinct.begin(), distinct.end(), chord) == distinct.end())
                distinct.push_back (chord);
        }
        return distinct;
    }

    // Turns distinct chords (each a list of MIDI pitches) into the chord zone's settings.
    inline ChordImport importChords (std::vector<std::vector<int>> chords)
    {
        using namespace importing;
        ChordImport r;
        for (auto& c : chords)
        {
            std::sort (c.begin(), c.end());
            c.erase (std::unique (c.begin(), c.end()), c.end());
        }
        chords.erase (std::remove_if (chords.begin(), chords.end(), [] (const std::vector<int>& c) { return c.empty(); }), chords.end());
        if (chords.empty())
        {
            r.message = "No chords found (a chord is two or more notes starting together).";
            return r;
        }
        if (chords.size() > (size_t) numChordSlots)
        {
            r.extraChords = (int) chords.size() - numChordSlots;
            chords.resize ((size_t) numChordSlots);
        }
        r.count = (int) chords.size();
        r.chords = chords;

        // Key and scale.
        std::vector<int> roots;
        for (const auto& c : chords)
            roots.push_back (rootOf (c));
        long bestScore = -1;
        for (int k = 0; k < numKeys; ++k)
            for (int s = 0; s < numScales; ++s)
            {
                unsigned scaleMask = 0;
                for (auto step : scaleInfo ((Scale) s).semitones)
                    scaleMask |= 1u << mod12 (k + step);
                int outside = 0;
                for (const auto& c : chords)
                    outside += naming::popcount12 (pcMask (c) & ~scaleMask);
                const int tonic = roots.front() == k ? 0 : (roots.back() == k ? 1 : (std::find (roots.begin(), roots.end(), k) != roots.end() ? 2 : 3));
                const long score = (((long) outside * 4 + scaleFamily ((Scale) s)) * 4 + tonic) * 256 + s * 12 + k;
                if (bestScore < 0 || score < bestScore)
                {
                    bestScore = score;
                    r.key = k;
                    r.scale = (Scale) s;
                }
            }

        // Encode every chord at each Chord Octave; keep the best (nearest 0 on a tie).
        struct Plan { int dropped = 0, cost = 0; std::array<int, numChordSlots> degree {}; std::array<Encoding, numChordSlots> enc {}; };
        Plan best;
        bool haveBest = false;
        for (int co : { 0, -1, 1, -2, 2, -3, 3 })
        {
            Plan plan;
            unsigned used = 0;
            for (size_t n = 0; n < chords.size(); ++n)
            {
                const int natural = degreeOfLetter (roots[n], r.key, r.scale);
                Encoding chosen;
                int chosenDegree = -1;
                for (int d = 0; d < numChordSlots; ++d)
                {
                    if ((used >> d) & 1u)
                        continue;
                    Encoding e = Encoder (chords[n], r.key, r.scale, d, co).run();
                    e.cost += d == natural ? 0 : 10;
                    if (chosenDegree < 0 || e.betterThan (chosen))
                    {
                        chosen = e;
                        chosenDegree = d;
                    }
                }
                used |= 1u << chosenDegree;
                plan.degree[n] = chosenDegree;
                plan.enc[n] = chosen;
                plan.dropped += chosen.dropped;
                plan.cost += chosen.cost;
            }
            if (! haveBest || plan.dropped < best.dropped || (plan.dropped == best.dropped && plan.cost < best.cost))
            {
                best = plan;
                haveBest = true;
                r.chordOctave = co;
            }
            if (haveBest && best.dropped == 0 && co == 0)
                break;                    // everything fits in the default octave
        }

        unsigned used = 0;
        for (int n = 0; n < r.count; ++n)
        {
            const int d = best.degree[(size_t) n];
            r.order[(size_t) n] = (int8_t) d;
            r.degrees[(size_t) d].edited = true;
            r.degrees[(size_t) d].steps = best.enc[(size_t) n].steps;
            used |= 1u << d;
        }
        int next = 0;
        for (int n = r.count; n < numChordSlots; ++n)
        {
            while ((used >> next) & 1u)
                ++next;
            r.order[(size_t) n] = (int8_t) next;
            used |= 1u << next;
        }
        r.droppedNotes = best.dropped;

        // What was imported, named in the key.
        const ChordContext ctx { r.key, r.scale, r.chordOctave };
        r.message = "Imported " + std::to_string (r.count) + (r.count == 1 ? " chord" : " chords") + " in "
                  + makeSpelling (r.key, r.scale).name (r.key) + " " + scaleInfo (r.scale).name + ":";
        for (int n = 0; n < r.count; ++n)
        {
            ChordSlot slot;
            slot.degree = r.order[(size_t) n];
            slot.steps = r.degrees[(size_t) slot.degree].steps;
            r.message += " " + nameSlot (slot, ctx).text();
        }
        if (r.extraChords > 0)
            r.message += " (" + std::to_string (r.extraChords) + " more left out: seven keys)";
        if (r.droppedNotes > 0)
            r.message += " (" + std::to_string (r.droppedNotes) + " notes out of range left out)";
        r.ok = true;
        return r;
    }

    inline ChordImport importMidi (const MidiSong& song)
    {
        return importChords (findChords (song));
    }
}
