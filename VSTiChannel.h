#pragma once

#include "Channel.h"

/**
 * @file VSTiChannel.h
 * @brief Virtual instrument channel with MIDI input
 * 
 * Signal flow:
 * MIDI Input → Channel Filter → VSTi → FX Chain → Split:
 *                                                   ├─ Live Monitor (always)
 *                                                   └─ Record → Loop → Playback → Output
 */

//==============================================================================
/**
 * @brief Virtual instrument channel processing MIDI input
 * 
 * Features:
 * - MIDI channel filtering (1-16 or all channels)
 * - VSTi hosting (single instrument)
 * - Insert FX chain (3 slots)
 * - Live monitoring (always active for VSTi)
 * - Recording/Overdub to loop buffer
 * - Playback from loop buffer
 * - Configurable output routing
 */
class VSTiChannel : public Channel
{
public:
    //==============================================================================
    /**
     * @brief Construct VSTi channel
     * @param channelIndex Channel number (0-5)
     */
    explicit VSTiChannel(int channelIndex);
    
    ~VSTiChannel() override = default;
    
    //==============================================================================
    // VSTi Management
    //==============================================================================
    
    /**
     * @brief Set the virtual instrument plugin
     * @param instrument Plugin instance (takes ownership)
     */
    void setVSTi(std::unique_ptr<juce::AudioPluginInstance> instrument);
    
    /**
     * @brief Remove the virtual instrument
     */
    void removeVSTi();
    
    /**
     * @brief Get the VSTi plugin instance
     */
    juce::AudioPluginInstance* getVSTi() const { return vsti.get(); }
    
    /**
     * @brief Check if VSTi is loaded
     */
    bool hasVSTi() const { return vsti != nullptr; }
    
    //==============================================================================
    // MIDI Configuration
    //==============================================================================
    
    /**
     * @brief Set MIDI channel filter
     * @param channel 0 = all channels, 1-16 = specific MIDI channel
     */
    void setMIDIChannelFilter(int channel);
    
    /**
     * @brief Get current MIDI channel filter
     */
    int getMIDIChannelFilter() const 
    { 
        return routing.midiChannelFilter; 
    }
    
    //==============================================================================
    // Audio Processing
    //==============================================================================
    
    /**
     * @brief Process one audio block
     * 
     * Processing steps:
     * 1. Filter MIDI by channel (if configured)
     * 2. Process VSTi with MIDI
     * 3. Process FX chain
     * 4. Live monitoring (always for VSTi)
     * 5. Record/Overdub (if in recording state)
     * 6. Playback from loop
     * 7. Apply gain
     * 8. Route to output
     */
    void processBlock(const float* const* inputChannelData,
                     float* const* outputChannelData,
                     const juce::MidiBuffer& midiBuffer,
                     int numSamples,
                     juce::int64 playheadPosition,
                     juce::int64 loopLength,
                     int numInputChannels,
                     int numOutputChannels) override;
    
    /**
     * @brief Prepare VSTi for playback
     */
    void prepareToPlay(double sampleRate,
                      int maxBlockSize,
                      juce::int64 maxLoopLengthSamples) override;
    
    /**
     * @brief Release VSTi resources
     */
    void releaseResources() override;
    
private:
    //==============================================================================
    // VSTi instance
    std::unique_ptr<juce::AudioPluginInstance> vsti;
    std::atomic<bool> vstiCrashed{false};
    
    // Working buffers
    juce::AudioBuffer<float> vstiOutputBuffer;
    juce::MidiBuffer filteredMidiBuffer;
    
    //==============================================================================
    // Private Processing Stages
    
    /**
     * @brief Filter MIDI events by channel
     * @param source Input MIDI buffer
     * @param dest Output MIDI buffer (filtered)
     * @param filterChannel 0 = all, 1-16 = specific channel
     */
    void filterMIDI(const juce::MidiBuffer& source,
                   juce::MidiBuffer& dest,
                   int filterChannel);
    
    /**
     * @brief Process VSTi with MIDI input
     * @param outputBuffer Buffer to fill with VSTi output
     * @param midiBuffer MIDI events to send to VSTi
     * @param numSamples Number of samples to process
     */
    void processVSTi(juce::AudioBuffer<float>& outputBuffer,
                    juce::MidiBuffer& midiBuffer,
                    int numSamples);
    
    /**
     * @brief Mix processed audio to hardware output
     */
    void routeOutput(float* const* outputChannelData,
                    const juce::AudioBuffer<float>& source,
                    int numOutputChannels,
                    int numSamples);
    
    /**
     * @brief Apply channel gain to buffer
     */
    void applyGain(juce::AudioBuffer<float>& buffer, int numSamples);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VSTiChannel)
};
