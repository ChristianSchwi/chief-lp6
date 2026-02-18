#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "Channel.h"

/**
 * @file RoutingComponent.h
 * @brief Popup component for configuring input/output routing of a channel
 *
 * Displayed via CallOutBox when the routing button in ChannelStripComponent is clicked.
 * Allows selecting:
 *  - Hardware input channels (Left / Right or Mono)
 *  - Hardware output channels (Left / Right)
 *  - MIDI channel filter (VSTi channels only)
 */
class RoutingComponent : public juce::Component
{
public:
    RoutingComponent(AudioEngine& engine, int channelIndex);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Preferred popup size
    static constexpr int kWidth  = 240;
    static constexpr int kHeight = 220;

private:
    AudioEngine& audioEngine;
    int channelIdx;
    bool isVSTiChannel{false};

    // --- Input section ---
    juce::Label inputLabel;
    juce::Label inputLeftLabel;
    juce::ComboBox inputLeftBox;
    juce::Label inputRightLabel;
    juce::ComboBox inputRightBox;

    // --- Output section ---
    juce::Label outputLabel;
    juce::Label outputLeftLabel;
    juce::ComboBox outputLeftBox;
    juce::Label outputRightLabel;
    juce::ComboBox outputRightBox;

    // --- MIDI section (VSTi only) ---
    juce::Label midiLabel;
    juce::ComboBox midiChannelBox;

    // --- Apply ---
    juce::TextButton applyButton{"Apply"};

    void populateChannelBoxes();
    void loadCurrentRouting();
    void applyRouting();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoutingComponent)
};
