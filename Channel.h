#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "Command.h"

//==============================================================================
enum class ChannelType  { Audio, VSTi };
enum class ChannelState { Idle, Recording, Playing, Overdubbing };

static constexpr int NUM_SECTIONS = 3;

//==============================================================================
struct SectionBufferSet
{
    juce::AudioBuffer<float> loopBuffer;
    std::vector<juce::AudioBuffer<float>> overdubLayers;
    int activeOverdubLayerIdx {-1};
    std::atomic<juce::AudioBuffer<float>*> stagedOverdubBuffer {nullptr};
    std::atomic<bool> loopHasContent {false};
    bool allocated {false};

    ~SectionBufferSet()
    {
        auto* staged = stagedOverdubBuffer.exchange(nullptr, std::memory_order_acquire);
        delete staged;
    }
};

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
    void requestRecordAtLoopEnd();
    void requestOverdubAtLoopEnd();
    void requestPlayAtLoopEnd();

    bool hasPendingRecord()  const { return recordPending .load(std::memory_order_relaxed); }
    bool hasPendingOverdub() const { return overdubPending.load(std::memory_order_relaxed); }
    bool hasPendingPlay()    const { return playPending   .load(std::memory_order_relaxed); }
    bool hasPendingStop()    const { return stopPending   .load(std::memory_order_relaxed); }
    void clearPendingActions();

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
    bool        isSolo()      const { return solo     .load(std::memory_order_relaxed); }
    void        setSoloMuted(bool muted) { soloMuted.store(muted, std::memory_order_release); }
    bool        isSoloMuted() const { return soloMuted.load(std::memory_order_relaxed); }
    void        setIsActiveChannel(bool active) { isActiveChannel.store(active, std::memory_order_release); }
    bool        getIsActiveChannel() const      { return isActiveChannel.load(std::memory_order_relaxed); }
    void        setOneShot(bool enabled)        { oneShot.store(enabled, std::memory_order_release); }
    bool        isOneShot() const               { return oneShot.load(std::memory_order_relaxed); }

    // Oneshot independent playhead + multi-voice
    void startOneShotRecord();
    void stopOneShotRecord(juce::int64 recordedSamples);
    void triggerOneShotPlayback();
    void stopAllOneShotVoices();
    juce::int64 getOneShotLength() const   { return oneShotLength.load(std::memory_order_relaxed); }
    juce::int64 getOneShotPlayhead() const { return oneShotPlayhead.load(std::memory_order_relaxed); }
    bool isOneShotPlaying() const;

    static constexpr int kMaxOneShotVoices = 8;

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

    /** Check if a plugin has crashed (processing is bypassed but still loaded). */
    bool isPluginCrashed(int slotIndex) const
    {
        if (slotIndex < 0 || slotIndex >= 3) return false;
        return fxChain[slotIndex].crashed.load(std::memory_order_relaxed);
    }

    /** Check if any plugin in the FX chain has crashed. */
    bool hasAnyCrashedPlugin() const
    {
        for (const auto& slot : fxChain)
            if (slot.crashed.load(std::memory_order_relaxed))
                return true;
        return false;
    }

    //==========================================================================
    // State Queries
    //==========================================================================

    ChannelType  getType()         const { return channelType; }
    ChannelState getState()        const { return state.load(std::memory_order_relaxed); }
    int          getChannelIndex() const { return channelIndex; }

    float getInputPeakL()  const { return inputPeakL.load(std::memory_order_relaxed); }
    float getInputPeakR()  const { return inputPeakR.load(std::memory_order_relaxed); }
    float getLoopPeakL()   const { return loopPeakL.load(std::memory_order_relaxed); }
    float getLoopPeakR()   const { return loopPeakR.load(std::memory_order_relaxed); }

    bool hasLoop()       const;
    bool isIdle()        const { return getState() == ChannelState::Idle; }
    bool isRecording()   const { return getState() == ChannelState::Recording; }
    bool isPlaying()     const { return getState() == ChannelState::Playing; }
    bool isOverdubbing() const { return getState() == ChannelState::Overdubbing; }

    //==========================================================================
    // Section Management
    //==========================================================================

    void setActiveSection(int s);
    int  getActiveSection() const { return activeSection.load(std::memory_order_relaxed); }
    bool sectionHasContent(int s) const;
    bool hasContentInAnySection() const;
    void allocateSection(int s);
    void clearSection(int s);
    void clearAllSections();

    //==========================================================================
    // Loop Buffer I/O (Message Thread — call only when audio is stopped or saved)
    //==========================================================================

    /** Read-only access to loop buffer for active section. */
    const juce::AudioBuffer<float>& getLoopBuffer() const { return sections[activeSection.load(std::memory_order_relaxed)].loopBuffer; }

    /** Read-only access to a specific section's loop buffer. */
    const juce::AudioBuffer<float>& getSectionLoopBuffer(int s) const { return sections[s].loopBuffer; }

    /** Read-only access to a specific section's overdub layers. */
    const std::vector<juce::AudioBuffer<float>>& getSectionOverdubLayers(int s) const { return sections[s].overdubLayers; }

    /** Max capacity in samples (0 if prepareToPlay not yet called). */
    juce::int64                     getLoopBufferSize() const { return loopBufferSize; }

    /** Load loop data into the active section. */
    bool loadLoopData(const juce::AudioBuffer<float>& source, juce::int64 numSamples);

    /** Load loop data into a specific section. */
    bool loadLoopData(int section, const juce::AudioBuffer<float>& source, juce::int64 numSamples);

    //==========================================================================
    // Overdub Layers
    //==========================================================================

    /** Pre-allocate a buffer for the next overdub pass (call on message thread). */
    void stageOverdubBuffer(juce::int64 loopLength);

    /** Pre-allocate for a specific section. */
    void stageOverdubBuffer(juce::int64 loopLength, int section);

    /** Remove the last overdub layer (or cancel active overdub). Audio thread. */
    void undoLastOverdub();

    int  getOverdubLayerCount() const;
    bool canUndoOverdub()       const;

    /** Append a pre-recorded overdub layer to active section (message thread, audio stopped). */
    bool loadOverdubLayer(const juce::AudioBuffer<float>& source, juce::int64 numSamples);

    /** Append a pre-recorded overdub layer to a specific section. */
    bool loadOverdubLayer(int section, const juce::AudioBuffer<float>& source, juce::int64 numSamples);

    /** Read-only access to overdub layers for active section. */
    const std::vector<juce::AudioBuffer<float>>& getOverdubLayers() const;

    /** Duplicate loop content for a specific section. Audio thread only. */
    void doubleBuffer(int sectionIndex, juce::int64 currentLoopLength);

    /** Duplicate loop content for active section (backward compat). */
    void doubleBuffer(juce::int64 currentLoopLength);

protected:
    //==========================================================================
    int         channelIndex;
    ChannelType channelType;

    std::atomic<ChannelState> state          {ChannelState::Idle};
    std::atomic<bool>         muted          {false};
    std::atomic<bool>         solo           {false};
    std::atomic<bool>         soloMuted      {false};
    std::atomic<bool>         stopPending    {false};
    std::atomic<bool>         recordPending  {false};
    std::atomic<bool>         overdubPending {false};
    std::atomic<bool>         playPending    {false};
    std::atomic<bool>         isActiveChannel{false};
    std::atomic<bool>         oneShot        {false};

    // Oneshot: independent playhead and multi-voice overlap
    std::atomic<juce::int64> oneShotPlayhead {0};
    std::atomic<juce::int64> oneShotLength   {0};
    std::array<std::atomic<juce::int64>, 8> oneShotVoices {};  // -1 = inactive

    std::atomic<float>       gainLinear  {1.0f};
    std::atomic<MonitorMode> monitorMode {MonitorMode::WhenTrackActive};

    RoutingConfig routing;

    std::array<SectionBufferSet, NUM_SECTIONS> sections;
    std::atomic<int> activeSection {0};
    juce::int64      loopBufferSize {0};

    juce::AudioBuffer<float> workingBuffer;
    juce::AudioBuffer<float> fxBuffer;

    // Peak meters (updated by audio thread, read by GUI)
    std::atomic<float> inputPeakL  {0.0f};
    std::atomic<float> inputPeakR  {0.0f};
    std::atomic<float> loopPeakL   {0.0f};
    std::atomic<float> loopPeakR   {0.0f};

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

    void checkOneShotStop(juce::int64 playheadPosition,
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

    /** Read from a loop buffer with wrap-around. addToDest=false uses copyFrom, true uses addFrom. */
    void readBufferWrapped(const juce::AudioBuffer<float>& src, juce::int64 bufSize,
                           juce::AudioBuffer<float>& dest, juce::int64 startPos,
                           int numSamples, bool addToDest);

    /** Write to a loop buffer with wrap-around. addToBuffer=false uses copyFrom, true uses addFrom. */
    void writeBufferWrapped(juce::AudioBuffer<float>& dst, juce::int64 bufSize,
                            const juce::AudioBuffer<float>& src, juce::int64 startPos,
                            int numSamples, bool addToBuffer);

    bool shouldMonitor() const;

    static float dbToLinear(float db);
    static float linearToDb(float linear);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Channel)
};
