#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "Command.h"

/**
 * @file Channel.h
 * @brief Base class for audio and VSTi channels
 * 
 * Each channel has:
 * - State machine (Idle, Recording, Playing, Overdubbing)
 * - Loop buffer (pre-allocated)
 * - 3 FX plugin slots
 * - Routing configuration
 * - Gain and monitoring controls
 */

//==============================================================================
/**
 * @brief Channel type enumeration
 */
enum class ChannelType
{
    Audio,  ///< Audio input channel with hardware routing
    VSTi    ///< Virtual instrument channel with MIDI input
};

//==============================================================================
/**
 * @brief Channel state machine
 */
enum class ChannelState
{
    Idle,        ///< Not recording or playing
    Recording,   ///< Recording first pass (establishes loop)
    Playing,     ///< Playing back loop
    Overdubbing  ///< Recording over existing loop
};

//==============================================================================
/**
 * @brief Base class for all channel types
 * 
 * This class manages:
 * - Loop buffer allocation and access
 * - State transitions
 * - Plugin chain
 * - Gain/monitoring
 * - Common processing utilities
 * 
 * Derived classes implement specific signal flow:
 * - AudioChannel: hardware input → FX → record/play
 * - VSTiChannel: MIDI → VSTi → FX → record/play
 */
class Channel
{
public:
    //==============================================================================
    Channel(int channelIndex, ChannelType type);
    virtual ~Channel() = default;
    
    //==============================================================================
    // Audio Thread Interface (Real-Time Safe)
    //==============================================================================
    
    /**
     * @brief Process one audio block
     * 
     * @param inputChannelData Input from hardware (nullptr for VSTi channels)
     * @param outputChannelData Output to hardware (for mixing)
     * @param midiBuffer MIDI events for this block
     * @param numSamples Number of samples in buffers
     * @param playheadPosition Global playhead position
     * @param loopLength Global loop length (0 = not established)
     * @param numInputChannels Available input channels
     * @param numOutputChannels Available output channels
     */
    virtual void processBlock(const float* const* inputChannelData,
                             float* const* outputChannelData,
                             const juce::MidiBuffer& midiBuffer,
                             int numSamples,
                             juce::int64 playheadPosition,
                             juce::int64 loopLength,
                             int numInputChannels,
                             int numOutputChannels) = 0;
    
    /**
     * @brief Prepare for playback
     * @param sampleRate Sample rate in Hz
     * @param maxBlockSize Maximum expected block size
     * @param maxLoopLengthSamples Maximum loop length for pre-allocation
     */
    virtual void prepareToPlay(double sampleRate,
                              int maxBlockSize,
                              juce::int64 maxLoopLengthSamples);
    
    /**
     * @brief Release resources
     */
    virtual void releaseResources();
    
    //==============================================================================
    // State Management (Called via Commands)
    //==============================================================================
    
    /**
     * @brief Start recording (first pass or overdub)
     */
    void startRecording(bool isOverdub = false);
    
    /**
     * @brief Stop recording
     */
    void stopRecording();
    
    /**
     * @brief Start playback
     */
    void startPlayback();
    
    /**
     * @brief Stop playback
     */
    void stopPlayback();
    
    /**
     * @brief Clear loop and return to idle
     */
    void clearLoop();
    
    //==============================================================================
    // Parameter Control
    //==============================================================================
    
    /**
     * @brief Set channel gain in dB
     */
    void setGainDb(float gainDb);
    
    /**
     * @brief Get current gain in dB
     */
    float getGainDb() const;
    
    /**
     * @brief Set monitor mode
     */
    void setMonitorMode(MonitorMode mode);
    
    /**
     * @brief Get monitor mode
     */
    MonitorMode getMonitorMode() const;
    
    /**
     * @brief Set mute state
     */
    void setMuted(bool shouldMute);
    
    /**
     * @brief Check if muted
     */
    bool isMuted() const { return muted.load(std::memory_order_relaxed); }
    
    /**
     * @brief Set solo state
     */
    void setSolo(bool shouldSolo);
    
    /**
     * @brief Check if soloed
     */
    bool isSolo() const { return solo.load(std::memory_order_relaxed); }
    
    //==============================================================================
    // Routing
    //==============================================================================
    
    /**
     * @brief Set routing configuration
     */
    void setRouting(const RoutingConfig& config);
    
    /**
     * @brief Get current routing
     */
    RoutingConfig getRouting() const { return routing; }
    
    //==============================================================================
    // Plugin Management
    //==============================================================================
    
    /**
     * @brief Add plugin to FX chain (called from message thread)
     */
    void addPlugin(int slotIndex, std::unique_ptr<juce::AudioPluginInstance> plugin);
    
    /**
     * @brief Remove plugin from FX chain
     */
    void removePlugin(int slotIndex);
    
    /**
     * @brief Set plugin bypass state
     */
    void setPluginBypassed(int slotIndex, bool bypassed);
    
    /**
     * @brief Check if plugin slot is bypassed
     */
    bool isPluginBypassed(int slotIndex) const;
    
    //==============================================================================
    // State Queries
    //==============================================================================
    
    ChannelType getType() const { return channelType; }
    ChannelState getState() const { return state.load(std::memory_order_relaxed); }
    int getChannelIndex() const { return channelIndex; }
    
    bool hasLoop() const { return loopHasContent.load(std::memory_order_relaxed); }
    bool isIdle() const { return getState() == ChannelState::Idle; }
    bool isRecording() const { return getState() == ChannelState::Recording; }
    bool isPlaying() const { return getState() == ChannelState::Playing; }
    bool isOverdubbing() const { return getState() == ChannelState::Overdubbing; }
    
protected:
    //==============================================================================
    // Protected Members (accessible by derived classes)
    
    int channelIndex;
    ChannelType channelType;
    
    // State (atomic for thread-safe access)
    std::atomic<ChannelState> state{ChannelState::Idle};
    std::atomic<bool> loopHasContent{false};
    std::atomic<bool> muted{false};
    std::atomic<bool> solo{false};
    
    // Parameters (atomic)
    std::atomic<float> gainLinear{1.0f};
    std::atomic<MonitorMode> monitorMode{MonitorMode::AlwaysOn};
    
    // Routing (read in audio thread, written from message thread via command)
    RoutingConfig routing;
    
    // Loop buffer (stereo, pre-allocated)
    juce::AudioBuffer<float> loopBuffer;
    juce::int64 loopBufferSize{0};  // Size in samples
    
    // Working buffers (allocated in prepareToPlay)
    juce::AudioBuffer<float> workingBuffer;  // For channel processing
    juce::AudioBuffer<float> fxBuffer;       // For FX chain processing
    
    // Plugin chain
    struct PluginSlot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::atomic<bool> bypassed{false};
        std::atomic<bool> crashed{false};
    };
    std::array<PluginSlot, 3> fxChain;
    
    // Audio parameters
    double sampleRate{44100.0};
    int maxBlockSize{512};
    
    //==============================================================================
    // Protected Helpers
    
    /**
     * @brief Process FX chain on a buffer
     * @param buffer Buffer to process in-place
     * @param numSamples Number of samples
     * @param midiBuffer MIDI events (for instruments) - mutable for plugin processing
     */
    void processFXChain(juce::AudioBuffer<float>& buffer,
                       int numSamples,
                       juce::MidiBuffer& midiBuffer);
    
    /**
     * @brief Record audio into loop buffer
     * @param source Source buffer (stereo)
     * @param startPosition Position in loop buffer
     * @param numSamples Number of samples to record
     * @param isOverdub If true, add to existing; if false, replace
     */
    void recordToLoop(const juce::AudioBuffer<float>& source,
                     juce::int64 startPosition,
                     int numSamples,
                     bool isOverdub);
    
    /**
     * @brief Play audio from loop buffer
     * @param dest Destination buffer (stereo)
     * @param startPosition Position in loop buffer
     * @param numSamples Number of samples to play
     */
    void playFromLoop(juce::AudioBuffer<float>& dest,
                     juce::int64 startPosition,
                     int numSamples);
    
    /**
     * @brief Check if monitoring should be active
     */
    bool shouldMonitor() const;
    
    /**
     * @brief Convert dB to linear gain
     */
    static float dbToLinear(float db);
    
    /**
     * @brief Convert linear gain to dB
     */
    static float linearToDb(float linear);
    
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Channel)
};
