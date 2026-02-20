#include "ChannelStripComponent.h"

//==============================================================================
ChannelStripComponent::ChannelStripComponent(AudioEngine& engine, int channelIndex)
    : audioEngine(engine)
    , channelIdx(channelIndex)
    , pluginStrip(engine, channelIndex)
{
    //==========================================================================
    // Channel label
    channelLabel.setText(juce::String(channelIndex + 1), juce::dontSendNotification);
    channelLabel.setFont(juce::Font(24.0f, juce::Font::bold));
    channelLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(channelLabel);

    // State label
    stateLabel.setText("Idle", juce::dontSendNotification);
    stateLabel.setJustificationType(juce::Justification::centred);
    stateLabel.setColour(juce::Label::backgroundColourId, juce::Colours::darkgrey);
    addAndMakeVisible(stateLabel);

    //==========================================================================
    // Main button — context-sensitive (see updateMainButton())
    mainButton.setClickingTogglesState(false);
    mainButton.onClick = [this] { mainButtonClicked(); };
    mainButton.addMouseListener(this, false);
    addAndMakeVisible(mainButton);

    //==========================================================================
    // Secondary buttons

    // CLR
    clearButton.setClickingTogglesState(false);
    clearButton.onClick = [this] { clearClicked(); };
    addAndMakeVisible(clearButton);

    // Routing popup
    routingButton.setClickingTogglesState(false);
    routingButton.onClick = [this]
    {
        auto content = std::make_unique<RoutingComponent>(audioEngine, channelIdx);
        juce::CallOutBox::launchAsynchronously(std::move(content),
                                               routingButton.getScreenBounds(),
                                               nullptr);
    };
    addAndMakeVisible(routingButton);

    // FX / Plugin popup
    fxButton.setClickingTogglesState(false);
    fxButton.onClick = [this]
    {
        auto content = std::make_unique<PluginManagerComponent>(audioEngine, channelIdx);
        juce::CallOutBox::launchAsynchronously(std::move(content),
                                               fxButton.getScreenBounds(),
                                               nullptr);
    };
    addAndMakeVisible(fxButton);

    //==========================================================================
    // Gain slider
    gainSlider.setSliderStyle(juce::Slider::LinearVertical);
    gainSlider.setRange(-60.0, 12.0, 0.1);
    gainSlider.setValue(0.0);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    gainSlider.onValueChange = [this] { gainChanged(); };
    gainSlider.addMouseListener(this, false);
    addAndMakeVisible(gainSlider);

    gainLabel.setText("Gain", juce::dontSendNotification);
    gainLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(gainLabel);

    //==========================================================================
    // Mute / Solo
    muteButton.onClick = [this] { muteClicked(); };
    muteButton.addMouseListener(this, false);
    addAndMakeVisible(muteButton);

    soloButton.onClick = [this] { soloClicked(); };
    soloButton.addMouseListener(this, false);
    addAndMakeVisible(soloButton);

    //==========================================================================
    // Monitor mode
    monitorModeBox.addItem("Off",            1);
    monitorModeBox.addItem("Always On",      2);
    monitorModeBox.addItem("While Rec",      3);
    monitorModeBox.addItem("When Active",    4);
    monitorModeBox.setSelectedId(2, juce::dontSendNotification);  // AlwaysOn
    monitorModeBox.onChange = [this] { monitorModeChanged(); };
    addAndMakeVisible(monitorModeBox);

    //==========================================================================
    // Plugin strip (embedded, fills remaining space)
    addAndMakeVisible(pluginStrip);

    // Initial button appearance
    updateMainButton();

    startTimer(100);  // 10 Hz refresh
}

ChannelStripComponent::~ChannelStripComponent()
{
    stopTimer();
}

//==============================================================================
// Layout
//==============================================================================

void ChannelStripComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    g.setColour(juce::Colours::grey);
    g.drawRect(getLocalBounds(), 2);

    // State colour on the state label background
    juce::Colour stateColour;
    switch (currentState)
    {
        case ChannelState::Recording:   stateColour = juce::Colours::red;      break;
        case ChannelState::Playing:     stateColour = juce::Colours::green;    break;
        case ChannelState::Overdubbing: stateColour = juce::Colours::orange;   break;
        default:                        stateColour = juce::Colours::darkgrey; break;
    }
    stateLabel.setColour(juce::Label::backgroundColourId, stateColour);
}

void ChannelStripComponent::resized()
{
    auto area = getLocalBounds().reduced(5);

    // Channel number
    channelLabel.setBounds(area.removeFromTop(30));

    // State indicator
    stateLabel.setBounds(area.removeFromTop(25));
    area.removeFromTop(5);

    // Main button (taller — it's the primary control)
    mainButton.setBounds(area.removeFromTop(36));
    area.removeFromTop(4);

    // Secondary buttons in a row
    auto secRow = area.removeFromTop(22);
    const int secW = secRow.getWidth() / 3;
    clearButton  .setBounds(secRow.removeFromLeft(secW));
    routingButton.setBounds(secRow.removeFromLeft(secW));
    fxButton     .setBounds(secRow);

    area.removeFromTop(5);

    // Gain
    gainLabel .setBounds(area.removeFromTop(20));
    gainSlider.setBounds(area.removeFromTop(150));
    area.removeFromTop(5);

    // Mute / Solo
    auto msRow = area.removeFromTop(25);
    muteButton.setBounds(msRow.removeFromLeft(msRow.getWidth() / 2));
    soloButton.setBounds(msRow);

    // Monitor mode
    monitorModeBox.setBounds(area.removeFromBottom(25));

    // Plugin strip fills the rest
    pluginStrip.setBounds(area.reduced(0, 2));
}

//==============================================================================
// Timer — poll channel state and refresh UI
//==============================================================================

void ChannelStripComponent::timerCallback()
{
    updateFromChannel();
}

void ChannelStripComponent::updateFromChannel()
{
    auto* channel = audioEngine.getChannel(channelIdx);
    if (!channel)
        return;

    const auto newState   = channel->getState();
    const bool newHasLoop = channel->hasLoop();

    // Only repaint / update button when something actually changed
    if (newState != currentState || newHasLoop != hasLoop)
    {
        currentState = newState;
        hasLoop      = newHasLoop;

        // Update state label text
        switch (currentState)
        {
            case ChannelState::Idle:        stateLabel.setText("Idle",       juce::dontSendNotification); break;
            case ChannelState::Recording:   stateLabel.setText("Recording",  juce::dontSendNotification); break;
            case ChannelState::Playing:     stateLabel.setText("Playing",    juce::dontSendNotification); break;
            case ChannelState::Overdubbing: stateLabel.setText("Overdubbing",juce::dontSendNotification); break;
        }

        updateMainButton();
        repaint();
    }

    // Mute / Solo toggles (keep in sync with engine state)
    muteButton.setToggleState(channel->isMuted(), juce::dontSendNotification);
    soloButton.setToggleState(channel->isSolo(),  juce::dontSendNotification);
}

//==============================================================================
// Main button — logic + appearance
//==============================================================================

/**
 * Determines what pressing the main button does, based on priority:
 *
 *  Priority 1 – Global overdub mode is ON
 *               → button toggles overdub on this channel
 *               (regardless of whether loop exists — AudioEngine enforces the
 *                "must have content" guard in Channel::startRecording(true))
 *
 *  Priority 2 – Loop is empty (no content yet)
 *               → button starts Recording
 *
 *  Priority 3 – Loop exists, channel is idle/stopped
 *               → button starts Playback
 *
 *  Priority 4 – Loop exists, channel is Playing
 *               → button stops Playback
 *
 *  While Recording (non-overdub): button stops recording and transitions
 *  to Playing automatically (handled by Channel::stopRecording()).
 */
void ChannelStripComponent::mainButtonClicked()
{
    const bool overdubMode = audioEngine.isInOverdubMode();

    if (overdubMode)
    {
        // Toggle overdub on this channel
        if (currentState == ChannelState::Overdubbing)
        {
            Command cmd;
            cmd.type         = CommandType::StopOverdub;
            cmd.channelIndex = channelIdx;
            audioEngine.sendCommand(cmd);
        }
        else
        {
            Command cmd;
            cmd.type         = CommandType::StartOverdub;
            cmd.channelIndex = channelIdx;
            audioEngine.sendCommand(cmd);
        }
        return;
    }

    // Normal mode
    switch (currentState)
    {
        case ChannelState::Idle:
        {
            if (!hasLoop)
            {
                // Empty loop → start recording
                audioEngine.sendCommand(Command::startRecord(channelIdx));
            }
            else
            {
                // Loop exists but stopped → start playback
                audioEngine.sendCommand(Command::startPlayback(channelIdx));
            }
            break;
        }

        case ChannelState::Recording:
        {
            // Stop recording → channel auto-transitions to Playing
            audioEngine.sendCommand(Command::stopRecord(channelIdx));
            break;
        }

        case ChannelState::Playing:
        {
            // Stop playback → back to Idle
            audioEngine.sendCommand(Command::stopPlayback(channelIdx));
            break;
        }

        case ChannelState::Overdubbing:
        {
            // Should not normally reach here outside overdub mode,
            // but handle gracefully just in case.
            Command cmd;
            cmd.type         = CommandType::StopOverdub;
            cmd.channelIndex = channelIdx;
            audioEngine.sendCommand(cmd);
            break;
        }
    }
}

void ChannelStripComponent::updateMainButton()
{
    const bool overdubMode = audioEngine.isInOverdubMode();

    juce::String label;
    juce::Colour colour;

    if (overdubMode)
    {
        if (currentState == ChannelState::Overdubbing)
        {
            label  = "STOP OVD";
            colour = juce::Colours::orange.darker(0.2f);
        }
        else
        {
            label  = "OVERDUB";
            colour = juce::Colours::orange;
        }
    }
    else
    {
        switch (currentState)
        {
            case ChannelState::Idle:
                label  = hasLoop ? "PLAY" : "REC";
                colour = hasLoop ? juce::Colours::green : juce::Colours::red;
                break;

            case ChannelState::Recording:
                label  = "STOP REC";
                colour = juce::Colours::red.darker(0.2f);
                break;

            case ChannelState::Playing:
                label  = "STOP";
                colour = juce::Colours::green.darker(0.2f);
                break;

            case ChannelState::Overdubbing:
                label  = "STOP OVD";
                colour = juce::Colours::orange.darker(0.2f);
                break;
        }
    }

    mainButton.setButtonText(label);
    mainButton.setColour(juce::TextButton::buttonColourId, colour);
}

//==============================================================================
// Secondary button handlers
//==============================================================================

void ChannelStripComponent::clearClicked()
{
    Command cmd;
    cmd.type         = CommandType::ClearChannel;
    cmd.channelIndex = channelIdx;
    audioEngine.sendCommand(cmd);
}

void ChannelStripComponent::gainChanged()
{
    audioEngine.sendCommand(
        Command::setGain(channelIdx, static_cast<float>(gainSlider.getValue())));
}

void ChannelStripComponent::monitorModeChanged()
{
    MonitorMode mode;
    switch (monitorModeBox.getSelectedId())
    {
        case 1:  mode = MonitorMode::Off;            break;
        case 2:  mode = MonitorMode::AlwaysOn;       break;
        case 3:  mode = MonitorMode::WhileRecording; break;
        case 4:  mode = MonitorMode::WhenTrackActive;break;
        default: mode = MonitorMode::AlwaysOn;       break;
    }

    Command cmd;
    cmd.type         = CommandType::SetMonitorMode;
    cmd.channelIndex = channelIdx;
    cmd.intValue1    = static_cast<int>(mode);
    audioEngine.sendCommand(cmd);
}

void ChannelStripComponent::muteClicked()
{
    Command cmd;
    cmd.type         = CommandType::SetMute;
    cmd.channelIndex = channelIdx;
    cmd.boolValue    = !audioEngine.getChannel(channelIdx)->isMuted();  // toggle
    audioEngine.sendCommand(cmd);
}

void ChannelStripComponent::soloClicked()
{
    Command cmd;
    cmd.type         = CommandType::SetSolo;
    cmd.channelIndex = channelIdx;
    cmd.boolValue    = !audioEngine.getChannel(channelIdx)->isSolo();   // toggle
    audioEngine.sendCommand(cmd);
}

//==============================================================================
// MIDI-Learn context menu
//==============================================================================

void ChannelStripComponent::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isRightButtonDown())
        return;

    struct ControlMap { juce::Component* comp; MidiControlTarget target; };
    const ControlMap controls[] =
    {
        { &mainButton,  MidiControlTarget::Record  },  // REC / PLAY maps to Record target
        { &gainSlider,  MidiControlTarget::Gain    },
        { &muteButton,  MidiControlTarget::Mute    },
        { &soloButton,  MidiControlTarget::Solo    },
    };

    for (auto& c : controls)
    {
        if (e.eventComponent == c.comp)
        {
            showMidiContextMenu(c.comp, c.target);
            return;
        }
    }
}

void ChannelStripComponent::showMidiContextMenu(juce::Component* control,
                                                MidiControlTarget target)
{
    auto& mlm      = audioEngine.getMidiLearnManager();
    const auto existing = mlm.getMapping(channelIdx, target);

    juce::PopupMenu menu;
    menu.addSectionHeader("MIDI — " + MidiLearnManager::targetName(target));
    menu.addSeparator();

    if (existing.isValid())
    {
        juce::String info = (existing.ccNumber >= 0)
            ? "CC "   + juce::String(existing.ccNumber)  + " (Ch " + juce::String(existing.midiChannel) + ")"
            : "Note " + juce::String(existing.noteNumber) + " (Ch " + juce::String(existing.midiChannel) + ")";

        menu.addItem(1, "Zugewiesen: " + info, false, false);
        menu.addSeparator();
        menu.addItem(3, "Zuweisung entfernen");
    }
    else
    {
        menu.addItem(1, "Keine Zuweisung", false, false);
        menu.addSeparator();
    }

    menu.addItem(2, "MIDI Learn (auf Regler/Taste drücken)");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(control),
        [this, target](int result)
        {
            auto& mlm = audioEngine.getMidiLearnManager();
            if (result == 2)
                mlm.startLearning(channelIdx, target);
            else if (result == 3)
                mlm.removeMapping(channelIdx, target);
        });
}

juce::String ChannelStripComponent::getMidiAssignmentLabel(MidiControlTarget target) const
{
    const auto m = audioEngine.getMidiLearnManager().getMapping(channelIdx, target);
    if (!m.isValid()) return {};
    return (m.ccNumber >= 0) ? "CC" + juce::String(m.ccNumber)
                             : "N"  + juce::String(m.noteNumber);
}
