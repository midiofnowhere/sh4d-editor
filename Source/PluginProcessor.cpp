#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;
using namespace sh4d;

namespace
{
    // Messages from the unit for a parameter we just sent are most likely its echo.
    constexpr double kEchoGuardMs = 150.0;

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

    String toCsv (const int* v, int n)
    {
        StringArray s;
        for (int i = 0; i < n; ++i)
            s.add (String (v[i]));
        return s.joinIntoString (",");
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

    // MIDI channel per part. Defaults are a guess - match them to the SH-4d.
    static const int defaultChannel[kNumParts] = { 1, 2, 3, 4, 10 };
    for (int p = 0; p < kNumParts; ++p)
    {
        const String name = p < kNumSynthParts ? "Part " + String (p + 1) + " Channel" : String ("Rhythm Channel");
        layout.add (std::make_unique<AudioParameterInt> (ParameterID (channelId (p), 1), name, 1, 16,
                                                         defaultChannel[p],
                                                         AudioParameterIntAttributes().withAutomatable (false)));
    }

    // Macros (automatable, learnable, usable as modulation sources)
    for (int m = 0; m < kNumMacros; ++m)
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID ("macro" + String (m + 1), 1), "Macro " + String (m + 1),
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.0f), 0.0f));

    // LFOs
    StringArray shapes, syncs;
    for (auto* s : kLfoShapes)    shapes.add (s);
    for (auto* s : kLfoSyncNames) syncs.add (s);

    for (int n = 0; n < kNumLfos; ++n)
    {
        const String t = "lfo" + String (n + 1) + "_";
        const String nm = "LFO " + String (n + 1) + " ";
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID (t + "rate", 1), nm + "Rate",
                                                           NormalisableRange<float> (0.02f, 30.0f, 0.0f, 0.4f), 1.0f));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID (t + "shape", 1), nm + "Shape", shapes, 0));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID (t + "sync", 1), nm + "Sync", syncs, 0));
    }

    // Scene morph (automatable)
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID ("morph", 1), "Scene Morph",
                                                       NormalisableRange<float> (0.0f, 1.0f, 0.0f), 0.0f));

    // Behaviour flags / randomiser settings (saved with the project, not automatable)
    const auto noAuto = AudioParameterBoolAttributes().withAutomatable (false);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID ("ccThru", 1), "Pass incoming CC through", false, noAuto));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID ("remapCtl", 1), "Remap controller to selected part", true, noAuto));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID ("sendOnConnect", 1), "Send All on connect", false, noAuto));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID ("hostOut", 1), "Host MIDI out",
                                                        StringArray { kHostOutModes[0], kHostOutModes[1], kHostOutModes[2] }, 0,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID ("randAmount", 1), "Random Amount",
                                                       NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
                                                       AudioParameterFloatAttributes().withAutomatable (false)));

    static const char* lockNames[5] = { "Lock Amp", "Lock Filter", "Lock LFO", "Lock Pitch", "Lock Sliders" };
    static const bool  lockDefault[5] = { true, false, false, true, false };
    for (int g = 0; g < 5; ++g)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID (kLockIds[g], 1), lockNames[g], lockDefault[g], noAuto));

    return layout;
}

//==============================================================================
SH4dProcessor::SH4dProcessor (bool startThreads)
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "SH4D_EDITOR", createLayout())
{
    std::fill (std::begin (ccToIndex), std::end (ccToIndex), -1);
    for (int i = 0; i < kNumCc; ++i)
        ccToIndex[kCc[i].cc] = i;

    for (auto* prm : getParameters())
    {
        if (auto* r = dynamic_cast<RangedAudioParameter*> (prm))
        {
            paramIndexById[r->paramID] = (int) allParams.size();
            allParams.push_back (r);
        }
    }

    for (int p = 0; p < kNumSynthParts; ++p)
    {
        for (int i = 0; i < kNumCc; ++i)
        {
            const auto id = paramId (p, kCc[i].id);
            ccParams[(size_t) p][(size_t) i]     = apvts.getParameter (id);
            ccRaw[(size_t) p][(size_t) i]        = apvts.getRawParameterValue (id);
            ccParamIndex[(size_t) p][(size_t) i] = paramIndexById[id];

            const int def = kCc[i].kind == Kind::Switch ? (kCc[i].def > 0 ? 127 : 0) : kCc[i].def;
            lastSent[p][i] = def;
            followVal[p][i] = -1;
            eff[p][i].store (def);
        }
    }

    for (int p = 0; p < kNumParts; ++p)
    {
        chParams[(size_t) p] = apvts.getParameter (channelId (p));
        chRaw[(size_t) p]    = apvts.getRawParameterValue (channelId (p));
    }

    for (int m = 0; m < kNumMacros; ++m)
        macroRaw[(size_t) m] = apvts.getRawParameterValue ("macro" + String (m + 1));

    for (int n = 0; n < kNumLfos; ++n)
    {
        const String t = "lfo" + String (n + 1) + "_";
        lfoRateRaw[(size_t) n]  = apvts.getRawParameterValue (t + "rate");
        lfoShapeRaw[(size_t) n] = apvts.getRawParameterValue (t + "shape");
        lfoSyncRaw[(size_t) n]  = apvts.getRawParameterValue (t + "sync");
    }

    thruRaw          = apvts.getRawParameterValue ("ccThru");
    hostOutRaw       = apvts.getRawParameterValue ("hostOut");
    remapRaw         = apvts.getRawParameterValue ("remapCtl");
    sendOnConnectRaw = apvts.getRawParameterValue ("sendOnConnect");
    morphRaw         = apvts.getRawParameterValue ("morph");
    lastMorph        = morphRaw->load();

    for (auto& s : srcValue)
        s.store (0.0f);

    loadModelFromState();
    selectedPart.store (jlimit (0, kNumParts - 1, (int) apvts.state.getProperty ("part", 0)));

    if (startThreads)
    {
        if (apvts.state.hasProperty ("hwIn") || apvts.state.hasProperty ("hwOut"))
            refreshMidiDevices();
        else
            autoDetectDevices();

        engineThread.startThread();
        startTimerHz (60);
        threadsRunning = true;
    }
}

SH4dProcessor::~SH4dProcessor()
{
    if (threadsRunning)
    {
        stopTimer();
        engineThread.stopThread (2000);
    }

    if (hwIn)  hwIn->stop();
    if (ctlIn) ctlIn->stop();
    hwIn.reset();
    ctlIn.reset();

    const ScopedLock sl (outLock);
    hwOut.reset();
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

RangedAudioParameter* SH4dProcessor::getParamByIndex (int index) const
{
    return (index >= 0 && index < (int) allParams.size()) ? allParams[(size_t) index] : nullptr;
}

int SH4dProcessor::getParamIndex (const String& id) const
{
    const auto it = paramIndexById.find (id);
    return it == paramIndexById.end() ? -1 : it->second;
}

//==============================================================================
// Part helpers
SH4dProcessor::PartValues SH4dProcessor::getPartValues (int part) const
{
    PartValues v {};
    for (int i = 0; i < kNumCc; ++i)
        v[(size_t) i] = getCcValue (part, i);
    return v;
}

void SH4dProcessor::setPartValues (int part, const PartValues& v)
{
    for (int i = 0; i < kNumCc; ++i)
    {
        auto* prm = ccParams[(size_t) part][(size_t) i];
        const int x = jlimit (0, 127, v[(size_t) i]);
        const float norm = kCc[i].kind == Kind::Switch ? (x >= 64 ? 1.0f : 0.0f) : prm->convertTo0to1 ((float) x);
        prm->setValueNotifyingHost (norm);
    }
}

void SH4dProcessor::copyPart (int from, int to)
{
    if (from != to)
        setPartValues (to, getPartValues (from));
}

void SH4dProcessor::resetPart (int part)
{
    for (int i = 0; i < kNumCc; ++i)
    {
        auto* prm = ccParams[(size_t) part][(size_t) i];
        prm->setValueNotifyingHost (prm->getDefaultValue());
    }
}

//==============================================================================
// Scenes
void SH4dProcessor::storeScene (int slot)
{
    slot = jlimit (0, kNumScenes - 1, slot);
    auto& d = sceneData[(size_t) slot];

    for (int p = 0; p < kNumSynthParts; ++p)
        for (int i = 0; i < kNumCc; ++i)
            d[(size_t) (p * kNumCc + i)] = getCcValue (p, i);

    sceneHas[(size_t) slot] = true;
    apvts.state.setProperty ("scene" + String (slot), toCsv (d.data(), (int) d.size()), nullptr);
}

void SH4dProcessor::recallScene (int slot)
{
    slot = jlimit (0, kNumScenes - 1, slot);

    if (! sceneHas[(size_t) slot])
        return;

    for (int p = 0; p < kNumSynthParts; ++p)
    {
        PartValues v {};
        for (int i = 0; i < kNumCc; ++i)
            v[(size_t) i] = sceneData[(size_t) slot][(size_t) (p * kNumCc + i)];
        setPartValues (p, v);
    }
}

void SH4dProcessor::morphScenes (int a, int b, float t)
{
    a = jlimit (0, kNumScenes - 1, a);
    b = jlimit (0, kNumScenes - 1, b);

    if (! sceneHas[(size_t) a] || ! sceneHas[(size_t) b])
        return;

    t = jlimit (0.0f, 1.0f, t);

    for (int p = 0; p < kNumSynthParts; ++p)
    {
        PartValues v {};
        for (int i = 0; i < kNumCc; ++i)
        {
            const int va = sceneData[(size_t) a][(size_t) (p * kNumCc + i)];
            const int vb = sceneData[(size_t) b][(size_t) (p * kNumCc + i)];
            v[(size_t) i] = kCc[i].kind == Kind::Switch ? (t < 0.5f ? va : vb)
                                                         : (int) std::lround ((float) va + (float) (vb - va) * t);
        }
        setPartValues (p, v);
    }
}

int  SH4dProcessor::getMorphA() const { return jlimit (0, kNumScenes - 1, (int) apvts.state.getProperty ("morphA", 0)); }
int  SH4dProcessor::getMorphB() const { return jlimit (0, kNumScenes - 1, (int) apvts.state.getProperty ("morphB", 1)); }

void SH4dProcessor::setMorphScenes (int a, int b)
{
    apvts.state.setProperty ("morphA", jlimit (0, kNumScenes - 1, a), nullptr);
    apvts.state.setProperty ("morphB", jlimit (0, kNumScenes - 1, b), nullptr);
}

//==============================================================================
// Routing model
std::vector<Route> SH4dProcessor::getRoutes() const { return routesModel; }

void SH4dProcessor::setRoutes (std::vector<Route> r)
{
    std::vector<Route> clean;

    for (auto x : r)
    {
        if (x.ccIndex < 0 || x.ccIndex >= kNumCc || kCc[x.ccIndex].kind != Kind::Knob)
            continue;

        x.source = jlimit (0, kNumSources - 1, x.source);
        x.part   = jlimit (0, kNumSynthParts - 1, x.part);
        x.depth  = jlimit (-1.0f, 1.0f, x.depth);

        if ((int) clean.size() < kMaxRoutes)
            clean.push_back (x);
    }

    routesModel = clean;
    writeRoutesToState (clean);
    publishRoutes (clean);
}

void SH4dProcessor::addRoute (const Route& r)
{
    auto v = routesModel;

    if ((int) v.size() < kMaxRoutes)
    {
        v.push_back (r);
        setRoutes (v);
    }
}

bool SH4dProcessor::isModulated (int part, int ccIndex) const
{
    for (const auto& r : routesModel)
        if (r.part == part && r.ccIndex == ccIndex)
            return true;
    return false;
}

void SH4dProcessor::publishRoutes (std::vector<Route> r)
{
    auto table = std::make_unique<RouteTable>();
    table->routes = std::move (r);

    for (const auto& x : table->routes)
        table->modulated[x.part][x.ccIndex] = true;

    routePtr.store (table.get());
    routeStore.push_back (std::move (table));

    // Readers hold a snapshot for microseconds, so keeping the last few dozen alive is plenty.
    if (routeStore.size() > 64)
        routeStore.erase (routeStore.begin(), routeStore.begin() + (long) (routeStore.size() - 64));

    routesVersion.fetch_add (1);
}

void SH4dProcessor::writeRoutesToState (const std::vector<Route>& r)
{
    auto node = apvts.state.getOrCreateChildWithName ("ROUTES", nullptr);
    node.removeAllChildren (nullptr);

    for (const auto& x : r)
    {
        ValueTree n ("R");
        n.setProperty ("src", x.source, nullptr);
        n.setProperty ("part", x.part, nullptr);
        n.setProperty ("cc", x.ccIndex, nullptr);
        n.setProperty ("depth", (double) x.depth, nullptr);
        node.appendChild (n, nullptr);
    }
}

//==============================================================================
// MIDI learn model
std::vector<LearnMap> SH4dProcessor::getLearnMaps() const { return learnModel; }

bool SH4dProcessor::hasLearn (int paramIndex) const
{
    for (const auto& m : learnModel)
        if (m.paramIndex == paramIndex)
            return true;
    return false;
}

void SH4dProcessor::clearLearn (int paramIndex)
{
    auto maps = learnModel;

    if (paramIndex < 0)
        maps.clear();
    else
        maps.erase (std::remove_if (maps.begin(), maps.end(), [paramIndex] (const LearnMap& m) { return m.paramIndex == paramIndex; }), maps.end());

    learnModel = maps;
    writeLearnToState (maps);
    publishLearn (maps);
}

void SH4dProcessor::removeLearnAt (int listIndex)
{
    if (listIndex < 0 || listIndex >= (int) learnModel.size())
        return;

    auto maps = learnModel;
    maps.erase (maps.begin() + listIndex);
    learnModel = maps;
    writeLearnToState (maps);
    publishLearn (maps);
}

void SH4dProcessor::publishLearn (std::vector<LearnMap> m)
{
    auto table = std::make_unique<LearnTable>();
    table->maps = std::move (m);
    learnPtr.store (table.get());
    learnStore.push_back (std::move (table));

    if (learnStore.size() > 64)
        learnStore.erase (learnStore.begin(), learnStore.begin() + (long) (learnStore.size() - 64));

    learnVersion.fetch_add (1);
}

void SH4dProcessor::writeLearnToState (const std::vector<LearnMap>& maps)
{
    auto node = apvts.state.getOrCreateChildWithName ("LEARN", nullptr);
    node.removeAllChildren (nullptr);

    for (const auto& m : maps)
    {
        if (auto* prm = getParamByIndex (m.paramIndex))
        {
            ValueTree n ("MAP");
            n.setProperty ("cc", m.cc, nullptr);
            n.setProperty ("ch", m.channel, nullptr);
            n.setProperty ("id", prm->paramID, nullptr);
            node.appendChild (n, nullptr);
        }
    }
}

void SH4dProcessor::loadModelFromState()
{
    std::vector<Route> routes;
    if (auto node = apvts.state.getChildWithName ("ROUTES"); node.isValid())
    {
        for (int i = 0; i < node.getNumChildren(); ++i)
        {
            auto n = node.getChild (i);
            Route r;
            r.source  = (int) n.getProperty ("src", 0);
            r.part    = (int) n.getProperty ("part", 0);
            r.ccIndex = (int) n.getProperty ("cc", 0);
            r.depth   = (float) (double) n.getProperty ("depth", 0.5);
            routes.push_back (r);
        }
    }

    std::vector<LearnMap> maps;
    if (auto node = apvts.state.getChildWithName ("LEARN"); node.isValid())
    {
        for (int i = 0; i < node.getNumChildren(); ++i)
        {
            auto n = node.getChild (i);
            const int idx = getParamIndex (n.getProperty ("id").toString());

            if (idx >= 0)
                maps.push_back ({ (int) n.getProperty ("cc", 0), (int) n.getProperty ("ch", 0), idx });
        }
    }

    learnModel = maps;
    publishLearn (maps);
    setRoutes (routes);      // also publishes

    for (int s = 0; s < kNumScenes; ++s)
    {
        sceneHas[(size_t) s] = false;
        const auto csv = StringArray::fromTokens (apvts.state.getProperty ("scene" + String (s)).toString(), ",", "");

        if (csv.size() == kNumSynthParts * kNumCc)
        {
            for (int k = 0; k < csv.size(); ++k)
                sceneData[(size_t) s][(size_t) k] = jlimit (0, 127, csv[k].getIntValue());
            sceneHas[(size_t) s] = true;
        }
    }
}

//==============================================================================
// MIDI devices
StringArray SH4dProcessor::getMidiInputNames() const
{
    StringArray n;
    for (const auto& d : MidiInput::getAvailableDevices())
        n.add (d.name);
    return n;
}

StringArray SH4dProcessor::getMidiOutputNames() const
{
    StringArray n;
    for (const auto& d : MidiOutput::getAvailableDevices())
        n.add (d.name);
    return n;
}

String SH4dProcessor::getHwInName() const  { return apvts.state.getProperty ("hwIn").toString(); }
String SH4dProcessor::getHwOutName() const { return apvts.state.getProperty ("hwOut").toString(); }
String SH4dProcessor::getCtlInName() const { return apvts.state.getProperty ("ctlIn").toString(); }

void SH4dProcessor::setHwIn (const String& name)  { apvts.state.setProperty ("hwIn", name, nullptr);  refreshMidiDevices(); }
void SH4dProcessor::setHwOut (const String& name) { apvts.state.setProperty ("hwOut", name, nullptr); refreshMidiDevices(); }
void SH4dProcessor::setCtlIn (const String& name) { apvts.state.setProperty ("ctlIn", name, nullptr); refreshMidiDevices(); }

void SH4dProcessor::autoDetectDevices()
{
    auto find = [] (const StringArray& names) -> String
    {
        for (const auto& n : names)
            if (n.containsIgnoreCase ("sh-4d") || n.containsIgnoreCase ("sh4d"))
                return n;
        return {};
    };

    apvts.state.setProperty ("hwIn",  find (getMidiInputNames()),  nullptr);
    apvts.state.setProperty ("hwOut", find (getMidiOutputNames()), nullptr);
    refreshMidiDevices();
}

void SH4dProcessor::openInput (std::unique_ptr<MidiInput>& port, InCallback& cb, std::atomic<bool>& flag, const String& wanted)
{
    String id;

    if (wanted.isNotEmpty())
        for (const auto& d : MidiInput::getAvailableDevices())
            if (d.name == wanted) { id = d.identifier; break; }

    if (id.isEmpty())
    {
        if (port != nullptr) { port->stop(); port.reset(); }
        flag.store (false);
        return;
    }

    if (port != nullptr && port->getIdentifier() == id)
        return;

    if (port != nullptr) { port->stop(); port.reset(); }

    port = MidiInput::openDevice (id, &cb);

    if (port != nullptr)
        port->start();

    flag.store (port != nullptr);
}

void SH4dProcessor::refreshMidiDevices()
{
    openInput (hwIn,  hwCallback,  hwInOpen,  getHwInName());
    openInput (ctlIn, ctlCallback, ctlInOpen, getCtlInName());

    const String wanted = getHwOutName();
    String id;

    if (wanted.isNotEmpty())
        for (const auto& d : MidiOutput::getAvailableDevices())
            if (d.name == wanted) { id = d.identifier; break; }

    bool justOpened = false;
    {
        const ScopedLock sl (outLock);

        if (id.isEmpty())
        {
            hwOut.reset();
            hwOutOpen.store (false);
        }
        else if (hwOut == nullptr || hwOut->getIdentifier() != id)
        {
            hwOut = MidiOutput::openDevice (id);
            hwOutOpen.store (hwOut != nullptr);
            justOpened = hwOut != nullptr;
        }
    }

    if (justOpened && sendOnConnectRaw->load() > 0.5f)
        requestSendAll();
}

void SH4dProcessor::pushDeviceEvent (int origin, const MidiMessage& m)
{
    const int len = m.getRawDataSize();
    const uint8_t* d = m.getRawData();

    if (len < 1 || len > 3 || d[0] >= 0xF0)
        return;   // ignore sysex, clock and other system messages

    InEvt e;
    e.b0 = d[0];
    e.b1 = len > 1 ? d[1] : 0;
    e.b2 = len > 2 ? d[2] : 0;
    e.len = (uint8_t) len;
    e.origin = (uint8_t) origin;

    const ScopedLock sl (devLock);
    devInFifo.push (e);
}

//==============================================================================
// UI -> engine
void SH4dProcessor::pushOut (const OutMsg& m)
{
    uiOutFifo.push (m);
}

void SH4dProcessor::sendProgram (int part, int bankMsb, int bankLsb, int program)
{
    const auto ch = (uint8_t) getChannelForPart (part);
    pushOut ({ ch, 0, 0,  (uint8_t) jlimit (0, 127, bankMsb) });
    pushOut ({ ch, 0, 32, (uint8_t) jlimit (0, 127, bankLsb) });
    pushOut ({ ch, 1, (uint8_t) jlimit (0, 127, program), 0 });
}

void SH4dProcessor::sendPanic (int part)
{
    const auto ch = (uint8_t) getChannelForPart (part);
    pushOut ({ ch, 0, 123, 0 });   // all notes off
    pushOut ({ ch, 0, 120, 0 });   // all sound off
}

void SH4dProcessor::sendNote (bool on, int part, int note, int velocity)
{
    pushOut ({ (uint8_t) getChannelForPart (part), (uint8_t) (on ? 2 : 3), (uint8_t) jlimit (0, 127, note), (uint8_t) jlimit (0, 127, velocity) });
}

void SH4dProcessor::sendPitchBend (int part, int value14)
{
    value14 = jlimit (0, 16383, value14);
    pushOut ({ (uint8_t) getChannelForPart (part), 4, (uint8_t) (value14 & 127), (uint8_t) (value14 >> 7) });
}

void SH4dProcessor::setSelectedPart (int part)
{
    part = jlimit (0, kNumParts - 1, part);
    selectedPart.store (part);
    apvts.state.setProperty ("part", part, nullptr);
}

//==============================================================================
// Monitor
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

//==============================================================================
// Output routing
bool SH4dProcessor::hostOutActive() const
{
    const int mode = (int) std::lround (hostOutRaw->load());

    if (mode == 1) return true;
    if (mode == 2) return false;
    return ! hwOutOpen.load();      // Auto: the DAW only gets the MIDI if no device is open
}

void SH4dProcessor::sendToDevice (const MidiMessage& m)
{
    {
        const ScopedLock sl (outLock);
        if (hwOut != nullptr)
            hwOut->sendMessageNow (m);
    }

    if (sink)
        sink (m);
}

void SH4dProcessor::sendToHostOut (const MidiMessage& m)
{
    if (! hostOutActive())
        return;

    const int len = m.getRawDataSize();

    if (len < 1 || len > 3)
        return;

    ShortMsg s;
    const uint8_t* d = m.getRawData();
    s.b0 = d[0];
    s.b1 = len > 1 ? d[1] : 0;
    s.b2 = len > 2 ? d[2] : 0;
    s.len = (uint8_t) len;
    hostOutFifo.push (s);
}

void SH4dProcessor::emit (const MidiMessage& m)
{
    sendToDevice (m);
    sendToHostOut (m);
}

void SH4dProcessor::emitOut (const OutMsg& m)
{
    switch (m.type)
    {
        case 0:  emit (MidiMessage::controllerEvent (m.channel, m.data1, m.data2)); break;
        case 1:  emit (MidiMessage::programChange (m.channel, m.data1)); break;
        case 2:  emit (MidiMessage::noteOn  (m.channel, m.data1, (uint8_t) m.data2)); break;
        case 4:  emit (MidiMessage::pitchWheel (m.channel, (int) m.data1 | ((int) m.data2 << 7))); break;
        default: emit (MidiMessage::noteOff (m.channel, m.data1, (uint8_t) m.data2)); break;
    }

    noteTx (m.type, m.channel, m.data1, m.data2);
}

MidiMessage SH4dProcessor::makeMsg (const uint8_t* b, int len)
{
    return MidiMessage (b, len, 0.0);
}

void SH4dProcessor::forwardController (MidiMessage m)
{
    if (remapRaw->load() > 0.5f && m.getRawDataSize() > 0)
        m.setChannel (partChannel (selectedPart.load()));

    emit (m);
}

//==============================================================================
// Input handling (engine thread)
const LearnMap* SH4dProcessor::findLearn (int cc, int channel) const
{
    if (const auto* t = learnPtr.load())
        for (const auto& m : t->maps)
            if (m.cc == cc && (m.channel == 0 || m.channel == channel))
                return &m;

    return nullptr;
}

bool SH4dProcessor::wouldConsume (const InEvt& e) const
{
    if ((e.b0 & 0xF0) != 0xB0 || e.b1 >= 120)
        return false;

    const int ch = (e.b0 & 0x0F) + 1;

    if (learnTarget.load() >= 0 || findLearn (e.b1, ch) != nullptr)
        return true;

    if (thruRaw->load() > 0.5f)
        return false;

    if (ccToIndex[e.b1 & 127] < 0)
        return false;

    for (int p = 0; p < kNumSynthParts; ++p)
        if (partChannel (p) == ch)
            return true;

    return false;
}

void SH4dProcessor::applyLearned (const LearnMap& m, int value)
{
    auto* prm = getParamByIndex (m.paramIndex);

    if (prm == nullptr)
        return;

    float norm = (float) value / 127.0f;

    if (prm->getNumSteps() == 2)
        norm = value >= 64 ? 1.0f : 0.0f;

    uiWriteFifo.push ({ m.paramIndex, norm });
}

void SH4dProcessor::followFromMidi (int p, int idx, int value, double nowMs)
{
    if (nowMs - lastSendMs[p][idx] < kEchoGuardMs)
        return;                                   // almost certainly the unit echoing what we just sent

    if (const auto* t = routePtr.load())
        if (t->modulated[p][idx])
            return;                               // modulated parameters are driven by us, not followed

    const auto& d = kCc[idx];
    const int wire = d.kind == Kind::Switch ? (value >= 64 ? 127 : 0) : value;

    // Treat it as already on the hardware, and hold that value until the editor catches up.
    lastSent[p][idx]  = wire;
    followVal[p][idx] = wire;
    followMs[p][idx]  = nowMs;

    auto* prm = ccParams[(size_t) p][(size_t) idx];
    const float norm = d.kind == Kind::Switch ? (wire > 0 ? 1.0f : 0.0f) : prm->convertTo0to1 ((float) wire);
    uiWriteFifo.push ({ ccParamIndex[(size_t) p][(size_t) idx], norm });
}

void SH4dProcessor::handleIncoming (const InEvt& e, double nowMs)
{
    const int status = e.b0 & 0xF0;
    const int ch = (e.b0 & 0x0F) + 1;
    const auto origin = (Origin) e.origin;
    const MidiMessage msg = makeMsg (&e.b0, e.len);   // b0,b1,b2 are contiguous

    if (status == 0xB0)
    {
        const int cc = e.b1, val = e.b2;

        if (origin != Origin::Hardware && cc < 120)
        {
            const int lt = learnTarget.load();

            if (lt >= 0)
            {
                learnFifo.push ({ cc, ch, lt });
                learnTarget.store (-1);
                return;
            }

            if (const auto* lm = findLearn (cc, ch))
            {
                applyLearned (*lm, val);
                return;
            }
        }

        if (origin == Origin::Controller)
        {
            forwardController (msg);
            return;
        }

        const int idx = ccToIndex[cc & 127];
        bool consumed = false;

        if (idx >= 0)
        {
            for (int p = 0; p < kNumSynthParts; ++p)
            {
                if (partChannel (p) == ch)
                {
                    followFromMidi (p, idx, val, nowMs);
                    consumed = true;
                }
            }

            if (consumed)
                noteRx (0, ch, cc, val);
        }

        if (consumed && thruRaw->load() < 0.5f)
            return;

        if (origin == Origin::Host) sendToDevice (msg);   // host-out pass-through happens in processBlock
        else                        sendToHostOut (msg);  // from the unit: only the DAW wants it
        return;
    }

    if (status == 0xD0)
        aftertouchIn = (float) e.b1 / 127.0f;
    else if (status == 0xE0)
        pitchBendIn = (float) (((int) e.b2 << 7 | (int) e.b1) - 8192) / 8192.0f;

    switch (origin)
    {
        case Origin::Hardware:   sendToHostOut (msg); break;
        case Origin::Host:       sendToDevice (msg);  break;
        case Origin::Controller: forwardController (msg); break;
    }
}

//==============================================================================
// Modulation sources (engine thread)
void SH4dProcessor::computeSources (double dtMs)
{
    for (int m = 0; m < kNumMacros; ++m)
        src[kSrcMacro1 + m] = macroRaw[(size_t) m]->load();

    const double bpm = hostBpm.load();

    for (int n = 0; n < kNumLfos; ++n)
    {
        auto& L = lfo[n];
        const int shape = jlimit (0, kNumLfoShapes - 1, (int) std::lround (lfoShapeRaw[(size_t) n]->load()));
        const int sync  = jlimit (0, kNumLfoSync - 1,   (int) std::lround (lfoSyncRaw[(size_t) n]->load()));

        double hz = (double) lfoRateRaw[(size_t) n]->load();

        if (sync > 0 && bpm > 1.0)
            hz = (bpm / 60.0) / kLfoSyncBeats[sync];

        L.phase += hz * dtMs * 0.001;

        if (L.phase >= 1.0)
        {
            L.phase -= std::floor (L.phase);
            L.prev = L.next;
            L.next = rng.nextFloat() * 2.0f - 1.0f;
        }

        const float ph = (float) L.phase;
        float v = 0.0f;

        switch (shape)
        {
            case 0:  v = std::sin (MathConstants<float>::twoPi * ph); break;
            case 1:  v = ph < 0.5f ? -1.0f + 4.0f * ph : 3.0f - 4.0f * ph; break;
            case 2:  v = 2.0f * ph - 1.0f; break;
            case 3:  v = 1.0f - 2.0f * ph; break;
            case 4:  v = ph < 0.5f ? 1.0f : -1.0f; break;
            case 5:  v = L.next; break;
            default: v = L.prev + (L.next - L.prev) * (ph * ph * (3.0f - 2.0f * ph)); break;
        }

        src[kSrcLfo1 + n] = v;
    }

    src[kSrcAftertouch] = aftertouchIn;
    src[kSrcPitchBend]  = pitchBendIn;

    for (int s = 0; s < kNumSources; ++s)
        srcValue[(size_t) s].store (src[s]);
}

//==============================================================================
void SH4dProcessor::engineTick (double nowMs)
{
    const double dtMs = lastTickMs > 0.0 ? jlimit (0.05, 100.0, nowMs - lastTickMs) : 2.0;
    lastTickMs = nowMs;

    // 1) incoming MIDI (DAW and devices)
    InEvt ev;
    while (hostInFifo.pop (ev)) handleIncoming (ev, nowMs);
    while (devInFifo.pop (ev))  handleIncoming (ev, nowMs);

    // 2) things the UI queued: bank/program, panic, audition notes
    OutMsg om;
    while (uiOutFifo.pop (om))
        emitOut (om);

    // 3) "Send All": forget what we think the hardware holds
    if (sendAllRequested.exchange (false))
        for (int p = 0; p < kNumSynthParts; ++p)
            for (int i = 0; i < kNumCc; ++i)
                lastSent[p][i] = -1;

    // 4) modulation
    computeSources (dtMs);

    float modSum[kNumSynthParts][kNumCc] = {};

    if (const auto* rt = routePtr.load())
        for (const auto& r : rt->routes)
            modSum[r.part][r.ccIndex] += src[r.source] * r.depth * 127.0f;

    // 5) effective values (base + modulation)
    for (int p = 0; p < kNumSynthParts; ++p)
    {
        for (int i = 0; i < kNumCc; ++i)
        {
            int base = getCcValue (p, i);
            int& fv = followVal[p][i];

            if (fv >= 0)
            {
                if (base == fv || nowMs - followMs[p][i] > 250.0)
                    fv = -1;
                else
                    base = fv;
            }

            int e = base;

            if (kCc[i].kind == Kind::Knob && std::abs (modSum[p][i]) > 1.0e-6f)
                e = jlimit (0, 127, (int) std::lround ((float) base + modSum[p][i]));

            eff[p][i].store (e);
        }
    }

    // 6) send whatever changed. A per-tick budget keeps a big "Send All" from flooding USB-MIDI.
    constexpr int total = kNumSynthParts * kNumCc;
    int budget = jlimit (16, 160, (int) (dtMs * 8.0));

    for (int n = 0; n < total && budget > 0; ++n)
    {
        const int flat = (scanStart + n) % total;
        const int p = flat / kNumCc;
        const int i = flat % kNumCc;
        const int cur = eff[p][i].load();

        if (cur != lastSent[p][i])
        {
            const int ch = partChannel (p);
            emit (MidiMessage::controllerEvent (ch, kCc[i].cc, cur));
            noteTx (0, ch, kCc[i].cc, cur);
            lastSent[p][i] = cur;
            lastSendMs[p][i] = nowMs;
            --budget;
        }
    }

    scanStart = (scanStart + 1) % total;
}

//==============================================================================
// Message thread (60 Hz)
void SH4dProcessor::pumpMessageThreadWork()
{
    UiWrite w;
    while (uiWriteFifo.pop (w))
        if (auto* prm = getParamByIndex (w.paramIndex))
            prm->setValueNotifyingHost (w.norm);

    LearnMap lm;
    bool learned = false;

    while (learnFifo.pop (lm))
    {
        auto maps = learnModel;
        maps.erase (std::remove_if (maps.begin(), maps.end(), [&lm] (const LearnMap& m)
                                    { return m.paramIndex == lm.paramIndex || (m.cc == lm.cc && m.channel == lm.channel); }),
                    maps.end());
        maps.push_back (lm);
        learnModel = maps;
        learned = true;
    }

    if (learned)
    {
        writeLearnToState (learnModel);
        publishLearn (learnModel);
    }

    const float m = morphRaw->load();

    if (std::abs (m - lastMorph) > 1.0e-4f)
    {
        lastMorph = m;
        morphScenes (getMorphA(), getMorphB(), m);
    }
}

//==============================================================================
void SH4dProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    buffer.clear();

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm);

    const bool hostOut = hostOutActive();
    scratch.clear();

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int len = msg.getRawDataSize();
        const uint8_t* d = msg.getRawData();

        if (len < 1 || len > 3 || d[0] >= 0xF0)
            continue;

        InEvt e;
        e.b0 = d[0];
        e.b1 = len > 1 ? d[1] : 0;
        e.b2 = len > 2 ? d[2] : 0;
        e.len = (uint8_t) len;
        e.origin = (uint8_t) Origin::Host;

        hostInFifo.push (e);

        // Sample-accurate pass-through of everything the editor doesn't swallow
        if (hostOut && ! wouldConsume (e))
            scratch.addEvent (msg, metadata.samplePosition);
    }

    ShortMsg sm;
    while (hostOutFifo.pop (sm))
        scratch.addEvent (makeMsg (&sm.b0, sm.len), 0);

    midi.swapWith (scratch);
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
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            loadModelFromState();
            selectedPart.store (jlimit (0, kNumParts - 1, (int) apvts.state.getProperty ("part", 0)));
            lastMorph = morphRaw->load();

            if (threadsRunning)
                refreshMidiDevices();
        }
    }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SH4dProcessor();
}
