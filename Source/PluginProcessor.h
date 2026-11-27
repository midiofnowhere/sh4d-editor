#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "CcTable.h"

//==============================================================================
// Small single-producer / single-consumer queue.
template <typename T, int N>
class Spsc
{
public:
    bool push (const T& v)
    {
        int s1, z1, s2, z2;
        fifo.prepareToWrite (1, s1, z1, s2, z2);
        if (z1 + z2 < 1) { fifo.finishedWrite (0); return false; }
        data[(size_t) (z1 > 0 ? s1 : s2)] = v;
        fifo.finishedWrite (1);
        return true;
    }

    bool pop (T& v)
    {
        int s1, z1, s2, z2;
        fifo.prepareToRead (1, s1, z1, s2, z2);
        if (z1 + z2 < 1) { fifo.finishedRead (0); return false; }
        v = data[(size_t) (z1 > 0 ? s1 : s2)];
        fifo.finishedRead (1);
        return true;
    }

private:
    juce::AbstractFifo fifo { N };
    std::array<T, (size_t) N> data {};
};

//==============================================================================
class SH4dProcessor : public juce::AudioProcessor,
                      private juce::Timer
{
public:
    // startThreads = false is for the headless tests: no engine thread, no timer, no MIDI devices.
    explicit SH4dProcessor (bool startThreads = true);
    ~SH4dProcessor() override;

    //==============================================================================
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SH-4d Editor"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    // Parameters
    static juce::String paramId (int part, const char* ccId) { return "p" + juce::String (part + 1) + "_" + ccId; }
    static juce::String channelId (int part)                 { return "p" + juce::String (part + 1) + "_channel"; }

    juce::RangedAudioParameter* getCcParam (int part, int ccIndex) const  { return ccParams[(size_t) part][(size_t) ccIndex]; }
    juce::RangedAudioParameter* getChannelParam (int part) const          { return chParams[(size_t) part]; }
    int  getChannelForPart (int part) const;

    int  getNumAllParams() const { return (int) allParams.size(); }
    juce::RangedAudioParameter* getParamByIndex (int index) const;
    int  getParamIndex (const juce::String& id) const;
    int  getCcParamIndex (int part, int ccIndex) const { return ccParamIndex[(size_t) part][(size_t) ccIndex]; }

    // What the knob shows (wire value 0-127; switches 0 or 127) ...
    int  getCcValue (int part, int ccIndex) const;
    // ... and what actually goes to the hardware after modulation.
    int  getEffectiveValue (int part, int ccIndex) const { return eff[(size_t) part][(size_t) ccIndex].load(); }

    using PartValues = std::array<int, (size_t) sh4d::kNumCc>;
    PartValues getPartValues (int part) const;
    void setPartValues (int part, const PartValues&);
    void copyPart (int from, int to);
    void resetPart (int part);

    //==============================================================================
    // Scenes + morph (message thread)
    bool hasScene (int slot) const    { return sceneHas[(size_t) slot]; }
    void storeScene (int slot);
    void recallScene (int slot);
    void morphScenes (int a, int b, float t);
    int  getMorphA() const;
    int  getMorphB() const;
    void setMorphScenes (int a, int b);

    //==============================================================================
    // Modulation routing (message thread). The engine thread reads published snapshots.
    std::vector<sh4d::Route> getRoutes() const;
    void setRoutes (std::vector<sh4d::Route>);
    void addRoute (const sh4d::Route&);
    bool isModulated (int part, int ccIndex) const;
    int  getRoutesVersion() const { return routesVersion.load(); }
    float getSourceValue (int source) const { return srcValue[(size_t) source].load(); }

    //==============================================================================
    // MIDI learn (message thread)
    void setLearnTarget (int paramIndex)  { learnTarget.store (paramIndex); }
    int  getLearnTarget() const           { return learnTarget.load(); }
    std::vector<sh4d::LearnMap> getLearnMaps() const;
    bool hasLearn (int paramIndex) const;
    void clearLearn (int paramIndex);         // -1 = all
    void removeLearnAt (int listIndex);
    int  getLearnVersion() const { return learnVersion.load(); }

    //==============================================================================
    // MIDI devices (message thread)
    juce::StringArray getMidiInputNames() const;
    juce::StringArray getMidiOutputNames() const;
    juce::String getHwInName() const;
    juce::String getHwOutName() const;
    juce::String getCtlInName() const;
    void setHwIn (const juce::String& name);
    void setHwOut (const juce::String& name);
    void setCtlIn (const juce::String& name);
    bool isHwInOpen() const   { return hwInOpen.load(); }
    bool isHwOutOpen() const  { return hwOutOpen.load(); }
    bool isCtlInOpen() const  { return ctlInOpen.load(); }
    void autoDetectDevices();
    void refreshMidiDevices();

    //==============================================================================
    // UI -> engine requests (message thread only)
    void requestSendAll()  { sendAllRequested.store (true); }
    void sendProgram (int part, int bankMsb, int bankLsb, int program);
    void sendPanic (int part);
    void sendNote (bool on, int part, int note, int velocity);
    void sendPitchBend (int part, int value14);      // 0..16383, 8192 = centre
    void setSelectedPart (int part);
    int  getSelectedPart() const { return selectedPart.load(); }

    //==============================================================================
    // Monitor: packed as (type << 24) | (channel << 16) | (data1 << 8) | data2
    // type 0 = control change, 1 = program change, 2 = note on, 3 = note off
    std::atomic<uint32_t> lastTx { 0 }, lastRx { 0 };
    std::atomic<uint32_t> txCount { 0 }, rxCount { 0 };

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    // Test hooks
    void setOutputSink (std::function<void (const juce::MidiMessage&)> s) { sink = std::move (s); }
    void engineTick (double nowMs);          // one iteration of the engine loop
    void pumpMessageThreadWork();            // what the 60 Hz timer does
    void pushDeviceEvent (int origin, const juce::MidiMessage&);

private:
    enum class Origin { Hardware = 0, Controller = 1, Host = 2 };

    struct InEvt   { uint8_t b0 = 0, b1 = 0, b2 = 0, len = 0, origin = 0; };
    struct ShortMsg{ uint8_t b0 = 0, b1 = 0, b2 = 0, len = 0; };
    struct OutMsg  { uint8_t channel = 0, type = 0, data1 = 0, data2 = 0; };
    struct UiWrite { int paramIndex = -1; float norm = 0.0f; };
    struct RouteTable { std::vector<sh4d::Route> routes; bool modulated[sh4d::kNumSynthParts][sh4d::kNumCc] {}; };
    struct LearnTable { std::vector<sh4d::LearnMap> maps; };

    struct EngineThread : public juce::Thread
    {
        explicit EngineThread (SH4dProcessor& o) : juce::Thread ("SH-4d engine"), owner (o) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                owner.engineTick (juce::Time::getMillisecondCounterHiRes());
                wait (2);
            }
        }
        SH4dProcessor& owner;
    };

    struct InCallback : public juce::MidiInputCallback
    {
        InCallback (SH4dProcessor& o, int org) : owner (o), origin (org) {}
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override { owner.pushDeviceEvent (origin, m); }
        SH4dProcessor& owner;
        int origin;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override { pumpMessageThreadWork(); if (++ticksSinceRefresh >= 60) { ticksSinceRefresh = 0; refreshMidiDevices(); } }

    // engine thread
    void handleIncoming (const InEvt&, double nowMs);
    void followFromMidi (int part, int ccIndex, int value, double nowMs);
    void applyLearned (const sh4d::LearnMap&, int value);
    void forwardController (juce::MidiMessage);
    void computeSources (double dtMs);
    void emit (const juce::MidiMessage&);
    void emitOut (const OutMsg&);
    void sendToDevice (const juce::MidiMessage&);
    void sendToHostOut (const juce::MidiMessage&);
    bool hostOutActive() const;
    bool wouldConsume (const InEvt&) const;
    const sh4d::LearnMap* findLearn (int cc, int channel) const;
    int  partChannel (int part) const { return getChannelForPart (part); }

    void noteTx (int type, int channel, int d1, int d2);
    void noteRx (int type, int channel, int d1, int d2);
    void pushOut (const OutMsg&);

    // message thread
    void publishRoutes (std::vector<sh4d::Route>);
    void publishLearn (std::vector<sh4d::LearnMap>);
    void loadModelFromState();
    void writeRoutesToState (const std::vector<sh4d::Route>&);
    void writeLearnToState (const std::vector<sh4d::LearnMap>&);
    void openInput (std::unique_ptr<juce::MidiInput>& port, InCallback& cb, std::atomic<bool>& flag, const juce::String& wanted);

    static juce::MidiMessage makeMsg (const uint8_t* b, int len);

    //==============================================================================
    std::array<std::array<juce::RangedAudioParameter*, (size_t) sh4d::kNumCc>, (size_t) sh4d::kNumSynthParts> ccParams {};
    std::array<std::array<std::atomic<float>*, (size_t) sh4d::kNumCc>, (size_t) sh4d::kNumSynthParts>          ccRaw {};
    std::array<std::array<int, (size_t) sh4d::kNumCc>, (size_t) sh4d::kNumSynthParts>                          ccParamIndex {};
    std::array<juce::RangedAudioParameter*, (size_t) sh4d::kNumParts> chParams {};
    std::array<std::atomic<float>*, (size_t) sh4d::kNumParts>         chRaw {};
    std::array<std::atomic<float>*, (size_t) sh4d::kNumMacros>        macroRaw {};
    std::array<std::atomic<float>*, (size_t) sh4d::kNumLfos>          lfoRateRaw {}, lfoShapeRaw {}, lfoSyncRaw {};
    std::atomic<float> *thruRaw = nullptr, *hostOutRaw = nullptr, *remapRaw = nullptr,
                       *sendOnConnectRaw = nullptr, *morphRaw = nullptr;

    std::vector<juce::RangedAudioParameter*> allParams;
    std::map<juce::String, int> paramIndexById;

    // ---- engine-thread state
    int  lastSent[sh4d::kNumSynthParts][sh4d::kNumCc] {};
    double lastSendMs[sh4d::kNumSynthParts][sh4d::kNumCc] {};
    int  followVal[sh4d::kNumSynthParts][sh4d::kNumCc] {};
    double followMs[sh4d::kNumSynthParts][sh4d::kNumCc] {};
    int  ccToIndex[128] {};
    int  scanStart = 0;
    double lastTickMs = 0.0;
    float src[sh4d::kNumSources] {};
    float aftertouchIn = 0.0f, pitchBendIn = 0.0f;
    struct Lfo { double phase = 0.0; float prev = 0.0f, next = 0.0f; } lfo[sh4d::kNumLfos];
    juce::Random rng;

    // ---- shared
    std::atomic<int> eff[sh4d::kNumSynthParts][sh4d::kNumCc] {};
    std::array<std::atomic<float>, (size_t) sh4d::kNumSources> srcValue {};
    std::atomic<bool> sendAllRequested { false };
    std::atomic<int>  selectedPart { 0 };
    std::atomic<int>  learnTarget { -1 };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<int>  routesVersion { 0 }, learnVersion { 0 };
    std::atomic<bool> hwInOpen { false }, hwOutOpen { false }, ctlInOpen { false };

    std::atomic<const RouteTable*> routePtr { nullptr };
    std::atomic<const LearnTable*> learnPtr { nullptr };
    std::vector<std::unique_ptr<RouteTable>> routeStore;     // recent snapshots kept alive for readers
    std::vector<std::unique_ptr<LearnTable>> learnStore;
    std::vector<sh4d::Route>     routesModel;                // message thread
    std::vector<sh4d::LearnMap>  learnModel;

    Spsc<OutMsg,   256>  uiOutFifo;
    Spsc<InEvt,   1024>  hostInFifo;
    Spsc<InEvt,   1024>  devInFifo;
    Spsc<ShortMsg, 1024> hostOutFifo;
    Spsc<UiWrite,  512>  uiWriteFifo;
    Spsc<sh4d::LearnMap, 16> learnFifo;
    juce::CriticalSection devLock;

    // scenes
    std::array<std::array<int, (size_t) (sh4d::kNumSynthParts * sh4d::kNumCc)>, (size_t) sh4d::kNumScenes> sceneData {};
    std::array<bool, (size_t) sh4d::kNumScenes> sceneHas {};
    float lastMorph = 0.0f;
    int ticksSinceRefresh = 0;

    // MIDI devices
    std::unique_ptr<juce::MidiInput>  hwIn, ctlIn;
    std::unique_ptr<juce::MidiOutput> hwOut;
    juce::CriticalSection outLock;
    InCallback hwCallback { *this, 0 }, ctlCallback { *this, 1 };

    std::function<void (const juce::MidiMessage&)> sink;
    juce::MidiBuffer scratch;
    EngineThread engineThread { *this };
    bool threadsRunning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SH4dProcessor)
};
