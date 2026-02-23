#include "VSTiChannel.h"

//==============================================================================
VSTiChannel::VSTiChannel(int index)
    : Channel(index, ChannelType::VSTi)
{
    // VSTi channels have no audio input
    routing.inputChannelLeft = -1;
    routing.inputChannelRight = -1;
}

//==============================================================================
// VSTi Management
//==============================================================================

void VSTiChannel::setVSTi(std::unique_ptr<juce::AudioPluginInstance> instrument)
{
    // Block audio thread from accessing vsti during the swap
    vstiCrashed.store(true, std::memory_order_release);
    juce::Thread::sleep(10);

    // Release old VSTi if present
    if (vsti)
    {
        vsti->releaseResources();
    }

    // Install new VSTi
    vsti = std::move(instrument);
    vstiCrashed.store(false, std::memory_order_release);
    
    // Prepare new VSTi
    if (vsti)
    {
        try
        {
            vsti->prepareToPlay(sampleRate, maxBlockSize);
            DBG("VSTi loaded on channel " + juce::String(channelIndex) + 
                ": " + vsti->getName());
        }
        catch (...)
        {
            DBG("VSTi crashed during prepareToPlay() on channel " + 
                juce::String(channelIndex));
            vstiCrashed.store(true, std::memory_order_release);
        }
    }
}

void VSTiChannel::removeVSTi()
{
    if (!vsti) return;
    // Block audio thread from accessing vsti during removal
    vstiCrashed.store(true, std::memory_order_release);
    juce::Thread::sleep(10);
    vsti->releaseResources();
    vsti.reset();
    DBG("VSTi removed from channel " + juce::String(channelIndex));
}

void VSTiChannel::setMIDIChannelFilter(int channel)
{
    jassert(channel >= 0 && channel <= 16);
    routing.midiChannelFilter = channel;
}

//==============================================================================
// Preparation
//==============================================================================

void VSTiChannel::prepareToPlay(double newSampleRate,
                                int newMaxBlockSize,
                                juce::int64 maxLoopLengthSamples)
{
    // Call base class preparation
    Channel::prepareToPlay(newSampleRate, newMaxBlockSize, maxLoopLengthSamples);
    
    // Allocate VSTi-specific buffers
    vstiOutputBuffer.setSize(2, maxBlockSize * 2, false, true, true);
    vstiOutputBuffer.clear();
    
    // Prepare VSTi if present
    if (vsti && !vstiCrashed.load(std::memory_order_relaxed))
    {
        try
        {
            vsti->prepareToPlay(newSampleRate, newMaxBlockSize);
        }
        catch (...)
        {
            DBG("VSTi crashed during prepareToPlay() on channel " + 
                juce::String(channelIndex));
            vstiCrashed.store(true, std::memory_order_release);
        }
    }
}

void VSTiChannel::releaseResources()
{
    // Release VSTi
    if (vsti && !vstiCrashed.load(std::memory_order_relaxed))
    {
        try
        {
            vsti->releaseResources();
        }
        catch (...)
        {
            DBG("VSTi crashed during releaseResources() on channel " + 
                juce::String(channelIndex));
            vstiCrashed.store(true, std::memory_order_release);
        }
    }
    
    // Clear VSTi buffers
    vstiOutputBuffer.setSize(0, 0);
    
    // Call base class cleanup
    Channel::releaseResources();
}

//==============================================================================
// Main Processing
//==============================================================================

void VSTiChannel::processBlock(const float* const* inputChannelData,
                               float* const* outputChannelData,
                               const juce::MidiBuffer& midiBuffer,
                               int numSamples,
                               juce::int64 playheadPosition,
                               juce::int64 loopLength,
                               int numInputChannels,
                               int numOutputChannels)
{
    checkAndExecutePendingStop(playheadPosition, loopLength, numSamples);

    // Clear all working buffers
    workingBuffer.clear(0, numSamples);
    vstiOutputBuffer.clear(0, numSamples);
    fxBuffer.clear(0, numSamples);
    filteredMidiBuffer.clear();

    // Check if muted (user mute OR silenced by another channel's solo)
    const bool isMutedNow = muted.load(std::memory_order_relaxed)
                         || soloMuted.load(std::memory_order_relaxed);
    const ChannelState currentState = state.load(std::memory_order_relaxed);

    //==========================================================================
    // 1. FILTER MIDI BY CHANNEL (if configured)
    //==========================================================================
    const int midiFilter = routing.midiChannelFilter;
    filterMIDI(midiBuffer, filteredMidiBuffer, midiFilter);

    //==========================================================================
    // 2. PROCESS VSTi → vstiOutputBuffer (dry instrument signal)
    //==========================================================================
    processVSTi(vstiOutputBuffer, filteredMidiBuffer, numSamples);

    // Copy dry VSTi output to workingBuffer (allocation-free, audio-thread safe)
    const int chsToCopy = juce::jmin(workingBuffer.getNumChannels(),
                                     vstiOutputBuffer.getNumChannels());
    for (int ch = 0; ch < chsToCopy; ++ch)
        workingBuffer.copyFrom(ch, 0, vstiOutputBuffer, ch, 0, numSamples);

    //==========================================================================
    // 3. RECORD DRY SIGNAL (before FX — loop always stores clean audio)
    //==========================================================================
    if (currentState == ChannelState::Recording)
        recordToLoop(workingBuffer, playheadPosition, numSamples, false);
    else if (currentState == ChannelState::Overdubbing && loopLength > 0)
        recordToLoop(workingBuffer, playheadPosition, numSamples, true);

    //==========================================================================
    // 4. BUILD OUTPUT MIX IN fxBuffer
    //    Monitoring: add dry VSTi; Playback: add loop signal with gain.
    //    Both paths are combined before FX so the chain runs exactly once.
    //==========================================================================
    if (shouldMonitor())
    {
        for (int ch = 0; ch < fxBuffer.getNumChannels(); ++ch)
            fxBuffer.addFrom(ch, 0, workingBuffer, ch, 0, numSamples);
    }

    if ((currentState == ChannelState::Playing ||
         currentState == ChannelState::Overdubbing) &&
        loopLength > 0)
    {
        // Reuse workingBuffer as a temporary read target for the loop
        workingBuffer.clear(0, numSamples);
        playFromLoop(workingBuffer, playheadPosition, numSamples);
        applyGain(workingBuffer, numSamples);

        for (int ch = 0; ch < fxBuffer.getNumChannels(); ++ch)
            fxBuffer.addFrom(ch, 0, workingBuffer, ch, 0, numSamples);
    }

    //==========================================================================
    // 5. PROCESS FX CHAIN (applied once to combined output signal)
    //==========================================================================
    juce::MidiBuffer emptyMidi;
    processFXChain(fxBuffer, numSamples, emptyMidi);

    //==========================================================================
    // 6. ROUTE TO OUTPUT
    //==========================================================================
    if (!isMutedNow)
        routeOutput(outputChannelData, fxBuffer, numOutputChannels, numSamples);
}

//==============================================================================
// Private Processing Stages
//==============================================================================

void VSTiChannel::filterMIDI(const juce::MidiBuffer& source,
                            juce::MidiBuffer& dest,
                            int filterChannel)
{
    dest.clear();
    
    // filterChannel: 0 = all channels, 1-16 = specific MIDI channel
    if (filterChannel == 0)
    {
        // No filtering - copy all events
        dest = source;
        return;
    }
    
    // Filter by MIDI channel
    // Note: juce::MidiMessage::getChannel() returns 1-16 (not 0-15)
    for (const auto metadata : source)
    {
        const juce::MidiMessage& msg = metadata.getMessage();
        
        // Pass through non-channel messages (SysEx, etc.)
        if (msg.getChannel() == 0)
        {
            dest.addEvent(msg, metadata.samplePosition);
        }
        // Filter by specific channel
        else if (msg.getChannel() == filterChannel)
        {
            dest.addEvent(msg, metadata.samplePosition);
        }
    }
}

void VSTiChannel::processVSTi(juce::AudioBuffer<float>& outputBuffer,
                             juce::MidiBuffer& midiBuffer,
                             int numSamples)
{
    // Check buffer size
    if (outputBuffer.getNumSamples() < numSamples)
    {
        DBG("VSTi output buffer too small in channel " + juce::String(channelIndex));
        return;
    }
    
    // Ensure buffer is cleared
    outputBuffer.clear(0, numSamples);
    
    // Skip if no VSTi loaded
    if (!vsti)
        return;
    
    // Skip if VSTi has crashed
    if (vstiCrashed.load(std::memory_order_relaxed))
        return;
    
    // Process VSTi with crash protection
    try
    {
        // Pass a non-owning view of exactly numSamples so the plugin advances its
        // internal state (envelopes, LFOs, delay lines) by exactly one real block,
        // not by the over-allocated buffer size (which would be 2× too fast).
        juce::AudioBuffer<float> view (outputBuffer.getArrayOfWritePointers(),
                                       outputBuffer.getNumChannels(),
                                       numSamples);
        vsti->processBlock(view, midiBuffer);
    }
    catch (...)
    {
        // VSTi crashed - mark as crashed and clear output
        vstiCrashed.store(true, std::memory_order_release);
        outputBuffer.clear(0, numSamples);

        DBG("VSTi crashed in channel " + juce::String(channelIndex) + "!");
    }
}

void VSTiChannel::routeOutput(float* const* outputChannelData,
                              const juce::AudioBuffer<float>& source,
                              int numOutputChannels,
                              int numSamples)
{
    // Safety check
    if (outputChannelData == nullptr || numOutputChannels == 0 || numSamples <= 0)
        return;
    
    // Check source buffer size
    if (source.getNumSamples() < numSamples)
        return;
    
    const int outputLeft = routing.outputChannelLeft;
    const int outputRight = routing.outputChannelRight;
    
    // Validate output channels
    if (outputLeft < 0 || outputLeft >= numOutputChannels)
        return;
    
    if (outputRight < 0 || outputRight >= numOutputChannels)
        return;
    
    // Mix to output (use addFrom to allow multiple channels on same output)
    if (outputChannelData[outputLeft] != nullptr && source.getNumChannels() > 0)
    {
        juce::FloatVectorOperations::add(outputChannelData[outputLeft],
                                         source.getReadPointer(0),
                                         numSamples);
    }
    
    if (outputChannelData[outputRight] != nullptr && source.getNumChannels() > 1)
    {
        juce::FloatVectorOperations::add(outputChannelData[outputRight],
                                         source.getReadPointer(1),
                                         numSamples);
    }
}

void VSTiChannel::applyGain(juce::AudioBuffer<float>& buffer, int numSamples)
{
    const float gain = gainLinear.load(std::memory_order_relaxed);
    
    if (gain == 0.0f)
    {
        buffer.clear(0, numSamples);
        return;
    }
    
    if (gain == 1.0f)
        return;  // Unity gain, no processing needed
    
    // Apply gain to both channels
    buffer.applyGain(0, numSamples, gain);
}
