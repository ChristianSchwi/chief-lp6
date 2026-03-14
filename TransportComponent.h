#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "SongManager.h"
#include "ContextMenuControls.h"
#include "CustomLookAndFeel.h"

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

    /** Optional override for master recording directory (empty = default). */
    std::function<juce::String()> getMasterRecordPath;

private:
    AudioEngine& audioEngine;

    struct SectionHeader { int y; int height; const char* label; };
    juce::Array<SectionHeader> sectionHeaders;

    //==========================================================================
    // Transport
    ContextMenuButton       playStopButton    {"Play"};
    ContextMenuButton       panicButton       {"PANIC"};
    ContextMenuToggleButton overdubButton     {"Overdub"};

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
    ContextMenuToggleButton latchModeButton{"Latch"};

    //==========================================================================
    // Metronome
    ContextMenuToggleButton metronomeButton{"On/Off"};
    juce::ToggleButton metronomeMuteButton{"Mute"};
    juce::Label        beatsPerBarLabel   {"", "Beats:"};
    juce::Slider       beatsPerBarSlider;
    juce::TextButton   metroIOButton      {"I/O"};
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
    juce::Slider     fixedLenSlider;

    //==========================================================================
    // Loop manipulation
    ContextMenuButton doubleLoopButton{"x2"};

    // Master Recording
    ContextMenuButton masterRecordButton {"Rec"};

    // Reset
    juce::TextButton resetButton{"Reset"};

    //==========================================================================
    // A/B/C Sections
    std::array<ContextMenuButton, 3> sectionButtons;

    //==========================================================================
    // Mute Groups
    std::array<ContextMenuButton, 4> muteGroupToggleButtons;

    //==========================================================================
    FilledBarSliderLookAndFeel filledBarLnF;
    SquareButtonLookAndFeel squareBtnLnF;

    bool lastHasRecordings { false };
    std::vector<juce::int64> tapTimes;

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
    void metroGainChanged();
    void autoStartChanged();
    void autoStartThresholdChanged();
    void countInChanged();
    void fixedLenSliderChanged();
    void resetClicked();
    void masterRecordClicked();
    void masterGainChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportComponent)
};
