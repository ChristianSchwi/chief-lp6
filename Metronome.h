#pragma once

#include <JuceHeader.h>
#include <atomic>

/**
 * @file Metronome.h
 *
 * Zwei unabhängige Flags:
 *   isEnabled  – Metronom-Modus aktiv (steuert Loop-Längen-Logik + Audio)
 *   isMuted    – Click-Sound stumm, Timing/Modus bleibt aktiv
 *
 *   enabled=true,  muted=false  →  Click hörbar
 *   enabled=true,  muted=true   →  kein Sound, Timing läuft
 *   enabled=false               →  alles inaktiv
 *
 * Thread-safety:
 *   processBlock()  → Audio-Thread (real-time safe)
 *   Alle Setter     → Message-Thread (atomics)
 */
class Metronome
{
public:
    Metronome();
    ~Metronome() = default;

    //==========================================================================
    // Setup (vor Audio-Start oder bei Sample-Rate-Änderung aufrufen)
    void prepareToPlay(double sampleRate);

    /** Phase auf 0 zurücksetzen (z.B. bei Song-Switch oder Playhead-Reset). */
    void reset();

    //==========================================================================
    // Metronom-Modus an/aus (Message-Thread)
    void setEnabled(bool e) { isEnabled.store(e, std::memory_order_release); }
    bool getEnabled() const { return isEnabled.load(std::memory_order_relaxed); }

    // Click-Sound stumm (Message-Thread) — Timing bleibt aktiv
    void setMuted(bool m) { isMuted.store(m, std::memory_order_release); }
    bool getMuted() const { return isMuted.load(std::memory_order_relaxed); }

    //==========================================================================
    // Konfiguration (Message-Thread)
    void   setBPM(double bpm);
    double getBPM() const { return currentBPM.load(std::memory_order_relaxed); }

    void setOutputChannels(int left, int right);
    int  getOutputLeft()  const { return outputLeft .load(std::memory_order_relaxed); }
    int  getOutputRight() const { return outputRight.load(std::memory_order_relaxed); }

    void setClickFrequency(double hz);
    void setClickDurationMs(double ms);
    void setAmplitude(float amp) { amplitude.store(amp, std::memory_order_release); }

    //==========================================================================
    // Audio-Thread — jeden Block aufrufen
    void processBlock(float* const* outputChannelData,
                      int            numOutputChannels,
                      int            numSamples,
                      juce::int64    globalPlayhead,
                      bool           isPlaying);

private:
    //==========================================================================
    // Atomics (geteilt zwischen Threads)
    std::atomic<bool>   isEnabled       {false};
    std::atomic<bool>   isMuted         {false};
    std::atomic<double> currentBPM      {120.0};
    std::atomic<int>    outputLeft      {0};
    std::atomic<int>    outputRight     {1};
    std::atomic<double> clickFreqHz     {1000.0};
    std::atomic<double> clickDurationMs {10.0};
    std::atomic<float>  amplitude       {0.7f};

    //==========================================================================
    // Audio-Thread only (keine Atomics nötig)
    double      sampleRate           {44100.0};
    double      samplesPerBeat       {0.0};
    double      sinePhase            {0.0};
    double      sinePhaseIncrement   {0.0};
    juce::int64 clickSampleCountdown {0};
    juce::int64 clickDurationSamples {0};
    double      beatPhaseAccumulator {0.0};

    void  recalculate();
    float nextSineSample();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Metronome)
};
