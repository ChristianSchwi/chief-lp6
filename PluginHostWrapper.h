#pragma once

#include <JuceHeader.h>
#include <memory>
#include <functional>

/**
 * @file PluginHostWrapper.h
 * @brief VST3 Plugin hosting and management
 * 
 * This class handles:
 * - Plugin scanning and discovery
 * - Async plugin loading
 * - Plugin state serialization (Base64)
 * - Plugin description management
 * - Known plugins list persistence
 */

//==============================================================================
/**
 * @brief Callback for async plugin loading
 * @param plugin Loaded plugin instance (nullptr if failed)
 * @param errorMessage Error description if loading failed
 */
using PluginLoadCallback = std::function<void(std::unique_ptr<juce::AudioPluginInstance>, 
                                               const juce::String& errorMessage)>;

//==============================================================================
/**
 * @brief VST3 Plugin host wrapper
 * 
 * Manages plugin discovery, loading, and state persistence.
 * All plugin loading is asynchronous to avoid blocking the message thread.
 * 
 * Usage:
 * 1. scanForPlugins() - Discover available plugins
 * 2. loadPluginAsync() - Load a plugin by description
 * 3. savePluginState() / loadPluginState() - Persist plugin settings
 */
class PluginHostWrapper
{
public:
    //==========================================================================
    PluginHostWrapper();
    ~PluginHostWrapper();
    
    //==========================================================================
    // Plugin Scanning
    //==========================================================================
    
    /**
     * @brief Scan for available VST3 plugins
     * @param showProgress If true, displays progress window
     * @return Number of plugins found
     */
    int scanForPlugins(bool showProgress = true);
    
    /**
     * @brief Rescan plugins (useful after installing new plugins)
     */
    void rescanPlugins(bool showProgress = true);
    
    /**
     * @brief Get list of all known plugins
     */
    const juce::KnownPluginList& getKnownPlugins() const { return knownPlugins; }
    
    /**
     * @brief Get plugin descriptions sorted by category
     * @return Map of category -> plugin list
     */
    std::map<juce::String, juce::Array<juce::PluginDescription>> 
        getPluginsByCategory() const;
    
    /**
     * @brief Search plugins by name
     * @param searchTerm Search string (case-insensitive)
     * @return Matching plugin descriptions
     */
    juce::Array<juce::PluginDescription> searchPlugins(const juce::String& searchTerm) const;
    
    //==========================================================================
    // Plugin Loading
    //==========================================================================
    
    /**
     * @brief Load a plugin asynchronously
     * @param description Plugin to load
     * @param sampleRate Sample rate for plugin
     * @param maxBlockSize Maximum buffer size
     * @param callback Called when loading completes (success or failure)
     */
    void loadPluginAsync(const juce::PluginDescription& description,
                        double sampleRate,
                        int maxBlockSize,
                        PluginLoadCallback callback);
    
    /**
     * @brief Load a plugin synchronously (blocks until loaded)
     * WARNING: Should only be used in non-realtime contexts
     * @param description Plugin to load
     * @param sampleRate Sample rate for plugin
     * @param maxBlockSize Maximum buffer size
     * @param errorMessage Output: error description if failed
     * @return Plugin instance or nullptr if failed
     */
    std::unique_ptr<juce::AudioPluginInstance> loadPluginSync(
        const juce::PluginDescription& description,
        double sampleRate,
        int maxBlockSize,
        juce::String& errorMessage);
    
    //==========================================================================
    // Plugin State Management
    //==========================================================================
    
    /**
     * @brief Save plugin state to memory block
     * @param plugin Plugin instance
     * @return State as memory block
     */
    juce::MemoryBlock savePluginState(juce::AudioPluginInstance* plugin);
    
    /**
     * @brief Load plugin state from memory block
     * @param plugin Plugin instance
     * @param state Previously saved state
     * @return true if state was loaded successfully
     */
    bool loadPluginState(juce::AudioPluginInstance* plugin, 
                        const juce::MemoryBlock& state);
    
    /**
     * @brief Convert memory block to Base64 string (for JSON storage)
     */
    static juce::String memoryBlockToBase64(const juce::MemoryBlock& block);
    
    /**
     * @brief Convert Base64 string back to memory block
     */
    static juce::MemoryBlock base64ToMemoryBlock(const juce::String& base64);
    
    //==========================================================================
    // Known Plugins Persistence
    //==========================================================================
    
    /**
     * @brief Save known plugins list to file
     * @param file File to save to (typically .xml)
     * @return true if saved successfully
     */
    bool saveKnownPluginList(const juce::File& file);
    
    /**
     * @brief Load known plugins list from file
     * @param file File to load from
     * @return true if loaded successfully
     */
    bool loadKnownPluginList(const juce::File& file);
    
    /**
     * @brief Get default file for known plugins list
     * @return File in app data directory
     */
    juce::File getDefaultPluginListFile() const;
    
    //==========================================================================
    // Utilities
    //==========================================================================
    
    /**
     * @brief Get number of known plugins
     */
    int getNumPlugins() const { return knownPlugins.getNumTypes(); }
    
    /**
 * @brief Find a plugin by its identifier string
 * @param identifier Identifier as returned by PluginDescription::createIdentifierString()
 * @return PluginDescription (name will be empty if not found)
 */
    juce::PluginDescription findPluginByIdentifier(const juce::String& identifier) const;

    /**
     * @brief Check if any plugins are available
     */
    bool hasPlugins() const { return getNumPlugins() > 0; }
    
    /**
     * @brief Get VST3 plugin paths being scanned
     */
    juce::StringArray getVST3Paths() const;
    
    /**
     * @brief Add custom VST3 path to scan
     */
    void addCustomVST3Path(const juce::File& path);
    
private:
    //==========================================================================
    // Plugin format manager
    juce::AudioPluginFormatManager formatManager;
    
    // Known plugins database
    juce::KnownPluginList knownPlugins;
    
    // Custom plugin paths
    juce::StringArray customVST3Paths;
    
    //==========================================================================
    // Helper methods
    
    /**
     * @brief Get default VST3 directories for current platform
     */
    static juce::FileSearchPath getDefaultVST3Paths();
    
    /**
     * @brief Create plugin format manager with VST3 support
     */
    void setupFormatManager();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHostWrapper)
};

//==============================================================================
/**
 * @brief Plugin scanner with progress reporting
 * 
 * Used for background plugin scanning with UI feedback
 */
class PluginScanner : public juce::Thread
{
public:
    PluginScanner(juce::KnownPluginList& listToUpdate,
                 juce::AudioPluginFormatManager& formatManager,
                 const juce::FileSearchPath& paths);
    
    ~PluginScanner() override;
    
    void run() override;
    
    // Progress info
    float getProgress() const { return progress.load(); }
    juce::String getCurrentPlugin() const;
    int getNumFound() const { return numFound.load(); }
    bool hasFinished() const { return finished.load(); }
    
private:
    juce::KnownPluginList& knownPlugins;
    juce::AudioPluginFormatManager& formats;
    juce::FileSearchPath searchPaths;
    
    std::atomic<float> progress{0.0f};
    std::atomic<int> numFound{0};
    std::atomic<bool> finished{false};
    
    juce::CriticalSection currentPluginLock;
    juce::String currentPluginName;
};
