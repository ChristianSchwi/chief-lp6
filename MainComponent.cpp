#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
    : transportComponent(audioEngine)
{
    songManager  = std::make_unique<SongManager>();
    showManager  = std::make_unique<ShowManager>();

    loadPreferences();

    // --- Audio init first ---
    initializeAudio();

    // --- Transport ---
    addAndMakeVisible(transportComponent);

    // BUG A FIX: refreshAfterAudioInit() muss nach initializeAudio() aufgerufen werden,
    // damit metroOutputBox die tatsächliche Kanal-Anzahl kennt.
    transportComponent.refreshAfterAudioInit();

    // --- Channel strips ---
    for (int i = 0; i < 6; ++i)
    {
        channelStrips[i] = std::make_unique<ChannelStripComponent>(audioEngine, i);
        addAndMakeVisible(channelStrips[i].get());
    }

    // --- Show component ---
    showComponent = std::make_unique<ShowComponent>(audioEngine,
                                                    *songManager,
                                                    *showManager);
    showComponent->setAudioReady(true);
    addAndMakeVisible(showComponent.get());

    // --- Auto-recall last session if preference is set ---
    if (autoRecallLastSession)
    {
        auto result = songManager->loadCurrentSong(audioEngine);
        if (!result.wasOk())
            DBG("Auto-recall: " + result.getErrorMessage()); // silently ignore on first run
    }

    // --- Info label ---
    infoLabel.setJustificationType(juce::Justification::centredLeft);
    infoLabel.setFont(juce::Font(12.0f));
    addAndMakeVisible(infoLabel);

    // --- Help button ---
    helpButton.setTooltip("Keyboard shortcuts and MIDI mapping reference");
    helpButton.onClick = [this] { showHelpMenu(); };
    addAndMakeVisible(helpButton);

    // --- Preferences button (⚙) ---
    preferencesButton.setButtonText(juce::CharPointer_UTF8("\xe2\x9a\x99\xef\xb8\x8f Prefs"));
    preferencesButton.setTooltip("Open application preferences");
    preferencesButton.onClick = [this]
    {
        auto* prefs = new PreferencesComponent(
            audioEngine.getMidiLearnManager(),
            [this]       { return autoRecallLastSession; },
            [this](bool v) { autoRecallLastSession = v; savePreferences(); });

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(prefs);
        opts.dialogTitle                  = "Preferences";
        opts.dialogBackgroundColour       = juce::Colour(0xFF1E1E1E);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar            = false;
        opts.resizable                    = false;
        opts.launchAsync();
    };
    addAndMakeVisible(preferencesButton);

    // --- Audio settings button ---
    audioSettingsButton.onClick = [this]
    {
        auto* selector = new juce::AudioDeviceSelectorComponent(
            audioEngine.getDeviceManager(),
            0, 64,    // min/max input channels
            0, 64,    // min/max output channels
            true,     // show MIDI input options
            true,     // show MIDI output selector
            false,    // stereo pair buttons
            false);   // hide advanced options
        selector->setSize(500, 420);

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(selector);
        opts.dialogTitle                  = "Audio & MIDI Settings";
        opts.dialogBackgroundColour       = juce::Colours::darkgrey;
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar            = false;
        opts.resizable                    = true;
        opts.launchAsync();
    };
    addAndMakeVisible(audioSettingsButton);

    // --- Logo ---
    logo = juce::ImageCache::getFromMemory(
        BinaryData::chief_lp6_logo_png,
        BinaryData::chief_lp6_logo_pngSize);

    // Show alert when a plugin fails to load
    audioEngine.onPluginLoadError = [](int ch, int slot, const juce::String& msg)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Plugin Load Error",
            "Ch" + juce::String(ch + 1) + " Slot " + juce::String(slot + 1) + ": " + msg);
    };

    // Auto-save settings whenever the audio device configuration changes
    audioEngine.getDeviceManager().addChangeListener(this);

    // Keyboard shortcuts — intercept before child components
    setWantsKeyboardFocus(true);
    addKeyListener(this);

    setSize(1400, 780);
    startTimerHz(20);  // 20 Hz for live status info in the info bar
}

MainComponent::~MainComponent()
{
    stopTimer();
    audioEngine.getDeviceManager().removeChangeListener(this);

    // Stop audio device before reading loop buffers (required for thread safety)
    audioEngine.getDeviceManager().closeAudioDevice();

    // Auto-save current session (channel settings, VSTs, metronome, loops, routings)
    songManager->saveCurrentSong(audioEngine);
    savePreferences();
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    if (logo.isValid() && !logoArea.isEmpty())
        g.drawImage(logo, logoArea.toFloat(),
                    juce::RectanglePlacement::centred |
                    juce::RectanglePlacement::onlyReduceInSize);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Show/song bar at the bottom — fixed height
    showComponent->setBounds(area.removeFromBottom(36));

    // Info row: logo (right corner) + audio settings + preferences + info label
    auto infoRow = area.removeFromBottom(26);
    logoArea = infoRow.removeFromRight(90).reduced(2, 2);
    audioSettingsButton.setBounds(infoRow.removeFromRight(120).reduced(2, 2));
    preferencesButton  .setBounds(infoRow.removeFromRight(90) .reduced(2, 2));
    helpButton         .setBounds(infoRow.removeFromRight(70) .reduced(2, 2));
    infoLabel.setBounds(infoRow.reduced(4, 0));

    // Remaining area: transport (left panel) + 6 channel strips
    const int transportWidth = 220;
    transportComponent.setBounds(area.removeFromLeft(transportWidth).reduced(4));

    const int channelWidth = area.getWidth() / 6;
    for (int i = 0; i < 6; ++i)
        channelStrips[i]->setBounds(area.removeFromLeft(channelWidth).reduced(3));
}

//==============================================================================
void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &audioEngine.getDeviceManager())
    {
        audioEngine.saveAudioSettings();
        updateInfoLabel();
        // Refresh metro output box — channel count may have changed
        transportComponent.refreshAfterAudioInit();
    }
}

void MainComponent::updateInfoLabel()
{
    // Called once on device change; timerCallback() keeps it live at 20 Hz.
    timerCallback();
}

void MainComponent::timerCallback()
{
    auto& le = audioEngine.getLoopEngine();
    const bool   metroActive = audioEngine.getMetronome().getEnabled();
    const juce::int64 loopLen = le.getLoopLength();
    const double cpu          = audioEngine.getCPUUsage();

    const juce::String loopStr = loopLen > 0
        ? juce::String(le.getLoopLengthSeconds(), 2) + "s"
        : "---";

    // Format last MIDI message
    auto& mlm = audioEngine.getMidiLearnManager();
    juce::String midiStr = "---";
    if (mlm.hasReceivedMidi())
    {
        const auto& m = mlm.getLastMidiMessage();
        if (m.isController())
            midiStr = "CC" + juce::String(m.getControllerNumber())
                    + "=" + juce::String(m.getControllerValue())
                    + " ch" + juce::String(m.getChannel());
        else if (m.isNoteOn())
            midiStr = "NoteOn " + juce::MidiMessage::getMidiNoteName(m.getNoteNumber(), true, true, 4)
                    + " v" + juce::String(m.getVelocity())
                    + " ch" + juce::String(m.getChannel());
        else if (m.isNoteOff())
            midiStr = "NoteOff " + juce::MidiMessage::getMidiNoteName(m.getNoteNumber(), true, true, 4)
                    + " ch" + juce::String(m.getChannel());
        else if (m.isProgramChange())
            midiStr = "PC " + juce::String(m.getProgramChangeNumber())
                    + " ch" + juce::String(m.getChannel());
        else if (m.isPitchWheel())
            midiStr = "PW " + juce::String(m.getPitchWheelValue())
                    + " ch" + juce::String(m.getChannel());
        else if (m.isChannelPressure())
            midiStr = "ChAT " + juce::String(m.getChannelPressureValue())
                    + " ch" + juce::String(m.getChannel());
        else
            midiStr = "0x" + juce::String::toHexString(m.getRawData()[0] & 0xF0)
                    + " ch" + juce::String(m.getChannel());
    }

    infoLabel.setText(
        "Audio: " + juce::String(audioEngine.getSampleRate(), 0) + " Hz  |  " +
        juce::String(audioEngine.getBufferSize()) + " samples  |  " +
        juce::String(audioEngine.getNumInputChannels()) + " in / " +
        juce::String(audioEngine.getNumOutputChannels()) + " out  |  " +
        "Mode: " + juce::String(metroActive ? "Metronome" : "Free") + "  |  " +
        "Loop: " + loopStr + "  |  " +
        "Pos: " + juce::String(le.getPlayheadSeconds(), 2) + "s  |  " +
        "CPU: " + juce::String(cpu, 1) + "%  |  " +
        "MIDI: " + midiStr,
        juce::dontSendNotification);
}

//==============================================================================
// Keyboard Shortcuts
//==============================================================================

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    const int code = key.getKeyCode();

    // Space — global play/stop
    if (code == juce::KeyPress::spaceKey)
    {
        audioEngine.setPlaying(!audioEngine.isPlaying());
        return true;
    }

    // O — toggle overdub mode
    if (code == 'o' || code == 'O')
    {
        audioEngine.setOverdubMode(!audioEngine.isInOverdubMode());
        return true;
    }

    // L — toggle latch mode
    if (code == 'l' || code == 'L')
    {
        audioEngine.setLatchMode(!audioEngine.isLatchMode());
        return true;
    }

    // R — trigger active channel (record/play/overdub/stop)
    if (code == 'r' || code == 'R')
    {
        triggerChannel(audioEngine.getActiveChannel());
        return true;
    }

    // 1-6 — set active channel and trigger it
    if (code >= '1' && code <= '6')
    {
        const int ch = code - '1';
        audioEngine.setActiveChannel(ch);
        triggerChannel(ch);
        return true;
    }

    // Left / Right arrow — previous / next channel
    if (code == juce::KeyPress::leftKey)
    {
        audioEngine.prevChannel();
        return true;
    }
    if (code == juce::KeyPress::rightKey)
    {
        audioEngine.nextChannel();
        return true;
    }

    // M — toggle mute on active channel
    if (code == 'm' || code == 'M')
    {
        const int ch = audioEngine.getActiveChannel();
        auto* channel = audioEngine.getChannel(ch);
        if (channel)
        {
            Command cmd;
            cmd.type         = CommandType::SetMute;
            cmd.channelIndex = ch;
            cmd.boolValue    = !channel->isMuted();
            audioEngine.sendCommand(cmd);
        }
        return true;
    }

    // C — clear active channel loop
    if (code == 'c' || code == 'C')
    {
        const int ch = audioEngine.getActiveChannel();
        auto* channel = audioEngine.getChannel(ch);
        if (channel && channel->hasLoop())
        {
            Command cmd;
            cmd.type         = CommandType::ClearChannel;
            cmd.channelIndex = ch;
            audioEngine.sendCommand(cmd);
        }
        return true;
    }

    // U — undo last overdub on active channel
    if (code == 'u' || code == 'U')
    {
        const int ch = audioEngine.getActiveChannel();
        Command cmd;
        cmd.type         = CommandType::UndoOverdub;
        cmd.channelIndex = ch;
        audioEngine.sendCommand(cmd);
        return true;
    }

    return false;
}

void MainComponent::triggerChannel(int channelIndex)
{
    auto* channel = audioEngine.getChannel(channelIndex);
    if (!channel) return;

    // Cancel pending latch action on second press
    if (channel->hasPendingRecord() || channel->hasPendingOverdub() ||
        channel->hasPendingPlay()   || channel->hasPendingStop())
    {
        Command cmd;
        cmd.type         = CommandType::CancelPending;
        cmd.channelIndex = channelIndex;
        audioEngine.sendCommand(cmd);
        return;
    }

    const bool overdubMode = audioEngine.isInOverdubMode();
    const auto state       = channel->getState();
    const bool hasLoop     = channel->hasLoop();

    if (overdubMode && state == ChannelState::Playing)
    {
        Command cmd;
        cmd.type         = CommandType::StartOverdub;
        cmd.channelIndex = channelIndex;
        audioEngine.sendCommand(cmd);
    }
    else if (state == ChannelState::Overdubbing)
        audioEngine.sendCommand(Command::stopRecord(channelIndex));
    else if (state == ChannelState::Recording)
        audioEngine.sendCommand(Command::stopRecord(channelIndex));
    else if (!hasLoop)
        audioEngine.sendCommand(Command::startRecord(channelIndex));
    else if (state == ChannelState::Playing)
        audioEngine.sendCommand(Command::stopPlayback(channelIndex));
    else
        audioEngine.sendCommand(Command::startPlayback(channelIndex));
}

//==============================================================================
// Preferences
//==============================================================================

juce::File MainComponent::getPreferencesFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("chief")
               .getChildFile("preferences.json");
}

void MainComponent::loadPreferences()
{
    const auto file = getPreferencesFile();
    if (!file.existsAsFile()) return;

    const auto json = juce::JSON::parse(file.loadFileAsString());
    if (const auto* obj = json.getDynamicObject())
        autoRecallLastSession = (bool)obj->getProperty("auto_recall_last_session");
}

void MainComponent::savePreferences()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("auto_recall_last_session", autoRecallLastSession);

    const auto file = getPreferencesFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(juce::JSON::toString(juce::var(obj), true));
}

//==============================================================================
// Help Dialogs
//==============================================================================

void MainComponent::showHelpMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Show shortcuts");
    menu.addItem(2, "Show MIDI mappings");
    menu.showMenuAsync({}, [this](int id) {
        if (id == 1) showShortcutsDialog();
        if (id == 2) showMidiMappingsDialog();
    });
}

void MainComponent::showShortcutsDialog()
{
    const juce::String text =
        "Space          Global Play / Stop\n"
        "R              Trigger active channel (record -> stop -> play -> stop ...)\n"
        "1 - 6          Select + trigger channel N\n"
        "O              Toggle Overdub Mode\n"
        "L              Toggle Latch Mode\n"
        "M              Toggle Mute on active channel\n"
        "C              Clear active channel loop\n"
        "U              Undo last overdub on active channel\n"
        "<- / ->        Previous / Next channel";

    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        "Keyboard Shortcuts",
        text);
}

void MainComponent::showMidiMappingsDialog()
{
    const auto& mappings = audioEngine.getMidiLearnManager().getAllMappings();

    if (mappings.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "MIDI Mappings",
            "No MIDI mappings defined.");
        return;
    }

    auto formatMapping = [](const MidiMapping& m) -> juce::String
    {
        juce::String src;
        if (m.ccNumber >= 0)
            src = "CC " + juce::String(m.ccNumber) + " (ch " + juce::String(m.midiChannel) + ")";
        else if (m.noteNumber >= 0)
            src = "Note " + juce::String(m.noteNumber) + " (ch " + juce::String(m.midiChannel) + ")";
        else if (m.programNumber >= 0)
            src = "PC " + juce::String(m.programNumber) + " (ch " + juce::String(m.midiChannel) + ")";
        else if (m.rawStatusNibble >= 0)
            src = "Raw 0x" + juce::String::toHexString(m.rawStatusNibble) + " (ch " + juce::String(m.midiChannel) + ")";

        const juce::String chStr = m.channelIndex >= 0
            ? "Ch " + juce::String(m.channelIndex + 1)
            : "Global";

        return "[" + chStr + "]  " + MidiLearnManager::targetName(m.target) + "  ->  " + src;
    };

    juce::String text;
    for (const auto& m : mappings)
    {
        if (m.isValid())
            text += formatMapping(m) + "\n";
    }

    if (text.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "MIDI Mappings",
            "No MIDI mappings defined.");
        return;
    }

    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        "MIDI Mappings",
        text.trimEnd());
}

//==============================================================================
void MainComponent::initializeAudio()
{
    const juce::String error = audioEngine.initialiseAudio(32, 32, 44100.0, 512);

    if (error.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Audio Error",
            "Failed to initialize audio: " + error);
        infoLabel.setText("Audio Error: " + error, juce::dontSendNotification);
        return;
    }

    // --- Global loop settings ---
    audioEngine.getLoopEngine().setBPM(120.0);
    audioEngine.getLoopEngine().setBeatsPerLoop(4);
    // Loop length starts at 0 in all modes; the first recording sets it
    // (bar-rounded in metronome mode, exact in free mode).

    // Transport starts stopped; user must press Play or hit Record on a channel.

    updateInfoLabel();
}
