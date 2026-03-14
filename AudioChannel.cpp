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
    // Oneshot channels use entirely independent processing
    if (oneShot.load(std::memory_order_relaxed))
    {
        processOneShotBlock(inputChannelData, outputChannelData,
                            numSamples, numInputChannels, numOutputChannels);
        return;
    }

    checkAndExecutePendingStop(playheadPosition, loopLength, numSamples);
    checkOneShotStop(playheadPosition, loopLength, numSamples);

    // Clear working and output-mix buffers
    workingBuffer.clear(0, numSamples);
    fxBuffer.clear(0, numSamples);

    // Check if muted (user mute OR silenced by another channel's solo)
    const bool isMutedNow = muted.load(std::memory_order_relaxed)
                         || soloMuted.load(std::memory_order_relaxed);
    const ChannelState currentState = state.load(std::memory_order_relaxed);

    //==========================================================================
    // 1. ROUTE INPUT FROM HARDWARE (dry signal → workingBuffer)
    //==========================================================================
    routeInput(inputChannelData, numInputChannels, numSamples);

    // Compute input peaks
    {
        auto rangeL = juce::FloatVectorOperations::findMinAndMax(workingBuffer.getReadPointer(0), numSamples);
        auto rangeR = juce::FloatVectorOperations::findMinAndMax(workingBuffer.getReadPointer(1), numSamples);
        inputPeakL.store(juce::jmax(std::abs(rangeL.getStart()), std::abs(rangeL.getEnd())), std::memory_order_relaxed);
        inputPeakR.store(juce::jmax(std::abs(rangeR.getStart()), std::abs(rangeR.getEnd())), std::memory_order_relaxed);
    }

    //==========================================================================
    // 2. RECORD DRY SIGNAL (before FX — loop always stores clean audio)
    //==========================================================================
    if (currentState == ChannelState::Recording)
        recordToLoop(workingBuffer, playheadPosition, numSamples, false);
    else if (currentState == ChannelState::Overdubbing && loopLength > 0)
        recordToLoop(workingBuffer, playheadPosition, numSamples, true);

    //==========================================================================
    // 3. BUILD OUTPUT MIX IN fxBuffer
    //    Monitoring: add dry input; Playback: add loop signal with gain.
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

        // Compute loop peaks
        {
            auto rangeL = juce::FloatVectorOperations::findMinAndMax(workingBuffer.getReadPointer(0), numSamples);
            auto rangeR = juce::FloatVectorOperations::findMinAndMax(workingBuffer.getReadPointer(1), numSamples);
            loopPeakL.store(juce::jmax(std::abs(rangeL.getStart()), std::abs(rangeL.getEnd())), std::memory_order_relaxed);
            loopPeakR.store(juce::jmax(std::abs(rangeR.getStart()), std::abs(rangeR.getEnd())), std::memory_order_relaxed);
        }

        for (int ch = 0; ch < fxBuffer.getNumChannels(); ++ch)
            fxBuffer.addFrom(ch, 0, workingBuffer, ch, 0, numSamples);
    }
    else
    {
        loopPeakL.store(0.0f, std::memory_order_relaxed);
        loopPeakR.store(0.0f, std::memory_order_relaxed);
    }

    //==========================================================================
    // 4. PROCESS FX CHAIN (applied once to combined output signal)
    //==========================================================================
    juce::MidiBuffer emptyMidi;
    processFXChain(fxBuffer, numSamples, emptyMidi);

    //==========================================================================
    // 5. ROUTE TO OUTPUT
    //==========================================================================
    if (!isMutedNow)
        routeOutput(outputChannelData, fxBuffer, numOutputChannels, numSamples);
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

//==============================================================================
void AudioChannel::processOneShotBlock(const float* const* inputChannelData,
                                       float* const* outputChannelData,
                                       int numSamples,
                                       int numInputChannels,
                                       int numOutputChannels)
{
    // Check oneshot voice completion
    checkOneShotStop(0, 0, numSamples);

    workingBuffer.clear(0, numSamples);
    fxBuffer.clear(0, numSamples);

    const bool isMutedNow = muted.load(std::memory_order_relaxed)
                         || soloMuted.load(std::memory_order_relaxed);
    const ChannelState currentState = state.load(std::memory_order_relaxed);

    // 1. Route input
    routeInput(inputChannelData, numInputChannels, numSamples);

    // Input peaks
    {
        auto rangeL = juce::FloatVectorOperations::findMinAndMax(workingBuffer.getReadPointer(0), numSamples);
        auto rangeR = juce::FloatVectorOperations::findMinAndMax(workingBuffer.getReadPointer(1), numSamples);
        inputPeakL.store(juce::jmax(std::abs(rangeL.getStart()), std::abs(rangeL.getEnd())), std::memory_order_relaxed);
        inputPeakR.store(juce::jmax(std::abs(rangeR.getStart()), std::abs(rangeR.getEnd())), std::memory_order_relaxed);
    }

    // 2. Record using oneshot's own playhead
    if (currentState == ChannelState::Recording)
    {
        const juce::int64 pos = oneShotPlayhead.load(std::memory_order_relaxed);
        recordToLoop(workingBuffer, pos, numSamples, false);
        oneShotPlayhead.store(pos + numSamples, std::memory_order_release);
    }

    // 3. Monitor
    if (shouldMonitor())
    {
        for (int ch = 0; ch < fxBuffer.getNumChannels(); ++ch)
            fxBuffer.addFrom(ch, 0, workingBuffer, ch, 0, numSamples);
    }

    // 4. Playback: sum all active voices
    {
        bool anyVoiceActive = false;
        const juce::int64 len = oneShotLength.load(std::memory_order_relaxed);

        if (len > 0)
        {
            for (auto& voice : oneShotVoices)
            {
                juce::int64 pos = voice.load(std::memory_order_relaxed);
                if (pos < 0 || pos >= len) continue;

                anyVoiceActive = true;
                workingBuffer.clear(0, numSamples);

                // Read from loop at voice position (no wrap — oneshot doesn't loop)
                const int samplesToRead = static_cast<int>(
                    juce::jmin(static_cast<juce::int64>(numSamples), len - pos));
                if (samplesToRead > 0)
                    playFromLoop(workingBuffer, pos, samplesToRead);

                applyGain(workingBuffer, numSamples);

                for (int ch = 0; ch < fxBuffer.getNumChannels(); ++ch)
                    fxBuffer.addFrom(ch, 0, workingBuffer, ch, 0, numSamples);

                voice.store(pos + numSamples, std::memory_order_release);
            }
        }

        if (anyVoiceActive)
        {
            auto rangeL = juce::FloatVectorOperations::findMinAndMax(fxBuffer.getReadPointer(0), numSamples);
            auto rangeR = juce::FloatVectorOperations::findMinAndMax(fxBuffer.getReadPointer(1), numSamples);
            loopPeakL.store(juce::jmax(std::abs(rangeL.getStart()), std::abs(rangeL.getEnd())), std::memory_order_relaxed);
            loopPeakR.store(juce::jmax(std::abs(rangeR.getStart()), std::abs(rangeR.getEnd())), std::memory_order_relaxed);
        }
        else
        {
            loopPeakL.store(0.0f, std::memory_order_relaxed);
            loopPeakR.store(0.0f, std::memory_order_relaxed);
        }
    }

    // 5. FX chain
    juce::MidiBuffer emptyMidi;
    processFXChain(fxBuffer, numSamples, emptyMidi);

    // 6. Route to output
    if (!isMutedNow)
        routeOutput(outputChannelData, fxBuffer, numOutputChannels, numSamples);
}
