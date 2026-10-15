#include "MidiExport.h"
#include <algorithm>
#include <cmath>

ExportResult buildExport (const std::vector<RecEvent>& events, double startPpq, double latestPpq,
                          int timeSigNumerator, int timeSigDenominator, double bpm, int maxBars)
{
    ExportResult result;

    const double beatsPerBar = timeSigNumerator * 4.0 / timeSigDenominator;
    if (beatsPerBar <= 0.0 || bpm <= 0.0)
        return result;

    const auto firstBar = (int64_t) std::floor (startPpq / beatsPerBar + 1.0e-9);
    const auto endBar   = (int64_t) std::floor (latestPpq / beatsPerBar + 1.0e-9);
    if (endBar <= firstBar)
        return result;

    const auto windowFirstBar = std::max (firstBar, endBar - (int64_t) maxBars);
    const double windowStart = (double) windowFirstBar * beatsPerBar;
    const double windowEnd   = (double) endBar * beatsPerBar;
    result.bars = (int) (endBar - windowFirstBar);

    constexpr int ticksPerQuarter = 960;
    auto toTicks = [&] (double ppq) { return std::round ((ppq - windowStart) * ticksPerQuarter); };

    // Events arrive per block in processing order; sort them into time order, with
    // note-offs before note-ons at the same instant.
    std::vector<RecEvent> sorted;
    sorted.reserve (events.size());
    for (const auto& e : events)
        if (e.type != RecEvent::transportStart)
            sorted.push_back (e);

    std::stable_sort (sorted.begin(), sorted.end(), [] (const RecEvent& a, const RecEvent& b)
    {
        if (a.ppq != b.ppq) return a.ppq < b.ppq;
        return a.type == RecEvent::noteOff && b.type == RecEvent::noteOn;
    });

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Chordism Export"), 0.0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (timeSigNumerator, timeSigDenominator), 0.0);

    std::array<bool, 128> open {};

    for (const auto& e : sorted)
    {
        const int note = e.note & 0x7f;

        if (e.type == RecEvent::noteOn)
        {
            if (e.ppq < windowStart - 1.0e-9 || e.ppq >= windowEnd - 1.0e-9)
                continue;

            if (open[(size_t) note])
                seq.addEvent (juce::MidiMessage::noteOff (1, note), toTicks (e.ppq));

            seq.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) e.velocity), toTicks (e.ppq));
            open[(size_t) note] = true;
            ++result.notes;
        }
        else if (open[(size_t) note])
        {
            seq.addEvent (juce::MidiMessage::noteOff (1, note), toTicks (std::min (e.ppq, windowEnd)));
            open[(size_t) note] = false;
        }
    }

    for (int note = 0; note < 128; ++note)
        if (open[(size_t) note])
            seq.addEvent (juce::MidiMessage::noteOff (1, note), toTicks (windowEnd));

    seq.addEvent (juce::MidiMessage::endOfTrack(), toTicks (windowEnd));
    seq.updateMatchedPairs();

    result.file.setTicksPerQuarterNote (ticksPerQuarter);
    result.file.addTrack (seq);
    return result;
}

//==============================================================================
void ExportRecorder::push (const RecEvent& e) noexcept
{
    fifo.write (1).forEach ([&] (int index) { ring[(size_t) index] = e; });
}

void ExportRecorder::setPosition (double ppq, double bpm, int num, int den) noexcept
{
    latestPpq.store (ppq);
    latestBpm.store (bpm);
    sigNum.store (num);
    sigDen.store (den);
}

void ExportRecorder::drain()
{
    fifo.read (fifo.getNumReady()).forEach ([&] (int index)
    {
        const auto& e = ring[(size_t) index];

        if (e.type == RecEvent::transportStart)
        {
            recorded.clear();
            startPpq = e.ppq;
            haveStart = true;
            return;
        }

        recorded.push_back (e);
    });

    // Plenty for 32 bars of dense output; drop the oldest half if it ever overflows.
    if (recorded.size() > 200000)
        recorded.erase (recorded.begin(), recorded.begin() + (std::ptrdiff_t) (recorded.size() / 2));
}

ExportResult ExportRecorder::build() const
{
    if (! haveStart)
        return {};

    return buildExport (recorded, startPpq, latestPpq.load(), sigNum.load(), sigDen.load(), latestBpm.load());
}

int ExportRecorder::completedBars() const
{
    if (! haveStart)
        return 0;

    const double beatsPerBar = sigNum.load() * 4.0 / sigDen.load();
    const auto bars = std::floor (latestPpq.load() / beatsPerBar + 1.0e-9)
                    - std::floor (startPpq / beatsPerBar + 1.0e-9);
    return (int) std::clamp (bars, 0.0, 32.0);
}

juce::File ExportRecorder::writeTempFile() const
{
    auto result = build();
    if (result.bars == 0)
        return {};

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ChordismSpike");
    dir.createDirectory();
    auto file = dir.getNonexistentChildFile ("Chordism Export", ".mid", false);

    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! result.file.writeTo (out, 1))
        return {};

    return file;
}
