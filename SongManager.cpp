#include "SongManager.h"
#include "PluginHostWrapper.h"
#include "AudioChannel.h"
#include "VSTiChannel.h"

//==============================================================================
SongManager::SongManager()
{
}

SongManager::~SongManager()
{
}

//==============================================================================
// Song Save/Load
//==============================================================================

juce::Result SongManager::saveSong(Song& song, AudioEngine& audioEngine)
{
    // Update timestamp
    song.lastModified = juce::Time::getCurrentTime();
    
    // Ensure directory exists
    auto result = song.createDirectory();
    if (result.failed())
        return result;
    
    // Read current state from audio engine
    song.loopLengthSamples = audioEngine.getLoopEngine().getLoopLength();
    song.bpm = audioEngine.getLoopEngine().getBPM();
    song.beatsPerLoop = audioEngine.getLoopEngine().getBeatsPerLoop();
    song.quantizationEnabled = audioEngine.getLoopEngine().isQuantizationEnabled();
    
    // Save each channel
    for (int i = 0; i < 6; ++i)
    {
        auto* channel = audioEngine.getChannel(i);
        if (!channel)
            continue;
        
        // Read channel configuration
        song.channels[i] = readChannelState(channel, audioEngine, i);
        
        // Save loop file if channel has content
        if (channel->hasLoop())
        {
            auto loopFile = song.getLoopFile(i);
            // Note: We need to read loop buffer from channel
            // This requires adding a getLoopBuffer() method to Channel
            // For now, mark that loop exists
            song.channels[i].hasLoopData = true;
            song.channels[i].loopFileName = loopFile.getFileName();
        }
    }
    
    // Serialize to JSON
    auto json = songToJSON(song);
    
    // Write JSON file
    auto songFile = song.getSongFile();
    auto jsonString = juce::JSON::toString(json, true);  // Pretty print
    
    if (!songFile.replaceWithText(jsonString))
    {
        return juce::Result::fail("Failed to write song.json: " + songFile.getFullPathName());
    }
    
    DBG("Song saved: " + songFile.getFullPathName());
    return juce::Result::ok();
}

juce::Result SongManager::loadSong(const juce::File& songFile, Song& song)
{
    if (!songFile.existsAsFile())
        return juce::Result::fail("Song file not found: " + songFile.getFullPathName());
    
    // Read JSON
    juce::String jsonString = songFile.loadFileAsString();
    auto json = juce::JSON::parse(jsonString);
    
    if (!json.isObject())
        return juce::Result::fail("Invalid JSON in song file");
    
    // Deserialize
    auto result = jsonToSong(json, song);
    if (result.failed())
        return result;
    
    // Set song directory
    song.songDirectory = songFile.getParentDirectory();
    
    DBG("Song loaded: " + songFile.getFullPathName());
    return juce::Result::ok();
}

juce::Result SongManager::applySongToEngine(const Song& song, AudioEngine& audioEngine)
{
    DBG("Applying song to audio engine: " + song.songName);
    
    // Stop playback first
    bool wasPlaying = audioEngine.isPlaying();
    if (wasPlaying)
        audioEngine.setPlaying(false);
    
    // Apply global settings
    audioEngine.getLoopEngine().setBPM(song.bpm);
    audioEngine.getLoopEngine().setBeatsPerLoop(song.beatsPerLoop);
    audioEngine.getLoopEngine().setQuantizationEnabled(song.quantizationEnabled);
    
    if (song.loopLengthSamples > 0)
    {
        audioEngine.getLoopEngine().setLoopLength(song.loopLengthSamples);
    }
    else
    {
        audioEngine.getLoopEngine().calculateLoopLengthFromBPM();
    }
    
    // Apply each channel configuration
    for (int i = 0; i < 6; ++i)
    {
        const auto& channelConfig = song.channels[i];
        
        // Set channel type
        audioEngine.setChannelType(i, channelConfig.type);
        
        auto* channel = audioEngine.getChannel(i);
        if (!channel)
            continue;
        
        // Apply basic settings
        channel->setGainDb(channelConfig.gainDb);
        channel->setMonitorMode(channelConfig.monitorMode);
        channel->setMuted(channelConfig.muted);
        channel->setSolo(channelConfig.solo);
        
        // Apply routing
        channel->setRouting(channelConfig.routing);
        
        // Load plugins
        // TODO: This should be async and handled carefully
        // For now, just log what would be loaded
        
        if (channelConfig.type == ChannelType::VSTi)
        {
            if (!channelConfig.vstInstrument.identifier.isEmpty())
            {
                DBG("  Channel " + juce::String(i) + " VSTi: " + 
                    channelConfig.vstInstrument.name);
            }
        }
        
        for (int slot = 0; slot < 3; ++slot)
        {
            const auto& pluginData = channelConfig.fxPlugins[slot];
            if (!pluginData.identifier.isEmpty())
            {
                DBG("  Channel " + juce::String(i) + " FX[" + juce::String(slot) + "]: " + 
                    pluginData.name);
            }
        }
        
        // Load loop file if exists
        if (channelConfig.hasLoopData)
        {
            auto loopFile = song.songDirectory.getChildFile(channelConfig.loopFileName);
            if (loopFile.existsAsFile())
            {
                DBG("  Channel " + juce::String(i) + " loop: " + loopFile.getFileName());
                // TODO: Load loop file into channel
            }
            else
            {
                DBG("  WARNING: Loop file missing: " + loopFile.getFullPathName());
            }
        }
    }
    
    // Restore playback state
    if (wasPlaying)
        audioEngine.setPlaying(true);
    
    return juce::Result::ok();
}

//==============================================================================
// Loop File I/O
//==============================================================================

juce::Result SongManager::saveLoopFile(const juce::File& file,
                                      const juce::AudioBuffer<float>& buffer,
                                      juce::int64 numSamples)
{
    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return juce::Result::fail("Invalid buffer");
    
    // Create output stream
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (!stream)
        return juce::Result::fail("Cannot create file: " + file.getFullPathName());
    
    // Write header
    stream->writeInt(0x4C4F4F50);  // "LOOP" magic number
    stream->writeInt(1);            // Version
    stream->writeInt64(numSamples); // Number of samples
    stream->writeInt(2);            // Channels (always stereo)
    stream->writeFloat(44100.0f);   // Sample rate (informational)
    
    // Write audio data (interleaved stereo, 32-bit float)
    const int samplesToWrite = static_cast<int>(juce::jmin(numSamples, 
                                                            static_cast<juce::int64>(buffer.getNumSamples())));
    
    for (int i = 0; i < samplesToWrite; ++i)
    {
        stream->writeFloat(buffer.getSample(0, i));  // Left
        stream->writeFloat(buffer.getSample(1, i));  // Right
    }
    
    stream->flush();
    
    DBG("Loop file saved: " + file.getFullPathName() + 
        " (" + juce::String(numSamples) + " samples)");
    
    return juce::Result::ok();
}

juce::int64 SongManager::loadLoopFile(const juce::File& file,
                                juce::AudioBuffer<float>& buffer,
                                juce::int64 maxSamples)
{
    if (!file.existsAsFile())
    {
        DBG("Loop file not found: " + file.getFullPathName());
        return -1;
    }
    
    // Create input stream
    std::unique_ptr<juce::FileInputStream> stream(file.createInputStream());
    if (!stream)
    {
        DBG("Cannot open file: " + file.getFullPathName());
        return -1;
    }
    
    // Read header
    int magic = stream->readInt();
    if (magic != 0x4C4F4F50)  // "LOOP"
    {
        DBG("Invalid loop file format");
        return -1;
    }
    
    int version = stream->readInt();
    juce::int64 numSamples = stream->readInt64();
    int numChannels = stream->readInt();
    float sampleRate = stream->readFloat();
    
    DBG("Loading loop file: " + file.getFileName());
    DBG("  Samples: " + juce::String(numSamples));
    DBG("  Channels: " + juce::String(numChannels));
    DBG("  Sample rate: " + juce::String(sampleRate));
    
    if (numChannels != 2)
    {
        DBG("Only stereo loops supported");
        return -1;
    }
    
    // Clamp to buffer size
    const juce::int64 samplesToRead = juce::jmin(numSamples, maxSamples);
    
    // Ensure buffer is large enough
    if (buffer.getNumSamples() < samplesToRead)
    {
        DBG("Buffer too small for loop data");
        return -1;
    }
    
    // Read audio data (interleaved stereo)
    for (juce::int64 i = 0; i < samplesToRead; ++i)
    {
        float left = stream->readFloat();
        float right = stream->readFloat();
        
        buffer.setSample(0, static_cast<int>(i), left);
        buffer.setSample(1, static_cast<int>(i), right);
    }
    
    DBG("Loop file loaded: " + juce::String(samplesToRead) + " samples");
    
    return samplesToRead;
}

//==============================================================================
// Utilities
//==============================================================================

juce::File SongManager::createSongDirectory(const juce::File& parentDirectory,
                                            const juce::String& songName)
{
    // Sanitize song name for file system
    juce::String safeName = songName.trim();
    safeName = safeName.replaceCharacters("/\\:*?\"<>|", "_________");
    
    if (safeName.isEmpty())
        safeName = "Untitled";
    
    auto songDir = parentDirectory.getChildFile(safeName);
    
    // Make unique if exists
    int suffix = 1;
    auto originalDir = songDir;
    while (songDir.exists())
    {
        songDir = originalDir.getSiblingFile(safeName + " " + juce::String(suffix));
        ++suffix;
    }
    
    if (songDir.createDirectory())
        return songDir;
    
    return juce::File();
}

bool SongManager::isValidSongDirectory(const juce::File& songDirectory)
{
    if (!songDirectory.exists() || !songDirectory.isDirectory())
        return false;
    
    // Check for song.json
    auto songFile = songDirectory.getChildFile("song.json");
    return songFile.existsAsFile();
}

//==============================================================================
// JSON Serialization
//==============================================================================

juce::var SongManager::songToJSON(const Song& song)
{
    auto* obj = new juce::DynamicObject();
    
    // Metadata
    obj->setProperty("format_version", song.formatVersion);
    obj->setProperty("song_name", song.songName);
    obj->setProperty("description", song.description);
    obj->setProperty("creation_time", song.creationTime.toISO8601(true));
    obj->setProperty("last_modified", song.lastModified.toISO8601(true));
    
    // Global settings
    obj->setProperty("loop_length_samples", song.loopLengthSamples);
    obj->setProperty("bpm", song.bpm);
    obj->setProperty("beats_per_loop", song.beatsPerLoop);
    obj->setProperty("quantization_enabled", song.quantizationEnabled);
    
    // Metronome
    obj->setProperty("metronome_enabled", song.metronomeEnabled);
    obj->setProperty("metronome_output_left", song.metronomeOutputLeft);
    obj->setProperty("metronome_output_right", song.metronomeOutputRight);
    
    // Channels
    juce::Array<juce::var> channelsArray;
    for (const auto& channel : song.channels)
    {
        channelsArray.add(channelToJSON(channel));
    }
    obj->setProperty("channels", channelsArray);
    
    return juce::var(obj);
}

juce::Result SongManager::jsonToSong(const juce::var& json, Song& song)
{
    if (!json.isObject())
        return juce::Result::fail("JSON is not an object");
    
    auto* obj = json.getDynamicObject();
    
    // Metadata
    song.formatVersion = obj->getProperty("format_version").toString();
    song.songName = obj->getProperty("song_name").toString();
    song.description = obj->getProperty("description").toString();
    song.creationTime = juce::Time::fromISO8601(obj->getProperty("creation_time").toString());
    song.lastModified = juce::Time::fromISO8601(obj->getProperty("last_modified").toString());
    
    // Global settings
    song.loopLengthSamples = obj->getProperty("loop_length_samples");
    song.bpm = obj->getProperty("bpm");
    song.beatsPerLoop = obj->getProperty("beats_per_loop");
    song.quantizationEnabled = obj->getProperty("quantization_enabled");
    
    // Metronome
    song.metronomeEnabled = obj->getProperty("metronome_enabled");
    song.metronomeOutputLeft = obj->getProperty("metronome_output_left");
    song.metronomeOutputRight = obj->getProperty("metronome_output_right");
    
    // Channels
    auto* channelsArray = obj->getProperty("channels").getArray();
    if (channelsArray && channelsArray->size() >= 6)
    {
        for (int i = 0; i < 6; ++i)
        {
            auto result = jsonToChannel(channelsArray->getReference(i), song.channels[i]);
            if (result.failed())
                DBG("Warning: Failed to load channel " + juce::String(i) + ": " + result.getErrorMessage());
        }
    }
    
    return juce::Result::ok();
}

juce::var SongManager::channelToJSON(const ChannelConfig& channel)
{
    auto* obj = new juce::DynamicObject();
    
    // Type and basic settings
    obj->setProperty("type", channel.type == ChannelType::Audio ? "Audio" : "VSTi");
    obj->setProperty("gain_db", channel.gainDb);
    obj->setProperty("monitor_mode", static_cast<int>(channel.monitorMode));
    obj->setProperty("muted", channel.muted);
    obj->setProperty("solo", channel.solo);
    
    // Routing
    obj->setProperty("routing", routingToJSON(channel.routing));
    
    // VSTi instrument
    if (channel.type == ChannelType::VSTi && !channel.vstInstrument.identifier.isEmpty())
    {
        obj->setProperty("vsti", pluginToJSON(channel.vstInstrument));
    }
    
    // FX plugins
    juce::Array<juce::var> fxArray;
    for (const auto& plugin : channel.fxPlugins)
    {
        if (!plugin.identifier.isEmpty())
        {
            fxArray.add(pluginToJSON(plugin));
        }
    }
    if (fxArray.size() > 0)
    {
        obj->setProperty("fx_plugins", fxArray);
    }
    
    // Loop data
    obj->setProperty("has_loop_data", channel.hasLoopData);
    obj->setProperty("loop_file_name", channel.loopFileName);
    
    return juce::var(obj);
}

juce::Result SongManager::jsonToChannel(const juce::var& json, ChannelConfig& channel)
{
    if (!json.isObject())
        return juce::Result::fail("Channel JSON is not an object");
    
    auto* obj = json.getDynamicObject();
    
    // Type
    juce::String typeStr = obj->getProperty("type").toString();
    channel.type = (typeStr == "VSTi") ? ChannelType::VSTi : ChannelType::Audio;
    
    // Basic settings
    channel.gainDb = obj->getProperty("gain_db");
    channel.monitorMode = static_cast<MonitorMode>((int)obj->getProperty("monitor_mode"));
    channel.muted = obj->getProperty("muted");
    channel.solo = obj->getProperty("solo");
    
    // Routing
    jsonToRouting(obj->getProperty("routing"), channel.routing);
    
    // VSTi
    if (obj->hasProperty("vsti"))
    {
        jsonToPlugin(obj->getProperty("vsti"), channel.vstInstrument);
    }
    
    // FX plugins
    if (obj->hasProperty("fx_plugins"))
    {
        auto* fxArray = obj->getProperty("fx_plugins").getArray();
        if (fxArray)
        {
            for (int i = 0; i < juce::jmin(3, fxArray->size()); ++i)
            {
                jsonToPlugin(fxArray->getReference(i), channel.fxPlugins[i]);
            }
        }
    }
    
    // Loop data
    channel.hasLoopData = obj->getProperty("has_loop_data");
    channel.loopFileName = obj->getProperty("loop_file_name").toString();
    
    return juce::Result::ok();
}

juce::var SongManager::pluginToJSON(const PluginData& plugin)
{
    auto* obj = new juce::DynamicObject();
    
    obj->setProperty("slot_index", plugin.slotIndex);
    obj->setProperty("identifier", plugin.identifier);
    obj->setProperty("name", plugin.name);
    obj->setProperty("manufacturer", plugin.manufacturer);
    obj->setProperty("state_base64", plugin.stateBase64);
    obj->setProperty("bypassed", plugin.bypassed);
    
    return juce::var(obj);
}

juce::Result SongManager::jsonToPlugin(const juce::var& json, PluginData& plugin)
{
    if (!json.isObject())
        return juce::Result::fail("Plugin JSON is not an object");
    
    auto* obj = json.getDynamicObject();
    
    plugin.slotIndex = obj->getProperty("slot_index");
    plugin.identifier = obj->getProperty("identifier").toString();
    plugin.name = obj->getProperty("name").toString();
    plugin.manufacturer = obj->getProperty("manufacturer").toString();
    plugin.stateBase64 = obj->getProperty("state_base64").toString();
    plugin.bypassed = obj->getProperty("bypassed");
    
    return juce::Result::ok();
}

juce::var SongManager::routingToJSON(const RoutingConfig& routing)
{
    auto* obj = new juce::DynamicObject();
    
    obj->setProperty("input_channel_left", routing.inputChannelLeft);
    obj->setProperty("input_channel_right", routing.inputChannelRight);
    obj->setProperty("output_channel_left", routing.outputChannelLeft);
    obj->setProperty("output_channel_right", routing.outputChannelRight);
    obj->setProperty("midi_channel_filter", routing.midiChannelFilter);
    
    return juce::var(obj);
}

juce::Result SongManager::jsonToRouting(const juce::var& json, RoutingConfig& routing)
{
    if (!json.isObject())
        return juce::Result::fail("Routing JSON is not an object");
    
    auto* obj = json.getDynamicObject();
    
    routing.inputChannelLeft = obj->getProperty("input_channel_left");
    routing.inputChannelRight = obj->getProperty("input_channel_right");
    routing.outputChannelLeft = obj->getProperty("output_channel_left");
    routing.outputChannelRight = obj->getProperty("output_channel_right");
    routing.midiChannelFilter = obj->getProperty("midi_channel_filter");
    
    return juce::Result::ok();
}

//==============================================================================
// Helper Methods
//==============================================================================

ChannelConfig SongManager::readChannelState(Channel* channel,
                                            AudioEngine& audioEngine,
                                            int channelIndex)
{
    ChannelConfig config;
    
    if (!channel)
        return config;
    
    // Type and basic settings
    config.type = channel->getType();
    config.gainDb = channel->getGainDb();
    config.monitorMode = channel->getMonitorMode();
    config.muted = channel->isMuted();
    config.solo = channel->isSolo();
    
    // Routing
    config.routing = channel->getRouting();
    
    // Plugins - TODO: Read actual plugin states
    // This requires accessing the plugin chain from Channel
    // For now, we'll mark them as empty
    
    // Loop data
    config.hasLoopData = channel->hasLoop();
    if (config.hasLoopData)
    {
        config.loopFileName = "channel_" + juce::String(channelIndex) + ".loop";
    }
    
    return config;
}
