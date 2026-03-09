#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"

class MetronomeRoutingComponent : public juce::Component
{
public:
    explicit MetronomeRoutingComponent(AudioEngine& engine)
        : audioEngine(engine)
    {
        leftLabel.setText("L:", juce::dontSendNotification);
        leftLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(leftLabel);

        rightLabel.setText("R:", juce::dontSendNotification);
        rightLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(rightLabel);

        addAndMakeVisible(leftBox);
        addAndMakeVisible(rightBox);

        leftBox.onChange = [this] { applyRouting(); };
        rightBox.onChange = [this] { applyRouting(); };

        closeButton.onClick = [this]
        {
            if (auto* dlg = findParentComponentOfClass<juce::DialogWindow>())
                dlg->exitModalState(0);
        };
        addAndMakeVisible(closeButton);

        populateBoxes();
        setSize(220, 120);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);

        const int labelW = 24;
        {
            auto row = area.removeFromTop(26);
            leftLabel.setBounds(row.removeFromLeft(labelW));
            row.removeFromLeft(4);
            leftBox.setBounds(row);
        }
        area.removeFromTop(4);
        {
            auto row = area.removeFromTop(26);
            rightLabel.setBounds(row.removeFromLeft(labelW));
            row.removeFromLeft(4);
            rightBox.setBounds(row);
        }
        area.removeFromTop(8);
        closeButton.setBounds(area.removeFromTop(26).reduced(40, 0));
    }

private:
    AudioEngine& audioEngine;

    juce::Label leftLabel;
    juce::Label rightLabel;
    juce::ComboBox leftBox;
    juce::ComboBox rightBox;
    juce::TextButton closeButton{"Close"};

    void populateBoxes()
    {
        leftBox.clear(juce::dontSendNotification);
        rightBox.clear(juce::dontSendNotification);

        const int numOut = juce::jmax(2, audioEngine.getNumOutputChannels());

        // Left box: Out 1..N (ID = i+1)
        for (int i = 0; i < numOut; ++i)
            leftBox.addItem("Out " + juce::String(i + 1), i + 1);

        // Right box: "Same as Left (mono)" (ID=1), then Out 1..N (ID = i+2)
        rightBox.addItem("Same as Left (mono)", 1);
        for (int i = 0; i < numOut; ++i)
            rightBox.addItem("Out " + juce::String(i + 1), i + 2);

        // Load current values
        const int curL = audioEngine.getMetronome().getOutputLeft();
        const int curR = audioEngine.getMetronome().getOutputRight();

        leftBox.setSelectedId(curL + 1, juce::dontSendNotification);

        if (curR == curL)
            rightBox.setSelectedId(1, juce::dontSendNotification);
        else
            rightBox.setSelectedId(curR + 2, juce::dontSendNotification);
    }

    void applyRouting()
    {
        const int left = leftBox.getSelectedId() - 1;

        int right;
        const int rightId = rightBox.getSelectedId();
        if (rightId == 1)
            right = left;  // Same as Left (mono)
        else
            right = rightId - 2;

        audioEngine.setMetronomeOutput(left, right);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MetronomeRoutingComponent)
};
