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
    /** @param getAutoRecall  Getter that returns the current "auto-recall last session" flag.
     *  @param setAutoRecall  Setter called when the user toggles the option. */
    PreferencesComponent(MidiLearnManager& midiLearnManager,
                         std::function<bool()>    getAutoRecall,
                         std::function<void(bool)> setAutoRecall);
    ~PreferencesComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    MidiLearnManager& midiLearnManager;

    std::function<bool()>     autoRecallGetter;
    std::function<void(bool)> autoRecallSetter;

    //==========================================================================
    // Section: MIDI Learn Mode
    juce::Label  sectionMidiLabel;
    juce::Label  midiLearnDescLabel;
    juce::TextButton perChannelButton    {"Per Channel"};
    juce::TextButton activeChannelButton {"Active Channel"};

    //==========================================================================
    // Section: Session
    juce::Label      sectionSessionLabel;
    juce::ToggleButton autoRecallButton {"Auto-recall last session on startup"};

    void updateMidiLearnModeButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesComponent)
};
