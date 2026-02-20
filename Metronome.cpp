#include "Metronome.h"

//==============================================================================
Metronome::Metronome()
{
    recalculate();
}

//==============================================================================
// Setup
//==============================================================================

void Metronome::prepareToPlay(double newSampleRate)
{
    jassert(newSampleRate > 0.0);
    sampleRate = newSampleRate;
    reset();
    recalculate();

    DBG("Metronome prepared: " + juce::String(sampleRate) + " Hz, " +
        juce::String(currentBPM.load(), 1) + " BPM, " +
        juce::String(samplesPerBeat, 1) + " samples/beat");
}

void Metronome::reset()
{
    sinePhase            = 0.0;
    beatPhaseAccumulator = 0.0;
    clickSampleCountdown = 0;
}

//==============================================================================
// Configuration (message thread)
//==============================================================================

void Metronome::setBPM(double bpm)
{
    jassert(bpm > 0.0 && bpm <= 999.0);
    currentBPM.store(bpm, std::memory_order_release);
    // samplesPerBeat is recalculated at the start of the next processBlock()
}

void Metronome::setOutputChannels(int left, int right)
{
    outputLeft.store(left,  std::memory_order_release);
    outputRight.store(right, std::memory_order_release);
}

void Metronome::setClickFrequency(double hz)
{
    jassert(hz > 0.0);
    clickFreqHz.store(hz, std::memory_order_release);
}

void Metronome::setClickDurationMs(double ms)
{
    jassert(ms > 0.0);
    clickDurationMs.store(ms, std::memory_order_release);
}

//==============================================================================
// Audio Thread
//==============================================================================

void Metronome::processBlock(float* const* outputChannelData,
                             int            numOutputChannels,
                             int            numSamples,
                             juce::int64    globalPlayhead,
                             bool           isPlaying)
{
    // Nothing to do if disabled or not playing
    if (!isEnabled.load(std::memory_order_relaxed) || !isPlaying)
    {
        clickSampleCountdown = 0;
        return;
    }

    // Safety check
    if (outputChannelData == nullptr || numSamples <= 0)
        return;

    // Validate output channels
    const int outL = outputLeft.load(std::memory_order_relaxed);
    const int outR = outputRight.load(std::memory_order_relaxed);

    const bool hasL = (outL >= 0 && outL < numOutputChannels && outputChannelData[outL] != nullptr);
    const bool hasR = (outR >= 0 && outR < numOutputChannels && outputChannelData[outR] != nullptr);

    if (!hasL && !hasR)
        return;

    // Recalculate timing parameters (cheap, reads atomics once per block)
    recalculate();

    if (samplesPerBeat <= 0.0)
        return;

    // Sync beat phase to global playhead to stay in lockstep with LoopEngine.
    // This keeps the metronome click exactly on beat even after a loop wrap.
    beatPhaseAccumulator = std::fmod(static_cast<double>(globalPlayhead), samplesPerBeat);

    // Generate samples
    for (int i = 0; i < numSamples; ++i)
    {
        // Detect beat boundary: accumulator crosses zero this sample
        // We check BEFORE advancing so sample 0 of a beat gets the click.
        const bool onBeat = (beatPhaseAccumulator < 1.0);

        if (onBeat)
        {
            // Start a new click: reset sine phase and set countdown
            sinePhase            = 0.0;
            clickSampleCountdown = clickDurationSamples;
        }

        // Generate output sample
        float sample = 0.0f;
        if (clickSampleCountdown > 0)
        {
            // Simple linear decay envelope: full amplitude at start, zero at end
            const float envelope = static_cast<float>(clickSampleCountdown) /
                                   static_cast<float>(clickDurationSamples);
            sample = nextSineSample() * amplitude.load(std::memory_order_relaxed) * envelope;
            --clickSampleCountdown;
        }

        // Write to output (additive — do not overwrite other channels)
        if (hasL) outputChannelData[outL][i] += sample;
        if (hasR) outputChannelData[outR][i] += sample;

        // Advance beat phase accumulator
        beatPhaseAccumulator += 1.0;
        if (beatPhaseAccumulator >= samplesPerBeat)
            beatPhaseAccumulator -= samplesPerBeat;
    }
}

//==============================================================================
// Private Helpers
//==============================================================================

void Metronome::recalculate()
{
    const double bpm = currentBPM.load(std::memory_order_relaxed);
    const double ms  = clickDurationMs.load(std::memory_order_relaxed);
    const double hz  = clickFreqHz.load(std::memory_order_relaxed);

    if (sampleRate <= 0.0 || bpm <= 0.0)
        return;

    // Samples per beat
    samplesPerBeat = (60.0 / bpm) * sampleRate;

    // Click duration in samples (minimum 1 sample)
    clickDurationSamples = juce::jmax(
        static_cast<juce::int64>(1),
        static_cast<juce::int64>((ms / 1000.0) * sampleRate));

    // Sine phase increment per sample
    sinePhaseIncrement = (2.0 * juce::MathConstants<double>::pi * hz) / sampleRate;
}

float Metronome::nextSineSample()
{
    const float sample = static_cast<float>(std::sin(sinePhase));
    sinePhase += sinePhaseIncrement;

    // Keep phase in [0, 2π) to avoid floating-point drift over time
    if (sinePhase >= juce::MathConstants<double>::twoPi)
        sinePhase -= juce::MathConstants<double>::twoPi;

    return sample;
}
