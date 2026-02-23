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
    
    const double sr = sampleRate.load(std::memory_order_relaxed);
    if (currentBpm <= 0.0 || beats <= 0 || sr <= 0.0)
    {
        DBG("LoopEngine::calculateLoopLengthFromBPM() - Invalid parameters");
        return;
    }

    // Formula: loopLengthSamples = beats * (60.0 / bpm) * sampleRate
    const double secondsPerBeat = 60.0 / currentBpm;
    const double loopLengthSeconds = beats * secondsPerBeat;
    const juce::int64 lengthInSamples = static_cast<juce::int64>(loopLengthSeconds * sr);
    
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

    sampleRate.store(newSampleRate, std::memory_order_release);
    updateSamplesPerBeat();

    // Do NOT automatically recalculate loop length here.
    // If metronome mode is active, AudioEngine::audioDeviceAboutToStart already
    // calls setMetronomeEnabled(true) → calculateLoopLengthFromBPM() via the
    // metronome path. Doing it here would also overwrite free-mode loop lengths
    // that were captured from a first recording, causing loops to play wrong
    // after an audio device change.
}

//==============================================================================
// Private Methods
//==============================================================================

void LoopEngine::updateSamplesPerBeat()
{
    const double currentBpm = bpm.load(std::memory_order_relaxed);

    const double sr = sampleRate.load(std::memory_order_relaxed);
    if (currentBpm <= 0.0 || sr <= 0.0)
    {
        samplesPerBeat = 0;
        return;
    }

    const double secondsPerBeat = 60.0 / currentBpm;
    samplesPerBeat = static_cast<juce::int64>(secondsPerBeat * sr);

    DBG("Samples per beat updated: " + juce::String(samplesPerBeat) +
        " @ " + juce::String(currentBpm, 1) + " BPM");
}
