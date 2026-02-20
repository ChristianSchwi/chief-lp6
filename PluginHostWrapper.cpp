#include "PluginHostWrapper.h"

//==============================================================================
PluginHostWrapper::PluginHostWrapper()
{
    setupFormatManager();
    
    // Try to load previously scanned plugins
    auto defaultFile = getDefaultPluginListFile();
    if (defaultFile.existsAsFile())
    {
        loadKnownPluginList(defaultFile);
    }
}

PluginHostWrapper::~PluginHostWrapper()
{
    // Save known plugins list on exit
    auto defaultFile = getDefaultPluginListFile();
    saveKnownPluginList(defaultFile);
}

//==============================================================================
// Setup
//==============================================================================

void PluginHostWrapper::setupFormatManager()
{
    // Add VST3 format
    formatManager.addDefaultFormats();
    
    DBG("Plugin formats available:");
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat(i);
        DBG("  - " + format->getName());
    }
}

juce::FileSearchPath PluginHostWrapper::getDefaultVST3Paths()
{
    juce::FileSearchPath paths;
    
#if JUCE_WINDOWS
    // Windows VST3 paths
    paths.add(juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory)
             .getChildFile("VST3"));
    paths.add(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
             .getChildFile("VST3"));
    paths.add(juce::File("C:\\Program Files\\Common Files\\VST3"));
    
#elif JUCE_MAC
    // macOS VST3 paths
    paths.add(juce::File("/Library/Audio/Plug-Ins/VST3"));
    paths.add(juce::File("~/Library/Audio/Plug-Ins/VST3").getFullPathName());
    
#elif JUCE_LINUX
    // Linux VST3 paths
    paths.add(juce::File("~/.vst3").getFullPathName());
    paths.add(juce::File("/usr/lib/vst3"));
    paths.add(juce::File("/usr/local/lib/vst3"));
#endif
    
    return paths;
}

//==============================================================================
// Plugin Scanning
//==============================================================================

int PluginHostWrapper::scanForPlugins(bool showProgress)
{
    knownPlugins.clear();
    
    juce::FileSearchPath searchPaths = getDefaultVST3Paths();
    
    // Add custom paths
    for (const auto& customPath : customVST3Paths)
        searchPaths.add(juce::File(customPath));
    
    DBG("Scanning for plugins in:");
    for (int i = 0; i < searchPaths.getNumPaths(); ++i)
    {
        DBG("  " + searchPaths[i].getFullPathName());
    }
    
    // Find VST3 format
    juce::AudioPluginFormat* vst3Format = nullptr;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat(i);
        if (format->getName().contains("VST3"))
        {
            vst3Format = format;
            break;
        }
    }
    
    if (!vst3Format)
    {
        DBG("ERROR: VST3 format not found!");
        return 0;
    }
    
    DBG("Using format: " + vst3Format->getName());
    
    // Scan for VST3 plugins
    juce::PluginDirectoryScanner scanner(knownPlugins,
                                         *vst3Format,
                                         searchPaths,
                                         true,   // Search recursively
                                         juce::File());
    
    juce::String pluginBeingScanned;
    
    while (scanner.scanNextFile(true, pluginBeingScanned))
    {
        if (showProgress)
        {
            DBG("Scanning: " + pluginBeingScanned + 
                " (" + juce::String(knownPlugins.getNumTypes()) + " found)");
        }
    }
    
    DBG("Plugin scan complete. Found " + juce::String(knownPlugins.getNumTypes()) + " plugins");
    
    // Auto-save the list
    saveKnownPluginList(getDefaultPluginListFile());
    
    return knownPlugins.getNumTypes();
}

void PluginHostWrapper::rescanPlugins(bool showProgress)
{
    scanForPlugins(showProgress);
}

std::map<juce::String, juce::Array<juce::PluginDescription>> 
    PluginHostWrapper::getPluginsByCategory() const
{
    std::map<juce::String, juce::Array<juce::PluginDescription>> categorized;
    
    for (const auto& type : knownPlugins.getTypes())
    {
        juce::String category = type.category.isEmpty() ? "Other" : type.category;
        categorized[category].add(type);
    }
    
    return categorized;
}

juce::Array<juce::PluginDescription> 
    PluginHostWrapper::searchPlugins(const juce::String& searchTerm) const
{
    juce::Array<juce::PluginDescription> results;
    
    juce::String lowerSearch = searchTerm.toLowerCase();
    
    for (const auto& type : knownPlugins.getTypes())
    {
        if (type.name.toLowerCase().contains(lowerSearch) ||
            type.manufacturerName.toLowerCase().contains(lowerSearch) ||
            type.category.toLowerCase().contains(lowerSearch))
        {
            results.add(type);
        }
    }
    
    return results;
}

//==============================================================================
// Plugin Loading
//==============================================================================

void PluginHostWrapper::loadPluginAsync(const juce::PluginDescription& description,
                                        double sampleRate,
                                        int maxBlockSize,
                                        PluginLoadCallback callback)
{
    formatManager.createPluginInstanceAsync(
        description,
        sampleRate,
        maxBlockSize,
        [callback](std::unique_ptr<juce::AudioPluginInstance> instance,
                   const juce::String& error)
        {
            if (instance)
            {
                DBG("Plugin loaded successfully: " + instance->getName());
            }
            else
            {
                DBG("Plugin load failed: " + error);
            }
            
            callback(std::move(instance), error);
        });
}

std::unique_ptr<juce::AudioPluginInstance> 
    PluginHostWrapper::loadPluginSync(const juce::PluginDescription& description,
                                     double sampleRate,
                                     int maxBlockSize,
                                     juce::String& errorMessage)
{
    errorMessage = juce::String();
    
    juce::String error;
    auto instance = formatManager.createPluginInstance(description,
                                                       sampleRate,
                                                       maxBlockSize,
                                                       error);
    
    if (!instance)
    {
        errorMessage = error;
        DBG("Plugin load failed: " + error);
        return nullptr;
    }
    
    DBG("Plugin loaded successfully: " + instance->getName());
    return instance;
}

//==============================================================================
// Plugin State Management
//==============================================================================

juce::MemoryBlock PluginHostWrapper::savePluginState(juce::AudioPluginInstance* plugin)
{
    if (!plugin)
        return juce::MemoryBlock();
    
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    
    DBG("Plugin state saved: " + juce::String(state.getSize()) + " bytes");
    return state;
}

bool PluginHostWrapper::loadPluginState(juce::AudioPluginInstance* plugin,
                                       const juce::MemoryBlock& state)
{
    if (!plugin || state.isEmpty())
        return false;
    
    try
    {
        plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        DBG("Plugin state loaded: " + juce::String(state.getSize()) + " bytes");
        return true;
    }
    catch (...)
    {
        DBG("Failed to load plugin state");
        return false;
    }
}

juce::String PluginHostWrapper::memoryBlockToBase64(const juce::MemoryBlock& block)
{
    return block.toBase64Encoding();
}

juce::MemoryBlock PluginHostWrapper::base64ToMemoryBlock(const juce::String& base64)
{
    juce::MemoryBlock block;
    block.fromBase64Encoding(base64);
    return block;
}

//==============================================================================
// Known Plugins Persistence
//==============================================================================

bool PluginHostWrapper::saveKnownPluginList(const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    
    auto xml = knownPlugins.createXml();
    if (!xml)
        return false;
    
    bool success = xml->writeTo(file);
    
    if (success)
    {
        DBG("Saved plugin list to: " + file.getFullPathName());
    }
    else
    {
        DBG("Failed to save plugin list to: " + file.getFullPathName());
    }
    
    return success;
}

bool PluginHostWrapper::loadKnownPluginList(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;
    
    auto xml = juce::parseXML(file);
    if (!xml)
        return false;
    
    knownPlugins.recreateFromXml(*xml);
    
    DBG("Loaded plugin list from: " + file.getFullPathName());
    DBG("  " + juce::String(knownPlugins.getNumTypes()) + " plugins loaded");
    
    return true;
}

juce::File PluginHostWrapper::getDefaultPluginListFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("chief")
        .getChildFile("KnownPlugins.xml");
}

//==============================================================================
// Utilities
//==============================================================================

juce::StringArray PluginHostWrapper::getVST3Paths() const
{
    juce::StringArray paths;
    
    auto defaultPaths = getDefaultVST3Paths();
    for (int i = 0; i < defaultPaths.getNumPaths(); ++i)
    {
        paths.add(defaultPaths[i].getFullPathName());
    }
    
    paths.addArray(customVST3Paths);
    
    return paths;
}

void PluginHostWrapper::addCustomVST3Path(const juce::File& path)
{
    customVST3Paths.addIfNotAlreadyThere(path.getFullPathName());
}

//==============================================================================
// PluginScanner Implementation
//==============================================================================

PluginScanner::PluginScanner(juce::KnownPluginList& listToUpdate,
                             juce::AudioPluginFormatManager& formatManager,
                             const juce::FileSearchPath& paths)
    : juce::Thread("PluginScanner")
    , knownPlugins(listToUpdate)
    , formats(formatManager)
    , searchPaths(paths)
{
}

PluginScanner::~PluginScanner()
{
    stopThread(5000);
}

void PluginScanner::run()
{
    // Note: knownPlugins should be cleared by caller before starting thread
    // We don't clear here to avoid race conditions
    
    for (int formatIdx = 0; formatIdx < formats.getNumFormats(); ++formatIdx)
    {
        if (threadShouldExit())
            break;
        
        auto* format = formats.getFormat(formatIdx);
        
        juce::PluginDirectoryScanner scanner(knownPlugins,
                                             *format,
                                             searchPaths,
                                             true,
                                             juce::File());
        
        juce::String pluginName;
        
        while (!threadShouldExit() && scanner.scanNextFile(true, pluginName))
        {
            {
                juce::ScopedLock lock(currentPluginLock);
                currentPluginName = pluginName;
            }
            
            numFound.store(knownPlugins.getNumTypes(), std::memory_order_release);
            progress.store(scanner.getProgress(), std::memory_order_release);
        }
    }
    
    finished.store(true, std::memory_order_release);
}

juce::String PluginScanner::getCurrentPlugin() const
{
    juce::ScopedLock lock(currentPluginLock);
    return currentPluginName;
}

juce::PluginDescription PluginHostWrapper::findPluginByIdentifier(const juce::String& identifier) const
{
    if (auto found = knownPlugins.getTypeForIdentifierString(identifier))
        return *found;

    return juce::PluginDescription{};  // Leere Description wenn nicht gefunden
}
