// The Smart Scale zone's patterns and its effect chain (PLAN.md §4.4, §4.5):
//
//   trigger → Mapping (ScaleMapper.h) → tone → [Pattern | Note Repeat] → Octave → Stack
//           → StepSeq gate → Velocity Algorithm → Hold → (Mute, in ChordismEngine) → out
//
// The zone plays through a second ChordPerformer (ChordFx.h) on the engine's clock, with this
// class as its voicing, so it shares the chord zone's scheduler, grid, Step Sequencer windows,
// Velocity Algorithm and Hold:
//
//   white key (a tone)   its mapped pitch, with Stack's octaves; struck while held, or on every
//                        step of Arp Rate with Note Repeat on (Arp Rate and Arp Gate of the
//                        selected pattern slot)
//   black key (pattern)  pattern slot n (C# D# F# G# A# = 1-5, in every octave): an arp over the
//                        current chord's stacked thirds (Arp Chord: 5ths … 13ths) in the key's
//                        octave, with the slot's Pattern, Rate, Range and Gate; each step is
//                        stacked after the pattern
//   melody slot          a slot in Melody mode (Melody.h) plays a seeded melody over the
//                        current chord instead: Notes per bar, Rhythm, Range, Arp Chord (the
//                        chord tones its strong notes land on) and Gate from the slot; Swing
//                        and Seed are shared by the five slots
//   Chromatic mapping    every key is a tone (the key itself); there are no pattern keys
//
// A pattern follows chord changes at its next step without restarting; a sounding tone keeps
// its pitch through a chord change (the next note repeat takes the new one). Each key's
// performance owns two VoiceTracker sources (384 + 2k, 385 + 2k), used in turn per strike or
// step, so every note-off ends exactly what its key, repeat or pattern step started.
//
// Plain C++, no allocation.
#pragma once

#include "ChordFx.h"
#include "Melody.h"
#include "ScaleMapper.h"
#include <array>

namespace chordism
{
    constexpr int numPatternSlots = 5;

    // Arp Chord: how many of the chord's stacked thirds a pattern uses. The order of the
    // parameter's choices: only ever append.
    enum class ArpChord : uint8_t { Fifths, Triads, Sevenths, Ninths, Elevenths, Thirteenths, Count };
    constexpr int numArpChords = (int) ArpChord::Count;

    inline const char* arpChordName (ArpChord c) noexcept
    {
        static constexpr const char* names[numArpChords] = { "5ths", "Triads", "7ths", "9ths", "11ths", "13ths" };
        const int i = (int) c;
        return names[i >= 0 && i < numArpChords ? i : 0];
    }

    // Chord tones for each Arp Chord: root and 5th, then 3 … 7 stacked thirds.
    inline int arpChordTones (ArpChord c) noexcept
    {
        static constexpr int n[numArpChords] = { 2, 3, 4, 5, 6, 7 };
        const int i = (int) c;
        return n[i >= 0 && i < numArpChords ? i : 1];
    }

    struct PatternSlotSettings
    {
        ArpPattern pattern = ArpPattern::LinearUp;   // never Off
        int rate = 5;                     // index into the pattern rates (1/16)
        int range = 1;                    // octaves, 1..3
        ArpChord chord = ArpChord::Triads;
        double gate = 0.5;                // 0..1
        bool melody = false;              // Melody mode (Phase 6): a melody instead of the arp
        int notes = 8;                    // Melody: notes per bar, 1..16
        MelodyRhythm rhythm = MelodyRhythm::Straight;
    };

    using PatternSlots = std::array<PatternSlotSettings, numPatternSlots>;

    // Factory settings of the five slots (Chordism's own).
    inline PatternSlots defaultPatternSlots() noexcept
    {
        return { {
            { ArpPattern::LinearUp,     5, 1, ArpChord::Triads,   0.5,  false, 8, MelodyRhythm::Straight },    // 1/16
            { ArpPattern::LinearDown,   5, 1, ArpChord::Sevenths, 0.5,  false, 8, MelodyRhythm::Straight },    // 1/16
            { ArpPattern::ConvergeUp,   3, 2, ArpChord::Triads,   0.5,  false, 8, MelodyRhythm::Straight },    // 1/8
            { ArpPattern::BoomerangUp,  6, 1, ArpChord::Ninths,   0.75, false, 8, MelodyRhythm::Straight },    // 1/16T
            { ArpPattern::ZigzagUp,     3, 2, ArpChord::Sevenths, 0.75, false, 8, MelodyRhythm::Straight },    // 1/8
        } };
    }

    // The Smart Scale zone's settings.
    struct SmartSettings
    {
        ScaleMapSettings map;             // Mapping, Inversions, Smart Octave
        bool noteRepeat = false;
        int patternSlot = 0;              // the selected slot (0..4): Note Repeat uses its Rate and Gate
        PatternSlots patterns = defaultPatternSlots();
        StackMode stack = StackMode::Off;
        StepSeqSettings seq;
        VelocitySettings velocity;
        HoldMode hold = HoldMode::Off;
        double melodySwing = 0.0;         // Melody: 0..1, all slots
        uint32_t melodySeed = 1;          // Melody: all slots (each slot mixes in its number)
    };

    class PatternEngine : public PerformerVoicing
    {
    public:
        // A performance's slot: a white key's tone index (from the router), or patternFlag + n
        // for pattern slot n.
        static constexpr int patternFlag = 1 << 12;

        static int slotForPattern (int n) noexcept { return patternFlag + n; }
        static bool isPattern (int slot) noexcept { return slot >= patternFlag && slot < patternFlag + numPatternSlots; }

        // The settings and the chord to follow. Call before the performer uses them.
        void update (const SmartSettings& s, const SmartChord& c) noexcept
        {
            settings = s;
            mapper.update (s.map, c);
        }

        void setChord (const SmartChord& c) noexcept { mapper.update (settings.map, c); }

        const ScaleMapper& getMapper() const noexcept { return mapper; }
        const SmartSettings& getSettings() const noexcept { return settings; }

        // The effect settings the zone's performer runs with.
        static ChordFxSettings fx (const SmartSettings& s) noexcept
        {
            ChordFxSettings f;
            f.stack = s.stack;
            f.strum = StrumMode::Off;
            f.arp = ArpPattern::Off;                  // patterns come from the slots (arp())
            const auto& slot = s.patterns[(size_t) clampSlot (s.patternSlot)];
            f.repeat = { s.noteRepeat, slot.rate, slot.gate };
            f.seq = s.seq;
            f.velocity = s.velocity;
            f.hold = s.hold;
            return f;
        }

        VoicedNotes tones (int key, int slot) const noexcept override
        {
            if (isPattern (slot))
                return mapper.chordTones (key, arpChordTones (settings.patterns[(size_t) (slot - patternFlag)].chord));

            const int t = mapper.tone (key);
            if (t < 0)
                return {};
            ChordNotes one;
            one.notes[0] = (uint8_t) t;
            one.size = 1;
            return stackNotes (one, settings.stack);
        }

        ArpSettings arp (int, int slot) const noexcept override
        {
            if (! isPattern (slot))
                return {};
            const int n = slot - patternFlag;
            const auto& p = settings.patterns[(size_t) n];
            const ArpPattern pattern = p.pattern == ArpPattern::Off ? ArpPattern::LinearUp : p.pattern;
            ArpSettings a { pattern, p.rate, p.range, p.gate, {} };
            if (p.melody)
                a.melody = { true, p.notes, p.rhythm, settings.melodySwing, settings.melodySeed, p.range, n, p.gate };
            return a;
        }

        int melodyTone (int key, int slot, int index, bool strong) const noexcept override
        {
            if (! isPattern (slot))
                return -1;
            return mapper.melodyTone (key, index, strong, arpChordTones (settings.patterns[(size_t) (slot - patternFlag)].chord));
        }

    private:
        static int clampSlot (int n) noexcept { return n < 0 ? 0 : (n >= numPatternSlots ? numPatternSlots - 1 : n); }

        SmartSettings settings;
        ScaleMapper mapper;
    };
}
