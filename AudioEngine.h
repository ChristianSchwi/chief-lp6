#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include "Command.h"
#include "LoopEngine.h"
#include "Metronome.h"
#include "PluginHostWrapper.h"
#include "Channel.h"
#include "MidiLearnManager.h"

class AudioChannel;
class VSTiChannel;

//==============================================================================
/**
 * @file AudioEngine.h
 * @brief Main audio processing engine
 *
 * Thread-safety:
 *   Audio thread  : audioDeviceIOCallbackWithContext()
 *   MIDI thread   : handleIncomingMidiMessage()
 *   Message thread: everything else
 */
class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    //==========================================================================
    // AudioIODeviceCallback
    //==========================================================================

    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    //==========================================================================
    // MidiInputCallback
    //==========================================================================

    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    void openMidiInputs();

    //==========================================================================
    // Initialization
    //==========================================================================

    juce::String initialiseAudio(int inputChannels  = 2,
                                 int outputChannels = 2,
                                 double sampleRate  = 0.0,
                                 int bufferSize     = 0);

    //==========================================================================
    // Command Queue
    //==========================================================================

    bool sendCommand(const Command& cmd);

    //==========================================================================
    // Device Info
    //==========================================================================

    juce::AudioDeviceManager& getDeviceManager()    { return deviceManager; }
    int    getNumInputChannels()  const { return numInputChannels; }
    int    getNumOutputChannels() const { return numOutputChannels; }
    double getSampleRate()        const { return currentSampleRate; }
    int    getBufferSize()        const { return currentBufferSize; }

    //==========================================================================
    // Channel Management
    //==========================================================================

    Channel*    getChannel(int index);
    void        setChannelType(int index, ChannelType type);
    ChannelType getChannelType(int index) const;

    /** Get/set the display name for a channel (message thread only). */
    juce::String getChannelName(int index) const;
    void         setChannelName(int index, const juce::String& name);

    //==========================================================================
    // Plugin Management
    //==========================================================================

    /** Non-owning access to plugin host for state serialization (Message Thread). */
    PluginHostWrapper& getPluginHost() { return *pluginHost; }

    /**
     * @brief Load plugin asynchronously, optionally restoring saved state.
     * @param stateBase64  Base64-encoded plugin state. Applied after load. Empty = default state.
     */
    void loadPluginAsync(int channelIndex,
                         int slotIndex,
                         const juce::String& pluginIdentifier,
                         const juce::String& stateBase64 = {},
                         bool bypassed = false);

    /** Called on the message thread when a plugin fails to load. Set before loading. */
    std::function<void(int channelIndex, int slotIndex, const juce::String& error)>
        onPluginLoadError;

    void removePlugin(int channelIndex, int slotIndex);

    //==========================================================================
    // Global Playback State
    //==========================================================================

    void setPlaying(bool shouldPlay);
    bool isPlaying() const { return isPlayingFlag.load(std::memory_order_relaxed); }

    void setOverdubMode(bool enabled);
    bool isInOverdubMode() const { return overdubMode.load(std::memory_order_relaxed); }

    /** Latch mode: actions (rec/play/stop) take effect at the next loop boundary. */
    void setLatchMode(bool enabled) { latchMode.store(enabled, std::memory_order_release); }
    bool isLatchMode()        const { return latchMode.load(std::memory_order_relaxed); }

    void emergencyStop();

    //==========================================================================
    // Auto-Start (input threshold trigger)
    //==========================================================================

    /** Enable/disable auto-start and set the trigger threshold in dB. */
    void  setAutoStart(bool enabled, float thresholdDb);
    bool  isAutoStartEnabled()    const { return autoStartEnabled.load(std::memory_order_relaxed); }
    float getAutoStartThresholdDb() const;

    //==========================================================================
    // Count-In
    //==========================================================================

    /** Set number of count-in beats before recording actually starts (0 = off). */
    void setCountInBeats(int beats);
    int  getCountInBeats() const { return countInBeats.load(std::memory_order_relaxed); }

    /** True while a count-in countdown is running (safe to call from message thread). */
    bool isCountingIn()            const { return countInActive.load(std::memory_order_relaxed); }
    /** Channel index waiting to record after count-in (-1 if none). */
    int  getCountInPendingChannel() const { return pendingRecordChannel.load(std::memory_order_relaxed); }

    //==========================================================================
    // Loop Engine
    //==========================================================================

    LoopEngine& getLoopEngine() { return *loopEngine; }

    //==========================================================================
    // Active Channel Navigation (Spec Abschnitt 7)
    //==========================================================================

    /** Currently active channel index (0-5). MIDI-mappable target for rec/play/stop. */
    int  getActiveChannel() const { return activeChannelIndex.load(std::memory_order_relaxed); }

    /** Set active channel directly (clamped to 0-5). */
    void setActiveChannel(int index);

    /** Move active channel to next slot (wraps around). */
    void nextChannel();

    /** Move active channel to previous slot (wraps around). */
    void prevChannel();

    //==========================================================================
    // Metronome
    //==========================================================================

    Metronome& getMetronome() { return *metronome; }

    /**
     * @brief Metronom-Modus an/aus.
     *
     * GEBLOCKT wenn hasAnyRecordings() == true.
     *
     * AN  → Loop-Länge sofort aus BPM × Beats berechnet, bleibt fix.
     * AUS → Loop-Länge auf 0 zurückgesetzt (freier Modus).
     */
    void setMetronomeEnabled(bool enabled);

    /**
     * @brief Click-Sound stumm/laut.  Timing/Modus bleibt aktiv.
     * Kann jederzeit geändert werden — unabhängig von hasAnyRecordings().
     */
    void setMetronomeMuted(bool muted);

    /** @brief Ausgangskanal-Paar für den Click. */
    void setMetronomeOutput(int leftChannel, int rightChannel);

    /** @brief Beats per bar — controls accent beat detection and bar-based count-in. */
    void setBeatsPerBar(int n);
    int  getBeatsPerBar() const;

    //==========================================================================
    // Song Reset
    //==========================================================================

    /**
     * @brief Alle Channels clearen + Loop-Länge zurücksetzen.
     * Nach diesem Aufruf ist hasAnyRecordings() == false → Metronom schaltbar.
     */
    void resetSong();

    /** @brief true wenn irgendein Channel aufgenommenen Inhalt hat. */
    bool hasAnyRecordings() const;

    //==========================================================================
    // Diagnostics
    //==========================================================================

    double getCPUUsage()           const { return deviceManager.getCpuUsage() * 100.0; }
    int    getNumPendingCommands() const { return commandQueue.getNumPending(); }
    bool   isCommandQueueFull()    const { return commandQueue.isFull(); }

    MidiLearnManager& getMidiLearnManager() { return *midiLearnManager; }

    //==========================================================================
    // Audio Settings Persistence
    //==========================================================================

    /** Save current AudioDeviceManager state to disk. Returns true on success. */
    bool saveAudioSettings() const;

    /** Returns the settings file path. */
    juce::File getAudioSettingsFile() const;

private:
    //==========================================================================
    // Core components
    juce::AudioDeviceManager       deviceManager;
    std::unique_ptr<LoopEngine>    loopEngine;
    std::unique_ptr<Metronome>     metronome;
    std::unique_ptr<PluginHostWrapper> pluginHost;
    std::unique_ptr<MidiLearnManager> midiLearnManager;
    CommandQueue commandQueue;

    // 6 stereo channels
    std::array<std::unique_ptr<Channel>, 6> channels;

    // MIDI: thread-safe bridge between MIDI thread and audio thread
    juce::MidiMessageCollector midiCollector;

    //==========================================================================
    // Audio thread state (written only from audioDeviceAboutToStart)
    int    numInputChannels  {0};
    int    numOutputChannels {0};
    double currentSampleRate {44100.0};
    int    currentBufferSize {512};

    // Atomics (shared between threads)
    std::atomic<bool>  isPlayingFlag        {false};
    std::atomic<bool>  overdubMode          {false};
    std::atomic<bool>  latchMode            {false};
    std::atomic<bool>  isInitialised        {false};
    std::atomic<int>   activeChannelIndex   {0};

    // Auto-start
    std::atomic<bool>  autoStartEnabled         {false};
    std::atomic<float> autoStartThresholdLinear {0.031623f};   // ~-30 dB
    bool               autoStartTriggered       {false};       // audio thread only

    // Count-in
    std::atomic<int>   countInBeats            {0};
    std::atomic<bool>  countInActive           {false};   // written audio thread, read UI thread
    juce::int64        countInSamplesRemaining {0};       // audio thread only
    std::atomic<int>   pendingRecordChannel    {-1};      // written audio thread, read UI thread

    // Working buffers (audio thread only)
    juce::AudioBuffer<float> inputBuffer;
    juce::AudioBuffer<float> outputBuffer;

    // Diagnostics
    std::atomic<juce::int64>  totalSamplesProcessed {0};
    std::atomic<juce::int32>  xrunCount             {0};

    // Channel display names (message thread only)
    std::array<juce::String, 6> channelNames;

    //==========================================================================
    // Command processing (audio thread)
    void processCommand       (const Command& cmd);
    void processGlobalCommand (const Command& cmd);
    void processChannelCommand(const Command& cmd);

    void clearOutputBuffer(float* const* outputChannelData, int numChannels, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
