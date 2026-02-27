#pragma once

#include <JuceHeader.h>
#include "MidiLearnManager.h"

/**
 * @file PreferencesComponent.h
 * @brief Application preferences dialog
 *
 * Opened via the preferences button (⚙) in MainComponent's info bar.
 * Add new preference sections below the MIDI Learn Mode section.
 */
class PreferencesComponent : public juce::Component
{
public:
    explicit PreferencesComponent(MidiLearnManager& midiLearnManager);
    ~PreferencesComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    MidiLearnManager& midiLearnManager;

    //==========================================================================
    // Section: MIDI Learn Mode
    juce::Label  sectionMidiLabel;
    juce::Label  midiLearnDescLabel;
    juce::TextButton perChannelButton    {"Per Channel"};
    juce::TextButton activeChannelButton {"Active Channel"};

    void updateMidiLearnModeButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesComponent)
};
