#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "ContextMenuControls.h"
#include "LevelMeterComponent.h"

//==============================================================================
/**
 * @file ChannelStripComponent.h
 * @brief Single channel strip UI
 *
 * Single context-aware main button (Spec Abschnitt 5):
 *   Priority: Overdub mode > Empty > Has loop (idle) > Playing > Recording
 *
 * Active Channel Highlight:
 *   When this strip's index == audioEngine.getActiveChannel(),
 *   a coloured border is drawn around the entire strip.
 *   Clicking anywhere on the strip sets it as the active channel.
 */
class ChannelStripComponent : public juce::Component,
                              private juce::Timer
{
public:
    ChannelStripComponent(AudioEngine& engine, int channelIndex);
    ~ChannelStripComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    AudioEngine& audioEngine;
    const int    channelIndex;

    //==========================================================================
    // Main context-aware button (ContextMenuButton so right-click → context menu)
    ContextMenuButton mainButton;

    // Secondary buttons (also ContextMenuButton for consistent right-click behaviour)
    ContextMenuButton clrButton   {"CLR"};
    ContextMenuButton ioButton    {"I/O"};
    ContextMenuButton fxButton    {"FX"};
    ContextMenuButton undoButton  {"UNDO"};

    // Controls
    ContextMenuSlider gainSlider;
    ContextMenuButton muteButton  {"M"};
    ContextMenuButton soloButton  {"S"};
    juce::ComboBox   monitorModeBox;

    // One-shot mode
    juce::TextButton oneShotButton {"1x"};

    // Mute group assignment buttons
    std::array<juce::TextButton, 4> muteGroupButtons;

    // Import audio file
    juce::TextButton openFileButton {"FILE"};
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Level meters
    LevelMeterComponent inputMeter;
    LevelMeterComponent loopMeter;

    // Display
    juce::Label      channelLabel;
    juce::Label      stateLabel;

    int muteGrpHeaderY = 0;

    //==========================================================================
    void timerCallback() override;
    void updateMainButton();

    void mainButtonClicked();
    void clrButtonClicked();
    void undoClicked();
    void muteClicked();
    void soloClicked();
    void gainChanged();

    void monitorModeChanged();
    void openFileClicked();
    void loadAudioFile(const juce::File& file);
    void showMidiContextMenu(MidiControlTarget target);

    bool isActiveChannel()  const { return audioEngine.getActiveChannel() == channelIndex; }
    bool channel_hasLoop()  const;  // helper — avoids repeated null-check

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripComponent)
};
