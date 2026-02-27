#include "ShowManager.h"

//==============================================================================
ShowManager::ShowManager()
{
}

ShowManager::~ShowManager()
{
}

//==============================================================================
// Show Save/Load
//==============================================================================

juce::Result ShowManager::saveShow(const Show& show, const juce::File& showFile)
{
    // Serialize to JSON
    auto json = showToJSON(show);
    
    // Write to file
    auto jsonString = juce::JSON::toString(json, true);  // Pretty print
    
    if (!showFile.replaceWithText(jsonString))
    {
        return juce::Result::fail("Failed to write show file: " + showFile.getFullPathName());
    }
    
    DBG("Show saved: " + showFile.getFullPathName());
    return juce::Result::ok();
}

juce::Result ShowManager::loadShow(const juce::File& showFile, Show& show)
{
    if (!showFile.existsAsFile())
        return juce::Result::fail("Show file not found: " + showFile.getFullPathName());

    // Set showFile before jsonToShow — it uses it to resolve relative song paths
    show.showFile = showFile;

    // Read JSON
    juce::String jsonString = showFile.loadFileAsString();
    auto json = juce::JSON::parse(jsonString);

    if (!json.isObject())
        return juce::Result::fail("Invalid JSON in show file");

    // Deserialize
    auto result = jsonToShow(json, show);
    if (result.failed())
        return result;

    DBG("Show loaded: " + showFile.getFullPathName());
    DBG("  Songs: " + juce::String(show.getNumSongs()));
    
    return juce::Result::ok();
}

//==============================================================================
// Show Management
//==============================================================================

juce::File ShowManager::createNewShow(const juce::String& showName,
                                      const juce::File& parentDirectory)
{
    // Sanitize show name
    juce::String safeName = showName.trim();
    safeName = safeName.replaceCharacters("/\\:*?\"<>|", "_________");
    
    if (safeName.isEmpty())
        safeName = "Untitled Show";
    
    auto showFile = parentDirectory.getChildFile(safeName + ".show");
    
    // Make unique if exists
    int suffix = 1;
    auto originalFile = showFile;
    while (showFile.exists())
    {
        showFile = originalFile.getSiblingFile(safeName + " " + juce::String(suffix) + ".show");
        ++suffix;
    }
    
    // Create empty show
    Show show;
    show.showName = safeName;
    show.showFile = showFile;
    
    ShowManager manager;
    auto result = manager.saveShow(show, showFile);
    
    if (result.failed())
        return juce::File();
    
    return showFile;
}

void ShowManager::addSongToShow(Show& show, const juce::File& songDirectory)
{
    if (songDirectory.exists() && songDirectory.isDirectory())
    {
        show.addSong(songDirectory);
    }
}

void ShowManager::removeSongFromShow(Show& show, int songIndex)
{
    show.removeSong(songIndex);
}

void ShowManager::reorderSongs(Show& show, int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= show.getNumSongs())
        return;
    
    if (toIndex < 0 || toIndex >= show.getNumSongs())
        return;
    
    if (fromIndex == toIndex)
        return;
    
    // Remove from old position
    auto song = show.songPaths[fromIndex];
    show.songPaths.remove(fromIndex);
    
    // Insert at new position
    show.songPaths.insert(toIndex, song);
}

//==============================================================================
// JSON Serialization
//==============================================================================

juce::var ShowManager::showToJSON(const Show& show)
{
    auto* obj = new juce::DynamicObject();
    
    obj->setProperty("format_version", show.formatVersion);
    obj->setProperty("show_name", show.showName);
    obj->setProperty("description", show.description);
    
    // Song paths (relative to show file if possible)
    juce::Array<juce::var> songsArray;
    auto showDir = show.showFile.getParentDirectory();
    
    for (const auto& songPath : show.songPaths)
    {
        // Try to make relative path
        juce::String relativePath = songPath.getRelativePathFrom(showDir);
        if (relativePath.isEmpty())
            relativePath = songPath.getFullPathName();
        
        songsArray.add(relativePath);
    }
    
    obj->setProperty("songs", songsArray);
    
    return juce::var(obj);
}

juce::Result ShowManager::jsonToShow(const juce::var& json, Show& show)
{
    if (!json.isObject())
        return juce::Result::fail("JSON is not an object");
    
    auto* obj = json.getDynamicObject();
    
    show.formatVersion = obj->getProperty("format_version").toString();
    show.showName = obj->getProperty("show_name").toString();
    show.description = obj->getProperty("description").toString();
    
    // Load song paths
    show.songPaths.clear();
    
    auto* songsArray = obj->getProperty("songs").getArray();
    if (songsArray)
    {
        auto showDir = show.showFile.getParentDirectory();
        
        for (const auto& songVar : *songsArray)
        {
            juce::String pathStr = songVar.toString();
            
            // Try as relative path first
            auto songPath = showDir.getChildFile(pathStr);
            if (!songPath.exists())
            {
                // Try as absolute path
                songPath = juce::File(pathStr);
            }
            
            if (songPath.exists() && songPath.isDirectory())
            {
                show.songPaths.add(songPath);
            }
            else
            {
                DBG("Warning: Song path not found: " + pathStr);
            }
        }
    }
    
    return juce::Result::ok();
}
