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
 * @brief Welches Control eines Channels ist gemeint
 */
enum class MidiControlTarget
{
    Record,
    Play,
    Overdub,
    Clear,
    Gain,        // CC → -60..+12 dB
    Mute,
    Solo,
    MonitorMode  // CC → 4 Modi
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

    bool saveMappings(const juce::File& file) const;
    bool loadMappings(const juce::File& file);
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

    void saveImmediately();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLearnManager)
};
