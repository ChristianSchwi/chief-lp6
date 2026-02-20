#include "Metronome.h"

//==============================================================================
Metronome::Metronome()
{
    recalculate();
}

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
void Metronome::setBPM(double bpm)
{
    jassert(bpm > 0.0 && bpm <= 999.0);
    currentBPM.store(bpm, std::memory_order_release);
}

void Metronome::setOutputChannels(int left, int right)
{
    outputLeft .store(left,  std::memory_order_release);
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
void Metronome::processBlock(float* const* outputChannelData,
                             int            numOutputChannels,
                             int            numSamples,
                             juce::int64    globalPlayhead,
                             bool           isPlaying)
{
    // Timing läuft nur wenn enabled + playing
    if (!isEnabled.load(std::memory_order_relaxed) || !isPlaying)
    {
        clickSampleCountdown = 0;
        beatPhaseAccumulator  = 0.0;   // clean state for next start
        sinePhase             = 0.0;
        return;
    }

    if (outputChannelData == nullptr || numSamples <= 0)
        return;

    recalculate();
    if (samplesPerBeat <= 0.0)
        return;

    const bool soundOn = !isMuted.load(std::memory_order_relaxed);
    const int  outL    = outputLeft .load(std::memory_order_relaxed);
    const int  outR    = outputRight.load(std::memory_order_relaxed);

    // Output-Pointer nur auflösen wenn Sound aktiv — verhindert Schreiben wenn muted
    const bool hasL = soundOn && (outL >= 0) && (outL < numOutputChannels)
                               && (outputChannelData[outL] != nullptr);
    const bool hasR = soundOn && (outR >= 0) && (outR < numOutputChannels)
                               && (outputChannelData[outR] != nullptr);

    // Beat-Phase mit globalem Playhead synchronisieren (bleibt im Takt nach Loop-Wrap)
    beatPhaseAccumulator = std::fmod(static_cast<double>(globalPlayhead), samplesPerBeat);

    for (int i = 0; i < numSamples; ++i)
    {
        // Beat-Grenze → neuen Click starten (Phase + Countdown zurücksetzen)
        if (beatPhaseAccumulator < 1.0)
        {
            sinePhase            = 0.0;
            clickSampleCountdown = clickDurationSamples;
        }

        if (clickSampleCountdown > 0)
        {
            // Sine-Phase IMMER weiterführen — auch wenn muted.
            // So entsteht beim Unmute keine Phase-Diskontinuität.
            const float s = nextSineSample();

            if (hasL || hasR)
            {
                const float env = static_cast<float>(clickSampleCountdown) /
                                  static_cast<float>(clickDurationSamples);
                const float out = s * amplitude.load(std::memory_order_relaxed) * env;
                if (hasL) outputChannelData[outL][i] += out;
                if (hasR) outputChannelData[outR][i] += out;
            }

            --clickSampleCountdown;
        }

        beatPhaseAccumulator += 1.0;
        if (beatPhaseAccumulator >= samplesPerBeat)
            beatPhaseAccumulator -= samplesPerBeat;
    }
}

//==============================================================================
void Metronome::recalculate()
{
    const double bpm = currentBPM    .load(std::memory_order_relaxed);
    const double ms  = clickDurationMs.load(std::memory_order_relaxed);
    const double hz  = clickFreqHz   .load(std::memory_order_relaxed);

    if (sampleRate <= 0.0 || bpm <= 0.0)
        return;

    samplesPerBeat       = (60.0 / bpm) * sampleRate;
    clickDurationSamples = juce::jmax(juce::int64(1),
                           static_cast<juce::int64>((ms / 1000.0) * sampleRate));
    sinePhaseIncrement   = (2.0 * juce::MathConstants<double>::pi * hz) / sampleRate;
}

float Metronome::nextSineSample()
{
    const float s = static_cast<float>(std::sin(sinePhase));
    sinePhase += sinePhaseIncrement;
    if (sinePhase >= juce::MathConstants<double>::twoPi)
        sinePhase -= juce::MathConstants<double>::twoPi;
    return s;
}
