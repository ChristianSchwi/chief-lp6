#include "LoopEngine.h"

//==============================================================================
LoopEngine::LoopEngine()
{
    updateSamplesPerBeat();
}

//==============================================================================
// Audio Thread Methods
//==============================================================================

void LoopEngine::processBlock(int numSamples, bool isPlaying)
{
    if (!isPlaying)
        return;
    
    const juce::int64 loopLength = loopLengthSamples.load(std::memory_order_relaxed);
    
    if (loopLength <= 0)
    {
        // No loop established yet, just advance playhead freely
        playheadPosition.fetch_add(numSamples, std::memory_order_relaxed);
        return;
    }
    
    // Advance playhead and wrap at loop boundary
    juce::int64 currentPos = playheadPosition.load(std::memory_order_relaxed);
    juce::int64 newPos = currentPos + numSamples;
    
    // Handle wrap-around
    while (newPos >= loopLength)
        newPos -= loopLength;
    
    playheadPosition.store(newPos, std::memory_order_relaxed);
}

bool LoopEngine::shouldStartRecording(juce::int64 currentSample) const
{
    if (!quantizationEnabled.load(std::memory_order_relaxed))
        return true;  // Start immediately if quantization disabled
    
    if (samplesPerBeat <= 0)
        return false;  // No valid beat grid
    
    // Check if we're within tolerance of a beat boundary
    const juce::int64 beatPosition = currentSample % samplesPerBeat;
    const int tolerance = 64;  // ~1.5ms at 44.1kHz
    
    return (beatPosition <= tolerance) || (beatPosition >= samplesPerBeat - tolerance);
}

juce::int64 LoopEngine::getNextQuantizedPosition(juce::int64 fromSample) const
{
    if (!quantizationEnabled.load(std::memory_order_relaxed) || samplesPerBeat <= 0)
        return fromSample;  // No quantization
    
    // Calculate next beat boundary
    const juce::int64 currentBeat = fromSample / samplesPerBeat;
    const juce::int64 nextBeat = currentBeat + 1;
    const juce::int64 nextBeatSample = nextBeat * samplesPerBeat;
    
    return nextBeatSample;
}

juce::int64 LoopEngine::getSamplesUntilNextBeat(juce::int64 fromSample) const
{
    if (!quantizationEnabled.load(std::memory_order_relaxed) || samplesPerBeat <= 0)
        return 0;
    
    const juce::int64 nextBeat = getNextQuantizedPosition(fromSample);
    return nextBeat - fromSample;
}

bool LoopEngine::isOnBeat(int tolerance) const
{
    if (!quantizationEnabled.load(std::memory_order_relaxed) || samplesPerBeat <= 0)
        return false;
    
    const juce::int64 currentPos = getCurrentPlayhead();
    const juce::int64 beatPosition = currentPos % samplesPerBeat;
    
    return (beatPosition <= tolerance) || (beatPosition >= samplesPerBeat - tolerance);
}

//==============================================================================
// Message Thread Methods
//==============================================================================

void LoopEngine::setLoopLength(juce::int64 lengthInSamples)
{
    jassert(lengthInSamples >= 0);
    loopLengthSamples.store(lengthInSamples, std::memory_order_release);
    
    // Reset playhead if it's beyond the new length
    juce::int64 currentPos = playheadPosition.load(std::memory_order_relaxed);
    if (currentPos >= lengthInSamples)
    {
        playheadPosition.store(0, std::memory_order_release);
    }
}

void LoopEngine::setQuantizationEnabled(bool enabled)
{
    quantizationEnabled.store(enabled, std::memory_order_release);
}

void LoopEngine::setBPM(double newBpm)
{
    jassert(newBpm > 0.0 && newBpm <= 999.0);
    
    bpm.store(newBpm, std::memory_order_release);
    updateSamplesPerBeat();
}

void LoopEngine::setBeatsPerLoop(int beats)
{
    jassert(beats > 0 && beats <= 64);
    
    beatsPerLoop.store(beats, std::memory_order_release);
}

void LoopEngine::calculateLoopLengthFromBPM()
{
    const double currentBpm = bpm.load(std::memory_order_relaxed);
    const int beats = beatsPerLoop.load(std::memory_order_relaxed);
    
    if (currentBpm <= 0.0 || beats <= 0 || sampleRate <= 0.0)
    {
        DBG("LoopEngine::calculateLoopLengthFromBPM() - Invalid parameters");
        return;
    }
    
    // Formula: loopLengthSamples = beats * (60.0 / bpm) * sampleRate
    const double secondsPerBeat = 60.0 / currentBpm;
    const double loopLengthSeconds = beats * secondsPerBeat;
    const juce::int64 lengthInSamples = static_cast<juce::int64>(loopLengthSeconds * sampleRate);
    
    setLoopLength(lengthInSamples);
    
    DBG("Loop length calculated: " + juce::String(lengthInSamples) + 
        " samples (" + juce::String(loopLengthSeconds, 2) + " seconds)");
}

void LoopEngine::resetPlayhead()
{
    playheadPosition.store(0, std::memory_order_release);
}

void LoopEngine::setSampleRate(double newSampleRate)
{
    jassert(newSampleRate > 0.0);
    
    sampleRate = newSampleRate;
    updateSamplesPerBeat();
    
    // Recalculate loop length if it was set via BPM
    if (quantizationEnabled.load(std::memory_order_relaxed))
    {
        calculateLoopLengthFromBPM();
    }
}

//==============================================================================
// Private Methods
//==============================================================================

void LoopEngine::updateSamplesPerBeat()
{
    const double currentBpm = bpm.load(std::memory_order_relaxed);
    
    if (currentBpm <= 0.0 || sampleRate <= 0.0)
    {
        samplesPerBeat = 0;
        return;
    }
    
    // Calculate samples per beat
    const double secondsPerBeat = 60.0 / currentBpm;
    samplesPerBeat = static_cast<juce::int64>(secondsPerBeat * sampleRate);
    
    DBG("Samples per beat updated: " + juce::String(samplesPerBeat) + 
        " @ " + juce::String(currentBpm, 1) + " BPM");
}

juce::int64 LoopEngine::quantizeToNearestBeat(juce::int64 samplePosition) const
{
    if (samplesPerBeat <= 0)
        return samplePosition;
    
    const juce::int64 beatIndex = (samplePosition + samplesPerBeat / 2) / samplesPerBeat;
    return beatIndex * samplesPerBeat;
}
