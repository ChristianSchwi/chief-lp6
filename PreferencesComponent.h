#pragma once

#include <JuceHeader.h>
#include "MidiLearnManager.h"

/**
 * @file PreferencesComponent.h
 * @brief Application preferences dialog
 *
 * Opened via the preferences button in MainComponent's info bar.
 */
class PreferencesComponent : public juce::Component
{
public:
    /** @param getAutoRecall  Getter that returns the current "auto-recall last session" flag.
     *  @param setAutoRecall  Setter called when the user toggles the option.
     *  @param getMasterRecordPath  Getter for master recording output path (empty = default).
     *  @param setMasterRecordPath  Setter called when the user changes the path. */
    PreferencesComponent(MidiLearnManager& midiLearnManager,
                         std::function<bool()>    getAutoRecall,
                         std::function<void(bool)> setAutoRecall,
                         std::function<juce::String()>              getMasterRecordPath = nullptr,
                         std::function<void(const juce::String&)>   setMasterRecordPath = nullptr);
    ~PreferencesComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    MidiLearnManager& midiLearnManager;

    std::function<bool()>     autoRecallGetter;
    std::function<void(bool)> autoRecallSetter;
    std::function<juce::String()>            masterRecordPathGetter;
    std::function<void(const juce::String&)> masterRecordPathSetter;

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

    //==========================================================================
    // Section: Paths
    juce::Label      sectionPathsLabel;

    juce::Label      masterRecordPathLabel {"", "Master Recording:"};
    juce::Label      masterRecordPathValue;
    juce::TextButton masterRecordBrowseButton {"..."};
    juce::TextButton masterRecordResetButton  {"Reset"};

    juce::Label      appDataLabel  {"", "App Data:"};
    juce::Label      appDataValue;
    juce::TextButton appDataOpenButton {"Open"};

    std::unique_ptr<juce::FileChooser> fileChooser;

    void updateMidiLearnModeButtons();
    void updateMasterRecordPathLabel();
    void browseForMasterRecordPath();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesComponent)
};
