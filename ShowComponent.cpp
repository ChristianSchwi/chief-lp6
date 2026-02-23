#include "ShowComponent.h"

//==============================================================================
ShowComponent::ShowComponent(AudioEngine& engine,
                             SongManager& songMgr,
                             ShowManager& showMgr)
    : audioEngine(engine)
    , songManager(songMgr)
    , showManager(showMgr)
{
    //--------------------------------------------------------------------------
    // Show controls
    loadShowButton.onClick = [this] { loadShowClicked(); };
    addAndMakeVisible(loadShowButton);

    saveShowButton.onClick = [this] { saveShowClicked(); };
    saveShowButton.setEnabled(false);  // only enabled when a show is loaded
    addAndMakeVisible(saveShowButton);

    showNameLabel.setText("No Show", juce::dontSendNotification);
    showNameLabel.setFont(juce::Font(13.0f, juce::Font::italic));
    showNameLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(showNameLabel);

    //--------------------------------------------------------------------------
    // Song navigation
    prevSongButton.onClick = [this] { prevSongClicked(); };
    prevSongButton.setEnabled(false);
    addAndMakeVisible(prevSongButton);

    nextSongButton.onClick = [this] { nextSongClicked(); };
    nextSongButton.setEnabled(false);
    addAndMakeVisible(nextSongButton);

    songPositionLabel.setText("No Song", juce::dontSendNotification);
    songPositionLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    songPositionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(songPositionLabel);

    //--------------------------------------------------------------------------
    // Individual song
    loadSongButton.onClick = [this] { loadSongClicked(); };
    addAndMakeVisible(loadSongButton);

    saveSongButton.onClick = [this] { saveSongClicked(); };
    addAndMakeVisible(saveSongButton);

    addToShowButton.onClick = [this] { addToShowClicked(); };
    addToShowButton.setEnabled(false);  // only when a show is loaded
    addAndMakeVisible(addToShowButton);

    // Wire MIDI song-navigation callbacks
    audioEngine.getMidiLearnManager().onNextSong = [this] { nextSongClicked(); };
    audioEngine.getMidiLearnManager().onPrevSong = [this] { prevSongClicked(); };

    updateSongPositionLabel();
    startTimer(500);  // 2 Hz — just for label refresh
}

ShowComponent::~ShowComponent() { stopTimer(); }

//==============================================================================
void ShowComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1E1E1E));
    g.setColour(juce::Colours::darkgrey);
    g.drawRect(getLocalBounds(), 1);

    // Separator between show section and song navigation
    const int sep1 = 270;
    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    g.drawVerticalLine(sep1, 4.0f, static_cast<float>(getHeight() - 4));
}

void ShowComponent::resized()
{
    auto area = getLocalBounds().reduced(4, 2);

    // --- Show section (left) ---
    auto showSection = area.removeFromLeft(260);
    loadShowButton.setBounds(showSection.removeFromLeft(90).reduced(2));
    saveShowButton.setBounds(showSection.removeFromLeft(90).reduced(2));
    showNameLabel .setBounds(showSection.reduced(2));

    area.removeFromLeft(10);  // gap after separator

    // --- Song navigation (centre) ---
    auto navSection = area.removeFromLeft(320);
    prevSongButton    .setBounds(navSection.removeFromLeft(36).reduced(2));
    nextSongButton    .setBounds(navSection.removeFromRight(36).reduced(2));
    songPositionLabel .setBounds(navSection.reduced(2));

    area.removeFromLeft(10);

    // --- Individual song (right) ---
    loadSongButton  .setBounds(area.removeFromLeft(90).reduced(2));
    saveSongButton  .setBounds(area.removeFromLeft(90).reduced(2));
    addToShowButton .setBounds(area.removeFromLeft(70).reduced(2));
}

//==============================================================================
void ShowComponent::timerCallback()
{
    updateSongPositionLabel();
}

void ShowComponent::updateSongPositionLabel()
{
    if (!showLoaded || currentShow.songPaths.isEmpty())
    {
        songPositionLabel.setText("No Song", juce::dontSendNotification);
        prevSongButton.setEnabled(false);
        nextSongButton.setEnabled(false);
        return;
    }

    const int total = currentShow.songPaths.size();

    if (currentSongIndex < 0)
    {
        songPositionLabel.setText("— / " + juce::String(total) + " songs",
                                  juce::dontSendNotification);
    }
    else
    {
        const auto& entry = currentShow.songPaths[currentSongIndex];
        const juce::String name = entry.getFileName();

        songPositionLabel.setText(
            juce::String(currentSongIndex + 1) + "/" + juce::String(total) +
            ": " + name,
            juce::dontSendNotification);
    }

    prevSongButton.setEnabled(showLoaded && total > 1);
    nextSongButton.setEnabled(showLoaded && total > 1);
}

//==============================================================================
// Show handlers
//==============================================================================

void ShowComponent::loadShowClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Show",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "show.json");

    const auto flags = juce::FileBrowserComponent::openMode |
                       juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (!file.existsAsFile()) return;

        Show loaded;
        const auto result = showManager.loadShow(file, loaded);

        if (!result.wasOk())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Load Show",
                "Failed to load show: " + result.getErrorMessage());
            return;
        }

        currentShow      = std::move(loaded);
        showLoaded       = true;
        currentSongIndex = -1;

        showNameLabel.setText(currentShow.showName, juce::dontSendNotification);
        saveShowButton.setEnabled(true);
        addToShowButton.setEnabled(true);
        updateSongPositionLabel();

        // Auto-load first song if available
        if (!currentShow.songPaths.isEmpty())
            loadAndApplySong(0);
    });
}

void ShowComponent::saveShowClicked()
{
    if (!showLoaded) return;

    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Show As",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory));

    const auto flags = juce::FileBrowserComponent::saveMode |
                       juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto dir = chooser.getResult();
        if (!dir.isDirectory()) return;

        auto showFile = dir.getChildFile("show.json");
        const auto result = showManager.saveShow(currentShow, showFile);

        if (!result.wasOk())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Save Show",
                "Failed to save show: " + result.getErrorMessage());
        }
    });
}

//==============================================================================
// Song navigation
//==============================================================================

void ShowComponent::prevSongClicked()
{
    if (!showLoaded || currentShow.songPaths.isEmpty()) return;

    const int total = currentShow.songPaths.size();
    const int next  = (currentSongIndex <= 0) ? total - 1 : currentSongIndex - 1;
    loadAndApplySong(next);
}

void ShowComponent::nextSongClicked()
{
    if (!showLoaded || currentShow.songPaths.isEmpty()) return;

    const int total = currentShow.songPaths.size();
    const int next  = (currentSongIndex >= total - 1) ? 0 : currentSongIndex + 1;
    loadAndApplySong(next);
}

bool ShowComponent::loadAndApplySong(int showSongIndex)
{
    if (!showLoaded || showSongIndex < 0 ||
        showSongIndex >= currentShow.songPaths.size()) return false;

    if (!audioIsReady)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Load Song",
            "Audio not yet initialized.");
        return false;
    }

    auto songDir = currentShow.songPaths[showSongIndex];
    auto songFile = songDir.getChildFile("song.json");

    if (!songFile.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Load Song",
            "song.json not found in: " + songDir.getFullPathName());
        return false;
    }

    Song song;
    auto result = songManager.loadSong(songFile, song);
    if (!result.wasOk())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Load Song",
            "Failed to load: " + result.getErrorMessage());
        return false;
    }

    result = songManager.applySongToEngine(song, audioEngine);
    if (!result.wasOk())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Apply Song",
            "Failed to apply song: " + result.getErrorMessage());
        return false;
    }

    currentSongIndex = showSongIndex;
    updateSongPositionLabel();
    return true;
}

//==============================================================================
// Individual song handlers
//==============================================================================

void ShowComponent::loadSongClicked()
{
    if (!audioIsReady)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Load Song",
            "Audio not yet initialized.");
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Song",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "song.json");

    const auto flags = juce::FileBrowserComponent::openMode |
                       juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (!file.existsAsFile()) return;

        Song song;
        auto result = songManager.loadSong(file, song);

        if (result.wasOk())
            result = songManager.applySongToEngine(song, audioEngine);

        if (!result.wasOk())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Load Song",
                result.getErrorMessage());
        }
    });
}

void ShowComponent::saveSongClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Song",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory));

    const auto flags = juce::FileBrowserComponent::saveMode |
                       juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto dir = chooser.getResult();
        if (!dir.isDirectory()) return;

        Song song;
        song.songName      = dir.getFileName();
        song.songDirectory = dir;

        const auto result = songManager.saveSong(song, audioEngine);

        if (!result.wasOk())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Save Song",
                "Failed to save: " + result.getErrorMessage());
        }
    });
}

void ShowComponent::addToShowClicked()
{
    if (!showLoaded) return;

    fileChooser = std::make_unique<juce::FileChooser>(
        "Select Song Directory to Add to Show",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory));

    const auto flags = juce::FileBrowserComponent::openMode |
                       juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto dir = chooser.getResult();
        if (!dir.isDirectory()) return;

        currentShow.addSong(dir);
        updateSongPositionLabel();
    });
}
