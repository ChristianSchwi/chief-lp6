#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
    : transportComponent(audioEngine)
{
    songManager  = std::make_unique<SongManager>();
    showManager  = std::make_unique<ShowManager>();

    // --- Audio init first ---
    initializeAudio();

    // --- Transport ---
    addAndMakeVisible(transportComponent);

    // BUG A FIX: refreshAfterAudioInit() muss nach initializeAudio() aufgerufen werden,
    // damit metroOutputBox die tatsächliche Kanal-Anzahl kennt.
    transportComponent.refreshAfterAudioInit();

    // --- Channel strips ---
    for (int i = 0; i < 6; ++i)
    {
        channelStrips[i] = std::make_unique<ChannelStripComponent>(audioEngine, i);
        addAndMakeVisible(channelStrips[i].get());
    }

    // --- Show component ---
    showComponent = std::make_unique<ShowComponent>(audioEngine,
                                                    *songManager,
                                                    *showManager);
    showComponent->setAudioReady(true);
    addAndMakeVisible(showComponent.get());

    // --- Info label ---
    infoLabel.setJustificationType(juce::Justification::centredRight);
    infoLabel.setFont(juce::Font(12.0f));
    addAndMakeVisible(infoLabel);

    // --- Logo ---
    logo = juce::ImageCache::getFromMemory(
        BinaryData::chief_lp6_logo_png,
        BinaryData::chief_lp6_logo_pngSize);

    setSize(1400, 780);
}

MainComponent::~MainComponent() {}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    if (logo.isValid())
    {
        g.drawImage(logo,
                    getLocalBounds().removeFromTop(50).toFloat(),
                    juce::RectanglePlacement::centred |
                    juce::RectanglePlacement::onlyReduceInSize);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Logo header
    area.removeFromTop(50);

    // Show/song bar at the bottom — fixed height
    showComponent->setBounds(area.removeFromBottom(36));

    // Info label below channels
    infoLabel.setBounds(area.removeFromBottom(20).reduced(4, 0));

    // Remaining area: transport (left panel) + 6 channel strips
    const int transportWidth = 220;
    transportComponent.setBounds(area.removeFromLeft(transportWidth).reduced(4));

    const int channelWidth = area.getWidth() / 6;
    for (int i = 0; i < 6; ++i)
        channelStrips[i]->setBounds(area.removeFromLeft(channelWidth).reduced(3));
}

//==============================================================================
void MainComponent::initializeAudio()
{
    const juce::String error = audioEngine.initialiseAudio(2, 2, 44100.0, 512);

    if (error.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Audio Error",
            "Failed to initialize audio: " + error);
        infoLabel.setText("Audio Error: " + error, juce::dontSendNotification);
        return;
    }

    // --- Global loop settings ---
    audioEngine.getLoopEngine().setBPM(120.0);
    audioEngine.getLoopEngine().setBeatsPerLoop(4);
    audioEngine.getLoopEngine().setQuantizationEnabled(true);

    // BUG B FIX: calculateLoopLengthFromBPM() NUR aufrufen wenn Metronom aktiv.
    // Im Startup-Zustand ist das Metronom AUS → freier Modus → Loop-Länge bleibt 0.
    // Sonst würde der Loop im Metronom-Modus starten obwohl der Schalter auf AUS steht.
    if (audioEngine.getMetronome().getEnabled())
        audioEngine.getLoopEngine().calculateLoopLengthFromBPM();
    // else: Loop-Länge = 0 → erste Aufnahme setzt sie (freier Modus)

    // Start playback
    audioEngine.setPlaying(true);

    infoLabel.setText(
        "Audio: " + juce::String(audioEngine.getSampleRate(), 0) + " Hz  |  " +
        juce::String(audioEngine.getBufferSize()) + " samples  |  " +
        juce::String(audioEngine.getNumInputChannels()) + " in / " +
        juce::String(audioEngine.getNumOutputChannels()) + " out",
        juce::dontSendNotification);
}
