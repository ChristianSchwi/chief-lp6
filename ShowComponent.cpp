#include "ShowComponent.h"
#include "AppConfig.h"

//==============================================================================
ShowComponent::ShowComponent(AudioEngine& engine,
                             SongManager& songMgr,
                             ShowManager& showMgr)
    : audioEngine(engine)
    , songManager(songMgr)
    , showManager(showMgr)
{
    //--------------------------------------------------------------------------
    // New button — context menu
    newButton.onClick = [this] { showNewMenu(); };
    addAndMakeVisible(newButton);

    // Load button — context menu
    loadButton.onClick = [this] { showLoadMenu(); };
    addAndMakeVisible(loadButton);

    // Save button — context menu
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
    addToShowButton.setEnabled(false);
    if (!kFreeVersion)
        addAndMakeVisible(addToShowButton);

    // Wire MIDI song-navigation callbacks
    audioEngine.getMidiLearnManager().onNextSong = [this] { nextSongClicked(); };
    audioEngine.getMidiLearnManager().onPrevSong = [this] { prevSongClicked(); };

    updateSongPositionLabel();
    startTimer(500);
}

ShowComponent::~ShowComponent() { stopTimer(); }

//==============================================================================
void ShowComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1E1E1E));
    g.setColour(juce::Colours::darkgrey);
    g.drawRect(getLocalBounds(), 1);

    const int sep1 = 300;
    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    g.drawVerticalLine(sep1, 4.0f, static_cast<float>(getHeight() - 4));
}

void ShowComponent::resized()
{
    auto area = getLocalBounds().reduced(4, 2);

    // --- Left: New / Load / Save + show name ---
    auto showSection = area.removeFromLeft(290);
    newButton .setBounds(showSection.removeFromLeft(55).reduced(2));
    loadButton.setBounds(showSection.removeFromLeft(55).reduced(2));
    saveButton.setBounds(showSection.removeFromLeft(55).reduced(2));
    showNameLabel.setBounds(showSection.reduced(2));

    area.removeFromLeft(10);

    // --- Centre: song navigation ---
    auto navSection = area.removeFromLeft(320);
    prevSongButton   .setBounds(navSection.removeFromLeft(36).reduced(2));
    nextSongButton   .setBounds(navSection.removeFromRight(36).reduced(2));
    songPositionLabel.setBounds(navSection.reduced(2));

    area.removeFromLeft(10);

    // --- Right: + Show ---
    if (!kFreeVersion)
        addToShowButton.setBounds(area.removeFromLeft(70).reduced(2));
}

//==============================================================================
void ShowComponent::timerCallback()
{
    updateSongPositionLabel();

    // Enable +Show only when a show is loaded
    addToShowButton.setEnabled(showLoaded);
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
        songPositionLabel.setText(juce::String::charToString(0x2014) + " / " +
                                  juce::String(total) + " songs",
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

void ShowComponent::showNewMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "New Song");
    menu.addItem(2, "New Show", !kFreeVersion);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&newButton),
                       [this](int id)
    {
        if (id == 1) newSongClicked();
        else if (id == 2) newShowClicked();
    });
}

void ShowComponent::showLoadMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Load Show", !kFreeVersion);
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
    menu.addItem(1, "Save Show", showLoaded && !kFreeVersion);
    menu.addItem(2, "Save Song");
    menu.addItem(3, "Save Song Template");
    menu.addSeparator();
    menu.addItem(4, "Save as Default Template");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&saveButton),
                       [this](int id)
    {
        if (id == 1) saveShowClicked();
        else if (id == 2) saveSongClicked();
        else if (id == 3) saveSongTemplateClicked();
        else if (id == 4) saveAsDefaultTemplateClicked();
    });
}

//==============================================================================
// New handlers
//==============================================================================

void ShowComponent::newSongClicked()
{
    if (!audioIsReady) return;

    // Reset everything first
    audioEngine.setPlaying(false);
    audioEngine.resetSong();

    // Apply default template if one is configured
    if (getDefaultTemplatePath)
    {
        const auto path = getDefaultTemplatePath();
        if (path.isNotEmpty())
        {
            auto tmplFile = juce::File(path);
            if (tmplFile.existsAsFile())
            {
                Song song;
                auto result = songManager.loadSong(tmplFile, song);
                if (result.wasOk())
                    songManager.applySongTemplateToEngine(song, audioEngine);
            }
        }
    }
}

void ShowComponent::newShowClicked()
{
    currentShow = Show();
    showLoaded = true;
    currentSongIndex = -1;
    showNameLabel.setText("New Show", juce::dontSendNotification);
    addToShowButton.setEnabled(true);
    updateSongPositionLabel();
}

//==============================================================================
// Show handlers
//==============================================================================

void ShowComponent::loadShowClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Show", getLastLocation(), "show.json");

    const auto flags = juce::FileBrowserComponent::openMode |
                       juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (!file.existsAsFile()) return;

        lastBrowseLocation = file.getParentDirectory();

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

        if (!currentShow.songPaths.isEmpty())
            loadAndApplySong(0);
    });
}

void ShowComponent::saveShowClicked()
{
    if (!showLoaded) return;

    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Show As", getLastLocation());

    const auto flags = juce::FileBrowserComponent::saveMode |
                       juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto dir = chooser.getResult();
        if (dir.getFullPathName().isEmpty()) return;
        dir.createDirectory();
        if (!dir.isDirectory()) return;

        lastBrowseLocation = dir;

        auto showFile = dir.getChildFile("show.json");
        currentShow.showFile = showFile;
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
    if (!e.mods.isRightButtonDown()) return;

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
        if (id == 1) mlm.startLearning(-1, target);
        else if (id == 2) mlm.removeMapping(-1, target);
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

    lastBrowseLocation = songDir;
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
        "Load Song", getLastLocation(), "song.json");

    const auto fcFlags = juce::FileBrowserComponent::openMode |
                         juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(fcFlags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (!file.existsAsFile()) return;

        lastBrowseLocation = file.getParentDirectory();

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
        "Save Song", getLastLocation().getChildFile("MySong"), "");

    const auto fcFlags = juce::FileBrowserComponent::saveMode;

    fileChooser->launchAsync(fcFlags, [this](const juce::FileChooser& chooser)
    {
        const auto result = chooser.getResult();
        if (result.getFullPathName().isEmpty()) return;

        const juce::String songName = result.getFileNameWithoutExtension();
        if (songName.isEmpty()) return;

        auto dir = result.getParentDirectory().getChildFile(songName);
        dir.createDirectory();
        if (!dir.isDirectory()) return;

        lastBrowseLocation = dir.getParentDirectory();

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
        "Load Song Template", getLastLocation(), "*.tmpl");

    const auto fcFlags = juce::FileBrowserComponent::openMode |
                         juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(fcFlags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (!file.existsAsFile()) return;

        lastBrowseLocation = file.getParentDirectory();

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
        "Save Song Template", getLastLocation().getChildFile("MyTemplate.tmpl"), "*.tmpl");

    const auto fcFlags = juce::FileBrowserComponent::saveMode;

    fileChooser->launchAsync(fcFlags, [this](const juce::FileChooser& chooser)
    {
        const auto result = chooser.getResult();
        if (result.getFullPathName().isEmpty()) return;

        lastBrowseLocation = result.getParentDirectory();

        // Ensure .tmpl extension
        auto tmplFile = result.hasFileExtension(".tmpl")
            ? result
            : result.withFileExtension(".tmpl");

        Song song;
        song.songName      = tmplFile.getFileNameWithoutExtension();
        song.songDirectory  = tmplFile.getParentDirectory();

        const auto saveResult = songManager.saveSongTemplate(song, audioEngine);

        if (!saveResult.wasOk())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Save Template",
                "Failed to save: " + saveResult.getErrorMessage());
        }
    });
}

void ShowComponent::saveAsDefaultTemplateClicked()
{
    // Save to a fixed location in the app data directory
    auto tmplFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile("chief")
                        .getChildFile("default_template.tmpl");
    tmplFile.getParentDirectory().createDirectory();

    Song song;
    song.songName      = "default_template";
    song.songDirectory  = tmplFile.getParentDirectory();

    const auto result = songManager.saveSongTemplate(song, audioEngine);

    if (result.wasOk())
    {
        if (setDefaultTemplatePath)
            setDefaultTemplatePath(tmplFile.getFullPathName());
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Save Default Template",
            "Failed to save: " + result.getErrorMessage());
    }
}

void ShowComponent::addToShowClicked()
{
    if (!showLoaded) return;

    fileChooser = std::make_unique<juce::FileChooser>(
        "Select Song Directory to Add to Show", getLastLocation());

    const auto fcFlags = juce::FileBrowserComponent::openMode |
                         juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync(fcFlags, [this](const juce::FileChooser& chooser)
    {
        auto dir = chooser.getResult();
        if (!dir.isDirectory()) return;

        lastBrowseLocation = dir.getParentDirectory();
        currentShow.addSong(dir);
        updateSongPositionLabel();
    });
}
