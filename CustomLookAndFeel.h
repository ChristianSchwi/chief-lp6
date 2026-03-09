#pragma once

#include <JuceHeader.h>

/**
 * Gray rectangle track filled with blue from the left.
 * Only overrides LinearHorizontal sliders; vertical sliders use default style.
 */
class FilledBarSliderLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                                             sliderPos, 0.f, 1.f, style, slider);
            return;
        }

        const float trackH = 10.0f;
        const float trackY = y + (height - trackH) * 0.5f;
        const float corner = 3.0f;

        // Gray track background
        g.setColour(juce::Colour(0xFF555555));
        g.fillRoundedRectangle((float)x, trackY, (float)width, trackH, corner);

        // Blue fill from left to slider position
        const float fillW = sliderPos - (float)x;
        if (fillW > 0.0f)
        {
            g.setColour(juce::Colour(0xFF3388CC));
            g.fillRoundedRectangle((float)x, trackY, fillW, trackH, corner);
        }
    }
};
