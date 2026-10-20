// Smart Scale mappings (PLAN.md §4.3): which pitch each white key of the Smart Scale zone (the
// split and above) plays, following the current chord (the chord slot played last).
//
// Let T be the key tonic and R the current chord's root (its degree d in the scale). Every key
// sounds in its own octave: C_o is the C at or below the key, moved by the Smart Octave, and
// the tonic sits on C_o + T, as in the chord zone (T on C). White key letter j (C = 0 … B = 6):
//
//   Chord Tones  scale index d + 2j: 1-3-5-7-9-11-13 stacked upward from R (R at its place in
//                the key, C_o + T + scale[d])
//   Dynamic      scale index d + j: the mode of the current chord, R on C
//   Relative     scale index j: the scale from T, T on C (static)
//   Absolute     the scale tone written with letter j, nearest the key (D major: F → F#; static)
//   Chromatic    the key itself (only moved by the Smart Octave)
//
// Chord Editor accidentals: in Chord Tones and Dynamic, a tone takes the accidental of the
// chord step it is (scale index d + 2k is step k: Dynamic's j is step 4j mod 7), but only when
// that step is on. Relative and Absolute are static and take none.
//
// Inversions (Chord Tones and Dynamic only, decided 2026-10-19): every tone is folded into the
// octave from the tonic, [C_o + T, C_o + T + 12), whatever the chord. The zone keeps its
// register, so a lower pitch can sit to the right of a higher one.
//
// The pattern keys (black) play the chord's stacked thirds from R in their octave
// (chordTones), or a melody (melodyTone). Plain C++, no allocation.
#pragma once

#include "../theory/ChordModel.h"
#include "../theory/ChordNamer.h"
#include "InputRouter.h"
#include "Stack.h"
#include <array>
#include <cstdint>
#include <initializer_list>

namespace chordism
{
    // The order of the Mapping parameter's choices: only ever append.
    enum class Mapping : uint8_t { ChordTones, Dynamic, Relative, Absolute, Chromatic, Count };
    constexpr int numMappings = (int) Mapping::Count;

    inline const char* mappingName (Mapping m) noexcept
    {
        static constexpr const char* names[numMappings] = { "Chord Tones", "Dynamic", "Relative", "Absolute", "Chromatic" };
        const int i = (int) m;
        return names[i >= 0 && i < numMappings ? i : 0];
    }

    struct ScaleMapSettings
    {
        Mapping mapping = Mapping::ChordTones;
        bool inversions = false;
        int octave = 0;                   // Smart Octave, -3..3

        bool operator== (const ScaleMapSettings& o) const noexcept
        {
            return mapping == o.mapping && inversions == o.inversions && octave == o.octave;
        }
        bool operator!= (const ScaleMapSettings& o) const noexcept { return ! (*this == o); }
    };

    // The chord the Smart Scale follows: the chord zone's key and scale (with a Key/Scale shifter
    // applied) and the chord slot played last. Before any chord is played: degree 0, no steps.
    struct SmartChord
    {
        int key = 0;
        Scale scale = Scale::Major;
        ChordSlot slot {};

        bool operator== (const SmartChord& o) const noexcept { return key == o.key && scale == o.scale && slot == o.slot; }
        bool operator!= (const SmartChord& o) const noexcept { return ! (*this == o); }
    };

    class ScaleMapper
    {
    public:
        ScaleMapper() noexcept { update ({}, {}); }

        void update (const ScaleMapSettings& s, const SmartChord& c) noexcept
        {
            if (s == settings && c == chord && built)
                return;
            settings = s;
            chord = c;
            chord.key = ((chord.key % numKeys) + numKeys) % numKeys;
            built = true;

            // Absolute: the letter each scale tone is written with in this key.
            const Spelling sp = makeSpelling (chord.key, chord.scale);
            const auto& steps = scaleInfo (chord.scale).semitones;
            letterShift.fill (0);
            for (int i = 0; i < 7; ++i)
            {
                const int pc = (chord.key + steps[(size_t) i]) % 12;
                letterShift[(size_t) sp.letter[(size_t) pc]] = sp.accidental[(size_t) pc];
            }
        }

        const ScaleMapSettings& getSettings() const noexcept { return settings; }
        const SmartChord& getChord() const noexcept { return chord; }

        // The pitch Smart Scale key `note` plays, or -1 (a black key outside Chromatic, or out of
        // MIDI range).
        int tone (int note) const noexcept
        {
            if (note < 0 || note > 127)
                return -1;
            if (settings.mapping == Mapping::Chromatic)
                return valid (note + 12 * settings.octave);

            const int j = keys::whiteLetter (note % 12);
            if (j < 0)
                return -1;
            const int c = octaveC (note);
            const int t = c + chord.key;
            const int d = chord.slot.degree;
            int p = -1;
            switch (settings.mapping)
            {
                case Mapping::ChordTones: p = t + scaleOffset (chord.scale, d + 2 * j) + accidental (j); break;
                case Mapping::Dynamic:    p = t + scaleOffset (chord.scale, d + j) + accidental ((4 * j) % 7); break;
                case Mapping::Relative:   p = t + scaleOffset (chord.scale, j); break;
                case Mapping::Absolute:   p = c + keys::whiteOffsets[j] + letterShift[(size_t) j]; break;
                case Mapping::Chromatic: case Mapping::Count: default: break;
            }
            if (settings.inversions && (settings.mapping == Mapping::ChordTones || settings.mapping == Mapping::Dynamic))
                p = t + (((p - t) % 12) + 12) % 12;
            return valid (p);
        }

        // The tones a pattern key `note` runs over: the chord's first `count` stacked thirds from
        // R (1, 3, 5, 7, 9, 11, 13; 2 = root and 5th), with the chord's accidentals, in the key's
        // octave, ascending.
        VoicedNotes chordTones (int note, int count) const noexcept
        {
            std::array<bool, 128> on {};
            const int t = octaveC (note) + chord.key;
            const int d = chord.slot.degree;
            auto add = [&] (int k) noexcept
            {
                const int p = t + scaleOffset (chord.scale, d + 2 * k) + accidental (k);
                if (p >= 0 && p < 128)
                    on[(size_t) p] = true;
            };
            if (count <= 2)
            {
                add (0);
                add (2);
            }
            else
            {
                for (int k = 0; k < count && k < numChordSteps; ++k)
                    add (k);
            }

            VoicedNotes v;
            for (int n = 0; n < 128 && v.size < VoicedNotes::capacity; ++n)
                if (on[(size_t) n])
                    v.notes[(size_t) v.size++] = (uint8_t) n;
            return v;
        }

        // A melody note's pitch (Melody.h) for a pattern key `note`: scale index `index` from the
        // tonic in the key's octave. A `strong` note moves to the nearest chord tone (the first
        // `count` stacked thirds from R, as chordTones; the lower one on a tie). A tone that is
        // a step of the chord (scale index d + 2k) takes that step's accidental when it's on.
        int melodyTone (int note, int index, bool strong, int count) const noexcept
        {
            if (note < 0 || note > 127)
                return -1;
            const int d = chord.slot.degree;
            if (strong)
            {
                for (int delta : { 0, -1, 1, -2, 2, -3, 3 })
                    if (isChordTone (index + delta - d, count))
                    {
                        index += delta;
                        break;
                    }
            }
            const int rel = (((index - d) % 7) + 7) % 7;
            return valid (octaveC (note) + chord.key + scaleOffset (chord.scale, index) + accidental ((4 * rel) % 7));
        }

    private:
        static int valid (int p) noexcept { return p >= 0 && p < 128 ? p : -1; }

        // Scale index `rel` above R is one of the chord's first `count` stacked thirds (2 = root
        // and 5th).
        static bool isChordTone (int rel, int count) noexcept
        {
            rel = ((rel % 7) + 7) % 7;
            if (count <= 2)
                return rel == 0 || rel == 4;
            for (int k = 0; k < count && k < numChordSteps; ++k)
                if ((2 * k) % 7 == rel)
                    return true;
            return false;
        }

        // The C of the key's octave, moved by the Smart Octave.
        int octaveC (int note) const noexcept { return 12 * (note / 12) + 12 * settings.octave; }

        // The current chord's accidental for chord step k, if that step is on.
        int accidental (int k) const noexcept
        {
            const Step& s = chord.slot.steps[(size_t) (k % numChordSteps)];
            return s.on ? s.accidental : 0;
        }

        ScaleMapSettings settings;
        SmartChord chord;
        std::array<int8_t, 7> letterShift {};
        bool built = false;
    };
}
