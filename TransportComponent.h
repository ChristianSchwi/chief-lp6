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

    struct SectionHeader { int y; const char* label; };
    juce::Array<SectionHeader> sectionHeaders;

    //==========================================================================
    // Transport
    juce::TextButton   playStopButton    {"Play"};
    juce::TextButton   panicButton       {"PANIC"};
    juce::ToggleButton overdubButton     {"Overdub Mode"};

    // Channel navigation
    juce::TextButton   prevChannelButton {"<"};
    juce::TextButton   nextChannelButton {">"};
    juce::Label        activeChannelLabel{"", "Active: Ch1"};

    // Global MIDI learn
    juce::TextButton   midiLearnButton   {"MIDI"};

    // Loop settings
    juce::Label        bpmLabel   {"", "BPM:"};
    juce::Slider       bpmSlider;
    juce::Label        beatsLabel {"", "Beats:"};
    juce::Slider       beatsSlider;
    juce::ToggleButton latchModeButton{"Latch Mode"};

    //==========================================================================
    // Metronome
    juce::ToggleButton metronomeButton    {"Metronome"};
    juce::ToggleButton metronomeMuteButton{"Mute Click"};
    juce::Label        beatsPerBarLabel   {"", "Bar:"};
    juce::Slider       beatsPerBarSlider;
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
    bool lastHasRecordings { false };

    struct MetroOutEntry { int left; int right; };
    juce::Array<MetroOutEntry> metroOutEntries;

    void timerCallback() override;
    void updateDisplay();
    void updateMetronomeButtonStates();

    void playStopClicked();
    void overdubModeChanged();
    void prevChannelClicked();
    void nextChannelClicked();
    void showGlobalMidiLearnMenu();
    void bpmChanged();
    void beatsChanged();
    void latchModeChanged();
    void metronomeChanged();
    void metronomeMuteChanged();
    void beatsPerBarChanged();
    void metroOutputChanged();
    void autoStartChanged();
    void autoStartThresholdChanged();
    void countInChanged();
    void resetSongClicked();
    void populateMetroOutputBox();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportComponent)
};
