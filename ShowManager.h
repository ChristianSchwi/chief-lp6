#pragma once

#include <JuceHeader.h>
#include "Song.h"
#include "SongManager.h"

/**
 * @file ShowManager.h
 * @brief Show (multi-song) management
 * 
 * A Show is a collection of Songs that can be performed in sequence.
 * This is useful for live performances where you need to switch between
 * different songs quickly.
 */

//==============================================================================
/**
 * @brief Show (multi-song collection) manager
 * 
 * Manages:
 * - Show metadata and song list
 * - Show save/load (show.json)
 * - Song switching logic
 */
class ShowManager
{
public:
    ShowManager();
    ~ShowManager();
    
    //==========================================================================
    // Show Operations
    //==========================================================================
    
    /**
     * @brief Save show to file
     * @param show Show data to save
     * @param showFile File to write to (show.json)
     * @return Result with error message if failed
     */
    juce::Result saveShow(const Show& show, const juce::File& showFile);
    
    /**
     * @brief Load show from file
     * @param showFile File to read from (show.json)
     * @param show Output: loaded show data
     * @return Result with error message if failed
     */
    juce::Result loadShow(const juce::File& showFile, Show& show);
    
    //==========================================================================
    // Show Management
    //==========================================================================
    
    /**
     * @brief Create new show
     * @param showName Name of show
     * @param parentDirectory Directory to create show in
     * @return Created show file, or invalid File if failed
     */
    static juce::File createNewShow(const juce::String& showName,
                                    const juce::File& parentDirectory);
    
    /**
     * @brief Add song to show
     * @param show Show to modify
     * @param songDirectory Song directory to add
     */
    static void addSongToShow(Show& show, const juce::File& songDirectory);
    
    /**
     * @brief Remove song from show
     * @param show Show to modify
     * @param songIndex Index of song to remove
     */
    static void removeSongFromShow(Show& show, int songIndex);
    
    /**
     * @brief Reorder songs in show
     * @param show Show to modify
     * @param fromIndex Current index
     * @param toIndex Desired index
     */
    static void reorderSongs(Show& show, int fromIndex, int toIndex);
    
private:
    //==========================================================================
    // JSON Serialization
    //==========================================================================
    
    /**
     * @brief Serialize show to JSON
     */
    juce::var showToJSON(const Show& show);
    
    /**
     * @brief Deserialize show from JSON
     */
    juce::Result jsonToShow(const juce::var& json, Show& show);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShowManager)
};
