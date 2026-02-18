#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include "Command.h"
#include "LoopEngine.h"
#include "PluginHostWrapper.h"
#include "Channel.h"
#include "MidiLearnManager.h"

// Forward declarations
class AudioChannel;
class VSTiChannel;

/**
 * @file AudioEngine.h
 * @brief Main audio processing engine
 * 
 * Central hub for all real-time audio processing:
 * - Manages AudioDeviceManager
 * - Processes command queue
 * - Coordinates 6 channels
 * - Manages global loop engine
 * - Routes audio between hardware I/O and channels
 */

//==============================================================================
/**
 * @brief Main audio engine managing all real-time processing
 * 
 * This is the bridge between:
 * - GUI (via command queue)
 * - Audio hardware (via AudioIODeviceCallback)
 * - Processing channels
 * - Loop engine
 * 
 * Thread-safety:
 * - Audio thread: audioDeviceIOCallback()
 * - Message thread: sendCommand(), loadSong(), etc.
 */
class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback
{
public:
    //==============================================================================
    AudioEngine();
    ~AudioEngine() override;
    
    //==============================================================================
    // AudioIODeviceCallback interface
    //==============================================================================
    
    /**
     * @brief Main audio callback - MUST BE REAL-TIME SAFE
     * 
     * Processing order:
     * 1. Process commands from queue
     * 2. Advance global playhead
     * 3. Process each channel
     * 4. Mix outputs
     */
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;


    
    /**
     * @brief Called when audio device is about to start
     */
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    
    /**
     * @brief Called when audio device stops
     */
    void audioDeviceStopped() override;
    
    /**
     * @brief Get the MIDI learn manager
     */
    MidiLearnManager& getMidiLearnManager() { return *midiLearnManager; }

    /**
     * @brief Open all available MIDI inputs
     */
    void openMidiInputs();

    // MidiInputCallback interface
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    //==============================================================================
    // Public Control Interface (Message Thread)
    //==============================================================================
    
    /**
     * @brief Send a command to the audio thread
     * @return true if command was queued successfully
     */
    bool sendCommand(const Command& cmd);
    
    /**
     * @brief Initialize audio device
     * @param inputChannels Number of input channels to enable
     * @param outputChannels Number of output channels to enable
     * @param sampleRate Desired sample rate (0 = use device default)
     * @param bufferSize Desired buffer size (0 = use device default)
     * @return Error string if failed, empty string if success
     */
    juce::String initialiseAudio(int inputChannels = 2,
                                 int outputChannels = 2,
                                 double sampleRate = 0.0,
                                 int bufferSize = 0);
    
    /**
     * @brief Get the audio device manager
     */
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    
    /**
     * @brief Get the loop engine
     */
    LoopEngine& getLoopEngine() { return *loopEngine; }
    
    /**
     * @brief Get number of available input channels
     */
    int getNumInputChannels() const { return numInputChannels; }
    
    /**
     * @brief Get number of available output channels
     */
    int getNumOutputChannels() const { return numOutputChannels; }
    
    /**
     * @brief Get current sample rate
     */
    double getSampleRate() const { return currentSampleRate; }
    
    /**
     * @brief Get current buffer size
     */
    int getBufferSize() const { return currentBufferSize; }
    
    //==============================================================================
    // Channel Management
    //==============================================================================
    
    /**
     * @brief Get a channel by index
     * @param index Channel index (0-5)
     * @return Pointer to channel or nullptr if invalid index
     */
    Channel* getChannel(int index);
    
    /**
     * @brief Set channel type (Audio or VSTi)
     * WARNING: This clears the channel's loop and plugins
     * @param index Channel index (0-5)
     * @param type New channel type
     */
    void setChannelType(int index, ChannelType type);
    
    /**
     * @brief Get channel type
     */
    ChannelType getChannelType(int index) const;
    
    //==============================================================================
    // Plugin Management
    //==============================================================================
    
    /**
     * @brief Get the plugin host wrapper
     */
    PluginHostWrapper& getPluginHost() { return *pluginHost; }
    
    /**
     * @brief Load plugin into channel slot (async)
     * @param channelIndex Channel (0-5)
     * @param slotIndex FX slot (0-2) or -1 for VSTi
     * @param pluginIdentifier Plugin identifier string
     */
    void loadPluginAsync(int channelIndex, int slotIndex, const juce::String& pluginIdentifier);
    
    /**
     * @brief Remove plugin from channel slot
     * @param channelIndex Channel (0-5)
     * @param slotIndex FX slot (0-2) or -1 for VSTi
     */
    void removePlugin(int channelIndex, int slotIndex);
    
    //==============================================================================
    // Global State
    //==============================================================================
    
    /**
     * @brief Start/stop global playback
     */
    void setPlaying(bool shouldPlay);
    
    /**
     * @brief Check if engine is playing
     */
    bool isPlaying() const { return isPlayingFlag.load(std::memory_order_relaxed); }
    
    /**
     * @brief Set global overdub mode
     */
    void setOverdubMode(bool enabled);
    
    /**
     * @brief Check if in overdub mode
     */
    bool isInOverdubMode() const { return overdubMode.load(std::memory_order_relaxed); }
    
    /**
     * @brief Emergency stop - stops all audio processing immediately
     */
    void emergencyStop();
    
    //==============================================================================
    // Diagnostics
    //==============================================================================
    
    /**
     * @brief Get CPU usage percentage
     */
    double getCPUUsage() const { return deviceManager.getCpuUsage() * 100.0; }
    
    /**
     * @brief Get number of commands pending in queue
     */
    int getNumPendingCommands() const { return commandQueue.getNumPending(); }
    
    /**
     * @brief Check if command queue is full (diagnostic warning)
     */
    bool isCommandQueueFull() const { return commandQueue.isFull(); }
    
private:
    //==============================================================================
    // Core components
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<LoopEngine> loopEngine;
    std::unique_ptr<PluginHostWrapper> pluginHost;
    std::unique_ptr<MidiLearnManager> midiLearnManager;
    CommandQueue commandQueue;
    
    // Channels (6 stereo channels)
    std::array<std::unique_ptr<Channel>, 6> channels;
    
    //==============================================================================
    // Audio thread state
    int numInputChannels{0};
    int numOutputChannels{0};
    double currentSampleRate{44100.0};
    int currentBufferSize{512};
    
    // Global flags
    std::atomic<bool> isPlayingFlag{false};
    std::atomic<bool> overdubMode{false};
    std::atomic<bool> isInitialised{false};
    
    // Working buffers for audio thread
    juce::AudioBuffer<float> inputBuffer;
    juce::AudioBuffer<float> outputBuffer;
    
    // Performance monitoring
    std::atomic<juce::int64> totalSamplesProcessed{0};
    std::atomic<juce::int32> xrunCount{0};  // Buffer overrun counter
    
    //==============================================================================
    // Command processing (audio thread)
    void processCommand(const Command& cmd);
    void processGlobalCommand(const Command& cmd);
    void processChannelCommand(const Command& cmd);
    
    // Buffer management
    void clearOutputBuffer(float* const* outputChannelData, int numChannels, int numSamples);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
