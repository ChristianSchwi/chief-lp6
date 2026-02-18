#pragma once

#include "Channel.h"

/**
 * @file AudioChannel.h
 * @brief Audio channel with hardware input routing
 * 
 * Signal flow:
 * Hardware Input → Routing (Mono/Stereo) → FX Chain → Monitor + Record → Loop → Playback → Output
 */

//==============================================================================
/**
 * @brief Audio channel processing hardware inputs
 * 
 * Features:
 * - Configurable mono/stereo input routing
 * - Mono input duplication to stereo
 * - Insert FX chain (3 slots)
 * - Post-FX monitoring
 * - Recording/Overdub to loop buffer
 * - Playback from loop buffer
 * - Configurable output routing
 */
class AudioChannel : public Channel
{
public:
    //==============================================================================
    /**
     * @brief Construct audio channel
     * @param channelIndex Channel number (0-5)
     */
    explicit AudioChannel(int channelIndex);
    
    ~AudioChannel() override = default;
    
    //==============================================================================
    // Audio Processing
    //==============================================================================
    
    /**
     * @brief Process one audio block
     * 
     * Processing steps:
     * 1. Route input (mono/stereo handling)
     * 2. Process FX chain
     * 3. Monitoring (if enabled)
     * 4. Record/Overdub (if in recording state)
     * 5. Playback from loop
     * 6. Apply gain
     * 7. Route to output
     */
    void processBlock(const float* const* inputChannelData,
                     float* const* outputChannelData,
                     const juce::MidiBuffer& midiBuffer,
                     int numSamples,
                     juce::int64 playheadPosition,
                     juce::int64 loopLength,
                     int numInputChannels,
                     int numOutputChannels) override;
    
private:
    //==============================================================================
    // Private Processing Stages
    
    /**
     * @brief Route input from hardware to working buffer
     * Handles mono→stereo duplication
     */
    void routeInput(const float* const* inputChannelData,
                   int numInputChannels,
                   int numSamples);
    
    /**
     * @brief Mix processed audio to hardware output
     * Uses addFrom to allow multiple channels on same output
     */
    void routeOutput(float* const* outputChannelData,
                    const juce::AudioBuffer<float>& source,
                    int numOutputChannels,
                    int numSamples);
    
    /**
     * @brief Apply channel gain to buffer
     */
    void applyGain(juce::AudioBuffer<float>& buffer, int numSamples);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioChannel)
};
