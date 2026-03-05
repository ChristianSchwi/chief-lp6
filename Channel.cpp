#include "Channel.h"

// SEH-protected plugin helpers — see VSTiChannel.cpp for rationale.
#if JUCE_WINDOWS
#include <excpt.h>

static bool pluginCallPrepareToPlay (juce::AudioPluginInstance* p,
                                     double sr, int bs) noexcept
{
    __try   { p->prepareToPlay (sr, bs); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool pluginCallProcessBlock (juce::AudioPluginInstance* p,
                                    juce::AudioBuffer<float>& buf,
                                    juce::MidiBuffer&         midi) noexcept
{
    __try   { p->processBlock (buf, midi); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

#else

static bool pluginCallPrepareToPlay (juce::AudioPluginInstance* p,
                                     double sr, int bs) noexcept
{
    try   { p->prepareToPlay (sr, bs); return true; }
    catch (...) { return false; }
}

static bool pluginCallProcessBlock (juce::AudioPluginInstance* p,
                                    juce::AudioBuffer<float>& buf,
                                    juce::MidiBuffer&         midi) noexcept
{
    try   { p->processBlock (buf, midi); return true; }
    catch (...) { return false; }
}

#endif

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

    overdubLayers.reserve(32);

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
    overdubLayers.clear();
    activeOverdubLayerIdx = -1;
    auto* staged = stagedOverdubBuffer.exchange(nullptr, std::memory_order_relaxed);
    delete staged;
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
    {
        // Create a new overdub layer from the pre-staged buffer (message thread allocated)
        auto* staged = stagedOverdubBuffer.exchange(nullptr, std::memory_order_acquire);
        if (staged)
        {
            overdubLayers.push_back(std::move(*staged));
            delete staged;
        }
        else
        {
            // Fallback: allocate on audio thread (small buffer = loopBufferSize)
            overdubLayers.emplace_back(2, static_cast<int>(loopBufferSize));
            overdubLayers.back().clear();
        }
        activeOverdubLayerIdx = static_cast<int>(overdubLayers.size()) - 1;
        state.store(ChannelState::Overdubbing, std::memory_order_release);
    }
    else
    {
        // Fresh recording: clear all overdub layers
        overdubLayers.clear();
        activeOverdubLayerIdx = -1;
        state.store(ChannelState::Recording, std::memory_order_release);
    }
}

void Channel::stopRecording()
{
    const auto cur = state.load(std::memory_order_relaxed);
    if (cur == ChannelState::Recording || cur == ChannelState::Overdubbing)
    {
        activeOverdubLayerIdx = -1;  // layer stays in vector for playback
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

void Channel::doubleBuffer(juce::int64 currentLoopLength)
{
    if (!loopHasContent.load(std::memory_order_relaxed)) return;
    if (currentLoopLength <= 0 || currentLoopLength * 2 > loopBufferSize) return;

    const int len = static_cast<int>(currentLoopLength);

    // Duplicate base loop buffer
    for (int ch = 0; ch < juce::jmin(2, loopBuffer.getNumChannels()); ++ch)
        loopBuffer.copyFrom(ch, len, loopBuffer, ch, 0, len);

    // Duplicate overdub layers
    for (auto& layer : overdubLayers)
    {
        const int layerLen = juce::jmin(len, layer.getNumSamples() - len);
        if (layerLen <= 0) continue;
        for (int ch = 0; ch < juce::jmin(2, layer.getNumChannels()); ++ch)
            layer.copyFrom(ch, len, layer, ch, 0, layerLen);
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
    overdubLayers.clear();
    activeOverdubLayerIdx = -1;
    auto* staged = stagedOverdubBuffer.exchange(nullptr, std::memory_order_relaxed);
    delete staged;
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
    const int blockMs = (sampleRate > 0.0 && maxBlockSize > 0)
                      ? static_cast<int>(maxBlockSize * 1000.0 / sampleRate) + 5
                      : 20;
    juce::Thread::sleep(blockMs);

    if (slot.plugin) { slot.plugin->releaseResources(); slot.plugin.reset(); }

    slot.plugin = std::move(plugin);
    slot.crashed.store(false, std::memory_order_release);

    if (slot.plugin)
    {
        if (pluginCallPrepareToPlay (slot.plugin.get(), sampleRate, maxBlockSize))
            slot.bypassed.store(false, std::memory_order_release);
        else
            slot.crashed.store(true, std::memory_order_release);
    }
}

void Channel::removePlugin(int slotIndex)
{
    jassert(slotIndex >= 0 && slotIndex < 3);
    auto& slot = fxChain[slotIndex];
    if (!slot.plugin) return;
    slot.bypassed.store(true, std::memory_order_release);
    const int blockMs = (sampleRate > 0.0 && maxBlockSize > 0)
                      ? static_cast<int>(maxBlockSize * 1000.0 / sampleRate) + 5
                      : 20;
    juce::Thread::sleep(blockMs);
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
        // Pass a non-owning view of exactly numSamples (SEH-protected on Windows).
        {
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(),
                                           buffer.getNumChannels(),
                                           numSamples);
            if (!pluginCallProcessBlock (slot.plugin.get(), view, midiBuffer))
            {
                slot.crashed.store (true, std::memory_order_release);
                DBG ("FX plugin crashed in channel " + juce::String (channelIndex));
            }
        }
    }
}

void Channel::recordToLoop(const juce::AudioBuffer<float>& source,
                           juce::int64 startPosition, int numSamples, bool isOverdub)
{
    if (numSamples <= 0 || startPosition < 0) return;
    if (source.getNumSamples() < numSamples) return;

    if (isOverdub && activeOverdubLayerIdx >= 0 &&
        activeOverdubLayerIdx < static_cast<int>(overdubLayers.size()))
    {
        // Write to active overdub layer (addFrom so multi-pass accumulates)
        auto& layer = overdubLayers[activeOverdubLayerIdx];
        writeBufferWrapped(layer, layer.getNumSamples(), source, startPosition, numSamples, true);
    }
    else
    {
        // Write to base loop buffer (copyFrom = overwrite)
        if (loopBufferSize <= 0) return;
        writeBufferWrapped(loopBuffer, loopBufferSize, source, startPosition, numSamples, false);
    }
}

void Channel::playFromLoop(juce::AudioBuffer<float>& dest,
                           juce::int64 startPosition, int numSamples)
{
    if (numSamples <= 0 || startPosition < 0 || loopBufferSize <= 0) return;
    if (!loopHasContent.load(std::memory_order_relaxed)) return;

    // Base layer (copyFrom)
    readBufferWrapped(loopBuffer, loopBufferSize, dest, startPosition, numSamples, false);

    // Overdub layers (addFrom)
    for (auto& layer : overdubLayers)
    {
        const juce::int64 layerSize = layer.getNumSamples();
        if (layerSize > 0)
            readBufferWrapped(layer, layerSize, dest, startPosition, numSamples, true);
    }
}

//==============================================================================
// Buffer I/O helpers with wrap-around
//==============================================================================

void Channel::readBufferWrapped(const juce::AudioBuffer<float>& src, juce::int64 bufSize,
                                juce::AudioBuffer<float>& dest, juce::int64 startPos,
                                int numSamples, bool addToDest)
{
    if (bufSize <= 0) return;

    juce::int64 readPos   = startPos % bufSize;
    int         destPos   = 0;
    int         remaining = numSamples;

    while (remaining > 0)
    {
        const int thisBlock = static_cast<int>(
            juce::jmin(static_cast<juce::int64>(remaining), bufSize - readPos));
        if (thisBlock <= 0 || destPos + thisBlock > dest.getNumSamples()) break;

        for (int ch = 0; ch < juce::jmin(2, dest.getNumChannels()); ++ch)
        {
            if (addToDest)
                dest.addFrom(ch, destPos, src, ch, static_cast<int>(readPos), thisBlock);
            else
                dest.copyFrom(ch, destPos, src, ch, static_cast<int>(readPos), thisBlock);
        }

        destPos   += thisBlock;
        remaining -= thisBlock;
        readPos    = (readPos + thisBlock) % bufSize;
    }
}

void Channel::writeBufferWrapped(juce::AudioBuffer<float>& dst, juce::int64 bufSize,
                                 const juce::AudioBuffer<float>& src, juce::int64 startPos,
                                 int numSamples, bool addToBuffer)
{
    if (bufSize <= 0) return;

    juce::int64 writePos = startPos % bufSize;
    int         srcPos   = 0;
    int         remaining = numSamples;

    while (remaining > 0)
    {
        const int thisBlock = static_cast<int>(
            juce::jmin(static_cast<juce::int64>(remaining), bufSize - writePos));
        if (thisBlock <= 0) break;

        for (int ch = 0; ch < juce::jmin(2, src.getNumChannels()); ++ch)
        {
            if (addToBuffer)
                dst.addFrom(ch, static_cast<int>(writePos), src, ch, srcPos, thisBlock);
            else
                dst.copyFrom(ch, static_cast<int>(writePos), src, ch, srcPos, thisBlock);
        }

        srcPos    += thisBlock;
        remaining -= thisBlock;
        writePos   = (writePos + thisBlock) % bufSize;
    }
}

//==============================================================================
// Overdub Layer Management
//==============================================================================

void Channel::stageOverdubBuffer(juce::int64 loopLength)
{
    if (loopLength <= 0) return;
    auto* buf = new juce::AudioBuffer<float>(2, static_cast<int>(loopLength));
    buf->clear();
    auto* old = stagedOverdubBuffer.exchange(buf, std::memory_order_release);
    delete old;
}

void Channel::undoLastOverdub()
{
    if (overdubLayers.empty()) return;

    const auto cur = state.load(std::memory_order_relaxed);
    if (cur == ChannelState::Overdubbing &&
        activeOverdubLayerIdx == static_cast<int>(overdubLayers.size()) - 1)
    {
        // Cancel active overdub -> revert to Playing
        activeOverdubLayerIdx = -1;
        overdubLayers.pop_back();
        state.store(ChannelState::Playing, std::memory_order_release);
    }
    else if (cur != ChannelState::Overdubbing)
    {
        // Remove last completed overdub layer
        overdubLayers.pop_back();
    }
}

bool Channel::loadOverdubLayer(const juce::AudioBuffer<float>& source, juce::int64 numSamples)
{
    if (numSamples <= 0 || source.getNumChannels() < 2) return false;

    overdubLayers.emplace_back(2, static_cast<int>(numSamples));
    auto& layer = overdubLayers.back();
    for (int ch = 0; ch < 2; ++ch)
        layer.copyFrom(ch, 0, source, ch, 0, static_cast<int>(numSamples));

    return true;
}

//==============================================================================
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
