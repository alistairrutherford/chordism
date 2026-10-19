// The MIDI engine of Phase 1: input router, plain chord output, the test pattern, and
// note bookkeeping through VoiceTracker. Plain C++, no JUCE, no allocation; the plug-in is a
// thin adapter around it, so every behaviour here is unit-testable.
//
//   chord zone, white key  → the slot's chord (Key, Scale, Chord Group, Chord Octave)
//   chord zone, black key  → shifter (Phase 2; consumed for now)
//   Smart Scale zone       → passed through on the output channel (mappings are Phase 4)
//   Enable off             → input passes through unchanged
#pragma once

#include "../theory/ChordGroups.h"
#include "InputRouter.h"
#include "MidiEvents.h"
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
        int split = InputRouter::defaultSplit;
        int chordOctave = 0;
        bool muteChords = false;
        bool muteScale = false;
        bool testPattern = false;
        int outputChannel = 0;            // 0..15

        bool sameChords (const EngineSettings& o) const noexcept
        {
            return key == o.key && scale == o.scale && group == o.group && chordOctave == o.chordOctave;
        }
    };

    class ChordismEngine
    {
    public:
        ChordismEngine() { rebuildChords(); }

        // Processes one block. `in` must be sorted by sample. Appends to `out` in sample order.
        void process (const EngineSettings& s, const Transport& t, const MidiEvent* in, int numIn,
                      int numSamples, BlockEvents& out) noexcept
        {
            if (resetRequested.exchange (false))
                releaseAll (0, out);

            applySettings (s, out);

            if (wasPlaying && ! t.playing)
                releaseAll (0, out);   // transport stop: all notes off (PLAN.md §5)
            wasPlaying = t.playing;

            injected.clear();
            pattern.generate (s.enabled && s.testPattern, t, numSamples, router, injected);

            int i = 0, j = 0;
            while (i < numIn || j < injected.size())
            {
                const bool fromInput = j >= injected.size() || (i < numIn && in[i].sample <= injected[j].sample);
                const MidiEvent& e = fromInput ? in[i++] : injected[j++];
                handle (e, numSamples, out);
            }
        }

        // All notes off now (bypass, transport stop, all-notes-off, destruction).
        void releaseAll (int sample, BlockEvents& out) noexcept
        {
            voices.releaseAll (sample, out);
            held = {};
        }

        // The next process() starts by releasing everything. For places where no MIDI can be
        // sent (releaseResources, prepareToPlay); callable from any thread.
        void requestReset() noexcept { resetRequested.store (true); }

        const VoiceTracker& getVoices() const noexcept { return voices; }
        const ChordSet& getChords() const noexcept { return chords; }
        int getLastChordSlot() const noexcept { return lastChordSlot; }
        int getPatternKey() const noexcept { return pattern.getHeldKey(); }

    private:
        struct HeldKey
        {
            bool down = false;
            uint8_t velocity = 0;
            uint8_t channel = 0;
            RoutedKey routed;
            bool passThrough = false;     // pressed while Enable was off
        };

        void rebuildChords() noexcept { chords = makeGroupChords (current.group, current.scale); }

        void applySettings (const EngineSettings& s, BlockEvents& out) noexcept
        {
            const EngineSettings old = current;
            current = s;
            router.split = s.split;

            if (s.enabled != old.enabled)
            {
                releaseAll (0, out);
                if (! s.sameChords (old))
                    rebuildChords();
                return;
            }

            if (! s.sameChords (old))
            {
                rebuildChords();
                // Held chords follow the change: release and replay each with the new settings.
                for (int n = 0; n < 128; ++n)
                {
                    auto& h = held[(size_t) n];
                    if (h.down && ! h.passThrough && h.routed.role == KeyRole::ChordSlot)
                    {
                        voices.release ((VoiceTracker::Source) n, 0, out);
                        startKey (n, h, 0, out);
                    }
                }
            }

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
                    const auto notes = chordNotes (chords[(size_t) h.routed.index],
                                                   { current.key, current.scale, current.chordOctave });
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
                    voices.release ((VoiceTracker::Source) n, sample, out);   // retrigger

                h.down = true;
                h.velocity = (uint8_t) e.velocity();
                h.channel = (uint8_t) e.channel();
                h.passThrough = ! current.enabled;
                h.routed = router.route (n);
                startKey (n, h, sample, out);
            }
            else if (e.isNoteOff())
            {
                const int n = e.note();
                if (held[(size_t) n].down)
                {
                    voices.release ((VoiceTracker::Source) n, sample, out);
                    held[(size_t) n].down = false;
                }
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

        EngineSettings current;
        InputRouter router;
        VoiceTracker voices;
        TestPattern pattern;
        ChordSet chords {};
        std::array<HeldKey, 128> held {};
        EventBuffer<256> injected;
        int lastChordSlot = -1;
        bool wasPlaying = false;
        std::atomic<bool> resetRequested { false };
    };
}
