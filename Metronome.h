#pragma once

#include <JuceHeader.h>
#include <atomic>

/**
 * @file Metronome.h
 * @brief Sample-accurate sine-wave metronome
 *
 * Spec compliance:
 *  - Sinusgenerator, samplegenau, BPM-basiert
 *  - Kein Taktmaß, keine Betonung (every beat identical)
 *  - Nicht in Loop aufgenommen (writes directly to hardware output)
 *  - Frei wählbarer Ausgangskanal
 *  - BPM-Änderung nur im gestoppten Zustand
 *
 * Thread-safety:
 *  - processBlock() runs on the audio thread (real-time safe)
 *  - All setters are called from the message thread and write atomics only
 */
class Metronome
{
public:
    //==========================================================================
    Metronome();
    ~Metronome() = default;

    //==========================================================================
    // Setup (message thread, before or while stopped)
    //==========================================================================

    /**
     * @brief Prepare for playback — call whenever audio device starts or
     *        sample rate changes. Resets the internal phase.
     */
    void prepareToPlay(double sampleRate);

    /** Reset phase to zero (e.g. on song switch or playhead reset). */
    void reset();

    //==========================================================================
    // Configuration (message thread)
    //==========================================================================

    /** Enable / disable the metronome click. */
    void setEnabled(bool enabled) { isEnabled.store(enabled, std::memory_order_release); }
    bool getEnabled() const       { return isEnabled.load(std::memory_order_relaxed); }

    /**
     * @brief Set BPM.
     * WARNING: only call while transport is stopped (spec requirement).
     */
    void setBPM(double bpm);
    double getBPM() const { return currentBPM.load(std::memory_order_relaxed); }

    /**
     * @brief Set hardware output channel indices (0-based).
     * @param left  Left output channel index
     * @param right Right output channel index
     */
    void setOutputChannels(int left, int right);
    int getOutputLeft()  const { return outputLeft.load(std::memory_order_relaxed); }
    int getOutputRight() const { return outputRight.load(std::memory_order_relaxed); }

    /** Click tone frequency in Hz (default 1000 Hz). */
    void setClickFrequency(double hz);

    /** Click duration in milliseconds (default 10 ms). */
    void setClickDurationMs(double ms);

    /** Click amplitude 0..1 (default 0.7). */
    void setAmplitude(float amp) { amplitude.store(amp, std::memory_order_release); }

    //==========================================================================
    // Audio thread
    //==========================================================================

    /**
     * @brief Generate metronome clicks into the hardware output buffers.
     *
     * Adds (not replaces) to existing output so multiple channels can share
     * the same hardware output pair.
     *
     * @param outputChannelData  Raw hardware output pointers
     * @param numOutputChannels  Number of available output channels
     * @param numSamples         Block size
     * @param globalPlayhead     Current playhead position from LoopEngine (samples)
     * @param isPlaying          Whether transport is running
     */
    void processBlock(float* const* outputChannelData,
                      int            numOutputChannels,
                      int            numSamples,
                      juce::int64    globalPlayhead,
                      bool           isPlaying);

private:
    //==========================================================================
    // Atomics (shared between threads)
    std::atomic<bool>   isEnabled      {false};
    std::atomic<double> currentBPM     {120.0};
    std::atomic<int>    outputLeft     {0};
    std::atomic<int>    outputRight    {1};
    std::atomic<double> clickFreqHz    {1000.0};
    std::atomic<double> clickDurationMs{10.0};
    std::atomic<float>  amplitude      {0.7f};

    //==========================================================================
    // Audio-thread only (no atomics needed)
    double sampleRate        {44100.0};
    double samplesPerBeat    {0.0};   // Recalculated from BPM + sampleRate

    // Sine generator state
    double sinePhase         {0.0};   // Current phase of the sine oscillator [0, 2π)
    double sinePhaseIncrement{0.0};   // Phase increment per sample

    // Click envelope
    juce::int64 clickSampleCountdown{0};  // Samples remaining in current click
    juce::int64 clickDurationSamples{0};  // Total click length in samples

    // Beat tracking
    double beatPhaseAccumulator{0.0};  // Fractional position within current beat [0, samplesPerBeat)

    //==========================================================================
    // Helpers (audio thread)

    /** Recalculate samplesPerBeat and clickDurationSamples from current atomics. */
    void recalculate();

    /** Generate one sample of the click sine and advance phase. */
    float nextSineSample();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Metronome)
};
