#include "Channel.h"

//==============================================================================
Channel::Channel(int index, ChannelType type)
    : channelIndex(index), channelType(type) {}

//==============================================================================
void Channel::prepareToPlay(double newSampleRate, int newMaxBlockSize,
                            juce::int64 maxLoopLengthSamples)
{
    sampleRate     = newSampleRate;
    maxBlockSize   = newMaxBlockSize;
    loopBufferSize = maxLoopLengthSamples;

    // Preserve loop content across device changes (keepExistingContent=true).
    // Only clear if there is no recorded audio to preserve.
    const bool hasContent = loopHasContent.load(std::memory_order_relaxed);
    loopBuffer.setSize(2, static_cast<int>(maxLoopLengthSamples),
                       hasContent,   // keepExistingContent
                       true,         // clearExtraSpace
                       false);       // avoidReallocating
    if (!hasContent)
        loopBuffer.clear();

    workingBuffer.setSize(2, newMaxBlockSize * 2, false, true, true);
    fxBuffer     .setSize(2, newMaxBlockSize * 2, false, true, true);
    workingBuffer.clear();
    fxBuffer     .clear();

    for (auto& slot : fxChain)
    {
        if (slot.plugin && !slot.crashed.load(std::memory_order_relaxed))
        {
            try   { slot.plugin->prepareToPlay(sampleRate, maxBlockSize); }
            catch (...) { slot.crashed.store(true, std::memory_order_release); }
        }
    }
}

void Channel::releaseResources()
{
    for (auto& slot : fxChain)
        if (slot.plugin && !slot.crashed.load(std::memory_order_relaxed))
        {
            try   { slot.plugin->releaseResources(); }
            catch (...) { slot.crashed.store(true, std::memory_order_release); }
        }

    loopBuffer   .setSize(0, 0);
    workingBuffer.setSize(0, 0);
    fxBuffer     .setSize(0, 0);
}

//==============================================================================
// State Management
//==============================================================================

void Channel::clearPendingActions()
{
    stopPending   .store(false, std::memory_order_release);
    recordPending .store(false, std::memory_order_release);
    overdubPending.store(false, std::memory_order_release);
    playPending   .store(false, std::memory_order_release);
}

void Channel::startRecording(bool isOverdub)
{
    if (isOverdub && loopHasContent.load(std::memory_order_relaxed))
        state.store(ChannelState::Overdubbing, std::memory_order_release);
    else
        state.store(ChannelState::Recording,   std::memory_order_release);
}

void Channel::stopRecording()
{
    const auto cur = state.load(std::memory_order_relaxed);
    if (cur == ChannelState::Recording || cur == ChannelState::Overdubbing)
    {
        loopHasContent.store(true, std::memory_order_release);
        state.store(ChannelState::Playing, std::memory_order_release);
    }
}

void Channel::startPlayback()
{
    if (loopHasContent.load(std::memory_order_relaxed))
        state.store(ChannelState::Playing, std::memory_order_release);
}

void Channel::stopPlayback()
{
    state.store(ChannelState::Idle, std::memory_order_release);
}

void Channel::requestStopAtLoopEnd()
{
    stopPending.store(true, std::memory_order_release);
}

void Channel::requestRecordAtLoopEnd()
{
    recordPending.store(true, std::memory_order_release);
}

void Channel::requestOverdubAtLoopEnd()
{
    overdubPending.store(true, std::memory_order_release);
}

void Channel::requestPlayAtLoopEnd()
{
    playPending.store(true, std::memory_order_release);
}

void Channel::checkAndExecutePendingStop(juce::int64 playheadPosition,
                                          juce::int64 loopLength, int numSamples)
{
    // Early exit if nothing is pending
    const bool anyPending = stopPending   .load(std::memory_order_acquire)
                         || recordPending .load(std::memory_order_relaxed)
                         || overdubPending.load(std::memory_order_relaxed)
                         || playPending   .load(std::memory_order_relaxed);
    if (!anyPending) return;

    // Fire only at loop boundary: playhead wrapped in this block
    if (loopLength <= 0 || loopLength <= static_cast<juce::int64>(numSamples)
        || playheadPosition >= static_cast<juce::int64>(numSamples))
        return;

    // Pending stop (highest priority — processed before any pending start)
    if (stopPending.load(std::memory_order_relaxed))
    {
        stopPending.store(false, std::memory_order_release);
        const auto cur = state.load(std::memory_order_relaxed);
        if (cur == ChannelState::Recording || cur == ChannelState::Overdubbing)
            stopRecording();
        else if (cur == ChannelState::Playing)
            stopPlayback();
    }

    // Pending record start
    if (recordPending.load(std::memory_order_relaxed))
    {
        recordPending.store(false, std::memory_order_release);
        startRecording(false);
    }

    // Pending overdub start
    if (overdubPending.load(std::memory_order_relaxed))
    {
        overdubPending.store(false, std::memory_order_release);
        startRecording(true);
    }

    // Pending play start
    if (playPending.load(std::memory_order_relaxed))
    {
        playPending.store(false, std::memory_order_release);
        startPlayback();
    }
}

void Channel::clearLoop()
{
    state.store(ChannelState::Idle, std::memory_order_release);
    loopHasContent.store(false,     std::memory_order_release);
    stopPending   .store(false,     std::memory_order_release);
    recordPending .store(false,     std::memory_order_release);
    overdubPending.store(false,     std::memory_order_release);
    playPending   .store(false,     std::memory_order_release);
    loopBuffer.clear();
}

//==============================================================================
// Parameters
//==============================================================================

void Channel::setGainDb(float gainDb)
{
    gainLinear.store(dbToLinear(juce::jlimit(-60.0f, 12.0f, gainDb)),
                     std::memory_order_release);
}

float       Channel::getGainDb()     const { return linearToDb(gainLinear.load(std::memory_order_relaxed)); }
void        Channel::setMonitorMode(MonitorMode m) { monitorMode.store(m, std::memory_order_release); }
MonitorMode Channel::getMonitorMode() const        { return monitorMode.load(std::memory_order_relaxed); }
void        Channel::setMuted(bool v)  { muted.store(v, std::memory_order_release); }
void        Channel::setSolo (bool v)  { solo .store(v, std::memory_order_release); }
void        Channel::setRouting(const RoutingConfig& c) { routing = c; }

//==============================================================================
// Plugin Management
//==============================================================================

void Channel::addPlugin(int slotIndex, std::unique_ptr<juce::AudioPluginInstance> plugin)
{
    jassert(slotIndex >= 0 && slotIndex < 3);
    auto& slot = fxChain[slotIndex];

    slot.bypassed.store(true, std::memory_order_release);
    juce::Thread::sleep(10);

    if (slot.plugin) { slot.plugin->releaseResources(); slot.plugin.reset(); }

    slot.plugin = std::move(plugin);
    slot.crashed.store(false, std::memory_order_release);

    if (slot.plugin)
    {
        try
        {
            slot.plugin->prepareToPlay(sampleRate, maxBlockSize);
            slot.bypassed.store(false, std::memory_order_release);
        }
        catch (...) { slot.crashed.store(true, std::memory_order_release); }
    }
}

void Channel::removePlugin(int slotIndex)
{
    jassert(slotIndex >= 0 && slotIndex < 3);
    auto& slot = fxChain[slotIndex];
    if (!slot.plugin) return;
    slot.bypassed.store(true, std::memory_order_release);
    juce::Thread::sleep(10);
    slot.plugin->releaseResources();
    slot.plugin.reset();
    slot.crashed.store(false, std::memory_order_release);
}

void Channel::setPluginBypassed(int slotIndex, bool bypassed)
{
    jassert(slotIndex >= 0 && slotIndex < 3);
    fxChain[slotIndex].bypassed.store(bypassed, std::memory_order_release);
}

bool Channel::isPluginBypassed(int slotIndex) const
{
    jassert(slotIndex >= 0 && slotIndex < 3);
    return fxChain[slotIndex].bypassed.load(std::memory_order_relaxed);
}

juce::AudioPluginInstance* Channel::getPlugin(int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= 3) return nullptr;
    const auto& slot = fxChain[slotIndex];
    if (slot.crashed.load(std::memory_order_relaxed)) return nullptr;
    return slot.plugin.get();  // non-owning — caller must not delete
}

//==============================================================================
// Loop Buffer I/O (Message Thread)
//==============================================================================

bool Channel::loadLoopData(const juce::AudioBuffer<float>& source, juce::int64 numSamples)
{
    if (numSamples <= 0)
    {
        DBG("Channel " + juce::String(channelIndex) + ": loadLoopData — numSamples=0");
        return false;
    }
    if (source.getNumChannels() < 2)
    {
        DBG("Channel " + juce::String(channelIndex) + ": loadLoopData — source not stereo");
        return false;
    }

    // BUG 2 FIX: Guard against call before prepareToPlay()
    if (loopBufferSize == 0 || loopBuffer.getNumSamples() == 0)
    {
        DBG("Channel " + juce::String(channelIndex) +
            ": loadLoopData — called before prepareToPlay(), skipping");
        return false;
    }

    const juce::int64 samplesToLoad = juce::jmin(numSamples, loopBufferSize);

    if (source.getNumSamples() < static_cast<int>(samplesToLoad))
    {
        DBG("Channel " + juce::String(channelIndex) + ": loadLoopData — source buffer too small");
        return false;
    }

    loopBuffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        loopBuffer.copyFrom(ch, 0, source, ch, 0, static_cast<int>(samplesToLoad));

    loopHasContent.store(true, std::memory_order_release);

    DBG("Channel " + juce::String(channelIndex) + ": loaded " +
        juce::String(samplesToLoad) + " samples into loop buffer");

    return true;
}

//==============================================================================
// Protected Helpers
//==============================================================================

void Channel::processFXChain(juce::AudioBuffer<float>& buffer,
                             int numSamples,
                             juce::MidiBuffer& midiBuffer)
{
    for (auto& slot : fxChain)
    {
        if (slot.bypassed.load(std::memory_order_acquire)) continue;
        if (slot.crashed .load(std::memory_order_acquire)) continue;
        if (!slot.plugin)                                   continue;
        try
        {
            // Pass a non-owning view of exactly numSamples so the plugin advances its
            // internal state (envelopes, LFOs, delay lines) by exactly one real block,
            // not by the over-allocated buffer size (which would be 2× too fast).
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(),
                                           buffer.getNumChannels(),
                                           numSamples);
            slot.plugin->processBlock(view, midiBuffer);
        }
        catch (...)
        {
            slot.crashed.store(true, std::memory_order_release);
            DBG("FX plugin crashed in channel " + juce::String(channelIndex));
        }
    }
}

void Channel::recordToLoop(const juce::AudioBuffer<float>& source,
                           juce::int64 startPosition, int numSamples, bool isOverdub)
{
    if (numSamples <= 0 || startPosition < 0 || loopBufferSize <= 0) return;
    if (source.getNumSamples() < numSamples) return;

    juce::int64 writePos = startPosition % loopBufferSize;
    int         srcPos   = 0;
    int         remaining = numSamples;

    while (remaining > 0)
    {
        const int thisBlock = static_cast<int>(
            juce::jmin(static_cast<juce::int64>(remaining), loopBufferSize - writePos));
        if (thisBlock <= 0) break;

        for (int ch = 0; ch < juce::jmin(2, source.getNumChannels()); ++ch)
        {
            if (isOverdub)
                loopBuffer.addFrom (ch, static_cast<int>(writePos), source, ch, srcPos, thisBlock);
            else
                loopBuffer.copyFrom(ch, static_cast<int>(writePos), source, ch, srcPos, thisBlock);
        }

        srcPos    += thisBlock;
        remaining -= thisBlock;
        writePos   = (writePos + thisBlock) % loopBufferSize;
    }
}

void Channel::playFromLoop(juce::AudioBuffer<float>& dest,
                           juce::int64 startPosition, int numSamples)
{
    if (numSamples <= 0 || startPosition < 0 || loopBufferSize <= 0) return;
    if (!loopHasContent.load(std::memory_order_relaxed)) return;

    juce::int64 readPos   = startPosition % loopBufferSize;
    int         destPos   = 0;
    int         remaining = numSamples;

    while (remaining > 0)
    {
        const int thisBlock = static_cast<int>(
            juce::jmin(static_cast<juce::int64>(remaining), loopBufferSize - readPos));
        if (thisBlock <= 0 || destPos + thisBlock > dest.getNumSamples()) break;

        for (int ch = 0; ch < juce::jmin(2, dest.getNumChannels()); ++ch)
            dest.copyFrom(ch, destPos, loopBuffer, ch, static_cast<int>(readPos), thisBlock);

        destPos   += thisBlock;
        remaining -= thisBlock;
        readPos    = (readPos + thisBlock) % loopBufferSize;
    }
}

bool Channel::shouldMonitor() const
{
    const auto mode = monitorMode.load(std::memory_order_relaxed);
    const auto cur  = state      .load(std::memory_order_relaxed);
    switch (mode)
    {
        case MonitorMode::Off:            return false;
        case MonitorMode::AlwaysOn:       return true;
        case MonitorMode::WhileRecording: return cur == ChannelState::Recording
                                              || cur == ChannelState::Overdubbing;
        case MonitorMode::WhenTrackActive:return isActiveChannel.load(std::memory_order_relaxed);
        default:                          return false;
    }
}

float Channel::dbToLinear(float db)  { return juce::Decibels::decibelsToGain(db); }
float Channel::linearToDb(float lin) { return juce::Decibels::gainToDecibels(lin); }
