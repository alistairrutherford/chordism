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
        notifiedEditsVersion = chordEditor.getVersion();
        uiState.addListener (this);
        startTimerHz (20);
    }

    ChordismProcessor::~ChordismProcessor()
    {
        stopTimer();
        uiState.removeListener (this);
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

        // Short messages go through the engine; anything longer (SysEx) passes straight on. Keys
        // played on the editor come first, at the block's first sample.
        input.clear();
        generated.clear();
        {
            const auto scope = uiFifo.read (uiFifo.getNumReady());
            for (int i = 0; i < scope.blockSize1; ++i) input.push (uiNotes[(size_t) (scope.startIndex1 + i)]);
            for (int i = 0; i < scope.blockSize2; ++i) input.push (uiNotes[(size_t) (scope.startIndex2 + i)]);
        }
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
        recorder.record (transport, numSamples, output.begin(), output.size());

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

        publishNoteStates();
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

    void ChordismProcessor::publishNoteStates() noexcept
    {
        std::array<uint64_t, 2> s {}, k {}, p {};
        for (int n = 0; n < 128; ++n)
        {
            const uint64_t bit = (uint64_t) 1 << (n & 63);
            if (engine.getVoices().isSounding (n)) s[(size_t) (n >> 6)] |= bit;
            if (engine.isKeyDown (n))              k[(size_t) (n >> 6)] |= bit;
            if (engine.isPerforming (n))           p[(size_t) (n >> 6)] |= bit;
        }
        for (size_t i = 0; i < 2; ++i)
        {
            sounding[i].store (s[i], std::memory_order_relaxed);
            keysDown[i].store (k[i], std::memory_order_relaxed);
            performing[i].store (p[i], std::memory_order_relaxed);
        }
    }

    void ChordismProcessor::pushUiNote (int note, int velocity, bool on) noexcept
    {
        if (note < 0 || note > 127)
            return;
        const auto scope = uiFifo.write (1);
        const MidiEvent e = on ? MidiEvent::noteOn (0, note, juce::jlimit (1, 127, velocity), 0) : MidiEvent::noteOff (0, note, 0);
        if (scope.blockSize1 > 0) uiNotes[(size_t) scope.startIndex1] = e;
        else if (scope.blockSize2 > 0) uiNotes[(size_t) scope.startIndex2] = e;
    }

    //==========================================================================
    void ChordismProcessor::timerCallback()
    {
        // A program the host chose off the message thread.
        if (const int program = pendingProgram.exchange (-1); program >= 0)
            loadProgram (program);

        // The test rig's takes, for LiveBridge.
        if (snapshot.engineSettings().testPattern)
        {
            const auto info = recorder.info();
            if (info.take != 0 && ! info.recording && info.take != testTakeWritten && info.completedBars() > 0)
            {
                testTakeWritten = info.take;
                juce::String error;
                writeTake (testTakeFile(), error);
            }
        }

        // Choosing a Chord Group resets the Chord Editor's edits.
        chordEditor.sync (snapshot.chordGroup(), engine.getEditsDiscarded(), engine.getDiscardedVersion());
        if (const auto v = chordEditor.getVersion(); v != notifiedEditsVersion.exchange (v))
            notifyStateChanged();

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

    void ChordismProcessor::notifyStateChanged()
    {
        if (! restoringState)
            updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
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

    //==========================================================================
    // Programs and presets.

    int ChordismProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

    const juce::String ChordismProcessor::getProgramName (int index)
    {
        const auto& all = factoryPresets();
        return index >= 0 && index < (int) all.size() ? all[(size_t) index].name : juce::String();
    }

    void ChordismProcessor::setCurrentProgram (int index)
    {
        if (index < 0 || index >= getNumPrograms())
            return;
        currentProgram = index;
        if (juce::MessageManager::existsAndIsCurrentThread())
            loadProgram (index);
        else
            pendingProgram = index;       // the timer loads it
    }

    void ChordismProcessor::loadProgram (int index)
    {
        const auto& p = factoryPresets()[(size_t) juce::jlimit (0, getNumPrograms() - 1, index)];
        applyPreset (PresetManager::factoryTree (index), p.category, p.name);
        currentProgram = index;
    }

    void ChordismProcessor::applyPreset (const juce::ValueTree& preset, const juce::String& category, const juce::String& name)
    {
        auto tree = preset.createCopy();
        migrateSeqSteps (tree);
        for (auto* p : getParameters())
        {
            auto* param = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (param == nullptr || ! params::isPresetParameter (param->paramID))
                continue;
            const auto child = tree.getChildWithProperty ("id", param->paramID);
            const float value = child.isValid() ? param->convertTo0to1 ((float) (double) child.getProperty ("value"))
                                                : param->getDefaultValue();
            if (std::abs (param->getValue() - value) > 1.0e-6f)
                param->setValueNotifyingHost (value);
        }
        chordEditor.restore (tree.getChildWithName (ChordEditorState::treeType), snapshot.chordGroup());
        engine.requestReset();            // all notes off on a preset change (PLAN.md §5)
        setPresetIdentity (category, name);
    }

    juce::ValueTree ChordismProcessor::presetState()
    {
        juce::ValueTree tree ("Chordism");
        for (const auto& child : state.copyState())
            if (child.hasType ("PARAM") && params::isPresetParameter (child.getProperty ("id").toString()))
                tree.appendChild (child.createCopy(), nullptr);
        tree.appendChild (chordEditor.copyTree(), nullptr);
        tree.setProperty ("version", JucePlugin_VersionString, nullptr);
        return tree;
    }

    void ChordismProcessor::setPresetIdentity (const juce::String& category, const juce::String& name)
    {
        presetCategory = category;
        presetName = name;
        notifyStateChanged();
    }

    //==========================================================================
    // MIDI import and export.

    ChordImport ChordismProcessor::importMidiFile (const juce::File& file)
    {
        ChordImport result;
        juce::MemoryBlock data;
        if (! file.loadFileAsData (data))
        {
            result.message = "Couldn't read " + file.getFileName().toStdString();
            return result;
        }
        MidiSong song;
        std::string error;
        if (! readMidiFile ((const uint8_t*) data.getData(), data.getSize(), song, error))
        {
            result.message = file.getFileName().toStdString() + ": " + error;
            return result;
        }
        result = importMidi (song);
        if (! result.ok)
            return result;

        auto set = [this] (const juce::String& id, float plain)
        {
            if (auto* param = state.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (plain));
        };
        set (params::key, (float) result.key);
        set (params::scale, (float) (int) result.scale);
        set (params::chordOctave, (float) result.chordOctave);
        for (int n = 0; n < numChordSlots; ++n)
            set (params::customDegree (n), (float) result.order[(size_t) n]);
        set (params::progression, (float) (int) Progression::Custom);
        chordEditor.setImported (snapshot.chordGroup(), result.degrees);
        setPresetIdentity ("Imported", file.getFileNameWithoutExtension());
        return result;
    }

    juce::File ChordismProcessor::testTakeFile()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Chordism").getChildFile ("Exports").getChildFile ("Test take.mid");
    }

    bool ChordismProcessor::writeTake (const juce::File& file, juce::String& error) const
    {
        TakeInfo info;
        std::vector<RecordedEvent> events;
        bool copied = false;
        for (int attempt = 0; attempt < 4 && ! copied; ++attempt)
            copied = recorder.snapshot (info, events);
        if (! copied || info.completedBars() <= 0)
        {
            error = "Nothing to export yet: play at least one whole bar.";
            return false;
        }
        const auto song = exportBars (info, events);
        const auto bytes = writeMidiFile (song);
        file.getParentDirectory().createDirectory();
        if (! file.replaceWithData (bytes.data(), bytes.size()))
        {
            error = "Couldn't write " + file.getFullPathName();
            return false;
        }
        return true;
    }

    juce::File ChordismProcessor::writeExport (juce::String& error)
    {
        const auto info = recorder.info();
        const int bars = juce::jmin (info.completedBars(), ExportRecorder::maxBars);
        const auto s = snapshot.engineSettings();
        const juce::String name = "Chordism " + juce::String (keyName (s.key)).upToFirstOccurrenceOf ("/", false, false) + " "
                                + scaleInfo (s.scale).name + " " + juce::String (bars) + (bars == 1 ? " bar" : " bars");
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Chordism Export")
                              .getChildFile (juce::File::createLegalFileName (name) + ".mid");
        return writeTake (file, error) ? file : juce::File();
    }

    juce::AudioProcessorEditor* ChordismProcessor::createEditor()
    {
        return new ChordismEditor (*this);
    }

    void ChordismProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto tree = state.copyState();
        tree.appendChild (chordEditor.copyTree(), nullptr);
        tree.appendChild (uiState.createCopy(), nullptr);
        tree.setProperty ("portIndex", port.isOpen() ? port.getIndex() : savedPortIndex, nullptr);
        tree.setProperty ("program", currentProgram.load(), nullptr);
        tree.setProperty ("presetName", presetName, nullptr);
        tree.setProperty ("presetCategory", presetCategory, nullptr);
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
        currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));
        presetName = tree.getProperty ("presetName", presetName).toString();
        presetCategory = tree.getProperty ("presetCategory", presetCategory).toString();
        const auto edits = tree.getChildWithName (ChordEditorState::treeType);
        tree.removeChild (edits, nullptr);
        const auto ui = tree.getChildWithName (uiState.getType());
        restoringState = true;
        if (ui.isValid())
        {
            uiState.copyPropertiesAndChildrenFrom (ui, nullptr);
            tree.removeChild (ui, nullptr);
        }
        restoringState = false;
        migrateSeqSteps (tree);

        // Parameters first, then the edits (made on the restored Chord Group): see run().
        pendingProgram = -1;              // a restore replaces a program change still waiting
        state.replaceState (tree);

        // APVTS leaves a parameter alone when its value rounds to the saved one (a bool a host
        // set to 0.3 is already "off"), so the host's raw value would survive the restore:
        // set each saved one exactly. (APVTS adds a child without a value for a parameter the
        // state doesn't have; that one keeps its value.)
        for (auto* p : getParameters())
            if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (const auto child = tree.getChildWithProperty ("id", param->paramID); child.hasProperty ("value"))
                    if (const float v = param->convertTo0to1 ((float) (double) child.getProperty ("value")); std::abs (param->getValue() - v) > 1.0e-6f)
                        param->setValueNotifyingHost (v);
        chordEditor.restore (edits, snapshot.chordGroup());
        notifiedEditsVersion = chordEditor.getVersion();   // a restore isn't a change to report
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chordism::ChordismProcessor();
}
