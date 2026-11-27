#include "PluginEditor.h"

using namespace juce;
using namespace sh4d;

//==============================================================================
// helpers
namespace
{
    int indexOfCc (int cc)
    {
        for (int i = 0; i < kNumCc; ++i)
            if (kCc[i].cc == cc)
                return i;
        return 0;
    }

    String describe (uint32_t v)
    {
        if (v == 0)
            return "-";

        const int type = (int) (v >> 24);
        const int ch   = (int) ((v >> 16) & 0xff);
        const int d1   = (int) ((v >> 8) & 0xff);
        const int d2   = (int) (v & 0xff);

        if (type == 1) return "ch" + String (ch) + " PC " + String (d1);
        if (type == 2) return "ch" + String (ch) + " note on " + String (d1);
        if (type == 3) return "ch" + String (ch) + " note off " + String (d1);

        String name;
        for (int i = 0; i < kNumCc; ++i)
            if (kCc[i].cc == d1)
                name = kCc[i].name;

        return "ch" + String (ch) + " CC" + String (d1)
               + (name.isNotEmpty() ? " (" + name + ")" : String())
               + " = " + String (d2);
    }

    bool isRandomisable (const CcDef& d)
    {
        // Never randomise level / pan / mod wheel, and never the on/off switches.
        return d.kind == Kind::Knob && d.cc != 7 && d.cc != 10 && d.cc != 1;
    }

    void drawPanels (Graphics& g, const std::vector<PanelRect>& panels, bool synthOnlyHidden = false, const std::vector<bool>* synthOnly = nullptr)
    {
        for (size_t i = 0; i < panels.size(); ++i)
        {
            if (synthOnlyHidden && synthOnly != nullptr && (*synthOnly)[i])
                continue;

            const auto& p = panels[i];
            g.setColour (SH4dLookAndFeel::panel);
            g.fillRoundedRectangle (p.bounds.toFloat(), 8.0f);
            g.setColour (SH4dLookAndFeel::accent);
            g.setFont (FontOptions (13.0f, Font::bold));
            g.drawText (p.title.toUpperCase(), p.bounds.getX() + 12, p.bounds.getY() + 4, p.bounds.getWidth() - 24, 20,
                        Justification::centredLeft);
        }
    }

    void setupLabel (Component& parent, Label& l, const String& t, Justification j = Justification::centredLeft, float size = 13.0f)
    {
        l.setText (t, dontSendNotification);
        l.setJustificationType (j);
        l.setColour (Label::textColourId, SH4dLookAndFeel::textDim);
        l.setFont (FontOptions (size));
        parent.addAndMakeVisible (l);
    }

    int  getInt (SH4dProcessor& p, const String& key, int def) { return (int) p.apvts.state.getProperty (key, def); }
    void setInt (SH4dProcessor& p, const String& key, int v)   { p.apvts.state.setProperty (key, v, nullptr); }

    // Right-click menu shared by every knob: MIDI learn + modulation shortcuts.
    void showParamMenu (SH4dProcessor& proc, Component* target, int paramIndex, int part, int ccIndex)
    {
        PopupMenu m;
        const bool learning = proc.getLearnTarget() == paramIndex;
        m.addItem (1, learning ? "Cancel MIDI learn" : "MIDI learn  (then move a knob on your controller)");

        if (proc.hasLearn (paramIndex))
            m.addItem (2, "Clear MIDI learn");

        if (ccIndex >= 0 && kCc[ccIndex].kind == Kind::Knob)
        {
            m.addSeparator();
            PopupMenu sub;
            for (int s = 0; s < kNumSources; ++s)
                sub.addItem (100 + s, kSourceNames[s]);
            m.addSubMenu ("Modulate with", sub);

            if (proc.isModulated (part, ccIndex))
                m.addItem (3, "Remove modulation from this knob");
        }

        m.showMenuAsync (PopupMenu::Options().withTargetComponent (target),
                         [&proc, paramIndex, part, ccIndex, learning] (int result)
        {
            if (result == 1)
            {
                proc.setLearnTarget (learning ? -1 : paramIndex);
            }
            else if (result == 2)
            {
                proc.clearLearn (paramIndex);
            }
            else if (result == 3)
            {
                auto routes = proc.getRoutes();
                routes.erase (std::remove_if (routes.begin(), routes.end(), [part, ccIndex] (const Route& r)
                                              { return r.part == part && r.ccIndex == ccIndex; }), routes.end());
                proc.setRoutes (routes);
            }
            else if (result >= 100)
            {
                proc.addRoute ({ result - 100, part, ccIndex, 0.5f });
            }
        });
    }
}

void showParamContextMenu (SH4dProcessor& p, Component* target, int paramIndex, int part, int ccIndex)
{
    showParamMenu (p, target, paramIndex, part, ccIndex);
}

//==============================================================================
SH4dLookAndFeel::SH4dLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, bg);
    setColour (Slider::textBoxTextColourId, text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::thumbColourId, accent);
    setColour (Slider::trackColourId, accent);
    setColour (Slider::backgroundColourId, track);
    setColour (Label::textColourId, text);
    setColour (TextButton::buttonColourId, Colour (0xff2b313c));
    setColour (TextButton::buttonOnColourId, accent);
    setColour (TextButton::textColourOffId, text);
    setColour (TextButton::textColourOnId, Colours::black);
    setColour (ComboBox::backgroundColourId, Colour (0xff2b313c));
    setColour (ComboBox::textColourId, text);
    setColour (ComboBox::outlineColourId, track);
    setColour (ComboBox::arrowColourId, textDim);
    setColour (PopupMenu::backgroundColourId, panel);
    setColour (PopupMenu::textColourId, text);
    setColour (PopupMenu::highlightedBackgroundColourId, accent);
    setColour (PopupMenu::highlightedTextColourId, Colours::black);
    setColour (TextEditor::backgroundColourId, Colour (0xff101216));
    setColour (TextEditor::textColourId, text);
    setColour (TextEditor::outlineColourId, track);
    setColour (TextEditor::focusedOutlineColourId, accent);
    setColour (ListBox::backgroundColourId, Colour (0xff101216));
    setColour (ListBox::outlineColourId, track);
    setColour (ScrollBar::thumbColourId, track.brighter (0.3f));
    setColour (MidiKeyboardComponent::keyDownOverlayColourId, accent.withAlpha (0.8f));
    setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, accent.withAlpha (0.3f));
}

void SH4dLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, Slider& s)
{
    auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float r = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float arcR = r - 3.0f;

    Path trackPath;
    trackPath.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (track);
    g.strokePath (trackPath, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

    Path valuePath;
    valuePath.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
    g.setColour (s.isEnabled() ? accent : textDim);
    g.strokePath (valuePath, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // modulation ring: where the value actually is on the wire right now
    const auto effVar = s.getProperties()["eff"];
    if (effVar.isDouble())
    {
        const float e = (float) (double) effVar;

        if (e >= 0.0f && std::abs (e - pos) > 0.004f)
        {
            const float effAngle = startAngle + e * (endAngle - startAngle);
            Path ring;
            ring.addCentredArc (c.x, c.y, arcR + 3.5f, arcR + 3.5f, 0.0f, jmin (angle, effAngle), jmax (angle, effAngle), true);
            g.setColour (mod);
            g.strokePath (ring, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));

            Path tick;
            tick.addRoundedRectangle (-1.0f, -(arcR + 6.0f), 2.0f, 5.0f, 1.0f);
            g.fillPath (tick, AffineTransform::rotation (effAngle).translated (c.x, c.y));
        }
    }

    g.setColour (Colour (0xff2b313c));
    g.fillEllipse (Rectangle<float> (arcR * 1.4f, arcR * 1.4f).withCentre (c));

    Path pointer;
    pointer.addRoundedRectangle (-1.5f, -(arcR * 0.7f), 3.0f, arcR * 0.4f, 1.5f);
    g.setColour (text);
    g.fillPath (pointer, AffineTransform::rotation (angle).translated (c.x, c.y));
}

void SH4dLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();

    g.setColour (on ? accent : Colour (0xff2b313c));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? Colours::black : (highlighted ? text : textDim));
    g.setFont (13.0f);
    g.drawText (b.getButtonText(), r.toNearestInt(), Justification::centred);
}

//==============================================================================
KnobComponent::KnobComponent (const String& title)
{
    label.setText (title, dontSendNotification);
    label.setJustificationType (Justification::centred);
    label.setInterceptsMouseClicks (false, false);
    label.setColour (Label::textColourId, SH4dLookAndFeel::textDim);
    label.setFont (FontOptions (12.0f));

    slider.setSliderStyle (Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 54, 16);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setPopupMenuEnabled (false);
    slider.addMouseListener (this, false);

    addAndMakeVisible (label);
    addAndMakeVisible (slider);
}

void KnobComponent::attach (RangedAudioParameter& p, UndoManager* um)
{
    attachment.reset();
    attachment = std::make_unique<SliderParameterAttachment> (p, slider, um);
}

void KnobComponent::setLearning (bool on)
{
    if (on == learning)
        return;

    learning = on;
    label.setColour (Label::textColourId, on ? SH4dLookAndFeel::accent : SH4dLookAndFeel::textDim);
}

void KnobComponent::setEffective (float norm)
{
    const auto old = slider.getProperties()["eff"];
    const double v = (double) norm;

    if (! old.isDouble() || std::abs ((double) old - v) > 0.002)
    {
        slider.getProperties().set ("eff", v);
        slider.repaint();
    }
}

void KnobComponent::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

void KnobComponent::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onContext)
        onContext();
}

//==============================================================================
SwitchComponent::SwitchComponent (const String& title)
{
    button.setButtonText (title);
    button.setClickingTogglesState (true);
    button.addMouseListener (this, false);
    addAndMakeVisible (button);
}

void SwitchComponent::attach (RangedAudioParameter& p, UndoManager* um)
{
    attachment.reset();
    attachment = std::make_unique<ButtonParameterAttachment> (p, button, um);
}

void SwitchComponent::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onContext)
        onContext();
}

//==============================================================================
void XYPad::setPosition (float x, float y)
{
    if (dragging)
        return;

    if (! approximatelyEqual (x, px) || ! approximatelyEqual (y, py))
    {
        px = x;
        py = y;
        repaint();
    }
}

void XYPad::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff101216));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (SH4dLookAndFeel::track);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    g.setColour (SH4dLookAndFeel::track.withAlpha (0.5f));
    g.drawLine (r.getCentreX(), r.getY(), r.getCentreX(), r.getBottom());
    g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY());

    const float cx = r.getX() + px * r.getWidth();
    const float cy = r.getBottom() - py * r.getHeight();

    g.setColour (SH4dLookAndFeel::accent.withAlpha (0.35f));
    g.drawLine (cx, r.getY(), cx, r.getBottom());
    g.drawLine (r.getX(), cy, r.getRight(), cy);
    g.setColour (SH4dLookAndFeel::accent);
    g.fillEllipse (cx - 6.0f, cy - 6.0f, 12.0f, 12.0f);
}

void XYPad::update (const MouseEvent& e)
{
    px = jlimit (0.0f, 1.0f, (float) e.x / (float) jmax (1, getWidth()));
    py = 1.0f - jlimit (0.0f, 1.0f, (float) e.y / (float) jmax (1, getHeight()));
    repaint();

    if (onMove)
        onMove (px, py);
}

void XYPad::mouseDown (const MouseEvent& e)
{
    dragging = true;
    if (onGesture) onGesture (true);
    update (e);
}

void XYPad::mouseDrag (const MouseEvent& e) { update (e); }

void XYPad::mouseUp (const MouseEvent&)
{
    dragging = false;
    if (onGesture) onGesture (false);
}

//==============================================================================
void EnvelopeView::setParams (RangedAudioParameter* a, RangedAudioParameter* d, RangedAudioParameter* s, RangedAudioParameter* r)
{
    prm = { a, d, s, r };
    repaint();
}

EnvelopeView::Geo EnvelopeView::geometry() const
{
    Geo g;
    g.area = getLocalBounds().toFloat().reduced (8.0f, 8.0f);
    const float W = g.area.getWidth(), H = g.area.getHeight();
    g.unit = 0.27f * W;

    const float aw = (0.04f + 0.96f * val (0)) * g.unit;
    const float dw = (0.04f + 0.96f * val (1)) * g.unit;
    const float rw = (0.04f + 0.96f * val (3)) * g.unit;
    const float hold = 0.10f * W;

    g.p0 = { g.area.getX(), g.area.getBottom() };
    g.p1 = { g.p0.x + aw, g.area.getY() };
    g.p2 = { g.p1.x + dw, g.area.getBottom() - val (2) * H };
    g.p3 = { g.p2.x + hold, g.p2.y };
    g.p4 = { g.p3.x + rw, g.area.getBottom() };
    return g;
}

void EnvelopeView::paint (Graphics& g)
{
    if (prm[0] == nullptr)
        return;

    const auto geo = geometry();

    g.setColour (Colour (0xff101216));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    Path fill;
    fill.startNewSubPath (geo.p0);
    fill.lineTo (geo.p1);
    fill.lineTo (geo.p2);
    fill.lineTo (geo.p3);
    fill.lineTo (geo.p4);
    fill.closeSubPath();
    g.setColour (SH4dLookAndFeel::accent.withAlpha (0.18f));
    g.fillPath (fill);

    Path line;
    line.startNewSubPath (geo.p0);
    line.lineTo (geo.p1);
    line.lineTo (geo.p2);
    line.lineTo (geo.p3);
    line.lineTo (geo.p4);
    g.setColour (SH4dLookAndFeel::accent);
    g.strokePath (line, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

    for (auto pt : { geo.p1, geo.p2, geo.p4 })
    {
        g.setColour (SH4dLookAndFeel::text);
        g.fillEllipse (pt.x - 5.0f, pt.y - 5.0f, 10.0f, 10.0f);
    }
}

void EnvelopeView::setNorm (int k, float n)
{
    if (prm[(size_t) k] != nullptr)
        prm[(size_t) k]->setValueNotifyingHost (jlimit (0.0f, 1.0f, n));
}

void EnvelopeView::mouseDown (const MouseEvent& e)
{
    if (prm[0] == nullptr)
        return;

    const auto geo = geometry();
    const auto pos = e.position;
    const std::array<Point<float>, 3> handles { geo.p1, geo.p2, geo.p4 };
    float best = 18.0f;
    dragging = -1;

    for (int k = 0; k < 3; ++k)
    {
        const float d = pos.getDistanceFrom (handles[(size_t) k]);
        if (d < best) { best = d; dragging = k; }
    }

    if (dragging < 0)
        return;

    if (dragging == 0)      prm[0]->beginChangeGesture();
    else if (dragging == 1) { prm[1]->beginChangeGesture(); prm[2]->beginChangeGesture(); }
    else                    prm[3]->beginChangeGesture();

    mouseDrag (e);
}

void EnvelopeView::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0)
        return;

    const auto geo = geometry();
    const float W = geo.area.getWidth(), H = geo.area.getHeight();
    auto fromWidth = [&geo] (float w) { return ((w / geo.unit) - 0.04f) / 0.96f; };

    if (dragging == 0)
    {
        setNorm (0, fromWidth (e.position.x - geo.p0.x));
    }
    else if (dragging == 1)
    {
        setNorm (1, fromWidth (e.position.x - geo.p1.x));
        setNorm (2, (geo.area.getBottom() - e.position.y) / jmax (1.0f, H));
    }
    else
    {
        setNorm (3, fromWidth (e.position.x - geo.p3.x));
    }

    juce::ignoreUnused (W);
    repaint();
}

void EnvelopeView::mouseUp (const MouseEvent&)
{
    if (dragging == 0)      prm[0]->endChangeGesture();
    else if (dragging == 1) { prm[1]->endChangeGesture(); prm[2]->endChangeGesture(); }
    else if (dragging == 2) prm[3]->endChangeGesture();

    dragging = -1;
}

//==============================================================================
void MeterBar::setValue (float v, bool bipolar)
{
    if (! approximatelyEqual (v, value) || bipolar != bi)
    {
        value = v;
        bi = bipolar;
        repaint();
    }
}

void MeterBar::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff101216));
    g.fillRoundedRectangle (r, 3.0f);

    g.setColour (SH4dLookAndFeel::mod);

    if (bi)
    {
        const float c = r.getCentreX();
        const float x = c + jlimit (-1.0f, 1.0f, value) * r.getWidth() * 0.5f;
        g.fillRect (Rectangle<float> (jmin (c, x), r.getY() + 2.0f, std::abs (x - c), r.getHeight() - 4.0f));
        g.setColour (SH4dLookAndFeel::track);
        g.fillRect (c - 0.5f, r.getY(), 1.0f, r.getHeight());
    }
    else
    {
        g.fillRect (Rectangle<float> (r.getX() + 2.0f, r.getY() + 2.0f,
                                      (r.getWidth() - 4.0f) * jlimit (0.0f, 1.0f, value), r.getHeight() - 4.0f));
    }
}

//==============================================================================
// SOUND PAGE
SoundPage::SoundPage (SH4dProcessor& p) : proc (p)
{
    for (int i = 0; i < kNumCc; ++i)
    {
        if (kCc[i].kind == Kind::Knob)
        {
            knobs[(size_t) i] = std::make_unique<KnobComponent> (kCc[i].shortName);
            knobs[(size_t) i]->onContext = [this, i] { showParamMenu (proc, knobs[(size_t) i].get(), proc.getCcParamIndex (part, i), part, i); };
            addAndMakeVisible (*knobs[(size_t) i]);
        }
        else
        {
            switches[(size_t) i] = std::make_unique<SwitchComponent> (kCc[i].shortName);
            switches[(size_t) i]->onContext = [this, i] { showParamMenu (proc, switches[(size_t) i].get(), proc.getCcParamIndex (part, i), part, -1); };
            addAndMakeVisible (*switches[(size_t) i]);
        }
    }

    addAndMakeVisible (filterEnv);
    addAndMakeVisible (ampEnv);

    // program select
    setupLabel (*this, msbLabel, "Bank MSB");
    setupLabel (*this, lsbLabel, "Bank LSB");
    setupLabel (*this, pcLabel,  "Program");

    for (auto* s : { &msbSlider, &lsbSlider, &pcSlider })
    {
        s->setSliderStyle (Slider::IncDecButtons);
        s->setTextBoxStyle (Slider::TextBoxLeft, false, 44, 24);
        s->setRange (0.0, 127.0, 1.0);
        addAndMakeVisible (*s);
    }

    msbSlider.onValueChange = [this] { setInt (proc, "msb" + String (part), (int) msbSlider.getValue()); };
    lsbSlider.onValueChange = [this] { setInt (proc, "lsb" + String (part), (int) lsbSlider.getValue()); };
    pcSlider.onValueChange  = [this] { setInt (proc, "pc"  + String (part), (int) pcSlider.getValue()); };

    sendProgButton.setButtonText ("Send");
    sendProgButton.onClick = [this] { sendCurrentProgram(); };
    prevButton.setButtonText ("<");
    prevButton.onClick = [this] { stepProgram (-1); };
    nextButton.setButtonText (">");
    nextButton.onClick = [this] { stepProgram (+1); };
    addAndMakeVisible (sendProgButton);
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);

    // randomiser
    randomButton.setButtonText ("Randomise");
    randomButton.onClick = [this] { randomise(); };
    resetButton.setButtonText ("Reset part");
    resetButton.onClick = [this] { proc.undoManager.beginNewTransaction ("Reset part"); proc.resetPart (part); };
    addAndMakeVisible (randomButton);
    addAndMakeVisible (resetButton);

    setupLabel (*this, amountLabel, "Amount");
    amountSlider.setSliderStyle (Slider::LinearHorizontal);
    amountSlider.setTextBoxStyle (Slider::TextBoxRight, false, 40, 24);
    addAndMakeVisible (amountSlider);
    amountAttachment = std::make_unique<SliderParameterAttachment> (*proc.apvts.getParameter ("randAmount"), amountSlider, nullptr);

    setupLabel (*this, lockCaption, "Locked groups are left alone:", Justification::centredLeft, 12.0f);
    static const char* lockText[5] = { "Amp", "Filter", "LFO", "Pitch", "Sliders" };
    for (size_t g = 0; g < 5; ++g)
    {
        lockButtons[g].setButtonText (lockText[g]);
        addAndMakeVisible (lockButtons[g]);
        lockAttachments[g] = std::make_unique<ButtonParameterAttachment> (*proc.apvts.getParameter (kLockIds[g]), lockButtons[g], nullptr);
    }

    setupLabel (*this, rhythmNote, "The rhythm part has no CC parameters on the SH-4d.\nUse Program Select below to pick a kit.",
                Justification::centred, 16.0f);

    partChanged();
}

SoundPage::~SoundPage() {}

void SoundPage::partChanged()
{
    part = proc.getSelectedPart();
    const bool synth = part < kNumSynthParts;

    for (int i = 0; i < kNumCc; ++i)
    {
        if (auto* k = knobs[(size_t) i].get())
        {
            if (synth) k->attach (*proc.getCcParam (part, i), &proc.undoManager); else k->detach();
            k->setVisible (synth);
        }

        if (auto* s = switches[(size_t) i].get())
        {
            if (synth) s->attach (*proc.getCcParam (part, i), &proc.undoManager); else s->detach();
            s->setVisible (synth);
        }
    }

    if (synth)
    {
        filterEnv.setParams (proc.getCcParam (part, indexOfCc (82)), proc.getCcParam (part, indexOfCc (83)),
                             proc.getCcParam (part, indexOfCc (28)), proc.getCcParam (part, indexOfCc (29)));
        ampEnv.setParams (proc.getCcParam (part, indexOfCc (73)), proc.getCcParam (part, indexOfCc (75)),
                          proc.getCcParam (part, indexOfCc (31)), proc.getCcParam (part, indexOfCc (72)));
    }
    else
    {
        filterEnv.setParams (nullptr, nullptr, nullptr, nullptr);
        ampEnv.setParams (nullptr, nullptr, nullptr, nullptr);
    }

    for (Component* c : std::initializer_list<Component*> { &filterEnv, &ampEnv, &randomButton, &resetButton,
                                                            &amountLabel, &amountSlider, &lockCaption })
        c->setVisible (synth);

    for (auto& b : lockButtons)
        b.setVisible (synth);

    rhythmNote.setVisible (! synth);
    loadProgramFields();
    repaint();
}

void SoundPage::tick()
{
    if (part < kNumSynthParts)
    {
        for (int i = 0; i < kNumCc; ++i)
        {
            if (auto* k = knobs[(size_t) i].get())
            {
                k->setEffective (proc.isModulated (part, i) ? (float) proc.getEffectiveValue (part, i) / 127.0f : -1.0f);
                k->setLearning (proc.getLearnTarget() == proc.getCcParamIndex (part, i));
            }
        }

        filterEnv.repaint();
        ampEnv.repaint();
    }

    // keep the program fields in sync if a patch or project load changed them
    const int msb = getInt (proc, "msb" + String (part), part < kNumSynthParts ? kToneBankMsb : kRhythmBankMsb);
    const int lsb = getInt (proc, "lsb" + String (part), part < kNumSynthParts ? kToneBankLsb : kRhythmBankLsb);
    const int pc  = getInt (proc, "pc"  + String (part), 0);

    if ((int) msbSlider.getValue() != msb) msbSlider.setValue (msb, dontSendNotification);
    if ((int) lsbSlider.getValue() != lsb) lsbSlider.setValue (lsb, dontSendNotification);
    if ((int) pcSlider.getValue()  != pc)  pcSlider.setValue (pc, dontSendNotification);
}

void SoundPage::loadProgramFields()
{
    const bool synth = part < kNumSynthParts;
    msbSlider.setValue (getInt (proc, "msb" + String (part), synth ? kToneBankMsb : kRhythmBankMsb), dontSendNotification);
    lsbSlider.setValue (getInt (proc, "lsb" + String (part), synth ? kToneBankLsb : kRhythmBankLsb), dontSendNotification);
    pcSlider.setValue  (getInt (proc, "pc"  + String (part), 0), dontSendNotification);
}

void SoundPage::sendCurrentProgram()
{
    proc.sendProgram (part, (int) msbSlider.getValue(), (int) lsbSlider.getValue(), (int) pcSlider.getValue());
}

void SoundPage::stepProgram (int delta)
{
    const int pc = ((int) pcSlider.getValue() + delta + 128) % 128;
    pcSlider.setValue (pc, sendNotification);
    sendCurrentProgram();
}

void SoundPage::randomise()
{
    if (part >= kNumSynthParts)
        return;

    auto& rnd = Random::getSystemRandom();
    const float amount = proc.apvts.getRawParameterValue ("randAmount")->load() / 100.0f;

    proc.undoManager.beginNewTransaction ("Randomise");

    for (int i = 0; i < kNumCc; ++i)
    {
        const auto& d = kCc[i];

        if (! isRandomisable (d))
            continue;

        if (proc.apvts.getRawParameterValue (kLockIds[(int) d.group])->load() > 0.5f)
            continue;

        auto* prm = proc.getCcParam (part, i);
        const int cur = proc.getCcValue (part, i);

        int target;
        if (amount >= 0.999f)
            target = rnd.nextInt (128);
        else
            target = jlimit (0, 127, (int) std::lround ((float) cur + (rnd.nextFloat() * 2.0f - 1.0f) * amount * 127.0f));

        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) target));
        prm->endChangeGesture();
    }
}

void SoundPage::placeGroup (Group g, Rectangle<int> panelBounds, int columns, int reserveBottom)
{
    std::vector<int> idx;
    for (int i = 0; i < kNumCc; ++i)
        if (kCc[i].group == g && kCc[i].kind == Kind::Knob)
            idx.push_back (i);

    auto area = panelBounds.reduced (10).withTrimmedTop (22).withTrimmedBottom (reserveBottom);
    const int n = (int) idx.size();
    const int rows = (n + columns - 1) / columns;
    const int cw = area.getWidth() / columns;
    const int ch = area.getHeight() / jmax (1, rows);

    for (int k = 0; k < n; ++k)
        knobs[(size_t) idx[(size_t) k]]->setBounds (area.getX() + (k % columns) * cw,
                                                    area.getY() + (k / columns) * ch, cw, ch);
}

void SoundPage::resized()
{
    const Rectangle<int> filterR (12, 0, 606, 240), ampR (626, 0, 382, 240),
                         lfoR (12, 248, 356, 164), pitchR (376, 248, 324, 164), sliderR (708, 248, 300, 164),
                         fenvR (12, 420, 250, 192), aenvR (270, 420, 250, 192), progR (528, 420, 250, 192), randR (786, 420, 222, 192);

    panels.clear();
    panels.push_back ({ filterR, "Filter" });
    panels.push_back ({ ampR,    "Amp" });
    panels.push_back ({ lfoR,    "LFO" });
    panels.push_back ({ pitchR,  "Pitch / Performance" });
    panels.push_back ({ sliderR, "Sliders" });
    panels.push_back ({ fenvR,   "Filter Envelope" });
    panels.push_back ({ aenvR,   "Amp Envelope" });
    panels.push_back ({ progR,   "Program Select" });
    panels.push_back ({ randR,   "Randomiser" });

    placeGroup (Group::Filter, filterR, 5);
    placeGroup (Group::Amp,    ampR,    3);
    placeGroup (Group::Lfo,    lfoR,    5);
    placeGroup (Group::Pitch,  pitchR,  3, 36);
    placeGroup (Group::Slider, sliderR, 4);

    {
        auto strip = pitchR.reduced (10).removeFromBottom (34);
        int n = 0;
        for (int i = 0; i < kNumCc; ++i)
            if (auto* s = switches[(size_t) i].get())
                s->setBounds (strip.getX() + (n++) * (strip.getWidth() / 3), strip.getY(), strip.getWidth() / 3, strip.getHeight());
    }

    filterEnv.setBounds (fenvR.reduced (10).withTrimmedTop (22));
    ampEnv.setBounds (aenvR.reduced (10).withTrimmedTop (22));
    rhythmNote.setBounds (12, 0, 996, 412);

    const int px = progR.getX() + 12;
    int py = progR.getY() + 30;
    msbLabel.setBounds (px, py, 66, 24);  msbSlider.setBounds (px + 68, py, 150, 24);  py += 30;
    lsbLabel.setBounds (px, py, 66, 24);  lsbSlider.setBounds (px + 68, py, 150, 24);  py += 30;
    pcLabel.setBounds  (px, py, 66, 24);  pcSlider.setBounds  (px + 68, py, 150, 24);  py += 36;
    sendProgButton.setBounds (px, py, 90, 28);
    prevButton.setBounds (px + 98, py, 40, 28);
    nextButton.setBounds (px + 144, py, 40, 28);

    const int rx = randR.getX() + 12;
    const int ry = randR.getY() + 28;
    randomButton.setBounds (rx, ry, 98, 26);
    resetButton.setBounds (rx + 102, ry, 98, 26);
    amountLabel.setBounds (rx, ry + 34, 56, 24);
    amountSlider.setBounds (rx + 58, ry + 34, 142, 24);
    lockCaption.setBounds (rx, ry + 66, 200, 18);
    for (int g = 0; g < 5; ++g)
        lockButtons[(size_t) g].setBounds (rx + (g % 3) * 67, ry + 88 + (g / 3) * 28, 63, 24);
}

void SoundPage::paint (Graphics& g)
{
    if (part < kNumSynthParts)
    {
        drawPanels (g, panels);
    }
    else
    {
        std::vector<PanelRect> only;
        if (panels.size() > 7)
            only.push_back (panels[7]);          // program select stays
        drawPanels (g, only);
    }
}

//==============================================================================
// MOD PAGE
RouteRow::RouteRow (const Route& r, std::function<void (RouteRow&)> onChange, std::function<void (RouteRow&)> onRemove)
    : changed (std::move (onChange))
{
    for (int s = 0; s < kNumSources; ++s)
        srcBox.addItem (kSourceNames[s], s + 1);

    for (int p = 0; p < kNumSynthParts; ++p)
        partBox.addItem ("P" + String (p + 1), p + 1);

    for (int i = 0; i < kNumCc; ++i)
        if (kCc[i].kind == Kind::Knob)
            paramBox.addItem (kCc[i].name, i + 1);

    srcBox.setSelectedId (r.source + 1, dontSendNotification);
    partBox.setSelectedId (r.part + 1, dontSendNotification);
    paramBox.setSelectedId (r.ccIndex + 1, dontSendNotification);

    depth.setSliderStyle (Slider::LinearHorizontal);
    depth.setRange (-100.0, 100.0, 1.0);
    depth.setTextBoxStyle (Slider::TextBoxRight, false, 52, 22);
    depth.setTextValueSuffix ("%");
    depth.setValue ((double) r.depth * 100.0, dontSendNotification);
    depth.setDoubleClickReturnValue (true, 50.0);

    removeButton.setButtonText ("x");
    removeButton.setTooltip ("Remove this routing");

    srcBox.onChange   = [this] { changed (*this); };
    partBox.onChange  = [this] { changed (*this); };
    paramBox.onChange = [this] { changed (*this); };
    depth.onValueChange = [this] { changed (*this); };
    removeButton.onClick = [this, onRemove] { onRemove (*this); };

    for (Component* c : std::initializer_list<Component*> { &srcBox, &partBox, &paramBox, &depth, &removeButton })
        addAndMakeVisible (c);
}

Route RouteRow::getRoute() const
{
    Route r;
    r.source  = jmax (0, srcBox.getSelectedId() - 1);
    r.part    = jmax (0, partBox.getSelectedId() - 1);
    r.ccIndex = jmax (0, paramBox.getSelectedId() - 1);
    r.depth   = (float) depth.getValue() / 100.0f;
    return r;
}

void RouteRow::resized()
{
    srcBox.setBounds (0, 3, 116, 24);
    partBox.setBounds (122, 3, 66, 24);
    paramBox.setBounds (194, 3, 170, 24);
    depth.setBounds (370, 3, 218, 24);
    removeButton.setBounds (594, 3, 28, 24);
}

void ModPage::RoutesList::layoutRows()
{
    const int w = getWidth();

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * 32, w, 30);

    setSize (w, jmax (10, rows.size() * 32));
}

ModPage::ModPage (SH4dProcessor& p) : proc (p)
{
    // macros
    for (int m = 0; m < kNumMacros; ++m)
    {
        const String id = "macro" + String (m + 1);
        macros[(size_t) m] = std::make_unique<KnobComponent> ("Macro " + String (m + 1));
        macros[(size_t) m]->attach (*proc.apvts.getParameter (id), &proc.undoManager);
        {
            auto& sl = macros[(size_t) m]->getSlider();
            sl.textFromValueFunction = [] (double v) { return String (roundToInt (v * 100.0)) + "%"; };
            sl.valueFromTextFunction = [] (const String& t) { return t.getDoubleValue() / 100.0; };
            sl.updateText();
        }
        macros[(size_t) m]->onContext = [this, m, id] { showParamMenu (proc, macros[(size_t) m].get(), proc.getParamIndex (id), 0, -1); };
        addAndMakeVisible (*macros[(size_t) m]);
    }

    // LFOs
    for (int n = 0; n < kNumLfos; ++n)
    {
        auto& L = lfos[(size_t) n];
        const String t = "lfo" + String (n + 1) + "_";

        for (int s = 0; s < kNumLfoShapes; ++s) L.shape.addItem (kLfoShapes[s], s + 1);
        for (int s = 0; s < kNumLfoSync; ++s)   L.sync.addItem (kLfoSyncNames[s], s + 1);

        L.shapeAtt = std::make_unique<ComboBoxParameterAttachment> (*proc.apvts.getParameter (t + "shape"), L.shape, nullptr);
        L.syncAtt  = std::make_unique<ComboBoxParameterAttachment> (*proc.apvts.getParameter (t + "sync"), L.sync, nullptr);

        L.rate = std::make_unique<KnobComponent> ("Rate");
        L.rate->attach (*proc.apvts.getParameter (t + "rate"), &proc.undoManager);
        {
            auto& sl = L.rate->getSlider();
            sl.textFromValueFunction = [] (double v) { return String (v, 2) + " Hz"; };
            sl.valueFromTextFunction = [] (const String& txt) { return txt.getDoubleValue(); };
            sl.updateText();
        }
        L.rate->onContext = [this, n, t] { showParamMenu (proc, lfos[(size_t) n].rate.get(), proc.getParamIndex (t + "rate"), 0, -1); };

        addAndMakeVisible (L.shape);
        addAndMakeVisible (L.sync);
        addAndMakeVisible (*L.rate);
        addAndMakeVisible (L.meter);
    }

    // hardware sources
    setupLabel (*this, aftertouchLabel, "Aftertouch");
    setupLabel (*this, pitchBendLabel, "Pitch bend");
    setupLabel (*this, dmotionNote,
                "The SH-4d sends D-Motion tilt as channel aftertouch or pitch bend (set per axis on the unit). "
                "With a MIDI input open they appear here as modulation sources: right-click any knob > Modulate with.",
                Justification::topLeft, 12.0f);
    addAndMakeVisible (aftertouchMeter);
    addAndMakeVisible (pitchBendMeter);

    // routing
    routesView.setViewedComponent (&routesList, false);
    routesView.setScrollBarsShown (true, false);
    addAndMakeVisible (routesView);

    addRouteButton.setButtonText ("+ Add route");
    addRouteButton.onClick = [this]
    {
        proc.addRoute ({ kSrcMacro1, part < kNumSynthParts ? part : 0, indexOfCc (74), 0.5f });
        seenRoutesVersion = -1;
    };
    addAndMakeVisible (addRouteButton);

    // XY
    xyX = jlimit (0, kNumCc - 1, getInt (proc, "xyX", indexOfCc (74)));
    xyY = jlimit (0, kNumCc - 1, getInt (proc, "xyY", indexOfCc (71)));

    addAndMakeVisible (xyPad);
    setupLabel (*this, xLabel, "X");
    setupLabel (*this, yLabel, "Y");
    addAndMakeVisible (xBox);
    addAndMakeVisible (yBox);
    xBox.onChange = [this] { xyX = jmax (0, xBox.getSelectedId() - 1); setInt (proc, "xyX", xyX); };
    yBox.onChange = [this] { xyY = jmax (0, yBox.getSelectedId() - 1); setInt (proc, "xyY", xyY); };

    xyPad.onGesture = [this] (bool begin)
    {
        if (part >= kNumSynthParts) return;

        for (int idx : { xyX, xyY })
        {
            if (idx == xyY && xyY == xyX) break;
            auto* prm = proc.getCcParam (part, idx);
            if (begin) prm->beginChangeGesture(); else prm->endChangeGesture();
        }
    };

    xyPad.onMove = [this] (float x, float y)
    {
        if (part >= kNumSynthParts) return;

        auto* px = proc.getCcParam (part, xyX);
        auto* py = proc.getCcParam (part, xyY);
        px->setValueNotifyingHost (px->convertTo0to1 (std::round (x * 127.0f)));
        py->setValueNotifyingHost (py->convertTo0to1 (std::round (y * 127.0f)));
    };

    rebuildXyBoxes();
    partChanged();
}

ModPage::~ModPage() {}

void ModPage::rebuildXyBoxes()
{
    xBox.clear (dontSendNotification);
    yBox.clear (dontSendNotification);

    for (int i = 0; i < kNumCc; ++i)
    {
        if (kCc[i].kind == Kind::Knob)
        {
            xBox.addItem (kCc[i].name, i + 1);
            yBox.addItem (kCc[i].name, i + 1);
        }
    }

    xBox.setSelectedId (xyX + 1, dontSendNotification);
    yBox.setSelectedId (xyY + 1, dontSendNotification);
}

void ModPage::partChanged()
{
    part = proc.getSelectedPart();
    const bool synth = part < kNumSynthParts;
    xyPad.setEnabled (synth);
    xyPad.setAlpha (synth ? 1.0f : 0.35f);
    xBox.setEnabled (synth);
    yBox.setEnabled (synth);
}

void ModPage::rebuildRoutes()
{
    routesList.rows.clear();

    for (const auto& r : proc.getRoutes())
        routesList.rows.add (new RouteRow (r, [this] (RouteRow& row) { commitRow (row); }, [this] (RouteRow& row) { removeRow (row); }));

    for (auto* row : routesList.rows)
        routesList.addAndMakeVisible (row);

    routesList.setSize (jmax (100, routesView.getMaximumVisibleWidth()), routesList.getHeight());
    routesList.layoutRows();
    seenRoutesVersion = proc.getRoutesVersion();
}

void ModPage::commitRow (RouteRow& row)
{
    const int i = routesList.rows.indexOf (&row);
    auto v = proc.getRoutes();

    if (i >= 0 && i < (int) v.size())
    {
        v[(size_t) i] = row.getRoute();
        proc.setRoutes (v);
        seenRoutesVersion = proc.getRoutesVersion();      // our own edit: don't rebuild the rows under the user's mouse
    }
}

void ModPage::removeRow (RouteRow& row)
{
    const int i = routesList.rows.indexOf (&row);
    auto v = proc.getRoutes();

    if (i >= 0 && i < (int) v.size())
    {
        v.erase (v.begin() + i);
        proc.setRoutes (v);
        seenRoutesVersion = -1;                           // rebuilt on the next tick, not inside this button's own callback
    }
}

void ModPage::tick()
{
    if (seenRoutesVersion != proc.getRoutesVersion())
        rebuildRoutes();

    for (int m = 0; m < kNumMacros; ++m)
        macros[(size_t) m]->setLearning (proc.getLearnTarget() == proc.getParamIndex ("macro" + String (m + 1)));

    for (int n = 0; n < kNumLfos; ++n)
    {
        lfos[(size_t) n].meter.setValue (proc.getSourceValue (kSrcLfo1 + n), true);
        lfos[(size_t) n].rate->setLearning (proc.getLearnTarget() == proc.getParamIndex ("lfo" + String (n + 1) + "_rate"));
    }

    aftertouchMeter.setValue (proc.getSourceValue (kSrcAftertouch), false);
    pitchBendMeter.setValue (proc.getSourceValue (kSrcPitchBend), true);

    if (part < kNumSynthParts && ! xyPad.isDragging())
        xyPad.setPosition ((float) proc.getCcValue (part, xyX) / 127.0f, (float) proc.getCcValue (part, xyY) / 127.0f);
}

void ModPage::resized()
{
    const Rectangle<int> macroR (12, 0, 640, 176), srcR (660, 0, 348, 176), routesR (12, 378, 664, 236), xyR (684, 378, 324, 236);

    panels.clear();
    panels.push_back ({ macroR, "Macros  (automate them, MIDI-learn them, or route them to anything)" });
    panels.push_back ({ srcR, "Hardware sources (D-Motion)" });

    {
        auto area = macroR.reduced (10).withTrimmedTop (22);
        const int cw = area.getWidth() / kNumMacros;
        for (int m = 0; m < kNumMacros; ++m)
            macros[(size_t) m]->setBounds (area.getX() + m * cw, area.getY(), cw, area.getHeight());
    }

    aftertouchLabel.setBounds (srcR.getX() + 12, srcR.getY() + 30, 90, 22);
    aftertouchMeter.setBounds (srcR.getX() + 104, srcR.getY() + 34, 230, 14);
    pitchBendLabel.setBounds  (srcR.getX() + 12, srcR.getY() + 58, 90, 22);
    pitchBendMeter.setBounds  (srcR.getX() + 104, srcR.getY() + 62, 230, 14);
    dmotionNote.setBounds (srcR.getX() + 12, srcR.getY() + 90, srcR.getWidth() - 24, 80);

    for (int n = 0; n < kNumLfos; ++n)
    {
        const Rectangle<int> r (12 + n * 252, 184, 240, 186);
        panels.push_back ({ r, "LFO " + String (n + 1) });
        auto& L = lfos[(size_t) n];
        L.shape.setBounds (r.getX() + 10, r.getY() + 28, 108, 24);
        L.sync.setBounds (r.getX() + 124, r.getY() + 28, 106, 24);
        L.rate->setBounds (r.getX() + 10, r.getY() + 58, 110, 118);
        L.meter.setBounds (r.getX() + 132, r.getY() + 100, 98, 16);
    }

    panels.push_back ({ routesR, "Modulation routing" });
    panels.push_back ({ xyR, "XY Pad" });

    addRouteButton.setBounds (routesR.getRight() - 112, routesR.getY() + 4, 100, 20);
    routesView.setBounds (routesR.getX() + 8, routesR.getY() + 30, routesR.getWidth() - 16, routesR.getHeight() - 38);
    routesList.setSize (jmax (100, routesView.getMaximumVisibleWidth()), routesList.getHeight());
    routesList.layoutRows();

    xyPad.setBounds (xyR.getX() + 12, xyR.getY() + 34, 160, 160);
    xLabel.setBounds (xyR.getX() + 182, xyR.getY() + 40, 20, 24);
    xBox.setBounds   (xyR.getX() + 204, xyR.getY() + 40, 108, 24);
    yLabel.setBounds (xyR.getX() + 182, xyR.getY() + 76, 20, 24);
    yBox.setBounds   (xyR.getX() + 204, xyR.getY() + 76, 108, 24);
}

void ModPage::paint (Graphics& g)
{
    drawPanels (g, panels);
}

//==============================================================================
// LIBRARY PAGE
LibraryPage::LibraryPage (SH4dProcessor& p) : proc (p)
{
    searchBox.setTextToShowWhenEmpty ("Search patches...", SH4dLookAndFeel::textDim);
    searchBox.onTextChange = [this] { refreshList(); };
    addAndMakeVisible (searchBox);

    nameBox.setTextToShowWhenEmpty ("Patch name", SH4dLookAndFeel::textDim);
    categoryBox.setTextToShowWhenEmpty ("Category (Pad, Bass, Lead...)", SH4dLookAndFeel::textDim);
    addAndMakeVisible (nameBox);
    addAndMakeVisible (categoryBox);

    list.setRowHeight (24);
    list.setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    addAndMakeVisible (list);

    saveButton.setButtonText ("Save current part");
    saveButton.onClick = [this] { saveCurrent(); };
    loadButton.setButtonText ("Load");
    loadButton.onClick = [this] { loadRow (list.getSelectedRow()); };
    deleteButton.setButtonText ("Delete");
    deleteButton.onClick = [this] { deleteRow (list.getSelectedRow()); };
    refreshButton.setButtonText ("Refresh");
    refreshButton.onClick = [this] { refreshList(); };
    openFolderButton.setButtonText ("Open folder");
    openFolderButton.onClick = [] { PatchLibrary::getFolder().revealToUser(); };

    for (auto* b : { &saveButton, &loadButton, &deleteButton, &refreshButton, &openFolderButton })
        addAndMakeVisible (*b);

    sendProgToggle.setButtonText ("Send bank / program when loading");
    sendProgToggle.setToggleState (getInt (proc, "libSendProg", 1) != 0, dontSendNotification);
    sendProgToggle.onClick = [this] { setInt (proc, "libSendProg", sendProgToggle.getToggleState() ? 1 : 0); };
    addAndMakeVisible (sendProgToggle);

    setupLabel (*this, statusLabel, "", Justification::centredLeft, 12.0f);

    // scenes
    for (int i = 0; i < kNumScenes; ++i)
    {
        auto& b = sceneButtons[(size_t) i];
        b.setButtonText (String (i + 1));
        b.onClick = [this, i]
        {
            if (storeMode.getToggleState())
            {
                proc.storeScene (i);
            }
            else
            {
                proc.undoManager.beginNewTransaction ("Recall scene");
                proc.recallScene (i);
            }
        };
        addAndMakeVisible (b);
    }

    storeMode.setButtonText ("Store mode");
    addAndMakeVisible (storeMode);

    for (int i = 0; i < kNumScenes; ++i)
    {
        morphA.addItem ("Scene " + String (i + 1), i + 1);
        morphB.addItem ("Scene " + String (i + 1), i + 1);
    }

    morphA.setSelectedId (proc.getMorphA() + 1, dontSendNotification);
    morphB.setSelectedId (proc.getMorphB() + 1, dontSendNotification);
    morphA.onChange = [this] { proc.setMorphScenes (morphA.getSelectedId() - 1, morphB.getSelectedId() - 1); };
    morphB.onChange = [this] { proc.setMorphScenes (morphA.getSelectedId() - 1, morphB.getSelectedId() - 1); };
    addAndMakeVisible (morphA);
    addAndMakeVisible (morphB);

    setupLabel (*this, morphLabel, "Morph", Justification::centredLeft);
    setupLabel (*this, morphALabel, "from");
    setupLabel (*this, morphBLabel, "to");

    morphSlider.setSliderStyle (Slider::LinearHorizontal);
    morphSlider.setTextBoxStyle (Slider::TextBoxRight, false, 48, 24);
    addAndMakeVisible (morphSlider);
    morphAttachment = std::make_unique<SliderParameterAttachment> (*proc.apvts.getParameter ("morph"), morphSlider, &proc.undoManager);
    morphSlider.textFromValueFunction = [] (double v) { return String (roundToInt (v * 100.0)) + "%"; };
    morphSlider.valueFromTextFunction = [] (const String& t) { return t.getDoubleValue() / 100.0; };
    morphSlider.updateText();

    // A/B and copy
    for (int i = 0; i < 2; ++i)
    {
        abButtons[i].setButtonText (i == 0 ? "A" : "B");
        abButtons[i].setClickingTogglesState (true);
        abButtons[i].setRadioGroupId (300);
        abButtons[i].onClick = [this, i] { if (abButtons[i].getToggleState()) switchAB (i); };
        addAndMakeVisible (abButtons[i]);
    }
    abButtons[0].setToggleState (true, dontSendNotification);

    setupLabel (*this, copyLabel, "Copy the current part to:");
    for (int k = 0; k < kNumSynthParts; ++k)
    {
        copyButtons[k].setButtonText ("Part " + String (k + 1));
        copyButtons[k].onClick = [this, k]
        {
            proc.undoManager.beginNewTransaction ("Copy part");
            proc.copyPart (part, k);
        };
        addAndMakeVisible (copyButtons[k]);
    }

    setupLabel (*this, folderLabel, "Patches are saved as plain files in:\n" + PatchLibrary::getFolder().getFullPathName()
                                    + "\n\nBack them up, share them, or drop in files from friends and hit Refresh.",
                Justification::topLeft, 12.0f);

    refreshList();
    partChanged();
}

LibraryPage::~LibraryPage() {}

void LibraryPage::partChanged()
{
    part = proc.getSelectedPart();
    const bool synth = part < kNumSynthParts;

    abValid[0] = abValid[1] = false;
    abCurrent = 0;
    abButtons[0].setToggleState (true, dontSendNotification);

    for (auto* b : { &saveButton, &loadButton, &abButtons[0], &abButtons[1] })
        b->setEnabled (synth);

    for (int k = 0; k < kNumSynthParts; ++k)
        copyButtons[k].setEnabled (synth && k != part);
}

void LibraryPage::tick()
{
    for (int i = 0; i < kNumScenes; ++i)
        sceneButtons[(size_t) i].setColour (TextButton::buttonColourId,
                                            proc.hasScene (i) ? Colour (0xff8a3f1c) : Colour (0xff2b313c));

    if (morphA.getSelectedId() - 1 != proc.getMorphA()) morphA.setSelectedId (proc.getMorphA() + 1, dontSendNotification);
    if (morphB.getSelectedId() - 1 != proc.getMorphB()) morphB.setSelectedId (proc.getMorphB() + 1, dontSendNotification);
}

void LibraryPage::refreshList()
{
    all = PatchLibrary::scan();
    shown.clear();
    const auto q = searchBox.getText().trim();

    for (const auto& pi : all)
        if (q.isEmpty() || pi.name.containsIgnoreCase (q) || pi.category.containsIgnoreCase (q))
            shown.add (pi);

    list.updateContent();
    list.repaint();
}

void LibraryPage::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= shown.size())
        return;

    if (selected)
    {
        g.setColour (SH4dLookAndFeel::accent.withAlpha (0.35f));
        g.fillRect (0, 0, w, h);
    }

    const auto& pi = shown.getReference (row);
    g.setColour (SH4dLookAndFeel::textDim);
    g.setFont (13.0f);
    g.drawText (pi.category.isEmpty() ? String ("-") : pi.category, 8, 0, 130, h, Justification::centredLeft);
    g.setColour (SH4dLookAndFeel::text);
    g.drawText (pi.name, 144, 0, w - 150, h, Justification::centredLeft);
}

void LibraryPage::selectedRowsChanged (int row)
{
    if (row >= 0 && row < shown.size())
    {
        nameBox.setText (shown.getReference (row).name, dontSendNotification);
        categoryBox.setText (shown.getReference (row).category, dontSendNotification);
    }
}

void LibraryPage::saveCurrent()
{
    const auto name = nameBox.getText().trim();

    if (name.isEmpty() || part >= kNumSynthParts)
    {
        statusLabel.setText ("Type a name first.", dontSendNotification);
        return;
    }

    const bool ok = PatchLibrary::save (name, categoryBox.getText(), proc.getPartValues (part),
                                        getInt (proc, "msb" + String (part), kToneBankMsb),
                                        getInt (proc, "lsb" + String (part), kToneBankLsb),
                                        getInt (proc, "pc" + String (part), 0));

    statusLabel.setText (ok ? "Saved \"" + name + "\"." : "Could not write the patch file.", dontSendNotification);
    refreshList();
}

void LibraryPage::loadRow (int row)
{
    if (row < 0 || row >= shown.size() || part >= kNumSynthParts)
        return;

    PatchLibrary::Values v = proc.getPartValues (part);
    PatchInfo info;

    if (! PatchLibrary::load (shown.getReference (row).file, v, info))
    {
        statusLabel.setText ("Could not read that patch.", dontSendNotification);
        return;
    }

    proc.undoManager.beginNewTransaction ("Load patch");
    proc.setPartValues (part, v);

    setInt (proc, "msb" + String (part), info.bankMsb);
    setInt (proc, "lsb" + String (part), info.bankLsb);
    setInt (proc, "pc"  + String (part), info.program);

    if (sendProgToggle.getToggleState())
        proc.sendProgram (part, info.bankMsb, info.bankLsb, info.program);

    nameBox.setText (info.name, dontSendNotification);
    categoryBox.setText (info.category, dontSendNotification);
    statusLabel.setText ("Loaded \"" + info.name + "\" into part " + String (part + 1) + ".", dontSendNotification);
}

void LibraryPage::deleteRow (int row)
{
    if (row < 0 || row >= shown.size())
        return;

    const auto name = shown.getReference (row).name;
    PatchLibrary::remove (shown.getReference (row).file);
    statusLabel.setText ("Deleted \"" + name + "\".", dontSendNotification);
    refreshList();
}

void LibraryPage::switchAB (int target)
{
    if (target == abCurrent || part >= kNumSynthParts)
        return;

    abSlot[abCurrent] = proc.getPartValues (part);
    abValid[abCurrent] = true;
    abCurrent = target;

    if (! abValid[target])
    {
        abSlot[target] = abSlot[1 - target];     // first visit: B starts as a copy of A, ready to tweak
        abValid[target] = true;
    }

    proc.undoManager.beginNewTransaction ("A/B");
    proc.setPartValues (part, abSlot[target]);
}

void LibraryPage::resized()
{
    const Rectangle<int> patchesR (12, 0, 560, 614), scenesR (580, 0, 428, 250), toolsR (580, 258, 428, 170), infoR (580, 436, 428, 178);

    panels.clear();
    panels.push_back ({ patchesR, "Patch library" });
    panels.push_back ({ scenesR, "Scenes + morph (all four parts)" });
    panels.push_back ({ toolsR, "A/B compare + copy" });
    panels.push_back ({ infoR, "Where patches live" });

    const int x = patchesR.getX() + 12, w = patchesR.getWidth() - 24;
    searchBox.setBounds (x, patchesR.getY() + 30, w, 26);
    list.setBounds (x, patchesR.getY() + 62, w, 380);
    nameBox.setBounds (x, patchesR.getY() + 452, 340, 26);
    categoryBox.setBounds (x + 348, patchesR.getY() + 452, w - 348, 26);
    saveButton.setBounds (x, patchesR.getY() + 488, 150, 28);
    loadButton.setBounds (x + 158, patchesR.getY() + 488, 80, 28);
    deleteButton.setBounds (x + 246, patchesR.getY() + 488, 80, 28);
    refreshButton.setBounds (x + 334, patchesR.getY() + 488, 90, 28);
    openFolderButton.setBounds (x + 432, patchesR.getY() + 488, 104, 28);
    sendProgToggle.setBounds (x, patchesR.getY() + 526, 260, 26);
    statusLabel.setBounds (x, patchesR.getY() + 560, w, 22);

    for (int i = 0; i < kNumScenes; ++i)
        sceneButtons[(size_t) i].setBounds (scenesR.getX() + 12 + (i % 4) * 102, scenesR.getY() + 32 + (i / 4) * 48, 96, 40);

    storeMode.setBounds (scenesR.getX() + 12, scenesR.getY() + 134, 120, 26);
    morphLabel.setBounds (scenesR.getX() + 12, scenesR.getY() + 172, 50, 24);
    morphALabel.setBounds (scenesR.getX() + 62, scenesR.getY() + 172, 34, 24);
    morphA.setBounds (scenesR.getX() + 94, scenesR.getY() + 172, 84, 24);
    morphBLabel.setBounds (scenesR.getX() + 184, scenesR.getY() + 172, 20, 24);
    morphB.setBounds (scenesR.getX() + 204, scenesR.getY() + 172, 84, 24);
    morphSlider.setBounds (scenesR.getX() + 12, scenesR.getY() + 206, 404, 28);

    abButtons[0].setBounds (toolsR.getX() + 12, toolsR.getY() + 34, 60, 32);
    abButtons[1].setBounds (toolsR.getX() + 78, toolsR.getY() + 34, 60, 32);
    copyLabel.setBounds (toolsR.getX() + 12, toolsR.getY() + 82, 300, 22);
    for (int k = 0; k < kNumSynthParts; ++k)
        copyButtons[k].setBounds (toolsR.getX() + 12 + k * 102, toolsR.getY() + 110, 96, 30);

    folderLabel.setBounds (infoR.getX() + 12, infoR.getY() + 30, infoR.getWidth() - 24, infoR.getHeight() - 40);
}

void LibraryPage::paint (Graphics& g)
{
    drawPanels (g, panels);

    g.setColour (SH4dLookAndFeel::textDim);
    g.setFont (12.0f);
    g.drawText ("Off = recall   On = store", 580 + 144, 0 + 134, 200, 26, Justification::centredLeft);

    g.setColour (Colour (0xff101216));
    g.fillRoundedRectangle (list.getBounds().toFloat(), 4.0f);

    if (shown.isEmpty())
    {
        g.setColour (SH4dLookAndFeel::textDim);
        g.setFont (13.0f);
        g.drawFittedText (all.isEmpty() ? "No patches yet.\n\nTweak a sound, type a name below and press Save current part."
                                        : "No patches match your search.",
                          list.getBounds().reduced (30), Justification::centred, 4);
    }
}

//==============================================================================
// SETUP PAGE
SetupPage::SetupPage (SH4dProcessor& p) : proc (p)
{
    setupLabel (*this, hwInLabel,   "Hardware MIDI in");
    setupLabel (*this, hwOutLabel,  "Hardware MIDI out");
    setupLabel (*this, ctlInLabel,  "Controller MIDI in");
    setupLabel (*this, hostOutLabel, "Host MIDI out");

    auto nameOf = [] (ComboBox& b) -> String
    {
        const auto t = b.getText();
        return t == "(none)" ? String() : t.replace (" (not connected)", "");
    };

    hwInBox.onChange  = [this, nameOf] { proc.setHwIn  (nameOf (hwInBox));  };
    hwOutBox.onChange = [this, nameOf] { proc.setHwOut (nameOf (hwOutBox)); };
    ctlInBox.onChange = [this, nameOf] { proc.setCtlIn (nameOf (ctlInBox)); };

    for (auto* b : { &hwInBox, &hwOutBox, &ctlInBox })
        addAndMakeVisible (*b);

    for (int i = 0; i < 3; ++i)
        hostOutBox.addItem (kHostOutModes[i], i + 1);
    addAndMakeVisible (hostOutBox);
    hostOutAtt = std::make_unique<ComboBoxParameterAttachment> (*proc.apvts.getParameter ("hostOut"), hostOutBox, nullptr);

    autoButton.setButtonText ("Auto-detect SH-4d");
    autoButton.onClick = [this] { proc.autoDetectDevices(); rebuildDeviceLists(); };
    refreshButton.setButtonText ("Rescan");
    refreshButton.onClick = [this] { proc.refreshMidiDevices(); rebuildDeviceLists(); };
    addAndMakeVisible (autoButton);
    addAndMakeVisible (refreshButton);

    sendOnConnect.setButtonText ("Send All when the unit connects");
    remapCtl.setButtonText ("Play controller notes on the selected part");
    ccThru.setButtonText ("Pass mapped CCs from the DAW through");
    for (auto* b : { &sendOnConnect, &remapCtl, &ccThru })
        addAndMakeVisible (*b);

    sendAtt  = std::make_unique<ButtonParameterAttachment> (*proc.apvts.getParameter ("sendOnConnect"), sendOnConnect, nullptr);
    remapAtt = std::make_unique<ButtonParameterAttachment> (*proc.apvts.getParameter ("remapCtl"), remapCtl, nullptr);
    thruAtt  = std::make_unique<ButtonParameterAttachment> (*proc.apvts.getParameter ("ccThru"), ccThru, nullptr);

    for (int i = 0; i < kNumParts; ++i)
    {
        setupLabel (*this, chLabels[(size_t) i], i < kNumSynthParts ? "Part " + String (i + 1) : String ("Rhythm"), Justification::centred);
        auto& s = chSliders[(size_t) i];
        s.setSliderStyle (Slider::IncDecButtons);
        s.setTextBoxStyle (Slider::TextBoxLeft, false, 34, 26);
        addAndMakeVisible (s);
        chAtts[(size_t) i] = std::make_unique<SliderParameterAttachment> (*proc.getChannelParam (i), s, nullptr);
    }

    learnList.setRowHeight (22);
    addAndMakeVisible (learnList);

    clearSelected.setButtonText ("Remove selected");
    clearSelected.onClick = [this] { proc.removeLearnAt (learnList.getSelectedRow()); };
    clearAll.setButtonText ("Clear all");
    clearAll.onClick = [this] { proc.clearLearn (-1); };
    addAndMakeVisible (clearSelected);
    addAndMakeVisible (clearAll);

    setupLabel (*this, learnStatus, "", Justification::centredLeft, 12.0f);
    setupLabel (*this, helpText,
                "How it fits together\n\n"
                "Hardware in/out: the SH-4d's USB MIDI ports. The plugin talks to the unit directly, so you don't need to route MIDI in your DAW. "
                "Turn Tx CC on in the SH-4d's system settings and its panel moves show up here.\n\n"
                "Controller in: any USB keyboard or knob box. Its notes play the selected part; right-click any knob and choose MIDI learn to map its knobs.\n\n"
                "Host MIDI out: Auto sends through your DAW only when no hardware out is open.",
                Justification::topLeft, 12.0f);

    rebuildDeviceLists();
    rebuildLearnList();
}

SetupPage::~SetupPage() {}

void SetupPage::fillBox (ComboBox& box, const StringArray& names, const String& wanted)
{
    box.clear (dontSendNotification);
    box.addItem ("(none)", 1);

    for (int i = 0; i < names.size(); ++i)
        box.addItem (names[i], i + 2);

    if (wanted.isEmpty())
    {
        box.setSelectedId (1, dontSendNotification);
    }
    else
    {
        const int at = names.indexOf (wanted);

        if (at >= 0)
        {
            box.setSelectedId (at + 2, dontSendNotification);
        }
        else
        {
            box.addItem (wanted + " (not connected)", names.size() + 2);
            box.setSelectedId (names.size() + 2, dontSendNotification);
        }
    }
}

void SetupPage::rebuildDeviceLists()
{
    lastInNames  = proc.getMidiInputNames();
    lastOutNames = proc.getMidiOutputNames();
    fillBox (hwInBox,  lastInNames,  proc.getHwInName());
    fillBox (hwOutBox, lastOutNames, proc.getHwOutName());
    fillBox (ctlInBox, lastInNames,  proc.getCtlInName());
}

void SetupPage::rebuildLearnList()
{
    maps = proc.getLearnMaps();
    learnList.updateContent();
    learnList.repaint();
    seenLearnVersion = proc.getLearnVersion();
}

void SetupPage::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) maps.size())
        return;

    if (selected)
    {
        g.setColour (SH4dLookAndFeel::accent.withAlpha (0.35f));
        g.fillRect (0, 0, w, h);
    }

    const auto& m = maps[(size_t) row];
    String target = "?";

    if (auto* prm = proc.getParamByIndex (m.paramIndex))
        target = prm->getName (64);

    g.setColour (SH4dLookAndFeel::text);
    g.setFont (13.0f);
    g.drawText ("CC " + String (m.cc) + (m.channel == 0 ? "  (any ch)" : "  ch " + String (m.channel)) + "   ->   " + target,
                8, 0, w - 12, h, Justification::centredLeft);
}

void SetupPage::tick()
{
    ++tickCount;

    if (seenLearnVersion != proc.getLearnVersion())
        rebuildLearnList();

    const int lt = proc.getLearnTarget();
    if (lt >= 0)
    {
        auto* prm = proc.getParamByIndex (lt);
        learnStatus.setText ("Learning: move a knob on your controller to map it to  " + (prm != nullptr ? prm->getName (64) : String ("?")),
                             dontSendNotification);
    }
    else
    {
        learnStatus.setText ("Tip: right-click any knob > MIDI learn.", dontSendNotification);
    }

    // pick up devices being plugged in / out (not while a menu is open)
    if (tickCount % 40 == 0 && ! hwInBox.isPopupActive() && ! hwOutBox.isPopupActive() && ! ctlInBox.isPopupActive())
    {
        if (proc.getMidiInputNames() != lastInNames || proc.getMidiOutputNames() != lastOutNames)
            rebuildDeviceLists();
    }

    const bool leds[3] = { proc.isHwInOpen(), proc.isHwOutOpen(), proc.isCtlInOpen() };
    bool changed = false;
    for (int i = 0; i < 3; ++i)
        if (leds[i] != lastLed[i]) { lastLed[i] = leds[i]; changed = true; }

    if (changed)
        repaint();
}

void SetupPage::resized()
{
    const Rectangle<int> midiR (12, 0, 500, 260), behaviourR (520, 0, 488, 260),
                         chR (12, 268, 500, 110), learnR (520, 268, 488, 346), helpR (12, 386, 500, 228);

    panels.clear();
    panels.push_back ({ midiR, "MIDI devices" });
    panels.push_back ({ behaviourR, "Behaviour" });
    panels.push_back ({ chR, "MIDI channel of each part on the SH-4d" });
    panels.push_back ({ learnR, "MIDI learn" });
    panels.push_back ({ helpR, "Notes" });

    const int x = midiR.getX() + 12;
    auto row = [&] (Label& l, ComboBox& b, int y) { l.setBounds (x, y, 120, 24); b.setBounds (x + 126, y, 318, 24); };
    row (hwInLabel,  hwInBox,  midiR.getY() + 36);
    row (hwOutLabel, hwOutBox, midiR.getY() + 76);
    row (ctlInLabel, ctlInBox, midiR.getY() + 116);

    ledHwIn  = { x + 456, midiR.getY() + 41, 14, 14 };
    ledHwOut = { x + 456, midiR.getY() + 81, 14, 14 };
    ledCtl   = { x + 456, midiR.getY() + 121, 14, 14 };

    autoButton.setBounds (x, midiR.getY() + 170, 170, 30);
    refreshButton.setBounds (x + 178, midiR.getY() + 170, 100, 30);

    const int bx = behaviourR.getX() + 12;
    hostOutLabel.setBounds (bx, behaviourR.getY() + 36, 120, 24);
    hostOutBox.setBounds (bx + 126, behaviourR.getY() + 36, 200, 24);
    sendOnConnect.setBounds (bx, behaviourR.getY() + 84, 380, 28);
    remapCtl.setBounds (bx, behaviourR.getY() + 120, 380, 28);
    ccThru.setBounds (bx, behaviourR.getY() + 156, 380, 28);

    for (int i = 0; i < kNumParts; ++i)
    {
        chLabels[(size_t) i].setBounds (chR.getX() + 12 + i * 96, chR.getY() + 30, 90, 20);
        chSliders[(size_t) i].setBounds (chR.getX() + 12 + i * 96, chR.getY() + 54, 90, 28);
    }

    learnList.setBounds (learnR.getX() + 12, learnR.getY() + 30, learnR.getWidth() - 24, 250);
    clearSelected.setBounds (learnR.getX() + 12, learnR.getY() + 290, 140, 28);
    clearAll.setBounds (learnR.getX() + 160, learnR.getY() + 290, 100, 28);
    learnStatus.setBounds (learnR.getX() + 12, learnR.getY() + 322, learnR.getWidth() - 24, 20);

    helpText.setBounds (helpR.getX() + 12, helpR.getY() + 28, helpR.getWidth() - 24, helpR.getHeight() - 36);
}

void SetupPage::paint (Graphics& g)
{
    drawPanels (g, panels);

    const std::array<std::pair<Rectangle<int>, bool>, 3> leds {{ { ledHwIn, proc.isHwInOpen() }, { ledHwOut, proc.isHwOutOpen() }, { ledCtl, proc.isCtlInOpen() } }};

    for (const auto& l : leds)
    {
        g.setColour (l.second ? Colour (0xff3ddc84) : Colour (0xff4a5160));
        g.fillEllipse (l.first.toFloat());
    }
}

//==============================================================================
// MAIN CONTENT (fixed 1020 x 790 design, scaled by the editor shell)
MainContent::MainContent (SH4dProcessor& p) : proc (p)
{
    setLookAndFeel (&laf);
    setWantsKeyboardFocus (true);

    for (int i = 0; i < kNumParts; ++i)
    {
        auto& b = partButtons[(size_t) i];
        b.setButtonText (i < kNumSynthParts ? "PART " + String (i + 1) : String ("RHYTHM"));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (100);
        b.onClick = [this, i] { if (partButtons[(size_t) i].getToggleState()) selectPart (i); };
        addAndMakeVisible (b);
    }

    static const char* tabNames[5] = { "PANEL", "SOUND", "MOD", "LIBRARY", "SETUP" };
    for (int i = 0; i < 5; ++i)
    {
        auto& b = tabButtons[(size_t) i];
        b.setButtonText (tabNames[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (200);
        b.onClick = [this, i] { if (tabButtons[(size_t) i].getToggleState()) selectPage (i); };
        addAndMakeVisible (b);
    }

    undoButton.setButtonText ("Undo");
    undoButton.onClick = [this] { proc.undoManager.undo(); };
    redoButton.setButtonText ("Redo");
    redoButton.onClick = [this] { proc.undoManager.redo(); };
    sendAllButton.setButtonText ("Send All");
    sendAllButton.setTooltip ("Push every value of all four synth parts to the SH-4d");
    sendAllButton.onClick = [this] { proc.requestSendAll(); };
    panicButton.setButtonText ("Panic");
    panicButton.onClick = [this] { proc.sendPanic (proc.getSelectedPart()); };

    for (auto* b : { &undoButton, &redoButton, &sendAllButton, &panicButton })
        addAndMakeVisible (*b);

    panelPage   = std::make_unique<PanelPage> (proc);
    soundPage   = std::make_unique<SoundPage> (proc);
    modPage     = std::make_unique<ModPage> (proc);
    libraryPage = std::make_unique<LibraryPage> (proc);
    setupPage   = std::make_unique<SetupPage> (proc);
    pages = { panelPage.get(), soundPage.get(), modPage.get(), libraryPage.get(), setupPage.get() };
    panelPage->requestPage = [this] (int pg) { selectPage (pg); };

    for (auto* pg : pages)
        addChildComponent (*pg);

    status.setFont (FontOptions (13.0f));
    status.setJustificationType (Justification::centredRight);
    addAndMakeVisible (status);

    monitor.setFont (FontOptions (12.5f));
    monitor.setColour (Label::textColourId, SH4dLookAndFeel::textDim);
    monitor.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (monitor);

    keyboard.setKeyWidth (23.0f);
    keyboard.setAvailableRange (24, 95);
    keyboard.setLowestVisibleKey (24);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setWantsKeyboardFocus (false);
    keyboard.setColour (MidiKeyboardComponent::upDownButtonBackgroundColourId, SH4dLookAndFeel::panel);
    kbState.addListener (this);
    addAndMakeVisible (keyboard);

    sendLookAndFeelChange();       // rebuild slider text boxes etc. with our colours

    selectPart (proc.getSelectedPart());
    selectPage (jlimit (0, 4, getInt (proc, "page", 0)));

    setSize (kWidth, kHeight);
    startTimerHz (20);
}

MainContent::~MainContent()
{
    stopTimer();
    kbState.removeListener (this);
    setLookAndFeel (nullptr);
}

void MainContent::selectPage (int newPage)
{
    page = jlimit (0, 4, newPage);
    setInt (proc, "page", page);

    for (int i = 0; i < 5; ++i)
        pages[(size_t) i]->setVisible (i == page);

    keyboard.setVisible (page != 0);       // the panel has its own button keyboard

    tabButtons[(size_t) page].setToggleState (true, dontSendNotification);
    pages[(size_t) page]->tick();
}

void MainContent::selectPart (int part)
{
    proc.setSelectedPart (part);
    shownPart = proc.getSelectedPart();
    partButtons[(size_t) shownPart].setToggleState (true, dontSendNotification);

    for (auto* pg : pages)
        pg->partChanged();
}

void MainContent::handleNoteOn (MidiKeyboardState*, int, int note, float velocity)
{
    proc.sendNote (true, proc.getSelectedPart(), note, jlimit (1, 127, (int) (velocity * 127.0f)));
}

void MainContent::handleNoteOff (MidiKeyboardState*, int, int note, float velocity)
{
    proc.sendNote (false, proc.getSelectedPart(), note, (int) (velocity * 127.0f));
}

bool MainContent::keyPressed (const KeyPress& key)
{
    const auto cmd = ModifierKeys::commandModifier;

    if (key == KeyPress ('z', cmd, 0))                           { proc.undoManager.undo(); return true; }
    if (key == KeyPress ('z', cmd | ModifierKeys::shiftModifier, 0)
        || key == KeyPress ('y', cmd, 0))                        { proc.undoManager.redo(); return true; }

    return false;
}

void MainContent::timerCallback()
{
    if (proc.getSelectedPart() != shownPart)      // changed externally (project load)
    {
        shownPart = proc.getSelectedPart();
        partButtons[(size_t) shownPart].setToggleState (true, dontSendNotification);

        for (auto* pg : pages)
            pg->partChanged();
    }

    pages[(size_t) page]->tick();

    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());

    monitor.setText ("OUT  " + describe (proc.lastTx.load()) + "  [" + String (proc.txCount.load()) + "]"
                     + "        IN  " + describe (proc.lastRx.load()) + "  [" + String (proc.rxCount.load()) + "]",
                     dontSendNotification);

    if (proc.isHwOutOpen())
    {
        status.setText ("SH-4d connected: " + proc.getHwOutName(), dontSendNotification);
        status.setColour (Label::textColourId, Colour (0xff3ddc84));
    }
    else
    {
        status.setText ("No hardware MIDI out open - using host MIDI out (see SETUP)", dontSendNotification);
        status.setColour (Label::textColourId, SH4dLookAndFeel::textDim);
    }
}

void MainContent::paint (Graphics& g)
{
    g.fillAll (SH4dLookAndFeel::bg);
    g.setColour (SH4dLookAndFeel::text);
    g.setFont (FontOptions (22.0f, Font::bold));
    g.drawText ("SH-4d EDITOR", 14, 8, 170, 32, Justification::centredLeft);
}

void MainContent::resized()
{
    for (int i = 0; i < kNumParts; ++i)
        partButtons[(size_t) i].setBounds (190 + i * 84, 10, 80, 28);

    undoButton.setBounds (626, 10, 58, 28);
    redoButton.setBounds (688, 10, 58, 28);
    sendAllButton.setBounds (770, 10, 110, 28);
    panicButton.setBounds (888, 10, 120, 28);

    for (int i = 0; i < 5; ++i)
        tabButtons[(size_t) i].setBounds (12 + i * 104, 48, 100, 30);

    status.setBounds (540, 48, 468, 30);

    for (auto* pg : pages)
        pg->setBounds (0, 86, kWidth, 614);

    keyboard.setBounds (12, 706, 996, 50);
    monitor.setBounds (14, 762, 992, 22);
}

//==============================================================================
SH4dEditor::SH4dEditor (SH4dProcessor& p)
    : AudioProcessorEditor (&p), content (p)
{
    addAndMakeVisible (content);

    setResizable (true, true);
    setResizeLimits (510, 395, 2040, 1580);
    getConstrainer()->setFixedAspectRatio ((double) MainContent::kWidth / (double) MainContent::kHeight);
    setSize (MainContent::kWidth, MainContent::kHeight);
}

SH4dEditor::~SH4dEditor() {}

void SH4dEditor::resized()
{
    content.setBounds (0, 0, MainContent::kWidth, MainContent::kHeight);
    content.setTransform (AffineTransform::scale ((float) getWidth() / (float) MainContent::kWidth));
}
