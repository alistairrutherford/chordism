// Export (PLAN.md §4.6): the generated MIDI since playback started, for the drag-out handle.
//
// The audio thread records every note-on and note-off Chordism sends while the host plays, in
// beats since the bar line at or before the start of playback (a *take*). Time runs on like a
// tape: a loop wrap or a locate doesn't move it back, so a take is what was heard, in order.
// The note-offs of the stop (ChordismEngine ends every note when the transport stops) close
// the take. The next start of playback begins a new take.
//
// Events go into a preallocated ring of `capacity` events (several times what 32 dense bars
// need); the oldest are overwritten. The message thread copies the take out (`snapshot`) with
// no locks: the take's header is a seqlock, the events are atomics stamped by a running count,
// so a copy never waits for the audio thread and drops anything overwritten while it copied.
//
// `exportBars` turns a take into a MIDI song: only completed bars, the last `maxBars` (32) of
// them; notes that started before the first exported bar are left out, and notes still
// sounding at the end are cut at the last bar line. Plain C++; record() doesn't allocate.
#pragma once

#include "../engine/MidiEvents.h"
#include "../engine/Transport.h"
#include "MidiFile.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>

namespace chordism
{
    struct RecordedEvent
    {
        double beat = 0.0;                // since the take's first bar line
        uint8_t status = 0, data1 = 0, data2 = 0;

        bool isNoteOn() const noexcept { return (status & 0xf0) == 0x90 && data2 > 0; }
        bool isNoteOff() const noexcept { return (status & 0xf0) == 0x80 || ((status & 0xf0) == 0x90 && data2 == 0); }
    };

    struct TakeInfo
    {
        uint32_t take = 0;                // 0 = nothing recorded yet
        bool recording = false;           // the host is playing it now
        double startBeat = 0.0;           // where playback started, after the first bar line
        double endBeat = 0.0;             // how far it has got
        double barLength = 4.0;           // quarter notes
        double bpm = 120.0;
        int numerator = 4, denominator = 4;
        uint64_t firstEvent = 0;          // the running count of its first event

        int completedBars() const noexcept
        {
            return take == 0 ? 0 : (int) std::floor (endBeat / (barLength > 0.0 ? barLength : 4.0) + 1.0e-9);
        }
    };

    class ExportRecorder
    {
    public:
        static constexpr int capacity = 1 << 15;
        static constexpr int maxBars = 32;

        ExportRecorder() : ring (std::make_unique<Slot[]> ((size_t) capacity)) {}

        // Audio thread, once per block after the engine: `out` is what Chordism sent this block.
        void record (const Transport& t, int numSamples, const MidiEvent* out, int numEvents) noexcept
        {
            if (t.playing && ! recording)
                begin (t);
            if (! recording)
                return;

            const double spq = t.samplesPerBeat();
            for (int i = 0; i < numEvents; ++i)
            {
                const MidiEvent& e = out[i];
                if (! e.isNoteOn() && ! e.isNoteOff())
                    continue;
                const double beat = t.playing ? position + (e.sample > 0 ? e.sample : 0) / spq : position;
                push (beat, e);
            }

            if (! t.playing)
            {
                recording = false;        // the stop's note-offs, at the end of the take
                publish();
                return;
            }
            position += (numSamples > 0 ? numSamples : 0) / spq;
            publish();
        }

        // Any thread: the take's header (cheap; for enabling the handle).
        TakeInfo info() const noexcept
        {
            TakeInfo h;
            for (int attempt = 0; attempt < 64; ++attempt)
            {
                const uint32_t s0 = seq.load (std::memory_order_acquire);
                if ((s0 & 1u) != 0)
                    continue;
                h.take = hTake.load (std::memory_order_relaxed);
                h.recording = hRecording.load (std::memory_order_relaxed);
                h.startBeat = hStart.load (std::memory_order_relaxed);
                h.endBeat = hEnd.load (std::memory_order_relaxed);
                h.barLength = hBar.load (std::memory_order_relaxed);
                h.bpm = hBpm.load (std::memory_order_relaxed);
                h.numerator = hNum.load (std::memory_order_relaxed);
                h.denominator = hDen.load (std::memory_order_relaxed);
                h.firstEvent = hFirst.load (std::memory_order_relaxed);
                std::atomic_thread_fence (std::memory_order_acquire);
                if (seq.load (std::memory_order_relaxed) == s0)
                    return h;
            }
            return {};
        }

        // Message thread: the current take and its events (oldest first). False if there is no
        // take, or it changed while copying (try again).
        bool snapshot (TakeInfo& out, std::vector<RecordedEvent>& events) const
        {
            events.clear();
            out = info();
            if (out.take == 0)
                return false;
            const uint64_t written = count.load (std::memory_order_acquire);
            uint64_t from = out.firstEvent;
            if (written > (uint64_t) capacity && from < written - (uint64_t) capacity)
                from = written - (uint64_t) capacity;
            events.reserve ((size_t) (written - from));
            for (uint64_t i = from; i < written; ++i)
            {
                const Slot& s = ring[(size_t) (i % (uint64_t) capacity)];
                RecordedEvent e;
                const uint64_t bits = s.beat.load (std::memory_order_relaxed);
                std::memcpy (&e.beat, &bits, sizeof (double));
                const uint32_t m = s.message.load (std::memory_order_relaxed);
                e.status = (uint8_t) (m >> 16);
                e.data1 = (uint8_t) (m >> 8);
                e.data2 = (uint8_t) m;
                events.push_back (e);
            }
            // Anything the audio thread overwrote meanwhile is dropped from the front.
            const uint64_t after = count.load (std::memory_order_acquire);
            if (after > (uint64_t) capacity && after - (uint64_t) capacity > from)
            {
                const auto lost = (size_t) std::min<uint64_t> (after - (uint64_t) capacity - from, events.size());
                events.erase (events.begin(), events.begin() + (long) lost);
            }
            return info().take == out.take;
        }

    private:
        struct Slot
        {
            std::atomic<uint64_t> beat { 0 };       // the double's bits
            std::atomic<uint32_t> message { 0 };    // status, data1, data2
        };

        void begin (const Transport& t) noexcept
        {
            barLength = t.barLength();
            const double origin = t.hasBarStart && std::isfinite (t.barStartPpq)
                                ? t.barStartPpq
                                : std::floor (t.ppq / barLength + 1.0e-9) * barLength;
            position = t.ppq - origin;
            position = position < 0.0 ? 0.0 : position;
            startBeat = position;
            bpm = t.bpm;
            numerator = t.numerator;
            denominator = t.denominator;
            firstEvent = count.load (std::memory_order_relaxed);
            recording = true;
            ++take;
        }

        void push (double beat, const MidiEvent& e) noexcept
        {
            const uint64_t i = count.load (std::memory_order_relaxed);
            Slot& s = ring[(size_t) (i % (uint64_t) capacity)];
            uint64_t bits = 0;
            std::memcpy (&bits, &beat, sizeof (double));
            s.beat.store (bits, std::memory_order_relaxed);
            s.message.store (((uint32_t) e.data[0] << 16) | ((uint32_t) e.data[1] << 8) | e.data[2], std::memory_order_relaxed);
            count.store (i + 1, std::memory_order_release);
        }

        void publish() noexcept
        {
            seq.fetch_add (1, std::memory_order_acq_rel);
            std::atomic_thread_fence (std::memory_order_release);
            hTake.store (take, std::memory_order_relaxed);
            hRecording.store (recording, std::memory_order_relaxed);
            hStart.store (startBeat, std::memory_order_relaxed);
            hEnd.store (position, std::memory_order_relaxed);
            hBar.store (barLength, std::memory_order_relaxed);
            hBpm.store (bpm, std::memory_order_relaxed);
            hNum.store (numerator, std::memory_order_relaxed);
            hDen.store (denominator, std::memory_order_relaxed);
            hFirst.store (firstEvent, std::memory_order_relaxed);
            seq.fetch_add (1, std::memory_order_release);
        }

        // Audio thread only.
        bool recording = false;
        uint32_t take = 0;
        double position = 0.0, startBeat = 0.0, barLength = 4.0, bpm = 120.0;
        int numerator = 4, denominator = 4;
        uint64_t firstEvent = 0;

        std::unique_ptr<Slot[]> ring;
        std::atomic<uint64_t> count { 0 };
        std::atomic<uint32_t> seq { 0 };
        std::atomic<uint32_t> hTake { 0 };
        std::atomic<bool> hRecording { false };
        std::atomic<double> hStart { 0.0 }, hEnd { 0.0 }, hBar { 4.0 }, hBpm { 120.0 };
        std::atomic<int> hNum { 4 }, hDen { 4 };
        std::atomic<uint64_t> hFirst { 0 };
    };

    // The last `maxBars` completed bars of a take as a MIDI song (`ppq` ticks per quarter),
    // starting at its first bar line. Empty if no bar has completed.
    inline MidiSong exportBars (const TakeInfo& take, const std::vector<RecordedEvent>& events,
                                int maxBars = ExportRecorder::maxBars, int ppq = 960)
    {
        MidiSong song;
        song.ppq = ppq;
        song.bpm = take.bpm;
        song.numerator = take.numerator;
        song.denominator = take.denominator;
        song.name = "Chordism";

        const int bars = take.completedBars();
        if (bars <= 0)
            return song;
        const int first = bars > maxBars ? bars - maxBars : 0;
        const double from = first * take.barLength, to = bars * take.barLength;
        const auto tick = [&] (double beat) { return (int64_t) std::llround ((beat - from) * ppq); };

        std::vector<std::vector<size_t>> open (16 * 128);   // (channel, pitch) → open notes, oldest first
        std::vector<double> starts;
        for (const auto& e : events)
        {
            const size_t slot = (size_t) (e.status & 15) * 128 + (e.data1 & 127);
            if (e.isNoteOn())
            {
                open[slot].push_back (starts.size());
                starts.push_back (e.beat);
                song.notes.push_back ({ 0, 0, (uint8_t) (e.data1 & 127), (uint8_t) e.data2, (uint8_t) (e.status & 15) });
                song.notes.back().end = -1;
            }
            else if (e.isNoteOff() && ! open[slot].empty())
            {
                const size_t n = open[slot].front();
                open[slot].erase (open[slot].begin());
                song.notes[n].end = std::min (tick (e.beat), tick (to));
            }
        }

        // In ticks, so a note a fraction of a sample before a bar line (the rounding of its
        // sample) belongs to the bar it starts.
        const int64_t last = tick (to);
        std::vector<MidiNote> kept;
        for (size_t n = 0; n < song.notes.size(); ++n)
        {
            const int64_t start = tick (starts[n]);
            if (start < 0 || start >= last)
                continue;
            MidiNote note = song.notes[n];
            note.start = start;
            if (note.end < 0)
                note.end = last;          // still sounding: cut at the last bar line
            if (note.end <= note.start)
                note.end = note.start + 1;
            kept.push_back (note);
        }
        std::stable_sort (kept.begin(), kept.end(), [] (const MidiNote& a, const MidiNote& b)
        {
            return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
        });
        song.notes = std::move (kept);
        return song;
    }
}
