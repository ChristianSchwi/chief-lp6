#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "Channel.h"

/**
 * @file PluginManagerComponent.h
 * @brief Popup-Komponente zum Verwalten von VST-Plugins eines Channels
 *
 * Zeigt:
 *  - Channel-Typ-Umschalter (Audio / VSTi)
 *  - VSTi-Slot  (nur bei VSTi-Channel): ComboBox mit Instrument-Plugins
 *  - 3 FX-Slots: jeweils ComboBox + Bypass-Toggle + Remove-Button
 *  - Scan-Button falls noch keine Plugins geladen wurden
 *
 * Geöffnet als CallOutBox via ChannelStripComponent.
 */

//==============================================================================
/**
 * @brief Ein einzelner Plugin-Slot (FX oder VSTi)
 */
class PluginSlotRow : public juce::Component
{
public:
    /** slotIndex: 0-2 = FX, -1 = VSTi */
    PluginSlotRow(AudioEngine& engine, int channelIndex, int slotIndex);

    void resized() override;

    /** Aktualisiert die Anzeige (nach Load/Remove) */
    void refresh();

private:
    AudioEngine& audioEngine;
    int channelIdx;
    int slotIdx;  // -1 = VSTi, 0-2 = FX

    juce::Label     slotLabel;
    juce::ComboBox  pluginComboBox;
    juce::TextButton bypassButton{"B"};
    juce::TextButton removeButton{"X"};

    // Identifier-String für jede ComboBox-ID → Plugin
    juce::StringArray pluginIdentifiers;

    void populateComboBox();
    void loadSelected();
    void removePlugin();
    void toggleBypass();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSlotRow)
};

//==============================================================================
/**
 * @brief Gesamte Plugin-Verwaltung für einen Channel
 */
class PluginManagerComponent : public juce::Component
{
public:
    PluginManagerComponent(AudioEngine& engine, int channelIndex);

    void paint(juce::Graphics& g) override;
    void resized() override;

    static constexpr int kWidth  = 380;
    static constexpr int kRowH   = 28;

private:
    AudioEngine& audioEngine;
    int channelIdx;
    bool isVSTi{false};

    // Channel-Typ-Umschalter
    juce::TextButton audioTypeButton{"Audio"};
    juce::TextButton vstiTypeButton {"VSTi"};

    // Scan-Button (falls keine Plugins gefunden)
    juce::TextButton scanButton{"Plugins scannen"};

    // Slots: index 0 = VSTi-Slot (nur bei VSTi), 1-3 = FX-Slots
    std::unique_ptr<PluginSlotRow> vstiSlot;
    std::array<std::unique_ptr<PluginSlotRow>, 3> fxSlots;

    void setChannelType(ChannelType type);
    void rebuildSlots();
    int  calcHeight() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginManagerComponent)
};
