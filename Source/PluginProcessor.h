#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include "CcTable.h"

class SH4dProcessor : public juce::AudioProcessor
{
public:
    SH4dProcessor();
    ~SH4dProcessor() override = default;

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
    // Parameter access (message thread or audio thread)
    static juce::String paramId (int part, const char* ccId)
    {
        return "p" + juce::String (part + 1) + "_" + ccId;
    }
    static juce::String channelId (int part) { return "p" + juce::String (part + 1) + "_channel"; }

    juce::RangedAudioParameter* getCcParam (int part, int ccIndex) const      { return ccParams[(size_t) part][(size_t) ccIndex]; }
    juce::RangedAudioParameter* getChannelParam (int part) const              { return chParams[(size_t) part]; }
    int  getChannelForPart (int part) const;

    // Current value as it would go on the wire (0-127; switches 0 or 127).
    int  getCcValue (int part, int ccIndex) const;

    //==============================================================================
    // UI -> audio thread requests (message thread only; lock-free)
    void requestSendAll()  { sendAllRequested.store (true); }
    void sendProgram (int part, int bankMsb, int bankLsb, int program);
    void sendPanic (int part);

    //==============================================================================
    // Monitor: packed as (type << 24) | (channel << 16) | (data1 << 8) | data2
    // type 0 = control change, 1 = program change
    std::atomic<uint32_t> lastTx { 0 }, lastRx { 0 };
    std::atomic<uint32_t> txCount { 0 }, rxCount { 0 };

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct OutMsg { uint8_t channel, type, data1, data2; };
    void pushOut (const OutMsg* items, int count);
    void noteTx (int type, int channel, int d1, int d2);
    void noteRx (int type, int channel, int d1, int d2);
    void applyIncoming (int part, int ccIndex, int value);

    std::array<std::array<juce::RangedAudioParameter*, (size_t) sh4d::kNumCc>, (size_t) sh4d::kNumSynthParts> ccParams {};
    std::array<std::array<std::atomic<float>*, (size_t) sh4d::kNumCc>, (size_t) sh4d::kNumSynthParts>          ccRaw {};
    std::array<juce::RangedAudioParameter*, (size_t) sh4d::kNumParts> chParams {};
    std::array<std::atomic<float>*, (size_t) sh4d::kNumParts>         chRaw {};
    std::atomic<float>* thruRaw = nullptr;

    int lastSent[sh4d::kNumSynthParts][sh4d::kNumCc] {};   // audio thread only
    int ccToIndex[128] {};
    int scanStart = 0;

    std::atomic<bool> sendAllRequested { false };

    juce::AbstractFifo fifo { 128 };
    std::array<OutMsg, 128> fifoData {};

    juce::MidiBuffer scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SH4dProcessor)
};
