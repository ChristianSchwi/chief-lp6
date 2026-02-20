#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "ChannelStripComponent.h"
#include "TransportComponent.h"
#include "SongManager.h"
#include "ShowManager.h"

/**
 * @file MainComponent.h
 * @brief Main GUI window
 * 
 * Layout:
 * - Top: Transport controls
 * - Middle: 6 channel strips
 * - Bottom: Song/Show controls
 */

//==============================================================================
class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    // Audio engine access
    AudioEngine& getAudioEngine() { return audioEngine; }
    
private:
    // Audio engine
    AudioEngine audioEngine;
    
    // Managers
    std::unique_ptr<SongManager> songManager;
    std::unique_ptr<ShowManager> showManager;
    
    // GUI Components
    TransportComponent transportComponent;
    std::array<std::unique_ptr<ChannelStripComponent>, 6> channelStrips;
    
    // Menu bar
    juce::TextButton loadSongButton{"Load Song"};
    juce::TextButton saveSongButton{"Save Song"};
    juce::Label infoLabel;
    juce::Image logo;
    
    // File chooser (must be member for async API)
    std::unique_ptr<juce::FileChooser> fileChooser;
    
    void loadSongClicked();
    void saveSongClicked();
    void initializeAudio();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
