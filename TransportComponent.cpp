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
    
    // Initialize from engine
    bpmSlider.setValue(audioEngine.getLoopEngine().getBPM(), juce::dontSendNotification);
    beatsSlider.setValue(audioEngine.getLoopEngine().getBeatsPerLoop(), juce::dontSendNotification);
    quantizeButton.setToggleState(audioEngine.getLoopEngine().isQuantizationEnabled(), 
                                  juce::dontSendNotification);
    
    // Start timer
    startTimer(50);  // 20Hz refresh
}

TransportComponent::~TransportComponent()
{
    stopTimer();
}

//==============================================================================
void TransportComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    
    // Draw border
    g.setColour(juce::Colours::grey);
    g.drawRect(getLocalBounds(), 1);
    
    // Draw title
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
    
    // BPM control
    auto bpmArea = area.removeFromTop(30);
    bpmLabel.setBounds(bpmArea.removeFromLeft(60));
    bpmSlider.setBounds(bpmArea);
    
    // Beats control
    auto beatsArea = area.removeFromTop(30);
    beatsLabel.setBounds(beatsArea.removeFromLeft(60));
    beatsSlider.setBounds(beatsArea);
    
    // Quantize
    quantizeButton.setBounds(area.removeFromTop(30));
    
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
    
    // Update play/stop button
    bool isPlaying = audioEngine.isPlaying();
    playStopButton.setButtonText(isPlaying ? "Stop" : "Play");
    playStopButton.setColour(juce::TextButton::buttonColourId, 
                             isPlaying ? juce::Colours::red : juce::Colours::green);
    
    // Update loop length display
    double loopSeconds = loopEngine.getLoopLengthSeconds();
    loopLengthLabel.setText("Loop: " + juce::String(loopSeconds, 2) + "s", 
                           juce::dontSendNotification);
    
    // Update playhead display
    double playheadSeconds = loopEngine.getPlayheadSeconds();
    playheadLabel.setText("Pos: " + juce::String(playheadSeconds, 2) + "s", 
                         juce::dontSendNotification);
    
    // Update CPU display
    double cpuUsage = audioEngine.getCPUUsage();
    juce::String cpuText = "CPU: " + juce::String(cpuUsage, 1) + "%";
    cpuLabel.setText(cpuText, juce::dontSendNotification);
    
    // Color code CPU usage
    if (cpuUsage > 80.0)
        cpuLabel.setColour(juce::Label::textColourId, juce::Colours::red);
    else if (cpuUsage > 50.0)
        cpuLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    else
        cpuLabel.setColour(juce::Label::textColourId, juce::Colours::green);
}

//==============================================================================
void TransportComponent::playStopClicked()
{
    bool isPlaying = audioEngine.isPlaying();
    audioEngine.setPlaying(!isPlaying);
}

void TransportComponent::bpmChanged()
{
    double bpm = bpmSlider.getValue();
    
    Command cmd;
    cmd.type = CommandType::SetBPM;
    cmd.floatValue = static_cast<float>(bpm);
    audioEngine.sendCommand(cmd);
}

void TransportComponent::beatsChanged()
{
    int beats = static_cast<int>(beatsSlider.getValue());
    
    Command cmd;
    cmd.type = CommandType::SetBeatsPerLoop;
    cmd.intValue1 = beats;
    audioEngine.sendCommand(cmd);
}

void TransportComponent::quantizeChanged()
{
    bool enabled = quantizeButton.getToggleState();
    
    Command cmd;
    cmd.type = CommandType::SetQuantization;
    cmd.boolValue = enabled;
    audioEngine.sendCommand(cmd);
}
