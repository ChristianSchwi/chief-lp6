#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "ChannelStripComponent.h"
#include "TransportComponent.h"
#include "ShowComponent.h"
#include "SongManager.h"
#include "ShowManager.h"
#include "PreferencesComponent.h"

/**
 * @file MainComponent.h
 * @brief Main GUI window
 *
 * Layout:
 *   Top:    Logo / header
 *   Row 1:  TransportComponent (left panel) + 6 channel strips
 *   Bottom: ShowComponent (show/song management bar)
 */
class MainComponent : public juce::Component,
                      public juce::ChangeListener,
                      public juce::KeyListener,
                      private juce::Timer
{
public:
    MainComponent(std::function<void(const juce::String&)> splashCallback = nullptr);
    ~MainComponent() override;

    void paint  (juce::Graphics& g) override;
    void resized() override;

    AudioEngine& getAudioEngine() { return audioEngine; }

    /** Splash screen status callback — set by Main.cpp before audio init. */
    std::function<void(const juce::String&)> onSplashStatus;

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

    juce::Label      infoLabel;
    juce::TextButton audioSettingsButton  {"Audio Settings"};
    juce::TextButton preferencesButton;   // gear icon — opens PreferencesComponent
    juce::TextButton helpButton           {"? Help"};
    juce::Image      logo;
    juce::Rectangle<int> logoArea;
    juce::Rectangle<int> freeLogoArea;    // empty space in free version for branding
    juce::Rectangle<int> progressBarArea; // 8px bar below channels showing loop progress

    juce::TooltipWindow tooltipWindow {this, 600};  // 600 ms hover delay

    //==========================================================================
    void initializeAudio();
    void updateInfoLabel();
    void timerCallback() override;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // KeyListener — global keyboard shortcuts
    bool keyPressed(const juce::KeyPress& key,
                    juce::Component* originatingComponent) override;
    void triggerChannel(int channelIndex);

    // Preferences persistence
    bool autoRecallLastSession {false};
    juce::String defaultTemplatePath;
    juce::String masterRecordPath;
    void loadPreferences();
    void savePreferences();
    juce::File getPreferencesFile() const;

    // Help dialogs
    void showHelpMenu();
    void showShortcutsDialog();
    void showMidiMappingsDialog();
    void showAboutDialog();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
