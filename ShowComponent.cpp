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
    // Load button — opens context menu
    loadButton.onClick = [this] { showLoadMenu(); };
    addAndMakeVisible(loadButton);

    // Save button — opens context menu
    saveButton.onClick = [this] { showSaveMenu(); };
    addAndMakeVisible(saveButton);

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
    // Add to show
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
    loadButton.setBounds(showSection.removeFromLeft(70).reduced(2));
    saveButton.setBounds(showSection.removeFromLeft(70).reduced(2));
    showNameLabel .setBounds(showSection.reduced(2));

    area.removeFromLeft(10);  // gap after separator

    // --- Song navigation (centre) ---
    auto navSection = area.removeFromLeft(320);
    prevSongButton    .setBounds(navSection.removeFromLeft(36).reduced(2));
    nextSongButton    .setBounds(navSection.removeFromRight(36).reduced(2));
    songPositionLabel .setBounds(navSection.reduced(2));

    area.removeFromLeft(10);

    // --- Individual song (right) ---
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

    prevSongButton.setEnabled(showLoaded && total >= 1);
    nextSongButton.setEnabled(showLoaded && total >= 1);
}

//==============================================================================
// Context menus
//==============================================================================

void ShowComponent::showLoadMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Load Show");
    menu.addItem(2, "Load Song");
    menu.addItem(3, "Load Song Template");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadButton),
                       [this](int id)
    {
        if (id == 1) loadShowClicked();
        else if (id == 2) loadSongClicked();
        else if (id == 3) loadSongTemplateClicked();
    });
}

void ShowComponent::showSaveMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Save Show", showLoaded);
    menu.addItem(2, "Save Song");
    menu.addItem(3, "Save Song Template");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&saveButton),
                       [this](int id)
    {
        if (id == 1) saveShowClicked();
        else if (id == 2) saveSongClicked();
        else if (id == 3) saveSongTemplateClicked();
    });
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
        if (dir.getFullPathName().isEmpty()) return;   // user cancelled
        dir.createDirectory();                         // create if not yet existing
        if (!dir.isDirectory()) return;

        auto showFile = dir.getChildFile("show.json");
        currentShow.showFile = showFile;  // update so relative paths are computed from new location
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

void ShowComponent::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isRightButtonDown())
        return;

    auto pos = e.getPosition();
    auto hit = [&](juce::Component& c) { return c.getBounds().contains(pos); };

    if      (hit(prevSongButton)) showMidiContextMenu(MidiControlTarget::PrevSong);
    else if (hit(nextSongButton)) showMidiContextMenu(MidiControlTarget::NextSong);
}

void ShowComponent::showMidiContextMenu(MidiControlTarget target)
{
    auto& mlm = audioEngine.getMidiLearnManager();
    const bool hasMapping = mlm.getMapping(-1, target).isValid();
    const auto name = MidiLearnManager::targetName(target);

    juce::PopupMenu menu;
    menu.addItem(1, "MIDI Learn: " + name);
    menu.addItem(2, "Remove MIDI: " + name, hasMapping);

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, target](int id)
    {
        auto& mlm = audioEngine.getMidiLearnManager();
        if (id == 1)
            mlm.startLearning(-1, target);
        else if (id == 2)
            mlm.removeMapping(-1, target);
    });
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
    // Open a save dialog: user types the song name (e.g. "MySong").
    // A subfolder with that name is created inside the chosen parent directory.
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Song – Enter Song Name",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("MySong"),
        "");

    const auto flags = juce::FileBrowserComponent::saveMode;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        const auto result = chooser.getResult();
        if (result.getFullPathName().isEmpty()) return;   // user cancelled

        const juce::String songName = result.getFileNameWithoutExtension();
        if (songName.isEmpty()) return;

        // Create a subfolder named after the song inside the chosen parent dir
        auto dir = result.getParentDirectory().getChildFile(songName);
        dir.createDirectory();
        if (!dir.isDirectory()) return;

        Song song;
        song.songName      = songName;
        song.songDirectory = dir;

        const auto saveResult = songManager.saveSong(song, audioEngine);

        if (!saveResult.wasOk())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Save Song",
                "Failed to save: " + saveResult.getErrorMessage());
        }
    });
}

//==============================================================================
// Song Template handlers
//==============================================================================

void ShowComponent::loadSongTemplateClicked()
{
    if (!audioIsReady)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Load Template",
            "Audio not yet initialized.");
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Song Template",
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
            result = songManager.applySongTemplateToEngine(song, audioEngine);

        if (!result.wasOk())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Load Template",
                result.getErrorMessage());
        }
    });
}

void ShowComponent::saveSongTemplateClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Song Template – Enter Name",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("MyTemplate"),
        "");

    const auto flags = juce::FileBrowserComponent::saveMode;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        const auto result = chooser.getResult();
        if (result.getFullPathName().isEmpty()) return;

        const juce::String templateName = result.getFileNameWithoutExtension();
        if (templateName.isEmpty()) return;

        auto dir = result.getParentDirectory().getChildFile(templateName);
        dir.createDirectory();
        if (!dir.isDirectory()) return;

        Song song;
        song.songName      = templateName;
        song.songDirectory = dir;

        const auto saveResult = songManager.saveSongTemplate(song, audioEngine);

        if (!saveResult.wasOk())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Save Template",
                "Failed to save: " + saveResult.getErrorMessage());
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
