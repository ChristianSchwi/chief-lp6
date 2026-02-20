#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"

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
    // Main context-aware button
    juce::TextButton mainButton;

    // Secondary buttons
    juce::TextButton clrButton   {"CLR"};
    juce::TextButton ioButton    {"I/O"};
    juce::TextButton fxButton    {"FX"};

    // Controls
    juce::Slider     gainSlider;
    juce::TextButton muteButton  {"M"};
    juce::TextButton soloButton  {"S"};

    // Display
    juce::Label      channelLabel;
    juce::Label      stateLabel;

    //==========================================================================
    void timerCallback() override;
    void updateMainButton();

    void mainButtonClicked();
    void clrButtonClicked();
    void muteClicked();
    void soloClicked();
    void gainChanged();

    void showContextMenu(const juce::MouseEvent& e);
    void showMidiLearnMenu(juce::Component* target);

    bool isActiveChannel()  const { return audioEngine.getActiveChannel() == channelIndex; }
    bool channel_hasLoop()  const;  // helper — avoids repeated null-check

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripComponent)
};
