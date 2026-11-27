// Headless checks of the editor's MIDI engine (no hardware, no audio device, no DAW).
#include "../Source/PluginProcessor.h"
#include "../Source/PatchLibrary.h"
#include <iostream>

using namespace juce;
using namespace sh4d;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; } } while (0)

static int idx (int cc) { for (int i = 0; i < kNumCc; ++i) if (kCc[i].cc == cc) return i; return -1; }

struct Rig
{
    SH4dProcessor p { false };
    std::vector<MidiMessage> sent;      // what would reach the hardware device
    double now = 1000.0;
    int maxPerTick = 0;

    Rig()
    {
        p.prepareToPlay (44100, 128);
        p.setOutputSink ([this] (const MidiMessage& m) { sent.push_back (m); });
    }

    void tick (int n = 1)
    {
        for (int i = 0; i < n; ++i)
        {
            const auto before = sent.size();
            now += 2.0;
            p.engineTick (now);
            maxPerTick = jmax (maxPerTick, (int) (sent.size() - before));
            p.pumpMessageThreadWork();
        }
    }

    std::vector<MidiMessage> take() { auto v = sent; sent.clear(); return v; }

    // run one audio block; returns what the DAW would receive on the plugin's MIDI output
    std::vector<MidiMessage> block (MidiBuffer in = {})
    {
        AudioBuffer<float> audio (2, 128);
        p.processBlock (audio, in);
        std::vector<MidiMessage> out;
        for (const auto m : in) out.push_back (m.getMessage());
        return out;
    }

    void setCc (int part, int cc, int v)
    {
        auto* prm = p.getCcParam (part, idx (cc));
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) v));
    }

    void setNorm (const char* id, float n) { p.apvts.getParameter (id)->setValueNotifyingHost (n); }
};

static int countCc (const std::vector<MidiMessage>& v, int cc)
{
    int n = 0;
    for (auto& m : v) if (m.isController() && m.getControllerNumber() == cc) ++n;
    return n;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;

    if (argc > 2 && String (argv[1]) == "--snapshot")
    {
        SH4dProcessor p (false);
        p.prepareToPlay (44100, 128);
        auto* c = p.getCcParam (0, idx (74)); c->setValueNotifyingHost (c->convertTo0to1 (90.0f));
        c = p.getCcParam (0, idx (71));       c->setValueNotifyingHost (c->convertTo0to1 (40.0f));
        p.addRoute ({ 8, 0, idx (74), 0.4f });
        p.addRoute ({ 0, 0, idx (79), 0.6f });
        p.storeScene (0);
        p.apvts.state.setProperty ("page", argc > 3 ? String (argv[3]).getIntValue() : 0, nullptr);
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
    { Rig r; r.tick (5); CHECK (r.take().empty()); }

    // 2. change -> exactly one CC on the part's channel; then silence
    {
        Rig r;
        r.setCc (0, 74, 100);
        r.tick();
        auto out = r.take();
        CHECK (out.size() == 1);
        CHECK (out[0].isController() && out[0].getChannel() == 1 && out[0].getControllerNumber() == 74 && out[0].getControllerValue() == 100);
        r.tick (3);
        CHECK (r.take().empty());
    }

    // 3. other part uses its own channel
    {
        Rig r;
        r.p.getChannelParam (1)->setValueNotifyingHost (r.p.getChannelParam (1)->convertTo0to1 (5.0f));
        r.setCc (1, 71, 33);
        r.tick();
        auto out = r.take();
        CHECK (out.size() == 1 && out[0].getChannel() == 5 && out[0].getControllerNumber() == 71);
    }

    // 4. switches go out as 0/127
    {
        Rig r;
        r.p.getCcParam (0, idx (64))->setValueNotifyingHost (1.0f);
        r.tick();
        auto out = r.take();
        CHECK (out.size() == 1 && out[0].getControllerNumber() == 64 && out[0].getControllerValue() == 127);
        r.p.getCcParam (0, idx (64))->setValueNotifyingHost (0.0f);
        r.tick();
        out = r.take();
        CHECK (out.size() == 1 && out[0].getControllerValue() == 0);
    }

    // 5. CC from the DAW follows the hardware: consumed, applied to the knob, not echoed
    {
        Rig r;
        MidiBuffer in;
        in.addEvent (MidiMessage::controllerEvent (1, 74, 33), 0);
        auto hostOut = r.block (in);
        CHECK (countCc (hostOut, 74) == 0);           // swallowed
        r.tick (3);
        CHECK (r.p.getCcValue (0, idx (74)) == 33);
        CHECK (r.take().empty());                     // nothing echoed back to the unit
        CHECK (r.p.rxCount.load() == 1);
    }

    // 5b. CC arriving from the unit itself (USB input) follows too, and is never forwarded to the device
    {
        Rig r;
        r.p.pushDeviceEvent (0, MidiMessage::controllerEvent (1, 74, 77));
        r.tick (3);
        CHECK (r.p.getCcValue (0, idx (74)) == 77);
        CHECK (r.take().empty());
    }

    // 6. thru: forwarded to the device once, knob follows, no duplicate
    {
        Rig r;
        r.setNorm ("ccThru", 1.0f);
        MidiBuffer in;
        in.addEvent (MidiMessage::controllerEvent (1, 74, 50), 0);
        auto hostOut = r.block (in);
        CHECK (countCc (hostOut, 74) == 1);
        r.tick (3);
        auto out = r.take();
        CHECK (countCc (out, 74) == 1);
        CHECK (r.p.getCcValue (0, idx (74)) == 50);
    }

    // 7. notes / unrelated messages pass through to both the DAW out and the device
    {
        Rig r;
        MidiBuffer in;
        in.addEvent (MidiMessage::noteOn (1, 60, (uint8_t) 100), 3);
        in.addEvent (MidiMessage::controllerEvent (1, 11, 9), 4);   // CC11 isn't in the map
        auto hostOut = r.block (in);
        CHECK (hostOut.size() == 2 && hostOut[0].isNoteOn() && hostOut[1].getControllerNumber() == 11);
        r.tick();
        auto out = r.take();
        CHECK (out.size() == 2 && out[0].isNoteOn());
    }

    // 7b. host MIDI out "Off": the DAW gets nothing, the device still does
    {
        Rig r;
        r.setNorm ("hostOut", 1.0f);       // choice index 2 = Off (3 items -> norm 1.0)
        MidiBuffer in;
        in.addEvent (MidiMessage::noteOn (1, 60, (uint8_t) 100), 0);
        auto hostOut = r.block (in);
        CHECK (hostOut.empty());
        r.tick();
        CHECK (r.take().size() == 1);
        r.setCc (0, 74, 5);
        r.tick();
        CHECK (r.block().empty());
    }

    // 8. Send All: every synth-part param goes out exactly once, spread over ticks
    {
        Rig r;
        r.p.requestSendAll();
        r.tick (40);
        CHECK ((int) r.take().size() == kNumSynthParts * kNumCc);
        CHECK (r.maxPerTick <= 16 && r.maxPerTick > 0);
    }

    // 9. bank/program: CC0, CC32, PC in order, on the right channel
    {
        Rig r;
        r.p.sendProgram (2, 87, 70, 12);
        r.tick();
        auto out = r.take();
        CHECK (out.size() == 3);
        CHECK (out[0].getControllerNumber() == 0  && out[0].getControllerValue() == 87 && out[0].getChannel() == 3);
        CHECK (out[1].getControllerNumber() == 32 && out[1].getControllerValue() == 70);
        CHECK (out[2].isProgramChange() && out[2].getProgramChangeNumber() == 12 && out[2].getChannel() == 3);
    }

    // 10. panic + audition notes
    {
        Rig r;
        r.p.sendPanic (0);
        r.p.sendNote (true, 1, 60, 100);
        r.p.sendNote (false, 1, 60, 0);
        r.p.sendPitchBend (1, 16383);
        r.p.sendPitchBend (1, 8192);
        r.tick();
        auto out = r.take();
        CHECK (out.size() == 6 && out[0].getControllerNumber() == 123 && out[1].getControllerNumber() == 120);
        CHECK (out[2].isNoteOn() && out[2].getChannel() == 2 && out[3].isNoteOff());
        CHECK (out[4].isPitchWheel() && out[4].getPitchWheelValue() == 16383 && out[5].getPitchWheelValue() == 8192);
    }

    // 11. echo guard: the unit repeating what we just sent must not fight the user
    {
        Rig r;
        r.setCc (0, 74, 100);
        r.tick();
        r.take();
        r.p.pushDeviceEvent (0, MidiMessage::controllerEvent (1, 74, 55));   // within 150 ms: an echo
        r.tick (2);
        CHECK (r.p.getCcValue (0, idx (74)) == 100);
        r.now += 300.0;
        r.p.pushDeviceEvent (0, MidiMessage::controllerEvent (1, 74, 55));   // later: a real knob turn
        r.tick (3);
        CHECK (r.p.getCcValue (0, idx (74)) == 55);
        CHECK (r.take().empty());
    }

    // 12. macro modulation: knob keeps its base value, the wire value moves
    {
        Rig r;
        r.p.addRoute ({ kSrcMacro1, 0, idx (90), 1.0f });
        r.setNorm ("macro1", 0.5f);
        r.tick (2);
        auto out = r.take();
        CHECK (countCc (out, 90) == 1);
        CHECK (r.p.getEffectiveValue (0, idx (90)) == 64);
        CHECK (r.p.getCcValue (0, idx (90)) == 0);           // base untouched
        r.setNorm ("macro1", 0.0f);
        r.tick (2);
        CHECK (r.p.getEffectiveValue (0, idx (90)) == 0);
        r.p.setRoutes ({});
        r.setNorm ("macro1", 1.0f);
        r.take();
        r.tick (3);
        CHECK (r.take().empty());                            // route removed: macro does nothing
    }

    // 13. LFO sweeps around the base value
    {
        Rig r;
        r.setCc (0, 71, 64);
        r.tick();
        r.take();
        r.p.addRoute ({ kSrcLfo1, 0, idx (71), 0.5f });
        auto* rate = r.p.apvts.getParameter ("lfo1_rate");
        rate->setValueNotifyingHost (rate->convertTo0to1 (2.0f));
        int lo = 127, hi = 0;
        for (int i = 0; i < 700; ++i)
        {
            r.tick();
            for (auto& m : r.take())
                if (m.getControllerNumber() == 71) { lo = jmin (lo, m.getControllerValue()); hi = jmax (hi, m.getControllerValue()); }
        }
        CHECK (lo < 64 - 40 && hi > 64 + 40);
    }

    // 13b. modulated parameters are not "followed" from the hardware
    {
        Rig r;
        r.p.addRoute ({ kSrcMacro1, 0, idx (90), 0.5f });
        r.p.pushDeviceEvent (0, MidiMessage::controllerEvent (1, 90, 99));
        r.tick (3);
        CHECK (r.p.getCcValue (0, idx (90)) == 0);
    }

    // 14. aftertouch / pitch bend from the unit (D-Motion) become sources and reach the DAW
    {
        Rig r;
        r.p.pushDeviceEvent (0, MidiMessage::channelPressureChange (1, 127));
        r.p.pushDeviceEvent (0, MidiMessage::pitchWheel (1, 16383));
        r.tick (2);
        CHECK (r.p.getSourceValue (kSrcAftertouch) > 0.99f);
        CHECK (r.p.getSourceValue (kSrcPitchBend) > 0.99f);
        auto hostOut = r.block();
        CHECK (hostOut.size() == 2);
        r.p.addRoute ({ kSrcAftertouch, 0, idx (74), -1.0f });
        r.tick (2);
        CHECK (r.p.getEffectiveValue (0, idx (74)) == 0);    // 127 - 127
    }

    // 15. MIDI learn from a controller, then drive a parameter
    {
        Rig r;
        const int target = r.p.getCcParamIndex (0, idx (74));
        r.p.setLearnTarget (target);
        r.p.pushDeviceEvent (1, MidiMessage::controllerEvent (1, 21, 10));
        r.tick (2);
        CHECK (r.p.hasLearn (target) && r.p.getLearnTarget() < 0);
        r.p.pushDeviceEvent (1, MidiMessage::controllerEvent (1, 21, 100));
        r.tick (2);
        CHECK (r.p.getCcValue (0, idx (74)) == 100);
        r.tick (2);
        CHECK (countCc (r.take(), 74) >= 1);
        // an unmapped controller message is forwarded to the selected part's channel
        r.p.setSelectedPart (1);
        r.p.pushDeviceEvent (1, MidiMessage::noteOn (7, 60, (uint8_t) 90));
        r.tick();
        auto out = r.take();
        CHECK (out.size() == 1 && out[0].isNoteOn() && out[0].getChannel() == 2);
        r.p.clearLearn (-1);
        CHECK (! r.p.hasLearn (target));
    }

    // 16. scenes and morph
    {
        Rig r;
        r.setCc (0, 74, 10);  r.p.storeScene (0);
        r.setCc (0, 74, 110); r.p.storeScene (1);
        CHECK (r.p.hasScene (0) && r.p.hasScene (1) && ! r.p.hasScene (2));
        r.p.morphScenes (0, 1, 0.5f);
        CHECK (r.p.getCcValue (0, idx (74)) == 60);
        r.p.recallScene (0);
        CHECK (r.p.getCcValue (0, idx (74)) == 10);
        r.p.setMorphScenes (0, 1);
        r.setNorm ("morph", 1.0f);
        r.tick();
        CHECK (r.p.getCcValue (0, idx (74)) == 110);
    }

    // 17. part helpers
    {
        Rig r;
        r.setCc (0, 74, 42);
        r.p.copyPart (0, 2);
        CHECK (r.p.getCcValue (2, idx (74)) == 42);
        r.p.resetPart (2);
        CHECK (r.p.getCcValue (2, idx (74)) == 127);
    }

    // 18. patch library round trip
    {
        auto tmp = File::getSpecialLocation (File::tempDirectory).getChildFile ("sh4d_test_patches");
        tmp.deleteRecursively();
        PatchLibrary::setFolderOverride (tmp);

        PatchLibrary::Values v {};
        for (int i = 0; i < kNumCc; ++i) v[(size_t) i] = (i * 5) % 128;

        File written;
        CHECK (PatchLibrary::save ("Test Pad", "Pads", v, 87, 65, 3, &written));
        auto list = PatchLibrary::scan();
        CHECK (list.size() == 1 && list[0].name == "Test Pad" && list[0].category == "Pads" && list[0].program == 3);

        PatchLibrary::Values w {};
        PatchInfo info;
        CHECK (PatchLibrary::load (written, w, info));
        CHECK (w == v && info.bankLsb == 65);
        CHECK (PatchLibrary::remove (written));
        CHECK (PatchLibrary::scan().isEmpty());
        tmp.deleteRecursively();
    }

    // 19. state round trip, including routes, scenes and learn; recalled values are pushed to the unit
    {
        Rig a;
        a.setCc (0, 74, 21);
        a.setCc (3, 88, 99);
        a.p.storeScene (2);
        a.p.addRoute ({ kSrcLfo1 + 1, 1, idx (74), -0.25f });
        a.p.setLearnTarget (a.p.getCcParamIndex (0, idx (71)));
        a.p.pushDeviceEvent (1, MidiMessage::controllerEvent (1, 30, 1));
        a.tick (2);
        MemoryBlock mb;
        a.p.getStateInformation (mb);

        Rig b;
        b.p.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (b.p.getCcValue (0, idx (74)) == 21);
        CHECK (b.p.getCcValue (3, idx (88)) == 99);
        CHECK (b.p.hasScene (2));
        auto routes = b.p.getRoutes();
        CHECK (routes.size() == 1 && routes[0].source == kSrcLfo1 + 1 && routes[0].part == 1 && routes[0].depth < -0.2f);
        CHECK (b.p.getLearnMaps().size() == 1 && b.p.getLearnMaps()[0].cc == 30);

        b.tick (3);
        auto out = b.take();
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
