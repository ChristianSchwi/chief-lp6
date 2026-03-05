#pragma once

#include <JuceHeader.h>

/**
 * Button/toggle/slider subclasses that forward right-clicks to the parent
 * component so it can show a context menu (e.g. MIDI Learn).
 */

struct ContextMenuButton : public juce::TextButton
{
    using juce::TextButton::TextButton;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            if (auto* p = getParentComponent())
                p->mouseDown(e.getEventRelativeTo(p));
            return;
        }
        juce::TextButton::mouseDown(e);
    }
};

struct ContextMenuToggleButton : public juce::ToggleButton
{
    using juce::ToggleButton::ToggleButton;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            if (auto* p = getParentComponent())
                p->mouseDown(e.getEventRelativeTo(p));
            return;
        }
        juce::ToggleButton::mouseDown(e);
    }
};

struct ContextMenuSlider : public juce::Slider
{
    using juce::Slider::Slider;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            if (auto* p = getParentComponent())
                p->mouseDown(e.getEventRelativeTo(p));
            return;
        }
        juce::Slider::mouseDown(e);
    }
};
