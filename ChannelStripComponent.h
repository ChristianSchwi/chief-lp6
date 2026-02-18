#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "Channel.h"
#include "RoutingComponent.h"
#include "MidiLearnManager.h"
#include "PluginManagerComponent.h"

/**
 * @file ChannelStripComponent.h
 * @brief GUI component for a single channel
 * 
 * Displays:
 * - Channel type (Audio/VSTi)
 * - Record/Play/Overdub buttons
 * - Gain fader
 * - Mute/Solo buttons
 * - Channel state indicator
 */

//==============================================================================
class ChannelStripComponent : public juce::Component,
                               private juce::Timer
{
public:
    ChannelStripComponent(AudioEngine& engine, int channelIndex);
    ~ChannelStripComponent() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void updateFromChannel();
    
private:
    AudioEngine& audioEngine;
    int channelIdx;
    
    // UI Components
    juce::Label channelLabel;
    juce::Label stateLabel;
    
    juce::TextButton recordButton{"REC"};
    juce::TextButton playButton{"PLAY"};
    juce::TextButton overdubButton{"OVDB"};
    juce::TextButton clearButton{"CLR"};
    juce::TextButton routingButton{"I/O"};
    juce::TextButton fxButton{"FX"};
    
    juce::Slider gainSlider;
    juce::Label gainLabel;
    
    juce::ToggleButton muteButton{"M"};
    juce::ToggleButton soloButton{"S"};
    
    juce::ComboBox monitorModeBox;

    // Plugin strip
    PluginManagerComponent pluginStrip;
    
    // State
    ChannelState currentState{ChannelState::Idle};
    bool hasLoop{false};
    
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void buttonClicked(juce::Button* button);
    void gainChanged();
    void monitorModeChanged();

    // MIDI-Learn Kontextmenü
    void showMidiContextMenu(juce::Component* control, MidiControlTarget target);
    juce::String getMidiAssignmentLabel(MidiControlTarget target) const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripComponent)
};
