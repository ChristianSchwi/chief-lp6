#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "SongManager.h"
#include "ShowManager.h"
#include "Song.h"

/**
 * @file ShowComponent.h
 * @brief Show & Song management panel
 *
 * Layout (horizontal bar):
 *   [Load Show] [Show Name]  |  [◀ Prev] [Song N/M: SongName] [Next ▶]  |  [Load Song] [Save Song]
 *
 * Features:
 * - Load/save show (collection of songs)
 * - Navigate between songs in show with Prev/Next (MIDI-mappable via MidiLearnManager)
 * - Load/save individual song
 * - Shows current song name and position in show
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

    /** Called from MainComponent after audio init to enable full song load. */
    void setAudioReady(bool ready) { audioIsReady = ready; }

private:
    AudioEngine&  audioEngine;
    SongManager&  songManager;
    ShowManager&  showManager;

    bool audioIsReady {false};

    //==========================================================================
    // Show state
    Show         currentShow;
    bool         showLoaded      {false};
    int          currentSongIndex{-1};   // index into currentShow.songs, -1 = none

    //==========================================================================
    // Show controls
    juce::TextButton loadShowButton {"Load Show"};
    juce::TextButton saveShowButton {"Save Show"};
    juce::Label      showNameLabel;

    // Song navigation
    juce::TextButton prevSongButton {"◀"};
    juce::TextButton nextSongButton {"▶"};
    juce::Label      songPositionLabel;  // "Song 2/5: My Song"

    // Individual song controls
    juce::TextButton loadSongButton {"Load Song"};
    juce::TextButton saveSongButton {"Save Song"};

    // File chooser (must outlive dialog)
    std::unique_ptr<juce::FileChooser> fileChooser;

    //==========================================================================
    void timerCallback() override;
    void updateSongPositionLabel();

    void loadShowClicked();
    void saveShowClicked();
    void prevSongClicked();
    void nextSongClicked();
    void loadSongClicked();
    void saveSongClicked();

    bool loadAndApplySong(int showSongIndex);
    void applyCurrentSong();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShowComponent)
};
