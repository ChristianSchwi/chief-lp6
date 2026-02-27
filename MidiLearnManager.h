#pragma once

#include <JuceHeader.h>
#include <map>
#include <functional>

/**
 * @file MidiLearnManager.h
 * @brief MIDI-Learn und Mapping-Persistenz für alle Channel-Controls
 *
 * Architektur:
 *  - Läuft auf dem Message-Thread (Timer-basiert)
 *  - MIDI-Messages kommen vom Audio/MIDI-Thread via lock-free Queue
 *  - Im Learn-Modus: nächste CC/Note → wird dem aktuellen Control zugewiesen
 *  - Im Normal-Modus: eingehende Messages → Commands an AudioEngine
 *  - Persistenz: sofortiges Speichern nach jeder Änderung (XML)
 */

// Forward declaration
class AudioEngine;

//==============================================================================
/**
 * @brief Controls how channel-specific MIDI mappings are dispatched.
 *
 * PerChannel    — each channel has independent MIDI mappings
 *                 (e.g. CC1→Ch1 main, CC2→Ch2 main).
 * ActiveChannel — every channel-specific mapping is redirected to whichever
 *                 channel is currently active; one set of controls for all.
 */
enum class MidiLearnMode
{
    PerChannel,     ///< Each channel reacts to its own dedicated MIDI messages
    ActiveChannel   ///< All channel controls always apply to the active channel
};

//==============================================================================
/**
 * @brief Welches Control eines Channels ist gemeint
 */
enum class MidiControlTarget
{
    Record,              // legacy — kept for XML back-compat; not shown in UI
    Play,                // legacy — kept for XML back-compat; not shown in UI
    Overdub,             // legacy — kept for XML back-compat; not shown in UI
    Clear,
    Gain,                // CC → -60..+12 dB
    Mute,
    Solo,
    MonitorMode,         // CC → 4 Modi
    MainButton,          // context-aware: same behaviour as clicking the main button
    GlobalPlayStop,      // toggle transport play/stop
    NextChannel,         // select next channel (global)
    PrevChannel,         // select previous channel (global)
    NextSong,            // navigate to next song in show (global)
    PrevSong,            // navigate to previous song in show (global)
    Panic,               // emergency stop (global)
    MetronomeToggle,     // toggle metronome on/off (global)
    GlobalOverdubToggle, // toggle global overdub mode (global)
    LatchModeToggle,     // toggle latch mode (global)
    AutoStartToggle      // toggle auto-start (global)
};

//==============================================================================
/**
 * @brief Eine einzelne MIDI-Zuweisung
 */
struct MidiMapping
{
    int channelIndex  {-1};  ///< 0-5, -1 = global
    MidiControlTarget target {MidiControlTarget::Gain};
    int midiChannel   {0};   ///< 0 = alle, 1-16 = spezifisch
    int ccNumber      {-1};  ///< -1 = ungenutzt (Note-based)
    int noteNumber    {-1};  ///< -1 = ungenutzt (CC-based)
    float minValue    {0.0f};
    float maxValue    {1.0f};

    bool isValid() const { return ccNumber >= 0 || noteNumber >= 0; }
    juce::String getKey() const
    {
        return juce::String(channelIndex) + "_" +
               juce::String(static_cast<int>(target));
    }
};

//==============================================================================
/**
 * @brief Verwaltet MIDI-Learn-Modus und alle Mappings
 *
 * Verwendung:
 *   1. startLearning(channelIdx, target) → Enter Learn-Mode
 *   2. Nächste MIDI-Nachricht wird automatisch zugewiesen
 *   3. assignmentMade-Callback informiert die GUI
 *   4. Mappings werden sofort gespeichert
 */
class MidiLearnManager : public juce::Timer
{
public:
    //==========================================================================
    explicit MidiLearnManager(AudioEngine& engine);
    ~MidiLearnManager() override;

    //==========================================================================
    // Learn-Modus
    //==========================================================================

    /**
     * @brief Startet Learn-Modus für ein bestimmtes Control
     * @param channelIndex  Channel-Index (0-5)
     * @param target        Welches Control lernen soll
     */
    void startLearning(int channelIndex, MidiControlTarget target);

    /** Bricht den Learn-Modus ohne Zuweisung ab */
    void stopLearning();

    /** True wenn gerade auf eine MIDI-Message gewartet wird */
    bool isLearning() const { return learningActive.load(); }

    /** Info was gerade gelernt wird (für GUI-Feedback) */
    MidiMapping getLearningTarget() const { return currentLearningTarget; }

    //==========================================================================
    // Mapping-Verwaltung
    //==========================================================================

    /** Entfernt die Zuweisung für ein Control */
    void removeMapping(int channelIndex, MidiControlTarget target);

    /** Gibt das Mapping für ein Control zurück (isValid() = false wenn keins) */
    MidiMapping getMapping(int channelIndex, MidiControlTarget target) const;

    /** Alle aktuellen Mappings */
    std::vector<MidiMapping> getAllMappings() const;

    /** Callback wenn eine neue Zuweisung abgeschlossen wurde */
    std::function<void(const MidiMapping&)> onAssignmentMade;

    //==========================================================================
    // MIDI Learn Mode
    //==========================================================================

    /** Set the dispatch mode (PerChannel or ActiveChannel). Thread-safe. */
    void setMidiLearnMode(MidiLearnMode mode);

    /** Get the current dispatch mode. Thread-safe. */
    MidiLearnMode getMidiLearnMode() const;

    /** Callbacks for global song navigation (set by ShowComponent) */
    std::function<void()> onNextSong;
    std::function<void()> onPrevSong;

    //==========================================================================
    // MIDI-Eingabe (vom Audio/MIDI-Thread aufgerufen — thread-safe)
    //==========================================================================

    /**
     * @brief Gibt eine eingehende MIDI-Message in die Queue
     * Wird vom AudioEngine-MIDI-Callback auf dem MIDI-Thread aufgerufen.
     */
    void postMidiMessage(const juce::MidiMessage& msg);

    //==========================================================================
    // Persistenz
    //==========================================================================

    /**
     * Save the current mapping set to an explicit file path.
     * Normally called internally; exposed for potential external use (e.g. export).
     */
    bool saveMappings(const juce::File& file) const;

    /**
     * Load a mapping set from an explicit file path and replace the current set.
     * The active mode is NOT changed by this call.
     */
    bool loadMappings(const juce::File& file);

    /**
     * Returns the mappings file for the currently active mode.
     * Legacy name kept for call-site compatibility.
     */
    juce::File getDefaultMappingsFile() const;

    /** Gibt eine lesbare Beschreibung eines Controls zurück */
    static juce::String targetName(MidiControlTarget t);

private:
    //==========================================================================
    AudioEngine& audioEngine;

    // Learn-State
    std::atomic<bool> learningActive{false};
    MidiMapping currentLearningTarget;
    juce::CriticalSection learningLock;

    // MIDI learn mode — 0 = PerChannel, 1 = ActiveChannel
    std::atomic<int> midiLearnModeFlag {0};

    // Mappings  key = channelIndex_targetIndex
    std::map<juce::String, MidiMapping> mappings;
    mutable juce::CriticalSection mappingsLock;

    // Lock-free Queue für MIDI-Messages aus dem Audio-Thread
    juce::AbstractFifo fifo{256};
    struct QueuedMidi
    {
        juce::MidiMessage message;
    };
    std::array<QueuedMidi, 256> midiQueue;

    //==========================================================================
    // Timer: verarbeitet Queue auf Message-Thread
    void timerCallback() override;
    void processMidiMessage(const juce::MidiMessage& msg);
    void applyMapping(const MidiMapping& mapping, const juce::MidiMessage& msg);

    // Persistence helpers
    void saveImmediately();                                    // save mode file + global file
    juce::File getMappingsFileForMode(MidiLearnMode m) const; // per-mode channel file path
    juce::File getGlobalMappingsFile()                const;  // midi_global.xml (shared)
    juce::File getPreferencesFile()                   const;  // preferences.xml path
    void savePreferences();                                    // write active mode to prefs
    void loadPreferences();                                    // read mode from prefs + migrate
    void saveGlobalMappings();                                 // write channelIndex==-1 entries
    void loadGlobalMappings();                                 // read channelIndex==-1 entries

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLearnManager)
};
