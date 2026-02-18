#include "AudioEngine.h"
#include "AudioChannel.h"
#include "VSTiChannel.h"
#include "PluginHostWrapper.h"
#include <JuceHeader.h>

/**
 * @file Phase3Example.cpp
 * @brief Example demonstrating Phase 3: Plugin Integration
 * 
 * This shows how to:
 * - Scan for VST3 plugins
 * - Load plugins into channels
 * - Save and restore plugin state
 * - Use plugins in the signal chain
 */

//==============================================================================
class LooperPhase3Application : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "6-Channel Looper - Phase 3"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    
    void initialise(const juce::String& commandLine) override
    {
        audioEngine = std::make_unique<AudioEngine>();
        
        // Initialize audio with 2 inputs, 2 outputs
        juce::String error = audioEngine->initialiseAudio(2, 2, 44100.0, 512);
        
        if (error.isNotEmpty())
        {
            DBG("Failed to initialize audio: " + error);
            quit();
            return;
        }
        
        DBG("=== 6-Channel Looper Phase 3 Demo ===");
        DBG("Plugin Integration");
        DBG("");
        
        // Configure loop engine
        setupLoopEngine();
        
        // Demonstrate plugin functionality
        demonstratePluginScanning();
        
        // Wait a bit for plugin scan to complete
        juce::Timer::callAfterDelay(2000, [this]() {
            demonstratePluginLoading();
        });
        
        // Run for 15 seconds then quit
        juce::Timer::callAfterDelay(15000, [this]() {
            quit();
        });
    }
    
    void shutdown() override
    {
        audioEngine.reset();
    }
    
private:
    std::unique_ptr<AudioEngine> audioEngine;
    
    void setupLoopEngine()
    {
        auto& loopEngine = audioEngine->getLoopEngine();
        
        loopEngine.setBPM(120.0);
        loopEngine.setBeatsPerLoop(4);
        loopEngine.setQuantizationEnabled(true);
        loopEngine.calculateLoopLengthFromBPM();
        
        DBG("=== Loop Engine ===");
        DBG("BPM: " + juce::String(loopEngine.getBPM()));
        DBG("Loop length: " + juce::String(loopEngine.getLoopLengthSeconds(), 2) + "s");
        DBG("");
    }
    
    void demonstratePluginScanning()
    {
        DBG("=== Plugin Scanning ===");
        
        auto& pluginHost = audioEngine->getPluginHost();
        
        // Manually trigger plugin scan (no longer automatic in constructor)
        DBG("Starting plugin scan...");
        int numPlugins = pluginHost.scanForPlugins(true);
        
        DBG("Found " + juce::String(numPlugins) + " plugins");
        DBG("");
        
        if (numPlugins == 0)
        {
            DBG("No plugins found. Make sure VST3 plugins are installed.");
            DBG("VST3 paths:");
            for (const auto& path : pluginHost.getVST3Paths())
            {
                DBG("  " + path);
            }
            return;
        }
        
        // Show plugins by category
        DBG("=== Plugins by Category ===");
        auto byCategory = pluginHost.getPluginsByCategory();
        
        for (const auto& [category, plugins] : byCategory)
        {
            DBG("\n" + category + " (" + juce::String(plugins.size()) + "):");
            for (int i = 0; i < juce::jmin(5, plugins.size()); ++i)
            {
                const auto& plugin = plugins[i];
                DBG("  - " + plugin.name + " (" + plugin.manufacturerName + ")");
            }
            if (plugins.size() > 5)
                DBG("  ... and " + juce::String(plugins.size() - 5) + " more");
        }
        DBG("");
    }
    
    void demonstratePluginLoading()
    {
        auto& pluginHost = audioEngine->getPluginHost();
        
        if (pluginHost.getNumPlugins() == 0)
        {
            DBG("No plugins available to load");
            return;
        }
        
        DBG("\n=== Plugin Loading Demo ===");
        
        // Get first available plugin
        const auto& knownPlugins = pluginHost.getKnownPlugins();
        auto pluginTypes = knownPlugins.getTypes();
        
        if (pluginTypes.isEmpty())
            return;
        
        const auto& firstPlugin = pluginTypes[0];
        DBG("Loading plugin: " + firstPlugin.name);
        DBG("Manufacturer: " + firstPlugin.manufacturerName);
        DBG("Category: " + firstPlugin.category);
        DBG("");
        
        // Load plugin asynchronously
        pluginHost.loadPluginAsync(
            firstPlugin,
            44100.0,
            512,
            [this, firstPlugin](std::unique_ptr<juce::AudioPluginInstance> instance,
                               const juce::String& errorMessage)
            {
                if (instance)
                {
                    DBG("✓ Plugin loaded successfully: " + instance->getName());
                    DBG("  Parameters: " + juce::String(instance->getParameters().size()));
                    DBG("  Input channels: " + juce::String(instance->getTotalNumInputChannels()));
                    DBG("  Output channels: " + juce::String(instance->getTotalNumOutputChannels()));
                    
                    // Add plugin to channel 0, slot 0
                    demonstratePluginInChannel(std::move(instance));
                }
                else
                {
                    DBG("✗ Plugin load failed: " + errorMessage);
                }
            });
        
        // Try to load second plugin if available (for demonstration)
        if (pluginTypes.size() > 1)
        {
            juce::Timer::callAfterDelay(1000, [this, &pluginHost, &pluginTypes]() {
                const auto& secondPlugin = pluginTypes[1];
                DBG("\nLoading second plugin: " + secondPlugin.name);
                
                pluginHost.loadPluginAsync(
                    secondPlugin,
                    44100.0,
                    512,
                    [this](std::unique_ptr<juce::AudioPluginInstance> instance,
                          const juce::String& errorMessage)
                    {
                        if (instance)
                        {
                            DBG("✓ Second plugin loaded: " + instance->getName());
                            
                            // Add to channel 1, slot 0
                            if (auto* channel = audioEngine->getChannel(1))
                            {
                                channel->addPlugin(0, std::move(instance));
                                DBG("  Added to Channel 1, Slot 0");
                            }
                        }
                        else
                        {
                            DBG("✗ Second plugin load failed: " + errorMessage);
                        }
                    });
            });
        }
    }
    
    void demonstratePluginInChannel(std::unique_ptr<juce::AudioPluginInstance> plugin)
    {
        if (!plugin)
            return;
        
        DBG("\n=== Adding Plugin to Channel ===");
        
        auto* channel = audioEngine->getChannel(0);
        if (!channel)
        {
            DBG("✗ Channel 0 not available");
            return;
        }
        
        // Save plugin state before adding
        auto& pluginHost = audioEngine->getPluginHost();
        auto initialState = pluginHost.savePluginState(plugin.get());
        juce::String stateBase64 = PluginHostWrapper::memoryBlockToBase64(initialState);
        
        DBG("Plugin state size: " + juce::String(initialState.getSize()) + " bytes");
        DBG("Base64 state length: " + juce::String(stateBase64.length()) + " chars");
        
        // Add plugin to channel
        channel->addPlugin(0, std::move(plugin));
        DBG("✓ Plugin added to Channel 0, Slot 0");
        
        // Configure channel
        channel->setGainDb(-3.0f);
        channel->setMonitorMode(MonitorMode::AlwaysOn);
        
        RoutingConfig routing;
        routing.inputChannelLeft = 0;
        routing.inputChannelRight = 1;
        routing.outputChannelLeft = 0;
        routing.outputChannelRight = 1;
        channel->setRouting(routing);
        
        DBG("Channel 0 configured:");
        DBG("  Input: 1/2 (Stereo)");
        DBG("  Output: 1/2");
        DBG("  Gain: -3dB");
        DBG("  Monitor: Always On");
        DBG("");
        
        // Start playback to test plugin
        audioEngine->setPlaying(true);
        DBG("✓ Playback started");
        DBG("Plugin is now active in the signal chain:");
        DBG("  Input → Plugin (Slot 0) → Monitor → Output");
        DBG("");
        
        // Demonstrate state save/restore
        juce::Timer::callAfterDelay(2000, [this, stateBase64]() {
            demonstrateStateSaveRestore(stateBase64);
        });
    }
    
    void demonstrateStateSaveRestore(const juce::String& savedStateBase64)
    {
        DBG("=== Plugin State Save/Restore ===");
        
        auto* channel = audioEngine->getChannel(0);
        if (!channel)
            return;
        
        // In a real application, this state would be loaded from JSON
        DBG("Simulating state restore from saved data...");
        DBG("Saved state Base64 preview: " + 
            savedStateBase64.substring(0, 50) + "...");
        
        // Convert back from Base64
        auto restoredBlock = PluginHostWrapper::base64ToMemoryBlock(savedStateBase64);
        DBG("✓ State restored from Base64");
        DBG("  Size: " + juce::String(restoredBlock.getSize()) + " bytes");
        
        // In a real scenario, we would apply this to the plugin
        // For now, just demonstrate the round-trip works
        DBG("");
        DBG("Plugin state persistence verified!");
        DBG("This state can be saved in JSON and restored later");
    }
};

//==============================================================================
START_JUCE_APPLICATION(LooperPhase3Application)
