// The MIDI engine: input router, the Chord Generator (Chord Groups, Chord Editor edits,
// Progressions, Shifters), the chord-zone effects (ChordFx.h: Stack, Strum, Arp, Repeat, Step
// Sequencer, Velocity Algorithm, Hold) on the Clock, the Smart Scale zone (ScaleMapper.h,
// PatternEngine.h: mappings, patterns, Note Repeat and its own effect chain), the test
// pattern, and note bookkeeping through VoiceTracker.
// Plain C++, no JUCE, no allocation; the plug-in is a thin adapter around it, so every
// behaviour here is unit-testable.
//
//   chord zone, white key  → the slot's chord: degree from the Progression, steps from the
//                            Chord Group or the degree's editor edits, Key, Scale, Chord Octave,
//                            then the effect chain (one performance per key, ChordFx.h)
//   chord zone, black key  → shifter (Shifters.h): swaps in its Chord Group or Key/Scale
//   Smart Scale zone       → white keys: the Mapping's tone for the current chord (the chord
//                            slot played last); black keys: pattern slots 1-5; then the Smart
//                            zone's chain (PatternEngine.h), one performance per key
//   Enable off             → input passes through unchanged
//
// Whenever the chords change while chord keys are held (a parameter, an edit, a shifter), each
// held key whose chord changed is released and replayed: shifting while a chord is held
// retriggers it (PLAN.md §4.2). An arp takes the new chord at its next step.
//
// A block is processed in the Clock's segments (split at loop wraps). Within a segment, input
// and queued events (of both zones) run in sample order; at one sample, queued note-offs come
// first, then input, then queued note-ons; between the zones, the chord zone's first.
#pragma once

#include "../theory/ChordEditor.h"
#include "ChordFx.h"
#include "Clock.h"
#include "InputRouter.h"
#include "MidiEvents.h"
#include "PatternEngine.h"
#include "Shifters.h"
#include "TestPattern.h"
#include "Transport.h"
#include "VoiceTracker.h"
#include <array>
#include <atomic>

namespace chordism
{
    struct EngineSettings
    {
        bool enabled = true;
        int key = 0;
        Scale scale = Scale::Major;
        ChordGroup group = ChordGroup::Triads;
        Progression progression = Progression::Diatonic;
        DegreeOrder customOrder = identityOrder();
        ShifterMode shifterMode = ShifterMode::Standard;
        ShifterTargets shifters = defaultShifterTargets();
        int split = InputRouter::defaultSplit;
        int chordOctave = 0;
        bool muteChords = false;
        bool muteScale = false;
        bool testPattern = false;
        int testShifter = -1;             // -1 = off, else the shifter slot the test pattern holds
        int outputChannel = 0;            // 0..15
        ChordFxSettings fx;               // Phase 3: the chord-zone effects
        SmartSettings smart;              // Phase 4: the Smart Scale zone
        int testSmart = 0;                // TestPattern::smartOff, smartTones, smartPattern + n
    };

    class ChordismEngine
    {
    public:
        ChordismEngine()
        {
            smart.setSibling (&performer);
            performer.setSibling (&smart);
            refreshChords (0, nullptr);
        }

        // Processes one block. `in` must be sorted by sample. Appends to `out` in sample order.
        // `edits` is the Chord Editor's current snapshot (see adoptEdits for when it applies).
        void process (const EngineSettings& s, const ChordEdits& edits, const Transport& t, const MidiEvent* in,
                      int numIn, int numSamples, BlockEvents& out) noexcept
        {
            numSamples = numSamples > 0 ? numSamples : 0;
            clock.beginBlock (t, numSamples);
            performer.setSegment (0);
            smart.setSegment (0);

            if (resetRequested.exchange (false))
                releaseAll (0, out);

            if (wasPlaying && ! t.playing)
                releaseAll (0, out);   // transport stop: all notes off (PLAN.md §5)
            if (! wasPlaying && t.playing)
            {
                performer.transportStarted();
                smart.transportStarted();
            }
            wasPlaying = t.playing;

            if (clock.jumpedAtStart())
            {
                performer.transportJump (clock.jumpDelta(), 0, out);
                smart.transportJump (clock.jumpDelta(), 0, out);
            }

            applySettings (s, edits, out);

            int i = 0;
            for (int seg = 0; seg < clock.numSegments(); ++seg)
            {
                const ClockSegment g = clock.segment (seg);
                const bool last = seg == clock.numSegments() - 1;
                performer.setSegment (seg);
                smart.setSegment (seg);
                if (seg > 0)
                {
                    performer.transportJump (clock.wrapDelta (seg), g.start, out);   // a loop wrap inside the block
                    smart.transportJump (clock.wrapDelta (seg), g.start, out);
                }

                Transport segmentTransport = t;
                segmentTransport.ppq = g.ppq;
                injected.clear();
                pattern.generate (s.enabled && s.testPattern, s.testShifter, segmentTransport, g.end - g.start, router,
                                  injected, g.start, s.testSmart);

                int j = 0, cursor = g.start, runs = 0;
                while (true)
                {
                    // The next key event of this segment: input before the test pattern's at one sample.
                    const bool haveIn = i < numIn && (last || clampSample (in[i].sample, numSamples) < g.end);
                    const bool haveInjected = j < injected.size();
                    const bool fromInput = haveIn && (! haveInjected || clampSample (in[i].sample, numSamples) <= injected[j].sample);
                    const MidiEvent* next = fromInput ? &in[i] : (haveInjected ? &injected[j] : nullptr);
                    const int nextSample = next != nullptr ? clampSample (next->sample, numSamples) : g.end;

                    int at = 0;
                    uint8_t order = 0;
                    ChordPerformer* due = runs < maxQueueRunsPerSegment
                                        ? peekNext (cursor, next != nullptr ? nextSample + 1 : g.end, at, order) : nullptr;
                    if (due != nullptr && (next == nullptr || at < nextSample || order < ChordPerformer::inputOrder))
                    {
                        ++runs;
                        cursor = at;
                        due->runNext (out);
                        continue;
                    }
                    if (next == nullptr)
                        break;
                    (fromInput ? i : j)++;
                    cursor = nextSample > cursor ? nextSample : cursor;
                    handle (*next, numSamples, out);
                }
            }
        }

        // Without Chord Editor edits.
        void process (const EngineSettings& s, const Transport& t, const MidiEvent* in, int numIn, int numSamples,
                      BlockEvents& out) noexcept
        {
            process (s, adopted, t, in, numIn, numSamples, out);
        }

        // All notes off now (bypass, transport stop, all-notes-off, destruction).
        void releaseAll (int sample, BlockEvents& out) noexcept
        {
            performer.killAll (sample, out);
            smart.killAll (sample, out);
            voices.releaseAll (sample, out);
            held = {};
            shifters.forgetHeld();            // a Standard shifter ends; a Toggle one stays on
            refreshChords (sample, nullptr);
        }

        // The next process() starts by releasing everything. For places where no MIDI can be
        // sent (releaseResources, prepareToPlay); callable from any thread.
        void requestReset() noexcept { resetRequested.store (true); }

        const VoiceTracker& getVoices() const noexcept { return voices; }
        const ChordSet& getChords() const noexcept { return chords; }
        const ChordContext& getChordContext() const noexcept { return context; }
        int getLastChordSlot() const noexcept { return lastChordSlot; }
        int getPatternKey() const noexcept { return pattern.getHeldKey(); }
        const ChordPerformer& getPerformer() const noexcept { return performer; }
        const ChordPerformer& getSmartPerformer() const noexcept { return smart; }
        const PatternEngine& getPatternEngine() const noexcept { return patterns; }
        const Clock& getClock() const noexcept { return clock; }
        int getActiveShifter() const noexcept { return activeShifter; }

        // For the editor: input key `note` is down; a performance of key `note` (chord, tone or
        // pattern) is playing, held or not.
        bool isKeyDown (int note) const noexcept { return note >= 0 && note < 128 && held[(size_t) note].down; }
        bool isPerforming (int note) const noexcept { return performer.isAlive (note) || smart.isAlive (note); }
        bool editsApply() const noexcept { return editsLive; }

        // Edits the engine dropped because the Chord Group changed: the count, and the version
        // of the last snapshot dropped. The plug-in clears its stored edits when it sees these
        // (callable from any thread).
        uint32_t getEditsDiscarded() const noexcept { return editsDiscarded.load (std::memory_order_acquire); }
        uint32_t getDiscardedVersion() const noexcept { return discardedVersion.load (std::memory_order_relaxed); }

    private:
        struct HeldKey
        {
            bool down = false;
            uint8_t velocity = 0;
            uint8_t channel = 0;
            RoutedKey routed;
            bool passThrough = false;     // pressed while Enable was off
        };

        bool isHeldChord (const HeldKey& h) const noexcept
        {
            return h.down && ! h.passThrough && h.routed.role == KeyRole::ChordSlot;
        }

        // The chord set and context for the current settings, edits and shifter.
        void effectiveChords (ChordSet& set, ChordContext& ctx) const noexcept
        {
            ChordGroup g = current.group;
            ctx = { current.key, current.scale, current.chordOctave };
            if (activeShifter >= 0)
            {
                const auto& target = current.shifters[(size_t) activeShifter];
                if (target.kind == ShifterKind::ChordGroup)
                {
                    g = target.group;
                }
                else
                {
                    if (target.key >= 0 && target.key < numKeys)
                        ctx.key = target.key;
                    if (target.scale >= 0 && target.scale < numScales)
                        ctx.scale = (Scale) target.scale;
                }
            }
            // Edits belong to the Chord Group parameter's group; a group shifter to another
            // group plays that group's own shapes.
            const bool useEdits = editsLive && g == current.group;
            set = makeChords (g, ctx.scale, progressionDegrees (current.progression, current.customOrder),
                              useEdits ? &adopted : nullptr);
        }

        // Recomputes the chords, marking in `changed` the slots whose chord changed. With `out`,
        // every performance whose chord changed is replayed at `sample` (ChordPerformer::retune).
        void refreshChords (int sample, BlockEvents* out, std::array<bool, numChordSlots>* changedOut = nullptr) noexcept
        {
            activeShifter = shifters.active (current.shifterMode);
            ChordSet next;
            ChordContext ctx;
            effectiveChords (next, ctx);

            const bool ctxChanged = ctx.key != context.key || ctx.scale != context.scale || ctx.outputOctave != context.outputOctave;
            std::array<bool, numChordSlots> changed {};
            bool any = false;
            for (size_t n = 0; n < changed.size(); ++n)
            {
                changed[n] = ctxChanged || ! (next[n] == chords[n]);
                any |= changed[n];
            }
            chords = next;
            context = ctx;
            if (changedOut != nullptr)
                *changedOut = changed;

            if (out != nullptr && any)
            {
                performer.retune (changed, sample, *out);
                followChord (sample, *out);
            }
        }

        // The chord the Smart Scale follows: the effective key and scale, and the chord slot
        // played last (before any: the tonic, with no steps).
        SmartChord smartChord() const noexcept
        {
            SmartChord c;
            c.key = context.key;
            c.scale = context.scale;
            if (lastChordSlot >= 0 && lastChordSlot < numChordSlots)
                c.slot = chords[(size_t) lastChordSlot];
            return c;
        }

        // The Smart Scale zone follows the current chord: its patterns take it at their next step.
        void followChord (int sample, BlockEvents& out) noexcept
        {
            const SmartChord c = smartChord();
            if (c == patterns.getMapper().getChord())
                return;
            patterns.setChord (c);
            smart.retuneAll (sample, out);
        }

        // The next queued event of either zone due before `limit` (the chord zone's first at a
        // tie), or nullptr.
        ChordPerformer* peekNext (int floor, int limit, int& at, uint8_t& order) noexcept
        {
            int a1 = 0, a2 = 0;
            uint8_t o1 = 0, o2 = 0;
            const bool c = performer.peek (floor, limit, a1, o1);
            const bool z = smart.peek (floor, limit, a2, o2);
            if (! c && ! z)
                return nullptr;
            if (c && (! z || a1 < a2 || (a1 == a2 && o1 <= o2)))
            {
                at = a1;
                order = o1;
                return &performer;
            }
            at = a2;
            order = o2;
            return &smart;
        }

        // Takes a new editor snapshot. Edits apply while their group is the Chord Group; once
        // the group changes they're dropped (ChordPrism resets the editor on a group change),
        // and stay dropped until a newer snapshot arrives, even if the group changes back.
        // A snapshot arriving with the group change it was made for (a state restore) applies.
        void adoptEdits (const ChordEdits& edits, bool groupChanged) noexcept
        {
            if (groupChanged && editsLive && adopted.any())
            {
                discardedVersion.store (adopted.version, std::memory_order_relaxed);
                editsDiscarded.fetch_add (1, std::memory_order_release);
            }
            if (groupChanged)
                editsLive = false;

            if (edits.version != adopted.version)
            {
                adopted = edits;
                editsLive = edits.group == current.group;
            }
        }

        void applySettings (const EngineSettings& s, const ChordEdits& edits, BlockEvents& out) noexcept
        {
            const EngineSettings old = current;
            current = s;
            router.split = s.split;
            router.chromatic = s.smart.map.mapping == Mapping::Chromatic;

            if (s.shifterMode != old.shifterMode)
                shifters.reset();
            adoptEdits (edits, s.group != old.group);

            if (s.enabled != old.enabled)
            {
                releaseAll (0, out);
                refreshChords (0, nullptr);
                performer.applySettings (s.fx, s.outputChannel, {}, 0, out);
                patterns.update (s.smart, smartChord());
                smart.applySettings (PatternEngine::fx (s.smart), s.outputChannel, {}, 0, out);
                return;
            }

            // Held chords follow parameter and edit changes, and the effects follow theirs; the
            // Smart Scale zone follows the chords and its own settings.
            std::array<bool, numChordSlots> changed {};
            refreshChords (0, nullptr, &changed);
            performer.applySettings (s.fx, s.outputChannel, changed, 0, out);
            patterns.update (s.smart, smartChord());
            smart.applySettings (PatternEngine::fx (s.smart), s.outputChannel, {}, 0, out);

            if (s.muteChords && ! old.muteChords)
                releaseZone (Zone::Chord, out);
            if (s.muteScale && ! old.muteScale)
                releaseZone (Zone::Scale, out);
        }

        void releaseZone (Zone z, BlockEvents& out) noexcept
        {
            if (z == Zone::Chord)
                performer.killAll (0, out);
            else
                smart.killAll (0, out);
            for (int n = 0; n < 128; ++n)
                if (held[(size_t) n].down && ! held[(size_t) n].passThrough && held[(size_t) n].routed.zone == z)
                    voices.release ((VoiceTracker::Source) n, 0, out);
        }

        void startKey (int n, const HeldKey& h, int sample, BlockEvents& out) noexcept
        {
            if (h.passThrough)
            {
                voices.start ((VoiceTracker::Source) n, h.channel, n, h.velocity, sample, out);
                return;
            }

            switch (h.routed.role)
            {
                case KeyRole::ChordSlot:
                {
                    if (current.muteChords)
                        return;
                    lastChordSlot = h.routed.index;
                    performer.press (n, h.routed.index, h.velocity, sample, out);
                    followChord (sample, out);
                    return;
                }
                case KeyRole::ScaleTone:
                case KeyRole::PassThrough:
                    if (! current.muteScale)
                        smart.press (n, h.routed.index, h.velocity, sample, out);
                    return;
                case KeyRole::PatternSlot:
                    if (! current.muteScale)
                        smart.press (n, PatternEngine::slotForPattern (h.routed.index), h.velocity, sample, out);
                    return;
                case KeyRole::Shifter:
                    return;
            }
        }

        static int clampSample (int sample, int numSamples) noexcept
        {
            return sample < 0 ? 0 : (sample >= numSamples ? (numSamples > 0 ? numSamples - 1 : 0) : sample);
        }

        void handle (const MidiEvent& e, int numSamples, BlockEvents& out) noexcept
        {
            const int sample = clampSample (e.sample, numSamples);

            if (e.isNoteOn())
            {
                const int n = e.note();
                auto& h = held[(size_t) n];
                if (h.down)
                {
                    performer.end (n, sample, out);   // retrigger: a held key ends, even with Hold
                    smart.end (n, sample, out);
                    keyUp (n, sample, out);
                }

                h.down = true;
                h.velocity = (uint8_t) e.velocity();
                h.channel = (uint8_t) e.channel();
                h.passThrough = ! current.enabled;
                h.routed = router.route (n);

                if (! h.passThrough && h.routed.role == KeyRole::Shifter)
                {
                    if (shifters.press (n, h.routed.index, current.shifterMode))
                        refreshChords (sample, &out);
                    return;
                }
                startKey (n, h, sample, out);
            }
            else if (e.isNoteOff())
            {
                const int n = e.note();
                if (held[(size_t) n].down)
                    keyUp (n, sample, out);
            }
            else if (e.isAllNotesOff())
            {
                releaseAll (sample, out);
                out.push ({ sample, { e.data[0], e.data[1], e.data[2] }, e.size });
            }
            else
            {
                out.push ({ sample, { e.data[0], e.data[1], e.data[2] }, e.size });
            }
        }

        void keyUp (int n, int sample, BlockEvents& out) noexcept
        {
            auto& h = held[(size_t) n];
            h.down = false;
            if (! h.passThrough && h.routed.role == KeyRole::ChordSlot)
                performer.release (n, sample, out);   // ends, or is held (Hold)
            else if (! h.passThrough && h.routed.zone == Zone::Scale)
                smart.release (n, sample, out);
            else
                voices.release ((VoiceTracker::Source) n, sample, out);
            if (! h.passThrough && h.routed.role == KeyRole::Shifter && shifters.release (n, current.shifterMode))
                refreshChords (sample, &out);
        }

        static constexpr int maxQueueRunsPerSegment = 1 << 16;
        static constexpr int smartFirstSource = ChordPerformer::firstSource + 2 * ChordPerformer::numPerformances;   // 384

        EngineSettings current;
        InputRouter router;
        VoiceTracker voices;
        Clock clock;
        TestPattern pattern;
        Shifters shifters;
        ChordEdits adopted;               // the last editor snapshot taken
        bool editsLive = false;
        int activeShifter = -1;
        ChordSet chords {};
        ChordContext context;
        std::array<HeldKey, 128> held {};
        EventBuffer<256> injected;
        int lastChordSlot = -1;
        bool wasPlaying = false;
        std::atomic<bool> resetRequested { false };
        std::atomic<uint32_t> editsDiscarded { 0 };
        std::atomic<uint32_t> discardedVersion { 0 };
        ChordPerformer performer { voices, clock, chords, context };
        PatternEngine patterns;
        ChordPerformer smart { voices, clock, chords, context, patterns, smartFirstSource };
    };
}
