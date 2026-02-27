#include "MidiLearnManager.h"
#include "AudioEngine.h"
#include "Command.h"

//==============================================================================
MidiLearnManager::MidiLearnManager(AudioEngine& engine)
    : audioEngine(engine)
{
    // Load the active mode preference first, then populate the mappings map:
    //   1. Global mappings (channelIndex == -1) from midi_global.xml — always active,
    //      independent of mode.  loadPreferences() also migrates the legacy file.
    //   2. Channel-specific mappings from the mode-appropriate file.
    loadPreferences();
    loadGlobalMappings();
    loadMappings(getMappingsFileForMode(getMidiLearnMode()));

    startTimerHz(100);  // process MIDI queue at ~10 ms resolution
}

MidiLearnManager::~MidiLearnManager()
{
    stopTimer();
}

//==============================================================================
// Learn-Modus
//==============================================================================

void MidiLearnManager::startLearning(int channelIndex, MidiControlTarget target)
{
    juce::ScopedLock sl(learningLock);
    currentLearningTarget.channelIndex = channelIndex;
    currentLearningTarget.target       = target;
    currentLearningTarget.ccNumber     = -1;
    currentLearningTarget.noteNumber   = -1;
    learningActive.store(true, std::memory_order_release);

    DBG("MIDI Learn: Warte auf Eingabe für Channel " +
        juce::String(channelIndex) + " / " + targetName(target));
}

void MidiLearnManager::stopLearning()
{
    learningActive.store(false, std::memory_order_release);
    DBG("MIDI Learn: Abgebrochen");
}

//==============================================================================
// MIDI Learn Mode
//==============================================================================

void MidiLearnManager::setMidiLearnMode(MidiLearnMode newMode)
{
    const MidiLearnMode currentMode = getMidiLearnMode();
    if (currentMode == newMode)
        return;

    // 1. Persist the current mapping set before switching.
    saveMappings(getMappingsFileForMode(currentMode));

    // 2. Switch the active mode.
    midiLearnModeFlag.store(static_cast<int>(newMode), std::memory_order_release);

    // 3. Load the mapping set that belongs to the new mode.
    loadMappings(getMappingsFileForMode(newMode));

    // 4. Persist the new mode preference.
    savePreferences();

    DBG("MIDI Learn Mode -> " +
        juce::String(newMode == MidiLearnMode::PerChannel ? "PerChannel"
                                                          : "ActiveChannel"));
}

MidiLearnMode MidiLearnManager::getMidiLearnMode() const
{
    return static_cast<MidiLearnMode>(
        midiLearnModeFlag.load(std::memory_order_acquire));
}

//==============================================================================
// Mapping-Verwaltung
//==============================================================================

void MidiLearnManager::removeMapping(int channelIndex, MidiControlTarget target)
{
    MidiMapping dummy;
    dummy.channelIndex = channelIndex;
    dummy.target       = target;

    juce::ScopedLock sl(mappingsLock);
    mappings.erase(dummy.getKey());
    saveImmediately();
}

MidiMapping MidiLearnManager::getMapping(int channelIndex, MidiControlTarget target) const
{
    MidiMapping dummy;
    dummy.channelIndex = channelIndex;
    dummy.target       = target;

    juce::ScopedLock sl(mappingsLock);
    auto it = mappings.find(dummy.getKey());
    if (it != mappings.end())
        return it->second;

    return MidiMapping{};  // isValid() == false
}

std::vector<MidiMapping> MidiLearnManager::getAllMappings() const
{
    juce::ScopedLock sl(mappingsLock);
    std::vector<MidiMapping> result;
    result.reserve(mappings.size());
    for (auto& kv : mappings)
        result.push_back(kv.second);
    return result;
}

//==============================================================================
// MIDI-Eingang vom Audio-Thread
//==============================================================================

void MidiLearnManager::postMidiMessage(const juce::MidiMessage& msg)
{
    // Nur CC und Note-On/Off interessieren uns
    if (!msg.isController() && !msg.isNoteOn() && !msg.isNoteOff())
        return;

    if (fifo.getFreeSpace() < 1)
        return;  // Queue voll

    int idx1, size1, idx2, size2;
    fifo.prepareToWrite(1, idx1, size1, idx2, size2);
    if (size1 > 0)
    {
        midiQueue[idx1].message = msg;
        fifo.finishedWrite(1);
    }
}

//==============================================================================
// Timer: verarbeitet Queue auf Message-Thread
//==============================================================================

void MidiLearnManager::timerCallback()
{
    int idx1, size1, idx2, size2;
    fifo.prepareToRead(fifo.getNumReady(), idx1, size1, idx2, size2);

    for (int i = 0; i < size1; ++i)
        processMidiMessage(midiQueue[idx1 + i].message);
    for (int i = 0; i < size2; ++i)
        processMidiMessage(midiQueue[idx2 + i].message);

    fifo.finishedRead(size1 + size2);
}

void MidiLearnManager::processMidiMessage(const juce::MidiMessage& msg)
{
    // ---- Learn-Modus ----
    if (learningActive.load(std::memory_order_acquire))
    {
        // Nur CC oder Note-On als Trigger akzeptieren
        if (!msg.isController() && !msg.isNoteOn())
            return;

        MidiMapping newMapping;
        {
            juce::ScopedLock sl(learningLock);
            newMapping = currentLearningTarget;
        }

        newMapping.midiChannel = msg.getChannel();

        if (msg.isController())
        {
            newMapping.ccNumber   = msg.getControllerNumber();
            newMapping.noteNumber = -1;
        }
        else
        {
            newMapping.noteNumber = msg.getNoteNumber();
            newMapping.ccNumber   = -1;
        }

        // Wertebereich je nach Control-Typ
        switch (newMapping.target)
        {
            case MidiControlTarget::Gain:
                newMapping.minValue = -60.0f;
                newMapping.maxValue =  12.0f;
                break;
            case MidiControlTarget::MonitorMode:
                newMapping.minValue = 0.0f;
                newMapping.maxValue = 3.0f;
                break;
            default:
                newMapping.minValue = 0.0f;
                newMapping.maxValue = 1.0f;
                break;
        }

        {
            juce::ScopedLock sl(mappingsLock);
            mappings[newMapping.getKey()] = newMapping;
        }

        learningActive.store(false, std::memory_order_release);
        saveImmediately();

        DBG("MIDI Learn: Zugewiesen – CC " + juce::String(newMapping.ccNumber) +
            " / Note " + juce::String(newMapping.noteNumber) +
            " → Ch" + juce::String(newMapping.channelIndex) +
            " " + targetName(newMapping.target));

        if (onAssignmentMade)
            onAssignmentMade(newMapping);

        return;
    }

    // ---- Normal-Modus: Mappings anwenden ----
    juce::ScopedLock sl(mappingsLock);
    for (auto& kv : mappings)
    {
        const auto& m = kv.second;
        if (!m.isValid())
            continue;

        // MIDI-Kanal prüfen
        if (m.midiChannel != 0 && m.midiChannel != msg.getChannel())
            continue;

        bool matches = false;
        if (m.ccNumber >= 0 && msg.isController() &&
            msg.getControllerNumber() == m.ccNumber)
            matches = true;
        if (m.noteNumber >= 0 && (msg.isNoteOn() || msg.isNoteOff()) &&
            msg.getNoteNumber() == m.noteNumber)
            matches = true;

        if (matches)
            applyMapping(m, msg);
    }
}

void MidiLearnManager::applyMapping(const MidiMapping& m, const juce::MidiMessage& msg)
{
    // Normalisierter Wert 0..1
    float norm = 0.0f;
    if (msg.isController())
        norm = msg.getControllerValue() / 127.0f;
    else if (msg.isNoteOn())
        norm = 1.0f;
    else if (msg.isNoteOff())
        norm = 0.0f;

    const float mapped = m.minValue + norm * (m.maxValue - m.minValue);

    // In ActiveChannel mode, channel-specific mappings are redirected to
    // whichever channel is currently active.  Global mappings (channelIndex < 0)
    // are never redirected.
    const int effectiveChannel =
        (getMidiLearnMode() == MidiLearnMode::ActiveChannel && m.channelIndex >= 0)
        ? audioEngine.getActiveChannel()
        : m.channelIndex;

    Command cmd;
    cmd.channelIndex = effectiveChannel;

    switch (m.target)
    {
        case MidiControlTarget::Gain:
            cmd.type       = CommandType::SetGain;
            cmd.floatValue = mapped;
            break;

        case MidiControlTarget::Mute:
            cmd.type      = CommandType::SetMute;
            cmd.boolValue = (norm >= 0.5f);
            break;

        case MidiControlTarget::Solo:
            cmd.type      = CommandType::SetSolo;
            cmd.boolValue = (norm >= 0.5f);
            break;

        case MidiControlTarget::Record:
        {
            if (norm < 0.5f) return;
            auto* ch = audioEngine.getChannel(effectiveChannel);
            if (!ch) return;
            const auto st = ch->getState();
            cmd.type = (st == ChannelState::Recording || st == ChannelState::Overdubbing)
                       ? CommandType::StopRecord : CommandType::StartRecord;
            break;
        }

        case MidiControlTarget::Play:
        {
            if (norm < 0.5f) return;
            auto* ch = audioEngine.getChannel(effectiveChannel);
            if (!ch) return;
            cmd.type = (ch->getState() == ChannelState::Playing)
                       ? CommandType::StopPlayback : CommandType::StartPlayback;
            break;
        }

        case MidiControlTarget::Overdub:
        {
            if (norm < 0.5f) return;
            auto* ch = audioEngine.getChannel(effectiveChannel);
            if (!ch) return;
            cmd.type = (ch->getState() == ChannelState::Overdubbing)
                       ? CommandType::StopOverdub : CommandType::StartOverdub;
            break;
        }

        case MidiControlTarget::Clear:
            if (norm >= 0.5f)
                cmd.type = CommandType::ClearChannel;
            else
                return;
            break;

        case MidiControlTarget::MonitorMode:
            cmd.type      = CommandType::SetMonitorMode;
            cmd.intValue1 = juce::jlimit(0, 3, static_cast<int>(mapped));
            break;

        case MidiControlTarget::MainButton:
        {
            if (norm < 0.5f) return;

            auto* ch = audioEngine.getChannel(effectiveChannel);
            if (!ch) return;

            if (ch->hasPendingRecord() || ch->hasPendingOverdub() ||
                ch->hasPendingPlay()   || ch->hasPendingStop())
            {
                Command cancelCmd;
                cancelCmd.type         = CommandType::CancelPending;
                cancelCmd.channelIndex = effectiveChannel;
                audioEngine.sendCommand(cancelCmd);
                return;
            }

            const bool overdubMode = audioEngine.isInOverdubMode();
            const auto st          = ch->getState();
            const bool hasLoop     = ch->hasLoop();

            audioEngine.setActiveChannel(effectiveChannel);

            if (overdubMode && st == ChannelState::Playing)
            {
                Command c;
                c.type         = CommandType::StartOverdub;
                c.channelIndex = effectiveChannel;
                audioEngine.sendCommand(c);
            }
            else if (st == ChannelState::Overdubbing || st == ChannelState::Recording)
                audioEngine.sendCommand(Command::stopRecord(effectiveChannel));
            else if (!hasLoop)
                audioEngine.sendCommand(Command::startRecord(effectiveChannel));
            else if (st == ChannelState::Playing)
                audioEngine.sendCommand(Command::stopPlayback(effectiveChannel));
            else
                audioEngine.sendCommand(Command::startPlayback(effectiveChannel));

            return;
        }

        case MidiControlTarget::GlobalPlayStop:
            if (norm >= 0.5f) audioEngine.setPlaying(!audioEngine.isPlaying());
            return;

        case MidiControlTarget::NextChannel:
            if (norm >= 0.5f) audioEngine.nextChannel();
            return;

        case MidiControlTarget::PrevChannel:
            if (norm >= 0.5f) audioEngine.prevChannel();
            return;

        case MidiControlTarget::NextSong:
            if (norm >= 0.5f && onNextSong) onNextSong();
            return;

        case MidiControlTarget::PrevSong:
            if (norm >= 0.5f && onPrevSong) onPrevSong();
            return;

        case MidiControlTarget::Panic:
            if (norm >= 0.5f) audioEngine.emergencyStop();
            return;

        case MidiControlTarget::MetronomeToggle:
            if (norm >= 0.5f)
                audioEngine.setMetronomeEnabled(!audioEngine.getMetronome().getEnabled());
            return;

        case MidiControlTarget::GlobalOverdubToggle:
            if (norm >= 0.5f)
                audioEngine.setOverdubMode(!audioEngine.isInOverdubMode());
            return;

        case MidiControlTarget::LatchModeToggle:
            if (norm >= 0.5f)
                audioEngine.setLatchMode(!audioEngine.isLatchMode());
            return;

        case MidiControlTarget::AutoStartToggle:
            if (norm >= 0.5f)
                audioEngine.setAutoStart(!audioEngine.isAutoStartEnabled(),
                                         audioEngine.getAutoStartThresholdDb());
            return;

        default:
            return;
    }

    audioEngine.sendCommand(cmd);
}

//==============================================================================
// Persistenz
//==============================================================================

void MidiLearnManager::saveImmediately()
{
    saveMappings(getMappingsFileForMode(getMidiLearnMode()));
    saveGlobalMappings();
}

bool MidiLearnManager::saveMappings(const juce::File& file) const
{
    file.getParentDirectory().createDirectory();

    auto xml = std::make_unique<juce::XmlElement>("MidiMappings");

    juce::ScopedLock sl(mappingsLock);
    for (auto& kv : mappings)
    {
        const auto& m = kv.second;
        if (!m.isValid() || m.channelIndex < 0)   // global entries go to midi_global.xml
            continue;

        auto* entry = xml->createNewChildElement("Mapping");
        entry->setAttribute("channelIndex",  m.channelIndex);
        entry->setAttribute("target",        static_cast<int>(m.target));
        entry->setAttribute("midiChannel",   m.midiChannel);
        entry->setAttribute("ccNumber",      m.ccNumber);
        entry->setAttribute("noteNumber",    m.noteNumber);
        entry->setAttribute("minValue",      m.minValue);
        entry->setAttribute("maxValue",      m.maxValue);
    }

    bool ok = xml->writeTo(file);
    DBG("MIDI-Mappings (channel) saved: " + file.getFullPathName() +
        " (" + juce::String(xml->getNumChildElements()) + " entries)");
    return ok;
}

bool MidiLearnManager::loadMappings(const juce::File& file)
{
    // Remove only channel-specific entries (channelIndex >= 0).
    // Global entries (channelIndex == -1) live in midi_global.xml and must
    // not be touched when swapping between per-channel and active-channel sets.
    {
        juce::ScopedLock sl(mappingsLock);
        for (auto it = mappings.begin(); it != mappings.end(); )
        {
            if (it->second.channelIndex >= 0)
                it = mappings.erase(it);
            else
                ++it;
        }
    }

    if (!file.existsAsFile())
    {
        DBG("MIDI-Mappings (channel): no file at " + file.getFullPathName() + " — starting empty");
        return false;
    }

    auto xml = juce::parseXML(file);
    if (!xml || xml->getTagName() != "MidiMappings")
        return false;

    juce::ScopedLock sl(mappingsLock);

    for (auto* entry : xml->getChildIterator())
    {
        MidiMapping m;
        m.channelIndex  = entry->getIntAttribute("channelIndex",  -1);
        m.target        = static_cast<MidiControlTarget>(
                              entry->getIntAttribute("target", 0));
        m.midiChannel   = entry->getIntAttribute("midiChannel",   0);
        m.ccNumber      = entry->getIntAttribute("ccNumber",      -1);
        m.noteNumber    = entry->getIntAttribute("noteNumber",    -1);
        m.minValue      = static_cast<float>(
                              entry->getDoubleAttribute("minValue", 0.0));
        m.maxValue      = static_cast<float>(
                              entry->getDoubleAttribute("maxValue", 1.0));

        // Only accept channel-specific entries here (globals come from loadGlobalMappings)
        if (m.channelIndex >= 0 && m.channelIndex < 6 && m.isValid())
            mappings[m.getKey()] = m;
    }

    DBG("MIDI-Mappings (channel) loaded: " + juce::String(mappings.size()) +
        " total entries (incl. globals) after " + file.getFullPathName());
    return true;
}

juce::File MidiLearnManager::getDefaultMappingsFile() const
{
    // Returns the file for the currently active mode (backward-compat name).
    return getMappingsFileForMode(getMidiLearnMode());
}

//==============================================================================
// Private persistence helpers
//==============================================================================

juce::File MidiLearnManager::getMappingsFileForMode(MidiLearnMode mode) const
{
    const juce::File dir =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("chief");

    return mode == MidiLearnMode::PerChannel
        ? dir.getChildFile("midi_per_channel.xml")
        : dir.getChildFile("midi_active_channel.xml");
}

juce::File MidiLearnManager::getGlobalMappingsFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("chief")
        .getChildFile("midi_global.xml");
}

void MidiLearnManager::saveGlobalMappings()
{
    const juce::File file = getGlobalMappingsFile();
    file.getParentDirectory().createDirectory();

    auto xml = std::make_unique<juce::XmlElement>("MidiMappings");

    juce::ScopedLock sl(mappingsLock);
    for (auto& kv : mappings)
    {
        const auto& m = kv.second;
        if (!m.isValid() || m.channelIndex >= 0)   // channel entries go to mode file
            continue;

        auto* entry = xml->createNewChildElement("Mapping");
        entry->setAttribute("channelIndex",  m.channelIndex);
        entry->setAttribute("target",        static_cast<int>(m.target));
        entry->setAttribute("midiChannel",   m.midiChannel);
        entry->setAttribute("ccNumber",      m.ccNumber);
        entry->setAttribute("noteNumber",    m.noteNumber);
        entry->setAttribute("minValue",      m.minValue);
        entry->setAttribute("maxValue",      m.maxValue);
    }

    xml->writeTo(file);
    DBG("MIDI-Mappings (global) saved: " + file.getFullPathName() +
        " (" + juce::String(xml->getNumChildElements()) + " entries)");
}

void MidiLearnManager::loadGlobalMappings()
{
    const juce::File file = getGlobalMappingsFile();

    if (!file.existsAsFile())
    {
        DBG("MIDI-Mappings (global): no file — starting empty");
        return;
    }

    auto xml = juce::parseXML(file);
    if (!xml || xml->getTagName() != "MidiMappings")
        return;

    // Remove any stale global entries first, then reload fresh ones.
    {
        juce::ScopedLock sl(mappingsLock);
        for (auto it = mappings.begin(); it != mappings.end(); )
        {
            if (it->second.channelIndex < 0)
                it = mappings.erase(it);
            else
                ++it;
        }
    }

    juce::ScopedLock sl(mappingsLock);
    for (auto* entry : xml->getChildIterator())
    {
        MidiMapping m;
        m.channelIndex  = entry->getIntAttribute("channelIndex",  -1);
        m.target        = static_cast<MidiControlTarget>(
                              entry->getIntAttribute("target", 0));
        m.midiChannel   = entry->getIntAttribute("midiChannel",   0);
        m.ccNumber      = entry->getIntAttribute("ccNumber",      -1);
        m.noteNumber    = entry->getIntAttribute("noteNumber",    -1);
        m.minValue      = static_cast<float>(
                              entry->getDoubleAttribute("minValue", 0.0));
        m.maxValue      = static_cast<float>(
                              entry->getDoubleAttribute("maxValue", 1.0));

        if (m.channelIndex == -1 && m.isValid())
            mappings[m.getKey()] = m;
    }

    DBG("MIDI-Mappings (global) loaded: " +
        juce::String(xml->getNumChildElements()) + " entries");
}

juce::File MidiLearnManager::getPreferencesFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("chief")
        .getChildFile("preferences.xml");
}

void MidiLearnManager::savePreferences()
{
    const juce::File file = getPreferencesFile();
    file.getParentDirectory().createDirectory();

    auto xml = std::make_unique<juce::XmlElement>("Preferences");
    xml->setAttribute("midiLearnMode",
                      midiLearnModeFlag.load(std::memory_order_relaxed));

    xml->writeTo(file);
    DBG("Preferences saved: midiLearnMode=" +
        juce::String(midiLearnModeFlag.load()));
}

void MidiLearnManager::loadPreferences()
{
    const juce::File file = getPreferencesFile();

    if (!file.existsAsFile())
    {
        // ── First-run migration ──────────────────────────────────────────────
        // If the legacy single-file MidiMappings.xml exists, copy its mapping
        // entries into the appropriate per-mode file and remove the old file so
        // this migration only happens once.
        const juce::File legacyFile =
            juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("chief")
            .getChildFile("MidiMappings.xml");

        if (legacyFile.existsAsFile())
        {
            DBG("Migrating legacy MidiMappings.xml ...");

            auto legacyXml = juce::parseXML(legacyFile);
            int legacyMode = 0;
            if (legacyXml)
                legacyMode = legacyXml->getIntAttribute("midiLearnMode", 0);

            midiLearnModeFlag.store(legacyMode, std::memory_order_relaxed);

            // Split the legacy file into channel-specific and global files.
            // The new loadMappings / loadGlobalMappings only accept their own kind,
            // so we build two separate XML documents from the one legacy file.
            if (legacyXml)
            {
                auto channelXml = std::make_unique<juce::XmlElement>("MidiMappings");
                auto globalXml  = std::make_unique<juce::XmlElement>("MidiMappings");

                for (auto* entry : legacyXml->getChildIterator())
                {
                    const int idx = entry->getIntAttribute("channelIndex", -1);
                    auto* dest = (idx >= 0) ? channelXml.get() : globalXml.get();
                    dest->addChildElement(new juce::XmlElement(*entry));
                }

                const juce::File modeFile =
                    getMappingsFileForMode(static_cast<MidiLearnMode>(legacyMode));
                modeFile.getParentDirectory().createDirectory();
                channelXml->writeTo(modeFile);

                const juce::File globalFile = getGlobalMappingsFile();
                globalXml->writeTo(globalFile);

                DBG("  -> " + juce::String(channelXml->getNumChildElements()) +
                    " channel entries → " + modeFile.getFileName());
                DBG("  -> " + juce::String(globalXml->getNumChildElements()) +
                    " global entries  → " + globalFile.getFileName());
            }
        }
        // else: completely fresh install — default mode (PerChannel) is fine.

        savePreferences();   // write preferences.xml so migration only runs once
        return;
    }

    // Normal load
    auto xml = juce::parseXML(file);
    if (!xml || xml->getTagName() != "Preferences")
        return;

    midiLearnModeFlag.store(xml->getIntAttribute("midiLearnMode", 0),
                            std::memory_order_relaxed);

    DBG("Preferences loaded: midiLearnMode=" +
        juce::String(midiLearnModeFlag.load()));
}

//==============================================================================
juce::String MidiLearnManager::targetName(MidiControlTarget t)
{
    switch (t)
    {
        case MidiControlTarget::Record:              return "Record";
        case MidiControlTarget::Play:                return "Play";
        case MidiControlTarget::Overdub:             return "Overdub";
        case MidiControlTarget::Clear:               return "Clear";
        case MidiControlTarget::MainButton:          return "Main Button";
        case MidiControlTarget::Gain:                return "Gain";
        case MidiControlTarget::Mute:                return "Mute";
        case MidiControlTarget::Solo:                return "Solo";
        case MidiControlTarget::MonitorMode:         return "Monitor Mode";
        case MidiControlTarget::GlobalPlayStop:      return "Global Play/Stop";
        case MidiControlTarget::NextChannel:         return "Next Channel";
        case MidiControlTarget::PrevChannel:         return "Prev Channel";
        case MidiControlTarget::NextSong:            return "Next Song";
        case MidiControlTarget::PrevSong:            return "Prev Song";
        case MidiControlTarget::Panic:               return "Panic";
        case MidiControlTarget::MetronomeToggle:     return "Metronome On/Off";
        case MidiControlTarget::GlobalOverdubToggle: return "Overdub Mode On/Off";
        case MidiControlTarget::LatchModeToggle:     return "Latch Mode On/Off";
        case MidiControlTarget::AutoStartToggle:     return "Auto Start On/Off";
        default:                                     return "Unknown";
    }
}
