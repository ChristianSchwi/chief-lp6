#include "TransportComponent.h"

//==============================================================================
TransportComponent::TransportComponent(AudioEngine& engine)
    : audioEngine(engine)
{
    //--------------------------------------------------------------------------
    // Transport
    playStopButton.setClickingTogglesState(false);
    playStopButton.onClick = [this] { playStopClicked(); };
    addAndMakeVisible(playStopButton);

    //--------------------------------------------------------------------------
    // Overdub mode toggle
    overdubButton.setToggleState(audioEngine.isInOverdubMode(), juce::dontSendNotification);
    overdubButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::orange);
    overdubButton.onClick = [this] { overdubModeChanged(); };
    addAndMakeVisible(overdubButton);

    //--------------------------------------------------------------------------
    // BPM
    bpmLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(bpmLabel);

    bpmSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider.setRange(40.0, 240.0, 0.1);
    bpmSlider.setValue(audioEngine.getLoopEngine().getBPM(), juce::dontSendNotification);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
    bpmSlider.onValueChange = [this] { bpmChanged(); };
    addAndMakeVisible(bpmSlider);

    //--------------------------------------------------------------------------
    // Beats
    beatsLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(beatsLabel);

    beatsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    beatsSlider.setRange(1.0, 32.0, 1.0);
    beatsSlider.setValue(audioEngine.getLoopEngine().getBeatsPerLoop(), juce::dontSendNotification);
    beatsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
    beatsSlider.onValueChange = [this] { beatsChanged(); };
    addAndMakeVisible(beatsSlider);

    //--------------------------------------------------------------------------
    // Quantize
    quantizeButton.setToggleState(audioEngine.getLoopEngine().isQuantizationEnabled(),
                                  juce::dontSendNotification);
    quantizeButton.onClick = [this] { quantizeChanged(); };
    addAndMakeVisible(quantizeButton);

    //--------------------------------------------------------------------------
    // Metronome enable
    metronomeButton.setToggleState(audioEngine.getMetronome().getEnabled(),
                                   juce::dontSendNotification);
    metronomeButton.onClick = [this] { metronomeChanged(); };
    addAndMakeVisible(metronomeButton);

    // Metronome mute
    metronomeMuteButton.setToggleState(audioEngine.getMetronome().getMuted(),
                                       juce::dontSendNotification);
    metronomeMuteButton.onClick = [this] { metronomeMuteChanged(); };
    addAndMakeVisible(metronomeMuteButton);

    // Output selector
    metroOutLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(metroOutLabel);
    metroOutputBox.onChange = [this] { metroOutputChanged(); };
    addAndMakeVisible(metroOutputBox);
    // FIX: populateMetroOutputBox() wird NICHT hier aufgerufen —
    //      Audio ist zu diesem Zeitpunkt noch nicht initialisiert.
    //      Stattdessen: refreshAfterAudioInit() aus MainComponent aufrufen.

    //--------------------------------------------------------------------------
    // Auto-Start
    autoStartButton.setToggleState(false, juce::dontSendNotification);
    autoStartButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
    autoStartButton.onClick = [this] { autoStartChanged(); };
    addAndMakeVisible(autoStartButton);

    autoStartThreshLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(autoStartThreshLabel);

    autoStartSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    autoStartSlider.setRange(-60.0, 0.0, 0.5);
    autoStartSlider.setValue(-30.0, juce::dontSendNotification);
    autoStartSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    autoStartSlider.setTextValueSuffix(" dB");
    autoStartSlider.onValueChange = [this] { autoStartThresholdChanged(); };
    addAndMakeVisible(autoStartSlider);

    //--------------------------------------------------------------------------
    // Count-In
    countInLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(countInLabel);

    countInBox.addItem("0 (Off)", 1);
    for (int i = 1; i <= 16; ++i)
        countInBox.addItem(juce::String(i) + (i == 1 ? " beat" : " beats"), i + 1);
    countInBox.setSelectedId(1, juce::dontSendNotification);
    countInBox.onChange = [this] { countInChanged(); };
    addAndMakeVisible(countInBox);

    //--------------------------------------------------------------------------
    // Reset Song
    resetSongButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
    resetSongButton.onClick = [this] { resetSongClicked(); };
    addAndMakeVisible(resetSongButton);

    //--------------------------------------------------------------------------
    // Display labels
    modeLabel      .setFont(juce::Font(14.0f, juce::Font::bold));
    loopLengthLabel.setFont(juce::Font(14.0f));
    playheadLabel  .setFont(juce::Font(14.0f));
    cpuLabel       .setFont(juce::Font(14.0f));
    cpuLabel       .setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(modeLabel);
    addAndMakeVisible(loopLengthLabel);
    addAndMakeVisible(playheadLabel);
    addAndMakeVisible(cpuLabel);

    updateMetronomeButtonStates();
    startTimer(50);  // 20 Hz
}

TransportComponent::~TransportComponent()
{
    stopTimer();
}

//==============================================================================
void TransportComponent::refreshAfterAudioInit()
{
    // Jetzt kennt die AudioEngine die tatsächliche Ausgangskanal-Anzahl
    populateMetroOutputBox();
}

//==============================================================================
void TransportComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::grey);
    g.drawRect(getLocalBounds(), 1);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText("Transport", 8, 4, 200, 20, juce::Justification::centredLeft);
}

void TransportComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    area.removeFromTop(24); // Titel

    // Play/Stop
    playStopButton.setBounds(area.removeFromTop(36).reduced(0, 4));
    area.removeFromTop(4);

    // Overdub mode
    overdubButton.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);

    // Auto-Start
    autoStartButton.setBounds(area.removeFromTop(24));
    {
        auto row = area.removeFromTop(26);
        autoStartThreshLabel.setBounds(row.removeFromLeft(72));
        autoStartSlider.setBounds(row);
    }
    area.removeFromTop(6);

    // Count-In
    {
        auto row = area.removeFromTop(26);
        countInLabel.setBounds(row.removeFromLeft(72));
        countInBox.setBounds(row);
    }
    area.removeFromTop(6);

    // BPM
    auto row = area.removeFromTop(28);
    bpmLabel .setBounds(row.removeFromLeft(55));
    bpmSlider.setBounds(row);

    // Beats
    row = area.removeFromTop(28);
    beatsLabel .setBounds(row.removeFromLeft(55));
    beatsSlider.setBounds(row);

    // Quantize
    quantizeButton.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);

    // ---- Metronome section ----
    metronomeButton    .setBounds(area.removeFromTop(26));
    metronomeMuteButton.setBounds(area.removeFromTop(26));

    row = area.removeFromTop(26);
    metroOutLabel .setBounds(row.removeFromLeft(80));
    metroOutputBox.setBounds(row);
    area.removeFromTop(6);

    // ---- Reset Song ----
    resetSongButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);

    // ---- Display ----
    cpuLabel       .setBounds(area.removeFromBottom(22));
    modeLabel      .setBounds(area.removeFromTop(22));
    loopLengthLabel.setBounds(area.removeFromTop(22));
    playheadLabel  .setBounds(area.removeFromTop(22));
}

//==============================================================================
// Timer (20 Hz)
//==============================================================================

void TransportComponent::timerCallback()
{
    updateDisplay();
    updateMetronomeButtonStates();
}

void TransportComponent::updateDisplay()
{
    auto& le = audioEngine.getLoopEngine();

    // Play/Stop
    const bool playing = audioEngine.isPlaying();
    playStopButton.setButtonText(playing ? "Stop" : "Play");
    playStopButton.setColour(juce::TextButton::buttonColourId,
                             playing ? juce::Colours::red : juce::Colours::green);

    bpmSlider  .setEnabled(!playing);
    beatsSlider.setEnabled(!playing);

    overdubButton.setToggleState(audioEngine.isInOverdubMode(), juce::dontSendNotification);

    // Modus-Anzeige
    const bool metroActive = audioEngine.getMetronome().getEnabled();
    modeLabel.setText("Mode: " + juce::String(metroActive ? "Metronome" : "Free"),
                      juce::dontSendNotification);
    modeLabel.setColour(juce::Label::textColourId,
                        metroActive ? juce::Colours::yellow : juce::Colours::lightblue);

    // Loop-Länge
    const juce::int64 loopLen = le.getLoopLength();
    loopLengthLabel.setText(
        loopLen > 0 ? "Loop: " + juce::String(le.getLoopLengthSeconds(), 2) + "s"
                    : "Loop: ---",
        juce::dontSendNotification);

    // Playhead
    playheadLabel.setText("Pos:  " + juce::String(le.getPlayheadSeconds(), 2) + "s",
                          juce::dontSendNotification);

    // CPU (farbkodiert)
    const double cpu = audioEngine.getCPUUsage();
    cpuLabel.setText("CPU: " + juce::String(cpu, 1) + "%", juce::dontSendNotification);
    cpuLabel.setColour(juce::Label::textColourId,
                       cpu > 80.0 ? juce::Colours::red    :
                       cpu > 50.0 ? juce::Colours::orange :
                                    juce::Colours::lightgreen);
}

void TransportComponent::updateMetronomeButtonStates()
{
    const bool hasRecordings = audioEngine.hasAnyRecordings();
    const bool metroEnabled  = audioEngine.getMetronome().getEnabled();

    // Metronom-Toggle: gesperrt wenn Aufnahmen vorhanden
    metronomeButton.setEnabled(!hasRecordings);
    metronomeButton.setToggleState(metroEnabled, juce::dontSendNotification);

    metronomeButton.setTooltip(hasRecordings
        ? "Nicht schaltbar solange Aufnahmen vorhanden. Zuerst \"Reset Song\"."
        : "");

    // Mute-Button: nur relevant wenn Metronom an
    metronomeMuteButton.setEnabled(metroEnabled);
    metronomeMuteButton.setToggleState(audioEngine.getMetronome().getMuted(),
                                       juce::dontSendNotification);
}

//==============================================================================
// Handlers
//==============================================================================

void TransportComponent::playStopClicked()
{
    audioEngine.setPlaying(!audioEngine.isPlaying());
}

void TransportComponent::overdubModeChanged()
{
    audioEngine.setOverdubMode(overdubButton.getToggleState());
}

void TransportComponent::bpmChanged()
{
    Command cmd;
    cmd.type       = CommandType::SetBPM;
    cmd.floatValue = static_cast<float>(bpmSlider.getValue());
    audioEngine.sendCommand(cmd);
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

void TransportComponent::metronomeChanged()
{
    // Doppelte Absicherung auf Message-Thread
    // (Button ist bereits disabled, aber defensiv trotzdem prüfen)
    if (audioEngine.hasAnyRecordings())
    {
        // Toggle-State zurücksetzen — User-Click rückgängig machen
        metronomeButton.setToggleState(!metronomeButton.getToggleState(),
                                       juce::dontSendNotification);
        return;
    }
    audioEngine.setMetronomeEnabled(metronomeButton.getToggleState());
}

void TransportComponent::metronomeMuteChanged()
{
    audioEngine.setMetronomeMuted(metronomeMuteButton.getToggleState());
}

void TransportComponent::metroOutputChanged()
{
    const int id = metroOutputBox.getSelectedId();
    if (id <= 0) return;
    const int L = (id - 1) * 2;
    audioEngine.setMetronomeOutput(L, L + 1);
}

void TransportComponent::autoStartChanged()
{
    audioEngine.setAutoStart(autoStartButton.getToggleState(),
                             static_cast<float>(autoStartSlider.getValue()));
}

void TransportComponent::autoStartThresholdChanged()
{
    audioEngine.setAutoStart(autoStartButton.getToggleState(),
                             static_cast<float>(autoStartSlider.getValue()));
}

void TransportComponent::countInChanged()
{
    // selectedId: 1 → 0 beats, 2 → 1 beat, ..., 17 → 16 beats
    audioEngine.setCountInBeats(countInBox.getSelectedId() - 1);
}

void TransportComponent::resetSongClicked()
{
    // FIX: showAsync statt deprecated showOkCancelBox + ModalCallbackFunction
    auto options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("Reset Song")
        .withMessage("Alle Aufnahmen loeschen und Loop zuruecksetzen?")
        .withButton("Reset")
        .withButton("Abbrechen");

    juce::AlertWindow::showAsync(options, [this](int result)
    {
        if (result == 1)  // "Reset" gedrückt
        {
            audioEngine.setPlaying(false);
            audioEngine.resetSong();
        }
    });
}

void TransportComponent::populateMetroOutputBox()
{
    metroOutputBox.clear(juce::dontSendNotification);

    // getNumOutputChannels() ist nach initialiseAudio() korrekt befüllt
    const int numOut = juce::jmax(2, audioEngine.getNumOutputChannels());
    for (int i = 0; i < numOut; i += 2)
        metroOutputBox.addItem("Out " + juce::String(i + 1) + "/" + juce::String(i + 2),
                               (i / 2) + 1);

    metroOutputBox.setSelectedId(1, juce::dontSendNotification);
}
