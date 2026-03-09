#include "AudioEngine.h"
#include "AudioChannel.h"
#include "VSTiChannel.h"
#include "PluginHostWrapper.h"

//==============================================================================
// Constructor / Destructor
//==============================================================================

AudioEngine::AudioEngine()
{
    formatManager.registerBasicFormats();
    loopEngine       = std::make_unique<LoopEngine>();
    metronome        = std::make_unique<Metronome>();
    pluginHost       = std::make_unique<PluginHostWrapper>();
    midiLearnManager = std::make_unique<MidiLearnManager>(*this);

    for (int i = 0; i < NUM_SECTIONS; ++i)
        sectionLoopLengths[i].store(0, std::memory_order_relaxed);

    for (int i = 0; i < 6; ++i)
    {
        channels[i]     = std::make_unique<AudioChannel>(i);
        channelNames[i] = "CH " + juce::String(i + 1);
    }

    deviceManager.addAudioCallback(this);
}

AudioEngine::~AudioEngine()
{
    if (masterRecordingActive.load(std::memory_order_relaxed))
        stopMasterRecording();

    const auto midiInputs = juce::MidiInput::getAvailableDevices();
    for (const auto& device : midiInputs)
        deviceManager.setMidiInputDeviceEnabled(device.identifier, false);

    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
}

//==============================================================================
// MIDI Input
//==============================================================================

void AudioEngine::openMidiInputs()
{
    const auto midiInputs = juce::MidiInput::getAvailableDevices();
    for (const auto& device : midiInputs)
    {
        if (!deviceManager.isMidiInputDeviceEnabled(device.identifier))
            deviceManager.setMidiInputDeviceEnabled(device.identifier, true);

        deviceManager.addMidiInputDeviceCallback(device.identifier, this);
        DBG("MIDI Input opened: " + device.name);
    }
    DBG("MIDI: " + juce::String(midiInputs.size()) + " device(s) found");
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput* /*source*/,
                                             const juce::MidiMessage& message)
{
    // MIDI thread — lock-free only!

    // 1. MidiLearnManager (für MIDI-Mapping)
    if (midiLearnManager)
        midiLearnManager->postMidiMessage(message);

    // 2. Collector → Audio-Thread liest ihn im nächsten Block aus
    midiCollector.addMessageToQueue(message);
}

//==============================================================================
// Initialization
//==============================================================================

juce::String AudioEngine::initialiseAudio(int inputChannels,
                                          int outputChannels,
                                          double sampleRate,
                                          int bufferSize)
{
    // Try to restore previously saved device settings
    std::unique_ptr<juce::XmlElement> savedXml;
    const auto settingsFile = getAudioSettingsFile();
    if (settingsFile.existsAsFile())
    {
        savedXml = juce::parseXML(settingsFile);
        DBG("Audio settings: loading from " + settingsFile.getFullPathName());
    }

    juce::String error;
    if (savedXml)
    {
        // Restore from saved state (ignores the setup struct — device manager handles it)
        error = deviceManager.initialise(inputChannels, outputChannels,
                                         savedXml.get(),
                                         true,          // select default on failure
                                         juce::String(),
                                         nullptr);
    }
    else
    {
        // No saved settings — use provided defaults
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup(setup);
        setup.inputChannels .setRange(0, inputChannels,  true);
        setup.outputChannels.setRange(0, outputChannels, true);
        if (sampleRate > 0.0) setup.sampleRate = sampleRate;
        if (bufferSize  > 0)  setup.bufferSize  = bufferSize;

        error = deviceManager.initialise(inputChannels, outputChannels,
                                         nullptr,
                                         true,
                                         juce::String(),
                                         &setup);
    }

    if (error.isEmpty())
    {
        isInitialised.store(true, std::memory_order_release);
        openMidiInputs();
        // Note: currentSampleRate/currentBufferSize are populated in
        // audioDeviceAboutToStart(), which is where the device-ready log is printed.
    }
    else
    {
        DBG("Audio engine init failed: " + error);
    }

    return error;
}

juce::File AudioEngine::getAudioSettingsFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("chief")
        .getChildFile("AudioSettings.xml");
}

bool AudioEngine::saveAudioSettings() const
{
    auto xml = deviceManager.createStateXml();
    if (!xml) return false;

    const auto file = getAudioSettingsFile();
    file.getParentDirectory().createDirectory();
    const bool ok = xml->writeTo(file);
    DBG("Audio settings " + juce::String(ok ? "saved" : "FAILED to save") +
        ": " + file.getFullPathName());
    return ok;
}

//==============================================================================
// AudioIODeviceCallback
//==============================================================================

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (!device) return;

    currentSampleRate  = device->getCurrentSampleRate();
    currentBufferSize  = device->getCurrentBufferSizeSamples();
    numInputChannels   = device->getActiveInputChannels() .countNumberOfSetBits();
    numOutputChannels  = device->getActiveOutputChannels().countNumberOfSetBits();

    // Loop engine
    loopEngine->setSampleRate(currentSampleRate);

    // Metronome — prepare (do NOT re-create here, that would reset config)
    metronome->setBPM(loopEngine->getBPM());
    metronome->prepareToPlay(currentSampleRate);

    // MIDI collector — must be reset when sample rate changes
    midiCollector.reset(currentSampleRate);

    // Working buffers
    inputBuffer .setSize(numInputChannels,  currentBufferSize * 2);
    outputBuffer.setSize(numOutputChannels, currentBufferSize * 2);

    // Master recording buffer (stereo)
    masterRecordBuffer.setSize(2, currentBufferSize * 2);

    // Prepare channels (max loop = 10 min)
    const juce::int64 maxLoopSamples = static_cast<juce::int64>(600.0 * currentSampleRate);
    for (auto& ch : channels)
        if (ch) ch->prepareToPlay(currentSampleRate, currentBufferSize, maxLoopSamples);

    DBG("Audio device ready: " +
        juce::String(currentSampleRate) + " Hz, " +
        juce::String(currentBufferSize) + " samples, " +
        juce::String(currentBufferSize / currentSampleRate * 1000.0, 2) + " ms latency");
}

void AudioEngine::audioDeviceStopped()
{
    if (masterRecordingActive.load(std::memory_order_relaxed))
        stopMasterRecording();

    isPlayingFlag.store(false, std::memory_order_release);
    for (auto& ch : channels)
        if (ch) ch->releaseResources();
    inputBuffer .clear();
    outputBuffer.clear();
    DBG("Audio device stopped");
}

void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    //==========================================================================
    // REAL-TIME AUDIO THREAD — NO ALLOCATIONS, NO LOCKS, NO BLOCKING
    //==========================================================================

    if (numSamples <= 0 || !isInitialised.load(std::memory_order_relaxed))
    {
        clearOutputBuffer(outputChannelData, numOutputChannels, numSamples);
        return;
    }

    //--- 1. PROCESS COMMANDS ---------------------------------------------------
    commandQueue.processCommands([this](const Command& cmd) {
        processCommand(cmd);
    });

    //--- 2. CLEAR OUTPUT -------------------------------------------------------
    clearOutputBuffer(outputChannelData, numOutputChannels, numSamples);

    //--- 3. ADVANCE PLAYHEAD ---------------------------------------------------
    bool playing = isPlayingFlag.load(std::memory_order_relaxed);

    // Capture playhead BEFORE advancing so channels and metronome see the
    // start-of-block position.  Without this, block 0 passes playhead=512
    // instead of 0 — the very first beat click is never fired.
    const juce::int64 playheadPos = loopEngine->getCurrentPlayhead();
    const juce::int64 loopLen     = loopEngine->getLoopLength();

    loopEngine->processBlock(numSamples, playing);

    //--- 3a. BAR-END PENDING STOP (metronome first-recording) -----------------
    if (pendingMetroBarEnd.load(std::memory_order_relaxed))
    {
        barEndSamplesRemaining -= numSamples;
        if (barEndSamplesRemaining <= 0)
        {
            const int pch = barEndPendingChannel.load(std::memory_order_relaxed);
            pendingMetroBarEnd.store(false, std::memory_order_release);
            barEndPendingChannel.store(-1, std::memory_order_release);
            const juce::int64 offset = barEndPlayheadOffset;
            barEndPlayheadOffset = 0;
            loopEngine->setLoopLength(barEndTargetSample);
            sectionLoopLengths[activeGlobalSection.load(std::memory_order_relaxed)]
                .store(barEndTargetSample, std::memory_order_release);
            loopEngine->setPlayhead(offset);   // 0 = normal, >0 = seamless snap-back
            if (pch >= 0 && pch < 6 && channels[pch])
                channels[pch]->stopRecording();
            DBG("Metronome: loop set to " + juce::String(barEndTargetSample) +
                " samples, playhead = " + juce::String(offset) + " samples");
        }
    }

    //--- 3a2. PENDING SECTION SWITCH (latch mode: fires at loop boundary) -----
    {
        const int ps = pendingGlobalSection.load(std::memory_order_relaxed);
        if (ps >= 0 && loopLen > 0 && playing)
        {
            // Fire at loop boundary: playhead wrapped in this block
            const juce::int64 curPos = loopEngine->getCurrentPlayhead();
            if (curPos < static_cast<juce::int64>(numSamples))
            {
                pendingGlobalSection.store(-1, std::memory_order_release);

                // Save current loop length
                const int curSec = activeGlobalSection.load(std::memory_order_relaxed);
                sectionLoopLengths[curSec].store(loopLen, std::memory_order_release);

                // Switch
                activeGlobalSection.store(ps, std::memory_order_release);
                juce::int64 newLen = sectionLoopLengths[ps].load(std::memory_order_relaxed);

                // Fallback: if new section is empty, inherit loop length from previous section
                if (newLen == 0)
                {
                    for (int fs = ps - 1; fs >= 0; --fs)
                    {
                        const juce::int64 fallbackLen = sectionLoopLengths[fs].load(std::memory_order_relaxed);
                        if (fallbackLen > 0) { newLen = fallbackLen; break; }
                    }
                }

                loopEngine->setLoopLength(newLen);
                loopEngine->resetPlayhead();

                // Start pending record if queued for this section switch
                const int pendRecCh = pendingSectionRecordChannel.load(std::memory_order_relaxed);
                if (pendRecCh >= 0)
                {
                    pendingSectionRecordChannel.store(-1, std::memory_order_release);
                    if (pendRecCh < 6 && channels[pendRecCh])
                        channels[pendRecCh]->startRecording(pendingSectionRecordIsOverdub.load(std::memory_order_relaxed));
                }

                for (auto& ch : channels)
                    if (ch) ch->setActiveSection(ps);

                DBG("Section switch (latched) -> " + juce::String(ps) +
                    ", loopLen=" + juce::String(newLen));
            }
        }
    }

    //--- 3b. STOP ALL CHANNELS WHEN TRANSPORT IS NOT RUNNING ------------------
    if (!playing)
    {
        // Also cancel any pending bar-end stop, saving whatever has been recorded so far
        if (pendingMetroBarEnd.load(std::memory_order_relaxed))
        {
            pendingMetroBarEnd.store(false, std::memory_order_release);
            barEndPendingChannel.store(-1, std::memory_order_release);
            barEndSamplesRemaining = 0;
            barEndPlayheadOffset = 0;
            const juce::int64 currentPos = loopEngine->getCurrentPlayhead();
            if (currentPos > 0)
            {
                loopEngine->setLoopLength(currentPos);
                sectionLoopLengths[activeGlobalSection.load(std::memory_order_relaxed)]
                    .store(currentPos, std::memory_order_release);
                loopEngine->resetPlayhead();
            }
        }

        // Cancel pending section switch
        pendingGlobalSection.store(-1, std::memory_order_release);

        for (auto& ch : channels)
        {
            if (!ch) continue;

            auto cs = ch->getState();

            // Stop recording first (transitions state to Playing)
            if (cs == ChannelState::Recording || cs == ChannelState::Overdubbing)
            {
                ch->stopRecording();
                cs = ch->getState();   // update after transition
            }

            // Then stop playing (including channels that just finished recording)
            if (cs == ChannelState::Playing)
                ch->stopPlayback();
        }

        // Cancel pending count-in and autostart
        if (countInActive.load(std::memory_order_relaxed))
        {
            countInActive.store(false, std::memory_order_release);
            pendingRecordChannel.store(-1, std::memory_order_release);
        }
        autoStartTriggered = false;
    }

    //--- 3c. AUTO-START THRESHOLD CHECK ----------------------------------------
    if (autoStartEnabled.load(std::memory_order_relaxed) && !playing && !autoStartTriggered
        && !countInActive.load(std::memory_order_relaxed))
    {
        const float threshold = autoStartThresholdLinear.load(std::memory_order_relaxed);
        float peak = 0.0f;
        for (int ch = 0; ch < numInputChannels && peak < threshold; ++ch)
        {
            if (inputChannelData[ch])
                for (int s = 0; s < numSamples; ++s)
                    peak = juce::jmax(peak, std::abs(inputChannelData[ch][s]));
        }

        if (peak >= threshold)
        {
            autoStartTriggered = true;
            const int activeIdx = activeChannelIndex.load(std::memory_order_relaxed);
            Command cmd = Command::startRecord(activeIdx);
            processCommand(cmd);
            // Re-read the playing flag — StartRecord may have set it
            playing = isPlayingFlag.load(std::memory_order_relaxed);
        }
    }

    //--- 3d. COUNT-IN COUNTDOWN ------------------------------------------------
    if (countInActive.load(std::memory_order_relaxed))
    {
        countInSamplesRemaining -= numSamples;
        if (countInSamplesRemaining <= 0)
        {
            countInActive.store(false, std::memory_order_release);
            const int pch = pendingRecordChannel.load(std::memory_order_relaxed);
            if (pch >= 0 && pch < 6)
            {
                if (auto* ch = channels[pch].get())
                {
                    if (loopEngine->getLoopLength() == 0)
                        loopEngine->resetPlayhead();
                    ch->startRecording(false);

                    // Fixed-length auto-stop after count-in
                    if (metronome->getEnabled())
                    {
                        const int flBars = fixedLengthBars.load(std::memory_order_relaxed);
                        if (flBars > 0)
                        {
                            const double bpm = loopEngine->getBPM();
                            const double spb = (60.0 / juce::jmax(1.0, bpm)) * currentSampleRate;
                            fixedLengthSamplesRemaining = static_cast<juce::int64>(
                                flBars * metronome->getBeatsPerBar() * spb + 0.5);
                            fixedLengthChannel.store(pch, std::memory_order_release);
                            fixedLengthActive.store(true, std::memory_order_release);
                        }
                    }
                }
            }
            pendingRecordChannel.store(-1, std::memory_order_release);
        }
    }

    //--- 3e. FIXED-LENGTH RECORDING AUTO-STOP ---------------------------------
    if (fixedLengthActive.load(std::memory_order_relaxed))
    {
        fixedLengthSamplesRemaining -= numSamples;
        if (fixedLengthSamplesRemaining <= 0)
        {
            const int pch2 = fixedLengthChannel.load(std::memory_order_relaxed);
            fixedLengthActive.store(false,  std::memory_order_release);
            fixedLengthChannel.store(-1,    std::memory_order_release);

            if (pch2 >= 0 && pch2 < 6 && channels[pch2] &&
                channels[pch2]->getState() == ChannelState::Recording)
            {
                // Establish loop length if this is the first recording
                if (loopEngine->getLoopLength() == 0)
                {
                    const double bpm = loopEngine->getBPM();
                    const double spb = (60.0 / juce::jmax(1.0, bpm)) * currentSampleRate;
                    const juce::int64 exactLen = static_cast<juce::int64>(
                        fixedLengthBars.load(std::memory_order_relaxed) *
                        metronome->getBeatsPerBar() * spb + 0.5);
                    loopEngine->setLoopLength(exactLen);
                    sectionLoopLengths[activeGlobalSection.load(std::memory_order_relaxed)]
                        .store(exactLen, std::memory_order_release);
                    loopEngine->resetPlayhead();
                }
                channels[pch2]->stopRecording();
                DBG("Fixed-length: auto-stopped recording on ch " + juce::String(pch2));
            }
        }
    }

    //--- 4. COLLECT MIDI -------------------------------------------------------
    // Thread-safe: MIDI thread writes via addMessageToQueue(),
    // audio thread reads here via removeNextBlockOfMessages()
    juce::MidiBuffer midiBuffer;
    midiCollector.removeNextBlockOfMessages(midiBuffer, numSamples);

    //--- 4b. SOLO ENFORCEMENT + ACTIVE-CHANNEL FLAG ----------------------------
    {
        bool anySolo = false;
        for (auto& ch : channels)
            if (ch && ch->isSolo()) { anySolo = true; break; }

        const int activeIdx = activeChannelIndex.load(std::memory_order_relaxed);
        for (int i = 0; i < 6; ++i)
        {
            if (!channels[i]) continue;
            channels[i]->setSoloMuted(anySolo && !channels[i]->isSolo());
            channels[i]->setIsActiveChannel(i == activeIdx);
        }
    }

    //--- 5. PROCESS CHANNELS ---------------------------------------------------
    for (auto& channel : channels)
    {
        if (channel)
        {
            channel->processBlock(inputChannelData,
                                  outputChannelData,
                                  midiBuffer,
                                  numSamples,
                                  playheadPos,
                                  loopLen,
                                  numInputChannels,
                                  numOutputChannels);
        }
    }

    //--- 5b. MASTER RECORDING --------------------------------------------------
    if (masterRecordingActive.load(std::memory_order_acquire))
    {
        masterRecordBuffer.clear();
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch])
                juce::FloatVectorOperations::add(
                    masterRecordBuffer.getWritePointer(ch & 1),
                    outputChannelData[ch], numSamples);

        const float mg = masterGain.load(std::memory_order_relaxed);
        if (mg != 1.0f)
            for (int c = 0; c < 2; ++c)
                juce::FloatVectorOperations::multiply(
                    masterRecordBuffer.getWritePointer(c), mg, numSamples);

        const float* const bufs[] = {
            masterRecordBuffer.getReadPointer(0),
            masterRecordBuffer.getReadPointer(1)
        };
        masterRecordWriter->write(bufs, numSamples);
    }

    //--- 6. METRONOME ----------------------------------------------------------
    metronome->processBlock(outputChannelData,
                            numOutputChannels,
                            numSamples,
                            playheadPos,
                            playing);

    //--- 7. MASTER GAIN -------------------------------------------------------
    {
        const float mg = masterGain.load(std::memory_order_relaxed);
        if (mg != 1.0f)
        {
            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (outputChannelData[ch])
                    juce::FloatVectorOperations::multiply(outputChannelData[ch], mg, numSamples);
        }
    }

    //--- DIAGNOSTICS -----------------------------------------------------------
    totalSamplesProcessed.fetch_add(numSamples, std::memory_order_relaxed);
}

//==============================================================================
// Command Processing (Audio Thread)
//==============================================================================

void AudioEngine::processCommand(const Command& cmd)
{
    if (cmd.channelIndex >= 0 && cmd.channelIndex < 6)
        processChannelCommand(cmd);
    else
        processGlobalCommand(cmd);
}

void AudioEngine::processGlobalCommand(const Command& cmd)
{
    switch (cmd.type)
    {
        case CommandType::SetBPM:
        {
            if (hasAnyRecordings()) { DBG("SetBPM ignored: recordings exist"); break; }
            if (pendingMetroBarEnd.load(std::memory_order_relaxed))
            { DBG("SetBPM ignored: bar-end snap pending"); break; }
            const double bpm = static_cast<double>(cmd.floatValue);
            loopEngine->setBPM(bpm);
            metronome->setBPM(bpm);
            // Loop length is determined by the first recording (bar-rounded in metronome mode),
            // so changing BPM does NOT recalculate loop length here.
            break;
        }

        case CommandType::SetBeatsPerLoop:
        {
            if (isPlayingFlag.load(std::memory_order_relaxed)) { DBG("SetBeatsPerLoop ignored: playing"); break; }
            loopEngine->setBeatsPerLoop(cmd.intValue1);
            break;
        }

        case CommandType::SetLoopLength:
        {
            loopEngine->setLoopLength(cmd.getLoopLength());
            break;
        }

        case CommandType::ResetPlayhead:
        {
            loopEngine->resetPlayhead();
            break;
        }

        case CommandType::SetGlobalOverdubMode:
        {
            overdubMode.store(cmd.boolValue, std::memory_order_release);
            break;
        }

        case CommandType::ChangeActiveChannel:
        {
            // intValue1: +1 = next, -1 = prev, 0-5 = direkt
            if (cmd.intValue1 == 1)
            {
                const int cur = activeChannelIndex.load(std::memory_order_relaxed);
                activeChannelIndex.store((cur + 1) % 6, std::memory_order_release);
            }
            else if (cmd.intValue1 == -1)
            {
                const int cur = activeChannelIndex.load(std::memory_order_relaxed);
                activeChannelIndex.store((cur + 5) % 6, std::memory_order_release);
            }
            else if (cmd.intValue1 >= 0 && cmd.intValue1 < 6)
            {
                activeChannelIndex.store(cmd.intValue1, std::memory_order_release);
            }
            break;
        }

        case CommandType::SetMetronomeOutput:
        {
            metronome->setOutputChannels(cmd.intValue1, cmd.intValue2);
            break;
        }

        case CommandType::SetMetronomeMute:
        {
            metronome->setMuted(cmd.boolValue);
            break;
        }

        case CommandType::ResetSong:
        {
            pendingMetroBarEnd.store(false, std::memory_order_release);
            barEndPendingChannel.store(-1, std::memory_order_release);
            barEndSamplesRemaining = 0;
            barEndTargetSample = 0;
            barEndPlayheadOffset = 0;
            fixedLengthActive.store(false, std::memory_order_release);
            fixedLengthChannel.store(-1,   std::memory_order_release);
            fixedLengthSamplesRemaining = 0;
            pendingGlobalSection.store(-1, std::memory_order_release);
            pendingSectionRecordChannel.store(-1, std::memory_order_release);
            for (auto& ch : channels)
                if (ch) ch->clearAllSections();

            activeGlobalSection.store(0, std::memory_order_release);
            for (int s = 0; s < NUM_SECTIONS; ++s)
                sectionLoopLengths[s].store(0, std::memory_order_release);

            loopEngine->setLoopLength(0);
            loopEngine->resetPlayhead();
            isPlayingFlag.store(false, std::memory_order_release);
            DBG("Song reset: all channels + sections cleared");
            break;
        }

        case CommandType::EmergencyStop:
        {
            pendingMetroBarEnd.store(false, std::memory_order_release);
            barEndPendingChannel.store(-1, std::memory_order_release);
            barEndSamplesRemaining = 0;
            barEndTargetSample = 0;
            barEndPlayheadOffset = 0;
            fixedLengthActive.store(false, std::memory_order_release);
            fixedLengthChannel.store(-1,   std::memory_order_release);
            fixedLengthSamplesRemaining = 0;
            pendingGlobalSection.store(-1, std::memory_order_release);
            pendingSectionRecordChannel.store(-1, std::memory_order_release);
            isPlayingFlag.store(false, std::memory_order_release);
            loopEngine->resetPlayhead();
            for (auto& ch : channels)
            {
                if (!ch) continue;
                const auto cs = ch->getState();
                if (cs == ChannelState::Recording || cs == ChannelState::Overdubbing)
                    ch->stopRecording();   // preserves loopHasContent
                else if (cs == ChannelState::Playing)
                    ch->stopPlayback();
            }
            break;
        }

        case CommandType::DoubleLoopLength:
        {
            const juce::int64 loopLen = loopEngine->getLoopLength();
            if (loopLen <= 0) break;

            const int curSec = activeGlobalSection.load(std::memory_order_relaxed);
            for (auto& ch : channels)
                if (ch) ch->doubleBuffer(curSec, loopLen);

            loopEngine->setLoopLength(loopLen * 2);
            sectionLoopLengths[curSec].store(loopLen * 2, std::memory_order_release);
            DBG("Loop doubled: " + juce::String(loopLen) + " -> " + juce::String(loopLen * 2));
            break;
        }

        case CommandType::SetActiveSection:
        {
            const int newSec = cmd.intValue1;
            if (newSec < 0 || newSec >= NUM_SECTIONS) break;

            const int curSec = activeGlobalSection.load(std::memory_order_relaxed);
            if (newSec == curSec) break;

            // Stop any recording/overdubbing first
            for (auto& ch : channels)
            {
                if (!ch) continue;
                const auto cs = ch->getState();
                if (cs == ChannelState::Recording || cs == ChannelState::Overdubbing)
                    ch->stopRecording();
            }

            // Save current loop length
            sectionLoopLengths[curSec].store(loopEngine->getLoopLength(), std::memory_order_release);

            // Switch
            activeGlobalSection.store(newSec, std::memory_order_release);
            juce::int64 newLen = sectionLoopLengths[newSec].load(std::memory_order_relaxed);

            // Fallback: if new section is empty, inherit loop length from previous section
            if (newLen == 0)
            {
                for (int fs = newSec - 1; fs >= 0; --fs)
                {
                    const juce::int64 fallbackLen = sectionLoopLengths[fs].load(std::memory_order_relaxed);
                    if (fallbackLen > 0) { newLen = fallbackLen; break; }
                }
            }

            loopEngine->setLoopLength(newLen);
            loopEngine->resetPlayhead();

            for (auto& ch : channels)
                if (ch) ch->setActiveSection(newSec);

            DBG("Section switch -> " + juce::String(newSec) +
                ", loopLen=" + juce::String(newLen));
            break;
        }

        default:
            DBG("Unknown global command: " + juce::String(static_cast<int>(cmd.type)));
            break;
    }
}

void AudioEngine::processChannelCommand(const Command& cmd)
{
    if (cmd.channelIndex < 0 || cmd.channelIndex >= 6) return;

    Channel* channel = channels[cmd.channelIndex].get();
    if (!channel) return;

    switch (cmd.type)
    {
        case CommandType::StartRecord:
        {
            // Guard 1: already recording/overdubbing on this channel — ignore duplicate
            {
                const auto chState = channel->getState();
                if (chState == ChannelState::Recording || chState == ChannelState::Overdubbing)
                    break;
            }

            // Guard 2: count-in already pending for this exact channel — ignore duplicate
            if (countInActive.load(std::memory_order_relaxed) &&
                pendingRecordChannel.load(std::memory_order_relaxed) == cmd.channelIndex)
                break;

            // Auto-start transport so the playhead advances
            const bool wasPlaying = isPlayingFlag.load(std::memory_order_relaxed);
            isPlayingFlag.store(true, std::memory_order_release);

            const juce::int64 loopLen = loopEngine->getLoopLength();
            const bool latch = latchMode.load(std::memory_order_relaxed);

            if (latch && loopLen > 0)
            {
                // Latch mode: defer recording start to next loop boundary
                channel->requestRecordAtLoopEnd();
            }
            else
            {
                const int ci = countInBeats.load(std::memory_order_relaxed);
                if (ci > 0 && !countInActive && !wasPlaying)
                {
                    // Count-in phase: transport runs, recording deferred
                    const double bpm = loopEngine->getBPM();
                    const double spb = (60.0 / juce::jmax(1.0, bpm)) * currentSampleRate;
                    countInSamplesRemaining = static_cast<juce::int64>(
                        juce::jmax(1, static_cast<int>(ci * spb)));
                    countInActive.store(true, std::memory_order_release);
                    pendingRecordChannel.store(cmd.channelIndex, std::memory_order_release);
                    // Reset playhead immediately so count-in starts at "beat 1" visually
                    if (loopLen == 0)
                        loopEngine->resetPlayhead();
                    DBG("Count-in: " + juce::String(ci) + " beat(s) = " +
                        juce::String(countInSamplesRemaining) + " samples");
                }
                else
                {
                    // Bar-quantized record start: if metronome is on and the transport was
                    // already playing, defer recording to the next bar boundary.
                    bool deferred = false;
                    if (metronome->getEnabled() && wasPlaying
                        && !countInActive.load(std::memory_order_relaxed))
                    {
                        const double bpm = loopEngine->getBPM();
                        const double spb = (60.0 / juce::jmax(1.0, bpm)) * currentSampleRate;
                        const juce::int64 barSamples = static_cast<juce::int64>(
                            metronome->getBeatsPerBar() * spb + 0.5);
                        const juce::int64 posInBar = loopEngine->getCurrentPlayhead() % barSamples;
                        const juce::int64 samplesUntilBar = posInBar > 0
                            ? barSamples - posInBar : 0;

                        if (samplesUntilBar > 0)
                        {
                            countInSamplesRemaining = samplesUntilBar;
                            countInActive.store(true, std::memory_order_release);
                            pendingRecordChannel.store(cmd.channelIndex, std::memory_order_release);
                            deferred = true;
                            DBG("Bar-quantize: " + juce::String(samplesUntilBar) +
                                " samples to bar start (" +
                                juce::String(static_cast<double>(samplesUntilBar) /
                                             currentSampleRate * 1000.0, 1) + " ms)");
                        }
                    }

                    if (!deferred)
                    {
                        if (loopLen == 0)
                            loopEngine->resetPlayhead();
                        channel->startRecording(false);

                        // Fixed-length auto-stop (metronome mode only)
                        if (metronome->getEnabled())
                        {
                            const int flBars = fixedLengthBars.load(std::memory_order_relaxed);
                            if (flBars > 0)
                            {
                                const double bpm = loopEngine->getBPM();
                                const double spb = (60.0 / juce::jmax(1.0, bpm)) * currentSampleRate;
                                fixedLengthSamplesRemaining = static_cast<juce::int64>(
                                    flBars * metronome->getBeatsPerBar() * spb + 0.5);
                                fixedLengthChannel.store(cmd.channelIndex, std::memory_order_release);
                                fixedLengthActive.store(true, std::memory_order_release);
                            }
                        }
                    }
                }
            }
            break;
        }

        case CommandType::StopRecord:
        {
            // Cancel any fixed-length countdown for this channel
            if (fixedLengthChannel.load(std::memory_order_relaxed) == cmd.channelIndex)
                fixedLengthActive.store(false, std::memory_order_release);

            const juce::int64 loopLen = loopEngine->getLoopLength();
            // Free mode: first recording sets the loop length exactly
            if (!metronome->getEnabled() && loopLen == 0)
            {
                const juce::int64 recorded = loopEngine->getCurrentPlayhead();
                if (recorded > 0)
                {
                    loopEngine->setLoopLength(recorded);
                    sectionLoopLengths[activeGlobalSection.load(std::memory_order_relaxed)]
                        .store(recorded, std::memory_order_release);
                    DBG("Loop length from first recording: " +
                        juce::String(recorded) + " samples (" +
                        juce::String(static_cast<double>(recorded) / currentSampleRate, 2) + "s)");
                }
                loopEngine->resetPlayhead();
                channel->stopRecording();
            }
            // Metronome mode: first recording — continue until end of current bar.
            // If stop is requested a second time while waiting, stop immediately.
            else if (metronome->getEnabled() && loopLen == 0)
            {
                const juce::int64 recorded = loopEngine->getCurrentPlayhead();

                // Second StopRecord while already waiting for bar end → cancel, stop now
                if (pendingMetroBarEnd.load(std::memory_order_relaxed) &&
                    barEndPendingChannel.load(std::memory_order_relaxed) == cmd.channelIndex)
                {
                    pendingMetroBarEnd.store(false, std::memory_order_release);
                    barEndPendingChannel.store(-1, std::memory_order_release);
                    barEndSamplesRemaining = 0;
                    barEndPlayheadOffset = 0;
                    if (recorded > 0)
                    {
                        loopEngine->setLoopLength(recorded);
                        sectionLoopLengths[activeGlobalSection.load(std::memory_order_relaxed)]
                            .store(recorded, std::memory_order_release);
                    }
                    loopEngine->resetPlayhead();
                    channel->stopRecording();
                    DBG("Metronome bar-end cancelled — stopped immediately at " +
                        juce::String(recorded) + " samples");
                }
                else if (recorded > 0)
                {
                    const double bpm        = loopEngine->getBPM();
                    const double spb        = (60.0 / juce::jmax(1.0, bpm)) * currentSampleRate;
                    const int    bpb        = metronome->getBeatsPerBar();
                    const double barSamples = bpb * spb;
                    const juce::int64 spbSamples = static_cast<juce::int64>(spb + 0.5);

                    const juce::int64 completedBars = static_cast<juce::int64>(
                        static_cast<double>(recorded) / barSamples);
                    const juce::int64 lastBarEnd = static_cast<juce::int64>(
                        completedBars * barSamples + 0.5);
                    const juce::int64 posInBar = recorded - lastBarEnd;  // overshoot past bar end

                    // Case 1: pressed stop within 1 beat after a completed bar end.
                    // Stop immediately here (command handler runs before playheadPos/loopLen
                    // are captured), so the channel sees the correct state this same block.
                    if (completedBars >= 1 && posInBar < spbSamples)
                    {
                        loopEngine->setLoopLength(lastBarEnd);
                        sectionLoopLengths[activeGlobalSection.load(std::memory_order_relaxed)]
                            .store(lastBarEnd, std::memory_order_release);
                        loopEngine->setPlayhead(posInBar);   // offset so no jump in playback
                        channel->stopRecording();
                        DBG("Metronome: snap to bar end (" + juce::String(completedBars) +
                            " bar(s)), playhead offset = " + juce::String(posInBar) + " samples (" +
                            juce::String(static_cast<double>(posInBar) / currentSampleRate * 1000.0, 1) +
                            " ms)");
                    }
                    else
                    {
                        // Case 2: mid-bar (beat 2 onwards) — keep recording until bar end,
                        // then start playback from position 0.
                        barEndTargetSample   = static_cast<juce::int64>(
                            (completedBars + 1) * barSamples + 0.5);
                        const juce::int64 samplesUntilBarEnd = barEndTargetSample - recorded;
                        barEndSamplesRemaining = juce::jmax(juce::int64(1), samplesUntilBarEnd);
                        barEndPlayheadOffset = 0;
                        barEndPendingChannel.store(cmd.channelIndex, std::memory_order_release);
                        pendingMetroBarEnd.store(true, std::memory_order_release);
                        DBG("Metronome: waiting for bar end at sample " +
                            juce::String(barEndTargetSample) + " (" +
                            juce::String(barEndSamplesRemaining) + " samples remaining)");
                    }
                    // Recording continues in Case 2 — do NOT stop the channel yet
                }
                else
                {
                    loopEngine->resetPlayhead();
                    channel->stopRecording();
                }
            }
            else if (latchMode.load(std::memory_order_relaxed))
            {
                channel->requestStopAtLoopEnd();
            }
            else
            {
                channel->stopRecording();
            }
            break;
        }

        case CommandType::StartPlayback:
        {
            const juce::int64 loopLen = loopEngine->getLoopLength();
            if (latchMode.load(std::memory_order_relaxed) && loopLen > 0
                && isPlayingFlag.load(std::memory_order_relaxed))
                channel->requestPlayAtLoopEnd();
            else
                channel->startPlayback();
            break;
        }

        case CommandType::StopPlayback:
        {
            if (latchMode.load(std::memory_order_relaxed) && loopEngine->getLoopLength() > 0)
                channel->requestStopAtLoopEnd();
            else
                channel->stopPlayback();
            break;
        }

        case CommandType::StartOverdub:
        {
            isPlayingFlag.store(true, std::memory_order_release);
            const juce::int64 loopLen = loopEngine->getLoopLength();
            if (latchMode.load(std::memory_order_relaxed) && loopLen > 0)
                channel->requestOverdubAtLoopEnd();
            else
                channel->startRecording(true);
            break;
        }

        case CommandType::StopOverdub:
        {
            if (latchMode.load(std::memory_order_relaxed) && loopEngine->getLoopLength() > 0)
                channel->requestStopAtLoopEnd();
            else
                channel->stopRecording();
            break;
        }

        case CommandType::SetGain:
        {
            channel->setGainDb(cmd.floatValue);
            break;
        }

        case CommandType::SetMonitorMode:
        {
            channel->setMonitorMode(static_cast<MonitorMode>(cmd.intValue1));
            break;
        }

        case CommandType::SetMute:
        {
            channel->setMuted(cmd.boolValue);
            break;
        }

        case CommandType::SetSolo:
        {
            channel->setSolo(cmd.boolValue);
            break;
        }

        case CommandType::SetInputRouting:
        case CommandType::SetOutputRouting:
        {
            channel->setRouting(cmd.data.routing);
            break;
        }

        case CommandType::SetMIDIChannelFilter:
        {
            if (channel->getType() == ChannelType::VSTi)
                static_cast<VSTiChannel*>(channel)->setMIDIChannelFilter(cmd.intValue1);
            break;
        }

        case CommandType::SetPluginBypass:
        {
            channel->setPluginBypassed(cmd.intValue1, cmd.boolValue);
            break;
        }

        case CommandType::LoadPlugin:
            // Loading happens on message thread via loadPluginAsync()
            break;

        case CommandType::UnloadPlugin:
        {
            const int slot = cmd.intValue1;
            if (slot == -1 && channel->getType() == ChannelType::VSTi)
                static_cast<VSTiChannel*>(channel)->removeVSTi();
            else if (slot >= 0 && slot < 3)
                channel->removePlugin(slot);
            break;
        }

        case CommandType::ClearChannel:
        {
            const int curSec = activeGlobalSection.load(std::memory_order_relaxed);
            channel->clearSection(curSec);
            // Reset this section's loop length when no channels have content in it
            {
                bool anyContent = false;
                for (auto& ch : channels)
                    if (ch && ch->sectionHasContent(curSec)) { anyContent = true; break; }
                if (!anyContent)
                {
                    sectionLoopLengths[curSec].store(0, std::memory_order_release);
                    // Only reset global loop engine if clearing the active section
                    if (curSec == activeGlobalSection.load(std::memory_order_relaxed))
                    {
                        loopEngine->setLoopLength(0);
                        loopEngine->resetPlayhead();
                    }
                }
            }
            break;
        }

        case CommandType::CancelPending:
            channel->clearPendingActions();
            break;

        case CommandType::UndoOverdub:
            channel->undoLastOverdub();
            break;

        default:
            DBG("Unknown channel command: " + juce::String(static_cast<int>(cmd.type)));
            break;
    }
}

//==============================================================================
// Public Control Interface (Message Thread)
//==============================================================================

bool AudioEngine::sendCommand(const Command& cmd)
{
    // Pre-allocate overdub buffer on message thread before queuing
    if (cmd.type == CommandType::StartOverdub)
    {
        auto* ch = getChannel(cmd.channelIndex);
        if (ch)
            ch->stageOverdubBuffer(loopEngine->getLoopLength());
    }

    // Pre-allocate active section buffer before recording
    if (cmd.type == CommandType::StartRecord || cmd.type == CommandType::StartOverdub)
    {
        auto* ch = getChannel(cmd.channelIndex);
        if (ch)
            ch->allocateSection(ch->getActiveSection());
    }

    if (!commandQueue.pushCommand(cmd))
    {
        DBG("WARNING: Command queue full — command dropped.");
        return false;
    }
    return true;
}

void AudioEngine::setPlaying(bool shouldPlay)
{
    if (!shouldPlay)
    {
        // Memorize which channels are currently playing or overdubbing
        uint8_t mask = 0;
        for (int i = 0; i < 6; ++i)
        {
            auto* ch = channels[i].get();
            if (ch)
            {
                const auto st = ch->getState();
                if (st == ChannelState::Playing || st == ChannelState::Overdubbing)
                    mask |= static_cast<uint8_t>(1u << i);
            }
        }
        lastActiveChannels.store(mask, std::memory_order_release);
    }

    // When all channels are empty, always start (and stop) from position 0.
    if (!hasAnyRecordings())
        loopEngine->resetPlayhead();

    isPlayingFlag.store(shouldPlay, std::memory_order_release);

    if (shouldPlay)
    {
        // Start all channels that have recordings
        for (int i = 0; i < 6; ++i)
            if (channels[i] && channels[i]->hasLoop())
                sendCommand(Command::startPlayback(i));
    }
}

void AudioEngine::setOverdubMode(bool enabled)
{
    Command cmd;
    cmd.type      = CommandType::SetGlobalOverdubMode;
    cmd.boolValue = enabled;
    sendCommand(cmd);
}

void AudioEngine::emergencyStop()
{
    if (masterRecordingActive.load(std::memory_order_relaxed))
        stopMasterRecording();

    sendCommand(Command::emergencyStop());
}

void AudioEngine::doubleLoopLength()
{
    Command cmd;
    cmd.type = CommandType::DoubleLoopLength;
    sendCommand(cmd);
}

//==============================================================================
// Active Channel Navigation
//==============================================================================

void AudioEngine::setActiveChannel(int index)
{
    activeChannelIndex.store(juce::jlimit(0, 5, index), std::memory_order_release);
}

void AudioEngine::nextChannel()
{
    const int cur = activeChannelIndex.load(std::memory_order_relaxed);
    activeChannelIndex.store((cur + 1) % 6, std::memory_order_release);
}

void AudioEngine::prevChannel()
{
    const int cur = activeChannelIndex.load(std::memory_order_relaxed);
    activeChannelIndex.store((cur + 5) % 6, std::memory_order_release);
}

//==============================================================================
// Utility
//==============================================================================

void AudioEngine::clearOutputBuffer(float* const* outputChannelData,
                                    int numChannels, int numSamples)
{
    for (int ch = 0; ch < numChannels; ++ch)
        if (outputChannelData[ch])
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);
}

//==============================================================================
// Channel Management (Message Thread)
//==============================================================================

Channel* AudioEngine::getChannel(int index)
{
    return (index >= 0 && index < 6) ? channels[index].get() : nullptr;
}

void AudioEngine::setChannelType(int index, ChannelType type)
{
    if (index < 0 || index >= 6) return;
    if (channels[index] && channels[index]->getType() == type) return;

    const bool wasPlaying = isPlaying();
    if (wasPlaying) setPlaying(false);

    // Wait for the audio thread to finish any in-flight processBlock() on the old
    // channel before we destroy it.  setPlaying(false) only sets a flag — the IO
    // callback continues running.  Sleep for two block periods to ensure at least
    // one full callback completes after the flag was set.
    const int blockMs = (currentBufferSize > 0 && currentSampleRate > 0)
                      ? static_cast<int>(currentBufferSize * 1000.0 / currentSampleRate) + 5
                      : 15;
    juce::Thread::sleep(blockMs * 2);

    // Create and fully prepare the new channel BEFORE installing it, so the audio
    // thread never encounters it with uninitialised (zero-size) buffers.
    auto newChannel = (type == ChannelType::Audio)
                      ? std::unique_ptr<Channel>(std::make_unique<AudioChannel>(index))
                      : std::unique_ptr<Channel>(std::make_unique<VSTiChannel>(index));

    if (isInitialised.load(std::memory_order_relaxed))
    {
        const juce::int64 maxSamples = static_cast<juce::int64>(600.0 * currentSampleRate);
        newChannel->prepareToPlay(currentSampleRate, currentBufferSize, maxSamples);
    }

    channels[index] = std::move(newChannel);

    if (wasPlaying) setPlaying(true);

    DBG("Channel " + juce::String(index) + " → " +
        (type == ChannelType::Audio ? "Audio" : "VSTi"));
}

ChannelType AudioEngine::getChannelType(int index) const
{
    return (index >= 0 && index < 6 && channels[index])
           ? channels[index]->getType()
           : ChannelType::Audio;
}

//==============================================================================
// Plugin Management (Message Thread)
//==============================================================================

void AudioEngine::loadPluginAsync(int channelIndex,
                                   int slotIndex,
                                   const juce::String& pluginIdentifier,
                                   const juce::String& stateBase64,
                                   bool bypassed)
{
    if (channelIndex < 0 || channelIndex >= 6)
    {
        DBG("loadPluginAsync: invalid channel " + juce::String(channelIndex));
        return;
    }
    if (slotIndex < -1 || slotIndex >= 3)
    {
        DBG("loadPluginAsync: invalid slot " + juce::String(slotIndex));
        return;
    }
    auto description = pluginHost->findPluginByIdentifier(pluginIdentifier);
    if (description.name.isEmpty())
    {
        DBG("loadPluginAsync: plugin not found: " + pluginIdentifier);
        return;
    }

    DBG("loadPluginAsync: " + description.name +
        " → ch " + juce::String(channelIndex) +
        " slot " + juce::String(slotIndex) +
        (stateBase64.isNotEmpty() ? " (with saved state)" : ""));

    pluginHost->loadPluginAsync(
        description,
        currentSampleRate,
        currentBufferSize,
        [this, channelIndex, slotIndex, stateBase64, bypassed, descName = description.name]
        (std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::String& error)
        {
            if (!plugin)
            {
                DBG("loadPluginAsync: failed — " + error);
                if (onPluginLoadError)
                    onPluginLoadError(channelIndex, slotIndex,
                                      "Could not load: " + descName);
                return;
            }

            // Restore saved state BEFORE handing off to channel
            if (stateBase64.isNotEmpty())
            {
                const auto stateBlock = PluginHostWrapper::base64ToMemoryBlock(stateBase64);
                if (!pluginHost->loadPluginState(plugin.get(), stateBlock))
                    DBG("loadPluginAsync: state restore failed — ch " +
                        juce::String(channelIndex) + " slot " + juce::String(slotIndex));
            }

            auto* channel = channels[channelIndex].get();
            if (!channel)
            {
                DBG("loadPluginAsync: channel " + juce::String(channelIndex) +
                    " no longer exists");
                return;
            }

            if (slotIndex == -1)
            {
                if (channel->getType() == ChannelType::VSTi)
                {
                    static_cast<VSTiChannel*>(channel)->setVSTi(std::move(plugin));
                    DBG("VSTi loaded -> ch " + juce::String(channelIndex));
                }
                else
                {
                    DBG("loadPluginAsync: cannot load VSTi into Audio channel");
                }
            }
            else
            {
                channel->addPlugin(slotIndex, std::move(plugin));
                if (bypassed)
                    channel->setPluginBypassed(slotIndex, true);
                DBG("FX loaded → ch " + juce::String(channelIndex) +
                    " slot " + juce::String(slotIndex));
            }
        });
}

void AudioEngine::removePlugin(int channelIndex, int slotIndex)
{
    if (channelIndex < 0 || channelIndex >= 6) return;
    auto* channel = channels[channelIndex].get();
    if (!channel) return;

    if (slotIndex == -1)
    {
        if (channel->getType() == ChannelType::VSTi)
        {
            static_cast<VSTiChannel*>(channel)->removeVSTi();
            DBG("VSTi removed from ch " + juce::String(channelIndex));
        }
    }
    else if (slotIndex >= 0 && slotIndex < 3)
    {
        channel->removePlugin(slotIndex);
        DBG("FX removed from ch " + juce::String(channelIndex) +
            " slot " + juce::String(slotIndex));
    }
}

//==============================================================================
// Metronome (Message Thread)
//==============================================================================

void AudioEngine::setMetronomeEnabled(bool enabled)
{
    // Geblockt wenn Aufnahmen vorhanden
    if (hasAnyRecordings())
    {
        DBG("Metronome toggle blocked: recordings exist");
        return;
    }

    metronome->setEnabled(enabled);
    metronome->setBPM(loopEngine->getBPM());

    // Always reset loop length to 0: in metronome mode the loop length is
    // determined from the first recording (rounded to full bars), not from BPM.
    loopEngine->setLoopLength(0);
    for (int s = 0; s < NUM_SECTIONS; ++s)
        sectionLoopLengths[s].store(0, std::memory_order_release);

    DBG("Metronome " + juce::String(enabled ? "enabled" : "disabled"));
}

void AudioEngine::setMetronomeMuted(bool muted)
{
    // Route via command queue only — the audio thread applies the change.
    // A direct message-thread write would race with the audio thread reading isMuted.
    Command cmd;
    cmd.type      = CommandType::SetMetronomeMute;
    cmd.boolValue = muted;
    sendCommand(cmd);
}

void AudioEngine::setMetronomeOutput(int leftChannel, int rightChannel)
{
    metronome->setOutputChannels(leftChannel, rightChannel);

    Command cmd;
    cmd.type      = CommandType::SetMetronomeOutput;
    cmd.intValue1 = leftChannel;
    cmd.intValue2 = rightChannel;
    sendCommand(cmd);
}

void AudioEngine::setBeatsPerBar(int n)
{
    metronome->setBeatsPerBar(n);
}

int AudioEngine::getBeatsPerBar() const
{
    return metronome->getBeatsPerBar();
}

void AudioEngine::setMetronomeGain(float gain) { metronome->setMasterGain(gain); }
float AudioEngine::getMetronomeGain() const    { return metronome->getMasterGain(); }

void AudioEngine::setMasterGain(float gain)    { masterGain.store(juce::jlimit(0.0f, 1.0f, gain), std::memory_order_release); }
float AudioEngine::getMasterGain() const       { return masterGain.load(std::memory_order_relaxed); }

//==============================================================================
// Master Recording (Message Thread)
//==============================================================================

bool AudioEngine::startMasterRecording(const juce::File& directory)
{
    if (masterRecordingActive.load(std::memory_order_relaxed))
        return false;

    if (!directory.createDirectory())
        return false;

    auto filename = "master_" + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S") + ".wav";
    masterRecordFile = directory.getChildFile(filename);

    juce::WavAudioFormat wavFormat;
    auto fileStream = masterRecordFile.createOutputStream();
    if (!fileStream)
        return false;

    auto* writer = wavFormat.createWriterFor(fileStream.get(), currentSampleRate, 2, 24, {}, 0);
    if (!writer)
        return false;

    fileStream.release();  // writer now owns the stream

    masterRecordThread.startThread();
    masterRecordWriter.reset(new juce::AudioFormatWriter::ThreadedWriter(writer, masterRecordThread, 65536));
    masterRecordingActive.store(true, std::memory_order_release);
    return true;
}

void AudioEngine::stopMasterRecording()
{
    masterRecordingActive.store(false, std::memory_order_release);
    masterRecordWriter.reset();
    masterRecordThread.stopThread(2000);
}

bool AudioEngine::isMasterRecording() const
{
    return masterRecordingActive.load(std::memory_order_relaxed);
}

juce::File AudioEngine::getMasterRecordFile() const
{
    return masterRecordFile;
}

//==============================================================================
// Song Reset (Message Thread)
//==============================================================================

bool AudioEngine::hasAnyRecordings() const
{
    for (const auto& ch : channels)
        if (ch && ch->hasContentInAnySection())
            return true;
    return false;
}

void AudioEngine::resetSong()
{
    if (masterRecordingActive.load(std::memory_order_relaxed))
        stopMasterRecording();

    // Clear mute groups
    channelMuteGroup.fill(0);
    muteGroupActive.fill(false);

    // Reset section state on message thread side
    pendingGlobalSection.store(-1, std::memory_order_release);
    pendingSectionRecordChannel.store(-1, std::memory_order_release);

    sendCommand(Command::resetSong());
}

//==============================================================================
// Mute Groups (Message Thread)
//==============================================================================

void AudioEngine::setChannelMuteGroup(int channelIndex, int group)
{
    if (channelIndex < 0 || channelIndex >= 6) return;
    if (group < 0 || group > 4) return;

    const int oldGroup = channelMuteGroup[channelIndex];
    channelMuteGroup[channelIndex] = group;

    // If leaving an active group, unmute. If joining an active group, mute.
    auto* ch = getChannel(channelIndex);
    if (!ch) return;

    if (oldGroup >= 1 && oldGroup <= 4 && muteGroupActive[oldGroup - 1])
        ch->setMuted(false);
    if (group >= 1 && group <= 4 && muteGroupActive[group - 1])
        ch->setMuted(true);
}

int AudioEngine::getChannelMuteGroup(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= 6) return 0;
    return channelMuteGroup[channelIndex];
}

void AudioEngine::toggleMuteGroup(int groupIndex)
{
    if (groupIndex < 0 || groupIndex >= 4) return;
    setMuteGroupActive(groupIndex, !muteGroupActive[groupIndex]);
}

bool AudioEngine::isMuteGroupActive(int groupIndex) const
{
    if (groupIndex < 0 || groupIndex >= 4) return false;
    return muteGroupActive[groupIndex];
}

void AudioEngine::setMuteGroupActive(int groupIndex, bool active)
{
    if (groupIndex < 0 || groupIndex >= 4) return;
    muteGroupActive[groupIndex] = active;

    const int group = groupIndex + 1;  // 1-based
    for (int i = 0; i < 6; ++i)
    {
        if (channelMuteGroup[i] == group)
        {
            auto* ch = getChannel(i);
            if (ch) ch->setMuted(active);
        }
    }
}

//==============================================================================
// Auto-Start (Message Thread)
//==============================================================================

void AudioEngine::setAutoStart(bool enabled, float thresholdDb)
{
    autoStartEnabled.store(enabled, std::memory_order_release);
    const float linear = juce::Decibels::decibelsToGain(
        juce::jlimit(-60.0f, 0.0f, thresholdDb));
    autoStartThresholdLinear.store(linear, std::memory_order_release);
}

float AudioEngine::getAutoStartThresholdDb() const
{
    return juce::Decibels::gainToDecibels(
        autoStartThresholdLinear.load(std::memory_order_relaxed));
}

//==============================================================================
// Count-In (Message Thread)
//==============================================================================

void AudioEngine::setCountInBeats(int beats)
{
    countInBeats.store(juce::jlimit(0, 16, beats), std::memory_order_release);
}

void AudioEngine::setFixedLengthBars(int bars) { fixedLengthBars.store(bars, std::memory_order_release); }
int  AudioEngine::getFixedLengthBars() const   { return fixedLengthBars.load(std::memory_order_relaxed); }

//==============================================================================
// Channel Names (Message Thread)
//==============================================================================

juce::String AudioEngine::getChannelName(int index) const
{
    return (index >= 0 && index < 6) ? channelNames[index] : juce::String();
}

void AudioEngine::setChannelName(int index, const juce::String& name)
{
    if (index >= 0 && index < 6)
        channelNames[index] = name.trim().isEmpty()
                              ? "CH " + juce::String(index + 1)
                              : name.trim();
}

//==============================================================================
// A/B/C Sections (Message Thread)
//==============================================================================

void AudioEngine::setActiveSection(int section)
{
    if (section < 0 || section >= NUM_SECTIONS) return;
    if (section == activeGlobalSection.load(std::memory_order_relaxed))
    {
        // Cancel any pending latched switch and any queued record-ahead
        pendingGlobalSection.store(-1, std::memory_order_release);
        pendingSectionRecordChannel.store(-1, std::memory_order_release);
        return;
    }

    // Stop any recording/overdubbing channels first
    for (int i = 0; i < 6; ++i)
    {
        auto* ch = channels[i].get();
        if (!ch) continue;
        const auto cs = ch->getState();
        if (cs == ChannelState::Recording || cs == ChannelState::Overdubbing)
            sendCommand(Command::stopRecord(i));
    }

    // Pre-allocate section buffers for all channels
    for (auto& ch : channels)
        if (ch) ch->allocateSection(section);

    const bool latch = latchMode.load(std::memory_order_relaxed);
    const bool playing = isPlayingFlag.load(std::memory_order_relaxed);
    const juce::int64 loopLen = loopEngine->getLoopLength();

    if (latch && playing && loopLen > 0)
    {
        // Deferred: fires at loop boundary in audio callback
        pendingGlobalSection.store(section, std::memory_order_release);
    }
    else
    {
        // Immediate: send command
        Command cmd;
        cmd.type = CommandType::SetActiveSection;
        cmd.intValue1 = section;
        sendCommand(cmd);
    }
}

juce::int64 AudioEngine::getSectionLoopLength(int s) const
{
    if (s < 0 || s >= NUM_SECTIONS) return 0;
    return sectionLoopLengths[s].load(std::memory_order_relaxed);
}

void AudioEngine::setSectionLoopLength(int s, juce::int64 len)
{
    if (s >= 0 && s < NUM_SECTIONS)
        sectionLoopLengths[s].store(len, std::memory_order_release);
}

void AudioEngine::queueRecordForPendingSection(int channelIndex, bool isOverdub)
{
    if (channelIndex < 0 || channelIndex >= 6) return;
    if (pendingGlobalSection.load(std::memory_order_relaxed) < 0) return;  // no pending section

    pendingSectionRecordIsOverdub.store(isOverdub, std::memory_order_release);
    pendingSectionRecordChannel.store(channelIndex, std::memory_order_release);
}

void AudioEngine::cancelPendingSectionRecord()
{
    pendingSectionRecordChannel.store(-1, std::memory_order_release);
}
