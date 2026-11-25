#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;
using namespace sh4d;

namespace
{
    // Max CC messages emitted per audio block. Keeps a "Send All" (or a wild
    // automation lane) from flooding the USB-MIDI link in one burst; anything
    // not sent this block is simply picked up on the next one.
    constexpr int kMsgBudgetPerBlock = 16;

    constexpr uint32_t pack (int type, int ch, int d1, int d2)
    {
        return ((uint32_t) type << 24) | ((uint32_t) ch << 16) | ((uint32_t) d1 << 8) | (uint32_t) d2;
    }

    int toWire (const CcDef& d, float raw)
    {
        if (d.kind == Kind::Switch)
            return raw > 0.5f ? 127 : 0;

        return jlimit (0, 127, (int) std::lround (raw));
    }
}

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout SH4dProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int p = 0; p < kNumSynthParts; ++p)
    {
        for (int i = 0; i < kNumCc; ++i)
        {
            const auto& d = kCc[i];
            const ParameterID id (paramId (p, d.id), 1);
            const String name = "P" + String (p + 1) + " " + d.name;

            if (d.kind == Kind::Knob)
                layout.add (std::make_unique<AudioParameterInt> (id, name, 0, 127, d.def));
            else
                layout.add (std::make_unique<AudioParameterBool> (id, name, d.def > 0));
        }
    }

    // MIDI channel per part. These defaults are a guess - set them to match the
    // channels configured on your SH-4d.
    static const int defaultChannel[kNumParts] = { 1, 2, 3, 4, 10 };
    for (int p = 0; p < kNumParts; ++p)
    {
        const String name = p < kNumSynthParts ? "Part " + String (p + 1) + " Channel" : String ("Rhythm Channel");
        layout.add (std::make_unique<AudioParameterInt> (ParameterID (channelId (p), 1), name, 1, 16,
                                                         defaultChannel[p],
                                                         AudioParameterIntAttributes().withAutomatable (false)));
    }

    // Behaviour flags / randomiser settings (saved with the project, not automatable)
    layout.add (std::make_unique<AudioParameterBool> (ParameterID ("ccThru", 1), "Pass incoming CC through", false,
                                                      AudioParameterBoolAttributes().withAutomatable (false)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID ("randAmount", 1), "Random Amount",
                                                       NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
                                                       AudioParameterFloatAttributes().withAutomatable (false)));

    static const char* lockNames[5] = { "Lock Amp", "Lock Filter", "Lock LFO", "Lock Pitch", "Lock Sliders" };
    static const bool  lockDefault[5] = { true, false, false, true, false };
    for (int g = 0; g < 5; ++g)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID (kLockIds[g], 1), lockNames[g], lockDefault[g],
                                                          AudioParameterBoolAttributes().withAutomatable (false)));

    return layout;
}

//==============================================================================
SH4dProcessor::SH4dProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SH4D_EDITOR", createLayout())
{
    std::fill (std::begin (ccToIndex), std::end (ccToIndex), -1);
    for (int i = 0; i < kNumCc; ++i)
        ccToIndex[kCc[i].cc] = i;

    for (int p = 0; p < kNumSynthParts; ++p)
    {
        for (int i = 0; i < kNumCc; ++i)
        {
            const auto id = paramId (p, kCc[i].id);
            ccParams[(size_t) p][(size_t) i] = apvts.getParameter (id);
            ccRaw[(size_t) p][(size_t) i]    = apvts.getRawParameterValue (id);
            lastSent[p][i] = kCc[i].kind == Kind::Switch ? (kCc[i].def > 0 ? 127 : 0) : kCc[i].def;
        }
    }

    for (int p = 0; p < kNumParts; ++p)
    {
        chParams[(size_t) p] = apvts.getParameter (channelId (p));
        chRaw[(size_t) p]    = apvts.getRawParameterValue (channelId (p));
    }

    thruRaw = apvts.getRawParameterValue ("ccThru");
}

void SH4dProcessor::prepareToPlay (double, int)
{
    scratch.ensureSize (8192);
}

bool SH4dProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::stereo() || out == AudioChannelSet::mono() || out.isDisabled();
}

int SH4dProcessor::getChannelForPart (int part) const
{
    return jlimit (1, 16, (int) std::lround (chRaw[(size_t) part]->load()));
}

int SH4dProcessor::getCcValue (int part, int ccIndex) const
{
    return toWire (kCc[ccIndex], ccRaw[(size_t) part][(size_t) ccIndex]->load());
}

//==============================================================================
void SH4dProcessor::noteTx (int type, int channel, int d1, int d2)
{
    lastTx.store (pack (type, channel, d1, d2));
    txCount.fetch_add (1);
}

void SH4dProcessor::noteRx (int type, int channel, int d1, int d2)
{
    lastRx.store (pack (type, channel, d1, d2));
    rxCount.fetch_add (1);
}

void SH4dProcessor::applyIncoming (int part, int ccIndex, int value)
{
    const auto& d = kCc[ccIndex];
    const int wire = d.kind == Kind::Switch ? (value >= 64 ? 127 : 0) : value;

    // Remember it as "already on the hardware" so it is not echoed back.
    lastSent[part][ccIndex] = wire;

    if (auto* param = ccParams[(size_t) part][(size_t) ccIndex])
    {
        const float norm = d.kind == Kind::Switch ? (wire > 0 ? 1.0f : 0.0f)
                                                  : param->convertTo0to1 ((float) wire);
        param->setValueNotifyingHost (norm);
    }
}

//==============================================================================
void SH4dProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    buffer.clear();

    int channel[kNumParts];
    for (int p = 0; p < kNumParts; ++p)
        channel[p] = getChannelForPart (p);

    const bool thru = thruRaw->load() > 0.5f;

    scratch.clear();

    // 1) Incoming MIDI. CCs that belong to a part's channel update the editor
    //    (this is how the hardware's panel moves show up when "Tx CC" is on).
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        bool consumed = false;

        if (msg.isController())
        {
            const int idx = ccToIndex[msg.getControllerNumber() & 127];

            if (idx >= 0)
            {
                for (int p = 0; p < kNumSynthParts; ++p)
                {
                    if (channel[p] == msg.getChannel())
                    {
                        applyIncoming (p, idx, msg.getControllerValue());
                        noteRx (0, msg.getChannel(), msg.getControllerNumber(), msg.getControllerValue());
                        consumed = ! thru;
                    }
                }
            }
        }

        if (! consumed)
            scratch.addEvent (msg, metadata.samplePosition);
    }

    // 2) Bank / program / panic messages queued by the UI
    {
        int s1, z1, s2, z2;
        fifo.prepareToRead (fifo.getNumReady(), s1, z1, s2, z2);

        auto emit = [this] (const OutMsg& m)
        {
            if (m.type == 0)
                scratch.addEvent (MidiMessage::controllerEvent (m.channel, m.data1, m.data2), 0);
            else
                scratch.addEvent (MidiMessage::programChange (m.channel, m.data1), 0);

            noteTx (m.type, m.channel, m.data1, m.data2);
        };

        for (int i = 0; i < z1; ++i) emit (fifoData[(size_t) (s1 + i)]);
        for (int i = 0; i < z2; ++i) emit (fifoData[(size_t) (s2 + i)]);
        fifo.finishedRead (z1 + z2);
    }

    // 3) "Send All": forget what we think the hardware holds
    if (sendAllRequested.exchange (false))
        for (int p = 0; p < kNumSynthParts; ++p)
            for (int i = 0; i < kNumCc; ++i)
                lastSent[p][i] = -1;

    // 4) Send whatever changed (UI, host automation, preset/project recall)
    {
        constexpr int total = kNumSynthParts * kNumCc;
        int budget = kMsgBudgetPerBlock;

        for (int n = 0; n < total && budget > 0; ++n)
        {
            const int flat = (scanStart + n) % total;
            const int p = flat / kNumCc;
            const int i = flat % kNumCc;
            const int cur = getCcValue (p, i);

            if (cur != lastSent[p][i])
            {
                scratch.addEvent (MidiMessage::controllerEvent (channel[p], kCc[i].cc, cur), 0);
                noteTx (0, channel[p], kCc[i].cc, cur);
                lastSent[p][i] = cur;
                --budget;
            }
        }

        scanStart = (scanStart + 1) % total;
    }

    midi.swapWith (scratch);
}

//==============================================================================
void SH4dProcessor::pushOut (const OutMsg* items, int count)
{
    int s1, z1, s2, z2;
    fifo.prepareToWrite (count, s1, z1, s2, z2);

    if (z1 + z2 < count)
    {
        fifo.finishedWrite (0);   // queue full: drop rather than block
        return;
    }

    int k = 0;
    for (int i = 0; i < z1; ++i) fifoData[(size_t) (s1 + i)] = items[k++];
    for (int i = 0; i < z2; ++i) fifoData[(size_t) (s2 + i)] = items[k++];
    fifo.finishedWrite (z1 + z2);
}

void SH4dProcessor::sendProgram (int part, int bankMsb, int bankLsb, int program)
{
    const auto ch = (uint8_t) getChannelForPart (part);

    const OutMsg msgs[3] = {
        { ch, 0, 0,  (uint8_t) jlimit (0, 127, bankMsb) },    // bank select MSB
        { ch, 0, 32, (uint8_t) jlimit (0, 127, bankLsb) },    // bank select LSB
        { ch, 1, (uint8_t) jlimit (0, 127, program), 0 }      // program change
    };

    pushOut (msgs, 3);
}

void SH4dProcessor::sendPanic (int part)
{
    const auto ch = (uint8_t) getChannelForPart (part);

    const OutMsg msgs[2] = {
        { ch, 0, 123, 0 },   // all notes off
        { ch, 0, 120, 0 }    // all sound off
    };

    pushOut (msgs, 2);
}

//==============================================================================
AudioProcessorEditor* SH4dProcessor::createEditor()
{
    return new SH4dEditor (*this);
}

void SH4dProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SH4dProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SH4dProcessor();
}
