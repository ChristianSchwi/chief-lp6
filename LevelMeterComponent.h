#pragma once

#include <JuceHeader.h>

/**
 * Simple vertical level meter: dark background, green fill bottom-up.
 * Yellow above 0.7, red above 0.9.  Fast attack, slow decay.
 */
class LevelMeterComponent : public juce::Component
{
public:
    void setLevel(float newLevel)
    {
        // Fast attack, slow decay
        if (newLevel >= displayLevel)
            displayLevel = newLevel;
        else
            displayLevel = displayLevel * 0.85f + newLevel * 0.15f;

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // Dark background
        g.setColour(juce::Colour(0xFF1A1A1A));
        g.fillRect(b);

        // Border
        g.setColour(juce::Colour(0xFF444444));
        g.drawRect(b, 1.0f);

        // Fill bottom-up
        const float level = juce::jlimit(0.0f, 1.0f, displayLevel);
        if (level <= 0.0f) return;

        const float inner = b.reduced(1.0f).getHeight();
        const float fillH = inner * level;
        auto fillArea = b.reduced(1.0f);
        fillArea = fillArea.removeFromBottom(fillH);

        if (level > 0.9f)
            g.setColour(juce::Colours::red);
        else if (level > 0.7f)
            g.setColour(juce::Colours::yellow);
        else
            g.setColour(juce::Colours::green);

        g.fillRect(fillArea);
    }

private:
    float displayLevel = 0.0f;
};
