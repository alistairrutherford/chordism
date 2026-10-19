// The MIDI engine: input router, the Chord Generator (Chord Groups, Chord Editor edits,
// Progressions, Shifters), the test pattern, and note bookkeeping through VoiceTracker.
// Plain C++, no JUCE, no allocation; the plug-in is a thin adapter around it, so every
// behaviour here is unit-testable.
//
//   chord zone, white key  → the slot's chord: degree from the Progression, steps from the
//                            Chord Group or the degree's editor edits, Key, Scale, Chord Octave
//   chord zone, black key  → shifter (Shifters.h): swaps in its Chord Group or Key/Scale
//   Smart Scale zone       → passed through on the output channel (mappings are Phase 4)
//   Enable off             → input passes through unchanged
//
// Whenever the chords change while chord keys are held (a parameter, an edit, a shifter), each
// held key whose chord changed is released and replayed: shifting while a chord is held
// retriggers it (PLAN.md §4.2).
#pragma once

#include "../theory/ChordEditor.h"
#include "InputRouter.h"
#include "MidiEvents.h"
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
    };

    class ChordismEngine
    {
    public:
        ChordismEngine() { refreshChords (0, nullptr); }

        // Processes one block. `in` must be sorted by sample. Appends to `out` in sample order.
        // `edits` is the Chord Editor's current snapshot (see adoptEdits for when it applies).
        void process (const EngineSettings& s, const ChordEdits& edits, const Transport& t, const MidiEvent* in,
                      int numIn, int numSamples, BlockEvents& out) noexcept
        {
            if (resetRequested.exchange (false))
                releaseAll (0, out);

            applySettings (s, edits, out);

            if (wasPlaying && ! t.playing)
                releaseAll (0, out);   // transport stop: all notes off (PLAN.md §5)
            wasPlaying = t.playing;

            injected.clear();
            pattern.generate (s.enabled && s.testPattern, s.testShifter, t, numSamples, router, injected);

            int i = 0, j = 0;
            while (i < numIn || j < injected.size())
            {
                const bool fromInput = j >= injected.size() || (i < numIn && in[i].sample <= injected[j].sample);
                const MidiEvent& e = fromInput ? in[i++] : injected[j++];
                handle (e, numSamples, out);
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
        int getActiveShifter() const noexcept { return activeShifter; }
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

        // Recomputes the chords. With `out`, every held chord key whose chord changed is
        // released and replayed at `sample` (all releases first, so a shared pitch restrikes).
        void refreshChords (int sample, BlockEvents* out) noexcept
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

            if (out == nullptr || ! any)
                return;

            for (int n = 0; n < 128; ++n)
                if (isHeldChord (held[(size_t) n]) && changed[(size_t) held[(size_t) n].routed.index])
                    voices.release ((VoiceTracker::Source) n, sample, *out);
            for (int n = 0; n < 128; ++n)
                if (isHeldChord (held[(size_t) n]) && changed[(size_t) held[(size_t) n].routed.index])
                    startKey (n, held[(size_t) n], sample, *out);
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

            if (s.shifterMode != old.shifterMode)
                shifters.reset();
            adoptEdits (edits, s.group != old.group);

            if (s.enabled != old.enabled)
            {
                releaseAll (0, out);
                refreshChords (0, nullptr);
                return;
            }

            refreshChords (0, &out);   // held chords follow parameter and edit changes

            if (s.muteChords && ! old.muteChords)
                releaseZone (Zone::Chord, out);
            if (s.muteScale && ! old.muteScale)
                releaseZone (Zone::Scale, out);
        }

        void releaseZone (Zone z, BlockEvents& out) noexcept
        {
            for (int n = 0; n < 128; ++n)
                if (held[(size_t) n].down && ! held[(size_t) n].passThrough && held[(size_t) n].routed.zone == z)
                    voices.release ((VoiceTracker::Source) n, 0, out);
        }

        void startKey (int n, const HeldKey& h, int sample, BlockEvents& out) noexcept
        {
            const auto src = (VoiceTracker::Source) n;
            const int ch = current.outputChannel;

            if (h.passThrough)
            {
                voices.start (src, h.channel, n, h.velocity, sample, out);
                return;
            }

            switch (h.routed.role)
            {
                case KeyRole::ChordSlot:
                {
                    if (current.muteChords)
                        return;
                    lastChordSlot = h.routed.index;
                    const auto notes = chordNotes (chords[(size_t) h.routed.index], context);
                    for (auto note : notes)
                        voices.start (src, ch, note, h.velocity, sample, out);
                    return;
                }
                case KeyRole::ScaleTone:
                case KeyRole::PatternSlot:
                case KeyRole::PassThrough:
                    if (! current.muteScale)
                        voices.start (src, ch, n, h.velocity, sample, out);
                    return;
                case KeyRole::Shifter:
                    return;
            }
        }

        void handle (const MidiEvent& e, int numSamples, BlockEvents& out) noexcept
        {
            int sample = e.sample < 0 ? 0 : (e.sample >= numSamples ? (numSamples > 0 ? numSamples - 1 : 0) : e.sample);

            if (e.isNoteOn())
            {
                const int n = e.note();
                auto& h = held[(size_t) n];
                if (h.down)
                    keyUp (n, sample, out);   // retrigger

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
            voices.release ((VoiceTracker::Source) n, sample, out);
            if (! h.passThrough && h.routed.role == KeyRole::Shifter && shifters.release (n, current.shifterMode))
                refreshChords (sample, &out);
        }

        EngineSettings current;
        InputRouter router;
        VoiceTracker voices;
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
    };
}
