#include "TransportComponent.h"

//==============================================================================
TransportComponent::TransportComponent(AudioEngine& engine)
    : audioEngine(engine)
{
    //--------------------------------------------------------------------------
    // Transport
    playStopButton.setClickingTogglesState(false);
    playStopButton.setTooltip("Start / stop global playback  [Space]");
    playStopButton.onClick = [this] { playStopClicked(); };
    addAndMakeVisible(playStopButton);

    // PANIC / Emergency Stop
    panicButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF8B0000));
    panicButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    panicButton.setTooltip("Emergency stop: immediately halts all channels and playback. "
                           "Loop content is preserved.");
    panicButton.onClick = [this] { audioEngine.emergencyStop(); };
    addAndMakeVisible(panicButton);

    //--------------------------------------------------------------------------
    // Channel navigation
    prevChannelButton.setTooltip("Previous channel  [<-]");
    prevChannelButton.onClick = [this] { prevChannelClicked(); };
    addAndMakeVisible(prevChannelButton);

    nextChannelButton.setTooltip("Next channel  [->]");
    nextChannelButton.onClick = [this] { nextChannelClicked(); };
    addAndMakeVisible(nextChannelButton);

    activeChannelLabel.setJustificationType(juce::Justification::centred);
    activeChannelLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    addAndMakeVisible(activeChannelLabel);

    // Global MIDI learn
    midiLearnButton.onClick = [this] { showGlobalMidiLearnMenu(); };
    midiLearnButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF004488));
    addAndMakeVisible(midiLearnButton);

    //--------------------------------------------------------------------------
    // Overdub mode toggle
    overdubButton.setToggleState(audioEngine.isInOverdubMode(), juce::dontSendNotification);
    overdubButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::orange);
    overdubButton.setTooltip("Overdub Mode: pressing a channel button while playing starts overdub "
                             "instead of stopping.  [O]");
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
    // Beats per loop
    beatsLabel.setText("Loop:", juce::dontSendNotification);
    beatsLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(beatsLabel);

    beatsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    beatsSlider.setRange(1.0, 32.0, 1.0);
    beatsSlider.setValue(audioEngine.getLoopEngine().getBeatsPerLoop(), juce::dontSendNotification);
    beatsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
    beatsSlider.onValueChange = [this] { beatsChanged(); };
    addAndMakeVisible(beatsSlider);

    //--------------------------------------------------------------------------
    // Latch Mode
    latchModeButton.setToggleState(audioEngine.isLatchMode(), juce::dontSendNotification);
    latchModeButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
    latchModeButton.setTooltip(
        "Latch Mode: REC / PLAY / STOP commands take effect at the next loop boundary, "
        "not immediately. Prevents hand-timing errors when triggering manually.  [L]");
    latchModeButton.onClick = [this] { latchModeChanged(); };
    addAndMakeVisible(latchModeButton);

    //--------------------------------------------------------------------------
    // Metronome enable
    metronomeButton.setToggleState(audioEngine.getMetronome().getEnabled(),
                                   juce::dontSendNotification);
    metronomeButton.setTooltip(
        "Metronome mode: loop length is fixed by BPM x Beats. "
        "Disable for free-form looping (first recording sets the loop length).");
    metronomeButton.onClick = [this] { metronomeChanged(); };
    addAndMakeVisible(metronomeButton);

    // Metronome mute
    metronomeMuteButton.setToggleState(audioEngine.getMetronome().getMuted(),
                                       juce::dontSendNotification);
    metronomeMuteButton.setTooltip(
        "Mute Click: silences the metronome click sound while the timing grid stays active.");
    metronomeMuteButton.onClick = [this] { metronomeMuteChanged(); };
    addAndMakeVisible(metronomeMuteButton);

    //--------------------------------------------------------------------------
    // Beats per bar
    beatsPerBarLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(beatsPerBarLabel);

    beatsPerBarSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    beatsPerBarSlider.setRange(1.0, 16.0, 1.0);
    beatsPerBarSlider.setValue(4.0, juce::dontSendNotification);
    beatsPerBarSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 30, 20);
    beatsPerBarSlider.setTooltip("Beats per bar: determines bar boundaries for the accent beat "
                                  "and bar-based count-in length.");
    beatsPerBarSlider.onValueChange = [this] { beatsPerBarChanged(); };
    addAndMakeVisible(beatsPerBarSlider);

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
    autoStartButton.setTooltip(
        "Auto Start: recording begins automatically when input level exceeds the threshold. "
        "No need to press Play first.");
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

    countInBox.addItem("Off",    1);   // 0 bars
    countInBox.addItem("1 Bar",  2);   // 1 bar
    countInBox.addItem("2 Bars", 3);   // 2 bars
    countInBox.addItem("4 Bars", 4);   // 4 bars
    countInBox.setSelectedId(1, juce::dontSendNotification);
    countInBox.setTooltip(
        "Count In: plays N full bars of metronome clicks before recording begins. "
        "Bar length = Beats/Bar setting. Requires Metronome to be enabled.");
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

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText("Transport", 8, 4, 200, 20, juce::Justification::centredLeft);

    // Section headers: horizontal rule with label badge on the left
    const int  margin      = 8;
    const int  headerH     = 14;
    const auto ruleColour  = juce::Colour(0xFF555555);
    const auto bgColour    = getLookAndFeel()
                                 .findColour(juce::ResizableWindow::backgroundColourId);

    g.setFont(juce::Font(9.5f, juce::Font::bold));

    for (const auto& hdr : sectionHeaders)
    {
        const int ruleY = hdr.y + headerH / 2;

        // Full-width rule
        g.setColour(ruleColour);
        g.drawHorizontalLine(ruleY, (float)margin, (float)(getWidth() - margin));

        // Badge: fill bg behind text so rule is "broken"
        const int textW = g.getCurrentFont().getStringWidth(hdr.label) + 8;
        const juce::Rectangle<int> badge (margin + 4, hdr.y, textW, headerH);
        g.setColour(bgColour);
        g.fillRect(badge);

        // Label text
        g.setColour(ruleColour);
        g.drawText(hdr.label, badge, juce::Justification::centredLeft, false);
    }
}

void TransportComponent::resized()
{
    sectionHeaders.clear();
    auto area = getLocalBounds().reduced(8);
    area.removeFromTop(24);  // title

    const int  hdrH = 14;
    const int  hdrGap = 3;
    const auto addSection = [&](const char* label)
    {
        sectionHeaders.add({ area.getY(), label });
        area.removeFromTop(hdrH + hdrGap);
    };

    // ── PLAYBACK ──────────────────────────────────────────────────────────────
    addSection("PLAYBACK");
    {
        auto row = area.removeFromTop(36);
        panicButton   .setBounds(row.removeFromRight(82).reduced(0, 4));
        playStopButton.setBounds(row.reduced(0, 4));
    }
    area.removeFromTop(6);

    // ── ACTIVE CHANNEL ────────────────────────────────────────────────────────
    addSection("ACTIVE CHANNEL");
    {
        auto row = area.removeFromTop(26);
        prevChannelButton .setBounds(row.removeFromLeft(36).reduced(1));
        nextChannelButton .setBounds(row.removeFromRight(36).reduced(1));
        activeChannelLabel.setBounds(row.reduced(2));
    }
    area.removeFromTop(6);

    // ── CLOCK ─────────────────────────────────────────────────────────────────
    addSection("CLOCK");
    metronomeButton    .setBounds(area.removeFromTop(26));
    area.removeFromTop(4);
    metronomeMuteButton.setBounds(area.removeFromTop(26));
    area.removeFromTop(4);
    {
        auto row = area.removeFromTop(26);
        bpmLabel .setBounds(row.removeFromLeft(55));
        bpmSlider.setBounds(row);
    }
    {
        auto row = area.removeFromTop(26);
        beatsLabel .setBounds(row.removeFromLeft(55));
        beatsSlider.setBounds(row);
    }
    {
        auto row = area.removeFromTop(26);
        beatsPerBarLabel .setBounds(row.removeFromLeft(55));
        beatsPerBarSlider.setBounds(row);
    }
    {
        auto row = area.removeFromTop(26);
        metroOutLabel .setBounds(row.removeFromLeft(80));
        metroOutputBox.setBounds(row);
    }
    {
        auto row = area.removeFromTop(26);
        countInLabel.setBounds(row.removeFromLeft(72));
        countInBox  .setBounds(row);
    }
    area.removeFromTop(6);

    // ── RECORDING ─────────────────────────────────────────────────────────────
    addSection("RECORDING");
    overdubButton  .setBounds(area.removeFromTop(24));
    area.removeFromTop(4);
    latchModeButton.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);
    autoStartButton.setBounds(area.removeFromTop(24));
    {
        auto row = area.removeFromTop(26);
        autoStartThreshLabel.setBounds(row.removeFromLeft(72));
        autoStartSlider     .setBounds(row);
    }
    area.removeFromTop(6);

    // ── UTILITY ───────────────────────────────────────────────────────────────
    addSection("UTILITY");
    {
        auto row = area.removeFromTop(30);
        const int half = row.getWidth() / 2;
        midiLearnButton .setBounds(row.removeFromLeft(half).reduced(2, 3));
        resetSongButton .setBounds(row                    .reduced(2, 3));
    }
    area.removeFromTop(6);

    // ── STATUS ────────────────────────────────────────────────────────────────
    addSection("STATUS");
    modeLabel      .setBounds(area.removeFromTop(22));
    loopLengthLabel.setBounds(area.removeFromTop(22));
    playheadLabel  .setBounds(area.removeFromTop(22));
    cpuLabel       .setBounds(area.removeFromTop(22));
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

    bpmSlider        .setEnabled(!playing);
    beatsSlider      .setEnabled(!playing);
    beatsPerBarSlider.setEnabled(!playing);

    overdubButton  .setToggleState(audioEngine.isInOverdubMode(), juce::dontSendNotification);
    latchModeButton.setToggleState(audioEngine.isLatchMode(),     juce::dontSendNotification);

    // Active channel display
    activeChannelLabel.setText("Active: Ch" + juce::String(audioEngine.getActiveChannel() + 1),
                               juce::dontSendNotification);

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

    // MIDI Learn visual feedback — blink "MIDI WAIT" while waiting for a CC/note
    const bool learning = audioEngine.getMidiLearnManager().isLearning();
    if (learning)
    {
        const bool blinkOn = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
        midiLearnButton.setButtonText(blinkOn ? "MIDI WAIT" : "MIDI...");
        midiLearnButton.setColour(juce::TextButton::buttonColourId,
                                  blinkOn ? juce::Colour(0xFF0066CC)
                                          : juce::Colour(0xFF004488));
    }
    else
    {
        midiLearnButton.setButtonText("MIDI");
        midiLearnButton.setColour(juce::TextButton::buttonColourId,
                                  juce::Colour(0xFF004488));
    }
}

void TransportComponent::updateMetronomeButtonStates()
{
    const bool hasRecordings = audioEngine.hasAnyRecordings();
    const bool metroEnabled  = audioEngine.getMetronome().getEnabled();

    // Metronom-Toggle: gesperrt wenn Aufnahmen vorhanden
    metronomeButton.setEnabled(!hasRecordings);
    metronomeButton.setToggleState(metroEnabled, juce::dontSendNotification);

    if (hasRecordings != lastHasRecordings)
    {
        lastHasRecordings = hasRecordings;
        metronomeButton.setTooltip(hasRecordings
            ? "Cannot change mode while recordings exist - use \"Reset Song\" first."
            : "Metronome mode: loop length is fixed by BPM x Beats. "
              "Disable for free-form looping (first recording sets the loop length).");
    }

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

void TransportComponent::latchModeChanged()
{
    audioEngine.setLatchMode(latchModeButton.getToggleState());
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

void TransportComponent::beatsPerBarChanged()
{
    audioEngine.setBeatsPerBar(static_cast<int>(beatsPerBarSlider.getValue()));
    // Re-sync count-in since bars × beatsPerBar changed
    countInChanged();
}

void TransportComponent::metroOutputChanged()
{
    const int id = metroOutputBox.getSelectedId();
    if (id < 1 || id > metroOutEntries.size()) return;
    const auto& entry = metroOutEntries.getReference(id - 1);
    audioEngine.setMetronomeOutput(entry.left, entry.right);
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
    // IDs: 1=Off(0 bars), 2=1 bar, 3=2 bars, 4=4 bars
    static const int barValues[] = { 0, 1, 2, 4 };
    const int id = countInBox.getSelectedId();
    if (id < 1 || id > 4) return;
    const int bars  = barValues[id - 1];
    const int beats = bars * audioEngine.getBeatsPerBar();
    audioEngine.setCountInBeats(beats);
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

void TransportComponent::prevChannelClicked()
{
    audioEngine.prevChannel();
}

void TransportComponent::nextChannelClicked()
{
    audioEngine.nextChannel();
}

void TransportComponent::showGlobalMidiLearnMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "MIDI Learn: Global Play/Stop");
    menu.addItem(2, "MIDI Learn: Panic");
    menu.addItem(3, "MIDI Learn: Metronome On/Off");
    menu.addItem(4, "MIDI Learn: Overdub Mode On/Off");
    menu.addItem(5, "MIDI Learn: Latch Mode On/Off");
    menu.addItem(6, "MIDI Learn: Auto Start On/Off");
    menu.addSeparator();
    menu.addItem(7, "MIDI Learn: Next Channel");
    menu.addItem(8, "MIDI Learn: Prev Channel");
    menu.addItem(9, "MIDI Learn: Next Song");
    menu.addItem(10, "MIDI Learn: Prev Song");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int id)
    {
        if (id <= 0) return;
        const MidiControlTarget targets[] = {
            MidiControlTarget::GlobalPlayStop,
            MidiControlTarget::Panic,
            MidiControlTarget::MetronomeToggle,
            MidiControlTarget::GlobalOverdubToggle,
            MidiControlTarget::LatchModeToggle,
            MidiControlTarget::AutoStartToggle,
            MidiControlTarget::NextChannel,
            MidiControlTarget::PrevChannel,
            MidiControlTarget::NextSong,
            MidiControlTarget::PrevSong
        };
        if (id >= 1 && id <= (int)std::size(targets))
            audioEngine.getMidiLearnManager().startLearning(-1, targets[id - 1]);
    });
}

void TransportComponent::populateMetroOutputBox()
{
    // Remember current routing so we can restore the selection after repopulating.
    const int curL = audioEngine.getMetronome().getOutputLeft();
    const int curR = audioEngine.getMetronome().getOutputRight();

    metroOutEntries.clear();
    metroOutputBox.clear(juce::dontSendNotification);

    // getNumOutputChannels() is correctly set after initialiseAudio()
    const int numOut = juce::jmax(2, audioEngine.getNumOutputChannels());
    int id        = 1;
    int selectedId = 1;  // fallback: first entry

    // --- Stereo pairs ---
    for (int i = 0; i + 1 < numOut; i += 2)
    {
        metroOutEntries.add({ i, i + 1 });
        metroOutputBox.addItem("Out " + juce::String(i + 1) + "/" + juce::String(i + 2), id);
        if (i == curL && i + 1 == curR)
            selectedId = id;
        ++id;
    }

    // --- Individual channels (mono: L = R) ---
    for (int i = 0; i < numOut; ++i)
    {
        metroOutEntries.add({ i, i });
        metroOutputBox.addItem("Out " + juce::String(i + 1) + " (mono)", id);
        if (i == curL && i == curR)
            selectedId = id;
        ++id;
    }

    metroOutputBox.setSelectedId(selectedId, juce::dontSendNotification);
}
