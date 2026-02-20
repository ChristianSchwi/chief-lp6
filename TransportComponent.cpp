#include "TransportComponent.h"

//==============================================================================
TransportComponent::TransportComponent(AudioEngine& engine)
    : audioEngine(engine)
{
    // Play/Stop button
    playStopButton.setClickingTogglesState(false);
    playStopButton.onClick = [this] { playStopClicked(); };
    addAndMakeVisible(playStopButton);

    // BPM control
    bpmLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(bpmLabel);

    bpmSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider.setRange(40.0, 240.0, 0.1);
    bpmSlider.setValue(120.0);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    bpmSlider.onValueChange = [this] { bpmChanged(); };
    addAndMakeVisible(bpmSlider);

    // Beats per loop
    beatsLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(beatsLabel);

    beatsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    beatsSlider.setRange(1.0, 32.0, 1.0);
    beatsSlider.setValue(4.0);
    beatsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    beatsSlider.onValueChange = [this] { beatsChanged(); };
    addAndMakeVisible(beatsSlider);

    // Quantize toggle
    quantizeButton.setToggleState(true, juce::dontSendNotification);
    quantizeButton.onClick = [this] { quantizeChanged(); };
    addAndMakeVisible(quantizeButton);

    // ---- Metronome section (NEW) ----
    metronomeButton.setToggleState(false, juce::dontSendNotification);
    metronomeButton.onClick = [this] { metronomeChanged(); };
    addAndMakeVisible(metronomeButton);

    metroOutLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(metroOutLabel);

    metroOutputBox.onChange = [this] { metroOutputChanged(); };
    addAndMakeVisible(metroOutputBox);
    populateMetroOutputBox();
    // ---------------------------------

    // Display labels
    loopLengthLabel.setJustificationType(juce::Justification::centredLeft);
    loopLengthLabel.setFont(juce::Font(16.0f));
    addAndMakeVisible(loopLengthLabel);

    playheadLabel.setJustificationType(juce::Justification::centredLeft);
    playheadLabel.setFont(juce::Font(16.0f));
    addAndMakeVisible(playheadLabel);

    cpuLabel.setJustificationType(juce::Justification::centredRight);
    cpuLabel.setFont(juce::Font(14.0f));
    addAndMakeVisible(cpuLabel);

    // Initialize controls from engine state
    bpmSlider.setValue(audioEngine.getLoopEngine().getBPM(), juce::dontSendNotification);
    beatsSlider.setValue(audioEngine.getLoopEngine().getBeatsPerLoop(), juce::dontSendNotification);
    quantizeButton.setToggleState(audioEngine.getLoopEngine().isQuantizationEnabled(),
                                  juce::dontSendNotification);
    metronomeButton.setToggleState(audioEngine.getMetronome().getEnabled(),
                                   juce::dontSendNotification);

    startTimer(50);  // 20 Hz refresh
}

TransportComponent::~TransportComponent()
{
    stopTimer();
}

//==============================================================================
void TransportComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    g.setColour(juce::Colours::grey);
    g.drawRect(getLocalBounds(), 1);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(18.0f, juce::Font::bold));
    g.drawText("Transport", 10, 5, 200, 20, juce::Justification::centredLeft);
}

void TransportComponent::resized()
{
    auto area = getLocalBounds().reduced(10);

    // Title space
    area.removeFromTop(25);

    // Play/Stop button (large)
    playStopButton.setBounds(area.removeFromTop(40).reduced(0, 5));

    area.removeFromTop(10);

    // BPM
    auto bpmArea = area.removeFromTop(30);
    bpmLabel.setBounds(bpmArea.removeFromLeft(60));
    bpmSlider.setBounds(bpmArea);

    // Beats
    auto beatsArea = area.removeFromTop(30);
    beatsLabel.setBounds(beatsArea.removeFromLeft(60));
    beatsSlider.setBounds(beatsArea);

    // Quantize
    quantizeButton.setBounds(area.removeFromTop(30));

    area.removeFromTop(8);

    // ---- Metronome section ----
    metronomeButton.setBounds(area.removeFromTop(28));

    auto metroOutRow = area.removeFromTop(28);
    metroOutLabel.setBounds(metroOutRow.removeFromLeft(80));
    metroOutputBox.setBounds(metroOutRow);
    // ---------------------------

    area.removeFromTop(10);

    // Display labels
    loopLengthLabel.setBounds(area.removeFromTop(25));
    playheadLabel.setBounds(area.removeFromTop(25));

    // CPU at bottom
    cpuLabel.setBounds(area.removeFromBottom(25));
}

//==============================================================================
void TransportComponent::timerCallback()
{
    updateDisplay();
}

void TransportComponent::updateDisplay()
{
    auto& loopEngine = audioEngine.getLoopEngine();

    // Play/Stop button
    const bool isPlaying = audioEngine.isPlaying();
    playStopButton.setButtonText(isPlaying ? "Stop" : "Play");
    playStopButton.setColour(juce::TextButton::buttonColourId,
                             isPlaying ? juce::Colours::red : juce::Colours::green);

    // Loop length
    loopLengthLabel.setText("Loop: " + juce::String(loopEngine.getLoopLengthSeconds(), 2) + "s",
                            juce::dontSendNotification);

    // Playhead
    playheadLabel.setText("Pos: " + juce::String(loopEngine.getPlayheadSeconds(), 2) + "s",
                          juce::dontSendNotification);

    // CPU — colour-coded
    const double cpu = audioEngine.getCPUUsage();
    cpuLabel.setText("CPU: " + juce::String(cpu, 1) + "%", juce::dontSendNotification);

    if (cpu > 80.0)
        cpuLabel.setColour(juce::Label::textColourId, juce::Colours::red);
    else if (cpu > 50.0)
        cpuLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    else
        cpuLabel.setColour(juce::Label::textColourId, juce::Colours::green);
}

//==============================================================================
void TransportComponent::playStopClicked()
{
    audioEngine.setPlaying(!audioEngine.isPlaying());
}

void TransportComponent::bpmChanged()
{
    const double bpm = bpmSlider.getValue();

    // BPM change is only allowed while stopped (spec requirement).
    // We still send the command; AudioEngine/LoopEngine enforce the restriction.
    Command cmd;
    cmd.type       = CommandType::SetBPM;
    cmd.floatValue = static_cast<float>(bpm);
    audioEngine.sendCommand(cmd);

    // Keep metronome in sync
    audioEngine.getMetronome().setBPM(bpm);
}

void TransportComponent::beatsChanged()
{
    Command cmd;
    cmd.type      = CommandType::SetBeatsPerLoop;
    cmd.intValue1 = static_cast<int>(beatsSlider.getValue());
    audioEngine.sendCommand(cmd);
}

void TransportComponent::quantizeChanged()
{
    Command cmd;
    cmd.type      = CommandType::SetQuantization;
    cmd.boolValue = quantizeButton.getToggleState();
    audioEngine.sendCommand(cmd);
}

//==============================================================================
// Metronome handlers (NEW)
//==============================================================================

void TransportComponent::metronomeChanged()
{
    audioEngine.setMetronomeEnabled(metronomeButton.getToggleState());
}

void TransportComponent::metroOutputChanged()
{
    // Item IDs are 1-based; each item represents a stereo pair.
    // ID 1 → Out 1/2 (left=0, right=1), ID 2 → Out 3/4 (left=2, right=3), …
    const int id = metroOutputBox.getSelectedId();
    if (id <= 0)
        return;

    const int leftChannel  = (id - 1) * 2;
    const int rightChannel = leftChannel + 1;

    audioEngine.setMetronomeOutput(leftChannel, rightChannel);
}

void TransportComponent::populateMetroOutputBox()
{
    metroOutputBox.clear(juce::dontSendNotification);

    // Populate with stereo output pairs based on available outputs.
    // If the engine is not yet initialised we default to 2 outputs (1 pair).
    const int numOut = juce::jmax(2, audioEngine.getNumOutputChannels());

    for (int i = 0; i < numOut; i += 2)
    {
        juce::String label = "Out " + juce::String(i + 1) +
                             "/" + juce::String(i + 2);
        metroOutputBox.addItem(label, (i / 2) + 1);  // ID starts at 1
    }

    metroOutputBox.setSelectedId(1, juce::dontSendNotification);
}
