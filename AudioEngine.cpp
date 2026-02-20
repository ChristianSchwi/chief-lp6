#include "AudioEngine.h"
#include "AudioChannel.h"
#include "VSTiChannel.h"
#include "PluginHostWrapper.h"

//==============================================================================
// Constructor / Destructor
//==============================================================================

AudioEngine::AudioEngine()
{
    loopEngine       = std::make_unique<LoopEngine>();
    metronome        = std::make_unique<Metronome>();
    pluginHost       = std::make_unique<PluginHostWrapper>();
    midiLearnManager = std::make_unique<MidiLearnManager>(*this);

    for (int i = 0; i < 6; ++i)
        channels[i] = std::make_unique<AudioChannel>(i);

    deviceManager.addAudioCallback(this);
}

AudioEngine::~AudioEngine()
{
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
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);

    setup.inputChannels .setRange(0, inputChannels,  true);
    setup.outputChannels.setRange(0, outputChannels, true);

    if (sampleRate > 0.0) setup.sampleRate = sampleRate;
    if (bufferSize  > 0)  setup.bufferSize  = bufferSize;

    const juce::String error = deviceManager.initialise(inputChannels,
                                                        outputChannels,
                                                        nullptr,
                                                        true,
                                                        juce::String(),
                                                        &setup);
    if (error.isEmpty())
    {
        isInitialised.store(true, std::memory_order_release);
        openMidiInputs();
        DBG("Audio engine initialized: " +
            juce::String(currentSampleRate) + " Hz, " +
            juce::String(currentBufferSize) + " samples, " +
            juce::String(numInputChannels) + " in / " +
            juce::String(numOutputChannels) + " out");
    }
    else
    {
        DBG("Audio engine init failed: " + error);
    }

    return error;
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
    loopEngine->processBlock(numSamples, playing);

    //--- 3b. STOP RECORDING IF TRANSPORT IS NOT RUNNING -----------------------
    // Prevents recording channels from overwriting loop data at a frozen playhead.
    if (!playing)
    {
        for (auto& ch : channels)
        {
            if (ch)
            {
                const auto cs = ch->getState();
                if (cs == ChannelState::Recording || cs == ChannelState::Overdubbing)
                    ch->stopRecording();
            }
        }

        // Cancel pending count-in and autostart when transport stops
        if (countInActive)
        {
            countInActive        = false;
            pendingRecordChannel = -1;
        }
        autoStartTriggered = false;
    }

    //--- 3c. AUTO-START THRESHOLD CHECK ----------------------------------------
    if (autoStartEnabled.load(std::memory_order_relaxed) && !playing && !autoStartTriggered
        && !countInActive)
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
    if (countInActive)
    {
        countInSamplesRemaining -= numSamples;
        if (countInSamplesRemaining <= 0)
        {
            countInActive = false;
            if (pendingRecordChannel >= 0 && pendingRecordChannel < 6)
            {
                if (auto* ch = channels[pendingRecordChannel].get())
                {
                    if (!metronome->getEnabled() && loopEngine->getLoopLength() == 0)
                        loopEngine->resetPlayhead();
                    ch->startRecording(false);
                }
            }
            pendingRecordChannel = -1;
        }
    }

    //--- 4. COLLECT MIDI -------------------------------------------------------
    // Thread-safe: MIDI thread writes via addMessageToQueue(),
    // audio thread reads here via removeNextBlockOfMessages()
    juce::MidiBuffer midiBuffer;
    midiCollector.removeNextBlockOfMessages(midiBuffer, numSamples);

    //--- 5. PROCESS CHANNELS ---------------------------------------------------
    const juce::int64 playheadPos = loopEngine->getCurrentPlayhead();
    const juce::int64 loopLen     = loopEngine->getLoopLength();

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

    //--- 6. METRONOME ----------------------------------------------------------
    metronome->processBlock(outputChannelData,
                            numOutputChannels,
                            numSamples,
                            playheadPos,
                            playing);

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
            if (isPlayingFlag.load(std::memory_order_relaxed)) { DBG("SetBPM ignored: playing"); break; }
            const double bpm = static_cast<double>(cmd.floatValue);
            loopEngine->setBPM(bpm);
            metronome->setBPM(bpm);
            // Wenn Metronom aktiv: Loop-Länge sofort aus neuem BPM berechnen
            if (metronome->getEnabled())
                loopEngine->calculateLoopLengthFromBPM();
            break;
        }

        case CommandType::SetBeatsPerLoop:
        {
            if (isPlayingFlag.load(std::memory_order_relaxed)) { DBG("SetBeatsPerLoop ignored: playing"); break; }
            loopEngine->setBeatsPerLoop(cmd.intValue1);
            // Nur neu berechnen wenn Metronom aktiv (sonst freier Modus)
            if (metronome->getEnabled())
                loopEngine->calculateLoopLengthFromBPM();
            break;
        }

        case CommandType::SetLoopLength:
        {
            loopEngine->setLoopLength(cmd.getLoopLength());
            break;
        }

        case CommandType::SetQuantization:
        {
            loopEngine->setQuantizationEnabled(cmd.boolValue);
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
            for (auto& ch : channels)
                if (ch) ch->clearLoop();

            loopEngine->setLoopLength(0);
            loopEngine->resetPlayhead();

            if (metronome->getEnabled())
                loopEngine->calculateLoopLengthFromBPM();

            isPlayingFlag.store(false, std::memory_order_release);
            DBG("Song reset: all channels cleared");
            break;
        }

        case CommandType::EmergencyStop:
        {
            isPlayingFlag.store(false, std::memory_order_release);
            loopEngine->resetPlayhead();
            for (auto& ch : channels)
                if (ch) ch->stopPlayback();
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
            // Auto-start transport so the playhead advances (needed for count-in + recording)
            isPlayingFlag.store(true, std::memory_order_release);

            const int ci = countInBeats.load(std::memory_order_relaxed);
            if (ci > 0 && !countInActive)
            {
                // Count-in phase: transport runs (metronome clicks), recording deferred
                const double bpm = loopEngine->getBPM();
                const double spb = (60.0 / juce::jmax(1.0, bpm)) * currentSampleRate;
                countInSamplesRemaining = static_cast<juce::int64>(
                    juce::jmax(1, static_cast<int>(ci * spb)));
                countInActive        = true;
                pendingRecordChannel = cmd.channelIndex;
                DBG("Count-in: " + juce::String(ci) + " beat(s) = " +
                    juce::String(countInSamplesRemaining) + " samples");
            }
            else
            {
                // Immediate recording
                if (!metronome->getEnabled() && loopEngine->getLoopLength() == 0)
                    loopEngine->resetPlayhead();
                channel->startRecording(false);
            }
            break;
        }

        case CommandType::StopRecord:
        {
            // Freier Modus: erste beendete Aufnahme setzt globale Loop-Länge
            if (!metronome->getEnabled() && loopEngine->getLoopLength() == 0)
            {
                const juce::int64 recorded = loopEngine->getCurrentPlayhead();
                if (recorded > 0)
                {
                    loopEngine->setLoopLength(recorded);
                    DBG("Loop length from first recording: " +
                        juce::String(recorded) + " samples (" +
                        juce::String(static_cast<double>(recorded) / currentSampleRate, 2) + "s)");
                }
                loopEngine->resetPlayhead();
                channel->stopRecording();
            }
            else
            {
                channel->requestStopAtLoopEnd();
            }
            break;
        }

        case CommandType::StartPlayback:
        {
            channel->startPlayback();
            break;
        }

        case CommandType::StopPlayback:
        {
            if (loopEngine->getLoopLength() == 0)
                channel->stopPlayback();
            else
                channel->requestStopAtLoopEnd();
            break;
        }

        case CommandType::StartOverdub:
        {
            isPlayingFlag.store(true, std::memory_order_release);
            channel->startRecording(true);
            break;
        }

        case CommandType::StopOverdub:
        {
            if (loopEngine->getLoopLength() == 0)
                channel->stopRecording();
            else
                channel->requestStopAtLoopEnd();
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
            channel->clearLoop();
            break;
        }

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
    if (!commandQueue.pushCommand(cmd))
    {
        DBG("WARNING: Command queue full — command dropped.");
        return false;
    }
    return true;
}

void AudioEngine::setPlaying(bool shouldPlay)
{
    isPlayingFlag.store(shouldPlay, std::memory_order_release);
  //  DBG(shouldPlay ? "Playback started" : "Playback stopped");
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
    sendCommand(Command::emergencyStop());
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

    channels[index] = (type == ChannelType::Audio)
                      ? std::unique_ptr<Channel>(std::make_unique<AudioChannel>(index))
                      : std::unique_ptr<Channel>(std::make_unique<VSTiChannel>(index));

    if (isInitialised.load(std::memory_order_relaxed))
    {
        const juce::int64 maxSamples = static_cast<juce::int64>(600.0 * currentSampleRate);
        channels[index]->prepareToPlay(currentSampleRate, currentBufferSize, maxSamples);
    }

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
                                   const juce::String& stateBase64)
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
    if (isPlayingFlag.load(std::memory_order_relaxed))
    {
        DBG("loadPluginAsync: blocked — engine is playing (Spec §10)");
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
        [this, channelIndex, slotIndex, stateBase64]
        (std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::String& error)
        {
            if (!plugin)
            {
                DBG("loadPluginAsync: failed — " + error);
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
                    DBG("VSTi loaded → ch " + juce::String(channelIndex));
                }
                else
                {
                    DBG("loadPluginAsync: cannot load VSTi into Audio channel");
                }
            }
            else
            {
                channel->addPlugin(slotIndex, std::move(plugin));
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
    if (isPlayingFlag.load(std::memory_order_relaxed))
    {
        DBG("removePlugin: blocked — engine is playing (Spec §10)");
        return;
    }

    if (slotIndex == -1 && channel->getType() == ChannelType::VSTi)
    {
        static_cast<VSTiChannel*>(channel)->removeVSTi();
        DBG("VSTi removed from ch " + juce::String(channelIndex));
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

    if (enabled)
        loopEngine->calculateLoopLengthFromBPM();   // AN  → Loop-Länge aus BPM
    else
        loopEngine->setLoopLength(0);               // AUS → freier Modus

    DBG("Metronome " + juce::String(enabled ? "enabled" : "disabled"));
}

void AudioEngine::setMetronomeMuted(bool muted)
{
    // Direkt per Atomic + Command für Audio-Thread
    metronome->setMuted(muted);

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

//==============================================================================
// Song Reset (Message Thread)
//==============================================================================

bool AudioEngine::hasAnyRecordings() const
{
    for (const auto& ch : channels)
        if (ch && ch->hasLoop())
            return true;
    return false;
}

void AudioEngine::resetSong()
{
    sendCommand(Command::resetSong());
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
