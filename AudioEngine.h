#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include "Command.h"
#include "LoopEngine.h"
#include "PluginHostWrapper.h"
#include "Channel.h"
#include "MidiLearnManager.h"
#include "Metronome.h"          // NEW

// Forward declarations
class AudioChannel;
class VSTiChannel;

/**
 * @file AudioEngine.h
 * @brief Main audio processing engine
 */

//==============================================================================
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

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // MidiInputCallback interface
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    //==============================================================================
    // Public Control Interface (Message Thread)
    //==============================================================================

    bool sendCommand(const Command& cmd);

    juce::String initialiseAudio(int inputChannels  = 2,
                                 int outputChannels = 2,
                                 double sampleRate  = 0.0,
                                 int bufferSize     = 0);

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    LoopEngine& getLoopEngine() { return *loopEngine; }

    int    getNumInputChannels()  const { return numInputChannels; }
    int    getNumOutputChannels() const { return numOutputChannels; }
    double getSampleRate()        const { return currentSampleRate; }
    int    getBufferSize()        const { return currentBufferSize; }

    void openMidiInputs();

    MidiLearnManager& getMidiLearnManager() { return *midiLearnManager; }

    //==============================================================================
    // Channel Management
    //==============================================================================

    Channel*    getChannel(int index);
    void        setChannelType(int index, ChannelType type);
    ChannelType getChannelType(int index) const;

    //==============================================================================
    // Plugin Management
    //==============================================================================

    PluginHostWrapper& getPluginHost() { return *pluginHost; }

    void loadPluginAsync(int channelIndex,
                         int slotIndex,
                         const juce::String& pluginIdentifier);

    void removePlugin(int channelIndex, int slotIndex);

    //==============================================================================
    // Global State
    //==============================================================================

    void setPlaying(bool shouldPlay);
    bool isPlaying() const { return isPlayingFlag.load(std::memory_order_relaxed); }

    void setOverdubMode(bool enabled);
    bool isInOverdubMode() const { return overdubMode.load(std::memory_order_relaxed); }

    void emergencyStop();

    //==============================================================================
    // Metronome (NEW)
    //==============================================================================

    /**
     * @brief Direct access to the metronome for TransportComponent.
     * All setters on Metronome are atomic — safe to call from the message thread.
     */
    Metronome& getMetronome() { return *metronome; }

    /**
     * @brief Convenience: enable / disable metronome and send state to Song.
     * Wraps Metronome::setEnabled() and also stores the flag so it can be
     * persisted via SongManager.
     */
    void setMetronomeEnabled(bool enabled);

    /**
     * @brief Convenience: set metronome output channel pair.
     */
    void setMetronomeOutput(int leftChannel, int rightChannel);

    //==============================================================================
    // Diagnostics
    //==============================================================================

    double getCPUUsage()         const { return deviceManager.getCpuUsage() * 100.0; }
    int    getNumPendingCommands() const { return commandQueue.getNumPending(); }
    bool   isCommandQueueFull()   const { return commandQueue.isFull(); }

private:
    //==============================================================================
    // Core components
    juce::AudioDeviceManager         deviceManager;
    std::unique_ptr<LoopEngine>       loopEngine;
    std::unique_ptr<PluginHostWrapper> pluginHost;
    std::unique_ptr<MidiLearnManager> midiLearnManager;
    std::unique_ptr<Metronome>        metronome;       // NEW
    CommandQueue                      commandQueue;

    // Channels (6 stereo channels)
    std::array<std::unique_ptr<Channel>, 6> channels;

    //==============================================================================
    // Audio thread state
    int    numInputChannels  {0};
    int    numOutputChannels {0};
    double currentSampleRate {44100.0};
    int    currentBufferSize {512};

    // Global flags
    std::atomic<bool>         isPlayingFlag    {false};
    std::atomic<bool>         overdubMode      {false};
    std::atomic<bool>         isInitialised    {false};

    // Working buffers for audio thread
    juce::AudioBuffer<float> inputBuffer;
    juce::AudioBuffer<float> outputBuffer;

    // Performance monitoring
    std::atomic<juce::int64>  totalSamplesProcessed {0};
    std::atomic<juce::int32>  xrunCount             {0};

    //==============================================================================
    // Command processing (audio thread)
    void processCommand(const Command& cmd);
    void processGlobalCommand(const Command& cmd);
    void processChannelCommand(const Command& cmd);

    // Buffer management
    void clearOutputBuffer(float* const* outputChannelData, int numChannels, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
