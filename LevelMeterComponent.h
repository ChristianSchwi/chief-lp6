#pragma once

#include <JuceHeader.h>

/**
 * Vertical level meter with logarithmic scale and seamless colour gradient.
 * Deep green → light green → yellow → orange → red.
 * Fast attack, slow decay.
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

        // Fill bottom-up (logarithmic: -60 dB to 0 dB range)
        const float linear = juce::jlimit(0.0f, 1.0f, displayLevel);
        if (linear <= 0.0f) return;

        const float minDb = -60.0f;
        const float dB = juce::jlimit(minDb, 0.0f,
                             20.0f * std::log10(linear));
        const float norm = (dB - minDb) / (0.0f - minDb);  // 0..1

        auto inner = b.reduced(1.0f);
        const float innerH = inner.getHeight();
        const float fillH = innerH * norm;
        auto fillArea = inner;
        fillArea = fillArea.removeFromBottom(fillH);

        // Draw gradient fill: paint 1-pixel horizontal strips with interpolated colour
        const float fillTop = fillArea.getY();
        const float fillBot = fillArea.getBottom();
        const float fillX = fillArea.getX();
        const float fillW = fillArea.getWidth();

        for (float py = fillBot - 1.0f; py >= fillTop; py -= 1.0f)
        {
            // Position 0 = bottom (quiet), 1 = top (loud)
            const float pos = (fillBot - py) / innerH;
            g.setColour(gradientColour(pos));
            g.fillRect(fillX, py, fillW, 1.0f);
        }
    }

private:
    float displayLevel = 0.0f;

    /** Seamless gradient: deep green → light green → yellow → orange → red. */
    static juce::Colour gradientColour(float pos)
    {
        // pos: 0 = bottom (silent), 1 = top (clipping)
        //  0.0–0.4  deep green → light green
        //  0.4–0.6  light green → yellow
        //  0.6–0.8  yellow → orange
        //  0.8–1.0  orange → red
        struct Stop { float pos; juce::Colour col; };
        static const Stop stops[] = {
            { 0.0f, juce::Colour(0xFF006600) },  // deep green
            { 0.4f, juce::Colour(0xFF00CC00) },  // light green
            { 0.6f, juce::Colour(0xFFCCCC00) },  // yellow
            { 0.8f, juce::Colour(0xFFCC6600) },  // orange
            { 1.0f, juce::Colour(0xFFCC0000) },  // red
        };

        if (pos <= 0.0f) return stops[0].col;
        if (pos >= 1.0f) return stops[4].col;

        for (int i = 0; i < 4; ++i)
        {
            if (pos <= stops[i + 1].pos)
            {
                const float t = (pos - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
                return stops[i].col.interpolatedWith(stops[i + 1].col, t);
            }
        }
        return stops[4].col;
    }
};
