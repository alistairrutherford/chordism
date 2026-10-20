#include "FactoryPresets.h"
#include "Parameters.h"

namespace chordism
{
    namespace
    {
        using V = std::vector<std::pair<juce::String, float>>;

        float f (Scale s) { return (float) (int) s; }
        float f (ChordGroup g) { return (float) (int) g; }
        float f (Progression p) { return (float) (int) p; }
        float f (StackMode m) { return (float) (int) m; }
        float f (StrumMode m) { return (float) (int) m; }
        float f (ArpPattern p) { return (float) (int) p; }
        float f (VelocityCurve c) { return (float) (int) c; }
        float f (HoldMode m) { return (float) (int) m; }
        float f (Mapping m) { return (float) (int) m; }
        float f (MelodyRhythm r) { return (float) (int) r; }
        float f (ArpChord c) { return (float) (int) c; }

        constexpr float melodyChoice = (float) (numArpPatterns - 1);

        // Strum and pattern rate indices (NoteValues.h).
        constexpr float strum1_64 = 2.0f, strum1_32 = 4.0f, strum1_16T = 5.0f;
        constexpr float rate1_8 = 3.0f, rate1_8T = 4.0f, rate1_16 = 5.0f, rate1_16T = 6.0f;

        // Pattern slot n (0-based) as a melody.
        V melody (int n, int notes, MelodyRhythm rhythm, int range, float gate)
        {
            return { { params::patternArp (n), melodyChoice }, { params::patternNotes (n), (float) notes },
                     { params::patternRhythm (n), f (rhythm) }, { params::patternRange (n), (float) range },
                     { params::patternGate (n), gate } };
        }

        V operator+ (V a, const V& b)
        {
            a.insert (a.end(), b.begin(), b.end());
            return a;
        }

        std::vector<FactoryPreset> make()
        {
            using namespace params;
            std::vector<FactoryPreset> p;
            p.push_back ({ "Init", "Init", {} });

            // Genre.
            p.push_back ({ "Genre", "Pop Anthem", {
                { key, 0 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Pop) },
                { stack, f (StackMode::PlusOne) }, { strum, f (StrumMode::Up) }, { strumRate, strum1_64 } } });
            p.push_back ({ "Genre", "Neo-Soul Keys", V {
                { key, 3 }, { scale, f (Scale::Dorian) }, { chordGroup, f (ChordGroup::NeoSoul) }, { progression, f (Progression::TwoFiveOne) },
                { strum, f (StrumMode::Up) }, { strumRate, strum1_32 }, { velocity, f (VelocityCurve::Breakbeat) }, { velocityPeak, 110 },
                { velocityRange, 30 }, { mapping, f (Mapping::Dynamic) } } + melody (0, 6, MelodyRhythm::Syncopated, 2, 70) + V { { melodySwing, 35 } } });
            p.push_back ({ "Genre", "House Stabs", {
                { key, 9 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::Sevenths) }, { progression, f (Progression::Fifties) },
                { seq, 1 }, { seqPattern, 1 }, { seqGate, 40 }, { velocity, f (VelocityCurve::House) }, { velocityRange, 40 } } });
            p.push_back ({ "Genre", "Lo-Fi Sevenths", V {
                { key, 5 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Sevenths) }, { progression, f (Progression::Fifties) },
                { strum, f (StrumMode::Up) }, { strumRate, strum1_32 }, { hold, f (HoldMode::Bar) } }
                + melody (0, 6, MelodyRhythm::Straight, 1, 60) + V { { melodySwing, 40 }, { melodySeed, 12 } } });
            p.push_back ({ "Genre", "Trap Minor Arps", {
                { key, 1 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Descending) },
                { params::arp, f (ArpPattern::LinearUp) }, { arpRate, rate1_16T }, { arpRange, 2 }, { arpGate, 60 },
                { velocity, f (VelocityCurve::Dancehall) }, { velocityRange, 35 } } });
            p.push_back ({ "Genre", "Reggaeton Dembow", {
                { key, 7 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Canon) },
                { seq, 1 }, { seqPattern, 3 }, { seqGate, 60 }, { velocity, f (VelocityCurve::Moombahton) }, { velocityRange, 30 } } });
            p.push_back ({ "Genre", "Synthwave Drive", V {
                { key, 9 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::Add9) }, { progression, f (Progression::Fifties) },
                { params::arp, f (ArpPattern::ConvergeUp) }, { arpRate, rate1_16 }, { arpRange, 2 }, { arpGate, 70 } }
                + melody (1, 8, MelodyRhythm::Syncopated, 2, 80) });
            p.push_back ({ "Genre", "Jazz Two-Five", {
                { key, 10 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Ninths) }, { progression, f (Progression::TwoFiveOne) },
                { strum, f (StrumMode::Down) }, { strumRate, strum1_64 }, { mapping, f (Mapping::ChordTones) } } });
            p.push_back ({ "Genre", "Gospel Thirteenths", {
                { key, 8 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Thirteenths) }, { progression, f (Progression::CircleOfFifths) },
                { strum, f (StrumMode::Up) }, { strumRate, strum1_64 }, { velocity, f (VelocityCurve::Up) }, { velocityRange, 25 } } });
            p.push_back ({ "Genre", "Bossa Ninths", {
                { key, 2 }, { scale, f (Scale::Dorian) }, { chordGroup, f (ChordGroup::Ninths) }, { progression, f (Progression::TwoFiveOne) },
                { seq, 1 }, { seqPattern, 2 }, { seqSwing, 20 }, { seqGate, 70 } } });

            // Artist-style: by style, never by name.
            p.push_back ({ "Artist-style", "Cinematic Swells", {
                { key, 2 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::OpenTriads) }, { progression, f (Progression::Descending) },
                { stack, f (StackMode::PlusOne) }, { hold, f (HoldMode::Latch) }, { velocity, f (VelocityCurve::UpDown) }, { velocityRange, 50 } } });
            p.push_back ({ "Artist-style", "Ambient Pads", V {
                { key, 4 }, { scale, f (Scale::Lydian) }, { chordGroup, f (ChordGroup::Add9) }, { progression, f (Progression::Thirds) },
                { stack, f (StackMode::PlusOneAndTwo) }, { hold, f (HoldMode::Latch) }, { smartHold, f (HoldMode::Latch) } }
                + melody (0, 3, MelodyRhythm::Free, 2, 100) });
            p.push_back ({ "Artist-style", "Funk Rhythm Guitar", {
                { key, 4 }, { scale, f (Scale::Dorian) }, { chordGroup, f (ChordGroup::Ninths) }, { progression, f (Progression::Diatonic) },
                { seq, 1 }, { seqPattern, 3 }, { seqGate, 30 }, { velocity, f (VelocityCurve::Breakbeat) }, { velocityRange, 45 } } });
            p.push_back ({ "Artist-style", "Classic Rock Power", {
                { key, 4 }, { scale, f (Scale::Mixolydian) }, { chordGroup, f (ChordGroup::Power) }, { progression, f (Progression::Pop) },
                { stack, f (StackMode::PlusOne) }, { repeat, 1 }, { repeatRate, rate1_8 }, { repeatGate, 80 } } });
            p.push_back ({ "Artist-style", "Baroque Canon", {
                { key, 2 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Canon) },
                { params::arp, f (ArpPattern::BoomerangUp) }, { arpRate, rate1_16 }, { arpRange, 2 }, { arpGate, 80 } } });

            // Vibe.
            p.push_back ({ "Vibe", "Dreamy", V {
                { key, 5 }, { scale, f (Scale::Lydian) }, { chordGroup, f (ChordGroup::Add9) }, { progression, f (Progression::Thirds) },
                { strum, f (StrumMode::Up) }, { strumRate, strum1_16T } } + melody (0, 6, MelodyRhythm::Free, 2, 90) + V { { melodySeed, 3 } } });
            p.push_back ({ "Vibe", "Dark", {
                { key, 0 }, { scale, f (Scale::PhrygianDominant) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Descending) },
                { chordOctave, -1 }, { stack, f (StackMode::PlusOne) }, { velocity, f (VelocityCurve::Down) }, { velocityRange, 30 } } });
            p.push_back ({ "Vibe", "Uplifting", {
                { key, 7 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Sus4) }, { progression, f (Progression::Fifties) },
                { params::arp, f (ArpPattern::LinearUp) }, { arpRate, rate1_8T }, { arpRange, 2 } } });
            p.push_back ({ "Vibe", "Melancholy", V {
                { key, 11 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::Sevenths) }, { progression, f (Progression::Canon) },
                { strum, f (StrumMode::Down) }, { strumRate, strum1_32 } } + melody (0, 8, MelodyRhythm::Straight, 1, 75) + V { { melodySeed, 21 } } });
            p.push_back ({ "Vibe", "Tense", {
                { key, 10 }, { scale, f (Scale::HarmonicMinor) }, { chordGroup, f (ChordGroup::Sevenths) }, { progression, f (Progression::CircleOfFifths) },
                { repeat, 1 }, { repeatRate, rate1_16 }, { repeatGate, 35 }, { velocity, f (VelocityCurve::Up) }, { velocityRange, 60 } } });
            p.push_back ({ "Vibe", "Bright Melody", V {
                { key, 0 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Diatonic) },
                { patternChord (0), f (ArpChord::Sevenths) } } + melody (0, 10, MelodyRhythm::Syncopated, 2, 70)
                + V { { melodySwing, 20 }, { melodySeed, 42 } } });
            return p;
        }
    }

    const std::vector<FactoryPreset>& factoryPresets()
    {
        static const std::vector<FactoryPreset> presets = make();
        return presets;
    }
}
