// Headless checks of the MIDI behaviour (no hardware needed).
#include "../Source/PluginProcessor.h"
#include <iostream>

using namespace juce;
using namespace sh4d;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; } } while (0)

static int idx (int cc) { for (int i = 0; i < kNumCc; ++i) if (kCc[i].cc == cc) return i; return -1; }

static std::vector<MidiMessage> run (SH4dProcessor& p, MidiBuffer in = {})
{
    AudioBuffer<float> audio (2, 128);
    p.processBlock (audio, in);
    std::vector<MidiMessage> out;
    for (const auto m : in) out.push_back (m.getMessage());
    return out;
}

static void setCc (SH4dProcessor& p, int part, int cc, int v)
{
    auto* prm = p.getCcParam (part, idx (cc));
    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) v));
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;

    if (argc > 2 && String (argv[1]) == "--snapshot")
    {
        SH4dProcessor p;
        p.prepareToPlay (44100, 128);
        setCc (p, 0, 74, 90);
        setCc (p, 0, 71, 40);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        ed->setVisible (true);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        File f (argv[2]);
        f.deleteFile();
        FileOutputStream os (f);
        PNGImageFormat().writeImageToStream (img, os);
        std::cout << "snapshot written\n";
        return 0;
    }

    // 1. quiet at start
    {
        SH4dProcessor p;
        p.prepareToPlay (44100, 128);
        CHECK (run (p).empty());
    }

    // 2. change -> exactly one CC on the part's channel; then silence
    {
        SH4dProcessor p;
        p.prepareToPlay (44100, 128);
        setCc (p, 0, 74, 100);
        auto out = run (p);
        CHECK (out.size() == 1);
        CHECK (out[0].isController() && out[0].getChannel() == 1
               && out[0].getControllerNumber() == 74 && out[0].getControllerValue() == 100);
        CHECK (run (p).empty());
    }

    // 3. other part uses its own channel
    {
        SH4dProcessor p;
        p.prepareToPlay (44100, 128);
        p.getChannelParam (1)->setValueNotifyingHost (p.getChannelParam (1)->convertTo0to1 (5.0f));
        setCc (p, 1, 71, 33);
        auto out = run (p);
        CHECK (out.size() == 1 && out[0].getChannel() == 5 && out[0].getControllerNumber() == 71);
    }

    // 4. switches go out as 0/127
    {
        SH4dProcessor p;
        p.prepareToPlay (44100, 128);
        p.getCcParam (0, idx (64))->setValueNotifyingHost (1.0f);
        auto out = run (p);
        CHECK (out.size() == 1 && out[0].getControllerNumber() == 64 && out[0].getControllerValue() == 127);
        p.getCcParam (0, idx (64))->setValueNotifyingHost (0.0f);
        out = run (p);
        CHECK (out.size() == 1 && out[0].getControllerValue() == 0);
    }

    // 5. incoming CC follows the hardware, is consumed, and is not echoed
    {
        SH4dProcessor p;
        p.prepareToPlay (44100, 128);
        MidiBuffer in;
        in.addEvent (MidiMessage::controllerEvent (1, 74, 33), 0);
        auto out = run (p, in);
        CHECK (out.empty());
        CHECK (p.getCcValue (0, idx (74)) == 33);
        CHECK (run (p).empty());
        CHECK (p.rxCount.load() == 1);
    }

    // 6. thru: forwarded, still updates the knob, still no duplicate
    {
        SH4dProcessor p;
        p.prepareToPlay (44100, 128);
        p.apvts.getParameter ("ccThru")->setValueNotifyingHost (1.0f);
        MidiBuffer in;
        in.addEvent (MidiMessage::controllerEvent (1, 74, 50), 0);
        auto out = run (p, in);
        CHECK (out.size() == 1 && out[0].getControllerValue() == 50);
        CHECK (p.getCcValue (0, idx (74)) == 50);
        CHECK (run (p).empty());
    }

    // 7. notes / unrelated messages pass through untouched
    {
        SH4dProcessor p;
        p.prepareToPlay (44100, 128);
        MidiBuffer in;
        in.addEvent (MidiMessage::noteOn (1, 60, (uint8_t) 100), 3);
        in.addEvent (MidiMessage::controllerEvent (1, 11, 9), 4);   // CC11 isn't in the map
        auto out = run (p, in);
        CHECK (out.size() == 2 && out[0].isNoteOn() && out[1].getControllerNumber() == 11);
    }

    // 8. Send All: every synth-part param goes out exactly once, spread over blocks
    {
        SH4dProcessor p;
        p.prepareToPlay (44100, 128);
        p.requestSendAll();
        int total = 0, blocks = 0, maxPerBlock = 0;
        for (int b = 0; b < 40; ++b)
        {
            auto out = run (p);
            total += (int) out.size();
            maxPerBlock = jmax (maxPerBlock, (int) out.size());
            if (! out.empty()) ++blocks;
        }
        CHECK (total == kNumSynthParts * kNumCc);
        CHECK (maxPerBlock <= 16 && blocks > 1);
    }

    // 9. bank/program: CC0, CC32, PC in order, on the right channel
    {
        SH4dProcessor p;
        p.prepareToPlay (44100, 128);
        p.sendProgram (2, 87, 70, 12);
        auto out = run (p);
        CHECK (out.size() == 3);
        CHECK (out[0].getControllerNumber() == 0  && out[0].getControllerValue() == 87 && out[0].getChannel() == 3);
        CHECK (out[1].getControllerNumber() == 32 && out[1].getControllerValue() == 70);
        CHECK (out[2].isProgramChange() && out[2].getProgramChangeNumber() == 12 && out[2].getChannel() == 3);
    }

    // 10. panic
    {
        SH4dProcessor p;
        p.prepareToPlay (44100, 128);
        p.sendPanic (0);
        auto out = run (p);
        CHECK (out.size() == 2 && out[0].getControllerNumber() == 123 && out[1].getControllerNumber() == 120);
    }

    // 11. state round trip; recalled values are pushed to the hardware
    {
        SH4dProcessor a;
        a.prepareToPlay (44100, 128);
        setCc (a, 0, 74, 21);
        setCc (a, 3, 88, 99);
        MemoryBlock mb;
        a.getStateInformation (mb);

        SH4dProcessor b;
        b.prepareToPlay (44100, 128);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (b.getCcValue (0, idx (74)) == 21);
        CHECK (b.getCcValue (3, idx (88)) == 99);

        auto out = run (b);
        bool s74 = false, s88 = false;
        for (auto& m : out)
        {
            if (m.getControllerNumber() == 74 && m.getControllerValue() == 21 && m.getChannel() == 1) s74 = true;
            if (m.getControllerNumber() == 88 && m.getControllerValue() == 99 && m.getChannel() == 4) s88 = true;
        }
        CHECK (s74 && s88);
    }

    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
