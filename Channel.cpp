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

    // Section 0 is always allocated
    auto& sec0 = sections[0];
    const bool hasContent0 = sec0.loopHasContent.load(std::memory_order_relaxed);
    sec0.loopBuffer.setSize(2, static_cast<int>(maxLoopLengthSamples),
                       hasContent0, true, false);
    if (!hasContent0)
        sec0.loopBuffer.clear();
    sec0.overdubLayers.reserve(32);
    sec0.allocated = true;

    // Sections 1/2: only resize if already allocated (lazy)
    for (int s = 1; s < NUM_SECTIONS; ++s)
    {
        auto& sec = sections[s];
        if (sec.allocated)
        {
            const bool hasContent = sec.loopHasContent.load(std::memory_order_relaxed);
            sec.loopBuffer.setSize(2, static_cast<int>(maxLoopLengthSamples),
                                   hasContent, true, false);
            if (!hasContent)
                sec.loopBuffer.clear();
            sec.overdubLayers.reserve(32);
        }
    }

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

    for (auto& sec : sections)
    {
        sec.loopBuffer.setSize(0, 0);
        sec.overdubLayers.clear();
        sec.activeOverdubLayerIdx = -1;
        auto* staged = sec.stagedOverdubBuffer.exchange(nullptr, std::memory_order_acquire);
        delete staged;
        sec.loopHasContent.store(false, std::memory_order_relaxed);
        sec.allocated = false;
    }
    sections[0].allocated = true;  // section 0 is always "allocated"

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
    const int s = activeSection.load(std::memory_order_relaxed);
    auto& sec = sections[s];

    if (isOverdub && sec.loopHasContent.load(std::memory_order_relaxed))
    {
        auto* staged = sec.stagedOverdubBuffer.exchange(nullptr, std::memory_order_acquire);
        if (staged)
        {
            // Validate staged buffer size matches current loop buffer capacity.
            // If mismatched (e.g. BPM changed after staging), re-allocate.
            if (staged->getNumSamples() != static_cast<int>(loopBufferSize) && loopBufferSize > 0)
            {
                delete staged;
                staged = nullptr;
            }
        }
        if (staged)
        {
            sec.overdubLayers.push_back(std::move(*staged));
            delete staged;
        }
        else
        {
            sec.overdubLayers.emplace_back(2, static_cast<int>(loopBufferSize));
            sec.overdubLayers.back().clear();
        }
        sec.activeOverdubLayerIdx = static_cast<int>(sec.overdubLayers.size()) - 1;
        state.store(ChannelState::Overdubbing, std::memory_order_release);
    }
    else
    {
        // Reset overdub layers without deallocating vector capacity.
        // AudioBuffer destructors still run (freeing sample data), which is
        // a heap operation on the audio thread. This is a known RT constraint
        // violation mitigated by the low frequency of this path (once per
        // fresh recording).
        sec.overdubLayers.clear();
        sec.activeOverdubLayerIdx = -1;
        state.store(ChannelState::Recording, std::memory_order_release);
    }
}

void Channel::stopRecording()
{
    const auto cur = state.load(std::memory_order_relaxed);
    if (cur == ChannelState::Recording || cur == ChannelState::Overdubbing)
    {
        const int s = activeSection.load(std::memory_order_relaxed);
        auto& sec = sections[s];
        sec.activeOverdubLayerIdx = -1;
        sec.loopHasContent.store(true, std::memory_order_release);
        state.store(ChannelState::Playing, std::memory_order_release);
    }
}

void Channel::startPlayback()
{
    if (hasLoop())
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

void Channel::checkOneShotStop(juce::int64 playheadPosition,
                                juce::int64 loopLength, int numSamples)
{
    if (!oneShot.load(std::memory_order_relaxed)) return;
    if (state.load(std::memory_order_relaxed) != ChannelState::Playing) return;
    if (loopLength <= 0) return;

    // Detect wrap: playhead just crossed 0 (position < numSamples means it wrapped this block)
    if (playheadPosition < static_cast<juce::int64>(numSamples))
        stopPlayback();
}

void Channel::checkAndExecutePendingStop(juce::int64 playheadPosition,
                                          juce::int64 loopLength, int numSamples)
{
    const bool anyPending = stopPending   .load(std::memory_order_acquire)
                         || recordPending .load(std::memory_order_relaxed)
                         || overdubPending.load(std::memory_order_relaxed)
                         || playPending   .load(std::memory_order_relaxed);
    if (!anyPending) return;

    if (loopLength <= 0 || loopLength <= static_cast<juce::int64>(numSamples)
        || playheadPosition >= static_cast<juce::int64>(numSamples))
        return;

    if (stopPending.load(std::memory_order_relaxed))
    {
        stopPending.store(false, std::memory_order_release);
        const auto cur = state.load(std::memory_order_relaxed);
        if (cur == ChannelState::Recording || cur == ChannelState::Overdubbing)
            stopRecording();
        else if (cur == ChannelState::Playing)
            stopPlayback();
    }

    if (recordPending.load(std::memory_order_relaxed))
    {
        recordPending.store(false, std::memory_order_release);
        startRecording(false);
    }

    if (overdubPending.load(std::memory_order_relaxed))
    {
        overdubPending.store(false, std::memory_order_release);
        startRecording(true);
    }

    if (playPending.load(std::memory_order_relaxed))
    {
        playPending.store(false, std::memory_order_release);
        startPlayback();
    }
}

void Channel::doubleBuffer(int sectionIndex, juce::int64 currentLoopLength)
{
    if (sectionIndex < 0 || sectionIndex >= NUM_SECTIONS) return;
    auto& sec = sections[sectionIndex];
    if (!sec.loopHasContent.load(std::memory_order_relaxed)) return;
    if (currentLoopLength <= 0 || currentLoopLength * 2 > loopBufferSize) return;

    const int len = static_cast<int>(currentLoopLength);

    for (int ch = 0; ch < juce::jmin(2, sec.loopBuffer.getNumChannels()); ++ch)
        sec.loopBuffer.copyFrom(ch, len, sec.loopBuffer, ch, 0, len);

    for (auto& layer : sec.overdubLayers)
    {
        const int layerLen = juce::jmin(len, layer.getNumSamples() - len);
        if (layerLen <= 0) continue;
        for (int ch = 0; ch < juce::jmin(2, layer.getNumChannels()); ++ch)
            layer.copyFrom(ch, len, layer, ch, 0, layerLen);
    }
}

void Channel::doubleBuffer(juce::int64 currentLoopLength)
{
    doubleBuffer(activeSection.load(std::memory_order_relaxed), currentLoopLength);
}

void Channel::clearLoop()
{
    clearSection(activeSection.load(std::memory_order_relaxed));
}

void Channel::clearSection(int s)
{
    if (s < 0 || s >= NUM_SECTIONS) return;

    // If clearing the active section, also stop playback
    if (s == activeSection.load(std::memory_order_relaxed))
    {
        state.store(ChannelState::Idle, std::memory_order_release);
        stopPending   .store(false, std::memory_order_release);
        recordPending .store(false, std::memory_order_release);
        overdubPending.store(false, std::memory_order_release);
        playPending   .store(false, std::memory_order_release);
    }

    auto& sec = sections[s];
    sec.loopHasContent.store(false, std::memory_order_release);
    sec.loopBuffer.clear();
    // NOTE: overdubLayers.clear() frees AudioBuffer sample data on the audio thread.
    // This is a known RT violation mitigated by the low frequency of clear operations.
    // Vector capacity is preserved so subsequent push_back won't reallocate (up to reserve limit).
    sec.overdubLayers.clear();
    sec.activeOverdubLayerIdx = -1;
    auto* staged = sec.stagedOverdubBuffer.exchange(nullptr, std::memory_order_acquire);
    delete staged;
}

void Channel::clearAllSections()
{
    state.store(ChannelState::Idle, std::memory_order_release);
    stopPending   .store(false, std::memory_order_release);
    recordPending .store(false, std::memory_order_release);
    overdubPending.store(false, std::memory_order_release);
    playPending   .store(false, std::memory_order_release);

    for (int s = 0; s < NUM_SECTIONS; ++s)
    {
        auto& sec = sections[s];
        sec.loopHasContent.store(false, std::memory_order_release);
        sec.loopBuffer.clear();
        sec.overdubLayers.clear();  // see clearSection() note about RT safety
        sec.activeOverdubLayerIdx = -1;
        auto* staged = sec.stagedOverdubBuffer.exchange(nullptr, std::memory_order_acquire);
        delete staged;
    }
}

//==============================================================================
// Section Management
//==============================================================================

void Channel::setActiveSection(int s)
{
    if (s >= 0 && s < NUM_SECTIONS)
        activeSection.store(s, std::memory_order_release);
}

bool Channel::sectionHasContent(int s) const
{
    if (s < 0 || s >= NUM_SECTIONS) return false;
    return sections[s].loopHasContent.load(std::memory_order_relaxed);
}

bool Channel::hasContentInAnySection() const
{
    for (int s = 0; s < NUM_SECTIONS; ++s)
        if (sections[s].loopHasContent.load(std::memory_order_relaxed))
            return true;
    return false;
}

bool Channel::hasLoop() const
{
    const int s = activeSection.load(std::memory_order_relaxed);
    if (sections[s].loopHasContent.load(std::memory_order_relaxed))
        return true;
    // Fallback to section A
    if (s != 0 && sections[0].loopHasContent.load(std::memory_order_relaxed))
        return true;
    return false;
}

void Channel::allocateSection(int s)
{
    if (s < 0 || s >= NUM_SECTIONS) return;
    if (sections[s].allocated) return;

    sections[s].loopBuffer.setSize(2, static_cast<int>(loopBufferSize), false, true, false);
    sections[s].loopBuffer.clear();
    sections[s].overdubLayers.reserve(32);
    sections[s].allocated = true;
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
    return slot.plugin.get();
}

//==============================================================================
// Loop Buffer I/O (Message Thread)
//==============================================================================

bool Channel::loadLoopData(const juce::AudioBuffer<float>& source, juce::int64 numSamples)
{
    return loadLoopData(activeSection.load(std::memory_order_relaxed), source, numSamples);
}

bool Channel::loadLoopData(int section, const juce::AudioBuffer<float>& source, juce::int64 numSamples)
{
    if (section < 0 || section >= NUM_SECTIONS) return false;
    if (numSamples <= 0) return false;
    if (source.getNumChannels() < 2) return false;

    if (loopBufferSize == 0) return false;

    // Ensure section is allocated
    allocateSection(section);

    auto& sec = sections[section];
    const juce::int64 samplesToLoad = juce::jmin(numSamples, loopBufferSize);

    if (source.getNumSamples() < static_cast<int>(samplesToLoad)) return false;

    sec.loopBuffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        sec.loopBuffer.copyFrom(ch, 0, source, ch, 0, static_cast<int>(samplesToLoad));

    sec.loopHasContent.store(true, std::memory_order_release);
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

    const int s = activeSection.load(std::memory_order_relaxed);
    auto& sec = sections[s];

    if (isOverdub && sec.activeOverdubLayerIdx >= 0 &&
        sec.activeOverdubLayerIdx < static_cast<int>(sec.overdubLayers.size()))
    {
        auto& layer = sec.overdubLayers[sec.activeOverdubLayerIdx];
        writeBufferWrapped(layer, layer.getNumSamples(), source, startPosition, numSamples, true);
    }
    else
    {
        if (loopBufferSize <= 0) return;
        writeBufferWrapped(sec.loopBuffer, loopBufferSize, source, startPosition, numSamples, false);
    }
}

void Channel::playFromLoop(juce::AudioBuffer<float>& dest,
                           juce::int64 startPosition, int numSamples)
{
    if (numSamples <= 0 || startPosition < 0 || loopBufferSize <= 0) return;

    int s = activeSection.load(std::memory_order_relaxed);

    // Fallback: if active section has no content, try previous sections
    while (s > 0 && !sections[s].loopHasContent.load(std::memory_order_relaxed))
        --s;
    if (!sections[s].loopHasContent.load(std::memory_order_relaxed))
        return;

    auto& sec = sections[s];

    // Base layer (copyFrom)
    readBufferWrapped(sec.loopBuffer, loopBufferSize, dest, startPosition, numSamples, false);

    // Overdub layers (addFrom)
    for (auto& layer : sec.overdubLayers)
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
    stageOverdubBuffer(loopLength, activeSection.load(std::memory_order_relaxed));
}

void Channel::stageOverdubBuffer(juce::int64 loopLength, int section)
{
    if (loopLength <= 0 || section < 0 || section >= NUM_SECTIONS) return;
    auto* buf = new juce::AudioBuffer<float>(2, static_cast<int>(loopLength));
    buf->clear();
    auto* old = sections[section].stagedOverdubBuffer.exchange(buf, std::memory_order_release);
    delete old;
}

void Channel::undoLastOverdub()
{
    const int s = activeSection.load(std::memory_order_relaxed);
    auto& sec = sections[s];

    if (sec.overdubLayers.empty()) return;

    const auto cur = state.load(std::memory_order_relaxed);
    if (cur == ChannelState::Overdubbing &&
        sec.activeOverdubLayerIdx == static_cast<int>(sec.overdubLayers.size()) - 1)
    {
        sec.activeOverdubLayerIdx = -1;
        sec.overdubLayers.pop_back();
        state.store(ChannelState::Playing, std::memory_order_release);
    }
    else if (cur != ChannelState::Overdubbing)
    {
        sec.overdubLayers.pop_back();
    }
}

int Channel::getOverdubLayerCount() const
{
    const int s = activeSection.load(std::memory_order_relaxed);
    return static_cast<int>(sections[s].overdubLayers.size());
}

bool Channel::canUndoOverdub() const
{
    const int s = activeSection.load(std::memory_order_relaxed);
    return !sections[s].overdubLayers.empty();
}

bool Channel::loadOverdubLayer(const juce::AudioBuffer<float>& source, juce::int64 numSamples)
{
    return loadOverdubLayer(activeSection.load(std::memory_order_relaxed), source, numSamples);
}

bool Channel::loadOverdubLayer(int section, const juce::AudioBuffer<float>& source, juce::int64 numSamples)
{
    if (section < 0 || section >= NUM_SECTIONS) return false;
    if (numSamples <= 0 || source.getNumChannels() < 2) return false;

    allocateSection(section);
    auto& sec = sections[section];
    sec.overdubLayers.emplace_back(2, static_cast<int>(numSamples));
    auto& layer = sec.overdubLayers.back();
    for (int ch = 0; ch < 2; ++ch)
        layer.copyFrom(ch, 0, source, ch, 0, static_cast<int>(numSamples));

    return true;
}

const std::vector<juce::AudioBuffer<float>>& Channel::getOverdubLayers() const
{
    const int s = activeSection.load(std::memory_order_relaxed);
    return sections[s].overdubLayers;
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
