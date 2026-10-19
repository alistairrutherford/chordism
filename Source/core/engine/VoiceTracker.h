// Which output notes each source owns (PLAN.md §4.5, "Note bookkeeping").
//
// A source is anything that starts notes: an input key, a chord performance's strike or arp
// step (held by its key, by Hold, or opened by a sequencer step; ChordFx.h), and later a
// pattern slot. Releasing a source ends exactly the notes it started, whatever
// changed in between (Key, Scale, Split, Octave…). A pitch owned by several sources is
// reference-counted: the first owner sends the note-on, the last one to let go sends the
// note-off, so one source never cuts another's note.
//
// Plain C++, fixed size, real-time safe.
#pragma once

#include "MidiEvents.h"
#include <array>
#include <cstdint>

namespace chordism
{
    class VoiceTracker
    {
    public:
        using Source = uint16_t;
        static constexpr int maxSources = 384;          // 0..127 input keys, 128..383 chord performances (ChordFx.h)
        static constexpr int maxNotesPerSource = 64;

        // Starts `note` for `source`. A source that already owns the pitch keeps it (no second
        // note-on). Returns false, and owns nothing new, if the source is full or the output
        // has no room left for the note-off that would one day be needed.
        template <class Out>
        bool start (Source source, int channel, int note, int velocity, int sample, Out& out) noexcept
        {
            if (source >= maxSources || note < 0 || note > 127)
                return false;

            auto& s = sources[source];
            const uint16_t key = keyOf (channel, note);
            for (int i = 0; i < s.count; ++i)
                if (s.notes[(size_t) i] == key)
                    return true;

            if (s.count >= maxNotesPerSource)
                return false;

            if (refs[key] == 0)
            {
                // Keep half the buffer for note-offs: every sounding pitch can always be released.
                if (out.size() >= Out::capacity / 2
                    || ! out.push (MidiEvent::noteOn (channel, note, velocity < 1 ? 1 : velocity, sample)))
                    return false;
                ++sounding;
            }

            ++refs[key];
            s.notes[(size_t) s.count++] = key;
            return true;
        }

        // Releases every note `source` owns.
        template <class Out>
        void release (Source source, int sample, Out& out) noexcept
        {
            if (source >= maxSources)
                return;

            auto& s = sources[source];
            for (int i = 0; i < s.count; ++i)
                drop (s.notes[(size_t) i], sample, out);
            s.count = 0;
        }

        template <class Out>
        void releaseAll (int sample, Out& out) noexcept
        {
            for (int src = 0; src < maxSources; ++src)
                release ((Source) src, sample, out);
        }

        bool owns (Source source) const noexcept { return source < maxSources && sources[source].count > 0; }
        int notesOwnedBy (Source source) const noexcept { return source < maxSources ? sources[source].count : 0; }
        int ownerCount (int channel, int note) const noexcept { return refs[keyOf (channel, note)]; }
        int soundingNotes() const noexcept { return sounding; }
        bool isEmpty() const noexcept { return sounding == 0; }

        // Forget everything without sending note-offs (after the receiver was reset some other way).
        void clear() noexcept
        {
            refs.fill (0);
            for (auto& s : sources)
                s.count = 0;
            sounding = 0;
        }

    private:
        static uint16_t keyOf (int channel, int note) noexcept { return (uint16_t) (((channel & 15) << 7) | (note & 127)); }

        template <class Out>
        void drop (uint16_t key, int sample, Out& out) noexcept
        {
            if (refs[key] == 0)
                return;
            if (--refs[key] == 0)
            {
                out.push (MidiEvent::noteOff (key >> 7, key & 127, sample));
                --sounding;
            }
        }

        struct Owned
        {
            std::array<uint16_t, maxNotesPerSource> notes {};
            int count = 0;
        };

        std::array<uint16_t, 16 * 128> refs {};
        std::array<Owned, maxSources> sources {};
        int sounding = 0;
    };
}
