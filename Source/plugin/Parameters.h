// Host parameters (APVTS). Every setting a test needs is one of these, because LiveBridge
// reaches the plug-in only through Live's device parameters (PLAN.md §8).
//
// IDs, names and choice lists are saved in Live sets and automation: never rename or reorder
// them, only add new ones.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/engine/ChordismEngine.h"
#include "../core/engine/PortTiming.h"

namespace chordism::params
{
    inline constexpr const char* enable       = "enable";
    inline constexpr const char* key          = "key";
    inline constexpr const char* scale        = "scale";
    inline constexpr const char* chordGroup   = "chordGroup";
    inline constexpr const char* split        = "split";
    inline constexpr const char* chordOctave  = "chordOctave";
    inline constexpr const char* muteChords   = "muteChords";
    inline constexpr const char* muteScale    = "muteScale";
    inline constexpr const char* testPattern  = "testPattern";
    inline constexpr const char* port         = "port";
    inline constexpr const char* portCompensate = "portCompensate";
    inline constexpr const char* portTrimMs   = "portTrimMs";

    // Phase 2 (appended).
    inline constexpr const char* progression  = "progression";
    inline constexpr const char* shifterMode  = "shifterMode";
    inline constexpr const char* testShifter  = "testShifter";
    juce::String customDegree (int slot);     // "customDegree1".."customDegree7"
    juce::String shifterKind (int slot);      // "shifter1Kind".."shifter5Kind"
    juce::String shifterGroup (int slot);     // "shifter1Group"…
    juce::String shifterKey (int slot);       // "shifter1Key"… (0 = Same, then the keys)
    juce::String shifterScale (int slot);     // "shifter1Scale"… (0 = Same, then the scales)

    // Phase 3 (appended): the chord-zone effects.
    inline constexpr const char* stack        = "stack";
    inline constexpr const char* strum        = "strum";
    inline constexpr const char* strumRate    = "strumRate";
    inline constexpr const char* strumMs      = "strumMs";
    inline constexpr const char* arp          = "arp";
    inline constexpr const char* arpRate      = "arpRate";
    inline constexpr const char* arpRange     = "arpRange";
    inline constexpr const char* arpGate      = "arpGate";
    inline constexpr const char* repeat       = "repeat";
    inline constexpr const char* repeatRate   = "repeatRate";
    inline constexpr const char* repeatGate   = "repeatGate";
    inline constexpr const char* seq          = "seq";
    inline constexpr const char* seqPattern   = "seqPattern";
    inline constexpr const char* seqSwing     = "seqSwing";
    inline constexpr const char* seqGate      = "seqGate";
    inline constexpr const char* seqSustain   = "seqSustain";
    inline constexpr const char* velocity     = "velocity";
    inline constexpr const char* velocityPeak = "velocityPeak";
    inline constexpr const char* velocityRange = "velocityRange";
    inline constexpr const char* hold         = "hold";

    // Phase 4 (2026-10-19): the 64 "seqNStepM" bools of Phase 3 were replaced by one 16-bit
    // mask per pattern (bit s = step s + 1), so Live's panel (at most 128 parameters, LiveBridge)
    // has room for the Smart Scale zone. Saved states are migrated (ChordismProcessor).
    juce::String seqStep (int pattern, int step);   // the old IDs "seq1Step1".."seq4Step16", for migration
    juce::String seqSteps (int pattern);            // "seq1Steps".."seq4Steps"

    // Phase 4 (appended): the Smart Scale zone and its pattern slots.
    inline constexpr const char* mapping       = "mapping";
    inline constexpr const char* inversions    = "inversions";
    inline constexpr const char* noteRepeat    = "noteRepeat";
    inline constexpr const char* patternSlot   = "patternSlot";
    inline constexpr const char* smartOctave   = "smartOctave";
    inline constexpr const char* smartStack    = "smartStack";
    inline constexpr const char* smartSeq      = "smartSeq";
    inline constexpr const char* smartSeqPattern = "smartSeqPattern";
    inline constexpr const char* smartSeqSwing = "smartSeqSwing";
    inline constexpr const char* smartSeqGate  = "smartSeqGate";
    inline constexpr const char* smartSeqSustain = "smartSeqSustain";
    inline constexpr const char* smartVelocity = "smartVelocity";
    inline constexpr const char* smartVelocityPeak = "smartVelocityPeak";
    inline constexpr const char* smartVelocityRange = "smartVelocityRange";
    inline constexpr const char* smartHold     = "smartHold";
    inline constexpr const char* testSmart     = "testSmart";
    juce::String smartSeqSteps (int pattern);       // "smartSeq1Steps".."smartSeq4Steps"
    juce::String patternArp (int slot);             // "pattern1Arp".."pattern5Arp"
    juce::String patternRate (int slot);            // "pattern1Rate"…
    juce::String patternRange (int slot);           // "pattern1Range"…
    juce::String patternChord (int slot);           // "pattern1Chord"…
    juce::String patternGate (int slot);            // "pattern1Gate"…

    // Phase 6 (appended): Melody mode. "Pattern n arp" gained a last choice, "Melody"; Notes
    // per bar and Rhythm are per slot, Swing and Seed shared by the five slots.
    juce::String patternNotes (int slot);           // "pattern1Notes"…
    juce::String patternRhythm (int slot);          // "pattern1Rhythm"…
    inline constexpr const char* melodySwing   = "melodySwing";
    inline constexpr const char* melodySeed    = "melodySeed";

    // Parameters a preset doesn't touch: routing and the test rig (PresetManager).
    bool isPresetParameter (const juce::String& paramId);

    // A sequencer pattern as Live shows it: "x" for an active step, "." for a rest, step 1 first.
    juce::String stepsText (int mask);
    int stepsFromText (const juce::String& text);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Lock-free reads of the current values, for the audio thread.
    class Snapshot
    {
    public:
        explicit Snapshot (juce::AudioProcessorValueTreeState& state);

        EngineSettings engineSettings() const noexcept;
        ChordGroup chordGroup() const noexcept;
        Scale currentScale() const noexcept;
        ChordFxSettings chordFx() const noexcept;
        SmartSettings smart() const noexcept;
        bool portEnabled() const noexcept { return port->load() > 0.5f; }
        bool portCompensation() const noexcept { return portCompensate->load() > 0.5f; }
        double portTrim() const noexcept { return (double) portTrimMs->load(); }

    private:
        std::atomic<float>* enable;
        std::atomic<float>* key;
        std::atomic<float>* scale;
        std::atomic<float>* group;
        std::atomic<float>* split;
        std::atomic<float>* chordOctave;
        std::atomic<float>* muteChords;
        std::atomic<float>* muteScale;
        std::atomic<float>* testPattern;
        std::atomic<float>* port;
        std::atomic<float>* portCompensate;
        std::atomic<float>* portTrimMs;
        std::atomic<float>* progression;
        std::atomic<float>* shifterMode;
        std::atomic<float>* testShifter;
        std::array<std::atomic<float>*, numChordSlots> customDegree {};
        std::array<std::atomic<float>*, numShifters> shifterKind {}, shifterGroup {}, shifterKey {}, shifterScale {};

        std::atomic<float>* fxStack;
        std::atomic<float>* fxStrum;
        std::atomic<float>* fxStrumRate;
        std::atomic<float>* fxStrumMs;
        std::atomic<float>* fxArp;
        std::atomic<float>* fxArpRate;
        std::atomic<float>* fxArpRange;
        std::atomic<float>* fxArpGate;
        std::atomic<float>* fxRepeat;
        std::atomic<float>* fxRepeatRate;
        std::atomic<float>* fxRepeatGate;
        std::atomic<float>* fxSeq;
        std::atomic<float>* fxSeqPattern;
        std::atomic<float>* fxSeqSwing;
        std::atomic<float>* fxSeqGate;
        std::atomic<float>* fxSeqSustain;
        std::atomic<float>* fxVelocity;
        std::atomic<float>* fxVelocityPeak;
        std::atomic<float>* fxVelocityRange;
        std::atomic<float>* fxHold;
        std::array<std::atomic<float>*, numSeqPatterns> fxSeqSteps {};

        std::atomic<float>* ssMapping;
        std::atomic<float>* ssInversions;
        std::atomic<float>* ssNoteRepeat;
        std::atomic<float>* ssPatternSlot;
        std::atomic<float>* ssOctave;
        std::atomic<float>* ssStack;
        std::atomic<float>* ssSeq;
        std::atomic<float>* ssSeqPattern;
        std::atomic<float>* ssSeqSwing;
        std::atomic<float>* ssSeqGate;
        std::atomic<float>* ssSeqSustain;
        std::atomic<float>* ssVelocity;
        std::atomic<float>* ssVelocityPeak;
        std::atomic<float>* ssVelocityRange;
        std::atomic<float>* ssHold;
        std::atomic<float>* ssTest;
        std::array<std::atomic<float>*, numSeqPatterns> ssSeqSteps {};
        std::array<std::atomic<float>*, numPatternSlots> patArp {}, patRate {}, patRange {}, patChord {}, patGate {};
        std::array<std::atomic<float>*, numPatternSlots> patNotes {}, patRhythm {};
        std::atomic<float>* ssMelodySwing;
        std::atomic<float>* ssMelodySeed;
    };

    // MIDI note name as Live writes it (60 = C3).
    juce::String liveNoteName (int note);
}
