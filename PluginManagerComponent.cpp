#include "PluginManagerComponent.h"
#include "VSTiChannel.h"

//==============================================================================
// PluginSlotRow
//==============================================================================

PluginSlotRow::PluginSlotRow(AudioEngine& engine, int channelIndex, int slotIndex)
    : audioEngine(engine)
    , channelIdx(channelIndex)
    , slotIdx(slotIndex)
{
    // Label
    juce::String labelText = (slotIdx == -1) ? "VSTi" : ("FX " + juce::String(slotIdx + 1));
    slotLabel.setText(labelText, juce::dontSendNotification);
    slotLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    addAndMakeVisible(slotLabel);

    // ComboBox
    pluginComboBox.setTextWhenNothingSelected("empty");
    pluginComboBox.onChange = [this] { loadSelected(); };
    addAndMakeVisible(pluginComboBox);

    // Bypass (nur FX-Slots)
    if (slotIdx >= 0)
    {
        bypassButton.setClickingTogglesState(true);
        bypassButton.setTooltip("Bypass");
        bypassButton.onClick = [this] { toggleBypass(); };
        addAndMakeVisible(bypassButton);
    }

    // Edit-Button (open plugin GUI)
    editButton.setTooltip("Open plugin editor");
    editButton.onClick = [this] { openEditor(); };
    addAndMakeVisible(editButton);

    // Remove-Button
    removeButton.setTooltip("Plugin entfernen");
    removeButton.onClick = [this] { removePlugin(); };
    addAndMakeVisible(removeButton);

    populateComboBox();
}

void PluginSlotRow::populateComboBox()
{
    pluginComboBox.clear(juce::dontSendNotification);
    pluginIdentifiers.clear();

    // ID 1 = "kein Plugin" (Platzhalter)
    pluginIdentifiers.add("");  // Index 0 → ID 1

    const auto& knownPlugins = audioEngine.getPluginHost().getKnownPlugins();
    const auto& types        = knownPlugins.getTypes();

    // Für VSTi-Slot nur Instruments, für FX-Slots nur Effects
    int id = 2;
    for (const auto& desc : types)
    {
        const bool isInstrument = desc.isInstrument;

        if (slotIdx == -1 && !isInstrument) continue;  // VSTi-Slot: nur Instruments
        if (slotIdx >= 0  &&  isInstrument) continue;  // FX-Slot:   keine Instruments

        pluginComboBox.addItem(desc.name + "  (" + desc.manufacturerName + ")", id);
        pluginIdentifiers.add(desc.createIdentifierString());
        ++id;
    }

    // Pre-select the currently loaded plugin (if any)
    int preSelectedId = 1;  // default: nothing selected
    auto* channel = audioEngine.getChannel(channelIdx);
    if (channel)
    {
        juce::String loadedIdentifier;
        if (slotIdx == -1)
        {
            if (auto* vstiCh = dynamic_cast<VSTiChannel*>(channel))
                if (auto* vsti = vstiCh->getVSTi())
                    loadedIdentifier = vsti->getPluginDescription().createIdentifierString();
        }
        else
        {
            if (auto* plugin = channel->getPlugin(slotIdx))
                loadedIdentifier = plugin->getPluginDescription().createIdentifierString();
        }

        if (loadedIdentifier.isNotEmpty())
        {
            for (int i = 1; i < pluginIdentifiers.size(); ++i)
            {
                if (pluginIdentifiers[i] == loadedIdentifier)
                {
                    preSelectedId = i + 1;  // ComboBox ID = array index + 1
                    break;
                }
            }
        }
    }
    pluginComboBox.setSelectedId(preSelectedId, juce::dontSendNotification);
}

void PluginSlotRow::loadSelected()
{
    const int selectedId = pluginComboBox.getSelectedId();
    if (selectedId <= 1)
        return;  // Platzhalter gewählt → nichts tun

    const int arrayIdx = selectedId - 2;  // Offset: ID 2 = Index 0 in pluginIdentifiers (ab Index 1)
    if (arrayIdx + 1 >= static_cast<int>(pluginIdentifiers.size()))
        return;

    const juce::String identifier = pluginIdentifiers[arrayIdx + 1];
    if (identifier.isEmpty())
        return;

    audioEngine.loadPluginAsync(channelIdx, slotIdx, identifier);
    DBG("Plugin laden: " + identifier + " → Ch" + juce::String(channelIdx) +
        " Slot " + juce::String(slotIdx));
}

void PluginSlotRow::removePlugin()
{
    audioEngine.removePlugin(channelIdx, slotIdx);
    pluginComboBox.setSelectedId(1, juce::dontSendNotification);
}

void PluginSlotRow::toggleBypass()
{
    if (slotIdx < 0) return;
    auto* channel = audioEngine.getChannel(channelIdx);
    if (!channel) return;

    const bool bypassed = bypassButton.getToggleState();
    channel->setPluginBypassed(slotIdx, bypassed);
    bypassButton.setButtonText(bypassed ? "B!" : "B");
}

void PluginSlotRow::openEditor()
{
    juce::AudioPluginInstance* plugin = nullptr;
    auto* channel = audioEngine.getChannel(channelIdx);
    if (!channel) return;

    if (slotIdx == -1)
    {
        if (auto* vstiCh = dynamic_cast<VSTiChannel*>(channel))
            plugin = vstiCh->getVSTi();
    }
    else
    {
        plugin = channel->getPlugin(slotIdx);
    }

    if (!plugin)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "No Plugin",
            "No plugin is loaded in this slot.");
        return;
    }

    // Re-open guard: if editor is already open, bring it to front
    if (auto* existing = plugin->getActiveEditor())
    {
        if (auto* w = existing->findParentComponentOfClass<juce::ResizableWindow>())
            w->toFront(true);
        else
            existing->toFront(true);
        return;
    }

    auto* editor = plugin->createEditor();
    if (!editor)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "No Editor",
            plugin->getName() + " provides no GUI editor.");
        return;
    }

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(editor);
    opts.dialogTitle                  = plugin->getName();
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar            = true;
    opts.resizable                    = true;
    opts.launchAsync();
}

void PluginSlotRow::refresh()
{
    populateComboBox();
}

void PluginSlotRow::resized()
{
    auto area = getLocalBounds();

    slotLabel.setBounds(area.removeFromLeft(32));

    if (slotIdx >= 0)
    {
        removeButton.setBounds(area.removeFromRight(22));
        bypassButton.setBounds(area.removeFromRight(22));
        editButton  .setBounds(area.removeFromRight(22));
    }
    else
    {
        removeButton.setBounds(area.removeFromRight(22));
        editButton  .setBounds(area.removeFromRight(22));
    }

    area.reduce(2, 0);
    pluginComboBox.setBounds(area);
}

//==============================================================================
// PluginManagerComponent
//==============================================================================

PluginManagerComponent::PluginManagerComponent(AudioEngine& engine, int channelIndex)
    : audioEngine(engine)
    , channelIdx(channelIndex)
{
    isVSTi = (audioEngine.getChannelType(channelIdx) == ChannelType::VSTi);

    // Channel-Typ-Buttons
    audioTypeButton.setClickingTogglesState(false);
    audioTypeButton.onClick = [this] { setChannelType(ChannelType::Audio); };
    addAndMakeVisible(audioTypeButton);

    vstiTypeButton.setClickingTogglesState(false);
    vstiTypeButton.onClick = [this] { setChannelType(ChannelType::VSTi); };
    addAndMakeVisible(vstiTypeButton);

    // Scan-Button — always visible so the user can rescan after installing new plugins
    scanButton.onClick = [this]
    {
        scanButton.setEnabled(false);
        scanButton.setButtonText("Scanning...");
        audioEngine.getPluginHost().scanForPlugins(false);
        scanButton.setButtonText("Rescan Plugins");
        scanButton.setEnabled(true);
        rebuildSlots();
        setSize(kWidth, calcHeight());
    };
    scanButton.setButtonText(audioEngine.getPluginHost().getNumPlugins() == 0
                             ? "Scan for Plugins" : "Rescan Plugins");
    addAndMakeVisible(scanButton);

    rebuildSlots();
    setSize(kWidth, calcHeight());
}

void PluginManagerComponent::setChannelType(ChannelType type)
{
    audioEngine.setChannelType(channelIdx, type);
    isVSTi = (type == ChannelType::VSTi);
    rebuildSlots();
    setSize(kWidth, calcHeight());

    // CallOutBox neu layouten
   // if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
     //   box->updatePosition(box->getTargetScreenArea(), box->getParentMonitorArea());
}

void PluginManagerComponent::rebuildSlots()
{
    // Vorhandene Slots entfernen
    vstiSlot.reset();
    for (auto& s : fxSlots) s.reset();

    // VSTi-Slot (nur wenn VSTi-Channel)
    if (isVSTi)
    {
        vstiSlot = std::make_unique<PluginSlotRow>(audioEngine, channelIdx, -1);
        addAndMakeVisible(*vstiSlot);
    }

    // 3 FX-Slots
    for (int i = 0; i < 3; ++i)
    {
        fxSlots[i] = std::make_unique<PluginSlotRow>(audioEngine, channelIdx, i);
        addAndMakeVisible(*fxSlots[i]);
    }

    resized();
}

int PluginManagerComponent::calcHeight() const
{
    int rows = 3;         // Immer 3 FX-Slots
    if (isVSTi) ++rows;   // VSTi-Slot

    return 10              // Top-Padding
         + 28              // Typ-Buttons
         + 6               // Abstand
         + 32              // Scan button (always present)
         + rows * (kRowH + 4)
         + 10;             // Bottom-Padding
}

void PluginManagerComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));

    // Aktiven Typ hervorheben
    auto btnArea = getLocalBounds().reduced(10).removeFromTop(28);
    auto audioBounds = btnArea.removeFromLeft(btnArea.getWidth() / 2).reduced(2);
    auto vstiBounds  = btnArea.reduced(2);

    if (!isVSTi)
        g.setColour(juce::Colour(0xff0077cc));
    else
        g.setColour(juce::Colours::transparentBlack);
    g.fillRect(audioBounds);

    if (isVSTi)
        g.setColour(juce::Colour(0xff0077cc));
    else
        g.setColour(juce::Colours::transparentBlack);
    g.fillRect(vstiBounds);
}

void PluginManagerComponent::resized()
{
    auto area = getLocalBounds().reduced(10);

    // Typ-Buttons
    auto typeRow = area.removeFromTop(28);
    audioTypeButton.setBounds(typeRow.removeFromLeft(typeRow.getWidth() / 2).reduced(2));
    vstiTypeButton.setBounds(typeRow.reduced(2));

    area.removeFromTop(6);

    // Scan-Button (always in layout)
    scanButton.setBounds(area.removeFromTop(28));
    area.removeFromTop(4);

    // VSTi-Slot
    if (vstiSlot)
    {
        vstiSlot->setBounds(area.removeFromTop(kRowH));
        area.removeFromTop(4);
    }

    // Separator
    if (vstiSlot)
        area.removeFromTop(2);

    // FX-Slots
    for (auto& slot : fxSlots)
    {
        if (slot)
        {
            slot->setBounds(area.removeFromTop(kRowH));
            area.removeFromTop(4);
        }
    }
}
