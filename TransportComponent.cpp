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
    // Tap tempo
    tapButton.setTooltip("Tap repeatedly to set BPM. Sequence resets after 3 s of silence.");
    tapButton.onClick = [this] { tapClicked(); };
    addAndMakeVisible(tapButton);

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
    // Reset Song
    resetSongButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
    resetSongButton.onClick = [this] { resetSongClicked(); };
    addAndMakeVisible(resetSongButton);

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
    tapButton.setBounds(area.removeFromTop(24));
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
        metroOutLabel .setBounds(row.removeFromLeft(80));
        metroOutputBox.setBounds(row);
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
        fixedLenPlusBtn  .setBounds(row.removeFromLeft(24).reduced(1, 3));
        fixedLenMinusBtn .setBounds(row.removeFromLeft(24).reduced(1, 3));
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
    bpmSlider .setEnabled(!hasRec);
    tapButton .setEnabled(!hasRec);
    beatsPerBarSlider.setEnabled(!playing);

    overdubButton  .setToggleState(audioEngine.isInOverdubMode(), juce::dontSendNotification);
    latchModeButton.setToggleState(audioEngine.isLatchMode(),     juce::dontSendNotification);

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
    Command cmd;
    cmd.type       = CommandType::SetBPM;
    cmd.floatValue = static_cast<float>(bpmSlider.getValue());
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
    bpmSlider.setValue(newBpm, juce::sendNotification);
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
    static const int steps[] = {0, 1, 2, 8, 16, 32, 64};
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

void TransportComponent::showGlobalMidiLearnMenu()
{
    static const MidiControlTarget targets[] = {
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
    static const char* labels[] = {
        "Global Play/Stop",
        "Panic",
        "Metronome On/Off",
        "Overdub Mode On/Off",
        "Latch Mode On/Off",
        "Auto Start On/Off",
        "Next Channel",
        "Prev Channel",
        "Next Song",
        "Prev Song"
    };
    static constexpr int numTargets = (int)std::size(targets);

    auto& mlm = audioEngine.getMidiLearnManager();
    const bool isLearn   = mlm.isLearning();
    const bool isUnlearn = mlm.isUnlearning();

    juce::PopupMenu menu;

    if (isLearn)
    {
        menu.addItem(20, "Abort Learning");
    }
    else if (isUnlearn)
    {
        for (int i = 0; i < numTargets; ++i)
        {
            const bool hasMidi = mlm.getMapping(-1, targets[i]).isValid();
            menu.addItem(i + 1,
                         juce::String("Remove MIDI: ") + labels[i],
                         hasMidi);  // greyed if no mapping
            if (i == 5) menu.addSeparator();  // separator after AutoStart
        }
        menu.addSeparator();
        menu.addItem(21, "Abort Unlearning");
    }
    else
    {
        for (int i = 0; i < numTargets; ++i)
        {
            menu.addItem(i + 1, juce::String("MIDI Learn: ") + labels[i]);
            if (i == 5) menu.addSeparator();  // separator after AutoStart
        }
        menu.addSeparator();
        menu.addItem(11, "Start MIDI Unlearn");
        menu.addItem(12, "Remove ALL MIDI Mappings");
    }

    const bool wasUnlearning = isUnlearn;
    menu.showMenuAsync(juce::PopupMenu::Options(), [this, wasUnlearning](int id)
    {
        if (id <= 0) return;
        auto& mlm = audioEngine.getMidiLearnManager();

        if (id == 20) { mlm.stopLearning();    return; }
        if (id == 21) { mlm.stopUnlearning();  return; }
        if (id == 11) { mlm.startUnlearning(); return; }
        if (id == 12)
        {
            auto options = juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Remove ALL MIDI Mappings")
                .withMessage("This will delete every MIDI mapping (all channels and global controls). Continue?")
                .withButton("Remove All")
                .withButton("Cancel");
            juce::AlertWindow::showAsync(options, [this](int result) {
                if (result == 1)
                    audioEngine.getMidiLearnManager().removeAllMappings();
            });
            return;
        }

        static const MidiControlTarget targets[] = {
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
        if (id < 1 || id > (int)std::size(targets)) return;

        if (wasUnlearning)
        {
            mlm.removeMapping(-1, targets[id - 1]);
            mlm.stopUnlearning();
        }
        else
        {
            mlm.startLearning(-1, targets[id - 1]);
        }
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
