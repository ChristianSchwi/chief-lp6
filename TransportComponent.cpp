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
    // Master volume
    masterGainLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(masterGainLabel);

    masterGainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    masterGainSlider.setRange(0.0, 1.0, 0.01);
    masterGainSlider.setValue(1.0, juce::dontSendNotification);
    masterGainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    masterGainSlider.setTooltip("Master output volume (0 = silent, 1 = full).");
    masterGainSlider.onValueChange = [this] { masterGainChanged(); };
    addAndMakeVisible(masterGainSlider);

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
    midiLearnButton.onClick = [this] { showMidiButtonMenu(); };
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

    bpmEditor.setInputRestrictions(6, "0123456789.");
    bpmEditor.setText(juce::String(audioEngine.getLoopEngine().getBPM(), 1), juce::dontSendNotification);
    bpmEditor.setJustification(juce::Justification::centred);
    bpmEditor.onReturnKey = [this] { bpmChanged(); };
    bpmEditor.onFocusLost = [this] { bpmChanged(); };
    addAndMakeVisible(bpmEditor);

    //--------------------------------------------------------------------------
    // Tap tempo
    tapButton.setTooltip("Tap repeatedly to set BPM. Sequence resets after 3 s of silence.");
    tapButton.onClick = [this] { tapClicked(); };
    addAndMakeVisible(tapButton);

    // Wire MIDI tap-tempo callback
    audioEngine.getMidiLearnManager().onTapTempo = [this] { tapClicked(); };

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
        "Metronome mode: the first recording sets the loop length, rounded to the nearest bar "
        "(bar = Beats per bar x BPM). "
        "Disable for free-form looping (first recording sets the length exactly).");
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

    // Volume
    metroGainLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(metroGainLabel);

    metroGainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    metroGainSlider.setRange(0.0, 1.0, 0.01);
    metroGainSlider.setValue(1.0, juce::dontSendNotification);
    metroGainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    metroGainSlider.setTooltip("Metronome click volume (0 = silent, 1 = full).");
    metroGainSlider.onValueChange = [this] { metroGainChanged(); };
    addAndMakeVisible(metroGainSlider);
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
    // Fixed-Length Recording
    fixedLenLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(fixedLenLabel);

    fixedLenEditor.setInputRestrictions(4, "0123456789");
    fixedLenEditor.setText("0", juce::dontSendNotification);
    fixedLenEditor.setJustification(juce::Justification::centred);
    fixedLenEditor.setTooltip("Auto-stop recording after this many bars (0 = off, metronome mode only).");
    fixedLenEditor.onReturnKey  = [this] { applyFixedLenEditor(); };
    fixedLenEditor.onFocusLost  = [this] { applyFixedLenEditor(); };
    addAndMakeVisible(fixedLenEditor);

    fixedLenBarsLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(fixedLenBarsLabel);

    fixedLenPlusBtn.setTooltip("Increase fixed length");
    fixedLenPlusBtn.onClick = [this] { fixedLenStep(+1); };
    addAndMakeVisible(fixedLenPlusBtn);

    fixedLenMinusBtn.setTooltip("Decrease fixed length");
    fixedLenMinusBtn.onClick = [this] { fixedLenStep(-1); };
    addAndMakeVisible(fixedLenMinusBtn);

    //--------------------------------------------------------------------------
    // Double Loop
    doubleLoopButton.setTooltip("Double the loop length. Recorded content is duplicated and plays twice.");
    doubleLoopButton.onClick = [this] { audioEngine.doubleLoopLength(); };
    addAndMakeVisible(doubleLoopButton);

    //--------------------------------------------------------------------------
    // Reset Song
    resetSongButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
    resetSongButton.onClick = [this] { resetSongClicked(); };
    addAndMakeVisible(resetSongButton);

    //--------------------------------------------------------------------------
    // Mute Groups
    for (int g = 0; g < 4; ++g)
    {
        muteGroupToggleButtons[g].setButtonText("G" + juce::String(g + 1));
        muteGroupToggleButtons[g].setClickingTogglesState(false);
        muteGroupToggleButtons[g].setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
        muteGroupToggleButtons[g].setTooltip("Toggle mute group " + juce::String(g + 1));
        muteGroupToggleButtons[g].onClick = [this, g]
        {
            audioEngine.setMuteGroupActive(g, !audioEngine.isMuteGroupActive(g));
        };
        addAndMakeVisible(muteGroupToggleButtons[g]);
    }

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
    metroGainSlider.setValue(audioEngine.getMetronomeGain(), juce::dontSendNotification);
    masterGainSlider.setValue(audioEngine.getMasterGain(), juce::dontSendNotification);

    // Restore fixed-length editor from engine value
    fixedLenEditor.setText(juce::String(audioEngine.getFixedLengthBars()),
                           juce::dontSendNotification);
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
    area.removeFromTop(2);
    {
        auto row = area.removeFromTop(26);
        masterGainLabel .setBounds(row.removeFromLeft(55));
        masterGainSlider.setBounds(row);
    }
    area.removeFromTop(2);
    doubleLoopButton.setBounds(area.removeFromTop(26).reduced(0, 1));
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

    // ── METRONOME ─────────────────────────────────────────────────────────────
    addSection("METRONOME");
    {
        auto row = area.removeFromTop(26);
        const int half = row.getWidth() / 2;
        metronomeButton    .setBounds(row.removeFromLeft(half));
        metronomeMuteButton.setBounds(row);
    }
    area.removeFromTop(4);
    {
        auto row = area.removeFromTop(26);
        bpmLabel .setBounds(row.removeFromLeft(38));
        bpmEditor.setBounds(row.removeFromLeft(55).reduced(0, 3));
        row.removeFromLeft(4);
        tapButton.setBounds(row);
    }
    area.removeFromTop(2);
    {
        auto row = area.removeFromTop(26);
        beatsPerBarLabel .setBounds(row.removeFromLeft(55));
        beatsPerBarSlider.setBounds(row);
    }
    {
        auto row = area.removeFromTop(26);
        metroGainLabel .setBounds(row.removeFromLeft(55));
        metroGainSlider.setBounds(row);
    }
    {
        auto row = area.removeFromTop(26);
        countInLabel.setBounds(row.removeFromLeft(72));
        countInBox  .setBounds(row);
    }
    {
        auto row = area.removeFromTop(26);
        fixedLenLabel    .setBounds(row.removeFromLeft(55));
        fixedLenEditor   .setBounds(row.removeFromLeft(40).reduced(0, 3));
        fixedLenBarsLabel.setBounds(row.removeFromLeft(30));
        fixedLenMinusBtn .setBounds(row.removeFromLeft(24).reduced(1, 3));
        fixedLenPlusBtn  .setBounds(row.removeFromLeft(24).reduced(1, 3));
    }
    {
        auto row = area.removeFromTop(26);
        metroOutLabel .setBounds(row.removeFromLeft(80));
        metroOutputBox.setBounds(row);
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

    // ── MUTE GROUPS ──────────────────────────────────────────────────────────
    addSection("MUTE GROUPS");
    {
        auto row = area.removeFromTop(24);
        const int btnW = row.getWidth() / 4;
        for (int g = 0; g < 4; ++g)
            muteGroupToggleButtons[g].setBounds(row.removeFromLeft(btnW).reduced(1, 1));
    }
    area.removeFromTop(6);
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
    // Play/Stop
    const bool playing = audioEngine.isPlaying();
    playStopButton.setButtonText(playing ? "Stop" : "Play");
    playStopButton.setColour(juce::TextButton::buttonColourId,
                             playing ? juce::Colours::red : juce::Colours::green);

    const bool hasRec = audioEngine.hasAnyRecordings();
    bpmEditor .setEnabled(!hasRec);
    tapButton .setEnabled(!hasRec);
    beatsPerBarSlider.setEnabled(!playing);

    overdubButton  .setToggleState(audioEngine.isInOverdubMode(), juce::dontSendNotification);
    latchModeButton.setToggleState(audioEngine.isLatchMode(),     juce::dontSendNotification);

    doubleLoopButton.setEnabled(hasRec);

    // Sync master volume slider when changed via MIDI
    if (!masterGainSlider.isMouseButtonDown())
        masterGainSlider.setValue(audioEngine.getMasterGain(), juce::dontSendNotification);

    // Active channel display
    activeChannelLabel.setText("Active: Ch" + juce::String(audioEngine.getActiveChannel() + 1),
                               juce::dontSendNotification);

    // MIDI button visual feedback — blink while learning or unlearning
    auto& mlm = audioEngine.getMidiLearnManager();
    const bool learning   = mlm.isLearning();
    const bool unlearning = mlm.isUnlearning();
    if (learning)
    {
        const bool blinkOn = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
        midiLearnButton.setButtonText(blinkOn ? "MIDI WAIT" : "MIDI...");
        midiLearnButton.setColour(juce::TextButton::buttonColourId,
                                  blinkOn ? juce::Colour(0xFF0066CC)
                                          : juce::Colour(0xFF004488));
    }
    else if (unlearning)
    {
        const bool blinkOn = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
        midiLearnButton.setButtonText(blinkOn ? "UNLEARN" : "UNLRN...");
        midiLearnButton.setColour(juce::TextButton::buttonColourId,
                                  blinkOn ? juce::Colour(0xFFCC6600)
                                          : juce::Colour(0xFF884400));
    }
    else
    {
        midiLearnButton.setButtonText("MIDI");
        midiLearnButton.setColour(juce::TextButton::buttonColourId,
                                  juce::Colour(0xFF004488));
    }

    // Mute group toggle buttons
    for (int g = 0; g < 4; ++g)
        muteGroupToggleButtons[g].setToggleState(audioEngine.isMuteGroupActive(g),
                                                  juce::dontSendNotification);
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

    // Fixed-length: only active when metronome is on and no recordings yet
    const bool fixedLenActive = metroEnabled && !hasRecordings;
    fixedLenEditor  .setEnabled(fixedLenActive);
    fixedLenPlusBtn .setEnabled(fixedLenActive);
    fixedLenMinusBtn.setEnabled(fixedLenActive);
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
    const double val = juce::jlimit(40.0, 240.0, bpmEditor.getText().getDoubleValue());
    bpmEditor.setText(juce::String(val, 1), juce::dontSendNotification);
    Command cmd;
    cmd.type       = CommandType::SetBPM;
    cmd.floatValue = static_cast<float>(val);
    audioEngine.sendCommand(cmd);
}

void TransportComponent::tapClicked()
{
    const juce::int64 now = juce::Time::getMillisecondCounter();

    if (!tapTimes.empty() && (now - tapTimes.back()) > 3000)
        tapTimes.clear();

    tapTimes.push_back(now);
    if (tapTimes.size() < 2) return;

    if (tapTimes.size() > 8)
        tapTimes.erase(tapTimes.begin());

    double totalMs = 0.0;
    for (size_t i = 1; i < tapTimes.size(); ++i)
        totalMs += static_cast<double>(tapTimes[i] - tapTimes[i - 1]);

    const double avgMs  = totalMs / static_cast<double>(tapTimes.size() - 1);
    const double newBpm = juce::jlimit(40.0, 240.0, 60000.0 / avgMs);
    bpmEditor.setText(juce::String(newBpm, 1), juce::dontSendNotification);
    bpmChanged();
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

void TransportComponent::metroGainChanged()
{
    audioEngine.setMetronomeGain(static_cast<float>(metroGainSlider.getValue()));
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

void TransportComponent::applyFixedLenEditor()
{
    const int val = juce::jlimit(0, 9999, fixedLenEditor.getText().getIntValue());
    audioEngine.setFixedLengthBars(val);
    fixedLenEditor.setText(juce::String(val), juce::dontSendNotification);
}

void TransportComponent::fixedLenStep(int direction)
{
    static const int steps[] = {0, 1, 2, 4, 8, 16, 32, 64};
    static const int nSteps  = (int)std::size(steps);

    const int current = audioEngine.getFixedLengthBars();
    int newVal = current;

    if (direction > 0)
    {
        // First step strictly greater than current
        for (int i = 0; i < nSteps; ++i)
            if (steps[i] > current) { newVal = steps[i]; break; }
    }
    else
    {
        // Last step strictly less than current
        for (int i = nSteps - 1; i >= 0; --i)
            if (steps[i] < current) { newVal = steps[i]; break; }
    }

    audioEngine.setFixedLengthBars(newVal);
    fixedLenEditor.setText(juce::String(newVal), juce::dontSendNotification);
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

void TransportComponent::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isRightButtonDown())
        return;

    auto pos = e.getPosition();
    auto hit = [&](juce::Component& c) { return c.getBounds().contains(pos); };

    if      (hit(playStopButton))    showMidiContextMenu(MidiControlTarget::GlobalPlayStop);
    else if (hit(panicButton))       showMidiContextMenu(MidiControlTarget::Panic);
    else if (hit(metronomeButton))   showMidiContextMenu(MidiControlTarget::MetronomeToggle);
    else if (hit(overdubButton))     showMidiContextMenu(MidiControlTarget::GlobalOverdubToggle);
    else if (hit(latchModeButton))   showMidiContextMenu(MidiControlTarget::LatchModeToggle);
    else if (hit(autoStartButton))   showMidiContextMenu(MidiControlTarget::AutoStartToggle);
    else if (hit(prevChannelButton)) showMidiContextMenu(MidiControlTarget::PrevChannel);
    else if (hit(nextChannelButton)) showMidiContextMenu(MidiControlTarget::NextChannel);
    else if (hit(tapButton))         showMidiContextMenu(MidiControlTarget::TapTempo);
    else if (hit(masterGainSlider))  showMidiContextMenu(MidiControlTarget::MasterGain);
    else if (hit(doubleLoopButton))  showMidiContextMenu(MidiControlTarget::DoubleLoopLength);
    else if (hit(muteGroupToggleButtons[0])) showMidiContextMenu(MidiControlTarget::MuteGroupToggle1);
    else if (hit(muteGroupToggleButtons[1])) showMidiContextMenu(MidiControlTarget::MuteGroupToggle2);
    else if (hit(muteGroupToggleButtons[2])) showMidiContextMenu(MidiControlTarget::MuteGroupToggle3);
    else if (hit(muteGroupToggleButtons[3])) showMidiContextMenu(MidiControlTarget::MuteGroupToggle4);
}

void TransportComponent::showMidiContextMenu(MidiControlTarget target)
{
    auto& mlm = audioEngine.getMidiLearnManager();
    const bool hasMapping = mlm.getMapping(-1, target).isValid();
    const auto name = MidiLearnManager::targetName(target);

    juce::PopupMenu menu;
    menu.addItem(1, "MIDI Learn: " + name);
    menu.addItem(2, "Remove MIDI: " + name, hasMapping);

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, target](int id)
    {
        auto& mlm = audioEngine.getMidiLearnManager();
        if (id == 1)
            mlm.startLearning(-1, target);
        else if (id == 2)
            mlm.removeMapping(-1, target);
    });
}

void TransportComponent::showMidiButtonMenu()
{
    auto& mlm = audioEngine.getMidiLearnManager();
    const bool isLearn   = mlm.isLearning();
    const bool isUnlearn = mlm.isUnlearning();

    juce::PopupMenu menu;

    if (isLearn)
        menu.addItem(1, "Abort Learning");
    else if (isUnlearn)
        menu.addItem(2, "Abort Unlearning");

    menu.addSeparator();
    menu.addItem(4, "Remove ALL MIDI Mappings");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int id)
    {
        auto& mlm = audioEngine.getMidiLearnManager();
        if (id == 1) mlm.stopLearning();
        else if (id == 2) mlm.stopUnlearning();
        else if (id == 3) mlm.startUnlearning();
        else if (id == 4)
        {
            auto opts = juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Remove ALL MIDI Mappings")
                .withMessage("This will delete every MIDI mapping. Continue?")
                .withButton("Remove All")
                .withButton("Cancel");
            juce::AlertWindow::showAsync(opts, [this](int result) {
                if (result == 1)
                    audioEngine.getMidiLearnManager().removeAllMappings();
            });
        }
    });
}

void TransportComponent::masterGainChanged()
{
    audioEngine.setMasterGain(static_cast<float>(masterGainSlider.getValue()));
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
