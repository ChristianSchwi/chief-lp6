#include "TransportComponent.h"
#include "MetronomeRoutingComponent.h"
#include "AppConfig.h"

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
    bpmLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(bpmLabel);

    bpmEditor.setInputRestrictions(6, "0123456789.");
    bpmEditor.setJustification(juce::Justification::centred);
    bpmEditor.setFont(juce::Font(36.0f, juce::Font::bold));
    bpmEditor.setText(juce::String(audioEngine.getLoopEngine().getBPM(), 1), juce::dontSendNotification);
    bpmEditor.applyFontToAllText(juce::Font(20.0f, juce::Font::bold), true);
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
    beatsPerBarLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(beatsPerBarLabel);

    beatsPerBarSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    beatsPerBarSlider.setRange(1.0, 16.0, 1.0);
    beatsPerBarSlider.setValue(4.0, juce::dontSendNotification);
    beatsPerBarSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 30, 20);
    beatsPerBarSlider.setTooltip("Beats per bar: determines bar boundaries for the accent beat "
                                  "and bar-based count-in length.");
    beatsPerBarSlider.onValueChange = [this] { beatsPerBarChanged(); };
    addAndMakeVisible(beatsPerBarSlider);

    // Metro I/O button
    metroIOButton.setTooltip("Configure metronome output routing");
    metroIOButton.onClick = [this]
    {
        auto* dlg = new MetronomeRoutingComponent(audioEngine);
        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(dlg);
        opts.dialogTitle = "Metronome Output";
        opts.dialogBackgroundColour = juce::Colours::darkgrey;
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar = false;
        opts.resizable = false;
        opts.launchAsync();
    };
    addAndMakeVisible(metroIOButton);

    // Volume
    metroGainLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(metroGainLabel);

    metroGainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    metroGainSlider.setRange(0.0, 1.0, 0.01);
    metroGainSlider.setValue(1.0, juce::dontSendNotification);
    metroGainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    metroGainSlider.setTooltip("Metronome click volume (0 = silent, 1 = full).");
    metroGainSlider.onValueChange = [this] { metroGainChanged(); };
    addAndMakeVisible(metroGainSlider);
    //--------------------------------------------------------------------------
    // Auto-Start
    autoStartButton.setToggleState(false, juce::dontSendNotification);
    autoStartButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
    autoStartButton.setTooltip(
        "Auto Start: recording begins automatically when input level exceeds the threshold. "
        "No need to press Play first.");
    autoStartButton.onClick = [this] { autoStartChanged(); };
    addAndMakeVisible(autoStartButton);

    autoStartThreshLabel.setJustificationType(juce::Justification::centredLeft);
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
    countInLabel.setJustificationType(juce::Justification::centredLeft);
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
    fixedLenLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(fixedLenLabel);

    fixedLenSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fixedLenSlider.setRange(0.0, 7.0, 1.0);
    fixedLenSlider.setValue(0.0, juce::dontSendNotification);
    fixedLenSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    fixedLenSlider.setTooltip("Fixed recording length in bars (0 = off). Metronome mode only.");
    fixedLenSlider.onValueChange = [this] { fixedLenSliderChanged(); };
    fixedLenSlider.textFromValueFunction = [](double val) {
        static const int steps[] = {0, 1, 2, 4, 8, 16, 32, 64};
        int idx = juce::jlimit(0, 7, (int)val);
        return juce::String(steps[idx]) + " bars";
    };
    fixedLenSlider.valueFromTextFunction = [](const juce::String& text) {
        static const int steps[] = {0, 1, 2, 4, 8, 16, 32, 64};
        int v = text.getIntValue();
        for (int i = 7; i >= 0; --i)
            if (steps[i] <= v) return (double)i;
        return 0.0;
    };
    addAndMakeVisible(fixedLenSlider);

    //--------------------------------------------------------------------------
    // Double Loop
    doubleLoopButton.setTooltip("Double the loop length. Recorded content is duplicated and plays twice.");
    doubleLoopButton.onClick = [this] { audioEngine.doubleLoopLength(); };
    addAndMakeVisible(doubleLoopButton);

    //--------------------------------------------------------------------------
    // Master Recording
    masterRecordButton.setClickingTogglesState(false);
    masterRecordButton.setTooltip("Record master output (all channels, no metronome) to WAV file.");
    masterRecordButton.onClick = [this] { masterRecordClicked(); };
    if (!kFreeVersion)
        addAndMakeVisible(masterRecordButton);

    //--------------------------------------------------------------------------
    // Reset
    resetButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
    resetButton.onClick = [this] { resetClicked(); };
    addAndMakeVisible(resetButton);

    //--------------------------------------------------------------------------
    // A/B/C Sections
    {
        static const char* labels[] = { "A", "B", "C" };
        for (int s = 0; s < 3; ++s)
        {
            sectionButtons[s].setButtonText(labels[s]);
            sectionButtons[s].setClickingTogglesState(false);
            sectionButtons[s].setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF2266AA));
            sectionButtons[s].setTooltip("Switch to section " + juce::String(labels[s]));
            sectionButtons[s].onClick = [this, s] { audioEngine.setActiveSection(s); };
            addAndMakeVisible(sectionButtons[s]);
        }
    }

    //--------------------------------------------------------------------------
    // Mute Groups
    for (int g = 0; g < kMaxMuteGroups; ++g)
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

    // Apply custom slider look-and-feel
    masterGainSlider .setLookAndFeel(&filledBarLnF);
    beatsPerBarSlider.setLookAndFeel(&filledBarLnF);
    metroGainSlider  .setLookAndFeel(&filledBarLnF);
    autoStartSlider  .setLookAndFeel(&filledBarLnF);
    fixedLenSlider   .setLookAndFeel(&filledBarLnF);

    updateMetronomeButtonStates();
    startTimer(50);  // 20 Hz
}

TransportComponent::~TransportComponent()
{
    masterGainSlider .setLookAndFeel(nullptr);
    beatsPerBarSlider.setLookAndFeel(nullptr);
    metroGainSlider  .setLookAndFeel(nullptr);
    autoStartSlider  .setLookAndFeel(nullptr);
    fixedLenSlider   .setLookAndFeel(nullptr);
    stopTimer();
}

//==============================================================================
void TransportComponent::refreshAfterAudioInit()
{
    metroGainSlider.setValue(audioEngine.getMetronomeGain(), juce::dontSendNotification);
    masterGainSlider.setValue(audioEngine.getMasterGain(), juce::dontSendNotification);
    beatsPerBarSlider.setValue(audioEngine.getBeatsPerBar(), juce::dontSendNotification);

    // Restore fixed-length slider from engine value
    {
        static const int steps[] = {0, 1, 2, 4, 8, 16, 32, 64};
        int bars = audioEngine.getFixedLengthBars();
        int idx = 0;
        for (int i = 7; i >= 0; --i)
            if (steps[i] <= bars) { idx = i; break; }
        fixedLenSlider.setValue((double)idx, juce::dontSendNotification);
    }
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
    area.removeFromTop(2);
    if (!kFreeVersion)
        masterRecordButton.setBounds(area.removeFromTop(26).reduced(0, 1));
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
        metroIOButton      .setBounds(row.removeFromRight(36).reduced(0, 1));
        const int half = row.getWidth() / 2;
        metronomeButton    .setBounds(row.removeFromLeft(half));
        metronomeMuteButton.setBounds(row);
    }
    area.removeFromTop(4);
    {
        const int labelW = 65;
        auto row = area.removeFromTop(25);
        bpmLabel .setBounds(row.removeFromLeft(labelW));
        tapButton.setBounds(row.removeFromRight(40).reduced(0, 1));
        row.removeFromRight(4);
        bpmEditor.setBounds(row);
    }
    area.removeFromTop(2);
    {
        const int labelW = 65;
        auto row = area.removeFromTop(26);
        beatsPerBarLabel .setBounds(row.removeFromLeft(labelW));
        beatsPerBarSlider.setBounds(row);
    }
    {
        const int labelW = 65;
        auto row = area.removeFromTop(26);
        metroGainLabel .setBounds(row.removeFromLeft(labelW));
        metroGainSlider.setBounds(row);
    }
    {
        const int labelW = 65;
        auto row = area.removeFromTop(26);
        fixedLenLabel  .setBounds(row.removeFromLeft(labelW));
        fixedLenSlider .setBounds(row);
    }
    {
        const int labelW = 65;
        auto row = area.removeFromTop(26);
        countInLabel.setBounds(row.removeFromLeft(labelW));
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
        autoStartThreshLabel.setBounds(row.removeFromLeft(65));
        autoStartSlider     .setBounds(row);
    }
    area.removeFromTop(6);

    // ── UTILITY ───────────────────────────────────────────────────────────────
    addSection("UTILITY");
    {
        auto row = area.removeFromTop(30);
        const int half = row.getWidth() / 2;
        midiLearnButton .setBounds(row.removeFromLeft(half).reduced(2, 3));
        resetButton     .setBounds(row                    .reduced(2, 3));
    }
    area.removeFromTop(6);

    // ── SECTIONS (A/B/C) ─────────────────────────────────────────────────────
    addSection("SECTIONS");
    {
        auto row = area.removeFromTop(24);
        const int btnW = row.getWidth() / 3;
        for (int s = 0; s < 3; ++s)
            sectionButtons[s].setBounds(row.removeFromLeft(btnW).reduced(1, 1));
    }
    area.removeFromTop(6);

    // ── MUTE GROUPS ──────────────────────────────────────────────────────────
    addSection("MUTE GROUPS");
    {
        auto row = area.removeFromTop(24);
        const int btnW = row.getWidth() / kMaxMuteGroups;
        for (int g = 0; g < kMaxMuteGroups; ++g)
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

    // Keep UI in sync with engine (e.g. after song load/template apply)
    {
        const int engineBPB = audioEngine.getBeatsPerBar();
        if (static_cast<int>(beatsPerBarSlider.getValue()) != engineBPB)
            beatsPerBarSlider.setValue(engineBPB, juce::dontSendNotification);

        const double engineBPM = audioEngine.getLoopEngine().getBPM();
        if (!bpmEditor.hasKeyboardFocus(false))
        {
            const double editorBPM = bpmEditor.getText().getDoubleValue();
            if (std::abs(editorBPM - engineBPM) > 0.05)
                bpmEditor.setText(juce::String(engineBPM, 1), juce::dontSendNotification);
        }
    }

    // Tap button blink in sync with BPM
    {
        const double bpm = audioEngine.getLoopEngine().getBPM();
        if (bpm > 0.0)
        {
            const double beatMs = 60000.0 / bpm;
            const double now = static_cast<double>(juce::Time::getMillisecondCounter());
            const bool beatOn = std::fmod(now / beatMs, 1.0) < 0.25;
            tapButton.setColour(juce::TextButton::buttonColourId,
                                beatOn ? juce::Colour(0xFF446688) : juce::Colour(0xFF333333));
        }
    }

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

    // A/B/C section buttons
    {
        const int activeSec  = audioEngine.getActiveSection();
        const int pendingSec = audioEngine.getPendingSection();
        const bool blinkOn   = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
        for (int s = 0; s < 3; ++s)
        {
            if (s == activeSec)
            {
                sectionButtons[s].setToggleState(true, juce::dontSendNotification);
                sectionButtons[s].setColour(juce::TextButton::buttonColourId,
                                            juce::Colour(0xFF2266AA));
            }
            else if (s == pendingSec)
            {
                sectionButtons[s].setToggleState(false, juce::dontSendNotification);
                sectionButtons[s].setColour(juce::TextButton::buttonColourId,
                                            blinkOn ? juce::Colour(0xFF2266AA)
                                                    : juce::Colour(0xFF334455));
            }
            else
            {
                sectionButtons[s].setToggleState(false, juce::dontSendNotification);
                sectionButtons[s].setColour(juce::TextButton::buttonColourId,
                                            juce::Colour(0xFF334455));
            }
        }
    }

    // Mute group toggle buttons
    for (int g = 0; g < kMaxMuteGroups; ++g)
        muteGroupToggleButtons[g].setToggleState(audioEngine.isMuteGroupActive(g),
                                                  juce::dontSendNotification);

    // Master recording button visual
    {
        const bool isRec = audioEngine.isMasterRecording();
        masterRecordButton.setButtonText(isRec ? "STOP Master Rec" : "Rec Master");
        masterRecordButton.setColour(juce::TextButton::buttonColourId,
                                     isRec ? juce::Colours::red : juce::Colour(0xFF333333));
    }
}

void TransportComponent::updateMetronomeButtonStates()
{
    const bool hasRecordings = audioEngine.hasAnyRecordings();
    const bool metroEnabled  = audioEngine.getMetronome().getEnabled();

    // Metronom-Toggle: gesperrt wenn Aufnahmen vorhanden
    metronomeButton.setEnabled(!hasRecordings);
    metronomeButton.setToggleState(metroEnabled, juce::dontSendNotification);

    // Beat-synced blink for metronome button — bright flash at beat start, then dark
    {
        const double bpm = audioEngine.getLoopEngine().getBPM();
        if (metroEnabled && bpm > 0.0)
        {
            const double beatMs = 60000.0 / bpm;
            const juce::uint32 now = juce::Time::getMillisecondCounter();
            const double posInBeat = std::fmod(now / beatMs, 1.0);
            const bool beatOn = posInBeat < 0.25;
            metronomeButton.setColour(juce::TextButton::buttonColourId,
                                      beatOn ? juce::Colour(0xFF446688) : juce::Colour(0xFF333333));
        }
        else
        {
            metronomeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF333333));
        }
    }

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
    fixedLenSlider.setEnabled(fixedLenActive);
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

void TransportComponent::fixedLenSliderChanged()
{
    static const int steps[] = {0, 1, 2, 4, 8, 16, 32, 64};
    int idx = juce::jlimit(0, 7, (int)fixedLenSlider.getValue());
    audioEngine.setFixedLengthBars(steps[idx]);
}

void TransportComponent::resetClicked()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Reset Recordings");
    menu.addItem(2, "Reset Plugins and Mute Groups");
    menu.addItem(3, "Reset Channel I/O");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&resetButton),
                       [this](int id)
    {
        if (id == 1)
        {
            audioEngine.setPlaying(false);
            audioEngine.resetSong();
        }
        else if (id == 2)
        {
            for (int i = 0; i < 6; ++i)
            {
                for (int slot = 0; slot < 3; ++slot)
                    audioEngine.removePlugin(i, slot);
                audioEngine.setChannelMuteGroup(i, 0);
            }
        }
        else if (id == 3)
        {
            for (int i = 0; i < 6; ++i)
            {
                auto* ch = audioEngine.getChannel(i);
                if (ch)
                    ch->setRouting(RoutingConfig{});
            }
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
    else if (hit(sectionButtons[0])) showMidiContextMenu(MidiControlTarget::SectionA);
    else if (hit(sectionButtons[1])) showMidiContextMenu(MidiControlTarget::SectionB);
    else if (hit(sectionButtons[2])) showMidiContextMenu(MidiControlTarget::SectionC);
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

void TransportComponent::masterRecordClicked()
{
    if (audioEngine.isMasterRecording())
    {
        audioEngine.stopMasterRecording();
    }
    else
    {
        juce::File dir;
        if (getMasterRecordPath)
        {
            auto customPath = getMasterRecordPath();
            if (customPath.isNotEmpty())
                dir = juce::File(customPath);
        }
        if (dir == juce::File())
            dir = SongManager::getCurrentSongDirectory();
        if (!audioEngine.startMasterRecording(dir))
            DBG("Master recording failed to start");
    }
}

void TransportComponent::masterGainChanged()
{
    audioEngine.setMasterGain(static_cast<float>(masterGainSlider.getValue()));
}

