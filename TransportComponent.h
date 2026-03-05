#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "ContextMenuControls.h"

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
    void mouseDown(const juce::MouseEvent& e) override;

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
    ContextMenuButton       playStopButton    {"Play"};
    ContextMenuButton       panicButton       {"PANIC"};
    ContextMenuToggleButton overdubButton     {"Overdub Mode"};

    // Master volume
    juce::Label  masterGainLabel {"", "Master:"};
    juce::Slider masterGainSlider;

    // Channel navigation
    ContextMenuButton       prevChannelButton {"<"};
    ContextMenuButton       nextChannelButton {">"};
    juce::Label             activeChannelLabel{"", "Active: Ch1"};

    // Global MIDI learn
    juce::TextButton        midiLearnButton   {"MIDI"};

    // Loop settings
    juce::Label        bpmLabel   {"", "BPM:"};
    juce::TextEditor   bpmEditor;
    ContextMenuButton  tapButton  {"Tap"};
    ContextMenuToggleButton latchModeButton{"Latch Mode"};

    //==========================================================================
    // Metronome
    ContextMenuToggleButton metronomeButton{"On/Off"};
    juce::ToggleButton metronomeMuteButton{"Mute"};
    juce::Label        beatsPerBarLabel   {"", "Beats:"};
    juce::Slider       beatsPerBarSlider;
    juce::Label        metroOutLabel      {"", "Metro Out:"};
    juce::ComboBox     metroOutputBox;
    juce::Label        metroGainLabel     {"", "Volume:"};
    juce::Slider       metroGainSlider;

    //==========================================================================
    // Auto-Start
    ContextMenuToggleButton autoStartButton{"Auto Start"};
    juce::Label        autoStartThreshLabel{"", "Threshold:"};
    juce::Slider       autoStartSlider;

    //==========================================================================
    // Count-In
    juce::Label    countInLabel{"", "Count In:"};
    juce::ComboBox countInBox;

    //==========================================================================
    // Fixed-Length Recording
    juce::Label      fixedLenLabel    {"", "Fix len:"};
    juce::TextEditor fixedLenEditor;
    juce::Label      fixedLenBarsLabel{"", "bars"};
    juce::TextButton fixedLenPlusBtn  {"+"};
    juce::TextButton fixedLenMinusBtn {"-"};

    //==========================================================================
    // Loop manipulation
    ContextMenuButton doubleLoopButton{"Double Loop"};

    // Reset Song
    juce::TextButton resetSongButton{"Reset Song"};

    //==========================================================================
    // Mute Groups
    std::array<ContextMenuButton, 4> muteGroupToggleButtons;

    //==========================================================================
    bool lastHasRecordings { false };
    std::vector<juce::int64> tapTimes;

    struct MetroOutEntry { int left; int right; };
    juce::Array<MetroOutEntry> metroOutEntries;

    void timerCallback() override;
    void updateDisplay();
    void updateMetronomeButtonStates();

    void playStopClicked();
    void overdubModeChanged();
    void prevChannelClicked();
    void nextChannelClicked();
    void showMidiContextMenu(MidiControlTarget target);
    void showMidiButtonMenu();
    void bpmChanged();
    void tapClicked();
    void latchModeChanged();
    void metronomeChanged();
    void metronomeMuteChanged();
    void beatsPerBarChanged();
    void metroOutputChanged();
    void metroGainChanged();
    void autoStartChanged();
    void autoStartThresholdChanged();
    void countInChanged();
    void applyFixedLenEditor();
    void fixedLenStep(int direction);
    void resetSongClicked();
    void populateMetroOutputBox();
    void masterGainChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportComponent)
};
