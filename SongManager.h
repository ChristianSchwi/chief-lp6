#pragma once

#include <JuceHeader.h>
#include "Song.h"
#include "Channel.h"
#include "AudioEngine.h"

/**
 * @file SongManager.h
 * @brief Song persistence manager
 * 
 * Handles:
 * - JSON serialization/deserialization
 * - Loop file I/O (.loop files)
 * - Plugin state persistence
 * - Song directory management
 */

//==============================================================================
/**
 * @brief Song persistence manager
 * 
 * Manages complete song save/load including:
 * - song.json metadata
 * - channel_N.loop audio files
 * - Plugin states (Base64 in JSON)
 */
class SongManager
{
public:
    SongManager();
    ~SongManager();
    
    //==========================================================================
    // Song Operations
    //==========================================================================
    
    /**
     * @brief Save complete song to directory
     * @param song Song data to save
     * @param audioEngine Audio engine for reading current state
     * @return Result with error message if failed
     */
    juce::Result saveSong(Song& song, AudioEngine& audioEngine);
    
    /**
     * @brief Load complete song from directory
     * @param songFile Path to song.json file
     * @param song Output: loaded song data
     * @return Result with error message if failed
     */
    juce::Result loadSong(const juce::File& songFile, Song& song);
    
    /**
     * @brief Apply loaded song to audio engine
     * @param song Song data to apply
     * @param audioEngine Audio engine to configure
     * @return Result with error message if failed
     */
    juce::Result applySongToEngine(const Song& song, AudioEngine& audioEngine);
    
    //==========================================================================
    // Loop File I/O
    //==========================================================================
    
    /**
     * @brief Save loop buffer to .loop file
     * @param file File to write to
     * @param buffer Audio buffer containing loop data
     * @param numSamples Number of samples to save
     * @return Result with error message if failed
     */
    juce::Result saveLoopFile(const juce::File& file,
                             const juce::AudioBuffer<float>& buffer,
                             juce::int64 numSamples);
    
    /**
     * @brief Load loop buffer from .loop file
     * @param file File to read from
     * @param buffer Output: audio buffer to fill
     * @param maxSamples Maximum samples to read
     * @return Number of samples read, or -1 if failed
     */
    juce::int64 loadLoopFile(const juce::File& file,
                      juce::AudioBuffer<float>& buffer,
                      juce::int64 maxSamples);
    
    //==========================================================================
    // Utilities
    //==========================================================================
    
    /**
     * @brief Create new song directory
     * @param parentDirectory Parent directory for song
     * @param songName Name of song (will be directory name)
     * @return Created song directory, or invalid File if failed
     */
    static juce::File createSongDirectory(const juce::File& parentDirectory,
                                          const juce::String& songName);
    
    /**
     * @brief Validate song directory structure
     * @param songDirectory Directory to check
     * @return true if valid song directory
     */
    static bool isValidSongDirectory(const juce::File& songDirectory);
    
private:
    //==========================================================================
    // JSON Serialization
    //==========================================================================
    
    /**
     * @brief Serialize song to JSON
     */
    juce::var songToJSON(const Song& song);
    
    /**
     * @brief Deserialize song from JSON
     */
    juce::Result jsonToSong(const juce::var& json, Song& song);
    
    /**
     * @brief Serialize channel config to JSON
     */
    juce::var channelToJSON(const ChannelConfig& channel);
    
    /**
     * @brief Deserialize channel config from JSON
     */
    juce::Result jsonToChannel(const juce::var& json, ChannelConfig& channel);
    
    /**
     * @brief Serialize plugin data to JSON
     */
    juce::var pluginToJSON(const PluginData& plugin);
    
    /**
     * @brief Deserialize plugin data from JSON
     */
    juce::Result jsonToPlugin(const juce::var& json, PluginData& plugin);
    
    /**
     * @brief Serialize routing config to JSON
     */
    juce::var routingToJSON(const RoutingConfig& routing);
    
    /**
     * @brief Deserialize routing config from JSON
     */
    juce::Result jsonToRouting(const juce::var& json, RoutingConfig& routing);
    
    //==========================================================================
    // Helper Methods
    //==========================================================================
    
    /**
     * @brief Read channel state from audio engine
     */
    ChannelConfig readChannelState(Channel* channel, 
                                   AudioEngine& audioEngine,
                                   int channelIndex);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongManager)
};
