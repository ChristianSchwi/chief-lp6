#include "PreferencesComponent.h"

static const juce::Colour bgColour      { 0xFF1E1E1E };
static const juce::Colour sectionColour { 0xFF2A2A2A };
static const juce::Colour accentColour  { 0xFF4A8FCC };
static const juce::Colour mutedColour   { 0xFF3A3A3A };

//==============================================================================
PreferencesComponent::PreferencesComponent(MidiLearnManager& mlm,
                                           std::function<bool()>    getAutoRecall,
                                           std::function<void(bool)> setAutoRecall)
    : midiLearnManager(mlm)
    , autoRecallGetter(std::move(getAutoRecall))
    , autoRecallSetter(std::move(setAutoRecall))
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

    //--------------------------------------------------------------------------
    // Section header: Session
    sectionSessionLabel.setText("Session", juce::dontSendNotification);
    sectionSessionLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    sectionSessionLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(sectionSessionLabel);

    autoRecallButton.setToggleState(autoRecallGetter ? autoRecallGetter() : false,
                                    juce::dontSendNotification);
    autoRecallButton.onClick = [this]
    {
        if (autoRecallSetter)
            autoRecallSetter(autoRecallButton.getToggleState());
    };
    addAndMakeVisible(autoRecallButton);

    setSize(520, 290);
}

//==============================================================================
void PreferencesComponent::paint(juce::Graphics& g)
{
    g.fillAll(bgColour);

    // MIDI section background panel
    auto midiSection = getLocalBounds().reduced(12).withHeight(140);
    g.setColour(sectionColour);
    g.fillRoundedRectangle(midiSection.toFloat(), 6.0f);
    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    g.drawHorizontalLine(midiSection.getY() + 28,
                         static_cast<float>(midiSection.getX() + 6),
                         static_cast<float>(midiSection.getRight() - 6));

    // Session section background panel
    auto sessionSection = getLocalBounds().reduced(12).withTop(midiSection.getBottom() + 8)
                                          .withHeight(66);
    g.setColour(sectionColour);
    g.fillRoundedRectangle(sessionSection.toFloat(), 6.0f);
    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    g.drawHorizontalLine(sessionSection.getY() + 28,
                         static_cast<float>(sessionSection.getX() + 6),
                         static_cast<float>(sessionSection.getRight() - 6));
}

void PreferencesComponent::resized()
{
    auto area = getLocalBounds().reduced(20);

    // --- MIDI Learn section ---
    sectionMidiLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);
    midiLearnDescLabel.setBounds(area.removeFromTop(52));
    area.removeFromTop(10);
    auto buttonRow = area.removeFromTop(30);
    perChannelButton   .setBounds(buttonRow.removeFromLeft(150).reduced(2));
    activeChannelButton.setBounds(buttonRow.removeFromLeft(150).reduced(2));

    area.removeFromTop(18);  // gap between sections

    // --- Session section ---
    sectionSessionLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);
    autoRecallButton.setBounds(area.removeFromTop(28));
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
