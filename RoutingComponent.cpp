#include "RoutingComponent.h"
#include "VSTiChannel.h"

//==============================================================================
RoutingComponent::RoutingComponent(AudioEngine& engine, int channelIndex)
    : audioEngine(engine)
    , channelIdx(channelIndex)
{
    auto* channel = audioEngine.getChannel(channelIdx);
    isVSTiChannel = (channel && channel->getType() == ChannelType::VSTi);

    // --- Input section ---
    inputLabel.setText("Inputs", juce::dontSendNotification);
    inputLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(inputLabel);

    inputLeftLabel.setText("L:", juce::dontSendNotification);
    inputLeftLabel.setFont(juce::Font(11.0f));
    addAndMakeVisible(inputLeftLabel);
    addAndMakeVisible(inputLeftBox);

    inputRightLabel.setText("R:", juce::dontSendNotification);
    inputRightLabel.setFont(juce::Font(11.0f));
    addAndMakeVisible(inputRightLabel);
    addAndMakeVisible(inputRightBox);

    // --- Output section ---
    outputLabel.setText("Outputs", juce::dontSendNotification);
    outputLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(outputLabel);

    outputLeftLabel.setText("L:", juce::dontSendNotification);
    outputLeftLabel.setFont(juce::Font(11.0f));
    addAndMakeVisible(outputLeftLabel);
    addAndMakeVisible(outputLeftBox);

    outputRightLabel.setText("R:", juce::dontSendNotification);
    outputRightLabel.setFont(juce::Font(11.0f));
    addAndMakeVisible(outputRightLabel);
    addAndMakeVisible(outputRightBox);

    // --- MIDI section ---
    midiLabel.setText("MIDI Channel", juce::dontSendNotification);
    midiLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    midiLabel.setVisible(isVSTiChannel);

    midiChannelBox.setVisible(isVSTiChannel);
    midiChannelBox.addItem("All channels", 1);
    for (int ch = 1; ch <= 16; ++ch)
        midiChannelBox.addItem("Channel " + juce::String(ch), ch + 1);

    addChildComponent(midiLabel);
    addChildComponent(midiChannelBox);

    // --- Apply button ---
    applyButton.onClick = [this] { applyRouting(); };
    addAndMakeVisible(applyButton);

    populateChannelBoxes();
    loadCurrentRouting();

    setSize(kWidth, isVSTiChannel ? kHeight + 30 : kHeight);
}

//==============================================================================
void RoutingComponent::populateChannelBoxes()
{
    const int numIn  = audioEngine.getNumInputChannels();
    const int numOut = audioEngine.getNumOutputChannels();

    // Input boxes  — item ID = hardware channel index + 1  (ID 1 = "None")
    inputLeftBox.clear();
    inputRightBox.clear();
    inputLeftBox.addItem("None", 1);
    inputRightBox.addItem("Mono (L only)", 1);

    for (int i = 0; i < numIn; ++i)
    {
        juce::String name = "In " + juce::String(i + 1);
        inputLeftBox.addItem(name,  i + 2);   // ID offset by 2 (1 = None)
        inputRightBox.addItem(name, i + 2);
    }

    // Output boxes — same scheme, no "None" option
    outputLeftBox.clear();
    outputRightBox.clear();
    for (int i = 0; i < numOut; ++i)
    {
        juce::String name = "Out " + juce::String(i + 1);
        outputLeftBox.addItem(name,  i + 1);
        outputRightBox.addItem(name, i + 1);
    }
}

void RoutingComponent::loadCurrentRouting()
{
    auto* channel = audioEngine.getChannel(channelIdx);
    if (!channel)
        return;

    const auto routing = channel->getRouting();

    // inputChannelLeft: -1 = None  → ID 1,  0..n → ID n+2
    inputLeftBox.setSelectedId(routing.inputChannelLeft < 0 ? 1
                                                             : routing.inputChannelLeft + 2,
                               juce::dontSendNotification);

    // inputChannelRight: -1 = Mono → ID 1,  0..n → ID n+2
    inputRightBox.setSelectedId(routing.inputChannelRight < 0 ? 1
                                                              : routing.inputChannelRight + 2,
                                juce::dontSendNotification);

    outputLeftBox.setSelectedId(routing.outputChannelLeft  + 1, juce::dontSendNotification);
    outputRightBox.setSelectedId(routing.outputChannelRight + 1, juce::dontSendNotification);

    if (isVSTiChannel)
        midiChannelBox.setSelectedId(routing.midiChannelFilter + 1, juce::dontSendNotification);
}

void RoutingComponent::applyRouting()
{
    auto* channel = audioEngine.getChannel(channelIdx);
    if (!channel)
        return;

    RoutingConfig config = channel->getRouting();  // Start from current values

    // ID 1 = None/-1,  ID 2..n = channel index 0..n-1
    const int leftId  = inputLeftBox.getSelectedId();
    const int rightId = inputRightBox.getSelectedId();
    config.inputChannelLeft  = (leftId  <= 1) ? -1 : leftId  - 2;
    config.inputChannelRight = (rightId <= 1) ? -1 : rightId - 2;

    config.outputChannelLeft  = outputLeftBox.getSelectedId()  - 1;
    config.outputChannelRight = outputRightBox.getSelectedId() - 1;

    if (isVSTiChannel)
        config.midiChannelFilter = midiChannelBox.getSelectedId() - 1;

    // Send to audio thread via command
    Command cmd;
    cmd.type         = CommandType::SetInputRouting;  // Engine handles both fields
    cmd.channelIndex = channelIdx;
    cmd.data.routing = config;
    audioEngine.sendCommand(cmd);

    // Schließt den CallOutBox nach dem Anwenden
    if (auto* parent = findParentComponentOfClass<juce::CallOutBox>())
        parent->dismiss();
}

//==============================================================================
void RoutingComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));
}

void RoutingComponent::resized()
{
    auto area = getLocalBounds().reduced(10);
    const int rowH    = 22;
    const int labelW  = 18;
    const int spacing = 4;

    // --- Inputs ---
    inputLabel.setBounds(area.removeFromTop(rowH));
    area.removeFromTop(spacing);

    auto rowL = area.removeFromTop(rowH);
    inputLeftLabel.setBounds(rowL.removeFromLeft(labelW));
    inputLeftBox.setBounds(rowL);

    area.removeFromTop(spacing);

    auto rowR = area.removeFromTop(rowH);
    inputRightLabel.setBounds(rowR.removeFromLeft(labelW));
    inputRightBox.setBounds(rowR);

    area.removeFromTop(8);

    // --- Outputs ---
    outputLabel.setBounds(area.removeFromTop(rowH));
    area.removeFromTop(spacing);

    auto rowOL = area.removeFromTop(rowH);
    outputLeftLabel.setBounds(rowOL.removeFromLeft(labelW));
    outputLeftBox.setBounds(rowOL);

    area.removeFromTop(spacing);

    auto rowOR = area.removeFromTop(rowH);
    outputRightLabel.setBounds(rowOR.removeFromLeft(labelW));
    outputRightBox.setBounds(rowOR);

    area.removeFromTop(8);

    // --- MIDI (VSTi only) ---
    if (isVSTiChannel)
    {
        midiLabel.setBounds(area.removeFromTop(rowH));
        area.removeFromTop(spacing);
        midiChannelBox.setBounds(area.removeFromTop(rowH));
        area.removeFromTop(8);
    }

    // --- Apply ---
    applyButton.setBounds(area.removeFromTop(rowH + 4));
}
