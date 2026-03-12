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
    song.sampleRate           = audioEngine.getSampleRate();
    song.metronomeEnabled      = audioEngine.getMetronome().getEnabled();
    song.metronomeOutputLeft   = audioEngine.getMetronome().getOutputLeft();
    song.metronomeOutputRight  = audioEngine.getMetronome().getOutputRight();
    song.metronomeBeatsPerBar  = audioEngine.getMetronome().getBeatsPerBar();
    song.metronomeGain         = audioEngine.getMetronomeGain();
    song.fixedLengthBars       = audioEngine.getFixedLengthBars();
    song.masterGain            = audioEngine.getMasterGain();

    // Section state
    song.activeSection = audioEngine.getActiveSection();
    for (int s = 0; s < NUM_SECTIONS; ++s)
        song.sectionLoopLengths[s] = audioEngine.getSectionLoopLength(s);

    // Determine filename prefix: auto-save uses fixed names, manual saves use song name
    juce::String wavPrefix;
    if (song.songName != "currentSong")
    {
        wavPrefix = song.songName.trim()
                        .replaceCharacters("/\\:*?\"<>|", "_________");
    }

    juce::StringArray writtenFiles;

    for (int i = 0; i < 6; ++i)
    {
        auto* channel = audioEngine.getChannel(i);
        if (!channel) continue;

        song.channels[i] = readChannelState(channel, audioEngine, i);

        // Save loop data for all sections as mixed-down WAV
        for (int s = 0; s < NUM_SECTIONS; ++s)
        {
            auto& sd = song.channels[i].sectionData[s];
            const juce::int64 secLoopLen = song.sectionLoopLengths[s];

            if (channel->sectionHasContent(s) && secLoopLen > 0)
            {
                auto wavFile = song.getWavFile(i, s, wavPrefix);
                auto mixed = mixDownChannel(channel->getSectionLoopBuffer(s),
                                            channel->getSectionOverdubLayers(s),
                                            secLoopLen);
                auto saveResult = saveWavFile(wavFile, mixed, secLoopLen, song.sampleRate);

                if (saveResult.failed())
                {
                    DBG("WARNING: WAV save failed for ch " + juce::String(i) +
                        " sec " + juce::String(s) + ": " + saveResult.getErrorMessage());
                    sd.hasLoopData = false;
                    sd.loopFileName = {};
                }
                else
                {
                    sd.hasLoopData = true;
                    sd.loopFileName = wavFile.getFileName();
                    writtenFiles.add(wavFile.getFileName());
                }

                sd.overdubLayerCount = 0; // overdubs baked into WAV
            }
            else
            {
                sd.hasLoopData = false;
                sd.loopFileName = {};
                sd.overdubLayerCount = 0;
            }
        }
    }

    // Copy master recordings from currentSong directory into the song directory
    {
        auto currentSongDir = getCurrentSongDirectory();
        if (currentSongDir != song.songDirectory)
        {
            auto masterFiles = currentSongDir.findChildFiles(
                juce::File::findFiles, false, "master_*.wav");
            for (auto& f : masterFiles)
            {
                auto dest = song.songDirectory.getChildFile(f.getFileName());
                if (!dest.existsAsFile())
                    f.copyFileTo(dest);
                writtenFiles.add(f.getFileName());
            }
        }
    }

    // Preserve existing master recordings in the song directory
    {
        auto masterFiles = song.songDirectory.findChildFiles(
            juce::File::findFiles, false, "master_*.wav");
        for (auto& f : masterFiles)
            writtenFiles.add(f.getFileName());
    }

    // Clean up stale .wav and .loop files no longer referenced
    {
        auto staleFiles = song.songDirectory.findChildFiles(
            juce::File::findFiles, false, "*.wav;*.loop");
        for (auto& f : staleFiles)
            if (!writtenFiles.contains(f.getFileName()))
                f.deleteFile();
    }

    auto json       = songToJSON(song);
    auto songFile   = song.getSongFile();
    auto jsonString = juce::JSON::toString(json, true);

    // Write JSON to temp file first, then rename for crash-safety
    auto tmpJsonFile = songFile.getSiblingFile(songFile.getFileName() + ".tmp");
    if (!tmpJsonFile.replaceWithText(jsonString))
        return juce::Result::fail("Failed to write temp song.json: " + tmpJsonFile.getFullPathName());
    if (!tmpJsonFile.moveFileTo(songFile))
        return juce::Result::fail("Failed to rename temp song.json: " + tmpJsonFile.getFullPathName());

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

    // Warn if sample rate differs from when the song was recorded
    const double currentSR = audioEngine.getSampleRate();
    if (song.sampleRate > 0.0 && currentSR > 0.0 &&
        std::abs(song.sampleRate - currentSR) > 1.0)
    {
        DBG("WARNING: Song was recorded at " + juce::String(song.sampleRate) +
            " Hz but current device is " + juce::String(currentSR) +
            " Hz — loops may play at wrong speed/pitch!");
    }

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
    audioEngine.setBeatsPerBar(song.metronomeBeatsPerBar);
    audioEngine.setMetronomeGain(song.metronomeGain);
    audioEngine.setFixedLengthBars(song.fixedLengthBars);
    audioEngine.setMasterGain(song.masterGain);

    for (int i = 0; i < 6; ++i)
    {
        const auto& cfg = song.channels[i];

        audioEngine.setChannelType(i, cfg.type);

        auto* channel = audioEngine.getChannel(i);
        if (!channel) continue;

        audioEngine.setChannelName(i, cfg.channelName);
        audioEngine.setChannelMuteGroup(i, cfg.muteGroup);
        channel->setGainDb    (cfg.gainDb);
        channel->setMonitorMode(cfg.monitorMode);
        channel->setMuted      (cfg.muted);
        channel->setSolo       (cfg.solo);
        channel->setRouting    (cfg.routing);
        channel->setOneShot   (cfg.oneShot);

        // --- Load all sections ---
        for (int s = 0; s < NUM_SECTIONS; ++s)
        {
            const auto& sd = cfg.sectionData[s];
            const juce::int64 secLoopLen = song.sectionLoopLengths[s];

            if (sd.hasLoopData && !sd.loopFileName.isEmpty())
            {
                auto loopFile = song.songDirectory.getChildFile(sd.loopFileName);

                if (loopFile.existsAsFile())
                {
                    if (channel->getLoopBufferSize() == 0)
                    {
                        DBG("WARNING: ch " + juce::String(i) +
                            " not yet prepared — loop file not loaded.");
                    }
                    else
                    {
                        channel->allocateSection(s);
                        const juce::int64 maxSamples = channel->getLoopBufferSize();
                        juce::AudioBuffer<float> tmp(2, static_cast<int>(maxSamples));
                        tmp.clear();

                        // Detect format by file extension
                        const juce::int64 loaded = loopFile.hasFileExtension(".wav")
                            ? loadWavFile(loopFile, tmp, maxSamples)
                            : loadLoopFile(loopFile, tmp, maxSamples);
                        if (loaded > 0)
                        {
                            if (!channel->loadLoopData(s, tmp, loaded))
                                DBG("WARNING: ch " + juce::String(i) + " sec " +
                                    juce::String(s) + " loadLoopData failed");
                        }
                    }
                }
            }

            // Overdub layers for this section
            for (int layer = 0; layer < sd.overdubLayerCount; ++layer)
            {
                auto layerFile = song.getSectionOverdubLayerFile(i, s, layer);
                if (layerFile.existsAsFile())
                {
                    const juce::int64 maxSamples = juce::jmax(secLoopLen,
                                                               channel->getLoopBufferSize());
                    juce::AudioBuffer<float> tmp(2, static_cast<int>(maxSamples));
                    tmp.clear();
                    const juce::int64 loaded = loadLoopFile(layerFile, tmp, maxSamples);
                    if (loaded > 0)
                        channel->loadOverdubLayer(s, tmp, loaded);
                }
            }
        }

        // Set active section on channel
        channel->setActiveSection(song.activeSection);

        // --- Plugins (async) ---
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

    // Restore section loop lengths
    for (int s = 0; s < NUM_SECTIONS; ++s)
        audioEngine.setSectionLoopLength(s, song.sectionLoopLengths[s]);

    // Set active section via command if not already at 0
    if (song.activeSection != 0)
    {
        Command cmd;
        cmd.type = CommandType::SetActiveSection;
        cmd.intValue1 = song.activeSection;
        audioEngine.sendCommand(cmd);
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

    // Write to a temp file first, then rename for crash-safety
    auto tmpFile = file.getSiblingFile(file.getFileName() + ".tmp");

    std::unique_ptr<juce::FileOutputStream> stream(tmpFile.createOutputStream());
    if (!stream)
        return juce::Result::fail("Cannot create: " + tmpFile.getFullPathName());

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
    if (stream->getStatus().failed())
        return juce::Result::fail("Write failed: " + tmpFile.getFullPathName());

    stream.reset();  // close file handle before rename

    if (!tmpFile.moveFileTo(file))
        return juce::Result::fail("Rename failed: " + tmpFile.getFullPathName() +
                                  " -> " + file.getFullPathName());

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
// WAV helpers
//==============================================================================

juce::AudioBuffer<float> SongManager::mixDownChannel(
    const juce::AudioBuffer<float>& baseLoop,
    const std::vector<juce::AudioBuffer<float>>& overdubLayers,
    juce::int64 numSamples)
{
    const int n = static_cast<int>(numSamples);
    juce::AudioBuffer<float> mixed(2, n);
    mixed.clear();

    // Copy base loop
    for (int ch = 0; ch < juce::jmin(2, baseLoop.getNumChannels()); ++ch)
        mixed.copyFrom(ch, 0, baseLoop, ch, 0, juce::jmin(n, baseLoop.getNumSamples()));

    // Sum overdub layers
    for (const auto& layer : overdubLayers)
        for (int ch = 0; ch < juce::jmin(2, layer.getNumChannels()); ++ch)
            mixed.addFrom(ch, 0, layer, ch, 0, juce::jmin(n, layer.getNumSamples()));

    return mixed;
}

juce::Result SongManager::saveWavFile(const juce::File& file,
                                       const juce::AudioBuffer<float>& buffer,
                                       juce::int64 numSamples,
                                       double sampleRate)
{
    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return juce::Result::fail("Invalid buffer for WAV save");
    if (buffer.getNumSamples() < static_cast<int>(numSamples))
        return juce::Result::fail("Buffer smaller than numSamples");

    auto tmpFile = file.getSiblingFile(file.getFileName() + ".tmp");

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::OutputStream> stream(tmpFile.createOutputStream());
    if (!stream)
        return juce::Result::fail("Cannot create: " + tmpFile.getFullPathName());

    auto writerOptions = juce::AudioFormatWriterOptions{}
        .withSampleRate(sampleRate)
        .withNumChannels(2)
        .withBitsPerSample(24);
    auto writer = wavFormat.createWriterFor(stream, writerOptions);
    if (!writer)
        return juce::Result::fail("Cannot create WAV writer for: " + tmpFile.getFullPathName());
    // stream ownership transferred to writer on success

    if (!writer->writeFromAudioSampleBuffer(buffer, 0, static_cast<int>(numSamples)))
    {
        writer.reset();
        tmpFile.deleteFile();
        return juce::Result::fail("WAV write failed: " + tmpFile.getFullPathName());
    }

    writer.reset(); // flush & close

    if (!tmpFile.moveFileTo(file))
        return juce::Result::fail("Rename failed: " + tmpFile.getFullPathName() +
                                  " -> " + file.getFullPathName());

    DBG("WAV saved: " + file.getFullPathName() +
        " (" + juce::String(numSamples) + " samples)");
    return juce::Result::ok();
}

juce::int64 SongManager::loadWavFile(const juce::File& file,
                                      juce::AudioBuffer<float>& buffer,
                                      juce::int64 maxSamples)
{
    if (!file.existsAsFile())
    {
        DBG("WAV file not found: " + file.getFullPathName());
        return -1;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (!reader)
    {
        DBG("Cannot create reader for: " + file.getFullPathName());
        return -1;
    }

    const juce::int64 samplesToRead = juce::jmin(static_cast<juce::int64>(reader->lengthInSamples),
                                                  maxSamples);
    if (samplesToRead <= 0) return -1;

    if (buffer.getNumSamples() < static_cast<int>(samplesToRead) ||
        buffer.getNumChannels() < 2)
    {
        DBG("Target buffer too small for WAV load");
        return -1;
    }

    // reader->read handles mono→stereo duplication when usesLeft/Right are both true
    const bool isMono = (reader->numChannels == 1);
    reader->read(&buffer, 0, static_cast<int>(samplesToRead), 0, true, !isMono);

    // If mono, duplicate channel 0 to channel 1
    if (isMono)
        buffer.copyFrom(1, 0, buffer, 0, 0, static_cast<int>(samplesToRead));

    DBG("WAV loaded: " + file.getFileName() +
        " (" + juce::String(samplesToRead) + "/" + juce::String(reader->lengthInSamples) + " samples)");
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
    cfg.muteGroup   = audioEngine.getChannelMuteGroup(channelIndex);
    cfg.oneShot     = channel->isOneShot();
    cfg.routing     = channel->getRouting();

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
    obj->setProperty("sample_rate",            song.sampleRate);
    obj->setProperty("metronome_enabled",       song.metronomeEnabled);
    obj->setProperty("metronome_output_left",   song.metronomeOutputLeft);
    obj->setProperty("metronome_output_right",  song.metronomeOutputRight);
    obj->setProperty("metronome_beats_per_bar", song.metronomeBeatsPerBar);
    obj->setProperty("metronome_gain",          song.metronomeGain);
    obj->setProperty("fixed_length_bars",       song.fixedLengthBars);
    obj->setProperty("master_gain",             song.masterGain);
    obj->setProperty("active_section",          song.activeSection);

    juce::Array<juce::var> secLenArr;
    for (int s = 0; s < NUM_SECTIONS; ++s)
        secLenArr.add(song.sectionLoopLengths[s]);
    obj->setProperty("section_loop_lengths",    secLenArr);

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
    if (song.formatVersion.isNotEmpty() &&
        song.formatVersion.compareNatural(Song::FORMAT_VERSION) > 0)
    {
        DBG("WARNING: Song format version " + song.formatVersion +
            " is newer than supported " + Song::FORMAT_VERSION +
            " — some data may be lost on re-save");
    }
    song.songName          = obj->getProperty("song_name")     .toString();
    song.description       = obj->getProperty("description")   .toString();
    song.creationTime      = juce::Time::fromISO8601(obj->getProperty("creation_time").toString());
    song.lastModified      = juce::Time::fromISO8601(obj->getProperty("last_modified") .toString());
    song.loopLengthSamples = obj->getProperty("loop_length_samples");
    song.bpm               = obj->getProperty("bpm");
    song.beatsPerLoop      = obj->getProperty("beats_per_loop");
    song.latchModeEnabled     = obj->getProperty("latch_mode_enabled");
    song.sampleRate           = obj->hasProperty("sample_rate")
                                 ? static_cast<double>(obj->getProperty("sample_rate")) : 44100.0;
    song.metronomeEnabled      = obj->getProperty("metronome_enabled");
    song.metronomeOutputLeft   = obj->getProperty("metronome_output_left");
    song.metronomeOutputRight  = obj->getProperty("metronome_output_right");
    song.metronomeBeatsPerBar  = obj->hasProperty("metronome_beats_per_bar")
                                 ? (int)obj->getProperty("metronome_beats_per_bar") : 4;
    song.metronomeGain         = obj->hasProperty("metronome_gain")
                                 ? static_cast<float>(static_cast<double>(
                                       obj->getProperty("metronome_gain"))) : 1.0f;
    song.fixedLengthBars       = obj->hasProperty("fixed_length_bars")
                                 ? (int)obj->getProperty("fixed_length_bars") : 0;
    song.masterGain            = obj->hasProperty("master_gain")
                                 ? static_cast<float>(static_cast<double>(
                                       obj->getProperty("master_gain"))) : 1.0f;

    song.activeSection         = obj->hasProperty("active_section")
                                 ? (int)obj->getProperty("active_section") : 0;

    if (obj->hasProperty("section_loop_lengths"))
    {
        auto* secArr = obj->getProperty("section_loop_lengths").getArray();
        if (secArr)
            for (int s = 0; s < juce::jmin(NUM_SECTIONS, secArr->size()); ++s)
                song.sectionLoopLengths[s] = static_cast<juce::int64>((double)secArr->getReference(s));
    }
    else
    {
        // Backward compat: old songs only have one loop length → section 0
        song.sectionLoopLengths[0] = song.loopLengthSamples;
    }

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
    obj->setProperty("mute_group",   ch.muteGroup);
    obj->setProperty("one_shot",     ch.oneShot);
    obj->setProperty("routing",      routingToJSON(ch.routing));

    // Per-section data
    juce::Array<juce::var> secArr;
    for (int s = 0; s < NUM_SECTIONS; ++s)
    {
        auto* secObj = new juce::DynamicObject();
        secObj->setProperty("has_loop_data",       ch.sectionData[s].hasLoopData);
        secObj->setProperty("loop_file_name",      ch.sectionData[s].loopFileName);
        secObj->setProperty("overdub_layer_count", ch.sectionData[s].overdubLayerCount);
        secArr.add(juce::var(secObj));
    }
    obj->setProperty("sections", secArr);

    // Backward compat: also write section 0 data at top level
    obj->setProperty("has_loop_data",       ch.sectionData[0].hasLoopData);
    obj->setProperty("loop_file_name",      ch.sectionData[0].loopFileName);
    obj->setProperty("overdub_layer_count", ch.sectionData[0].overdubLayerCount);

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
    ch.muteGroup   = obj->hasProperty("mute_group")
                     ? static_cast<int>(obj->getProperty("mute_group")) : 0;
    ch.oneShot     = obj->hasProperty("one_shot")
                     ? static_cast<bool>(obj->getProperty("one_shot")) : false;
    jsonToRouting(obj->getProperty("routing"), ch.routing);

    // Per-section data
    if (obj->hasProperty("sections"))
    {
        auto* secArr = obj->getProperty("sections").getArray();
        if (secArr)
        {
            for (int s = 0; s < juce::jmin(NUM_SECTIONS, secArr->size()); ++s)
            {
                auto* secObj = secArr->getReference(s).getDynamicObject();
                if (!secObj) continue;
                ch.sectionData[s].hasLoopData      = secObj->getProperty("has_loop_data");
                ch.sectionData[s].loopFileName      = secObj->getProperty("loop_file_name").toString();
                ch.sectionData[s].overdubLayerCount = secObj->hasProperty("overdub_layer_count")
                    ? static_cast<int>(secObj->getProperty("overdub_layer_count")) : 0;
            }
        }
    }
    else
    {
        // Backward compat: old format → section 0 only
        ch.sectionData[0].hasLoopData      = obj->getProperty("has_loop_data");
        ch.sectionData[0].loopFileName     = obj->getProperty("loop_file_name").toString();
        ch.sectionData[0].overdubLayerCount = obj->hasProperty("overdub_layer_count")
                                              ? static_cast<int>(obj->getProperty("overdub_layer_count")) : 0;
    }

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

//==============================================================================
// Current Song (auto-save / auto-restore)
//==============================================================================

juce::File SongManager::getCurrentSongDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("chief")
               .getChildFile("currentSong");
}

juce::Result SongManager::saveCurrentSong(AudioEngine& audioEngine)
{
    const auto dir = getCurrentSongDirectory();
    if (!dir.createDirectory())
        return juce::Result::fail("Cannot create currentSong directory: " +
                                  dir.getFullPathName());

    Song song;
    song.songName      = "currentSong";
    song.songDirectory = dir;

    const auto result = saveSong(song, audioEngine);
    if (result.wasOk())
        DBG("Current song auto-saved to: " + dir.getFullPathName());
    else
        DBG("Current song auto-save FAILED: " + result.getErrorMessage());
    return result;
}

juce::Result SongManager::loadCurrentSong(AudioEngine& audioEngine)
{
    const auto songFile = getCurrentSongDirectory().getChildFile("song.json");
    Song song;
    auto result = loadSong(songFile, song);
    if (result.failed()) return result;
    return applySongToEngine(song, audioEngine);
}

//==============================================================================
// Song Template (settings only, no recordings)
//==============================================================================

juce::Result SongManager::saveSongTemplate(Song& song, AudioEngine& audioEngine)
{
    song.lastModified = juce::Time::getCurrentTime();

    // Global state
    song.loopLengthSamples     = 0;
    song.bpm                   = audioEngine.getLoopEngine().getBPM();
    song.beatsPerLoop          = audioEngine.getLoopEngine().getBeatsPerLoop();
    song.latchModeEnabled      = audioEngine.isLatchMode();
    song.sampleRate            = audioEngine.getSampleRate();
    song.metronomeEnabled      = audioEngine.getMetronome().getEnabled();
    song.metronomeOutputLeft   = audioEngine.getMetronome().getOutputLeft();
    song.metronomeOutputRight  = audioEngine.getMetronome().getOutputRight();
    song.metronomeBeatsPerBar  = audioEngine.getMetronome().getBeatsPerBar();
    song.metronomeGain         = audioEngine.getMetronomeGain();
    song.fixedLengthBars       = audioEngine.getFixedLengthBars();
    song.masterGain            = audioEngine.getMasterGain();

    song.activeSection = 0;
    for (int s = 0; s < NUM_SECTIONS; ++s)
        song.sectionLoopLengths[s] = 0;

    for (int i = 0; i < 6; ++i)
    {
        auto* channel = audioEngine.getChannel(i);
        if (!channel) continue;

        song.channels[i] = readChannelState(channel, audioEngine, i);

        for (int s = 0; s < NUM_SECTIONS; ++s)
        {
            song.channels[i].sectionData[s].hasLoopData = false;
            song.channels[i].sectionData[s].loopFileName = {};
            song.channels[i].sectionData[s].overdubLayerCount = 0;
        }
    }

    auto json       = songToJSON(song);
    auto jsonString = juce::JSON::toString(json, true);

    // Save as a single .tmpl file (JSON) in songDirectory
    auto tmplFile = song.songDirectory.getChildFile(song.songName + ".tmpl");
    auto tmpFile  = tmplFile.getSiblingFile(tmplFile.getFileName() + ".tmp");

    if (!tmpFile.replaceWithText(jsonString))
        return juce::Result::fail("Failed to write: " + tmpFile.getFullPathName());
    if (!tmpFile.moveFileTo(tmplFile))
        return juce::Result::fail("Failed to rename: " + tmpFile.getFullPathName());

    DBG("Song template saved: " + tmplFile.getFullPathName());
    return juce::Result::ok();
}

juce::Result SongManager::applySongTemplateToEngine(const Song& song, AudioEngine& audioEngine)
{
    DBG("Applying song template to engine: " + song.songName);

    // Global settings
    audioEngine.getLoopEngine().setBPM(song.bpm);
    audioEngine.getLoopEngine().setBeatsPerLoop(song.beatsPerLoop);
    audioEngine.setLatchMode(song.latchModeEnabled);

    // Metronome
    audioEngine.getMetronome().setEnabled(song.metronomeEnabled);
    audioEngine.getMetronome().setBPM(song.bpm);
    audioEngine.getMetronome().setOutputChannels(song.metronomeOutputLeft,
                                                 song.metronomeOutputRight);
    audioEngine.setBeatsPerBar(song.metronomeBeatsPerBar);
    audioEngine.setMetronomeGain(song.metronomeGain);
    audioEngine.setFixedLengthBars(song.fixedLengthBars);
    audioEngine.setMasterGain(song.masterGain);

    // Per-channel settings (no loop data — preserve existing recordings)
    for (int i = 0; i < 6; ++i)
    {
        const auto& cfg = song.channels[i];

        audioEngine.setChannelType(i, cfg.type);

        auto* channel = audioEngine.getChannel(i);
        if (!channel) continue;

        audioEngine.setChannelName(i, cfg.channelName);
        audioEngine.setChannelMuteGroup(i, cfg.muteGroup);
        channel->setGainDb     (cfg.gainDb);
        channel->setMonitorMode(cfg.monitorMode);
        channel->setMuted      (cfg.muted);
        channel->setSolo       (cfg.solo);
        channel->setRouting    (cfg.routing);
        channel->setOneShot    (cfg.oneShot);

        // Load plugins (async)
        if (cfg.type == ChannelType::VSTi && !cfg.vstInstrument.identifier.isEmpty())
            audioEngine.loadPluginAsync(i, -1, cfg.vstInstrument.identifier,
                                        cfg.vstInstrument.stateBase64);

        for (int slot = 0; slot < 3; ++slot)
        {
            const auto& pd = cfg.fxPlugins[slot];
            if (!pd.identifier.isEmpty())
                audioEngine.loadPluginAsync(i, slot, pd.identifier, pd.stateBase64, pd.bypassed);
        }
    }

    return juce::Result::ok();
}
