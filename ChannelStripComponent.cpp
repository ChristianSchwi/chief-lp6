#include "ChannelStripComponent.h"

//==============================================================================
ChannelStripComponent::ChannelStripComponent(AudioEngine& engine, int channelIndex)
    : audioEngine(engine)
    , channelIdx(channelIndex)
    , pluginStrip(engine, channelIndex)
{
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
    
    // Record button
    recordButton.setClickingTogglesState(false);
    recordButton.onClick = [this] { buttonClicked(&recordButton); };
    recordButton.onStateChange = []{};
    addAndMakeVisible(recordButton);
    recordButton.addMouseListener(this, false);

    // Play button
    playButton.setClickingTogglesState(false);
    playButton.onClick = [this] { buttonClicked(&playButton); };
    addAndMakeVisible(playButton);
    playButton.addMouseListener(this, false);

    // Overdub button
    overdubButton.setClickingTogglesState(false);
    overdubButton.onClick = [this] { buttonClicked(&overdubButton); };
    addAndMakeVisible(overdubButton);
    overdubButton.addMouseListener(this, false);

    // Clear button
    clearButton.setClickingTogglesState(false);
    clearButton.onClick = [this] { buttonClicked(&clearButton); };
    addAndMakeVisible(clearButton);
    clearButton.addMouseListener(this, false);
    
    // Routing button
    routingButton.setClickingTogglesState(false);
    routingButton.onClick = [this]
    {
        auto content = std::make_unique<RoutingComponent>(audioEngine, channelIdx);
        juce::CallOutBox::launchAsynchronously(std::move(content),
                                               routingButton.getScreenBounds(),
                                               nullptr);
    };
    addAndMakeVisible(routingButton);

    // FX / Plugin button
    fxButton.setClickingTogglesState(false);
    fxButton.onClick = [this]
    {
        auto content = std::make_unique<PluginManagerComponent>(audioEngine, channelIdx);
        juce::CallOutBox::launchAsynchronously(std::move(content),
                                               fxButton.getScreenBounds(),
                                               nullptr);
    };
    addAndMakeVisible(fxButton);
    
    // Gain slider – Rechtsklick für MIDI Learn
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
    
    // Mute button
    muteButton.onClick = [this] { buttonClicked(&muteButton); };
    muteButton.addMouseListener(this, false);
    addAndMakeVisible(muteButton);
    
    // Solo button
    soloButton.onClick = [this] { buttonClicked(&soloButton); };
    soloButton.addMouseListener(this, false);
    addAndMakeVisible(soloButton);
    
    // Monitor mode
    monitorModeBox.addItem("Off", 1);
    monitorModeBox.addItem("Always On", 2);
    monitorModeBox.addItem("While Recording", 3);
    monitorModeBox.addItem("When Active", 4);
    monitorModeBox.setSelectedId(2);  // AlwaysOn
    monitorModeBox.onChange = [this] { monitorModeChanged(); };
    addAndMakeVisible(monitorModeBox);

    // Plugin strip
    addAndMakeVisible(pluginStrip);
    
    // Start timer for state updates
    startTimer(100);  // 10Hz refresh
}

ChannelStripComponent::~ChannelStripComponent()
{
    stopTimer();
}

//==============================================================================
void ChannelStripComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    
    // Draw border
    g.setColour(juce::Colours::grey);
    g.drawRect(getLocalBounds(), 2);
    
    // Draw state indicator
    juce::Colour stateColour = juce::Colours::darkgrey;
    
    switch (currentState)
    {
        case ChannelState::Idle:
            stateColour = juce::Colours::darkgrey;
            break;
        case ChannelState::Recording:
            stateColour = juce::Colours::red;
            break;
        case ChannelState::Playing:
            stateColour = juce::Colours::green;
            break;
        case ChannelState::Overdubbing:
            stateColour = juce::Colours::orange;
            break;
    }
    
    stateLabel.setColour(juce::Label::backgroundColourId, stateColour);
}

void ChannelStripComponent::resized()
{
    auto area = getLocalBounds().reduced(5);
    
    // Channel number at top
    channelLabel.setBounds(area.removeFromTop(30));
    
    // State indicator
    stateLabel.setBounds(area.removeFromTop(25));
    
    area.removeFromTop(5);
    
    // Transport buttons
    auto buttonArea = area.removeFromTop(120);
    recordButton.setBounds(buttonArea.removeFromTop(20));
    playButton.setBounds(buttonArea.removeFromTop(20));
    overdubButton.setBounds(buttonArea.removeFromTop(20));
    clearButton.setBounds(buttonArea.removeFromTop(20));
    routingButton.setBounds(buttonArea.removeFromTop(20));
    fxButton.setBounds(buttonArea.removeFromTop(20));
    
    area.removeFromTop(5);
    
    // Gain section
    gainLabel.setBounds(area.removeFromTop(20));
    gainSlider.setBounds(area.removeFromTop(150));
    
    area.removeFromTop(5);
    
    // Mute/Solo
    auto muteArea = area.removeFromTop(25);
    muteButton.setBounds(muteArea.removeFromLeft(muteArea.getWidth() / 2));
    soloButton.setBounds(muteArea);
    
    // Monitor mode at bottom
    monitorModeBox.setBounds(area.removeFromBottom(25));

    // Plugin strip fills remaining space
    pluginStrip.setBounds(area.reduced(0, 2));
}

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
    
    // Update state
    auto newState = channel->getState();
    if (newState != currentState)
    {
        currentState = newState;
        
        switch (currentState)
        {
            case ChannelState::Idle:
                stateLabel.setText("Idle", juce::dontSendNotification);
                break;
            case ChannelState::Recording:
                stateLabel.setText("Recording", juce::dontSendNotification);
                break;
            case ChannelState::Playing:
                stateLabel.setText("Playing", juce::dontSendNotification);
                break;
            case ChannelState::Overdubbing:
                stateLabel.setText("Overdubbing", juce::dontSendNotification);
                break;
        }
        
        repaint();
    }
    
    // Update hasLoop flag
    hasLoop = channel->hasLoop();
    
    // Update mute/solo
    muteButton.setToggleState(channel->isMuted(), juce::dontSendNotification);
    soloButton.setToggleState(channel->isSolo(), juce::dontSendNotification);
}

//==============================================================================
void ChannelStripComponent::buttonClicked(juce::Button* button)
{
    if (button == &recordButton)
    {
        Command cmd = Command::startRecord(channelIdx);
        audioEngine.sendCommand(cmd);
    }
    else if (button == &playButton)
    {
        Command cmd = Command::startPlayback(channelIdx);
        audioEngine.sendCommand(cmd);
    }
    else if (button == &overdubButton)
    {
        Command cmd;
        cmd.type = CommandType::StartOverdub;
        cmd.channelIndex = channelIdx;
        audioEngine.sendCommand(cmd);
    }
    else if (button == &clearButton)
    {
        Command cmd;
        cmd.type = CommandType::ClearChannel;
        cmd.channelIndex = channelIdx;
        audioEngine.sendCommand(cmd);
    }
    else if (button == &muteButton)
    {
        Command cmd;
        cmd.type = CommandType::SetMute;
        cmd.channelIndex = channelIdx;
        cmd.boolValue = muteButton.getToggleState();
        audioEngine.sendCommand(cmd);
    }
    else if (button == &soloButton)
    {
        Command cmd;
        cmd.type = CommandType::SetSolo;
        cmd.channelIndex = channelIdx;
        cmd.boolValue = soloButton.getToggleState();
        audioEngine.sendCommand(cmd);
    }
}

void ChannelStripComponent::gainChanged()
{
    Command cmd = Command::setGain(channelIdx, static_cast<float>(gainSlider.getValue()));
    audioEngine.sendCommand(cmd);
}

void ChannelStripComponent::monitorModeChanged()
{
    int selectedId = monitorModeBox.getSelectedId();
    MonitorMode mode = MonitorMode::Off;
    
    switch (selectedId)
    {
        case 1: mode = MonitorMode::Off; break;
        case 2: mode = MonitorMode::AlwaysOn; break;
        case 3: mode = MonitorMode::WhileRecording; break;
        case 4: mode = MonitorMode::WhenTrackActive; break;
    }
    
    Command cmd;
    cmd.type = CommandType::SetMonitorMode;
    cmd.channelIndex = channelIdx;
    cmd.intValue1 = static_cast<int>(mode);
    audioEngine.sendCommand(cmd);
}

//==============================================================================
// MIDI-Learn Kontextmenü
//==============================================================================

void ChannelStripComponent::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isRightButtonDown())
        return;

    // Prüfen welches Control angeklickt wurde
    struct ControlMap { juce::Component* comp; MidiControlTarget target; };
    const ControlMap controls[] =
    {
        { &recordButton,  MidiControlTarget::Record      },
        { &playButton,    MidiControlTarget::Play        },
        { &overdubButton, MidiControlTarget::Overdub     },
        { &clearButton,   MidiControlTarget::Clear       },
        { &gainSlider,    MidiControlTarget::Gain        },
        { &muteButton,    MidiControlTarget::Mute        },
        { &soloButton,    MidiControlTarget::Solo        },
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
    auto& mlm = audioEngine.getMidiLearnManager();
    const auto existing = mlm.getMapping(channelIdx, target);

    juce::PopupMenu menu;
    menu.addSectionHeader("MIDI – " + MidiLearnManager::targetName(target));
    menu.addSeparator();

    // Aktuelles Mapping anzeigen
    if (existing.isValid())
    {
        juce::String info;
        if (existing.ccNumber >= 0)
            info = "CC " + juce::String(existing.ccNumber) +
                   " (Ch " + juce::String(existing.midiChannel) + ")";
        else
            info = "Note " + juce::String(existing.noteNumber) +
                   " (Ch " + juce::String(existing.midiChannel) + ")";

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

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(control),
        [this, target](int result)
        {
            auto& mlm = audioEngine.getMidiLearnManager();
            if (result == 2)
            {
                mlm.startLearning(channelIdx, target);
                DBG("MIDI Learn aktiv für Channel " + juce::String(channelIdx) +
                    " / " + MidiLearnManager::targetName(target));
            }
            else if (result == 3)
            {
                mlm.removeMapping(channelIdx, target);
            }
        });
}

juce::String ChannelStripComponent::getMidiAssignmentLabel(MidiControlTarget target) const
{
    const auto m = audioEngine.getMidiLearnManager().getMapping(channelIdx, target);
    if (!m.isValid())
        return {};
    if (m.ccNumber >= 0)
        return "CC" + juce::String(m.ccNumber);
    return "N" + juce::String(m.noteNumber);
}
