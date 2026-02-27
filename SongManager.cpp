#include "SongManager.h"
#include "PluginHostWrapper.h"
#include "VSTiChannel.h"

//==============================================================================
SongManager::SongManager()  {}
SongManager::~SongManager() {}

//==============================================================================
// Save
//==============================================================================

juce::Result SongManager::saveSong(Song& song, AudioEngine& audioEngine)
{
    song.lastModified = juce::Time::getCurrentTime();

    auto result = song.createDirectory();
    if (result.failed()) return result;

    // Global state
    song.loopLengthSamples    = audioEngine.getLoopEngine().getLoopLength();
    song.bpm                  = audioEngine.getLoopEngine().getBPM();
    song.beatsPerLoop         = audioEngine.getLoopEngine().getBeatsPerLoop();
    song.latchModeEnabled     = audioEngine.isLatchMode();
    song.metronomeEnabled     = audioEngine.getMetronome().getEnabled();
    song.metronomeOutputLeft  = audioEngine.getMetronome().getOutputLeft();
    song.metronomeOutputRight = audioEngine.getMetronome().getOutputRight();

    const juce::int64 loopLen = song.loopLengthSamples;

    for (int i = 0; i < 6; ++i)
    {
        auto* channel = audioEngine.getChannel(i);
        if (!channel) continue;

        // Reads basic params + plugin states
        song.channels[i] = readChannelState(channel, audioEngine, i);

        // Save loop buffer if content exists and loop length is known
        if (channel->hasLoop() && loopLen > 0)
        {
            auto loopFile  = song.getLoopFile(i);
            auto saveResult = saveLoopFile(loopFile, channel->getLoopBuffer(), loopLen);

            if (saveResult.failed())
            {
                DBG("WARNING: loop save failed for ch " + juce::String(i) +
                    ": " + saveResult.getErrorMessage());
                // non-fatal — continue; clear the flag so the JSON doesn't claim a file exists
                song.channels[i].hasLoopData  = false;
                song.channels[i].loopFileName = {};
            }
            else
            {
                song.channels[i].hasLoopData  = true;
                song.channels[i].loopFileName = loopFile.getFileName();
            }
        }
        else
        {
            // No loop or loop length unknown — ensure JSON doesn't reference a missing file
            song.channels[i].hasLoopData  = false;
            song.channels[i].loopFileName = {};
        }
    }

    auto json       = songToJSON(song);
    auto songFile   = song.getSongFile();
    auto jsonString = juce::JSON::toString(json, true);

    if (!songFile.replaceWithText(jsonString))
        return juce::Result::fail("Failed to write song.json: " + songFile.getFullPathName());

    DBG("Song saved: " + songFile.getFullPathName());
    return juce::Result::ok();
}

//==============================================================================
// Load
//==============================================================================

juce::Result SongManager::loadSong(const juce::File& songFile, Song& song)
{
    if (!songFile.existsAsFile())
        return juce::Result::fail("Song file not found: " + songFile.getFullPathName());

    auto json = juce::JSON::parse(songFile.loadFileAsString());
    if (!json.isObject())
        return juce::Result::fail("Invalid JSON in song file");

    auto result = jsonToSong(json, song);
    if (result.failed()) return result;

    song.songDirectory = songFile.getParentDirectory();

    DBG("Song loaded: " + songFile.getFullPathName());
    return juce::Result::ok();
}

//==============================================================================
// Apply to Engine
//==============================================================================

juce::Result SongManager::applySongToEngine(const Song& song, AudioEngine& audioEngine)
{
    DBG("Applying song to engine: " + song.songName);

    bool wasPlaying = audioEngine.isPlaying();
    if (wasPlaying) audioEngine.setPlaying(false);

    // Global loop settings
    audioEngine.getLoopEngine().setBPM(song.bpm);
    audioEngine.getLoopEngine().setBeatsPerLoop(song.beatsPerLoop);
    audioEngine.setLatchMode(song.latchModeEnabled);

    // Restore the saved loop length exactly.  In metronome mode the loop length
    // is now established by the first recording (bar-rounded), not from BPM,
    // so we never call calculateLoopLengthFromBPM() here.
    audioEngine.getLoopEngine().setLoopLength(song.loopLengthSamples);

    // Metronome
    audioEngine.getMetronome().setEnabled(song.metronomeEnabled);
    audioEngine.getMetronome().setBPM(song.bpm);
    audioEngine.getMetronome().setOutputChannels(song.metronomeOutputLeft,
                                                 song.metronomeOutputRight);

    for (int i = 0; i < 6; ++i)
    {
        const auto& cfg = song.channels[i];

        // setChannelType() creates + prepares channel if audio is initialised
        audioEngine.setChannelType(i, cfg.type);

        auto* channel = audioEngine.getChannel(i);
        if (!channel) continue;

        audioEngine.setChannelName(i, cfg.channelName);
        channel->setGainDb    (cfg.gainDb);
        channel->setMonitorMode(cfg.monitorMode);
        channel->setMuted      (cfg.muted);
        channel->setSolo       (cfg.solo);
        channel->setRouting    (cfg.routing);

        // --- Loop file ---
        if (cfg.hasLoopData && !cfg.loopFileName.isEmpty())
        {
            auto loopFile = song.songDirectory.getChildFile(cfg.loopFileName);

            if (loopFile.existsAsFile())
            {
                // BUG 2 FIX: check channel was prepared before attempting load
                if (channel->getLoopBufferSize() == 0)
                {
                    DBG("WARNING: ch " + juce::String(i) +
                        " not yet prepared — loop file not loaded. "
                        "Call applySongToEngine() after initialiseAudio().");
                }
                else
                {
                    const juce::int64 maxSamples = channel->getLoopBufferSize();
                    juce::AudioBuffer<float> tmp(2, static_cast<int>(maxSamples));
                    tmp.clear();

                    const juce::int64 loaded = loadLoopFile(loopFile, tmp, maxSamples);
                    if (loaded > 0)
                    {
                        if (!channel->loadLoopData(tmp, loaded))
                            DBG("WARNING: ch " + juce::String(i) + " loadLoopData failed");
                        else
                            DBG("  ch " + juce::String(i) + ": " +
                                juce::String(loaded) + " samples loaded");
                    }
                    else
                    {
                        DBG("WARNING: loop file could not be read: " + loopFile.getFullPathName());
                    }
                }
            }
            else
            {
                DBG("WARNING: loop file missing: " + loopFile.getFullPathName());
            }
        }

        // --- Plugins (async) — state is passed into callback ---
        if (cfg.type == ChannelType::VSTi && !cfg.vstInstrument.identifier.isEmpty())
        {
            audioEngine.loadPluginAsync(i, -1,
                                        cfg.vstInstrument.identifier,
                                        cfg.vstInstrument.stateBase64);
        }

        for (int slot = 0; slot < 3; ++slot)
        {
            const auto& pd = cfg.fxPlugins[slot];
            if (!pd.identifier.isEmpty())
                audioEngine.loadPluginAsync(i, slot, pd.identifier, pd.stateBase64, pd.bypassed);
        }
    }

    if (wasPlaying) audioEngine.setPlaying(true);

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
    if (buffer.getNumSamples() < static_cast<int>(numSamples))
        return juce::Result::fail("Buffer smaller than numSamples");

    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (!stream)
        return juce::Result::fail("Cannot create: " + file.getFullPathName());

    stream->writeInt  (0x4C4F4F50);  // "LOOP" magic
    stream->writeInt  (1);            // version
    stream->writeInt64(numSamples);
    stream->writeInt  (2);            // always stereo
    stream->writeFloat(44100.0f);     // informational

    const float* L = buffer.getReadPointer(0);
    const float* R = buffer.getReadPointer(1);
    for (juce::int64 i = 0; i < numSamples; ++i)
    {
        stream->writeFloat(L[i]);
        stream->writeFloat(R[i]);
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

    std::unique_ptr<juce::FileInputStream> stream(file.createInputStream());
    if (!stream) return -1;

    if (stream->readInt() != 0x4C4F4F50)
    {
        DBG("Invalid loop file magic: " + file.getFullPathName());
        return -1;
    }
    /*version*/      stream->readInt();
    const juce::int64 numSamples  = stream->readInt64();
    const int         numChannels = stream->readInt();
    /*sampleRate*/   stream->readFloat();

    if (numChannels != 2 || numSamples <= 0) return -1;

    const juce::int64 samplesToRead = juce::jmin(numSamples, maxSamples);
    if (buffer.getNumSamples() < static_cast<int>(samplesToRead) ||
        buffer.getNumChannels() < 2)
    {
        DBG("Target buffer too small");
        return -1;
    }

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);
    for (juce::int64 i = 0; i < samplesToRead; ++i)
    {
        L[i] = stream->readFloat();
        R[i] = stream->readFloat();
    }

    DBG("Loop loaded: " + file.getFileName() +
        " (" + juce::String(samplesToRead) + "/" + juce::String(numSamples) + " samples)");
    return samplesToRead;
}

//==============================================================================
// readChannelState — liest auch Plugin-States
//==============================================================================

ChannelConfig SongManager::readChannelState(Channel* channel,
                                            AudioEngine& audioEngine,
                                            int channelIndex)
{
    ChannelConfig cfg;
    if (!channel) return cfg;

    cfg.type        = channel->getType();
    cfg.channelName = audioEngine.getChannelName(channelIndex);
    cfg.gainDb      = channel->getGainDb();
    cfg.monitorMode = channel->getMonitorMode();
    cfg.muted       = channel->isMuted();
    cfg.solo        = channel->isSolo();
    cfg.routing     = channel->getRouting();
    cfg.hasLoopData = channel->hasLoop();

    if (cfg.hasLoopData)
        cfg.loopFileName = "channel_" + juce::String(channelIndex) + ".loop";

    // --- VSTi instrument state ---
    if (cfg.type == ChannelType::VSTi)
    {
        auto* vstiChannel = static_cast<VSTiChannel*>(channel);
        auto* vsti        = vstiChannel->getVSTi();

        if (vsti)
        {
            cfg.vstInstrument.identifier = vsti->getPluginDescription().createIdentifierString();
            cfg.vstInstrument.name       = vsti->getName();
            cfg.vstInstrument.manufacturer = vsti->getPluginDescription().manufacturerName;
            cfg.vstInstrument.slotIndex  = -1;
            cfg.vstInstrument.bypassed   = false;

            // Save plugin state as Base64
            const auto block = audioEngine.getPluginHost().savePluginState(vsti);
            if (block.getSize() > 0)
                cfg.vstInstrument.stateBase64 =
                    PluginHostWrapper::memoryBlockToBase64(block);
        }
    }

    // --- FX slot states ---
    for (int slot = 0; slot < 3; ++slot)
    {
        auto* plugin = channel->getPlugin(slot);
        if (!plugin) continue;

        auto& pd = cfg.fxPlugins[slot];
        pd.slotIndex    = slot;
        pd.identifier   = plugin->getPluginDescription().createIdentifierString();
        pd.name         = plugin->getName();
        pd.manufacturer = plugin->getPluginDescription().manufacturerName;
        pd.bypassed     = channel->isPluginBypassed(slot);

        // Save plugin state as Base64
        const auto block = audioEngine.getPluginHost().savePluginState(plugin);
        if (block.getSize() > 0)
            pd.stateBase64 = PluginHostWrapper::memoryBlockToBase64(block);
    }

    return cfg;
}

//==============================================================================
// JSON Serialization
//==============================================================================

juce::var SongManager::songToJSON(const Song& song)
{
    auto* obj = new juce::DynamicObject();

    obj->setProperty("format_version",        song.formatVersion);
    obj->setProperty("song_name",             song.songName);
    obj->setProperty("description",           song.description);
    obj->setProperty("creation_time",         song.creationTime.toISO8601(true));
    obj->setProperty("last_modified",         song.lastModified .toISO8601(true));
    obj->setProperty("loop_length_samples",   song.loopLengthSamples);
    obj->setProperty("bpm",                   song.bpm);
    obj->setProperty("beats_per_loop",        song.beatsPerLoop);
    obj->setProperty("latch_mode_enabled",    song.latchModeEnabled);
    obj->setProperty("metronome_enabled",     song.metronomeEnabled);
    obj->setProperty("metronome_output_left", song.metronomeOutputLeft);
    obj->setProperty("metronome_output_right",song.metronomeOutputRight);

    juce::Array<juce::var> chArr;
    for (const auto& ch : song.channels)
        chArr.add(channelToJSON(ch));
    obj->setProperty("channels", chArr);

    return juce::var(obj);
}

juce::Result SongManager::jsonToSong(const juce::var& json, Song& song)
{
    if (!json.isObject()) return juce::Result::fail("JSON not an object");
    auto* obj = json.getDynamicObject();

    song.formatVersion     = obj->getProperty("format_version").toString();
    song.songName          = obj->getProperty("song_name")     .toString();
    song.description       = obj->getProperty("description")   .toString();
    song.creationTime      = juce::Time::fromISO8601(obj->getProperty("creation_time").toString());
    song.lastModified      = juce::Time::fromISO8601(obj->getProperty("last_modified") .toString());
    song.loopLengthSamples = obj->getProperty("loop_length_samples");
    song.bpm               = obj->getProperty("bpm");
    song.beatsPerLoop      = obj->getProperty("beats_per_loop");
    song.latchModeEnabled     = obj->getProperty("latch_mode_enabled");
    song.metronomeEnabled     = obj->getProperty("metronome_enabled");
    song.metronomeOutputLeft  = obj->getProperty("metronome_output_left");
    song.metronomeOutputRight = obj->getProperty("metronome_output_right");

    auto* chArr = obj->getProperty("channels").getArray();
    if (chArr && chArr->size() >= 6)
        for (int i = 0; i < 6; ++i)
            jsonToChannel(chArr->getReference(i), song.channels[i]);

    return juce::Result::ok();
}

juce::var SongManager::channelToJSON(const ChannelConfig& ch)
{
    auto* obj = new juce::DynamicObject();

    obj->setProperty("type",         ch.type == ChannelType::Audio ? "Audio" : "VSTi");
    obj->setProperty("channel_name", ch.channelName);
    obj->setProperty("gain_db",      ch.gainDb);
    obj->setProperty("monitor_mode", static_cast<int>(ch.monitorMode));
    obj->setProperty("muted",        ch.muted);
    obj->setProperty("solo",         ch.solo);
    obj->setProperty("routing",      routingToJSON(ch.routing));
    obj->setProperty("has_loop_data",ch.hasLoopData);
    obj->setProperty("loop_file_name",ch.loopFileName);

    if (ch.type == ChannelType::VSTi && !ch.vstInstrument.identifier.isEmpty())
        obj->setProperty("vsti", pluginToJSON(ch.vstInstrument));

    juce::Array<juce::var> fxArr;
    for (const auto& pd : ch.fxPlugins)
        if (!pd.identifier.isEmpty())
            fxArr.add(pluginToJSON(pd));
    if (fxArr.size() > 0)
        obj->setProperty("fx_plugins", fxArr);

    return juce::var(obj);
}

juce::Result SongManager::jsonToChannel(const juce::var& json, ChannelConfig& ch)
{
    if (!json.isObject()) return juce::Result::fail("Channel JSON not an object");
    auto* obj = json.getDynamicObject();

    ch.type        = (obj->getProperty("type").toString() == "VSTi")
                     ? ChannelType::VSTi : ChannelType::Audio;
    ch.channelName = obj->getProperty("channel_name").toString();
    ch.gainDb      = obj->getProperty("gain_db");
    ch.monitorMode = static_cast<MonitorMode>((int)obj->getProperty("monitor_mode"));
    ch.muted       = obj->getProperty("muted");
    ch.solo        = obj->getProperty("solo");
    jsonToRouting(obj->getProperty("routing"), ch.routing);
    ch.hasLoopData  = obj->getProperty("has_loop_data");
    ch.loopFileName = obj->getProperty("loop_file_name").toString();

    if (obj->hasProperty("vsti"))
        jsonToPlugin(obj->getProperty("vsti"), ch.vstInstrument);

    if (obj->hasProperty("fx_plugins"))
    {
        auto* fxArr = obj->getProperty("fx_plugins").getArray();
        if (fxArr)
            for (const auto& pluginVar : *fxArr)
            {
                PluginData pd;
                jsonToPlugin(pluginVar, pd);
                if (pd.slotIndex >= 0 && pd.slotIndex < 3)
                    ch.fxPlugins[pd.slotIndex] = pd;
            }
    }

    return juce::Result::ok();
}

juce::var SongManager::pluginToJSON(const PluginData& p)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("slot_index",   p.slotIndex);
    obj->setProperty("identifier",   p.identifier);
    obj->setProperty("name",         p.name);
    obj->setProperty("manufacturer", p.manufacturer);
    obj->setProperty("state_base64", p.stateBase64);
    obj->setProperty("bypassed",     p.bypassed);
    return juce::var(obj);
}

juce::Result SongManager::jsonToPlugin(const juce::var& json, PluginData& p)
{
    if (!json.isObject()) return juce::Result::fail("Plugin JSON not an object");
    auto* obj = json.getDynamicObject();
    p.slotIndex    = obj->getProperty("slot_index");
    p.identifier   = obj->getProperty("identifier")  .toString();
    p.name         = obj->getProperty("name")         .toString();
    p.manufacturer = obj->getProperty("manufacturer") .toString();
    p.stateBase64  = obj->getProperty("state_base64") .toString();
    p.bypassed     = obj->getProperty("bypassed");
    return juce::Result::ok();
}

juce::var SongManager::routingToJSON(const RoutingConfig& r)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("input_left",   r.inputChannelLeft);
    obj->setProperty("input_right",  r.inputChannelRight);
    obj->setProperty("output_left",  r.outputChannelLeft);
    obj->setProperty("output_right", r.outputChannelRight);
    obj->setProperty("midi_filter",  r.midiChannelFilter);
    return juce::var(obj);
}

juce::Result SongManager::jsonToRouting(const juce::var& json, RoutingConfig& r)
{
    if (!json.isObject()) return juce::Result::fail("Routing JSON not an object");
    auto* obj = json.getDynamicObject();
    r.inputChannelLeft   = obj->getProperty("input_left");
    r.inputChannelRight  = obj->getProperty("input_right");
    r.outputChannelLeft  = obj->getProperty("output_left");
    r.outputChannelRight = obj->getProperty("output_right");
    r.midiChannelFilter  = obj->getProperty("midi_filter");
    return juce::Result::ok();
}

//==============================================================================
// Utilities
//==============================================================================

juce::File SongManager::createSongDirectory(const juce::File& parentDirectory,
                                            const juce::String& songName)
{
    juce::String safeName = songName.trim()
                             .replaceCharacters("/\\:*?\"<>|", "_________");
    if (safeName.isEmpty()) safeName = "Untitled";

    auto songDir     = parentDirectory.getChildFile(safeName);
    auto originalDir = songDir;
    int  suffix      = 1;
    while (songDir.exists())
    {
        songDir = originalDir.getSiblingFile(safeName + " " + juce::String(suffix++));
    }
    return songDir.createDirectory() ? songDir : juce::File();
}

bool SongManager::isValidSongDirectory(const juce::File& dir)
{
    return dir.isDirectory() && dir.getChildFile("song.json").existsAsFile();
}
