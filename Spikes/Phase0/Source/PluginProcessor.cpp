#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
    constexpr int progression[4] = { 0, 3, 4, 5 };      // I IV V vi

    int whiteKeyDegree (int pitchClass) noexcept
    {
        switch (pitchClass)
        {
            case 0:  return 0;
            case 2:  return 1;
            case 4:  return 2;
            case 5:  return 3;
            case 7:  return 4;
            case 9:  return 5;
            case 11: return 6;
            default: return -1;
        }
    }
}

SpikeProcessor::SpikeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      instanceId (SharedSession::get().nextInstanceId.fetch_add (1))
{
    SharedSession::get().instanceCount.fetch_add (1);

    for (auto& chord : heldChords)
        chord.fill (-1);

    startTimerHz (30);
}

SpikeProcessor::~SpikeProcessor()
{
    stopTimer();
    SharedSession::get().instanceCount.fetch_sub (1);
}

void SpikeProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    generated.ensureSize (8192);

    outCount.fill (0);
    inputHeld.fill (false);
    for (auto& chord : heldChords)
        chord.fill (-1);
    for (auto& p : pendingOffs)
        p = {};

    lastBeatFired = std::numeric_limits<int64_t>::min();
    wasPlaying = false;

    hosted.prepare (newSampleRate, samplesPerBlock);
}

void SpikeProcessor::releaseResources()
{
    hosted.release();
}

bool SpikeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled()
        && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

//==============================================================================
void SpikeProcessor::setKeyFromUi (int key)
{
    key = juce::jlimit (0, 11, key);
    localKey.store (key);

    if (syncKey.load())
        SharedSession::get().setKey (key, instanceId);
}

int SpikeProcessor::getEffectiveKey() const noexcept
{
    return syncKey.load() ? SharedSession::get().key.load() : localKey.load();
}

int SpikeProcessor::soundingNoteCount() const noexcept
{
    int n = 0;
    for (auto c : outCount)
        n += c > 0 ? 1 : 0;
    return n;
}

double SpikeProcessor::sampleToPpq (int sample) const noexcept
{
    return blockPpq + sample / samplesPerBeat;
}

void SpikeProcessor::noteOn (juce::MidiBuffer& out, int note, int velocity, int sample)
{
    if (note < 0 || note > 127)
        return;

    if (outCount[(size_t) note]++ == 0)
    {
        out.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, velocity)), sample);
        outputNoteOns.fetch_add (1);
        lastOutputNote.store (note);

        if (blockPlaying)
            recorder.push ({ sampleToPpq (sample), RecEvent::noteOn, (uint8_t) note, (uint8_t) velocity });
    }
}

void SpikeProcessor::noteOff (juce::MidiBuffer& out, int note, int sample)
{
    if (note < 0 || note > 127 || outCount[(size_t) note] == 0)
        return;

    if (--outCount[(size_t) note] == 0)
    {
        out.addEvent (juce::MidiMessage::noteOff (1, note), sample);

        if (blockPlaying)
            recorder.push ({ sampleToPpq (sample), RecEvent::noteOff, (uint8_t) note, 0 });
    }
}

void SpikeProcessor::scheduleOff (int note, int samplesFromBlockStart)
{
    for (auto& p : pendingOffs)
    {
        if (p.note < 0)
        {
            p = { note, samplesFromBlockStart };
            return;
        }
    }

    // Queue full: release at the end of this block rather than leak a note.
    noteOff (generated, note, 0);
}

void SpikeProcessor::flushPendingOffs (juce::MidiBuffer& out, int sample)
{
    for (auto& p : pendingOffs)
    {
        if (p.note >= 0)
            noteOff (out, p.note, sample);
        p = {};
    }
}

void SpikeProcessor::playTriad (juce::MidiBuffer& out, int rootDegree, int octaveBase, int key, int velocity,
                                int sample, std::array<int, 3>* notesOut, int lengthSamples)
{
    std::array<int, 3> notes { -1, -1, -1 };

    for (int i = 0; i < 3; ++i)
    {
        const int index = rootDegree + 2 * i;
        const int note = octaveBase + key + majorScale[index % 7] + 12 * (index / 7);
        if (note < 0 || note > 127)
            continue;

        notes[(size_t) i] = note;
        noteOn (out, note, velocity, sample);

        if (lengthSamples > 0)
            scheduleOff (note, sample + lengthSamples);
    }

    if (notesOut != nullptr)
        *notesOut = notes;
}

//==============================================================================
void SpikeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    generated.clear();

    // ---- transport ----
    bool isPlaying = false;
    double ppq = lastBlockEndPpq, bpm = currentBpm.load();
    int num = 4, den = 4;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            isPlaying = pos->getIsPlaying();

            if (auto b = pos->getBpm(); b.hasValue() && std::isfinite (*b) && *b > 0.0)
                bpm = *b;
            if (auto p = pos->getPpqPosition(); p.hasValue() && std::isfinite (*p))
                ppq = *p;
            if (auto ts = pos->getTimeSignature(); ts.hasValue() && ts->numerator > 0 && ts->denominator > 0)
            {
                num = ts->numerator;
                den = ts->denominator;
            }
        }
    }

    samplesPerBeat = sampleRate * 60.0 / bpm;
    blockPpq = ppq;

    if (wasPlaying && ! isPlaying)
    {
        flushPendingOffs (generated, 0);   // recorded while blockPlaying is still true
        lastBeatFired = std::numeric_limits<int64_t>::min();
    }

    blockPlaying = isPlaying;

    if (isPlaying && ! wasPlaying)
    {
        recorder.push ({ ppq, RecEvent::transportStart, 0, 0 });
        lastBeatFired = std::numeric_limits<int64_t>::min();
    }
    else if (isPlaying && ppq < lastBlockEndPpq - 1.0e-6)
    {
        lastBeatFired = std::numeric_limits<int64_t>::min();   // loop wrap or jump back
    }

    wasPlaying = isPlaying;
    const int key = getEffectiveKey();

    // ---- input: white keys → diatonic triads, everything else passes through ----
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int pos = juce::jlimit (0, juce::jmax (0, numSamples - 1), meta.samplePosition);

        if (msg.isNoteOn())
        {
            const int n = msg.getNoteNumber();
            lastInputNote.store (n);

            if (inputHeld[(size_t) n])
                for (auto out : heldChords[(size_t) n])
                    noteOff (generated, out, pos);

            const int degree = whiteKeyDegree (n % 12);
            if (degree >= 0)
            {
                playTriad (generated, degree, n - n % 12, key, msg.getVelocity(), pos, &heldChords[(size_t) n], 0);
            }
            else
            {
                noteOn (generated, n, msg.getVelocity(), pos);
                heldChords[(size_t) n] = { n, -1, -1 };
            }

            inputHeld[(size_t) n] = true;
        }
        else if (msg.isNoteOff())
        {
            const int n = msg.getNoteNumber();
            if (inputHeld[(size_t) n])
            {
                for (auto out : heldChords[(size_t) n])
                    noteOff (generated, out, pos);

                heldChords[(size_t) n].fill (-1);
                inputHeld[(size_t) n] = false;
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (int n = 0; n < 128; ++n)
            {
                if (! inputHeld[(size_t) n])
                    continue;

                for (auto out : heldChords[(size_t) n])
                    noteOff (generated, out, pos);

                heldChords[(size_t) n].fill (-1);
                inputHeld[(size_t) n] = false;
            }

            generated.addEvent (msg, pos);
        }
        else
        {
            generated.addEvent (msg, pos);
        }
    }

    // ---- test pattern: I IV V vi, one chord per bar, struck on every beat ----
    const double endPpq = ppq + numSamples / samplesPerBeat;

    if (testPattern.load() && isPlaying)
    {
        const double beatsPerBar = num * 4.0 / den;

        for (auto beat = (int64_t) std::ceil (ppq - 1.0e-7); (double) beat < endPpq - 1.0e-9; ++beat)
        {
            if (beat <= lastBeatFired)
                continue;

            lastBeatFired = beat;
            const int offset = juce::jlimit (0, numSamples - 1, (int) std::llround (((double) beat - ppq) * samplesPerBeat));
            const auto bar = (int64_t) std::floor ((double) beat / beatsPerBar + 1.0e-9);
            const int degree = progression[(size_t) (((bar % 4) + 4) % 4)];

            playTriad (generated, degree, 48, key, 100, offset, nullptr, (int) (samplesPerBeat * 0.5));
        }
    }

    // ---- note-offs that fall in this block ----
    for (auto& p : pendingOffs)
    {
        if (p.note < 0)
            continue;

        if (p.samplesLeft < numSamples)
        {
            noteOff (generated, p.note, juce::jmax (0, p.samplesLeft));
            p = {};
        }
        else
        {
            p.samplesLeft -= numSamples;
        }
    }

    lastBlockEndPpq = isPlaying ? endPpq : ppq;
    playing.store (isPlaying);
    currentBpm.store (bpm);
    currentPpq.store (ppq);

    if (isPlaying)
        recorder.setPosition (endPpq, bpm, num, den);

    // Copy rather than swap: swapping would hand `generated` the host's (possibly
    // small) buffer and make the next block allocate.
    midi.clear();
    midi.addEvents (generated, 0, -1, 0);

    // Spike (2): the hosted instrument plays the generated MIDI.
    hosted.process (buffer, midi);
}

//==============================================================================
void SpikeProcessor::timerCallback()
{
    recorder.drain();
}

juce::AudioProcessorEditor* SpikeProcessor::createEditor()
{
    return new SpikeEditor (*this);
}

void SpikeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("ChordismSpike");
    xml.setAttribute ("syncKey", syncKey.load());
    xml.setAttribute ("key", getEffectiveKey());
    xml.setAttribute ("localKey", localKey.load());
    xml.setAttribute ("testPattern", testPattern.load());

    if (auto hostedXml = hosted.saveState())
        xml.addChildElement (hostedXml.release());

    copyXmlToBinary (xml, destData);
}

void SpikeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("ChordismSpike"))
        return;

    syncKey.store (xml->getBoolAttribute ("syncKey", true));
    localKey.store (juce::jlimit (0, 11, xml->getIntAttribute ("localKey", 0)));
    testPattern.store (xml->getBoolAttribute ("testPattern", false));

    if (syncKey.load())
        SharedSession::get().setKey (juce::jlimit (0, 11, xml->getIntAttribute ("key", 0)), instanceId);

    if (auto* hostedXml = xml->getChildByName ("Hosted"))
    {
        auto copy = std::make_shared<juce::XmlElement> (*hostedXml);
        auto restore = [this, copy] { hosted.restoreState (*copy); };

        if (juce::MessageManager::existsAndIsCurrentThread())
            restore();
        else
            juce::MessageManager::callAsync ([safe = juce::WeakReference<SpikeProcessor> (this), restore]
            {
                if (safe != nullptr)
                    restore();
            });
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpikeProcessor();
}
