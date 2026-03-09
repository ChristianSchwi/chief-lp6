#pragma once

#include <JuceHeader.h>
#include <array>
#include "Command.h"
#include "Channel.h"

/**
 * @file Song.h
 * @brief Song data structures for persistence
 * 
 * Defines the complete song format including:
 * - Channel configurations
 * - Plugin states
 * - Loop data references
 * - Routing configurations
 * - Global settings
 */

//==============================================================================
/**
 * @brief Plugin data for serialization
 */
struct PluginData
{
    int slotIndex{-1};                    ///< 0-2 for FX, -1 for VSTi instrument
    juce::String identifier;              ///< Plugin unique identifier
    juce::String name;                    ///< Plugin display name
    juce::String manufacturer;            ///< Plugin manufacturer
    juce::String stateBase64;             ///< Plugin state as Base64
    bool bypassed{false};                 ///< Is plugin bypassed
    
    PluginData() = default;
    
    PluginData(int slot, const juce::PluginDescription& desc, const juce::String& state)
        : slotIndex(slot)
        , identifier(desc.createIdentifierString())
        , name(desc.name)
        , manufacturer(desc.manufacturerName)
        , stateBase64(state)
    {
    }
};

//==============================================================================
/**
 * @brief Complete channel configuration for a song
 */
struct ChannelConfig
{
    ChannelType type{ChannelType::Audio};
    RoutingConfig routing;

    juce::String channelName;   ///< User-defined display name (empty = use default "CH N")

    float gainDb{0.0f};
    MonitorMode monitorMode{MonitorMode::WhenTrackActive};  // matches Channel default
    bool muted{false};
    bool solo{false};
    int  muteGroup{0};   ///< 0=none, 1-4
    bool oneShot{false};
    
    // VSTi instrument (only for VSTi channels)
    PluginData vstInstrument;
    
    // Insert FX plugins (3 slots)
    std::array<PluginData, 3> fxPlugins;
    
    // Per-section loop data
    struct SectionData
    {
        juce::String loopFileName;
        bool hasLoopData {false};
        int overdubLayerCount {0};
    };
    std::array<SectionData, NUM_SECTIONS> sectionData;

    ChannelConfig() = default;
};

//==============================================================================
/**
 * @brief Complete song data structure
 */
struct Song
{
    static constexpr const char* FORMAT_VERSION = "1.1.0";
    
    // Metadata
    juce::String formatVersion{FORMAT_VERSION};
    juce::String songName{"Untitled"};
    juce::String description;
    juce::Time creationTime;
    juce::Time lastModified;
    
    // Global loop settings
    juce::int64 loopLengthSamples{0};
    double bpm{120.0};
    int beatsPerLoop{4};
    bool latchModeEnabled{false};
    double sampleRate{44100.0};           ///< Sample rate at which loops were recorded
    
    // A/B/C sections
    int activeSection {0};
    std::array<juce::int64, NUM_SECTIONS> sectionLoopLengths {0, 0, 0};

    // Channel configurations
    std::array<ChannelConfig, 6> channels;
    
    // Metronome settings
    bool  metronomeEnabled{false};
    int   metronomeOutputLeft{0};
    int   metronomeOutputRight{1};
    int   metronomeBeatsPerBar{4};
    float metronomeGain{1.0f};
    int   fixedLengthBars{0};
    float masterGain{1.0f};
    
    // File system
    juce::File songDirectory;                ///< Directory containing song files
    
    Song() 
    {
        creationTime = juce::Time::getCurrentTime();
        lastModified = creationTime;
    }
    
    /**
     * @brief Get the song.json file path
     */
    juce::File getSongFile() const
    {
        return songDirectory.getChildFile("song.json");
    }
    
    /**
     * @brief Get loop file for a channel (section 0 = backward compat)
     */
    juce::File getLoopFile(int channelIndex) const
    {
        return songDirectory.getChildFile("channel_" + juce::String(channelIndex) + ".loop");
    }

    /**
     * @brief Get loop file for a channel in a specific section
     */
    juce::File getSectionLoopFile(int channelIndex, int section) const
    {
        if (section == 0)
            return getLoopFile(channelIndex);
        return songDirectory.getChildFile(
            "channel_" + juce::String(channelIndex) +
            "_sec_" + juce::String(section) + ".loop");
    }

    /**
     * @brief Get overdub layer file for a channel (section 0 = backward compat)
     */
    juce::File getOverdubLayerFile(int channelIndex, int layerIndex) const
    {
        return songDirectory.getChildFile(
            "channel_" + juce::String(channelIndex) +
            "_ovd_" + juce::String(layerIndex) + ".loop");
    }

    /**
     * @brief Get overdub layer file for a channel in a specific section
     */
    juce::File getSectionOverdubLayerFile(int channelIndex, int section, int layerIndex) const
    {
        if (section == 0)
            return getOverdubLayerFile(channelIndex, layerIndex);
        return songDirectory.getChildFile(
            "channel_" + juce::String(channelIndex) +
            "_sec_" + juce::String(section) +
            "_ovd_" + juce::String(layerIndex) + ".loop");
    }
    
    /**
     * @brief Get WAV file path for a channel/section
     * @param prefix Song name prefix (empty for auto-save fixed names)
     */
    juce::File getWavFile(int channelIndex, int section,
                          const juce::String& prefix = {}) const
    {
        juce::String name;
        if (prefix.isNotEmpty())
            name = prefix + "_";
        name += "channel_" + juce::String(channelIndex) +
                "_section_" + juce::String(section) + ".wav";
        return songDirectory.getChildFile(name);
    }

    /**
     * @brief Check if song directory exists and is valid
     */
    bool isValid() const
    {
        return songDirectory.exists() && songDirectory.isDirectory();
    }
    
    /**
     * @brief Create song directory structure
     */
    juce::Result createDirectory()
    {
        if (!songDirectory.createDirectory())
            return juce::Result::fail("Failed to create song directory: " + songDirectory.getFullPathName());
        
        return juce::Result::ok();
    }
};

//==============================================================================
/**
 * @brief Show data structure (collection of songs)
 */
struct Show
{
    static constexpr const char* FORMAT_VERSION = "1.0.0";
    
    juce::String formatVersion{FORMAT_VERSION};
    juce::String showName{"Untitled Show"};
    juce::String description;
    
    juce::Array<juce::File> songPaths;        ///< Paths to song directories
    
    juce::File showFile;                      ///< Path to show.json
    
    /**
     * @brief Add a song to the show
     */
    void addSong(const juce::File& songDirectory)
    {
        if (songDirectory.exists() && songDirectory.isDirectory())
        {
            songPaths.addIfNotAlreadyThere(songDirectory);
        }
    }
    
    /**
     * @brief Remove a song from the show
     */
    void removeSong(int index)
    {
        if (index >= 0 && index < songPaths.size())
            songPaths.remove(index);
    }
    
    /**
     * @brief Get number of songs in show
     */
    int getNumSongs() const
    {
        return songPaths.size();
    }
};
