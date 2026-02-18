#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"

/**
 * @file TransportComponent.h
 * @brief Global transport and loop controls
 * 
 * Displays:
 * - Play/Stop button
 * - BPM control
 * - Beats per loop
 * - Quantization toggle
 * - Loop length display
 * - CPU meter
 */

//==============================================================================
class TransportComponent : public juce::Component,
                           private juce::Timer
{
public:
    TransportComponent(AudioEngine& engine);
    ~TransportComponent() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
private:
    AudioEngine& audioEngine;
    
    // Transport controls
    juce::TextButton playStopButton{"Play"};
    
    // Loop controls
    juce::Label bpmLabel{"", "BPM:"};
    juce::Slider bpmSlider;
    
    juce::Label beatsLabel{"", "Beats:"};
    juce::Slider beatsSlider;
    
    juce::ToggleButton quantizeButton{"Quantize"};
    
    // Display
    juce::Label loopLengthLabel{"", "Loop: 0.00s"};
    juce::Label playheadLabel{"", "Pos: 0.00s"};
    juce::Label cpuLabel{"", "CPU: 0%"};
    
    void timerCallback() override;
    void playStopClicked();
    void bpmChanged();
    void beatsChanged();
    void quantizeChanged();
    void updateDisplay();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportComponent)
};
