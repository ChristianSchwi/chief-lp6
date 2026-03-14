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

/**
 * Square (non-rounded) button look-and-feel for mute group buttons.
 */
class SquareButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();

        auto baseColour = backgroundColour;
        if (shouldDrawButtonAsDown)
            baseColour = baseColour.brighter(0.1f);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.05f);

        g.setColour(baseColour);
        g.fillRect(bounds);

        g.setColour(juce::Colour(0xFF888888));
        g.drawRect(bounds, 1.0f);
    }
};

/**
 * Fader-style vertical slider: narrow track with a horizontal thumb/cap.
 */
/**
 * Button look-and-feel that draws a small folder icon before the button text.
 */
class FolderIconButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool /*isHighlighted*/, bool /*isDown*/) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
        const float iconH = bounds.getHeight() * 0.6f;
        const float iconW = iconH * 1.2f;
        const float iconX = bounds.getX() + 4.0f;
        const float iconY = bounds.getCentreY() - iconH * 0.5f;

        // Folder body
        g.setColour(juce::Colour(0xFFCCBB77));
        g.fillRoundedRectangle(iconX, iconY + iconH * 0.2f, iconW, iconH * 0.8f, 1.5f);
        // Folder tab
        g.fillRoundedRectangle(iconX, iconY, iconW * 0.45f, iconH * 0.35f, 1.0f);

        // Text after icon
        auto textArea = bounds.withLeft(iconX + iconW + 4.0f);
        g.setFont(juce::Font(bounds.getHeight() * 0.6f));
        g.setColour(button.findColour(button.getToggleState()
                        ? juce::TextButton::textColourOnId
                        : juce::TextButton::textColourOffId));
        g.drawText(button.getButtonText(), textArea, juce::Justification::centredLeft, false);
    }
};

/**
 * Fader-style vertical slider: narrow track with a horizontal thumb/cap.
 */
class FaderSliderLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                                             sliderPos, 0.f, 1.f, style, slider);
            return;
        }

        const float cx = x + width * 0.5f;
        const float trackW = 4.0f;
        const float trackTop = (float)y;
        const float trackBot = (float)(y + height);

        // Track groove
        g.setColour(juce::Colour(0xFF333333));
        g.fillRoundedRectangle(cx - trackW * 0.5f, trackTop, trackW, trackBot - trackTop, 2.0f);

        // Fill below thumb
        if (sliderPos < trackBot)
        {
            g.setColour(juce::Colour(0xFF3388CC));
            g.fillRoundedRectangle(cx - trackW * 0.5f, sliderPos, trackW, trackBot - sliderPos, 2.0f);
        }

        // Knob fader thumb (height = 3x width)
        const float thumbW = juce::jmin((float)width, 20.0f);
        const float thumbH = thumbW * 2.25f;
        const float thumbX = cx - thumbW * 0.5f;
        const float thumbY = sliderPos - thumbH * 0.5f;
        const float corner = thumbW * 0.2f;

        // Shadow
        g.setColour(juce::Colour(0x40000000));
        g.fillRoundedRectangle(thumbX, thumbY + 1.0f, thumbW, thumbH, corner);

        // Thumb body
        g.setColour(juce::Colour(0xFFBBBBBB));
        g.fillRoundedRectangle(thumbX, thumbY, thumbW, thumbH, corner);

        // Center line
        g.setColour(juce::Colour(0xFF666666));
        g.drawHorizontalLine((int)(thumbY + thumbH * 0.5f), thumbX + 3.0f, thumbX + thumbW - 3.0f);
    }
};
