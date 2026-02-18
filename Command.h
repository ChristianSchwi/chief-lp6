#pragma once

#include <JuceHeader.h>

/**
 * @file Command.h
 * @brief Thread-safe command system for GUI → Audio Thread communication
 * 
 * All commands are POD types to allow safe copying into lock-free FIFO.
 * Commands are processed in the audio thread at the start of each callback.
 */

//==============================================================================
/**
 * @brief Types of commands that can be sent from GUI to audio thread
 */
enum class CommandType
{
    // Channel state commands
    StartRecord,
    StopRecord,
    StartPlayback,
    StopPlayback,
    StartOverdub,
    StopOverdub,
    
    // Channel parameter commands
    SetGain,
    SetMonitorMode,
    SetMute,
    SetSolo,
    
    // Routing commands
    SetInputRouting,
    SetOutputRouting,
    SetMIDIChannelFilter,
    
    // Plugin commands
    LoadPlugin,
    UnloadPlugin,
    SetPluginBypass,
    
    // Loop engine commands
    SetLoopLength,
    SetBPM,
    SetBeatsPerLoop,
    SetQuantization,
    ResetPlayhead,
    
    // Global commands
    SetGlobalOverdubMode,
    ChangeActiveChannel,
    ClearChannel,
    
    // Metronome commands
    SetMetronomeEnabled,
    SetMetronomeOutput,
    
    // Emergency commands
    EmergencyStop
};

//==============================================================================
/**
 * @brief Monitor modes for channel monitoring
 */
enum class MonitorMode
{
    Off,              ///< No monitoring
    AlwaysOn,         ///< Monitor always
    WhileRecording,   ///< Monitor only during recording
    WhenTrackActive   ///< Monitor only when track is selected
};

//==============================================================================
/**
 * @brief Routing configuration for audio channels
 */
struct RoutingConfig
{
    int inputChannelLeft{0};    ///< -1 = no input (VSTi), >= 0 = hardware channel
    int inputChannelRight{1};   ///< -1 = mono (duplicate left), >= 0 = hardware channel
    int outputChannelLeft{0};   ///< Hardware output channel
    int outputChannelRight{1};  ///< Hardware output channel
    
    int midiChannelFilter{0};   ///< 0 = all channels, 1-16 = specific MIDI channel (VSTi only)
    
    /**
     * @brief Check if this routing is mono input
     */
    bool isMono() const { return inputChannelRight == -1; }
    
    /**
     * @brief Check if this routing has audio input (vs VSTi with no input)
     */
    bool hasAudioInput() const { return inputChannelLeft >= 0; }
    
    /**
     * @brief Validate routing against available channels
     * @param availableInputs Number of available input channels
     * @param availableOutputs Number of available output channels
     * @return Validated routing configuration
     */
    RoutingConfig validated(int availableInputs, int availableOutputs) const
    {
        RoutingConfig result = *this;
        
        // Validate input channels
        if (result.inputChannelLeft >= availableInputs && result.inputChannelLeft != -1)
        {
            result.inputChannelLeft = juce::jmax(0, availableInputs - 2);  // Try to use last stereo pair
        }
        
        if (result.inputChannelRight >= availableInputs && result.inputChannelRight != -1)
        {
            if (availableInputs > 1)
                result.inputChannelRight = availableInputs - 1;
            else
                result.inputChannelRight = -1;  // Fall back to mono
        }
        
        // Validate output channels
        if (result.outputChannelLeft >= availableOutputs)
        {
            result.outputChannelLeft = juce::jmax(0, availableOutputs - 2);
        }
        
        if (result.outputChannelRight >= availableOutputs)
        {
            if (availableOutputs > 1)
                result.outputChannelRight = availableOutputs - 1;
            else
                result.outputChannelRight = 0;  // Both outputs to same channel
        }
        
        // Ensure MIDI channel filter is in valid range
        if (result.midiChannelFilter < 0 || result.midiChannelFilter > 16)
        {
            result.midiChannelFilter = 0;  // Default to all channels
        }
        
        return result;
    }
};

//==============================================================================
/**
 * @brief Command structure for lock-free FIFO communication
 * 
 * This is a POD type that can be safely copied between threads.
 * Use union for complex data to avoid dynamic allocation.
 */
struct Command
{
    CommandType type;
    int channelIndex{-1};  ///< -1 for global commands, 0-5 for channel-specific
    
    // Generic parameters for simple commands
    float floatValue{0.0f};
    int intValue1{0};
    int intValue2{0};
    bool boolValue{false};
    
    // Union for complex data (no dynamic allocation)
    union ComplexData
    {
        RoutingConfig routing;
        
        struct PluginData
        {
            int slotIndex;      ///< 0-2 for FX slots
            char identifier[256]; ///< Plugin identifier string
            
            PluginData() : slotIndex(0), identifier{0} {}
        } plugin;
        
        struct MetronomeData
        {
            int outputLeft;
            int outputRight;
            
            MetronomeData() : outputLeft(0), outputRight(0) {}
        } metronome;
        
        // Explicit constructors/destructors for union members
        ComplexData() : routing() {}
        ~ComplexData() {}
    } data;
    
    // Default constructor
    Command() : type(CommandType::EmergencyStop), data() {}
    
    // Convenience constructors for common command types
    static Command startRecord(int channel)
    {
        Command cmd;
        cmd.type = CommandType::StartRecord;
        cmd.channelIndex = channel;
        return cmd;
    }
    
    static Command stopRecord(int channel)
    {
        Command cmd;
        cmd.type = CommandType::StopRecord;
        cmd.channelIndex = channel;
        return cmd;
    }
    
    static Command startPlayback(int channel)
    {
        Command cmd;
        cmd.type = CommandType::StartPlayback;
        cmd.channelIndex = channel;
        return cmd;
    }
    
    static Command stopPlayback(int channel)
    {
        Command cmd;
        cmd.type = CommandType::StopPlayback;
        cmd.channelIndex = channel;
        return cmd;
    }
    
    static Command setGain(int channel, float gainDb)
    {
        Command cmd;
        cmd.type = CommandType::SetGain;
        cmd.channelIndex = channel;
        cmd.floatValue = gainDb;
        return cmd;
    }
    
    static Command setMonitorMode(int channel, MonitorMode mode)
    {
        Command cmd;
        cmd.type = CommandType::SetMonitorMode;
        cmd.channelIndex = channel;
        cmd.intValue1 = static_cast<int>(mode);
        return cmd;
    }
    
    static Command setRouting(int channel, const RoutingConfig& routing)
    {
        Command cmd;
        cmd.type = CommandType::SetInputRouting;  // Can handle both input/output
        cmd.channelIndex = channel;
        cmd.data.routing = routing;
        return cmd;
    }
    
    static Command setBPM(double bpm)
    {
        Command cmd;
        cmd.type = CommandType::SetBPM;
        cmd.floatValue = static_cast<float>(bpm);
        return cmd;
    }
    
    static Command setLoopLength(juce::int64 lengthInSamples)
    {
        Command cmd;
        cmd.type = CommandType::SetLoopLength;
        cmd.intValue1 = static_cast<int>(lengthInSamples >> 32);  // High 32 bits
        cmd.intValue2 = static_cast<int>(lengthInSamples & 0xFFFFFFFF);  // Low 32 bits
        return cmd;
    }
    
    static Command emergencyStop()
    {
        Command cmd;
        cmd.type = CommandType::EmergencyStop;
        return cmd;
    }
    
    /**
     * @brief Create load plugin command
     * @param channel Channel index
     * @param slot FX slot (0-2) or -1 for VSTi
     * @param identifier Plugin identifier string
     */
    static Command loadPlugin(int channel, int slot, const juce::String& identifier)
    {
        Command cmd;
        cmd.type = CommandType::LoadPlugin;
        cmd.channelIndex = channel;
        cmd.intValue1 = slot;
        
        // Copy identifier to union (max 255 chars)
        const int maxLen = 255;
        const int len = juce::jmin(identifier.length(), maxLen);
        identifier.copyToUTF8(cmd.data.plugin.identifier, len + 1);
        cmd.data.plugin.slotIndex = slot;
        
        return cmd;
    }
    
    /**
     * @brief Create unload plugin command
     */
    static Command unloadPlugin(int channel, int slot)
    {
        Command cmd;
        cmd.type = CommandType::UnloadPlugin;
        cmd.channelIndex = channel;
        cmd.intValue1 = slot;
        return cmd;
    }
    
    /**
     * @brief Reconstruct juce::int64 from two int32 values
     */
    juce::int64 getLoopLength() const
    {
        return (static_cast<juce::int64>(intValue1) << 32) | (static_cast<juce::int64>(intValue2) & 0xFFFFFFFF);
    }
};

//==============================================================================
/**
 * @brief Thread-safe command queue using lock-free FIFO
 * 
 * Usage:
 *   GUI Thread: pushCommand(cmd)
 *   Audio Thread: processCommands([](const Command& cmd) { ... })
 */
class CommandQueue
{
public:
    static constexpr int MAX_COMMANDS = 512;  ///< Enough for ~1 second @ 44.1kHz with 512 buffer
    
    CommandQueue()
        : fifo(MAX_COMMANDS)
    {
        commands.resize(MAX_COMMANDS);
    }
    
    /**
     * @brief Push a command from GUI thread (non-blocking)
     * @return true if command was added, false if queue is full
     */
    bool pushCommand(const Command& cmd)
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite(1, start1, size1, start2, size2);
        
        if (size1 > 0)
        {
            commands[start1] = cmd;
            fifo.finishedWrite(size1);
            return true;
        }
        
        return false;  // Queue full
    }
    
    /**
     * @brief Process all pending commands in audio thread
     * @param processor Function to process each command
     */
    template<typename Func>
    void processCommands(Func&& processor)
    {
        const int numReady = fifo.getNumReady();
        
        if (numReady == 0)
            return;
        
        int start1, size1, start2, size2;
        fifo.prepareToRead(numReady, start1, size1, start2, size2);
        
        // Process first block
        for (int i = 0; i < size1; ++i)
            processor(commands[start1 + i]);
        
        // Process second block (if wrapped)
        if (size2 > 0)
        {
            for (int i = 0; i < size2; ++i)
                processor(commands[start2 + i]);
        }
        
        fifo.finishedRead(size1 + size2);
    }
    
    /**
     * @brief Get number of commands waiting to be processed
     */
    int getNumPending() const
    {
        return fifo.getNumReady();
    }
    
    /**
     * @brief Check if queue is full
     */
    bool isFull() const
    {
        return fifo.getFreeSpace() == 0;
    }
    
private:
    juce::AbstractFifo fifo;
    std::vector<Command> commands;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CommandQueue)
};
