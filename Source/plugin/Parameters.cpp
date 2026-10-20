#include "Parameters.h"

namespace chordism::params
{
    namespace
    {
        juce::ParameterID id (const char* name) { return { name, 1 }; }

        juce::StringArray keyNames()
        {
            juce::StringArray a;
            for (int k = 0; k < numKeys; ++k)
                a.add (keyName (k));
            return a;
        }

        juce::StringArray scaleNames()
        {
            juce::StringArray a;
            for (int s = 0; s < numScales; ++s)
                a.add (scaleInfo ((Scale) s).name);
            return a;
        }

        juce::StringArray groupNames()
        {
            juce::StringArray a;
            for (int g = 0; g < numChordGroups; ++g)
                a.add (chordGroupInfo ((ChordGroup) g).name);
            return a;
        }

        juce::ParameterID id (const juce::String& name) { return { name, 1 }; }

        juce::StringArray progressionNames()
        {
            juce::StringArray a;
            for (int p = 0; p < numProgressions; ++p)
                a.add (progressionInfo ((Progression) p).name);
            return a;
        }

        juce::StringArray degreeNames() { return { "I", "II", "III", "IV", "V", "VI", "VII" }; }

        juce::StringArray withSame (juce::StringArray a)
        {
            a.insert (0, "Same");
            return a;
        }

        juce::String numbered (const char* prefix, int slot, const char* suffix)
        {
            return juce::String (prefix) + juce::String (slot + 1) + suffix;
        }

        bool on (const std::atomic<float>* p) noexcept { return p->load() > 0.5f; }
        int whole (const std::atomic<float>* p) noexcept { return (int) std::lround (p->load()); }
        int choice (const std::atomic<float>* p, int count) noexcept { return juce::jlimit (0, count - 1, whole (p)); }
        double fraction (const std::atomic<float>* p) noexcept { return juce::jlimit (0.0, 1.0, (double) p->load() / 100.0); }

        template <class Name>
        juce::StringArray names (int count, Name name)
        {
            juce::StringArray a;
            for (int i = 0; i < count; ++i)
                a.add (name (i));
            return a;
        }

        std::unique_ptr<juce::AudioParameterFloat> percent (const juce::String& pid, const juce::String& name, float initial)
        {
            return std::make_unique<juce::AudioParameterFloat> (
                id (pid), name, juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), initial,
                juce::AudioParameterFloatAttributes().withLabel ("%"));
        }
    }

    juce::String customDegree (int slot) { return numbered ("customDegree", slot, ""); }
    juce::String shifterKind (int slot)  { return numbered ("shifter", slot, "Kind"); }
    juce::String shifterGroup (int slot) { return numbered ("shifter", slot, "Group"); }
    juce::String shifterKey (int slot)   { return numbered ("shifter", slot, "Key"); }
    juce::String shifterScale (int slot) { return numbered ("shifter", slot, "Scale"); }
    juce::String seqStep (int pattern, int step) { return "seq" + juce::String (pattern + 1) + "Step" + juce::String (step + 1); }
    juce::String seqSteps (int pattern)      { return "seq" + juce::String (pattern + 1) + "Steps"; }
    juce::String smartSeqSteps (int pattern) { return "smartSeq" + juce::String (pattern + 1) + "Steps"; }
    juce::String patternArp (int slot)   { return numbered ("pattern", slot, "Arp"); }
    juce::String patternRate (int slot)  { return numbered ("pattern", slot, "Rate"); }
    juce::String patternRange (int slot) { return numbered ("pattern", slot, "Range"); }
    juce::String patternChord (int slot) { return numbered ("pattern", slot, "Chord"); }
    juce::String patternGate (int slot)  { return numbered ("pattern", slot, "Gate"); }
    juce::String patternNotes (int slot) { return numbered ("pattern", slot, "Notes"); }
    juce::String patternRhythm (int slot) { return numbered ("pattern", slot, "Rhythm"); }

    bool isPresetParameter (const juce::String& paramId)
    {
        for (const char* excluded : { enable, testPattern, testShifter, testSmart, syncGroup })
            if (paramId == excluded)
                return false;
        return true;
    }

    juce::String stepsText (int mask)
    {
        juce::String t;
        for (int s = 0; s < numSeqSteps; ++s)
            t << (((mask >> s) & 1) != 0 ? "x" : ".");
        return t;
    }

    int stepsFromText (const juce::String& text)
    {
        const auto t = text.trim();
        if (t.containsOnly ("xX.-_o0 ") && t.containsAnyOf ("xX.-_"))
        {
            int mask = 0, s = 0;
            for (auto c : t)
            {
                if (c == ' ')
                    continue;
                if (s < numSeqSteps && (c == 'x' || c == 'X'))
                    mask |= 1 << s;
                ++s;
            }
            return mask;
        }
        return juce::jlimit (0, 0xffff, t.getIntValue());
    }

    juce::String liveNoteName (int note)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 2);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<AudioParameterBool> (id (enable), "Enable", true));
        layout.add (std::make_unique<AudioParameterChoice> (id (key), "Key", keyNames(), 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (scale), "Scale", scaleNames(), 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (chordGroup), "Chord Group", groupNames(), 0));
        layout.add (std::make_unique<AudioParameterInt> (
            id (split), "Split", 0, 127, InputRouter::defaultSplit,
            AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return liveNoteName (v); })));
        layout.add (std::make_unique<AudioParameterInt> (id (chordOctave), "Chord Octave", -3, 3, 0));
        layout.add (std::make_unique<AudioParameterBool> (id (muteChords), "Mute CG", false));
        layout.add (std::make_unique<AudioParameterBool> (id (muteScale), "Mute SS", false));
        layout.add (std::make_unique<AudioParameterBool> (id (testPattern), "Test pattern", false));

        // The virtual port's three parameters ("port", "portCompensate", "portTrimMs") were
        // removed with the AU builds on 2026-10-20; a saved state's values for them are ignored.

        // Phase 2: Progression, its custom order, and the shifters. Appended, never reordered.
        layout.add (std::make_unique<AudioParameterChoice> (id (progression), "Progression", progressionNames(), 0));
        for (int n = 0; n < numChordSlots; ++n)
            layout.add (std::make_unique<AudioParameterChoice> (id (customDegree (n)), "Custom " + String (n + 1), degreeNames(), n));

        layout.add (std::make_unique<AudioParameterChoice> (id (shifterMode), "Shifter mode", StringArray { "Standard", "Toggle" }, 0));
        const auto targets = defaultShifterTargets();
        for (int n = 0; n < numShifters; ++n)
        {
            const auto& t = targets[(size_t) n];
            const String name = "Shifter " + String (n + 1);
            layout.add (std::make_unique<AudioParameterChoice> (id (shifterKind (n)), name + " type",
                                                                StringArray { "Chord Group", "Key/Scale" }, (int) t.kind));
            layout.add (std::make_unique<AudioParameterChoice> (id (shifterGroup (n)), name + " group", groupNames(), (int) t.group));
            layout.add (std::make_unique<AudioParameterChoice> (id (shifterKey (n)), name + " key", withSame (keyNames()), t.key + 1));
            layout.add (std::make_unique<AudioParameterChoice> (id (shifterScale (n)), name + " scale", withSame (scaleNames()), t.scale + 1));
        }

        // The test pattern holds this shifter through every odd bar (TestPattern.h).
        layout.add (std::make_unique<AudioParameterChoice> (id (testShifter), "Test shifter",
                                                            StringArray { "Off", "1", "2", "3", "4", "5" }, 0));

        // Phase 3: the chord-zone effects (Source/core/engine/ChordFx.h). Appended, never
        // reordered; the choice lists come from the engine's enums, which only ever append.
        const ChordFxSettings fx;
        layout.add (std::make_unique<AudioParameterChoice> (id (stack), "Stack",
                                                            names (numStackModes, [] (int i) { return stackModeName ((StackMode) i); }), 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (strum), "Strum",
                                                            names (numStrumModes, [] (int i) { return strumModeName ((StrumMode) i); }), 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (strumRate), "Strum rate", names (numStrumRates, strumRateName), fx.strumRate));
        layout.add (std::make_unique<AudioParameterFloat> (
            id (strumMs), "Strum time", NormalisableRange<float> (0.0f, 200.0f, 0.1f), (float) fx.strumMs,
            AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<AudioParameterChoice> (id (arp), "Arp",
                                                            names (numArpPatterns, [] (int i) { return arpPatternName ((ArpPattern) i); }), 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (arpRate), "Arp rate", names (numPatternRates, patternRateName), fx.arpRate));
        layout.add (std::make_unique<AudioParameterInt> (id (arpRange), "Arp range", minArpRange, maxArpRange, fx.arpRange));
        layout.add (percent (arpGate, "Arp gate", (float) (fx.arpGate * 100.0)));
        layout.add (std::make_unique<AudioParameterBool> (id (repeat), "Repeat", false));
        layout.add (std::make_unique<AudioParameterChoice> (id (repeatRate), "Repeat rate", names (numPatternRates, patternRateName), fx.repeat.rate));
        layout.add (percent (repeatGate, "Repeat gate", (float) (fx.repeat.gate * 100.0)));
        layout.add (std::make_unique<AudioParameterBool> (id (seq), "Step seq", false));
        layout.add (std::make_unique<AudioParameterChoice> (id (seqPattern), "Seq pattern", StringArray { "1", "2", "3", "4" }, 0));
        layout.add (percent (seqSwing, "Seq swing", 0.0f));
        layout.add (percent (seqGate, "Seq gate", (float) (fx.seq.gate * 100.0)));
        layout.add (std::make_unique<AudioParameterBool> (id (seqSustain), "Seq sustain", false));
        layout.add (std::make_unique<AudioParameterChoice> (id (velocity), "Velocity",
                                                            names (numVelocityCurves, [] (int i) { return velocityCurveName ((VelocityCurve) i); }), 0));
        layout.add (std::make_unique<AudioParameterInt> (id (velocityPeak), "Velocity peak", 1, 127, fx.velocity.peak));
        layout.add (percent (velocityRange, "Velocity range", (float) (fx.velocity.range * 100.0)));
        layout.add (std::make_unique<AudioParameterChoice> (id (hold), "Hold",
                                                            names (numHoldModes, [] (int i) { return holdModeName ((HoldMode) i); }), 0));

        // The four sequencer patterns, 16 steps each as a mask (bit s = step s + 1), starting as
        // the factory patterns. (Phase 3's 64 step bools; replaced in Phase 4, see Parameters.h.)
        auto stepsParam = [] (const String& pid, const String& name, int initial)
        {
            return std::make_unique<AudioParameterInt> (
                id (pid), name, 0, 0xffff, initial,
                AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return stepsText (v); })
                                             .withValueFromStringFunction ([] (const String& t) { return stepsFromText (t); }));
        };
        const auto patterns = defaultSeqPatterns();
        for (int p = 0; p < numSeqPatterns; ++p)
            layout.add (stepsParam (seqSteps (p), "Seq " + String (p + 1) + " steps", patterns[(size_t) p]));

        // Phase 4: the Smart Scale zone (Source/core/engine/ScaleMapper.h, PatternEngine.h).
        // Appended, never reordered; choice lists come from the engine's enums.
        const SmartSettings ss;
        const StringArray slotNames { "1", "2", "3", "4", "5" };
        layout.add (std::make_unique<AudioParameterChoice> (id (mapping), "Mapping",
                                                            names (numMappings, [] (int i) { return mappingName ((Mapping) i); }),
                                                            (int) ss.map.mapping));
        layout.add (std::make_unique<AudioParameterBool> (id (inversions), "Inversions", ss.map.inversions));
        layout.add (std::make_unique<AudioParameterBool> (id (noteRepeat), "Note repeat", ss.noteRepeat));
        layout.add (std::make_unique<AudioParameterChoice> (id (patternSlot), "Pattern slot", slotNames, ss.patternSlot));
        layout.add (std::make_unique<AudioParameterInt> (id (smartOctave), "SS octave", -3, 3, ss.map.octave));
        layout.add (std::make_unique<AudioParameterChoice> (id (smartStack), "SS stack",
                                                            names (numStackModes, [] (int i) { return stackModeName ((StackMode) i); }), 0));
        layout.add (std::make_unique<AudioParameterBool> (id (smartSeq), "SS step seq", false));
        layout.add (std::make_unique<AudioParameterChoice> (id (smartSeqPattern), "SS seq pattern", StringArray { "1", "2", "3", "4" }, 0));
        layout.add (percent (smartSeqSwing, "SS seq swing", 0.0f));
        layout.add (percent (smartSeqGate, "SS seq gate", (float) (ss.seq.gate * 100.0)));
        layout.add (std::make_unique<AudioParameterBool> (id (smartSeqSustain), "SS seq sustain", false));
        layout.add (std::make_unique<AudioParameterChoice> (id (smartVelocity), "SS velocity",
                                                            names (numVelocityCurves, [] (int i) { return velocityCurveName ((VelocityCurve) i); }), 0));
        layout.add (std::make_unique<AudioParameterInt> (id (smartVelocityPeak), "SS velocity peak", 1, 127, ss.velocity.peak));
        layout.add (percent (smartVelocityRange, "SS velocity range", (float) (ss.velocity.range * 100.0)));
        layout.add (std::make_unique<AudioParameterChoice> (id (smartHold), "SS hold",
                                                            names (numHoldModes, [] (int i) { return holdModeName ((HoldMode) i); }), 0));
        for (int p = 0; p < numSeqPatterns; ++p)
            layout.add (stepsParam (smartSeqSteps (p), "SS seq " + String (p + 1) + " steps", ss.seq.patterns[(size_t) p]));

        // The five pattern slots. The arp list has no Off: a pattern key always plays. Its last
        // choice (Phase 6, appended) is Melody.
        for (int n = 0; n < numPatternSlots; ++n)
        {
            const auto& slot = ss.patterns[(size_t) n];
            const String name = "Pattern " + String (n + 1);
            auto arps = names (numArpPatterns - 1, [] (int i) { return arpPatternName ((ArpPattern) (i + 1)); });
            arps.add ("Melody");
            layout.add (std::make_unique<AudioParameterChoice> (id (patternArp (n)), name + " arp", arps,
                                                                slot.melody ? numArpPatterns - 1 : (int) slot.pattern - 1));
            layout.add (std::make_unique<AudioParameterChoice> (id (patternRate (n)), name + " rate", names (numPatternRates, patternRateName), slot.rate));
            layout.add (std::make_unique<AudioParameterInt> (id (patternRange (n)), name + " range", minArpRange, maxArpRange, slot.range));
            layout.add (std::make_unique<AudioParameterChoice> (id (patternChord (n)), name + " chord",
                                                                names (numArpChords, [] (int i) { return arpChordName ((ArpChord) i); }),
                                                                (int) slot.chord));
            layout.add (percent (patternGate (n), name + " gate", (float) (slot.gate * 100.0)));
        }

        // Test smart: the test pattern also plays the Smart Scale zone (TestPattern.h).
        layout.add (std::make_unique<AudioParameterChoice> (id (testSmart), "Test smart",
                                                            StringArray { "Off", "Tones", "Pattern 1", "Pattern 2", "Pattern 3", "Pattern 4", "Pattern 5",
                                                                          "Tones early" }, 0));

        // Phase 6: Melody mode (Source/core/engine/Melody.h). Appended, never reordered.
        for (int n = 0; n < numPatternSlots; ++n)
        {
            const auto& slot = ss.patterns[(size_t) n];
            const String name = "Pattern " + String (n + 1);
            layout.add (std::make_unique<AudioParameterInt> (id (patternNotes (n)), name + " notes", minMelodyNotes, maxMelodyNotes, slot.notes));
            layout.add (std::make_unique<AudioParameterChoice> (id (patternRhythm (n)), name + " rhythm",
                                                                names (numMelodyRhythms, [] (int i) { return melodyRhythmName ((MelodyRhythm) i); }),
                                                                (int) slot.rhythm));
        }
        layout.add (percent (melodySwing, "Melody swing", (float) (ss.melodySwing * 100.0)));
        layout.add (std::make_unique<AudioParameterInt> (id (melodySeed), "Melody seed", minMelodySeed, maxMelodySeed, (int) ss.melodySeed));

        // Phase 7: Instance Sync (SharedSession.h), look-ahead (ChordTimeline.h), voice leading
        // (VoiceLeading.h). Appended, never reordered.
        layout.add (std::make_unique<AudioParameterChoice> (id (syncGroup), "Sync group",
                                                            names (numSyncGroups + 1, syncGroupName), 0));
        layout.add (std::make_unique<AudioParameterChoice> (id (lookAhead), "Look-ahead", names (numLookAheads, lookAheadName),
                                                            defaultLookAhead));
        layout.add (std::make_unique<AudioParameterChoice> (id (voiceLeading), "Voice leading",
                                                            names (numVoiceLeadingModes, [] (int i) { return voiceLeadingName ((VoiceLeadingMode) i); }), 0));
        return layout;
    }

    Snapshot::Snapshot (juce::AudioProcessorValueTreeState& state)
        : enable (state.getRawParameterValue (params::enable)),
          keyParam (state.getRawParameterValue (params::key)),
          scale (state.getRawParameterValue (params::scale)),
          group (state.getRawParameterValue (params::chordGroup)),
          split (state.getRawParameterValue (params::split)),
          chordOctave (state.getRawParameterValue (params::chordOctave)),
          muteChords (state.getRawParameterValue (params::muteChords)),
          muteScale (state.getRawParameterValue (params::muteScale)),
          testPattern (state.getRawParameterValue (params::testPattern)),
          progression (state.getRawParameterValue (params::progression)),
          shifterMode (state.getRawParameterValue (params::shifterMode)),
          testShifter (state.getRawParameterValue (params::testShifter)),
          fxStack (state.getRawParameterValue (params::stack)),
          fxStrum (state.getRawParameterValue (params::strum)),
          fxStrumRate (state.getRawParameterValue (params::strumRate)),
          fxStrumMs (state.getRawParameterValue (params::strumMs)),
          fxArp (state.getRawParameterValue (params::arp)),
          fxArpRate (state.getRawParameterValue (params::arpRate)),
          fxArpRange (state.getRawParameterValue (params::arpRange)),
          fxArpGate (state.getRawParameterValue (params::arpGate)),
          fxRepeat (state.getRawParameterValue (params::repeat)),
          fxRepeatRate (state.getRawParameterValue (params::repeatRate)),
          fxRepeatGate (state.getRawParameterValue (params::repeatGate)),
          fxSeq (state.getRawParameterValue (params::seq)),
          fxSeqPattern (state.getRawParameterValue (params::seqPattern)),
          fxSeqSwing (state.getRawParameterValue (params::seqSwing)),
          fxSeqGate (state.getRawParameterValue (params::seqGate)),
          fxSeqSustain (state.getRawParameterValue (params::seqSustain)),
          fxVelocity (state.getRawParameterValue (params::velocity)),
          fxVelocityPeak (state.getRawParameterValue (params::velocityPeak)),
          fxVelocityRange (state.getRawParameterValue (params::velocityRange)),
          fxHold (state.getRawParameterValue (params::hold)),
          ssMapping (state.getRawParameterValue (params::mapping)),
          ssInversions (state.getRawParameterValue (params::inversions)),
          ssNoteRepeat (state.getRawParameterValue (params::noteRepeat)),
          ssPatternSlot (state.getRawParameterValue (params::patternSlot)),
          ssOctave (state.getRawParameterValue (params::smartOctave)),
          ssStack (state.getRawParameterValue (params::smartStack)),
          ssSeq (state.getRawParameterValue (params::smartSeq)),
          ssSeqPattern (state.getRawParameterValue (params::smartSeqPattern)),
          ssSeqSwing (state.getRawParameterValue (params::smartSeqSwing)),
          ssSeqGate (state.getRawParameterValue (params::smartSeqGate)),
          ssSeqSustain (state.getRawParameterValue (params::smartSeqSustain)),
          ssVelocity (state.getRawParameterValue (params::smartVelocity)),
          ssVelocityPeak (state.getRawParameterValue (params::smartVelocityPeak)),
          ssVelocityRange (state.getRawParameterValue (params::smartVelocityRange)),
          ssHold (state.getRawParameterValue (params::smartHold)),
          ssTest (state.getRawParameterValue (params::testSmart)),
          ssMelodySwing (state.getRawParameterValue (params::melodySwing)),
          ssMelodySeed (state.getRawParameterValue (params::melodySeed)),
          sync (state.getRawParameterValue (params::syncGroup)),
          ssLookAhead (state.getRawParameterValue (params::lookAhead)),
          fxVoiceLeading (state.getRawParameterValue (params::voiceLeading))
    {
        jassert (enable && keyParam && scale && group && split && chordOctave && muteChords && muteScale
                 && testPattern && progression && shifterMode && testShifter);

        for (int n = 0; n < numChordSlots; ++n)
        {
            customDegree[(size_t) n] = state.getRawParameterValue (params::customDegree (n));
            jassert (customDegree[(size_t) n] != nullptr);
        }
        for (int n = 0; n < numShifters; ++n)
        {
            shifterKind[(size_t) n] = state.getRawParameterValue (params::shifterKind (n));
            shifterGroup[(size_t) n] = state.getRawParameterValue (params::shifterGroup (n));
            shifterKey[(size_t) n] = state.getRawParameterValue (params::shifterKey (n));
            shifterScale[(size_t) n] = state.getRawParameterValue (params::shifterScale (n));
            jassert (shifterKind[(size_t) n] && shifterGroup[(size_t) n] && shifterKey[(size_t) n] && shifterScale[(size_t) n]);
        }

        jassert (fxStack && fxStrum && fxStrumRate && fxStrumMs && fxArp && fxArpRate && fxArpRange && fxArpGate && fxRepeat
                 && fxRepeatRate && fxRepeatGate && fxSeq && fxSeqPattern && fxSeqSwing && fxSeqGate && fxSeqSustain && fxVelocity
                 && fxVelocityPeak && fxVelocityRange && fxHold);
        for (int p = 0; p < numSeqPatterns; ++p)
        {
            fxSeqSteps[(size_t) p] = state.getRawParameterValue (params::seqSteps (p));
            ssSeqSteps[(size_t) p] = state.getRawParameterValue (params::smartSeqSteps (p));
            jassert (fxSeqSteps[(size_t) p] != nullptr && ssSeqSteps[(size_t) p] != nullptr);
        }

        jassert (ssMapping && ssInversions && ssNoteRepeat && ssPatternSlot && ssOctave && ssStack && ssSeq && ssSeqPattern
                 && ssSeqSwing && ssSeqGate && ssSeqSustain && ssVelocity && ssVelocityPeak && ssVelocityRange && ssHold && ssTest);
        for (int n = 0; n < numPatternSlots; ++n)
        {
            patArp[(size_t) n] = state.getRawParameterValue (params::patternArp (n));
            patRate[(size_t) n] = state.getRawParameterValue (params::patternRate (n));
            patRange[(size_t) n] = state.getRawParameterValue (params::patternRange (n));
            patChord[(size_t) n] = state.getRawParameterValue (params::patternChord (n));
            patGate[(size_t) n] = state.getRawParameterValue (params::patternGate (n));
            jassert (patArp[(size_t) n] && patRate[(size_t) n] && patRange[(size_t) n] && patChord[(size_t) n] && patGate[(size_t) n]);
            patNotes[(size_t) n] = state.getRawParameterValue (params::patternNotes (n));
            patRhythm[(size_t) n] = state.getRawParameterValue (params::patternRhythm (n));
            jassert (patNotes[(size_t) n] && patRhythm[(size_t) n]);
        }
        jassert (ssMelodySwing && ssMelodySeed);
        jassert (sync && ssLookAhead && fxVoiceLeading);
    }

    ChordFxSettings Snapshot::chordFx() const noexcept
    {
        ChordFxSettings fx;
        fx.stack = (StackMode) choice (fxStack, numStackModes);
        fx.strum = (StrumMode) choice (fxStrum, numStrumModes);
        fx.strumRate = choice (fxStrumRate, numStrumRates);
        fx.strumMs = juce::jlimit (0.0, 200.0, (double) fxStrumMs->load());
        fx.arp = (ArpPattern) choice (fxArp, numArpPatterns);
        fx.arpRate = choice (fxArpRate, numPatternRates);
        fx.arpRange = juce::jlimit (minArpRange, maxArpRange, whole (fxArpRange));
        fx.arpGate = fraction (fxArpGate);
        fx.repeat.on = on (fxRepeat);
        fx.repeat.rate = choice (fxRepeatRate, numPatternRates);
        fx.repeat.gate = fraction (fxRepeatGate);
        fx.seq.on = on (fxSeq);
        fx.seq.pattern = choice (fxSeqPattern, numSeqPatterns);
        fx.seq.swing = fraction (fxSeqSwing);
        fx.seq.gate = fraction (fxSeqGate);
        fx.seq.sustain = on (fxSeqSustain);
        for (int p = 0; p < numSeqPatterns; ++p)
            fx.seq.patterns[(size_t) p] = (uint16_t) juce::jlimit (0, 0xffff, whole (fxSeqSteps[(size_t) p]));
        fx.velocity.curve = (VelocityCurve) choice (fxVelocity, numVelocityCurves);
        fx.velocity.peak = juce::jlimit (1, 127, whole (fxVelocityPeak));
        fx.velocity.range = fraction (fxVelocityRange);
        fx.hold = (HoldMode) choice (fxHold, numHoldModes);
        fx.voiceLeading = (VoiceLeadingMode) choice (fxVoiceLeading, numVoiceLeadingModes);
        return fx;
    }

    SmartSettings Snapshot::smart() const noexcept
    {
        SmartSettings s;
        s.map.mapping = (Mapping) choice (ssMapping, numMappings);
        s.map.inversions = on (ssInversions);
        s.map.octave = juce::jlimit (-3, 3, whole (ssOctave));
        s.noteRepeat = on (ssNoteRepeat);
        s.patternSlot = choice (ssPatternSlot, numPatternSlots);
        s.stack = (StackMode) choice (ssStack, numStackModes);
        s.seq.on = on (ssSeq);
        s.seq.pattern = choice (ssSeqPattern, numSeqPatterns);
        s.seq.swing = fraction (ssSeqSwing);
        s.seq.gate = fraction (ssSeqGate);
        s.seq.sustain = on (ssSeqSustain);
        for (int p = 0; p < numSeqPatterns; ++p)
            s.seq.patterns[(size_t) p] = (uint16_t) juce::jlimit (0, 0xffff, whole (ssSeqSteps[(size_t) p]));
        s.velocity.curve = (VelocityCurve) choice (ssVelocity, numVelocityCurves);
        s.velocity.peak = juce::jlimit (1, 127, whole (ssVelocityPeak));
        s.velocity.range = fraction (ssVelocityRange);
        s.hold = (HoldMode) choice (ssHold, numHoldModes);
        for (int n = 0; n < numPatternSlots; ++n)
        {
            auto& slot = s.patterns[(size_t) n];
            const int arp = choice (patArp[(size_t) n], numArpPatterns);   // the last choice is Melody
            slot.melody = arp == numArpPatterns - 1;
            slot.pattern = slot.melody ? ArpPattern::LinearUp : (ArpPattern) (arp + 1);
            slot.rate = choice (patRate[(size_t) n], numPatternRates);
            slot.range = juce::jlimit (minArpRange, maxArpRange, whole (patRange[(size_t) n]));
            slot.chord = (ArpChord) choice (patChord[(size_t) n], numArpChords);
            slot.gate = fraction (patGate[(size_t) n]);
            slot.notes = juce::jlimit (minMelodyNotes, maxMelodyNotes, whole (patNotes[(size_t) n]));
            slot.rhythm = (MelodyRhythm) choice (patRhythm[(size_t) n], numMelodyRhythms);
        }
        s.melodySwing = fraction (ssMelodySwing);
        s.melodySeed = (uint32_t) juce::jlimit (minMelodySeed, maxMelodySeed, whole (ssMelodySeed));
        return s;
    }

    ChordGroup Snapshot::chordGroup() const noexcept
    {
        return (ChordGroup) juce::jlimit (0, numChordGroups - 1, whole (group));
    }

    Scale Snapshot::currentScale() const noexcept
    {
        return (Scale) juce::jlimit (0, numScales - 1, whole (scale));
    }

    int Snapshot::key() const noexcept { return juce::jlimit (0, numKeys - 1, whole (keyParam)); }

    int Snapshot::syncGroup() const noexcept { return choice (sync, numSyncGroups + 1); }

    EngineSettings Snapshot::engineSettings() const noexcept
    {
        EngineSettings s;
        s.enabled = on (enable);
        s.key = key();
        s.scale = currentScale();
        s.group = chordGroup();
        s.split = juce::jlimit (0, 127, whole (split));
        s.chordOctave = juce::jlimit (-3, 3, whole (chordOctave));
        s.muteChords = on (muteChords);
        s.muteScale = on (muteScale);
        s.testPattern = on (testPattern);

        s.progression = (Progression) juce::jlimit (0, numProgressions - 1, whole (progression));
        for (int n = 0; n < numChordSlots; ++n)
            s.customOrder[(size_t) n] = (int8_t) juce::jlimit (0, numChordSlots - 1, whole (customDegree[(size_t) n]));
        s.shifterMode = whole (shifterMode) == 1 ? ShifterMode::Toggle : ShifterMode::Standard;
        for (int n = 0; n < numShifters; ++n)
        {
            auto& t = s.shifters[(size_t) n];
            t.kind = whole (shifterKind[(size_t) n]) == 1 ? ShifterKind::KeyScale : ShifterKind::ChordGroup;
            t.group = (ChordGroup) juce::jlimit (0, numChordGroups - 1, whole (shifterGroup[(size_t) n]));
            t.key = juce::jlimit (0, numKeys, whole (shifterKey[(size_t) n])) - 1;
            t.scale = juce::jlimit (0, numScales, whole (shifterScale[(size_t) n])) - 1;
        }
        s.testShifter = juce::jlimit (0, numShifters, whole (testShifter)) - 1;
        s.fx = chordFx();
        s.smart = smart();
        s.testSmart = choice (ssTest, TestPattern::numSmartTests);
        s.syncGroup = syncGroup();
        s.lookAhead = choice (ssLookAhead, numLookAheads);
        return s;
    }
}
