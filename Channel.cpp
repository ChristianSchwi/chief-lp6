#include "Channel.h"

//==============================================================================
Channel::Channel(int index, ChannelType type)
    : channelIndex(index)
    , channelType(type)
{
}

//==============================================================================
// Preparation and Resource Management
//==============================================================================

void Channel::prepareToPlay(double newSampleRate,
                           int newMaxBlockSize,
                           juce::int64 maxLoopLengthSamples)
{
    sampleRate = newSampleRate;
    maxBlockSize = newMaxBlockSize;
    loopBufferSize = maxLoopLengthSamples;
    
    // Allocate loop buffer (stereo, pre-allocated to maximum size)
    loopBuffer.setSize(2, static_cast<int>(maxLoopLengthSamples), false, true, true);
    loopBuffer.clear();
    
    // Allocate working buffers
    workingBuffer.setSize(2, maxBlockSize * 2, false, true, true);  // Extra space for safety
    fxBuffer.setSize(2, maxBlockSize * 2, false, true, true);
    
    workingBuffer.clear();
    fxBuffer.clear();
    
    // Prepare plugins
    for (auto& slot : fxChain)
    {
        if (slot.plugin && !slot.crashed.load(std::memory_order_relaxed))
        {
            try
            {
                slot.plugin->prepareToPlay(sampleRate, maxBlockSize);
            }
            catch (...)
            {
                DBG("Plugin crashed during prepareToPlay() in channel " + juce::String(channelIndex));
                slot.crashed.store(true, std::memory_order_release);
            }
        }
    }
    
    DBG("Channel " + juce::String(channelIndex) + " prepared: " +
        juce::String(sampleRate) + " Hz, " +
        juce::String(maxBlockSize) + " samples, " +
        juce::String(maxLoopLengthSamples) + " loop samples");
}

void Channel::releaseResources()
{
    // Release plugin resources
    for (auto& slot : fxChain)
    {
        if (slot.plugin && !slot.crashed.load(std::memory_order_relaxed))
        {
            try
            {
                slot.plugin->releaseResources();
            }
            catch (...)
            {
                DBG("Plugin crashed during releaseResources() in channel " + juce::String(channelIndex));
                slot.crashed.store(true, std::memory_order_release);
            }
        }
    }
    
    // Clear buffers (free memory)
    loopBuffer.setSize(0, 0);
    workingBuffer.setSize(0, 0);
    fxBuffer.setSize(0, 0);
}

//==============================================================================
// State Management
//==============================================================================

void Channel::startRecording(bool isOverdub)
{
    ChannelState currentState = state.load(std::memory_order_relaxed);
    
    if (isOverdub)
    {
        // Can only overdub if we have existing content
        if (loopHasContent.load(std::memory_order_relaxed))
        {
            state.store(ChannelState::Overdubbing, std::memory_order_release);
            DBG("Channel " + juce::String(channelIndex) + " started overdubbing");
        }
    }
    else
    {
        // Fresh recording
        state.store(ChannelState::Recording, std::memory_order_release);
        DBG("Channel " + juce::String(channelIndex) + " started recording");
    }
}

void Channel::stopRecording()
{
    ChannelState currentState = state.load(std::memory_order_relaxed);
    
    if (currentState == ChannelState::Recording)
    {
        // First recording completed - we now have content
        loopHasContent.store(true, std::memory_order_release);
        state.store(ChannelState::Playing, std::memory_order_release);
        DBG("Channel " + juce::String(channelIndex) + " recording complete, now playing");
    }
    else if (currentState == ChannelState::Overdubbing)
    {
        // Overdub complete - return to playing
        state.store(ChannelState::Playing, std::memory_order_release);
        DBG("Channel " + juce::String(channelIndex) + " overdub complete, now playing");
    }
}

void Channel::startPlayback()
{
    if (loopHasContent.load(std::memory_order_relaxed))
    {
        state.store(ChannelState::Playing, std::memory_order_release);
        DBG("Channel " + juce::String(channelIndex) + " started playback");
    }
}

void Channel::stopPlayback()
{
    state.store(ChannelState::Idle, std::memory_order_release);
    DBG("Channel " + juce::String(channelIndex) + " stopped playback");
}

void Channel::clearLoop()
{
    state.store(ChannelState::Idle, std::memory_order_release);
    loopHasContent.store(false, std::memory_order_release);
    loopBuffer.clear();
    DBG("Channel " + juce::String(channelIndex) + " loop cleared");
}

//==============================================================================
// Parameter Control
//==============================================================================

void Channel::setGainDb(float gainDb)
{
    // Clamp to reasonable range
    gainDb = juce::jlimit(-60.0f, 12.0f, gainDb);
    gainLinear.store(dbToLinear(gainDb), std::memory_order_release);
}

float Channel::getGainDb() const
{
    return linearToDb(gainLinear.load(std::memory_order_relaxed));
}

void Channel::setMonitorMode(MonitorMode mode)
{
    monitorMode.store(mode, std::memory_order_release);
}

MonitorMode Channel::getMonitorMode() const
{
    return monitorMode.load(std::memory_order_relaxed);
}

void Channel::setMuted(bool shouldMute)
{
    muted.store(shouldMute, std::memory_order_release);
}

void Channel::setSolo(bool shouldSolo)
{
    solo.store(shouldSolo, std::memory_order_release);
}

//==============================================================================
// Routing
//==============================================================================

void Channel::setRouting(const RoutingConfig& config)
{
    // WARNING: This should only be called from audio thread or via Command system!
    // Calling from message thread while audio is running causes race conditions.
    // The routing struct is read by audio thread without synchronization.
    
    routing = config;
    DBG("Channel " + juce::String(channelIndex) + " routing updated");
}

//==============================================================================
// Plugin Management
//==============================================================================

void Channel::addPlugin(int slotIndex, std::unique_ptr<juce::AudioPluginInstance> plugin)
{
    jassert(slotIndex >= 0 && slotIndex < 3);
    
    auto& slot = fxChain[slotIndex];
    
    // THREAD-SAFE PLUGIN SWAP:
    // 1. Set bypass flag (audio thread will skip this slot)
    slot.bypassed.store(true, std::memory_order_release);
    
    // 2. Small delay to ensure audio thread has seen bypass flag
    //    This is a simplified approach; proper solution would use lock-free queue
    juce::Thread::sleep(10);  // 10ms should cover any audio callback
    
    // 3. Release old plugin if present (now safe - audio thread bypassing)
    if (slot.plugin)
    {
        slot.plugin->releaseResources();
        slot.plugin.reset();
    }
    
    // 4. Install new plugin
    slot.plugin = std::move(plugin);
    slot.crashed.store(false, std::memory_order_release);
    
    // 5. Prepare new plugin
    if (slot.plugin)
    {
        try
        {
            slot.plugin->prepareToPlay(sampleRate, maxBlockSize);
            
            // 6. Re-enable (audio thread can now use new plugin)
            slot.bypassed.store(false, std::memory_order_release);
        }
        catch (...)
        {
            DBG("Plugin crashed during prepareToPlay() on load");
            slot.crashed.store(true, std::memory_order_release);
            // Keep bypassed = true since plugin crashed
        }
    }
}

void Channel::removePlugin(int slotIndex)
{
    jassert(slotIndex >= 0 && slotIndex < 3);
    
    auto& slot = fxChain[slotIndex];
    
    if (!slot.plugin)
        return;  // Nothing to remove
    
    // THREAD-SAFE PLUGIN REMOVAL:
    // 1. Set bypass flag
    slot.bypassed.store(true, std::memory_order_release);
    
    // 2. Small delay to ensure audio thread has seen bypass
    juce::Thread::sleep(10);
    
    // 3. Release plugin (now safe)
    slot.plugin->releaseResources();
    slot.plugin.reset();
    
    // 4. Clear flags
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

//==============================================================================
// Protected Processing Helpers
//==============================================================================

void Channel::processFXChain(juce::AudioBuffer<float>& buffer,
                            int numSamples,
                            juce::MidiBuffer& midiBuffer)
{
    // Process through each plugin slot
    for (auto& slot : fxChain)
    {
        // Check if bypassed first (atomic read)
        if (slot.bypassed.load(std::memory_order_acquire))
            continue;
        
        // Check if crashed
        if (slot.crashed.load(std::memory_order_acquire))
            continue;
        
        // Double-check plugin pointer (could be swapped between bypass check and here)
        if (slot.plugin == nullptr)
            continue;
        
        // Process plugin with crash protection
        try
        {
            slot.plugin->processBlock(buffer, midiBuffer);
        }
        catch (...)
        {
            // Plugin crashed - mark as crashed and bypass
            slot.crashed.store(true, std::memory_order_release);
            DBG("Plugin crashed in channel " + juce::String(channelIndex) + " FX chain!");
            
            // Could trigger async notification to GUI here
        }
    }
}

void Channel::recordToLoop(const juce::AudioBuffer<float>& source,
                          juce::int64 startPosition,
                          int numSamples,
                          bool isOverdub)
{
    if (numSamples <= 0 || startPosition < 0)
        return;
    
    if (loopBufferSize <= 0)
        return;
    
    if (source.getNumSamples() < numSamples)
        return;  // Source buffer too small
    
    // Handle wrapping at loop boundary
    juce::int64 writePos = startPosition % loopBufferSize;
    int samplesRemaining = numSamples;
    int sourcePos = 0;
    
    while (samplesRemaining > 0)
    {
        // Calculate samples until loop end
        const juce::int64 samplesToEnd = loopBufferSize - writePos;
        const int samplesThisIteration = static_cast<int>(juce::jmin(
            static_cast<juce::int64>(samplesRemaining),
            samplesToEnd
        ));
        
        // Ensure we don't write beyond loop buffer
        if (writePos + samplesThisIteration > loopBufferSize)
            break;
        
        if (isOverdub)
        {
            // Overdub mode: ADD to existing content
            for (int ch = 0; ch < juce::jmin(2, source.getNumChannels()); ++ch)
            {
                loopBuffer.addFrom(ch, static_cast<int>(writePos),
                                 source, ch, sourcePos,
                                 samplesThisIteration);
            }
        }
        else
        {
            // Recording mode: REPLACE existing content
            for (int ch = 0; ch < juce::jmin(2, source.getNumChannels()); ++ch)
            {
                loopBuffer.copyFrom(ch, static_cast<int>(writePos),
                                  source, ch, sourcePos,
                                  samplesThisIteration);
            }
        }
        
        sourcePos += samplesThisIteration;
        samplesRemaining -= samplesThisIteration;
        writePos = (writePos + samplesThisIteration) % loopBufferSize;
    }
}

void Channel::playFromLoop(juce::AudioBuffer<float>& dest,
                          juce::int64 startPosition,
                          int numSamples)
{
    if (numSamples <= 0 || startPosition < 0)
        return;
    
    if (!loopHasContent.load(std::memory_order_relaxed))
        return;
    
    if (loopBufferSize <= 0)
        return;
    
    // Handle wrapping at loop boundary
    juce::int64 readPos = startPosition % loopBufferSize;
    int samplesRemaining = numSamples;
    int destPos = 0;
    
    while (samplesRemaining > 0)
    {
        // Calculate samples until loop end
        const juce::int64 samplesToEnd = loopBufferSize - readPos;
        const int samplesThisIteration = static_cast<int>(juce::jmin(
            static_cast<juce::int64>(samplesRemaining),
            samplesToEnd
        ));
        
        // Ensure we don't read beyond destination buffer
        if (destPos + samplesThisIteration > dest.getNumSamples())
            break;
        
        // Copy from loop buffer to destination
        for (int ch = 0; ch < juce::jmin(2, dest.getNumChannels()); ++ch)
        {
            dest.copyFrom(ch, destPos,
                         loopBuffer, ch, static_cast<int>(readPos),
                         samplesThisIteration);
        }
        
        destPos += samplesThisIteration;
        samplesRemaining -= samplesThisIteration;
        readPos = (readPos + samplesThisIteration) % loopBufferSize;
    }
}

bool Channel::shouldMonitor() const
{
    const MonitorMode mode = monitorMode.load(std::memory_order_relaxed);
    const ChannelState currentState = state.load(std::memory_order_relaxed);
    
    switch (mode)
    {
        case MonitorMode::Off:
            return false;
        
        case MonitorMode::AlwaysOn:
            return true;
        
        case MonitorMode::WhileRecording:
            return (currentState == ChannelState::Recording || 
                    currentState == ChannelState::Overdubbing);
        
        case MonitorMode::WhenTrackActive:
            // TODO: Check if this is the active track
            return (currentState != ChannelState::Idle);
        
        default:
            return false;
    }
}

//==============================================================================
// Utility Methods
//==============================================================================

float Channel::dbToLinear(float db)
{
    return juce::Decibels::decibelsToGain(db);
}

float Channel::linearToDb(float linear)
{
    return juce::Decibels::gainToDecibels(linear);
}
