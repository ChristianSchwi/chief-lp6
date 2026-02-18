#include "AudioEngine.h"
#include "AudioChannel.h"
#include "VSTiChannel.h"
#include "PluginHostWrapper.h"

//==============================================================================
AudioEngine::AudioEngine()
{
    loopEngine       = std::make_unique<LoopEngine>();
    pluginHost       = std::make_unique<PluginHostWrapper>();
    midiLearnManager = std::make_unique<MidiLearnManager>(*this);
    
    // Create 6 audio channels (default to Audio type)
    for (int i = 0; i < 6; ++i)
    {
        channels[i] = std::make_unique<AudioChannel>(i);
    }
    
    // Set up audio device manager
    deviceManager.addAudioCallback(this);
    
    // Note: Plugin scanning removed from constructor to avoid blocking
    // Call audioEngine->getPluginHost().scanForPlugins() manually when ready
}

AudioEngine::~AudioEngine()
{
    // Close all open MIDI inputs
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
    // Enable all available MIDI inputs and register this as callback
    const auto midiInputs = juce::MidiInput::getAvailableDevices();

    for (const auto& device : midiInputs)
    {
        if (!deviceManager.isMidiInputDeviceEnabled(device.identifier))
            deviceManager.setMidiInputDeviceEnabled(device.identifier, true);

        deviceManager.addMidiInputDeviceCallback(device.identifier, this);
        DBG("MIDI Input geöffnet: " + device.name);
    }

    DBG("MIDI Inputs: " + juce::String(midiInputs.size()) + " Gerät(e) gefunden");
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput* /*source*/,
                                             const juce::MidiMessage& message)
{
    // Läuft auf dem MIDI-Thread — nur lock-free Operationen!
    if (midiLearnManager)
        midiLearnManager->postMidiMessage(message);
}

//==============================================================================
// Audio Device Initialization
//==============================================================================

juce::String AudioEngine::initialiseAudio(int inputChannels,
                                          int outputChannels,
                                          double sampleRate,
                                          int bufferSize)
{
    // Create setup for audio device
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    
    // Configure inputs/outputs
    setup.inputChannels.setRange(0, inputChannels, true);
    setup.outputChannels.setRange(0, outputChannels, true);
    
    if (sampleRate > 0.0)
        setup.sampleRate = sampleRate;
    
    if (bufferSize > 0)
        setup.bufferSize = bufferSize;
    
    // Initialize device
    juce::String error = deviceManager.initialise(inputChannels,
                                                  outputChannels,
                                                  nullptr,
                                                  true,
                                                  juce::String(),
                                                  &setup);
    
    if (error.isEmpty())
    {
        isInitialised.store(true, std::memory_order_release);
        openMidiInputs();
        DBG("Audio engine initialized successfully");
        DBG("Sample Rate: " + juce::String(currentSampleRate));
        DBG("Buffer Size: " + juce::String(currentBufferSize));
        DBG("Input Channels: " + juce::String(numInputChannels));
        DBG("Output Channels: " + juce::String(numOutputChannels));
    }
    else
    {
        DBG("Audio engine initialization failed: " + error);
    }
    
    return error;
}

//==============================================================================
// AudioIODeviceCallback Implementation
//==============================================================================

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr)
        return;
    
    // Store device parameters
    currentSampleRate = device->getCurrentSampleRate();
    currentBufferSize = device->getCurrentBufferSizeSamples();
    numInputChannels = device->getActiveInputChannels().countNumberOfSetBits();
    numOutputChannels = device->getActiveOutputChannels().countNumberOfSetBits();
    
    // Configure loop engine
    loopEngine->setSampleRate(currentSampleRate);
    
    // Pre-allocate working buffers
    inputBuffer.setSize(numInputChannels, currentBufferSize * 2);  // Double for safety
    outputBuffer.setSize(numOutputChannels, currentBufferSize * 2);
    
    // Prepare all channels
    // Max loop length: 10 minutes @ 96kHz stereo = 57,600,000 samples
    const juce::int64 maxLoopLengthSamples = static_cast<juce::int64>(600.0 * currentSampleRate);
    
    for (auto& channel : channels)
    {
        if (channel)
        {
            channel->prepareToPlay(currentSampleRate, currentBufferSize, maxLoopLengthSamples);
        }
    }
    
    DBG("Audio device about to start:");
    DBG("  Sample Rate: " + juce::String(currentSampleRate) + " Hz");
    DBG("  Buffer Size: " + juce::String(currentBufferSize) + " samples");
    DBG("  Input Channels: " + juce::String(numInputChannels));
    DBG("  Output Channels: " + juce::String(numOutputChannels));
    DBG("  Latency: " + juce::String(currentBufferSize / currentSampleRate * 1000.0, 2) + " ms");
}

void AudioEngine::audioDeviceStopped()
{
    DBG("Audio device stopped");
    
    // Reset state
    isPlayingFlag.store(false, std::memory_order_release);
    
    // Release channel resources
    for (auto& channel : channels)
    {
        if (channel)
        {
            channel->releaseResources();
        }
    }
    
    // Clear working buffers
    inputBuffer.clear();
    outputBuffer.clear();
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& context)

{
    //==============================================================================
    // REAL-TIME AUDIO THREAD - NO ALLOCATIONS, NO LOCKS, NO BLOCKING
    //==============================================================================
    
    // Safety check
    if (numSamples <= 0 || !isInitialised.load(std::memory_order_relaxed))
    {
        clearOutputBuffer(outputChannelData, numOutputChannels, numSamples);
        return;
    }
    
    //==============================================================================
    // 1. PROCESS COMMANDS FROM QUEUE
    //==============================================================================
    commandQueue.processCommands([this](const Command& cmd) {
        processCommand(cmd);
    });
    
    //==============================================================================
    // 2. CLEAR OUTPUT BUFFER
    //==============================================================================
    clearOutputBuffer(outputChannelData, numOutputChannels, numSamples);
    
    //==============================================================================
    // 3. ADVANCE GLOBAL PLAYHEAD
    //==============================================================================
    const bool playing = isPlayingFlag.load(std::memory_order_relaxed);
    loopEngine->processBlock(numSamples, playing);
    
    //==============================================================================
    // 4. PROCESS EACH CHANNEL
    //==============================================================================
    const juce::int64 playheadPos = loopEngine->getCurrentPlayhead();
    const juce::int64 loopLen = loopEngine->getLoopLength();
    
    // Empty MIDI buffer for now (Phase 6 will add MIDI processor)
    juce::MidiBuffer emptyMidi;
    
    for (auto& channel : channels)
    {
        if (channel)
        {
            channel->processBlock(inputChannelData,
                                outputChannelData,
                                emptyMidi,
                                numSamples,
                                playheadPos,
                                loopLen,
                                numInputChannels,
                                numOutputChannels);
        }
    }
    
    //==============================================================================
    // 5. GENERATE METRONOME (if enabled)
    //==============================================================================
    // TODO: Phase 6 - Metronome generation
    
    //==============================================================================
    // UPDATE DIAGNOSTICS
    //==============================================================================
    totalSamplesProcessed.fetch_add(numSamples, std::memory_order_relaxed);
}

//==============================================================================
// Command Processing (Audio Thread)
//==============================================================================

void AudioEngine::processCommand(const Command& cmd)
{
    // Route command to appropriate handler
    if (cmd.channelIndex >= 0 && cmd.channelIndex < 6)
    {
        processChannelCommand(cmd);
    }
    else
    {
        processGlobalCommand(cmd);
    }
}

void AudioEngine::processGlobalCommand(const Command& cmd)
{
    switch (cmd.type)
    {
        case CommandType::SetBPM:
        {
            const double bpm = static_cast<double>(cmd.floatValue);
            loopEngine->setBPM(bpm);
            break;
        }
        
        case CommandType::SetBeatsPerLoop:
        {
            loopEngine->setBeatsPerLoop(cmd.intValue1);
            loopEngine->calculateLoopLengthFromBPM();
            break;
        }
        
        case CommandType::SetLoopLength:
        {
            const juce::int64 length = cmd.getLoopLength();
            loopEngine->setLoopLength(length);
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
        
        case CommandType::EmergencyStop:
        {
            isPlayingFlag.store(false, std::memory_order_release);
            loopEngine->resetPlayhead();
            // TODO: Stop all channels
            break;
        }
        
        default:
            // Unknown command type
            DBG("Unknown global command type: " + juce::String(static_cast<int>(cmd.type)));
            break;
    }
}

void AudioEngine::processChannelCommand(const Command& cmd)
{
    // Validate channel index
    if (cmd.channelIndex < 0 || cmd.channelIndex >= 6)
        return;
    
    Channel* channel = channels[cmd.channelIndex].get();
    if (!channel)
        return;
    
    switch (cmd.type)
    {
        case CommandType::StartRecord:
        {
            channel->startRecording(false);
            break;
        }
        
        case CommandType::StopRecord:
        {
            channel->stopRecording();
            break;
        }
        
        case CommandType::StartPlayback:
        {
            channel->startPlayback();
            break;
        }
        
        case CommandType::StopPlayback:
        {
            channel->stopPlayback();
            break;
        }
        
        case CommandType::StartOverdub:
        {
            channel->startRecording(true);  // true = overdub mode
            break;
        }
        
        case CommandType::StopOverdub:
        {
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
            // Only for VSTi channels
            if (channel->getType() == ChannelType::VSTi)
            {
                VSTiChannel* vstiChannel = static_cast<VSTiChannel*>(channel);
                vstiChannel->setMIDIChannelFilter(cmd.intValue1);
            }
            break;
        }
        
        case CommandType::SetPluginBypass:
        {
            channel->setPluginBypassed(cmd.intValue1, cmd.boolValue);
            break;
        }
        
        case CommandType::LoadPlugin:
        {
            // Note: Actual loading happens in message thread via loadPluginAsync()
            // This command can trigger state updates if needed
            break;
        }
        
        case CommandType::UnloadPlugin:
        {
            const int slot = cmd.intValue1;
            
            if (slot == -1 && channel->getType() == ChannelType::VSTi)
            {
                auto* vstiChannel = static_cast<VSTiChannel*>(channel);
                vstiChannel->removeVSTi();
            }
            else if (slot >= 0 && slot < 3)
            {
                channel->removePlugin(slot);
            }
            break;
        }
        
        case CommandType::ClearChannel:
        {
            channel->clearLoop();
            break;
        }
        
        default:
            break;
    }
}

//==============================================================================
// Public Control Interface (Message Thread)
//==============================================================================

bool AudioEngine::sendCommand(const Command& cmd)
{
    bool success = commandQueue.pushCommand(cmd);
    
    if (!success)
    {
        DBG("WARNING: Command queue full! Command dropped.");
        // Could trigger a warning to GUI here via AsyncUpdater
    }
    
    return success;
}

void AudioEngine::setPlaying(bool shouldPlay)
{
    isPlayingFlag.store(shouldPlay, std::memory_order_release);
    
    if (shouldPlay)
    {
        DBG("Playback started");
    }
    else
    {
        DBG("Playback stopped");
    }
}

void AudioEngine::setOverdubMode(bool enabled)
{
    Command cmd;
    cmd.type = CommandType::SetGlobalOverdubMode;
    cmd.boolValue = enabled;
    sendCommand(cmd);
}

void AudioEngine::emergencyStop()
{
    sendCommand(Command::emergencyStop());
}

//==============================================================================
// Utility Methods
//==============================================================================

void AudioEngine::clearOutputBuffer(float* const* outputChannelData, int numChannels, int numSamples)
{
    for (int ch = 0; ch < numChannels; ++ch)
    {
        if (outputChannelData[ch] != nullptr)
        {
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);
        }
    }
}

//==============================================================================
// Channel Management (Message Thread)
//==============================================================================

Channel* AudioEngine::getChannel(int index)
{
    if (index >= 0 && index < 6)
        return channels[index].get();
    
    return nullptr;
}

void AudioEngine::setChannelType(int index, ChannelType type)
{
    if (index < 0 || index >= 6)
        return;
    
    // Get current type
    if (channels[index] && channels[index]->getType() == type)
        return;  // Already correct type
    
    // Stop playback first (safety)
    bool wasPlaying = isPlaying();
    if (wasPlaying)
        setPlaying(false);
    
    // Create new channel of appropriate type
    if (type == ChannelType::Audio)
    {
        channels[index] = std::make_unique<AudioChannel>(index);
    }
    else
    {
        channels[index] = std::make_unique<VSTiChannel>(index);
    }
    
    // Prepare if device is active
    if (isInitialised.load(std::memory_order_relaxed))
    {
        const juce::int64 maxLoopLengthSamples = static_cast<juce::int64>(600.0 * currentSampleRate);
        channels[index]->prepareToPlay(currentSampleRate, currentBufferSize, maxLoopLengthSamples);
    }
    
    // Resume playback if it was active
    if (wasPlaying)
        setPlaying(true);
    
    DBG("Channel " + juce::String(index) + " type changed to " + 
        (type == ChannelType::Audio ? "Audio" : "VSTi"));
}

ChannelType AudioEngine::getChannelType(int index) const
{
    if (index >= 0 && index < 6 && channels[index])
        return channels[index]->getType();
    
    return ChannelType::Audio;  // Default
}

//==============================================================================
// Plugin Management (Message Thread)
//==============================================================================

void AudioEngine::loadPluginAsync(int channelIndex, int slotIndex, const juce::String& pluginIdentifier)
{
    if (channelIndex < 0 || channelIndex >= 6)
    {
        DBG("Invalid channel index: " + juce::String(channelIndex));
        return;
    }
    
    if (slotIndex < -1 || slotIndex >= 3)
    {
        DBG("Invalid slot index: " + juce::String(slotIndex));
        return;
    }
    
    // Find plugin description
    auto description = pluginHost->findPluginByIdentifier(pluginIdentifier);
    
    if (description.name.isEmpty())
    {
        DBG("Plugin not found: " + pluginIdentifier);
        return;
    }
    
    DBG("Loading plugin: " + description.name + " into channel " + 
        juce::String(channelIndex) + ", slot " + juce::String(slotIndex));
    
    // Load plugin asynchronously
    pluginHost->loadPluginAsync(
        description,
        currentSampleRate,
        currentBufferSize,
        [this, channelIndex, slotIndex](std::unique_ptr<juce::AudioPluginInstance> plugin,
                                        const juce::String& error)
        {
            if (!plugin)
            {
                DBG("Failed to load plugin: " + error);
                return;
            }
            
            // Get the channel
            auto* channel = channels[channelIndex].get();
            if (!channel)
                return;
            
            if (slotIndex == -1)
            {
                // Load as VSTi (only for VSTi channels)
                if (channel->getType() == ChannelType::VSTi)
                {
                    auto* vstiChannel = static_cast<VSTiChannel*>(channel);
                    vstiChannel->setVSTi(std::move(plugin));
                    DBG("VSTi loaded into channel " + juce::String(channelIndex));
                }
                else
                {
                    DBG("Cannot load VSTi into Audio channel");
                }
            }
            else
            {
                // Load as FX
                channel->addPlugin(slotIndex, std::move(plugin));
                DBG("FX loaded into channel " + juce::String(channelIndex) + 
                    ", slot " + juce::String(slotIndex));
            }
        });
}

void AudioEngine::removePlugin(int channelIndex, int slotIndex)
{
    if (channelIndex < 0 || channelIndex >= 6)
        return;
    
    auto* channel = channels[channelIndex].get();
    if (!channel)
        return;
    
    if (slotIndex == -1)
    {
        // Remove VSTi
        if (channel->getType() == ChannelType::VSTi)
        {
            auto* vstiChannel = static_cast<VSTiChannel*>(channel);
            vstiChannel->removeVSTi();
            DBG("VSTi removed from channel " + juce::String(channelIndex));
        }
    }
    else if (slotIndex >= 0 && slotIndex < 3)
    {
        // Remove FX
        channel->removePlugin(slotIndex);
        DBG("FX removed from channel " + juce::String(channelIndex) + 
            ", slot " + juce::String(slotIndex));
    }
}
