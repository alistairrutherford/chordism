// Instance Sync (PLAN.md §4.7): instances of Chordism in one process that are in the same sync
// group (A-D) share the chord the Smart Scale follows, and their Key and Scale.
//
//   Chords   Whenever a member's current chord changes because it played one (a chord key, or
//            a held chord retuned by a shifter or a setting), it publishes that chord (key,
//            scale and the chord slot as played) stamped with its PPQ time. The other members
//            read it at the start of their next block and apply it at the stamped sample if it
//            falls in that block, else at once: an instance that processes earlier in the host's
//            cycle follows one block late, which look-ahead covers on a loop (ChordTimeline.h).
//   Key and  Published from the message thread when a member's Key or Scale parameter changes;
//   Scale    the others set theirs (ChordismProcessor's timer). The last change wins.
//
// Live runs every plug-in instance in one process (Phase 0), so a process-wide object works;
// hosts that sandbox plug-ins don't sync (PLAN.md §9).
//
// Chords travel through one ring of 64 slots per group. Each slot is a seqlock: a writer claims
// the next index (an atomic counter), marks the slot odd, writes the packed words and a checksum
// of them and its index, and marks it even (2·index + 2); a reader copies the words and checks
// the mark again and the checksum, so it never sees a torn chord, even if a writer stalled so
// long that another lapped it on the same slot. Neither side waits or allocates; any number of
// writers and readers (the host's audio threads) may run at once. A reader more than a ring
// behind skips to what's left: only the latest chord matters, and the ring holds many blocks'
// worth.
//
// Plain C++, fixed size, no locks.
#pragma once

#include "ScaleMapper.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace chordism
{
    // The Sync group parameter: 0 = Off, 1-4 = A-D. Only ever append.
    constexpr int numSyncGroups = 4;

    inline const char* syncGroupName (int g) noexcept
    {
        static constexpr const char* names[numSyncGroups + 1] = { "Off", "A", "B", "C", "D" };
        return names[g >= 0 && g <= numSyncGroups ? g : 0];
    }

    // A chord one member played.
    struct SyncChord
    {
        SmartChord chord;
        double ppq = 0.0;                 // when it changed (the sender's musical time)
        bool playing = false;             // the sender's transport was running
        uint32_t sender = 0;
    };

    class SharedSession
    {
    public:
        static constexpr int ringSize = 64;

        enum class Read { Ready, Pending, Gone };

        // The session every instance in this process shares.
        static SharedSession& process() noexcept
        {
            static SharedSession session;
            return session;
        }

        // A new member's id (never 0).
        uint32_t join() noexcept { return nextMember.fetch_add (1, std::memory_order_relaxed) + 1; }

        static bool validGroup (int group) noexcept { return group >= 1 && group <= numSyncGroups; }

        //======================================================================
        // Chords (audio threads).

        void publish (int group, const SyncChord& c) noexcept
        {
            if (! validGroup (group))
                return;
            Group& g = groups[(size_t) (group - 1)];
            const uint64_t i = g.head.fetch_add (1, std::memory_order_acq_rel);
            Slot& s = g.ring[(size_t) (i % ringSize)];
            std::array<uint64_t, words> w {};
            pack (c, w);
            w[words - 1] = checksum (i, w);
            s.seq.store (2 * i + 1, std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_release);
            for (int k = 0; k < words; ++k)
                s.w[(size_t) k].store (w[(size_t) k], std::memory_order_relaxed);
            s.seq.store (2 * i + 2, std::memory_order_release);
        }

        // How many chords the group has had (the next index to be written).
        uint64_t head (int group) const noexcept
        {
            return validGroup (group) ? groups[(size_t) (group - 1)].head.load (std::memory_order_acquire) : 0;
        }

        // Chord `index` of the group: Ready (copied to `out`), Pending (still being written) or
        // Gone (overwritten by a newer one).
        Read read (int group, uint64_t index, SyncChord& out) const noexcept
        {
            if (! validGroup (group))
                return Read::Gone;
            const Slot& s = groups[(size_t) (group - 1)].ring[(size_t) (index % ringSize)];
            const uint64_t want = 2 * index + 2;
            const uint64_t before = s.seq.load (std::memory_order_acquire);
            if (before != want)
                return before < want ? Read::Pending : Read::Gone;
            std::array<uint64_t, words> w {};
            for (int k = 0; k < words; ++k)
                w[(size_t) k] = s.w[(size_t) k].load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (s.seq.load (std::memory_order_relaxed) != before || w[words - 1] != checksum (index, w))
                return Read::Gone;
            unpack (w, out);
            return Read::Ready;
        }

        //======================================================================
        // Key and Scale (message threads).

        struct KeyScale
        {
            bool valid = false;
            int key = 0, scale = 0;
            uint32_t version = 0;
        };

        KeyScale keyScale (int group) const noexcept
        {
            KeyScale k;
            if (! validGroup (group))
                return k;
            const uint64_t v = groups[(size_t) (group - 1)].keyScale.load (std::memory_order_acquire);
            k.version = (uint32_t) (v >> 32);
            k.valid = k.version != 0;
            k.key = (int) (v & 0xff);
            k.scale = (int) ((v >> 8) & 0xff);
            return k;
        }

        // Makes `key` and `scale` the group's; returns the new version.
        uint32_t publishKeyScale (int group, int key, int scale) noexcept
        {
            if (! validGroup (group))
                return 0;
            auto& slot = groups[(size_t) (group - 1)].keyScale;
            uint64_t old = slot.load (std::memory_order_relaxed), next = 0;
            do
            {
                uint32_t version = (uint32_t) (old >> 32) + 1;
                version = version == 0 ? 1 : version;
                next = ((uint64_t) version << 32) | ((uint64_t) (scale & 0xff) << 8) | (uint64_t) (key & 0xff);
            }
            while (! slot.compare_exchange_weak (old, next, std::memory_order_acq_rel, std::memory_order_relaxed));
            return (uint32_t) (next >> 32);
        }

    private:
        static constexpr int words = 4;   // three of chord, one checksum

        static uint64_t mix (uint64_t h, uint64_t x) noexcept
        {
            h ^= x + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h ^= h >> 31;
            h *= 0xbf58476d1ce4e5b9ull;
            return h ^ (h >> 29);
        }

        static uint64_t checksum (uint64_t index, const std::array<uint64_t, words>& w) noexcept
        {
            uint64_t h = mix (0x2545f4914f6cdd1dull, index);
            for (int k = 0; k < words - 1; ++k)
                h = mix (h, w[(size_t) k]);
            return h;
        }

        // w0: ppq (the double's bits); w1: sender, key, scale, degree, flags; w2: the seven steps,
        // 9 bits each (on, accidental + 2 in 3 bits, octave mask in 5).
        static void pack (const SyncChord& c, std::array<uint64_t, words>& w) noexcept
        {
            std::memcpy (&w[0], &c.ppq, sizeof (double));
            w[1] = (uint64_t) c.sender | ((uint64_t) (c.chord.key & 0xff) << 32) | ((uint64_t) ((int) c.chord.scale & 0xff) << 40)
                 | ((uint64_t) (c.chord.slot.degree & 0xff) << 48) | ((uint64_t) (c.playing ? 1 : 0) << 56);
            uint64_t steps = 0;
            for (int k = 0; k < numChordSteps; ++k)
            {
                const Step& s = c.chord.slot.steps[(size_t) k];
                const uint64_t bits = (uint64_t) (s.on ? 1 : 0) | ((uint64_t) ((s.accidental + 2) & 7) << 1)
                                    | ((uint64_t) (s.octaveMask & 0x1f) << 4);
                steps |= bits << (9 * k);
            }
            w[2] = steps;
        }

        static void unpack (const std::array<uint64_t, words>& w, SyncChord& c) noexcept
        {
            std::memcpy (&c.ppq, &w[0], sizeof (double));
            c.sender = (uint32_t) (w[1] & 0xffffffffu);
            c.chord.key = (int) ((w[1] >> 32) & 0xff) % numKeys;
            const int scale = (int) ((w[1] >> 40) & 0xff);
            c.chord.scale = (Scale) (scale < numScales ? scale : 0);
            c.chord.slot.degree = (int) ((w[1] >> 48) & 0xff) % 7;
            c.playing = ((w[1] >> 56) & 1) != 0;
            for (int k = 0; k < numChordSteps; ++k)
            {
                const uint64_t bits = (w[2] >> (9 * k)) & 0x1ff;
                Step& s = c.chord.slot.steps[(size_t) k];
                s.on = (bits & 1) != 0;
                s.accidental = (int8_t) ((int) ((bits >> 1) & 7) - 2);
                s.octaveMask = (uint8_t) ((bits >> 4) & 0x1f);
            }
        }

        struct Slot
        {
            std::atomic<uint64_t> seq { 0 };
            std::array<std::atomic<uint64_t>, words> w {};
        };

        struct Group
        {
            std::atomic<uint64_t> head { 0 };
            std::atomic<uint64_t> keyScale { 0 };   // version (32) | scale (8) | key (8); version 0 = none yet
            std::array<Slot, ringSize> ring {};
        };

        std::array<Group, numSyncGroups> groups {};
        std::atomic<uint32_t> nextMember { 0 };
    };

    // One member's reading position in its group's ring (one per engine, audio thread).
    class SyncReader
    {
    public:
        // Calls `f (const SyncChord&)` for every chord published in `group` since the last call
        // by another member than `self`, oldest first. On joining a group (or changing it) the
        // group's latest chord, if any, comes first. Group 0: nothing.
        template <class F>
        void poll (const SharedSession& session, int group, uint32_t self, F&& f) noexcept
        {
            if (group != current)
            {
                current = group;
                const uint64_t h = session.head (group);
                next = h > 0 ? h - 1 : 0;
            }
            if (! SharedSession::validGroup (group))
                return;

            const uint64_t h = session.head (group);
            if (h > next + SharedSession::ringSize)
                next = h - SharedSession::ringSize;
            while (next < h)
            {
                SyncChord c;
                const auto r = session.read (group, next, c);
                if (r == SharedSession::Read::Pending && ++pendingPolls <= maxPendingPolls)
                    break;                // still being written: next block (a writer that never
                                          // finishes, lapped by another, is skipped in the end)
                pendingPolls = 0;
                ++next;
                if (r == SharedSession::Read::Ready && c.sender != self)
                    f (c);
            }
        }

        int group() const noexcept { return current; }

    private:
        static constexpr int maxPendingPolls = 16;

        int current = 0;
        uint64_t next = 0;
        int pendingPolls = 0;
    };
}
