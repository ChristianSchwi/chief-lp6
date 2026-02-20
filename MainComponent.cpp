#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
    : transportComponent(audioEngine)
{
 
    
    // Initialize managers
    songManager = std::make_unique<SongManager>();
    showManager = std::make_unique<ShowManager>();
    
    // Initialize audio engine
    initializeAudio();
    
    // Create transport component
    addAndMakeVisible(transportComponent);
    
    // Create channel strips
    for (int i = 0; i < 6; ++i)
    {
        channelStrips[i] = std::make_unique<ChannelStripComponent>(audioEngine, i);
        addAndMakeVisible(channelStrips[i].get());
    }
    
    // Load/Save buttons
    loadSongButton.onClick = [this] { loadSongClicked(); };
    addAndMakeVisible(loadSongButton);
    
    saveSongButton.onClick = [this] { saveSongClicked(); };
    addAndMakeVisible(saveSongButton);
    
    // Info label
    infoLabel.setText("Ready", juce::dontSendNotification);
    infoLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(infoLabel);


    logo = juce::ImageCache::getFromMemory(
        BinaryData::chief_lp6_logo_png,
        BinaryData::chief_lp6_logo_pngSize);

    setSize(1400, 700);
}

MainComponent::~MainComponent()
{
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    
    // Draw title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(28.0f, juce::Font::bold));
    /*g.drawText("6-Channel Software Looper", 
               getLocalBounds().removeFromTop(50), 
               juce::Justification::centred);*/
    if (logo.isValid())
    {
        auto headerArea = getLocalBounds().removeFromTop(60);

        //g.drawImageWithin(logo,
        //    headerArea.getX(),
        //    headerArea.getY(),
        //    headerArea.getWidth(),
        //    headerArea.getHeight(),
        //    juce::RectanglePlacement::centred);

        g.drawImage(logo,
            headerArea.toFloat(),
            juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);

    }

}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    
    // Title area
    area.removeFromTop(50);
    
    // Top menu bar
    auto menuArea = area.removeFromTop(40);
    loadSongButton.setBounds(menuArea.removeFromLeft(120).reduced(5));
    saveSongButton.setBounds(menuArea.removeFromLeft(120).reduced(5));
    infoLabel.setBounds(menuArea.reduced(5));
    
    // Transport section
    transportComponent.setBounds(area.removeFromTop(250).reduced(10));
    
    // Channel strips (6 columns)
    auto channelArea = area.reduced(10);
    const int channelWidth = channelArea.getWidth() / 6;
    
    for (int i = 0; i < 6; ++i)
    {
        auto stripArea = channelArea.removeFromLeft(channelWidth).reduced(5);
        channelStrips[i]->setBounds(stripArea);
    }
}

//==============================================================================
void MainComponent::initializeAudio()
{
    // Initialize with 2 inputs, 2 outputs
    juce::String error = audioEngine.initialiseAudio(2, 2, 44100.0, 512);
    
    if (error.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Audio Error",
            "Failed to initialize audio: " + error);
        
        infoLabel.setText("Audio Error: " + error, juce::dontSendNotification);
        return;
    }
    
    // Configure loop engine
    audioEngine.getLoopEngine().setBPM(120.0);
    audioEngine.getLoopEngine().setBeatsPerLoop(4);
    audioEngine.getLoopEngine().setQuantizationEnabled(true);
    audioEngine.getLoopEngine().calculateLoopLengthFromBPM();
    
    // Start playback
    audioEngine.setPlaying(true);
    
    infoLabel.setText("Audio: " + 
                     juce::String(audioEngine.getSampleRate()) + " Hz, " +
                     juce::String(audioEngine.getBufferSize()) + " samples",
                     juce::dontSendNotification);
}

void MainComponent::loadSongClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Song",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.json");
    
    auto flags = juce::FileBrowserComponent::openMode | 
                 juce::FileBrowserComponent::canSelectFiles;
    
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (!file.existsAsFile())
            return;
        
        Song song;
        auto result = songManager->loadSong(file, song);
        
        if (result.wasOk())
        {
            result = songManager->applySongToEngine(song, audioEngine);
            
            if (result.wasOk())
            {
                infoLabel.setText("Loaded: " + song.songName, juce::dontSendNotification);
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Load Error",
                    "Failed to apply song: " + result.getErrorMessage());
            }
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Load Error",
                "Failed to load song: " + result.getErrorMessage());
        }
    });
}

void MainComponent::saveSongClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Song",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory));
    
    auto flags = juce::FileBrowserComponent::openMode | 
                 juce::FileBrowserComponent::canSelectDirectories;
    
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto directory = chooser.getResult();
        if (!directory.isDirectory())
            return;
        
        Song song;
        song.songName = directory.getFileName();
        song.songDirectory = directory;
        
        auto result = songManager->saveSong(song, audioEngine);
        
        if (result.wasOk())
        {
            infoLabel.setText("Saved: " + song.songName, juce::dontSendNotification);
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Save Error",
                "Failed to save song: " + result.getErrorMessage());
        }
    });
}
