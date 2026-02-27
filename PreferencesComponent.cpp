#include "PreferencesComponent.h"

static const juce::Colour bgColour      { 0xFF1E1E1E };
static const juce::Colour sectionColour { 0xFF2A2A2A };
static const juce::Colour accentColour  { 0xFF4A8FCC };
static const juce::Colour mutedColour   { 0xFF3A3A3A };

//==============================================================================
PreferencesComponent::PreferencesComponent(MidiLearnManager& mlm)
    : midiLearnManager(mlm)
{
    //--------------------------------------------------------------------------
    // Section header: MIDI Learn Mode
    sectionMidiLabel.setText("MIDI Learn Mode", juce::dontSendNotification);
    sectionMidiLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    sectionMidiLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(sectionMidiLabel);

    midiLearnDescLabel.setText(
        "Per Channel: each channel reacts to its own dedicated MIDI messages.\n"
        "Active Channel: all channel controls always apply to whichever channel "
        "is currently active (one set of controls for everything).",
        juce::dontSendNotification);
    midiLearnDescLabel.setFont(juce::Font(12.0f));
    midiLearnDescLabel.setColour(juce::Label::textColourId,
                                 juce::Colour(0xFFAAAAAA));
    midiLearnDescLabel.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(midiLearnDescLabel);

    //--------------------------------------------------------------------------
    // Toggle buttons — behave as a radio group
    perChannelButton.onClick = [this]
    {
        midiLearnManager.setMidiLearnMode(MidiLearnMode::PerChannel);
        updateMidiLearnModeButtons();
    };
    addAndMakeVisible(perChannelButton);

    activeChannelButton.onClick = [this]
    {
        midiLearnManager.setMidiLearnMode(MidiLearnMode::ActiveChannel);
        updateMidiLearnModeButtons();
    };
    addAndMakeVisible(activeChannelButton);

    updateMidiLearnModeButtons();

    setSize(520, 200);
}

//==============================================================================
void PreferencesComponent::paint(juce::Graphics& g)
{
    g.fillAll(bgColour);

    // Section background panel
    auto sectionBounds = getLocalBounds().reduced(12).withHeight(140);
    g.setColour(sectionColour);
    g.fillRoundedRectangle(sectionBounds.toFloat(), 6.0f);

    // Section divider line below header
    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    g.drawHorizontalLine(sectionBounds.getY() + 28,
                         static_cast<float>(sectionBounds.getX() + 6),
                         static_cast<float>(sectionBounds.getRight() - 6));
}

void PreferencesComponent::resized()
{
    auto area = getLocalBounds().reduced(20);

    // Section header
    sectionMidiLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);

    // Description text
    midiLearnDescLabel.setBounds(area.removeFromTop(52));
    area.removeFromTop(10);

    // Mode buttons
    auto buttonRow = area.removeFromTop(30);
    perChannelButton   .setBounds(buttonRow.removeFromLeft(150).reduced(2));
    activeChannelButton.setBounds(buttonRow.removeFromLeft(150).reduced(2));
}

//==============================================================================
void PreferencesComponent::updateMidiLearnModeButtons()
{
    const bool isPerChannel =
        midiLearnManager.getMidiLearnMode() == MidiLearnMode::PerChannel;

    perChannelButton.setColour(juce::TextButton::buttonColourId,
                               isPerChannel ? accentColour : mutedColour);
    perChannelButton.setColour(juce::TextButton::textColourOffId,
                               isPerChannel ? juce::Colours::white
                                            : juce::Colour(0xFFAAAAAA));

    activeChannelButton.setColour(juce::TextButton::buttonColourId,
                                  !isPerChannel ? accentColour : mutedColour);
    activeChannelButton.setColour(juce::TextButton::textColourOffId,
                                  !isPerChannel ? juce::Colours::white
                                               : juce::Colour(0xFFAAAAAA));

    repaint();
}
