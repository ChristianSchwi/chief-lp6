#pragma once

#include <JuceHeader.h>

//==============================================================================
enum class CommandType
{
    // Channel state
    StartRecord, StopRecord,
    StartPlayback, StopPlayback,
    StartOverdub, StopOverdub,

    // Channel parameters
    SetGain, SetMonitorMode, SetMute, SetSolo,

    // Routing
    SetInputRouting, SetOutputRouting, SetMIDIChannelFilter,

    // Plugins
    LoadPlugin, UnloadPlugin, SetPluginBypass,

    // Loop engine
    SetLoopLength, SetBPM, SetBeatsPerLoop, ResetPlayhead,

    // Global
    SetGlobalOverdubMode, ChangeActiveChannel, ClearChannel,
    ResetSong,        ///< Alle Channels clearen + Loop-Länge zurücksetzen

    // Metronome
    SetMetronomeOutput,
    SetMetronomeMute, ///< Sound stumm, Timing bleibt aktiv
    // NOTE: SetMetronomeEnabled wird NICHT als Command benötigt —
    //       setMetronomeEnabled() in AudioEngine schreibt direkt auf
    //       Message-Thread (Atomics + LoopEngine-Calls).

    // Emergency
    EmergencyStop,

    // Latch
    CancelPending
};

//==============================================================================
enum class MonitorMode { Off, AlwaysOn, WhileRecording, WhenTrackActive };

//==============================================================================
struct RoutingConfig
{
    int inputChannelLeft  {0};
    int inputChannelRight {1};
    int outputChannelLeft {0};
    int outputChannelRight{1};
    int midiChannelFilter {0};

    bool isMono()        const { return inputChannelRight == -1; }
    bool hasAudioInput() const { return inputChannelLeft  >= 0;  }

    RoutingConfig validated(int availableInputs, int availableOutputs) const
    {
        RoutingConfig r = *this;
        if (r.inputChannelLeft  >= availableInputs  && r.inputChannelLeft  != -1)
            r.inputChannelLeft  = juce::jmax(0, availableInputs - 2);
        if (r.inputChannelRight >= availableInputs  && r.inputChannelRight != -1)
            r.inputChannelRight = (availableInputs  > 1) ? availableInputs  - 1 : -1;
        if (r.outputChannelLeft >= availableOutputs)
            r.outputChannelLeft = juce::jmax(0, availableOutputs - 2);
        if (r.outputChannelRight >= availableOutputs)
            r.outputChannelRight = (availableOutputs > 1) ? availableOutputs - 1 : 0;
        if (r.midiChannelFilter < 0 || r.midiChannelFilter > 16)
            r.midiChannelFilter = 0;
        return r;
    }
};

//==============================================================================
struct Command
{
    CommandType type;
    int   channelIndex{-1};
    float floatValue  {0.0f};
    int   intValue1   {0};
    int   intValue2   {0};
    bool  boolValue   {false};

    union ComplexData
    {
        RoutingConfig routing;
        struct PluginData
        {
            int slotIndex; char identifier[256];
            PluginData() : slotIndex(0), identifier{0} {}
        } plugin;
        struct MetronomeData { int outputLeft{0}, outputRight{1}; } metronome;
        ComplexData()  : routing() {}
        ~ComplexData() {}
    } data;

    Command() : type(CommandType::EmergencyStop), data() {}

    // Factory methods
    static Command startRecord   (int ch) { Command c; c.type=CommandType::StartRecord;    c.channelIndex=ch; return c; }
    static Command stopRecord    (int ch) { Command c; c.type=CommandType::StopRecord;     c.channelIndex=ch; return c; }
    static Command startPlayback (int ch) { Command c; c.type=CommandType::StartPlayback;  c.channelIndex=ch; return c; }
    static Command stopPlayback  (int ch) { Command c; c.type=CommandType::StopPlayback;   c.channelIndex=ch; return c; }
    static Command setGain(int ch, float db) { Command c; c.type=CommandType::SetGain; c.channelIndex=ch; c.floatValue=db; return c; }
    static Command resetSong()             { Command c; c.type=CommandType::ResetSong;     return c; }
    static Command emergencyStop()         { Command c; c.type=CommandType::EmergencyStop; return c; }
    static Command setRouting(int ch, const RoutingConfig& r)
    {
        Command c; c.type=CommandType::SetInputRouting; c.channelIndex=ch; c.data.routing=r; return c;
    }
    static Command loadPlugin(int ch, int slot, const juce::String& id)
    {
        Command c; c.type=CommandType::LoadPlugin; c.channelIndex=ch; c.intValue1=slot;
        const int len=juce::jmin(id.length(),255); id.copyToUTF8(c.data.plugin.identifier,len+1);
        c.data.plugin.slotIndex=slot; return c;
    }
    static Command unloadPlugin(int ch, int slot)
    { Command c; c.type=CommandType::UnloadPlugin; c.channelIndex=ch; c.intValue1=slot; return c; }

    juce::int64 getLoopLength() const
    {
        return (static_cast<juce::int64>(intValue1) << 32) |
               (static_cast<juce::int64>(intValue2) & 0xFFFFFFFF);
    }
};

//==============================================================================
class CommandQueue
{
public:
    static constexpr int MAX_COMMANDS = 512;
    CommandQueue() : fifo(MAX_COMMANDS) { commands.resize(MAX_COMMANDS); }

    bool pushCommand(const Command& cmd)
    {
        int s1,n1,s2,n2;
        fifo.prepareToWrite(1,s1,n1,s2,n2);
        if (n1>0) { commands[s1]=cmd; fifo.finishedWrite(n1); return true; }
        return false;
    }
    template<typename F> void processCommands(F&& fn)
    {
        const int n=fifo.getNumReady(); if(!n) return;
        int s1,n1,s2,n2; fifo.prepareToRead(n,s1,n1,s2,n2);
        for(int i=0;i<n1;++i) fn(commands[s1+i]);
        for(int i=0;i<n2;++i) fn(commands[s2+i]);
        fifo.finishedRead(n1+n2);
    }
    int  getNumPending() const { return fifo.getNumReady(); }
    bool isFull()        const { return fifo.getFreeSpace()==0; }
private:
    juce::AbstractFifo   fifo;
    std::vector<Command> commands;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CommandQueue)
};
