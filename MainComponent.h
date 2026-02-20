#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "ChannelStripComponent.h"
#include "TransportComponent.h"
#include "ShowComponent.h"
#include "SongManager.h"
#include "ShowManager.h"

/**
 * @file MainComponent.h
 * @brief Main GUI window
 *
 * Layout:
 *   Top:    Logo / header
 *   Row 1:  TransportComponent (left panel) + 6 channel strips
 *   Bottom: ShowComponent (show/song management bar)
 */
class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint  (juce::Graphics& g) override;
    void resized() override;

    AudioEngine& getAudioEngine() { return audioEngine; }

private:
    //==========================================================================
    AudioEngine audioEngine;

    std::unique_ptr<SongManager> songManager;
    std::unique_ptr<ShowManager> showManager;

    //==========================================================================
    // GUI
    TransportComponent transportComponent;
    std::array<std::unique_ptr<ChannelStripComponent>, 6> channelStrips;
    std::unique_ptr<ShowComponent> showComponent;

    juce::Label infoLabel;
    juce::Image logo;

    //==========================================================================
    void initializeAudio();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
