// The chord zone's effect chain and its scheduler (PLAN.md §4.5):
//
//   trigger → chord slot → voicing → Octave → Stack → [Strum | Arp] → Repeat → StepSeq gate
//           → Velocity Algorithm → Hold → (Mute, in ChordismEngine) → out
//
// Each chord key that goes down starts a *performance* (one per key, so 128). A performance
// plays its chord in strikes: one strike for as long as the chord is held, or a strike per
// Repeat step and/or per open Step Sequencer window. A strike starts all the tones at once, or
// one after another with Strum, and ends them together. With Arp the performance plays one tone
// per step instead (Repeat and Strum don't apply). Velocity Algorithm sets each note-on's
// velocity from its place in the bar. Hold keeps a performance going after its key is released
// (Hold.h).
//
// Note bookkeeping: performance k owns two VoiceTracker sources, 128 + 2k and 129 + 2k, and
// uses them in turn, one per strike or arp step, so each note-off ends exactly the notes its
// strike or step started, whatever changed meanwhile. Ending a performance releases both.
//
// Scheduling: what happens later (the next strummed tone, a strike's or a step's end, the next
// strike or step, a held chord's bar end) waits in a fixed-size EventQueue, in musical time
// (or in samples, for Strum in ms while stopped). Every performance has at most a handful of
// events queued, so the queue can't overflow. A tempo change needs nothing (musical events
// find their sample in the block they fall in); on a loop wrap or locate, `transportJump`
// moves the note-offs and strum tones with the music (they keep their lengths) and puts each
// pattern's next step on the grid at the new position.
//
// The Smart Scale zone runs a second ChordPerformer (PatternEngine.h) on the same clock: its
// tones and each key's pattern come from a PerformerVoicing instead of the chord set, its
// patterns stack each step after the pattern (Smart chain: Pattern → Octave → Stack), its
// strikes keep sounding through a chord change (the next strike takes the new tones), and its
// performances own the VoiceTracker sources from `sourceBase` on.
//
// Plain C++, no JUCE, no allocation.
#pragma once

#include "../theory/ChordModel.h"
#include "../theory/ChordGroups.h"
#include "Arp.h"
#include "Clock.h"
#include "EventQueue.h"
#include "Hold.h"
#include "MidiEvents.h"
#include "Repeat.h"
#include "Stack.h"
#include "StepSequencer.h"
#include "Strum.h"
#include "VelocityAlgorithm.h"
#include "VoiceTracker.h"
#include <array>
#include <cmath>
#include <limits>

namespace chordism
{
    struct ChordFxSettings
    {
        StackMode stack = StackMode::Off;
        StrumMode strum = StrumMode::Off;
        int strumRate = 2;                // index into the strum rates (1/64)
        double strumMs = 20.0;            // between tones while stopped
        ArpPattern arp = ArpPattern::Off;
        int arpRate = 5;                  // index into the pattern rates (1/16)
        int arpRange = 1;                 // octaves, 1..3
        double arpGate = 0.5;             // 0..1
        RepeatSettings repeat;
        StepSeqSettings seq;
        VelocitySettings velocity;
        HoldMode hold = HoldMode::Off;
    };

    // The arp a performance runs: the chord zone's Arp settings, or a Smart Scale pattern slot's.
    struct ArpSettings
    {
        ArpPattern pattern = ArpPattern::Off;
        int rate = 5;                     // index into the pattern rates
        int range = 1;                    // octaves, 1..3
        double gate = 0.5;                // 0..1
    };

    // Where a performer's tones come from when it isn't the chord zone's (the Smart Scale zone,
    // PatternEngine.h). Called on the audio thread: no allocation, no locks.
    class PerformerVoicing
    {
    public:
        virtual ~PerformerVoicing() = default;

        // The tones key `key` plays (ascending, each pitch once); `slot` is what it was pressed as.
        virtual VoicedNotes tones (int key, int slot) const noexcept = 0;

        // The pattern it runs: an arp, or pattern Off to strike its tones.
        virtual ArpSettings arp (int key, int slot) const noexcept = 0;
    };

    class ChordPerformer
    {
    public:
        static constexpr int numPerformances = 128;
        static constexpr int firstSource = 128;   // sources 0..127 are input keys

        static VoiceTracker::Source sourceOf (int key, int which) noexcept
        {
            return (VoiceTracker::Source) (firstSource + 2 * key + (which & 1));
        }

        enum EventType : uint8_t { StrumNote, StrikeEnd, NextStrike, ArpStep, ArpNoteEnd, HoldEnd };

        // Ties at one sample: note-offs, then input (key presses and releases), then note-ons.
        static constexpr uint8_t endOrder = 0, inputOrder = 1, startOrder = 2;

        ChordPerformer (VoiceTracker& v, Clock& c, const ChordSet& chordSet, const ChordContext& ctx) noexcept
            : voices (v), clock (c), chords (chordSet), context (ctx) {}

        // A performer whose tones and patterns come from `voicing` (the Smart Scale zone), with
        // the sources sourceBase + 2k and sourceBase + 2k + 1 for key k. `chordSet` and `ctx`
        // aren't used.
        ChordPerformer (VoiceTracker& v, Clock& c, const ChordSet& chordSet, const ChordContext& ctx,
                        const PerformerVoicing& voicingIn, int sourceBaseIn) noexcept
            : voices (v), clock (c), chords (chordSet), context (ctx), voicing (&voicingIn), sourceBase (sourceBaseIn) {}

        // The other zone's performer on the same clock: a stopped clock is only re-anchored on
        // a key press when neither has anything playing.
        void setSibling (const ChordPerformer* other) noexcept { sibling = other; }

        VoiceTracker::Source source (int key, int which) const noexcept
        {
            return (VoiceTracker::Source) (sourceBase + 2 * key + (which & 1));
        }

        // Forgets every performance without sending anything (the voices were released or
        // cleared already), and reseeds Random strum.
        void reset() noexcept
        {
            for (auto& p : perfs)
                if (p.alive)
                    forget (p);
            queue.clear();
            alive = 0;
            strummer.reset();
        }

        // The block segment (Clock) that the following calls' samples are in.
        void setSegment (int seg) noexcept { segment = seg; }

        // Settings for this block, at `sample` (the block start); `changed` marks the chord
        // slots whose chord changed with them. Turning Arp, Repeat or the Step Sequencer on or
        // off restarts every pattern; otherwise changed chords (or a Stack change) restrike.
        void applySettings (const ChordFxSettings& s, int channel, const std::array<bool, numChordSlots>& changed,
                            int sample, BlockEvents& o) noexcept
        {
            out = &o;
            const ChordFxSettings old = fx;
            fx = s;
            outChannel = channel;
            if (alive == 0)
                return;

            if (old.hold != HoldMode::Off && s.hold == HoldMode::Off)
                endLatched (sample, -1);

            // Turning a pattern kind on or off restarts a performance; other arp settings
            // (rate, range, gate, pattern) take effect at its next step.
            const bool restartAll = (voicing == nullptr && (old.arp == ArpPattern::Off) != (s.arp == ArpPattern::Off))
                                 || old.repeat.on != s.repeat.on || old.seq.on != s.seq.on;
            std::array<bool, numPerformances> restart {};
            bool anyRestart = false;
            for (int k = 0; k < numPerformances; ++k)
            {
                Perf& p = perfs[(size_t) k];
                if (! p.alive)
                    continue;
                p.arp = arpOf (k, p.slot);
                restart[(size_t) k] = restartAll || p.arpMode != (p.arp.pattern != ArpPattern::Off);
                anyRestart |= restart[(size_t) k];
            }
            if (anyRestart)
            {
                for (int k = 0; k < numPerformances; ++k)
                    if (restart[(size_t) k])
                        stopPattern (k, sample);
                for (int k = 0; k < numPerformances; ++k)
                    if (restart[(size_t) k])
                    {
                        perfs[(size_t) k].notes = voiced (k, perfs[(size_t) k].slot);
                        startPattern (k, sample, true);
                    }
                if (restartAll)
                    return;
            }

            std::array<bool, numChordSlots> slots = changed;
            if (old.stack != s.stack)
                slots.fill (true);
            retuneWhere ([&] (int k, const Perf& p) noexcept
            {
                return ! restart[(size_t) k] && (voicing != nullptr || (p.slot >= 0 && p.slot < numChordSlots && slots[(size_t) p.slot]));
            }, sample);
        }

        // The transport started (stopped → playing): Random strum starts from its seed again.
        void transportStarted() noexcept { strummer.reset(); }

        // Musical time moved by `delta` at `sample` (the start of the current segment): a loop
        // wrap, a locate, or the start of playback. Queued note-offs and strum tones move with
        // the music; each pattern's next step goes onto the grid at the new position.
        void transportJump (double delta, int sample, BlockEvents& o) noexcept
        {
            out = &o;
            if (alive == 0)
                return;
            const double now = ppqAt (sample);
            // A grid line up to a sample before `now` is still "now": a wrap lands a fraction of
            // a sample past the loop start.
            const double from = now - 1.0 / clock.samplesPerQuarter();
            queue.forEach ([&] (TimedEvent& e) noexcept
            {
                if (e.base != TimedEvent::Base::Musical)
                    return;
                if (e.type == HoldEnd)
                    e.ppq = clock.isPlaying() ? clock.nextBarLine (now) : e.ppq + delta;
                else
                    e.ppq += delta;
            });
            queue.removeIf ([] (const TimedEvent& e) noexcept { return e.type == NextStrike || e.type == ArpStep; });

            for (int k = 0; k < numPerformances; ++k)
            {
                const Perf& p = perfs[(size_t) k];
                if (! p.alive)
                    continue;
                if (p.arpMode)
                    pushAt (grid::atOrAfter (from, patternRatePpq (p.arp.rate)), ArpStep, startOrder, k, p.gen, 0);
                else if (hasRhythm())
                    pushAt (nextStrikeFrom (now, from), NextStrike, startOrder, k, p.gen, 0);
            }
        }

        // Chord key `key` went down: plays chord slot `slot`.
        void press (int key, int slot, int velocity, int sample, BlockEvents& o) noexcept
        {
            out = &o;
            if (! validKey (key))
                return;
            const bool stopped = ! clock.isPlaying();
            const bool wasLatched = perfs[(size_t) key].alive && perfs[(size_t) key].latched;

            endLatched (sample, -1);                // a new selection ends the held chords
            if (perfs[(size_t) key].alive)
                endPerformance (key, sample);
            if (stopped && wasLatched)
                return;                             // stopped: pressing a held chord's key ends it

            if (stopped && alive == 0 && (sibling == nullptr || sibling->aliveCount() == 0))
                clock.anchor (sample);              // patterns start on a bar of their own

            Perf& p = perfs[(size_t) key];
            p.alive = true;
            p.keyDown = true;
            p.latched = false;
            p.slot = slot;
            p.velocity = (uint8_t) (velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity));
            p.notes = voiced (key, slot);
            ++alive;
            startPattern (key, sample, true);
        }

        // Chord key `key` went up: the performance ends, or is held (Hold).
        void release (int key, int sample, BlockEvents& o) noexcept
        {
            out = &o;
            if (! validKey (key))
                return;
            Perf& p = perfs[(size_t) key];
            if (! p.alive || ! p.keyDown)
                return;
            p.keyDown = false;
            if (fx.hold == HoldMode::Off)
            {
                endPerformance (key, sample);
                return;
            }
            p.latched = true;
            if (fx.hold == HoldMode::Bar && clock.isPlaying())
                pushAt (clock.nextBarLine (ppqAt (sample)), HoldEnd, endOrder, key, p.life, 0);
        }

        // Ends key `key`'s performance now, held or not.
        void end (int key, int sample, BlockEvents& o) noexcept
        {
            out = &o;
            if (validKey (key) && perfs[(size_t) key].alive)
                endPerformance (key, sample);
        }

        void killAll (int sample, BlockEvents& o) noexcept
        {
            out = &o;
            for (int k = 0; k < numPerformances; ++k)
                if (perfs[(size_t) k].alive)
                    endPerformance (k, sample);
        }

        // The chords changed (parameters, edits, a shifter): every performance whose slot is
        // marked and whose tones changed plays the new tones. A sounding strike is restruck at
        // `sample` (all releases first, so a shared pitch restrikes); an arp takes the new tones
        // at its next step. With a voicing (the Smart Scale zone) every performance is checked,
        // and a sounding strike keeps its notes: the next strike plays the new tones.
        void retune (const std::array<bool, numChordSlots>& changed, int sample, BlockEvents& o) noexcept
        {
            out = &o;
            retuneWhere ([&] (int, const Perf& p) noexcept
            {
                return voicing != nullptr || (p.slot >= 0 && p.slot < numChordSlots && changed[(size_t) p.slot]);
            }, sample);
        }

        // Every performance takes its tones again (the Smart Scale zone's chord or mapping changed).
        void retuneAll (int sample, BlockEvents& o) noexcept
        {
            out = &o;
            retuneWhere ([] (int, const Perf&) noexcept { return true; }, sample);
        }

        // The next queued event due in the current segment before block sample `limit` (and at
        // least `floor`): true with its sample and tie order. Run it with runNext().
        bool peek (int floor, int limit, int& at, uint8_t& order) noexcept
        {
            pending = queue.earliestDue (clock, segment, floor, limit, pendingAt);
            if (pending < 0)
                return false;
            at = pendingAt;
            order = queue[pending].order;
            return true;
        }

        void runNext (BlockEvents& o) noexcept
        {
            out = &o;
            if (pending < 0 || pending >= queue.size())
                return;
            const TimedEvent e = queue.take (pending);
            pending = -1;
            const int s = pendingAt;
            Perf& p = perfs[(size_t) e.owner];
            if (! p.alive)
                return;

            switch (e.type)
            {
                case StrumNote:
                    if (e.tag == p.gen && p.strikeOpen && e.arg == p.strumGen)
                        playStrumNote (e.owner, s);
                    break;
                case StrikeEnd:
                    if (e.tag == p.gen && p.strikeOpen && e.arg == p.strikeSeq)
                        closeStrike (e.owner, s);
                    break;
                case NextStrike:
                    if (e.tag == p.gen && ! p.arpMode)
                        planStrike (e.owner, s, e.ppq, false);
                    break;
                case ArpStep:
                    if (e.tag == p.gen && p.arpMode)
                        arpStep (e.owner, s, e.ppq, false);
                    break;
                case ArpNoteEnd:
                    if (e.tag == p.gen && p.arpNoteOpen && e.arg == p.arpNoteSeq)
                        closeArpNote (e.owner, s);
                    break;
                case HoldEnd:
                    if (e.tag == p.life && p.latched && fx.hold == HoldMode::Bar)
                        endPerformance (e.owner, s);
                    break;
                default:
                    break;
            }
        }

        bool isAlive (int key) const noexcept { return validKey (key) && perfs[(size_t) key].alive; }
        bool isLatched (int key) const noexcept { return isAlive (key) && perfs[(size_t) key].latched; }
        int aliveCount() const noexcept { return alive; }
        int queued() const noexcept { return queue.size(); }
        const ChordFxSettings& settings() const noexcept { return fx; }

    private:
        struct Perf
        {
            bool alive = false, keyDown = false, latched = false, arpMode = false;
            uint32_t gen = 0;             // the pattern: bumped when it restarts or ends
            uint32_t life = 0;            // the performance: bumped when it ends
            int slot = 0;
            uint8_t velocity = 100;
            VoicedNotes notes;
            ArpSettings arp;              // the pattern's arp (pattern Off: strikes)

            uint8_t src = 0;              // which of the two sources the current strike / step uses
            bool strikeOpen = false;
            uint32_t strikeSeq = 0, strumGen = 0;
            bool strumMusical = true;
            double strumStartPpq = 0.0, strumStepPpq = 0.0, strumStepSamples = 0.0;
            int64_t strumStartAbs = 0;
            int strumCount = 0, strumNext = 0;
            std::array<uint8_t, VoicedNotes::capacity> order {};

            int64_t arpStepIndex = 0;
            bool arpNoteOpen = false;
            uint32_t arpNoteSeq = 0;
        };

        static bool validKey (int key) noexcept { return key >= 0 && key < numPerformances; }

        double ppqAt (double sample) const noexcept { return clock.ppqAt (segment, sample); }
        bool hasRhythm() const noexcept { return fx.repeat.on || fx.seq.on; }
        StepGrid stepGrid() const noexcept { return StepGrid (fx.seq, clock.barOrigin(), clock.barLength()); }

        VoicedNotes voiced (int key, int slot) const noexcept
        {
            if (voicing != nullptr)
                return voicing->tones (key, slot);
            if (slot < 0 || slot >= numChordSlots)
                return {};
            return stackNotes (chordNotes (chords[(size_t) slot], context), fx.stack);
        }

        ArpSettings arpOf (int key, int slot) const noexcept
        {
            if (voicing != nullptr)
                return voicing->arp (key, slot);
            return { fx.arp, fx.arpRate, fx.arpRange, fx.arpGate };
        }

        template <class Pred>
        void retuneWhere (Pred marked, int sample) noexcept
        {
            if (alive == 0)
                return;
            std::array<bool, numPerformances> restrike {};
            for (int k = 0; k < numPerformances; ++k)
            {
                Perf& p = perfs[(size_t) k];
                if (! p.alive || ! marked (k, (const Perf&) p))
                    continue;
                const VoicedNotes next = voiced (k, p.slot);
                if (next == p.notes)
                    continue;
                p.notes = next;
                if (voicing == nullptr && ! p.arpMode && p.strikeOpen)
                {
                    voices.release (source (k, p.src), sample, *out);
                    restrike[(size_t) k] = true;
                }
            }
            for (int k = 0; k < numPerformances; ++k)
                if (restrike[(size_t) k])
                    strum (k, sample);
        }

        // The velocity for a note-on at `sample`. The bar position is read half a sample on, so
        // a note on a grid line counts as on it even when rounding put it a fraction early.
        int velocityAt (const Perf& p, int sample) const noexcept
        {
            return fx.velocity.apply (p.velocity, clock.barFraction (ppqAt (sample + 0.5)));
        }

        void pushAt (double ppq, uint8_t type, uint8_t order, int key, uint32_t tag, uint32_t arg) noexcept
        {
            if (std::isfinite (ppq))
                queue.push (TimedEvent::musical (ppq, type, order, (uint16_t) key, tag, arg));
        }

        // Where the next strike goes after a jump to `now` (a strike at `now` if the
        // sequencer's window is open there); grid lines from `from` on count.
        double nextStrikeFrom (double now, double from) const noexcept
        {
            if (! fx.seq.on)
                return grid::atOrAfter (from, fx.repeat.step());
            const StepGrid g = stepGrid();
            const SeqWindow w = g.windowAt (now);
            if (w.valid && w.end - now >= grid::minFirstStep * w.slotLength)
                return now;
            double next = 0.0;
            return g.nextStart (from, true, next) ? next : std::numeric_limits<double>::infinity();
        }

        void startPattern (int key, int sample, bool press) noexcept
        {
            Perf& p = perfs[(size_t) key];
            p.arp = arpOf (key, p.slot);
            p.arpMode = p.arp.pattern != ArpPattern::Off;
            if (p.arpMode)
            {
                p.arpStepIndex = 0;
                arpStep (key, sample, ppqAt (sample), press);
            }
            else
            {
                planStrike (key, sample, ppqAt (sample), press);
            }
        }

        // Ends what the pattern is sounding and drops its queued events (not a hold's bar end).
        void stopPattern (int key, int sample) noexcept
        {
            Perf& p = perfs[(size_t) key];
            closeStrike (key, sample);
            closeArpNote (key, sample);
            ++p.gen;
            queue.removeIf ([key] (const TimedEvent& e) noexcept { return e.owner == key && e.type != HoldEnd; });
        }

        void endPerformance (int key, int sample) noexcept
        {
            Perf& p = perfs[(size_t) key];
            voices.release (source (key, 0), sample, *out);
            voices.release (source (key, 1), sample, *out);
            forget (p);
            queue.removeIf ([key] (const TimedEvent& e) noexcept { return e.owner == key; });
            --alive;
        }

        void forget (Perf& p) noexcept
        {
            p.alive = p.keyDown = p.latched = false;
            p.strikeOpen = p.arpNoteOpen = false;
            ++p.gen;
            ++p.life;
        }

        // Ends every held (released) performance, except key `keep`.
        void endLatched (int sample, int keep) noexcept
        {
            for (int k = 0; k < numPerformances; ++k)
                if (k != keep && perfs[(size_t) k].alive && perfs[(size_t) k].latched)
                    endPerformance (k, sample);
        }

        //======================================================================
        // Strikes (chord mode)

        // The strike at musical time T (a key press, or a queued NextStrike), and the next one.
        void planStrike (int key, int sample, double T, bool press) noexcept
        {
            const Perf& p = perfs[(size_t) key];
            if (! hasRhythm())
            {
                beginStrike (key, sample, std::numeric_limits<double>::infinity());
                return;
            }

            if (! fx.seq.on)
            {
                const RepeatStrike r = repeatStrike (fx.repeat, T, press);
                beginStrike (key, sample, r.end);
                pushAt (r.next, NextStrike, startOrder, key, p.gen, 0);
                return;
            }

            const StepGrid g = stepGrid();
            SeqWindow w = g.windowAt (T);
            if (w.valid && press && w.end - T < grid::minFirstStep * w.slotLength)
                w.valid = false;           // too little of the step left: wait for the next one

            double next = 0.0;
            bool haveNext = false;
            if (w.valid)
            {
                double endPpq = w.end;
                if (fx.repeat.on)
                {
                    const RepeatStrike r = repeatStrike (fx.repeat, T, press);
                    endPpq = r.end < endPpq ? r.end : endPpq;
                    if (r.next < w.end - grid::minFirstStep * fx.repeat.step())
                    {
                        next = r.next;
                        haveNext = true;
                    }
                }
                if (! haveNext)
                    haveNext = g.nextStart (T, false, next);
                beginStrike (key, sample, endPpq);
            }
            else
            {
                haveNext = g.nextStart (T, press, next);
                if (haveNext && ! press && next <= T + grid::epsilon)
                    haveNext = false;
            }
            if (haveNext)
                pushAt (next, NextStrike, startOrder, key, perfs[(size_t) key].gen, 0);
        }

        void beginStrike (int key, int sample, double endPpq) noexcept
        {
            Perf& p = perfs[(size_t) key];
            closeStrike (key, sample);
            drop (key, StrikeEnd);
            p.src ^= 1;
            ++p.strikeSeq;
            p.strikeOpen = true;
            if (std::isfinite (endPpq))
                pushAt (endPpq, StrikeEnd, endOrder, key, p.gen, p.strikeSeq);
            strum (key, sample);
        }

        // Starts the strike's tones: all now, or the first now and the rest queued (Strum).
        void strum (int key, int sample) noexcept
        {
            Perf& p = perfs[(size_t) key];
            ++p.strumGen;
            drop (key, StrumNote);
            const int n = p.notes.size;
            if (fx.strum == StrumMode::Off || n <= 1)
            {
                const int vel = velocityAt (p, sample);
                for (const auto note : p.notes)
                    voices.start (source (key, p.src), outChannel, note, vel, sample, *out);
                return;
            }

            strummer.order (fx.strum, n, p.order.data());
            p.strumCount = n;
            p.strumNext = 0;
            p.strumMusical = clock.isPlaying();
            p.strumStartPpq = ppqAt (sample);
            p.strumStartAbs = clock.blockStart() + sample;
            p.strumStepPpq = strumRatePpq (fx.strumRate);
            p.strumStepSamples = (fx.strumMs > 0.0 ? fx.strumMs : 0.0) * clock.sampleRate() / 1000.0;
            playStrumNote (key, sample);
        }

        void playStrumNote (int key, int sample) noexcept
        {
            Perf& p = perfs[(size_t) key];
            if (p.strumNext >= p.strumCount)
                return;
            const int i = p.order[(size_t) p.strumNext++];
            if (i < p.notes.size)
                voices.start (source (key, p.src), outChannel, p.notes[i], velocityAt (p, sample), sample, *out);

            if (p.strumNext >= p.strumCount)
                return;
            const double k = p.strumNext;
            if (p.strumMusical)
                pushAt (p.strumStartPpq + k * p.strumStepPpq, StrumNote, startOrder, key, p.gen, p.strumGen);
            else
                queue.push (TimedEvent::atSample (p.strumStartAbs + (int64_t) std::llround (k * p.strumStepSamples), StrumNote,
                                                  startOrder, (uint16_t) key, p.gen, p.strumGen));
        }

        void closeStrike (int key, int sample) noexcept
        {
            Perf& p = perfs[(size_t) key];
            if (! p.strikeOpen)
                return;
            voices.release (source (key, p.src), sample, *out);
            p.strikeOpen = false;
            ++p.strumGen;
            drop (key, StrumNote);
        }

        // Drops `key`'s queued events of one type (stale ones, so the queue stays short).
        void drop (int key, uint8_t type) noexcept
        {
            queue.removeIf ([key, type] (const TimedEvent& e) noexcept { return e.owner == key && e.type == type; });
        }

        //======================================================================
        // Arp

        void arpStep (int key, int sample, double T, bool press) noexcept
        {
            Perf& p = perfs[(size_t) key];
            closeArpNote (key, sample);

            const double step = patternRatePpq (p.arp.rate);
            const double next = press ? grid::afterPress (T, step) : grid::after (T, step);
            double endPpq = T + grid::clampGate (p.arp.gate) * (next - T);
            bool sound = true;
            if (fx.seq.on)
            {
                const SeqWindow w = stepGrid().windowAt (T);
                sound = w.valid;
                endPpq = w.valid && w.end < endPpq ? w.end : endPpq;
            }

            const VoicedNotes tones = arpTones (p.notes, p.arp.range);
            const int i = arpIndex (p.arp.pattern, tones.size, p.arpStepIndex++);
            if (sound && i >= 0 && endPpq > T)
            {
                p.src ^= 1;
                ++p.arpNoteSeq;
                const int vel = velocityAt (p, sample);
                voices.start (source (key, p.src), outChannel, tones[i], vel, sample, *out);
                if (voicing != nullptr)
                {
                    // The Smart Scale zone stacks after the pattern: each step with its octaves.
                    const bool one = fx.stack == StackMode::PlusOne || fx.stack == StackMode::PlusOneAndTwo;
                    const bool two = fx.stack == StackMode::PlusTwo || fx.stack == StackMode::PlusOneAndTwo;
                    if (one && tones[i] + 12 < 128)
                        voices.start (source (key, p.src), outChannel, tones[i] + 12, vel, sample, *out);
                    if (two && tones[i] + 24 < 128)
                        voices.start (source (key, p.src), outChannel, tones[i] + 24, vel, sample, *out);
                }
                p.arpNoteOpen = true;
                pushAt (endPpq, ArpNoteEnd, endOrder, key, p.gen, p.arpNoteSeq);
            }
            pushAt (next, ArpStep, startOrder, key, p.gen, 0);
        }

        void closeArpNote (int key, int sample) noexcept
        {
            Perf& p = perfs[(size_t) key];
            if (! p.arpNoteOpen)
                return;
            voices.release (source (key, p.src), sample, *out);
            p.arpNoteOpen = false;
            drop (key, ArpNoteEnd);
        }

        VoiceTracker& voices;
        Clock& clock;
        const ChordSet& chords;
        const ChordContext& context;
        const PerformerVoicing* voicing = nullptr;
        int sourceBase = firstSource;
        const ChordPerformer* sibling = nullptr;

        ChordFxSettings fx;
        int outChannel = 0;
        int segment = 0;
        BlockEvents* out = nullptr;
        std::array<Perf, numPerformances> perfs {};
        int alive = 0;
        Strummer strummer;
        EventQueue<1024> queue;           // ≤ 4 events per performance (strum tone, end, next, hold)
        int pending = -1, pendingAt = 0;
    };
}
