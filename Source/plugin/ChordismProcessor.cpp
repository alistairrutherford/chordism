#include "ChordismProcessor.h"
#include "../ui/ChordismEditor.h"
#include <cmath>

namespace chordism
{
    namespace
    {
        // Phase 3 saved each sequencer step as a bool ("seq1Step1"…); Phase 4 keeps one mask
        // per pattern ("seq1Steps", Parameters.h). Folds the old steps of a saved state into
        // the masks, so a set saved before Phase 4 plays the same patterns.
        void migrateSeqSteps (juce::ValueTree& tree)
        {
            for (int p = 0; p < numSeqPatterns; ++p)
            {
                int mask = 0;
                bool found = false;
                for (int s = 0; s < numSeqSteps; ++s)
                {
                    auto step = tree.getChildWithProperty ("id", params::seqStep (p, s));
                    if (! step.isValid())
                        continue;
                    found = true;
                    if ((double) step.getProperty ("value") > 0.5)
                        mask |= 1 << s;
                    tree.removeChild (step, nullptr);
                }
                if (found && ! tree.getChildWithProperty ("id", params::seqSteps (p)).isValid())
                {
                    juce::ValueTree param ("PARAM");
                    param.setProperty ("id", params::seqSteps (p), nullptr);
                    param.setProperty ("value", mask, nullptr);
                    tree.appendChild (param, nullptr);
                }
            }
        }
    }

    juce::AudioProcessor::BusesProperties ChordismProcessor::makeBuses()
    {
       #if JucePlugin_IsMidiEffect
        return {};
       #else
        // An instrument with a silent stereo output: hosts route MIDI-only instruments more
        // reliably when there's an audio bus.
        return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
       #endif
    }

    ChordismProcessor::ChordismProcessor()
        : AudioProcessor (makeBuses()),
          state (*this, nullptr, "Chordism", params::createLayout()),
          snapshot (state)
    {
        generated.ensureSize (output.capacity * 4);
        startTimerHz (20);
    }

    ChordismProcessor::~ChordismProcessor()
    {
        stopTimer();
        port.close();   // releases any notes the port's receiver still holds
    }

    void ChordismProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        preparedSampleRate.store (sampleRate);
        preparedBlockSize.store (samplesPerBlock);
        generated.ensureSize (output.capacity * 4);
        engine.requestReset();
    }

    void ChordismProcessor::releaseResources()
    {
        engine.requestReset();
    }

    bool ChordismProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
       #if JucePlugin_IsMidiEffect
        juce::ignoreUnused (layouts);
        return true;
       #else
        const auto out = layouts.getMainOutputChannelSet();
        return layouts.getMainInputChannelSet().isDisabled()
            && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
       #endif
    }

    Transport ChordismProcessor::readTransport (int64_t& blockNs)
    {
        Transport t = lastTransport;
        t.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : preparedSampleRate.load();
        t.playing = false;
        t.looping = false;
        t.hasBarStart = false;

        blockNs = VirtualMidiOut::nowNs();
        bool hostClock = false;

        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                t.playing = pos->getIsPlaying();
                if (auto b = pos->getBpm(); b.hasValue() && std::isfinite (*b) && *b > 0.0)
                    t.bpm = *b;
                if (auto p = pos->getPpqPosition(); p.hasValue() && std::isfinite (*p))
                    t.ppq = *p;
                if (auto ts = pos->getTimeSignature(); ts.hasValue() && ts->numerator > 0 && ts->denominator > 0)
                {
                    t.numerator = ts->numerator;
                    t.denominator = ts->denominator;
                }
                // Loop points and the bar position, for the effects' clock (Source/core/engine/Clock.h).
                if (auto loop = pos->getLoopPoints(); loop.hasValue() && std::isfinite (loop->ppqStart) && std::isfinite (loop->ppqEnd))
                {
                    t.looping = pos->getIsLooping() && loop->ppqEnd > loop->ppqStart;
                    t.loopStart = loop->ppqStart;
                    t.loopEnd = loop->ppqEnd;
                }
                if (auto bar = pos->getPpqPositionOfLastBarStart(); bar.hasValue() && std::isfinite (*bar))
                {
                    t.hasBarStart = true;
                    t.barStartPpq = *bar;
                }

                // The port's timestamps use the host's time for this block when it's on the mach
                // clock (within 200 ms of now), else the time of processing.
                if (auto h = pos->getHostTimeNs(); h.hasValue() && std::llabs ((int64_t) *h - blockNs) < 200'000'000)
                {
                    blockNs = (int64_t) *h;
                    hostClock = true;
                }
            }
        }

        usingHostClock.store (hostClock, std::memory_order_relaxed);
        return t;
    }

    void ChordismProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        run (buffer, midi, false);
    }

    void ChordismProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        run (buffer, midi, true);
    }

    void ChordismProcessor::run (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed)
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();
        const int numSamples = buffer.getNumSamples();

        int64_t blockNs = 0;
        const Transport transport = readTransport (blockNs);

        // The editor snapshot first: a state restore publishes it after setting the parameters,
        // so reading the parameters second never pairs restored edits with the old group.
        const ChordEdits& edits = chordEditor.read();
        EngineSettings settings = snapshot.engineSettings();
        if (bypassed)
        {
            // Host bypass behaves like Enable off: generated notes end, input passes through.
            settings.enabled = false;
            settings.testPattern = false;
        }

        // Short messages go through the engine; anything longer (SysEx) passes straight on.
        input.clear();
        generated.clear();
        for (const auto meta : midi)
        {
            if (meta.numBytes >= 1 && meta.numBytes <= 3)
            {
                MidiEvent e;
                e.sample = meta.samplePosition;
                e.size = (uint8_t) meta.numBytes;
                for (int i = 0; i < meta.numBytes; ++i)
                    e.data[i] = meta.data[i];
                input.push (e);
            }
            else
            {
                generated.addEvent (meta.data, meta.numBytes, meta.samplePosition);
            }
        }

        output.clear();
        engine.process (settings, edits, transport, input.begin(), input.size(), numSamples, output);

        const bool toPort = port.isOpen() && snapshot.portEnabled();
        const PortTiming timing { transport.sampleRate, snapshot.portCompensation(), snapshot.portTrim() };

        for (const auto& e : output)
        {
            generated.addEvent (e.data, e.size, e.sample);
            if (toPort)
                port.push (e.data, e.size, timing.stampNs (blockNs, e.sample));
        }

        // Copy rather than swap: swapping would hand `generated` the host's (possibly small)
        // buffer, and the next block would allocate.
        midi.clear();
        midi.addEvents (generated, 0, -1, 0);

        lastTransport = transport;
        if (transport.playing)
            lastTransport.ppq = transport.ppq + numSamples / transport.samplesPerBeat();

        lastChordSlot.store (engine.getLastChordSlot(), std::memory_order_relaxed);
        soundingNotes.store (engine.getVoices().soundingNotes(), std::memory_order_relaxed);
        transportPlaying.store (transport.playing, std::memory_order_relaxed);

        ChordDisplay now { engine.getChords(), engine.getChordContext(), engine.getLastChordSlot(), engine.getActiveShifter() };
        if (! (now == lastDisplay))
        {
            lastDisplay = now;
            display.publish (now);
        }
    }

    //==========================================================================
    void ChordismProcessor::timerCallback()
    {
        // Choosing a Chord Group resets the Chord Editor's edits.
        chordEditor.sync (snapshot.chordGroup(), engine.getEditsDiscarded(), engine.getDiscardedVersion());

        const bool want = snapshot.portEnabled();
        if (want && ! port.isOpen() && port.getLastError() == 0)
        {
            port.open (savedPortIndex);
            if (port.isOpen())
                savedPortIndex = port.getIndex();
        }
        else if (! want && port.isOpen())
        {
            savedPortIndex = port.getIndex();
            port.close();
        }
    }

    juce::String ChordismProcessor::getPortStatus() const
    {
        if (! snapshot.portEnabled())
            return "off";
        if (! port.isOpen())
            return port.getLastError() != 0 ? "error " + juce::String (port.getLastError()) : juce::String ("opening...");

        // Live records port input one buffer late; only the fixed part is compensated (§3).
        const double sr = preparedSampleRate.load();
        const int bs = preparedBlockSize.load();
        const PortTiming t { sr, snapshot.portCompensation(), snapshot.portTrim() };
        return port.getName() + "  (" + juce::String ((int) port.getNumSent()) + " sent, stamped "
             + juce::String ((double) t.leadNs() / 1.0e6, 2) + " ms early, "
             + (usingHostClock.load() ? "host clock" : "processing clock") + "; Live records it 1 buffer late = "
             + juce::String (PortTiming::expectedLateMs (sr, bs), 1) + " ms at " + juce::String (bs) + " @ "
             + juce::String (sr / 1000.0, 1) + " kHz)";
    }

    juce::AudioProcessorEditor* ChordismProcessor::createEditor()
    {
        return new ChordismEditor (*this);
    }

    void ChordismProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto tree = state.copyState();
        tree.appendChild (chordEditor.copyTree(), nullptr);
        tree.setProperty ("portIndex", port.isOpen() ? port.getIndex() : savedPortIndex, nullptr);
        tree.setProperty ("version", JucePlugin_VersionString, nullptr);
        if (auto xml = tree.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void ChordismProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;

        auto tree = juce::ValueTree::fromXml (*xml);
        savedPortIndex = tree.getProperty ("portIndex", 0);
        const auto edits = tree.getChildWithName (ChordEditorState::treeType);
        tree.removeChild (edits, nullptr);
        migrateSeqSteps (tree);

        // Parameters first, then the edits (made on the restored Chord Group): see run().
        state.replaceState (tree);
        chordEditor.restore (edits, snapshot.chordGroup());
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chordism::ChordismProcessor();
}
