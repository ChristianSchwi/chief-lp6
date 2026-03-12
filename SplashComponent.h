#pragma once

#include <JuceHeader.h>
#include "AppConfig.h"

class SplashComponent : public juce::Component
{
public:
    SplashComponent()
    {
        if constexpr (kFreeVersion)
            logo = juce::ImageCache::getFromMemory(
                BinaryData::chief_lp2_Free_logo_png,
                BinaryData::chief_lp2_Free_logo_pngSize);
        else
            logo = juce::ImageCache::getFromMemory(
                BinaryData::chief_lp6_logo_png,
                BinaryData::chief_lp6_logo_pngSize);

        statusLabel.setJustificationType(juce::Justification::centred);
        statusLabel.setFont(juce::Font(13.0f));
        statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(statusLabel);

        setSize(500, 350);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF1A1A1A));

        if (logo.isValid())
        {
            auto logoArea = getLocalBounds().reduced(40).withTrimmedBottom(40);
            g.drawImage(logo, logoArea.toFloat(),
                        juce::RectanglePlacement::centred |
                        juce::RectanglePlacement::onlyReduceInSize);
        }
    }

    void resized() override
    {
        statusLabel.setBounds(getLocalBounds().removeFromBottom(40).reduced(10, 0));
    }

    void setStatusText(const juce::String& text)
    {
        statusLabel.setText(text, juce::dontSendNotification);
        statusLabel.repaint();
    }

private:
    juce::Image logo;
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplashComponent)
};
