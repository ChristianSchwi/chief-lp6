#pragma once

#include <JuceHeader.h>
#include <atomic>

/**
 * @file LoopEngine.h
 * @brief Global loop engine managing playhead position and quantization
 * 
 * This is the master clock for the entire looper application.
 * All 6 channels are synchronized to this global loop length and playhead.
 */

//==============================================================================
/**
 * @brief Global loop engine with sample-accurate playhead and quantization
 * 
 * Key responsibilities:
 * - Maintain global playhead position (sample-based)
 * - Enforce global loop length (all channels same length)
 * - Provide beat quantization for recording start/stop
 * - Calculate loop length from BPM and beats
 * 
 * Thread-safety:
 * - Audio thread: reads atomics, advances playhead
 * - Message thread: writes atomics via commands
 */
class LoopEngine
{
public:
    //==============================================================================
    LoopEngine();
    
    //==============================================================================
    // Audio Thread Interface (Real-Time Safe)
    //==============================================================================
    
    /**
     * @brief Process one audio block and advance playhead
     * @param numSamples Number of samples in current buffer
     * @param isPlaying Whether playback is active
     */
    void processBlock(int numSamples, bool isPlaying);
    
    /**
     * @brief Get current playhead position in samples
     * @return Current position (0 to loopLength-1)
     */
    juce::int64 getCurrentPlayhead() const
    {
        return playheadPosition.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Get global loop length in samples
     * @return Loop length (0 = not set yet)
     */
    juce::int64 getLoopLength() const
    {
        return loopLengthSamples.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Check if loop length has been established
     */
    bool hasLoopLength() const
    {
        return getLoopLength() > 0;
    }
    
    /**
     * @brief Check if playhead is currently at loop boundary
     * @param tolerance Samples before/after loop point to consider "at boundary"
     */
    bool isAtLoopBoundary(int tolerance = 0) const
    {
        if (!hasLoopLength())
            return false;
        
        juce::int64 pos = getCurrentPlayhead();
        juce::int64 length = getLoopLength();
        
        return (pos <= tolerance) || (pos >= length - tolerance);
    }
    
    /**
     * @brief Calculate if recording should start based on quantization
     * @param currentSample Current playhead position
     * @return true if this is a valid start point
     */
    bool shouldStartRecording(juce::int64 currentSample) const;
    
    /**
     * @brief Get next quantized position from a given sample
     * @param fromSample Starting sample position
     * @return Next quantized sample position
     */
    juce::int64 getNextQuantizedPosition(juce::int64 fromSample) const;
    
    /**
     * @brief Get samples remaining until next quantization point
     * @param fromSample Starting sample position
     * @return Number of samples until next quantized position
     */
    juce::int64 getSamplesUntilNextBeat(juce::int64 fromSample) const;
    
    /**
     * @brief Check if current position is on a beat boundary
     * @param tolerance Samples of tolerance
     */
    bool isOnBeat(int tolerance = 1) const;
    
    //==============================================================================
    // Message Thread Interface (Non-Real-Time)
    //==============================================================================
    
    /**
     * @brief Set loop length explicitly (in samples)
     * Called when first recording completes or from song load
     */
    void setLoopLength(juce::int64 lengthInSamples);
    
    /**
     * @brief Enable/disable beat quantization
     */
    void setQuantizationEnabled(bool enabled);
    
    /**
     * @brief Set BPM for metronome mode
     * Only allowed when playback is stopped
     */
    void setBPM(double bpm);
    
    /**
     * @brief Set number of beats per loop (for metronome mode)
     */
    void setBeatsPerLoop(int beats);
    
    /**
     * @brief Calculate loop length from current BPM and beats
     * Must be called after setBPM() or setBeatsPerLoop()
     */
    void calculateLoopLengthFromBPM();
    
    /**
     * @brief Reset playhead to zero
     */
    void resetPlayhead();
    
    /**
     * @brief Prepare for new sample rate
     * Called when audio device changes
     */
    void setSampleRate(double newSampleRate);
    
    /**
     * @brief Get current sample rate
     */
    double getSampleRate() const { return sampleRate; }
    
    /**
     * @brief Get current BPM
     */
    double getBPM() const { return bpm.load(std::memory_order_relaxed); }
    
    /**
     * @brief Get beats per loop
     */
    int getBeatsPerLoop() const { return beatsPerLoop.load(std::memory_order_relaxed); }
    
    /**
     * @brief Check if quantization is enabled
     */
    bool isQuantizationEnabled() const { return quantizationEnabled.load(std::memory_order_relaxed); }
    
    //==============================================================================
    // Diagnostics
    //==============================================================================
    
    /**
     * @brief Get loop length in seconds
     */
    double getLoopLengthSeconds() const
    {
        if (sampleRate <= 0.0)
            return 0.0;
        return static_cast<double>(getLoopLength()) / sampleRate;
    }
    
    /**
     * @brief Get current playhead position in seconds
     */
    double getPlayheadSeconds() const
    {
        if (sampleRate <= 0.0)
            return 0.0;
        return static_cast<double>(getCurrentPlayhead()) / sampleRate;
    }
    
private:
    //==============================================================================
    // Atomic state (shared between threads)
    std::atomic<juce::int64> playheadPosition{0};
    std::atomic<juce::int64> loopLengthSamples{0};
    std::atomic<bool> quantizationEnabled{false};
    std::atomic<double> bpm{120.0};
    std::atomic<int> beatsPerLoop{4};
    
    // Non-atomic (only accessed from message thread or during setup)
    double sampleRate{44100.0};
    juce::int64 samplesPerBeat{0};
    
    //==============================================================================
    // Internal helpers
    void updateSamplesPerBeat();
    juce::int64 quantizeToNearestBeat(juce::int64 samplePosition) const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopEngine)
};
