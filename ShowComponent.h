#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "SongManager.h"
#include "ShowManager.h"
#include "Song.h"
#include "ContextMenuControls.h"

/**
 * @file ShowComponent.h
 * @brief Show & Song management panel
 *
 * Layout (horizontal bar):
 *   [New] [Load] [Save]  |  [< Prev] [Song N/M: SongName] [Next >]  |  [+ Show] [Show Name]
 *
 * New button context menu:  New Song, New Show
 * Load button context menu: Load Show, Load Song, Load Song Template
 * Save button context menu: Save Show, Save Song, Save Song Template, Save as Default Template
 */
class ShowComponent : public juce::Component,
                      private juce::Timer
{
public:
    ShowComponent(AudioEngine& engine,
                  SongManager& songMgr,
                  ShowManager& showMgr);
    ~ShowComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    /** Called from MainComponent after audio init to enable full song load. */
    void setAudioReady(bool ready) { audioIsReady = ready; }

    /** Set callbacks for default template path persistence. */
    void setDefaultTemplateFunctions(std::function<juce::String()> getter,
                                     std::function<void(const juce::String&)> setter)
    {
        getDefaultTemplatePath = std::move(getter);
        setDefaultTemplatePath = std::move(setter);
    }

private:
    AudioEngine&  audioEngine;
    SongManager&  songManager;
    ShowManager&  showManager;

    bool audioIsReady {false};

    //==========================================================================
    // Show state
    Show         currentShow;
    bool         showLoaded      {false};
    int          currentSongIndex{-1};

    //==========================================================================
    // Remembered last file-browser location
    juce::File lastBrowseLocation;

    juce::File getLastLocation() const
    {
        return lastBrowseLocation.exists() ? lastBrowseLocation
             : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    }

    //==========================================================================
    // Default template path (persisted via MainComponent preferences)
    std::function<juce::String()>              getDefaultTemplatePath;
    std::function<void(const juce::String&)>   setDefaultTemplatePath;

    //==========================================================================
    // Controls
    juce::TextButton newButton  {"New"};
    juce::TextButton loadButton {"Load"};
    juce::TextButton saveButton {"Save"};
    juce::Label      showNameLabel;

    // Song navigation
    ContextMenuButton prevSongButton {"<"};
    ContextMenuButton nextSongButton {">"};
    juce::Label      songPositionLabel;

    juce::TextButton addToShowButton {"+ Show"};

    // File chooser (must outlive dialog)
    std::unique_ptr<juce::FileChooser> fileChooser;

    //==========================================================================
    void timerCallback() override;
    void updateSongPositionLabel();

    // Context menu handlers
    void showNewMenu();
    void showLoadMenu();
    void showSaveMenu();

    void newSongClicked();
    void newShowClicked();
    void loadShowClicked();
    void saveShowClicked();
    void prevSongClicked();
    void nextSongClicked();
    void showMidiContextMenu(MidiControlTarget target);
    void loadSongClicked();
    void saveSongClicked();
    void loadSongTemplateClicked();
    void saveSongTemplateClicked();
    void saveAsDefaultTemplateClicked();
    void addToShowClicked();

    bool loadAndApplySong(int showSongIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShowComponent)
};
