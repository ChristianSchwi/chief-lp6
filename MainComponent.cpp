#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
    : transportComponent(audioEngine)
{
    songManager  = std::make_unique<SongManager>();
    showManager  = std::make_unique<ShowManager>();

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

    // --- Info label ---
    infoLabel.setJustificationType(juce::Justification::centredLeft);
    infoLabel.setFont(juce::Font(12.0f));
    addAndMakeVisible(infoLabel);

    // --- Preferences button (⚙) ---
    preferencesButton.setButtonText(juce::CharPointer_UTF8("\xe2\x9a\x99\xef\xb8\x8f Prefs"));
    preferencesButton.setTooltip("Open application preferences");
    preferencesButton.onClick = [this]
    {
        auto* prefs = new PreferencesComponent(
            audioEngine.getMidiLearnManager());

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
}

MainComponent::~MainComponent()
{
    audioEngine.getDeviceManager().removeChangeListener(this);
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
    infoLabel.setText(
        "Audio: " + juce::String(audioEngine.getSampleRate(), 0) + " Hz  |  " +
        juce::String(audioEngine.getBufferSize()) + " samples  |  " +
        juce::String(audioEngine.getNumInputChannels()) + " in / " +
        juce::String(audioEngine.getNumOutputChannels()) + " out",
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
void MainComponent::initializeAudio()
{
    const juce::String error = audioEngine.initialiseAudio(2, 2, 44100.0, 512);

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
