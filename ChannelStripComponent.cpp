#include "ChannelStripComponent.h"
#include "RoutingComponent.h"
#include "PluginManagerComponent.h"

//==============================================================================
ChannelStripComponent::ChannelStripComponent(AudioEngine& engine, int index)
    : audioEngine(engine), channelIndex(index)
{
    //--------------------------------------------------------------------------
    // Channel label — double-click to rename
    channelLabel.setText(audioEngine.getChannelName(index), juce::dontSendNotification);
    channelLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    channelLabel.setJustificationType(juce::Justification::centred);
    channelLabel.setEditable(false, true, false);   // double-click to edit
    channelLabel.setInterceptsMouseClicks(true, false);
    channelLabel.addMouseListener(this, false);     // propagate clicks to strip
    channelLabel.onTextChange = [this]
    {
        audioEngine.setChannelName(channelIndex, channelLabel.getText());
    };
    addAndMakeVisible(channelLabel);

    stateLabel.setText("Idle", juce::dontSendNotification);
    stateLabel.setFont(juce::Font(11.0f));
    stateLabel.setJustificationType(juce::Justification::centred);
    stateLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(stateLabel);

    //--------------------------------------------------------------------------
    // Main button
    mainButton.setClickingTogglesState(false);
    mainButton.onClick = [this] { mainButtonClicked(); };
    addAndMakeVisible(mainButton);

    //--------------------------------------------------------------------------
    // Secondary buttons
    clrButton.onClick = [this] { clrButtonClicked(); };
    clrButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    clrButton.setTooltip("Clear: erase this channel's recorded loop. [C] clears the active channel.");
    addAndMakeVisible(clrButton);

    ioButton.onClick = [this]
    {
        auto* channel = audioEngine.getChannel(channelIndex);
        if (!channel) return;
        auto* dlg = new RoutingComponent(audioEngine, channelIndex);
        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(dlg);
        opts.dialogTitle           = "Routing Ch " + juce::String(channelIndex + 1);
        opts.dialogBackgroundColour= juce::Colours::darkgrey;
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar     = false;
        opts.resizable             = false;
        opts.launchAsync();
    };
    ioButton.setTooltip(
        "Routing: configure input source and output bus for this channel.");
    addAndMakeVisible(ioButton);

    fxButton.onClick = [this]
    {
        auto* channel = audioEngine.getChannel(channelIndex);
        if (!channel) return;
        auto* dlg = new PluginManagerComponent(audioEngine, channelIndex);
        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(dlg);
        opts.dialogTitle           = "FX Ch " + juce::String(channelIndex + 1);
        opts.dialogBackgroundColour= juce::Colours::darkgrey;
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar     = false;
        opts.resizable             = false;
        opts.launchAsync();
    };
    fxButton.setTooltip(
        "FX: add, remove, or bypass VST/AU plugins in this channel's insert chain.");
    addAndMakeVisible(fxButton);

    undoButton.onClick = [this] { undoClicked(); };
    undoButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    undoButton.setTooltip("Undo last overdub layer");
    addAndMakeVisible(undoButton);

    //--------------------------------------------------------------------------
    // Monitor mode
    // Order matches user request: Always On / While Recording / While Active / Always Off
    monitorModeBox.addItem("Always On",       1);   // MonitorMode::AlwaysOn
    monitorModeBox.addItem("While Recording", 2);   // MonitorMode::WhileRecording
    monitorModeBox.addItem("While Active",    3);   // MonitorMode::WhenTrackActive (default)
    monitorModeBox.addItem("Always Off",      4);   // MonitorMode::Off
    monitorModeBox.setSelectedId(3, juce::dontSendNotification);
    monitorModeBox.setTooltip(
        "Monitor - when live input is passed through to output:\n"
        "  Always On       - input always audible\n"
        "  While Recording - input audible during REC / OVERDUB only\n"
        "  While Active    - input audible only when this is the active channel\n"
        "  Always Off      - input never passed through (tape-style)");
    monitorModeBox.onChange = [this] { monitorModeChanged(); };
    addAndMakeVisible(monitorModeBox);

    //--------------------------------------------------------------------------
    // Gain
    gainSlider.setSliderStyle(juce::Slider::LinearVertical);
    gainSlider.setRange(-60.0, 12.0, 0.1);
    gainSlider.setValue(0.0, juce::dontSendNotification);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    gainSlider.onValueChange = [this] { gainChanged(); };
    gainSlider.addMouseListener(this, false);
    addAndMakeVisible(gainSlider);

    //--------------------------------------------------------------------------
    // Mute / Solo
    muteButton.setClickingTogglesState(true);
    muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
    muteButton.setTooltip(
        "Mute: silence this channel's output. Recording continues unaffected.");
    muteButton.onClick = [this] { muteClicked(); };
    addAndMakeVisible(muteButton);

    soloButton.setClickingTogglesState(true);
    soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow);
    soloButton.setTooltip(
        "Solo: isolate this channel - all other non-soloed channels are silenced.");
    soloButton.onClick = [this] { soloClicked(); };
    addAndMakeVisible(soloButton);

    // Mute group assignment buttons
    for (int g = 0; g < 4; ++g)
    {
        muteGroupButtons[g].setButtonText(juce::String(g + 1));
        muteGroupButtons[g].setClickingTogglesState(false);
        muteGroupButtons[g].setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
        muteGroupButtons[g].setTooltip("Assign to mute group " + juce::String(g + 1));
        muteGroupButtons[g].onClick = [this, g]
        {
            const int current = audioEngine.getChannelMuteGroup(channelIndex);
            audioEngine.setChannelMuteGroup(channelIndex, current == (g + 1) ? 0 : (g + 1));
        };
        addAndMakeVisible(muteGroupButtons[g]);
    }

    // One-shot button
    oneShotButton.setClickingTogglesState(true);
    oneShotButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
    oneShotButton.setTooltip("One-shot: channel stops automatically after one full loop playback.");
    oneShotButton.onClick = [this]
    {
        auto* channel = audioEngine.getChannel(channelIndex);
        if (channel) channel->setOneShot(oneShotButton.getToggleState());
    };
    addAndMakeVisible(oneShotButton);

    // Open file button
    openFileButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    openFileButton.setTooltip("Import an audio file (WAV/AIFF/FLAC/MP3) into this channel's loop.");
    openFileButton.onClick = [this] { openFileClicked(); };
    addAndMakeVisible(openFileButton);

    addAndMakeVisible(inputMeter);
    addAndMakeVisible(loopMeter);

    updateMainButton();
    startTimer(100);  // 10 Hz
}

ChannelStripComponent::~ChannelStripComponent()
{
    stopTimer();
}

//==============================================================================
void ChannelStripComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF2A2A2A));

    // Active channel: bright coloured border
    if (isActiveChannel())
    {
        g.setColour(juce::Colours::cyan);
        g.drawRect(getLocalBounds(), 3);
    }
    else
    {
        g.setColour(juce::Colours::darkgrey);
        g.drawRect(getLocalBounds(), 1);
    }

    // Mute group section header
    if (muteGrpHeaderY > 0)
    {
        const int margin = 6;
        const int headerH = 14;
        const int ruleY = muteGrpHeaderY + headerH / 2;
        const auto ruleColour = juce::Colour(0xFF555555);
        const auto bgColour = juce::Colour(0xFF2A2A2A);
        const char* label = "MUTE GRP";

        g.setFont(juce::Font(9.5f, juce::Font::bold));
        g.setColour(ruleColour);
        g.drawHorizontalLine(ruleY, (float)margin, (float)(getWidth() - margin));

        const int textW = g.getCurrentFont().getStringWidth(label) + 8;
        const juce::Rectangle<int> badge(margin + 4, muteGrpHeaderY, textW, headerH);
        g.setColour(bgColour);
        g.fillRect(badge);
        g.setColour(ruleColour);
        g.drawText(label, badge, juce::Justification::centredLeft, false);
    }
}

void ChannelStripComponent::resized()
{
    auto area = getLocalBounds().reduced(6);

    channelLabel.setBounds(area.removeFromTop(20));
    stateLabel  .setBounds(area.removeFromTop(16));
    area.removeFromTop(4);

    mainButton.setBounds(area.removeFromTop(36));
    area.removeFromTop(4);

    // Secondary buttons row
    auto btnRow = area.removeFromTop(26);
    const int btnW = btnRow.getWidth() / 4;
    clrButton .setBounds(btnRow.removeFromLeft(btnW).reduced(1));
    ioButton  .setBounds(btnRow.removeFromLeft(btnW).reduced(1));
    fxButton  .setBounds(btnRow.removeFromLeft(btnW).reduced(1));
    undoButton.setBounds(btnRow                     .reduced(1));
    area.removeFromTop(4);

    // Mute / Solo
    auto msRow = area.removeFromTop(26);
    muteButton.setBounds(msRow.removeFromLeft(msRow.getWidth() / 2).reduced(2));
    soloButton.setBounds(msRow.reduced(2));
    area.removeFromTop(2);

    // Monitor mode
    monitorModeBox.setBounds(area.removeFromTop(22));
    area.removeFromTop(2);

    // Reserve bottom for FILE + 1x + mute group section
    openFileButton.setBounds(area.removeFromBottom(26).reduced(1));
    area.removeFromBottom(2);
    oneShotButton.setBounds(area.removeFromBottom(26).reduced(1));
    area.removeFromBottom(2);

    // Reserve bottom for mute group section: 14px header + 4px gap + 20px buttons = 38px
    auto muteGrpArea = area.removeFromBottom(38);
    muteGrpHeaderY = muteGrpArea.getY();
    muteGrpArea.removeFromTop(18); // 14px header + 4px gap
    const int mgW = muteGrpArea.getWidth() / 4;
    for (int g = 0; g < 4; ++g)
        muteGroupButtons[g].setBounds(muteGrpArea.removeFromLeft(mgW).reduced(1));

    // Gain slider + meters fill remaining space
    auto meterArea = area.removeFromRight(38); // 16px + 2px + 16px + 2px + 2px margin
    meterArea.removeFromRight(2);
    loopMeter .setBounds(meterArea.removeFromRight(16));
    meterArea.removeFromRight(2);
    inputMeter.setBounds(meterArea.removeFromRight(16));
    gainSlider.setBounds(area);
}

//==============================================================================
// Clicking anywhere on the strip activates this channel
void ChannelStripComponent::mouseDown(const juce::MouseEvent& e)
{
    audioEngine.setActiveChannel(channelIndex);
    repaint();

    if (!e.mods.isRightButtonDown())
        return;

    auto pos = e.getPosition();
    auto hit = [&](juce::Component& c) { return c.getBounds().contains(pos); };

    if      (hit(mainButton))  showMidiContextMenu(MidiControlTarget::MainButton);
    else if (hit(clrButton))   showMidiContextMenu(MidiControlTarget::Clear);
    else if (hit(muteButton))  showMidiContextMenu(MidiControlTarget::Mute);
    else if (hit(soloButton))  showMidiContextMenu(MidiControlTarget::Solo);
    else if (hit(gainSlider))  showMidiContextMenu(MidiControlTarget::Gain);
}

//==============================================================================
// Timer
//==============================================================================

void ChannelStripComponent::timerCallback()
{
    updateMainButton();

    // Sync channel name label (skip when the user is actively editing it)
    if (!channelLabel.isBeingEdited())
    {
        const auto name = audioEngine.getChannelName(channelIndex);
        if (channelLabel.getText() != name)
            channelLabel.setText(name, juce::dontSendNotification);
    }

    auto* channel = audioEngine.getChannel(channelIndex);
    if (!channel) return;

    clrButton.setEnabled(channel_hasLoop());
    undoButton.setEnabled(channel->canUndoOverdub());

    // Sync mute / solo buttons
    muteButton.setToggleState(channel->isMuted(), juce::dontSendNotification);
    soloButton.setToggleState(channel->isSolo(),  juce::dontSendNotification);

    // Sync one-shot button
    oneShotButton.setToggleState(channel->isOneShot(), juce::dontSendNotification);

    // Sync mute group buttons
    const int currentGroup = audioEngine.getChannelMuteGroup(channelIndex);
    for (int g = 0; g < 4; ++g)
        muteGroupButtons[g].setToggleState(currentGroup == g + 1, juce::dontSendNotification);

    // Sync monitor mode box
    {
        int id;
        switch (channel->getMonitorMode())
        {
            case MonitorMode::AlwaysOn:        id = 1; break;
            case MonitorMode::WhileRecording:  id = 2; break;
            case MonitorMode::WhenTrackActive: id = 3; break;
            case MonitorMode::Off:             id = 4; break;
            default:                           id = 3; break;
        }
        if (monitorModeBox.getSelectedId() != id)
            monitorModeBox.setSelectedId(id, juce::dontSendNotification);
    }

    // State label — show MIDI LEARN / UNLEARN feedback
    auto& mlm = audioEngine.getMidiLearnManager();
    if (mlm.isLearning() && mlm.getLearningTarget().channelIndex == channelIndex)
    {
        stateLabel.setText("MIDI LEARN", juce::dontSendNotification);
    }
    else if (mlm.isUnlearning())
    {
        stateLabel.setText("UNLEARN", juce::dontSendNotification);
    }
    else
    {
        const auto state = channel->getState();
        juce::String stateText;
        switch (state)
        {
            case ChannelState::Idle:        stateText = "Idle";    break;
            case ChannelState::Recording:   stateText = "REC";     break;
            case ChannelState::Playing:     stateText = "PLAY";    break;
            case ChannelState::Overdubbing: stateText = "OVERDUB"; break;
        }
        stateLabel.setText(stateText, juce::dontSendNotification);
    }

    // Update level meters
    inputMeter.setLevel(juce::jmax(channel->getInputPeakL(), channel->getInputPeakR()));
    loopMeter .setLevel(juce::jmax(channel->getLoopPeakL(),  channel->getLoopPeakR()));

    // Repaint border when active state changes
    repaint();
}

//==============================================================================
void ChannelStripComponent::updateMainButton()
{
    auto* channel = audioEngine.getChannel(channelIndex);
    if (!channel) return;

    // Count-in blink: flash before recording starts on this channel
    if (audioEngine.isCountingIn() && audioEngine.getCountInPendingChannel() == channelIndex)
    {
        const bool blinkOn = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
        mainButton.setButtonText("REC");
        mainButton.setColour(juce::TextButton::buttonColourId,
                             blinkOn ? juce::Colours::red : juce::Colour(0xFF550000));
        return;
    }

    // Pending section record-ahead: blink while queued
    if (audioEngine.getPendingSectionRecordChannel() == channelIndex)
    {
        const bool blinkOn = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
        mainButton.setButtonText("PEND REC");
        mainButton.setColour(juce::TextButton::buttonColourId,
                             blinkOn ? juce::Colour(0xFFAA4400) : juce::Colour(0xFF663300));
        return;
    }

    if (channel->hasPendingRecord())
    {
        mainButton.setButtonText("PEND REC");
        mainButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFAA4400));
        return;
    }
    if (channel->hasPendingOverdub())
    {
        mainButton.setButtonText("PEND OVD");
        mainButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFAA6600));
        return;
    }
    if (channel->hasPendingPlay())
    {
        mainButton.setButtonText("PEND PLY");
        mainButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF447700));
        return;
    }
    if (channel->hasPendingStop())
    {
        mainButton.setButtonText("PEND STP");
        mainButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey.brighter());
        return;
    }

    const bool   overdubMode = audioEngine.isInOverdubMode();
    const auto   state       = channel->getState();
    const bool   hasLoop     = channel->hasLoop();

    // When a section switch is pending (latch mode), show what the button will do
    // in the NEW section — if the channel has no content there, show REC
    const int pendingSec = audioEngine.getPendingSection();
    const bool showNextSectionAction = (pendingSec >= 0 && audioEngine.isLatchMode());
    const bool hasLoopInPendingSec = showNextSectionAction && channel->sectionHasContent(pendingSec);

    juce::String label;
    juce::Colour colour;

    // Priority (Spec Abschnitt 5):
    // Overdub mode redirects Playing→Overdub; all other states behave normally.
    // 1. Overdub mode ON + Playing  → start Overdub
    // 2. Overdubbing                → Stop Overdub
    // 3. Recording                  → Stop Rec
    // 4. Loop empty                 → Record
    // 5. Loop exists, idle          → Play
    // 6. Loop exists, playing       → Stop

    if (overdubMode && state == ChannelState::Playing)
    {
        label  = "OVERDUB";
        colour = juce::Colours::orange;
    }
    else if (state == ChannelState::Overdubbing)
    {
        label  = "STOP OVD";
        colour = juce::Colours::darkorange;
    }
    else if (state == ChannelState::Recording)
    {
        label  = "STOP REC";
        colour = juce::Colour(0xFF8B0000);  // dark red
    }
    else if (!hasLoop || (showNextSectionAction && !hasLoopInPendingSec))
    {
        label  = "REC";
        colour = juce::Colours::red;
    }
    else if (state == ChannelState::Playing)
    {
        label  = "STOP";
        colour = juce::Colour(0xFF006400);  // dark green
    }
    else  // has loop, idle
    {
        label  = "PLAY";
        colour = juce::Colours::green;
    }

    mainButton.setButtonText(label);
    mainButton.setColour(juce::TextButton::buttonColourId, colour);
}

void ChannelStripComponent::mainButtonClicked()
{
    auto* channel = audioEngine.getChannel(channelIndex);
    if (!channel) return;

    // Set as active channel on button press
    audioEngine.setActiveChannel(channelIndex);

    // Cancel pending section record-ahead on second press
    if (audioEngine.getPendingSectionRecordChannel() == channelIndex)
    {
        audioEngine.cancelPendingSectionRecord();
        return;
    }

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

    // Latch record-ahead: if a section switch is pending and channel has no content
    // in the pending section, queue recording for when the section activates
    const int pendingSec = audioEngine.getPendingSection();
    if (pendingSec >= 0 && audioEngine.isLatchMode())
    {
        if (!channel->sectionHasContent(pendingSec) &&
            (state == ChannelState::Idle || state == ChannelState::Playing))
        {
            audioEngine.queueRecordForPendingSection(channelIndex, false);
            return;
        }
        if (overdubMode && state == ChannelState::Playing && channel->sectionHasContent(pendingSec))
        {
            audioEngine.queueRecordForPendingSection(channelIndex, true);
            return;
        }
    }

    if (overdubMode && state == ChannelState::Playing)
    {
        // Overdub mode: playing → start overdub instead of stopping
        Command cmd;
        cmd.type         = CommandType::StartOverdub;
        cmd.channelIndex = channelIndex;
        audioEngine.sendCommand(cmd);
    }
    else if (state == ChannelState::Overdubbing)
    {
        audioEngine.sendCommand(Command::stopRecord(channelIndex));
    }
    else if (state == ChannelState::Recording)
    {
        audioEngine.sendCommand(Command::stopRecord(channelIndex));
    }
    else if (!hasLoop)
    {
        audioEngine.sendCommand(Command::startRecord(channelIndex));
    }
    else if (state == ChannelState::Playing)
    {
        audioEngine.sendCommand(Command::stopPlayback(channelIndex));
    }
    else  // idle, has loop
    {
        audioEngine.sendCommand(Command::startPlayback(channelIndex));
    }
}

void ChannelStripComponent::clrButtonClicked()
{
    if (!channel_hasLoop()) return;

    Command cmd;
    cmd.type         = CommandType::ClearChannel;
    cmd.channelIndex = channelIndex;
    audioEngine.sendCommand(cmd);
}

void ChannelStripComponent::undoClicked()
{
    Command cmd;
    cmd.type         = CommandType::UndoOverdub;
    cmd.channelIndex = channelIndex;
    audioEngine.sendCommand(cmd);
}

bool ChannelStripComponent::channel_hasLoop() const
{
    auto* channel = audioEngine.getChannel(channelIndex);
    return channel && channel->hasLoop();
}

void ChannelStripComponent::muteClicked()
{
    Command cmd;
    cmd.type         = CommandType::SetMute;
    cmd.channelIndex = channelIndex;
    cmd.boolValue    = muteButton.getToggleState();
    audioEngine.sendCommand(cmd);
}

void ChannelStripComponent::soloClicked()
{
    Command cmd;
    cmd.type         = CommandType::SetSolo;
    cmd.channelIndex = channelIndex;
    cmd.boolValue    = soloButton.getToggleState();
    audioEngine.sendCommand(cmd);
}

void ChannelStripComponent::gainChanged()
{
    audioEngine.sendCommand(
        Command::setGain(channelIndex, static_cast<float>(gainSlider.getValue())));
}

void ChannelStripComponent::openFileClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Import Audio File",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        audioEngine.getFormatManager().getWildcardForAllFormats());

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file.existsAsFile())
            loadAudioFile(file);
    });
}

void ChannelStripComponent::loadAudioFile(const juce::File& file)
{
    auto& fm = audioEngine.getFormatManager();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (!reader)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Import Error", "Could not read file: " + file.getFileName());
        return;
    }

    const double engineSR = audioEngine.getSampleRate();
    if (engineSR <= 0.0) return;

    const juce::int64 fileLenSamples = static_cast<juce::int64>(reader->lengthInSamples);
    if (fileLenSamples <= 0) return;

    // Read file into stereo buffer
    juce::AudioBuffer<float> fileBuffer(2, static_cast<int>(fileLenSamples));
    reader->read(&fileBuffer, 0, static_cast<int>(fileLenSamples), 0, true, true);

    // Resample if needed
    juce::AudioBuffer<float>* srcBuffer = &fileBuffer;
    juce::AudioBuffer<float> resampledBuffer;

    if (std::abs(reader->sampleRate - engineSR) > 1.0)
    {
        const double ratio = reader->sampleRate / engineSR;
        const int newLen = static_cast<int>(fileLenSamples / ratio);
        resampledBuffer.setSize(2, newLen);

        for (int ch = 0; ch < 2; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process(ratio,
                           fileBuffer.getReadPointer(ch),
                           resampledBuffer.getWritePointer(ch),
                           newLen);
        }
        srcBuffer = &resampledBuffer;
    }

    const juce::int64 fileLen = srcBuffer->getNumSamples();
    auto& loopEng = audioEngine.getLoopEngine();
    juce::int64 loopLen = loopEng.getLoopLength();

    // Helper lambda to finish loading with a given target length
    auto finishLoad = [this, engineSR, fileLen, loopLen]
                      (std::shared_ptr<juce::AudioBuffer<float>> buf, juce::int64 targetLen)
    {
        juce::AudioBuffer<float> finalBuffer(2, static_cast<int>(targetLen));
        finalBuffer.clear();

        const int copyLen = static_cast<int>(juce::jmin(fileLen, targetLen));
        for (int ch = 0; ch < 2; ++ch)
            finalBuffer.copyFrom(ch, 0, *buf, ch, 0, copyLen);

        // Apply 50ms fadeout if truncating
        if (fileLen > targetLen)
        {
            const int fadeSamples = juce::jmin(static_cast<int>(engineSR * 0.05),
                                               static_cast<int>(targetLen));
            const int fadeStart = static_cast<int>(targetLen) - fadeSamples;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* data = finalBuffer.getWritePointer(ch);
                for (int i = 0; i < fadeSamples; ++i)
                    data[fadeStart + i] *= static_cast<float>(fadeSamples - i) / static_cast<float>(fadeSamples);
            }
        }

        auto* channel = audioEngine.getChannel(channelIndex);
        if (!channel) return;

        audioEngine.sendCommand(Command::stopPlayback(channelIndex));
        juce::Thread::sleep(20);

        channel->loadLoopData(finalBuffer, targetLen);

        if (loopLen == 0 || targetLen > loopLen)
        {
            audioEngine.getLoopEngine().setLoopLength(targetLen);
            const int activeSec = audioEngine.getActiveSection();
            audioEngine.setSectionLoopLength(activeSec, targetLen);
        }

        if (audioEngine.isPlaying())
            audioEngine.sendCommand(Command::startPlayback(channelIndex));
    };

    // Wrap buffer in shared_ptr for async callback
    auto sharedBuf = std::make_shared<juce::AudioBuffer<float>>(std::move(*srcBuffer));

    if (loopLen == 0)
    {
        finishLoad(sharedBuf, fileLen);
    }
    else if (loopLen < fileLen)
    {
        auto opts = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Extend Loop?")
            .withMessage("The audio file is longer than the current loop.\n"
                         "Extend the loop to fit, or truncate the file?")
            .withButton("Extend")
            .withButton("Truncate");

        juce::AlertWindow::showAsync(opts,
            [finishLoad, sharedBuf, fileLen, loopLen](int result)
            {
                finishLoad(sharedBuf, result == 1 ? fileLen : loopLen);
            });
    }
    else
    {
        finishLoad(sharedBuf, loopLen);
    }
}

void ChannelStripComponent::monitorModeChanged()
{
    MonitorMode mode;
    switch (monitorModeBox.getSelectedId())
    {
        case 1: mode = MonitorMode::AlwaysOn;        break;
        case 2: mode = MonitorMode::WhileRecording;  break;
        case 3: mode = MonitorMode::WhenTrackActive; break;
        case 4: mode = MonitorMode::Off;             break;
        default: return;
    }
    Command cmd;
    cmd.type         = CommandType::SetMonitorMode;
    cmd.channelIndex = channelIndex;
    cmd.intValue1    = static_cast<int>(mode);
    audioEngine.sendCommand(cmd);
}

void ChannelStripComponent::showMidiContextMenu(MidiControlTarget target)
{
    auto& mlm = audioEngine.getMidiLearnManager();
    const bool hasMapping = mlm.getMapping(channelIndex, target).isValid();
    const auto name = MidiLearnManager::targetName(target);

    juce::PopupMenu menu;
    menu.addItem(1, "MIDI Learn: " + name);
    menu.addItem(2, "Remove MIDI: " + name, hasMapping);

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, target](int id)
    {
        auto& mlm = audioEngine.getMidiLearnManager();
        if (id == 1)
            mlm.startLearning(channelIndex, target);
        else if (id == 2)
            mlm.removeMapping(channelIndex, target);
    });
}
