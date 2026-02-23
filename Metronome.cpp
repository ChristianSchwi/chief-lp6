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
    sampleRate.store(newSampleRate, std::memory_order_release);
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

    // For accent detection: samplesPerBar = samplesPerBeat * beatsPerBar
    const int    bpb         = beatsPerBar.load(std::memory_order_relaxed);
    const double samplesPerBar = samplesPerBeat * juce::jmax(1, bpb);

    for (int i = 0; i < numSamples; ++i)
    {
        // Beat-Grenze → neuen Click starten
        if (beatPhaseAccumulator < 1.0)
        {
            // Determine whether this beat is the first beat of a bar (accent)
            const juce::int64 samplePos = globalPlayhead + static_cast<juce::int64>(i);
            const int barBeat = (samplesPerBar > 0.0)
                ? static_cast<int>(std::fmod(static_cast<double>(samplePos), samplesPerBar)
                                   / samplesPerBeat)
                : 0;
            const bool isAccent = (barBeat == 0);

            sinePhase            = 0.0;
            sinePhaseIncrement   = isAccent ? accentSinePhaseIncrement
                                            : regularSinePhaseIncrement;
            currentClickAmplitude = isAccent ? accentAmplitude.load(std::memory_order_relaxed)
                                             : amplitude      .load(std::memory_order_relaxed);
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
                const float out = s * currentClickAmplitude * env;
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

    const double sr = sampleRate.load(std::memory_order_relaxed);
    if (sr <= 0.0 || bpm <= 0.0)
        return;

    samplesPerBeat            = (60.0 / bpm) * sr;
    clickDurationSamples      = juce::jmax(juce::int64(1),
                                static_cast<juce::int64>((ms / 1000.0) * sr));
    regularSinePhaseIncrement = (2.0 * juce::MathConstants<double>::pi * hz) / sr;
    accentSinePhaseIncrement  = (2.0 * juce::MathConstants<double>::pi *
                                  accentFreqHz.load(std::memory_order_relaxed)) / sr;
}

float Metronome::nextSineSample()
{
    const float s = static_cast<float>(std::sin(sinePhase));
    sinePhase += sinePhaseIncrement;
    if (sinePhase >= juce::MathConstants<double>::twoPi)
        sinePhase -= juce::MathConstants<double>::twoPi;
    return s;
}
