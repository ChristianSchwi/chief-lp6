#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "Command.h"

//==============================================================================
enum class ChannelType  { Audio, VSTi };
enum class ChannelState { Idle, Recording, Playing, Overdubbing };

//==============================================================================
class Channel
{
public:
    Channel(int channelIndex, ChannelType type);
    virtual ~Channel() = default;

    //==========================================================================
    // Audio Thread
    //==========================================================================

    virtual void processBlock(const float* const* inputChannelData,
                              float* const*       outputChannelData,
                              const juce::MidiBuffer& midiBuffer,
                              int numSamples,
                              juce::int64 playheadPosition,
                              juce::int64 loopLength,
                              int numInputChannels,
                              int numOutputChannels) = 0;

    virtual void prepareToPlay(double sampleRate,
                               int maxBlockSize,
                               juce::int64 maxLoopLengthSamples);

    virtual void releaseResources();

    //==========================================================================
    // State Management
    //==========================================================================

    void startRecording(bool isOverdub = false);
    void stopRecording();
    void startPlayback();
    void stopPlayback();
    void clearLoop();
    void requestStopAtLoopEnd();

    //==========================================================================
    // Parameters
    //==========================================================================

    void        setGainDb(float gainDb);
    float       getGainDb() const;
    void        setMonitorMode(MonitorMode mode);
    MonitorMode getMonitorMode() const;
    void        setMuted(bool shouldMute);
    bool        isMuted()  const { return muted.load(std::memory_order_relaxed); }
    void        setSolo(bool shouldSolo);
    bool        isSolo()   const { return solo .load(std::memory_order_relaxed); }

    //==========================================================================
    // Routing
    //==========================================================================

    void          setRouting(const RoutingConfig& config);
    RoutingConfig getRouting() const { return routing; }

    //==========================================================================
    // Plugin Management
    //==========================================================================

    void addPlugin(int slotIndex, std::unique_ptr<juce::AudioPluginInstance> plugin);
    void removePlugin(int slotIndex);
    void setPluginBypassed(int slotIndex, bool bypassed);
    bool isPluginBypassed(int slotIndex) const;

    /**
     * @brief Non-owning access to an FX plugin for state read/write (Message Thread only).
     * @param slotIndex  0-2
     * @return Plugin pointer or nullptr if slot empty / crashed
     */
    juce::AudioPluginInstance* getPlugin(int slotIndex) const;

    //==========================================================================
    // State Queries
    //==========================================================================

    ChannelType  getType()         const { return channelType; }
    ChannelState getState()        const { return state.load(std::memory_order_relaxed); }
    int          getChannelIndex() const { return channelIndex; }

    bool hasLoop()       const { return loopHasContent.load(std::memory_order_relaxed); }
    bool isIdle()        const { return getState() == ChannelState::Idle; }
    bool isRecording()   const { return getState() == ChannelState::Recording; }
    bool isPlaying()     const { return getState() == ChannelState::Playing; }
    bool isOverdubbing() const { return getState() == ChannelState::Overdubbing; }

    //==========================================================================
    // Loop Buffer I/O (Message Thread — call only when audio is stopped or saved)
    //==========================================================================

    /** Read-only access to loop buffer for SongManager::saveSong(). */
    const juce::AudioBuffer<float>& getLoopBuffer()   const { return loopBuffer; }

    /** Max capacity in samples (0 if prepareToPlay not yet called). */
    juce::int64                     getLoopBufferSize() const { return loopBufferSize; }

    /**
     * @brief Copy pre-recorded audio into the loop buffer.
     *
     * Only call from message thread when audio is stopped.
     * Clears existing content before copy.
     *
     * @param source     Stereo source buffer (≥2 channels)
     * @param numSamples Samples to copy (clamped to loopBufferSize)
     * @return true on success
     */
    bool loadLoopData(const juce::AudioBuffer<float>& source, juce::int64 numSamples);

protected:
    //==========================================================================
    int         channelIndex;
    ChannelType channelType;

    std::atomic<ChannelState> state          {ChannelState::Idle};
    std::atomic<bool>         loopHasContent {false};
    std::atomic<bool>         muted          {false};
    std::atomic<bool>         solo           {false};
    std::atomic<bool>         stopPending    {false};

    std::atomic<float>       gainLinear  {1.0f};
    std::atomic<MonitorMode> monitorMode {MonitorMode::WhenTrackActive};

    RoutingConfig routing;

    juce::AudioBuffer<float> loopBuffer;
    juce::int64              loopBufferSize {0};

    juce::AudioBuffer<float> workingBuffer;
    juce::AudioBuffer<float> fxBuffer;

    struct PluginSlot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::atomic<bool> bypassed {false};
        std::atomic<bool> crashed  {false};
    };
    std::array<PluginSlot, 3> fxChain;

    double sampleRate   {44100.0};
    int    maxBlockSize {512};

    //==========================================================================
    void checkAndExecutePendingStop(juce::int64 playheadPosition,
                                    juce::int64 loopLength,
                                    int numSamples);

    void processFXChain(juce::AudioBuffer<float>& buffer,
                        int numSamples,
                        juce::MidiBuffer& midiBuffer);

    void recordToLoop(const juce::AudioBuffer<float>& source,
                      juce::int64 startPosition,
                      int numSamples,
                      bool isOverdub);

    void playFromLoop(juce::AudioBuffer<float>& dest,
                      juce::int64 startPosition,
                      int numSamples);

    bool shouldMonitor() const;

    static float dbToLinear(float db);
    static float linearToDb(float linear);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Channel)
};
