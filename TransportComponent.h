#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"

/**
 * @file TransportComponent.h
 *
 * Metronom-Logik:
 *   metronomeButton      – AN/AUS, gesperrt wenn Aufnahmen vorhanden
 *   metronomeMuteButton  – Sound stumm, Timing bleibt (wie Channel-Mute)
 *   metroOutputBox       – Ausgangskanal-Paar
 *
 * Reset Song:
 *   resetSongButton      – AlertWindow → alle Channels clearen + Loop zurücksetzen
 *
 * FIX: refreshAfterAudioInit() muss nach initialiseAudio() aufgerufen werden,
 *      damit metroOutputBox die tatsächliche Kanal-Anzahl kennt.
 */
class TransportComponent : public juce::Component,
                           private juce::Timer
{
public:
    explicit TransportComponent(AudioEngine& engine);
    ~TransportComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Nach AudioEngine::initialiseAudio() aufrufen.
     * Aktualisiert metroOutputBox mit der tatsächlichen Ausgangskanal-Anzahl.
     * Muss vom MainComponent nach erfolgreicher Audio-Initialisierung aufgerufen werden.
     */
    void refreshAfterAudioInit();

private:
    AudioEngine& audioEngine;

    //==========================================================================
    // Transport
    juce::TextButton   playStopButton{"Play"};
    juce::ToggleButton overdubButton {"Overdub Mode"};

    // Loop settings
    juce::Label        bpmLabel   {"", "BPM:"};
    juce::Slider       bpmSlider;
    juce::Label        beatsLabel {"", "Beats:"};
    juce::Slider       beatsSlider;
    juce::ToggleButton quantizeButton{"Quantize"};

    //==========================================================================
    // Metronome
    juce::ToggleButton metronomeButton    {"Metronome"};
    juce::ToggleButton metronomeMuteButton{"Mute Click"};
    juce::Label        metroOutLabel      {"", "Metro Out:"};
    juce::ComboBox     metroOutputBox;

    //==========================================================================
    // Auto-Start
    juce::ToggleButton autoStartButton     {"Auto Start"};
    juce::Label        autoStartThreshLabel{"", "Threshold:"};
    juce::Slider       autoStartSlider;

    //==========================================================================
    // Count-In
    juce::Label    countInLabel{"", "Count In:"};
    juce::ComboBox countInBox;

    //==========================================================================
    // Reset Song
    juce::TextButton resetSongButton{"Reset Song"};

    //==========================================================================
    // Display
    juce::Label modeLabel      {"", "Mode: Free"};
    juce::Label loopLengthLabel{"", "Loop: ---"};
    juce::Label playheadLabel  {"", "Pos:  0.00s"};
    juce::Label cpuLabel       {"", "CPU: 0%"};

    //==========================================================================
    void timerCallback() override;
    void updateDisplay();
    void updateMetronomeButtonStates();

    void playStopClicked();
    void overdubModeChanged();
    void bpmChanged();
    void beatsChanged();
    void quantizeChanged();
    void metronomeChanged();
    void metronomeMuteChanged();
    void metroOutputChanged();
    void autoStartChanged();
    void autoStartThresholdChanged();
    void countInChanged();
    void resetSongClicked();
    void populateMetroOutputBox();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportComponent)
};
