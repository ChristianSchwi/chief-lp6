#include "AudioChannel.h"

//==============================================================================
AudioChannel::AudioChannel(int index)
    : Channel(index, ChannelType::Audio)
{
}

//==============================================================================
// Main Processing
//==============================================================================

void AudioChannel::processBlock(const float* const* inputChannelData,
                                float* const* outputChannelData,
                                const juce::MidiBuffer& midiBuffer,
                                int numSamples,
                                juce::int64 playheadPosition,
                                juce::int64 loopLength,
                                int numInputChannels,
                                int numOutputChannels)
{
    // Clear working buffer
    workingBuffer.clear(0, numSamples);
    
    // Check if muted
    const bool isMutedNow = muted.load(std::memory_order_relaxed);
    const ChannelState currentState = state.load(std::memory_order_relaxed);
    
    //==========================================================================
    // 1. ROUTE INPUT FROM HARDWARE
    //==========================================================================
    routeInput(inputChannelData, numInputChannels, numSamples);
    
    //==========================================================================
    // 2. PROCESS FX CHAIN
    //==========================================================================
    juce::MidiBuffer emptyMidi;  // Audio channels don't use MIDI
    processFXChain(workingBuffer, numSamples, emptyMidi);
    
    //==========================================================================
    // 3. MONITORING (Post-FX)
    //==========================================================================
    if (shouldMonitor() && !isMutedNow)
    {
        // Monitor signal goes to output
        routeOutput(outputChannelData, workingBuffer, numOutputChannels, numSamples);
    }
    
    //==========================================================================
    // 4. RECORDING / OVERDUBBING
    //==========================================================================
    if (currentState == ChannelState::Recording && loopLength > 0)
    {
        // First recording pass - replace any existing content
        recordToLoop(workingBuffer, playheadPosition, numSamples, false);
    }
    else if (currentState == ChannelState::Overdubbing && loopLength > 0)
    {
        // Overdub - add to existing content
        recordToLoop(workingBuffer, playheadPosition, numSamples, true);
    }
    
    //==========================================================================
    // 5. PLAYBACK FROM LOOP
    //==========================================================================
    if ((currentState == ChannelState::Playing || 
         currentState == ChannelState::Overdubbing) &&
        loopLength > 0)
    {
        // Clear working buffer for playback
        workingBuffer.clear(0, numSamples);
        
        // Read from loop buffer
        playFromLoop(workingBuffer, playheadPosition, numSamples);
        
        //======================================================================
        // 6. APPLY GAIN
        //======================================================================
        applyGain(workingBuffer, numSamples);
        
        //======================================================================
        // 7. ROUTE TO OUTPUT
        //======================================================================
        if (!isMutedNow)
        {
            routeOutput(outputChannelData, workingBuffer, numOutputChannels, numSamples);
        }
    }
}

//==============================================================================
// Private Processing Stages
//==============================================================================

void AudioChannel::routeInput(const float* const* inputChannelData,
                              int numInputChannels,
                              int numSamples)
{
    // Safety check
    if (inputChannelData == nullptr || numInputChannels == 0 || numSamples <= 0)
        return;
    
    // Ensure working buffer is large enough
    if (workingBuffer.getNumSamples() < numSamples)
        return;
    
    const int inputLeft = routing.inputChannelLeft;
    const int inputRight = routing.inputChannelRight;
    
    // Check if this is a valid input routing
    if (inputLeft < 0 || inputLeft >= numInputChannels)
        return;  // No valid input
    
    // Route left channel
    if (inputChannelData[inputLeft] != nullptr)
    {
        workingBuffer.copyFrom(0, 0,
                             inputChannelData[inputLeft],
                             numSamples);
    }
    else
    {
        workingBuffer.clear(0, 0, numSamples);
    }
    
    // Route right channel
    if (inputRight == -1)
    {
        // Mono mode: duplicate left channel to right
        workingBuffer.copyFrom(1, 0,
                             workingBuffer, 0, 0,
                             numSamples);
    }
    else if (inputRight >= 0 && inputRight < numInputChannels)
    {
        // Stereo mode: use separate right channel
        if (inputChannelData[inputRight] != nullptr)
        {
            workingBuffer.copyFrom(1, 0,
                                 inputChannelData[inputRight],
                                 numSamples);
        }
        else
        {
            workingBuffer.clear(1, 0, numSamples);
        }
    }
    else
    {
        // Invalid right channel - clear it
        workingBuffer.clear(1, 0, numSamples);
    }
}

void AudioChannel::routeOutput(float* const* outputChannelData,
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

void AudioChannel::applyGain(juce::AudioBuffer<float>& buffer, int numSamples)
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
