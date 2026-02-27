#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"

/**
 * @brief TextButton that forwards right-clicks to its parent component.
 *
 * Without this, JUCE TextButton fires onClick on both left AND right mouse
 * button release, so right-clicking the main REC/PLAY button would start
 * recording instead of showing the context menu.
 */
struct ContextMenuButton : public juce::TextButton
{
    using juce::TextButton::TextButton;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            // Forward to parent so it can show the context menu.
            // Do NOT call TextButton::mouseDown — that would arm the button.
            if (auto* p = getParentComponent())
                p->mouseDown(e.getEventRelativeTo(p));
            return;
        }
        juce::TextButton::mouseDown(e);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContextMenuButton)
};

//==============================================================================
/**
 * @file ChannelStripComponent.h
 * @brief Single channel strip UI
 *
 * Single context-aware main button (Spec Abschnitt 5):
 *   Priority: Overdub mode > Empty > Has loop (idle) > Playing > Recording
 *
 * Active Channel Highlight:
 *   When this strip's index == audioEngine.getActiveChannel(),
 *   a coloured border is drawn around the entire strip.
 *   Clicking anywhere on the strip sets it as the active channel.
 */
class ChannelStripComponent : public juce::Component,
                              private juce::Timer
{
public:
    ChannelStripComponent(AudioEngine& engine, int channelIndex);
    ~ChannelStripComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    AudioEngine& audioEngine;
    const int    channelIndex;

    //==========================================================================
    // Main context-aware button (ContextMenuButton so right-click → context menu)
    ContextMenuButton mainButton;

    // Secondary buttons (also ContextMenuButton for consistent right-click behaviour)
    ContextMenuButton clrButton   {"CLR"};
    ContextMenuButton ioButton    {"I/O"};
    ContextMenuButton fxButton    {"FX"};

    // Controls
    juce::Slider      gainSlider;
    ContextMenuButton muteButton  {"M"};
    ContextMenuButton soloButton  {"S"};
    juce::ComboBox   monitorModeBox;

    // Display
    juce::Label      channelLabel;
    juce::Label      stateLabel;

    //==========================================================================
    void timerCallback() override;
    void updateMainButton();

    void mainButtonClicked();
    void clrButtonClicked();
    void muteClicked();
    void soloClicked();
    void gainChanged();

    void monitorModeChanged();
    void showContextMenu(const juce::MouseEvent& e);

    bool isActiveChannel()  const { return audioEngine.getActiveChannel() == channelIndex; }
    bool channel_hasLoop()  const;  // helper — avoids repeated null-check

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripComponent)
};
