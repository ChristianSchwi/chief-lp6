#include "ChannelStripComponent.h"
#include "RoutingComponent.h"
#include "PluginManagerComponent.h"

//==============================================================================
ChannelStripComponent::ChannelStripComponent(AudioEngine& engine, int index)
    : audioEngine(engine), channelIndex(index)
{
    //--------------------------------------------------------------------------
    // Channel label
    channelLabel.setText("CH " + juce::String(index + 1), juce::dontSendNotification);
    channelLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    channelLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(channelLabel);

    stateLabel.setText("Idle", juce::dontSendNotification);
    stateLabel.setFont(juce::Font(11.0f));
    stateLabel.setJustificationType(juce::Justification::centred);
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
    addAndMakeVisible(clrButton);

    ioButton.onClick = [this]
    {
        auto* channel = audioEngine.getChannel(channelIndex);
        if (!channel) return;
        auto* dlg = new RoutingComponent(audioEngine, channelIndex);
        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(dlg);
        opts.dialogTitle           = "Routing — Ch " + juce::String(channelIndex + 1);
        opts.dialogBackgroundColour= juce::Colours::darkgrey;
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar     = false;
        opts.resizable             = false;
        opts.launchAsync();
    };
    addAndMakeVisible(ioButton);

    fxButton.onClick = [this]
    {
        auto* channel = audioEngine.getChannel(channelIndex);
        if (!channel) return;
        auto* dlg = new PluginManagerComponent(audioEngine, channelIndex);
        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(dlg);
        opts.dialogTitle           = "FX — Ch " + juce::String(channelIndex + 1);
        opts.dialogBackgroundColour= juce::Colours::darkgrey;
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar     = false;
        opts.resizable             = false;
        opts.launchAsync();
    };
    addAndMakeVisible(fxButton);

    //--------------------------------------------------------------------------
    // Monitor mode
    // Order matches user request: Always On / While Recording / While Active / Always Off
    monitorModeBox.addItem("Always On",       1);   // MonitorMode::AlwaysOn
    monitorModeBox.addItem("While Recording", 2);   // MonitorMode::WhileRecording
    monitorModeBox.addItem("While Active",    3);   // MonitorMode::WhenTrackActive (default)
    monitorModeBox.addItem("Always Off",      4);   // MonitorMode::Off
    monitorModeBox.setSelectedId(3, juce::dontSendNotification);
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
    muteButton.onClick = [this] { muteClicked(); };
    addAndMakeVisible(muteButton);

    soloButton.setClickingTogglesState(true);
    soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow);
    soloButton.onClick = [this] { soloClicked(); };
    addAndMakeVisible(soloButton);

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
    const int btnW = btnRow.getWidth() / 3;
    clrButton.setBounds(btnRow.removeFromLeft(btnW).reduced(1));
    ioButton .setBounds(btnRow.removeFromLeft(btnW).reduced(1));
    fxButton .setBounds(btnRow                     .reduced(1));
    area.removeFromTop(4);

    // Mute / Solo
    auto msRow = area.removeFromTop(26);
    muteButton.setBounds(msRow.removeFromLeft(msRow.getWidth() / 2).reduced(2));
    soloButton.setBounds(msRow.reduced(2));
    area.removeFromTop(4);

    // Monitor mode
    monitorModeBox.setBounds(area.removeFromTop(22));
    area.removeFromTop(2);

    // Gain slider fills remaining space
    gainSlider.setBounds(area);
}

//==============================================================================
// Clicking anywhere on the strip activates this channel
void ChannelStripComponent::mouseDown(const juce::MouseEvent& e)
{
    audioEngine.setActiveChannel(channelIndex);
    repaint();  // immediate visual feedback

    // Right-click context menu
    if (e.mods.isRightButtonDown())
        showContextMenu(e);
}

//==============================================================================
// Timer
//==============================================================================

void ChannelStripComponent::timerCallback()
{
    updateMainButton();

    auto* channel = audioEngine.getChannel(channelIndex);
    if (!channel) return;

    // Sync mute / solo buttons
    muteButton.setToggleState(channel->isMuted(), juce::dontSendNotification);
    soloButton.setToggleState(channel->isSolo(),  juce::dontSendNotification);

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

    // State label — show MIDI LEARN if this channel is being assigned
    auto& mlm = audioEngine.getMidiLearnManager();
    if (mlm.isLearning() && mlm.getLearningTarget().channelIndex == channelIndex)
    {
        stateLabel.setText("MIDI LEARN", juce::dontSendNotification);
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

    // Repaint border when active state changes
    repaint();
}

//==============================================================================
void ChannelStripComponent::updateMainButton()
{
    auto* channel = audioEngine.getChannel(channelIndex);
    if (!channel) return;

    const bool   overdubMode = audioEngine.isInOverdubMode();
    const auto   state       = channel->getState();
    const bool   hasLoop     = channel->hasLoop();

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
    else if (!hasLoop)
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

    const bool overdubMode = audioEngine.isInOverdubMode();
    const auto state       = channel->getState();
    const bool hasLoop     = channel->hasLoop();

    // Set as active channel on button press
    audioEngine.setActiveChannel(channelIndex);

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

    auto options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("Clear Channel")
        .withMessage("Clear channel " + juce::String(channelIndex + 1) + "?")
        .withButton("Clear")
        .withButton("Cancel");

    juce::AlertWindow::showAsync(options, [this](int result)
    {
        if (result == 1)
        {
            Command cmd;
            cmd.type         = CommandType::ClearChannel;
            cmd.channelIndex = channelIndex;
            audioEngine.sendCommand(cmd);
        }
    });
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

void ChannelStripComponent::showContextMenu(const juce::MouseEvent&)
{
    juce::PopupMenu menu;
    menu.addItem(1, "MIDI Learn: Main Button");
    menu.addItem(2, "MIDI Learn: Gain");
    menu.addItem(3, "MIDI Learn: Mute");
    menu.addItem(4, "MIDI Learn: Solo");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int id)
    {
        switch (id)
        {
            case 1: showMidiLearnMenu(&mainButton); break;
            case 2: showMidiLearnMenu(&gainSlider); break;
            case 3: showMidiLearnMenu(&muteButton); break;
            case 4: showMidiLearnMenu(&soloButton); break;
            default: break;
        }
    });
}

void ChannelStripComponent::showMidiLearnMenu(juce::Component* target)
{
    MidiControlTarget midiTarget;

    if      (target == &mainButton) midiTarget = MidiControlTarget::Record;
    else if (target == &gainSlider) midiTarget = MidiControlTarget::Gain;
    else if (target == &muteButton) midiTarget = MidiControlTarget::Mute;
    else if (target == &soloButton) midiTarget = MidiControlTarget::Solo;
    else return;

    audioEngine.getMidiLearnManager().startLearning(channelIndex, midiTarget);
    DBG("MIDI Learn started: ch" + juce::String(channelIndex) +
        " target " + MidiLearnManager::targetName(midiTarget));
}
