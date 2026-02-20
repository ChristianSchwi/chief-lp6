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
 * Main button behaviour (Spec Abschnitt 5):
 *   Loop leer              → press starts Recording
 *   Loop vorhanden, idle   → press starts Playback
 *   Loop vorhanden, playing→ press stops Playback
 *   Global overdub active  → press toggles Overdub on this channel
 *
 * Secondary buttons: CLR, I/O, FX
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
    int          channelIdx;

    //==========================================================================
    // UI Components

    // Channel number + state
    juce::Label channelLabel;
    juce::Label stateLabel;

    // ---- Main button (context-sensitive) ----
    juce::TextButton mainButton{"REC"};

    // ---- Secondary buttons ----
    juce::TextButton clearButton  {"CLR"};
    juce::TextButton routingButton{"I/O"};
    juce::TextButton fxButton     {"FX"};

    // ---- Gain ----
    juce::Label  gainLabel;
    juce::Slider gainSlider;

    // ---- Mute / Solo ----
    juce::ToggleButton muteButton{"M"};
    juce::ToggleButton soloButton{"S"};

    // ---- Monitor mode ----
    juce::ComboBox monitorModeBox;

    // ---- Plugin strip (embedded) ----
    PluginManagerComponent pluginStrip;

    //==========================================================================
    // Cached state (updated by timer on message thread)
    ChannelState currentState {ChannelState::Idle};
    bool         hasLoop      {false};

    //==========================================================================
    // Timer
    void timerCallback() override;

    //==========================================================================
    // Main button logic
    void mainButtonClicked();
    void updateMainButton();   // Refreshes label + colour to reflect current context

    //==========================================================================
    // Other handlers
    void clearClicked();
    void gainChanged();
    void monitorModeChanged();
    void muteClicked();
    void soloClicked();

    //==========================================================================
    // MIDI-Learn context menu
    void mouseDown(const juce::MouseEvent& e) override;
    void showMidiContextMenu(juce::Component* control, MidiControlTarget target);
    juce::String getMidiAssignmentLabel(MidiControlTarget target) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripComponent)
};
