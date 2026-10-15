#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

// Spike (3): record the MIDI the plugin generates, in musical time, and hand the
// completed bars to the DAW as a .mid file by external drag and drop.

struct RecEvent
{
    enum Type : uint8_t { noteOn, noteOff, transportStart };

    double ppq = 0.0;
    Type type = noteOn;
    uint8_t note = 0, velocity = 0;
};

struct ExportResult
{
    juce::MidiFile file;
    int bars = 0;
    int notes = 0;
};

// Builds a MIDI file from the events recorded since the most recent transport
// start. Only bars that have fully played (end ≤ latestPpq) are included, at most
// maxBars of them (the most recent ones). Notes still sounding at the end of the
// window are cut there; notes that started before it are dropped.
ExportResult buildExport (const std::vector<RecEvent>& events, double startPpq, double latestPpq,
                          int timeSigNumerator, int timeSigDenominator, double bpm, int maxBars = 32);

// Audio thread pushes; message thread drains and exports.
class ExportRecorder
{
public:
    // ---- audio thread ----
    void push (const RecEvent& e) noexcept;
    void setPosition (double ppq, double bpm, int num, int den) noexcept;

    // ---- message thread ----
    void drain();
    int completedBars() const;
    ExportResult build() const;
    juce::File writeTempFile() const;   // returns {} when there is nothing to export

private:
    static constexpr int capacity = 8192;
    juce::AbstractFifo fifo { capacity };
    std::array<RecEvent, capacity> ring;

    std::atomic<double> latestPpq { 0.0 }, latestBpm { 120.0 };
    std::atomic<int> sigNum { 4 }, sigDen { 4 };

    std::vector<RecEvent> recorded;     // since the last transport start
    double startPpq = 0.0;
    bool haveStart = false;
};
