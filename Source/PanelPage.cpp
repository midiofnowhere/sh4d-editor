// The PANEL page: a recreation of the SH-4d's top panel, laid out from a top-down photograph of the unit.
//
// Positions below are in the photo's pixel space (kS maps them to the editor's design space). Controls that have a
// MIDI CC on the SH-4d work; the rest are drawn for fidelity and say so on the LCD when touched.

#include "PluginEditor.h"

using namespace juce;
using namespace sh4d;

namespace
{
    // photo -> design space: the unit spans x 15..583, y 149..446 in the 600px photo
    constexpr double kS = 1020.0 / 568.0;
    constexpr int kPanelW = 1020, kPanelH = 533;

    float sx (double x) { return (float) ((x - 15.0) * kS); }
    float sy (double y) { return (float) ((y - 149.0) * kS); }

    const Colour kInk   { 0xffe6e8ea };
    const Colour kFrame { 0xffa9acb1 };

    int indexOfCcLocal (int cc)
    {
        for (int i = 0; i < kNumCc; ++i)
            if (kCc[i].cc == cc)
                return i;
        return 0;
    }

    constexpr float kA0 = MathConstants<float>::pi * 1.25f;
    constexpr float kA1 = MathConstants<float>::pi * 2.75f;

    Point<float> polar (Point<float> c, float angle, float radius)
    {
        return { c.x + std::sin (angle) * radius, c.y - std::cos (angle) * radius };
    }
}

//==============================================================================
// PanelKnob: serrated skirt, faceted cap with a white pointer
PanelKnob::PanelKnob (const String& l, bool isInert, bool isBipolar) : label (l), inert (isInert), bipolar (isBipolar) {}

void PanelKnob::attach (RangedAudioParameter& p, UndoManager* um)
{
    attachment.reset();
    param = &p;
    attachment = std::make_unique<ParameterAttachment> (p, [this] (float v)
    {
        norm = param != nullptr ? param->convertTo0to1 (v) : 0.0f;
        repaint();
    }, um);
    attachment->sendInitialUpdate();
}

void PanelKnob::detach()
{
    attachment.reset();
    param = nullptr;
    norm = 0.0f;
    repaint();
}

void PanelKnob::setEffective (float n)
{
    if (! approximatelyEqual (n, effNorm))
    {
        effNorm = n;
        repaint();
    }
}

String PanelKnob::getValueText() const
{
    return param != nullptr ? param->getText (norm, 8) : String();
}

void PanelKnob::paint (Graphics& g)
{
    const float r = radius;
    const Point<float> c (getWidth() * 0.5f, r + 4.0f);
    const float angle = kA0 + norm * (kA1 - kA0);

    // modulation ring
    if (effNorm >= 0.0f && std::abs (effNorm - norm) > 0.004f)
    {
        const float ea = kA0 + effNorm * (kA1 - kA0);
        Path ring;
        ring.addCentredArc (c.x, c.y, r + 5.0f, r + 5.0f, 0.0f, jmin (angle, ea), jmax (angle, ea), true);
        g.setColour (SH4dLookAndFeel::mod);
        g.strokePath (ring, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // skirt
    g.setColour (Colour (0xff060708));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setGradientFill (ColourGradient (Colour (0xff2d3034), c.x - r * 0.4f, c.y - r * 0.5f,
                                       Colour (0xff08090a), c.x + r * 0.6f, c.y + r * 0.7f, true));
    g.fillEllipse (Rectangle<float> (r * 1.84f, r * 1.84f).withCentre (c));

    // serrations
    constexpr int teeth = 33;
    g.setColour (Colour (0xffd0d3d8));
    for (int i = 0; i < teeth; ++i)
    {
        const float a = (kA0 - 0.14f) + (kA1 - kA0 + 0.28f) * (float) i / (float) (teeth - 1);
        g.drawLine (Line<float> (polar (c, a, r * 0.80f), polar (c, a, r * 0.99f)), jmax (1.0f, r * 0.055f));
    }

    // cap
    const float cr = r * 0.66f;
    g.setGradientFill (ColourGradient (Colour (0xff4c4f55), c.x - cr * 0.5f, c.y - cr * 0.6f,
                                       Colour (0xff0d0e10), c.x + cr * 0.6f, c.y + cr * 0.7f, true));
    g.fillEllipse (Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c));

    for (int side = -1; side <= 1; side += 2)         // the two light facets either side of the pointer axis
    {
        const float a = angle + (float) side * MathConstants<float>::halfPi;
        Path w;
        const auto p1 = polar (c, a - 0.62f, cr * 0.97f), p2 = polar (c, a + 0.62f, cr * 0.97f);
        w.addTriangle (c.x, c.y, p1.x, p1.y, p2.x, p2.y);
        g.setColour (Colour (0xff80848a).withAlpha (0.55f));
        g.fillPath (w);
    }

    g.setColour (Colour (0x33ffffff));
    g.drawEllipse (Rectangle<float> (cr * 2.0f - 1.0f, cr * 2.0f - 1.0f).withCentre (c), 1.0f);

    // pointer
    g.setColour (Colour (0xfff2f3f5));
    g.drawLine (Line<float> (polar (c, angle, cr * 0.28f), polar (c, angle, cr * 0.98f)), jmax (1.8f, r * 0.10f));

    // centre mark for bipolar controls (printed above the knob)
    if (bipolar)
    {
        g.setColour (kInk);
        g.fillRect (c.x - 0.8f, c.y - r - 3.0f, 1.6f, 3.5f);
    }

    // printed label
    g.setColour (inert ? kInk.withAlpha (0.62f) : kInk);
    g.setFont (FontOptions (10.5f, Font::bold));
    g.drawText (label, Rectangle<int> (0, (int) (c.y + r - 1.0f), getWidth(), 14), Justification::centred);
}

void PanelKnob::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onContext) onContext();
        return;
    }

    if (onTouch) onTouch (*this);

    if (inert || param == nullptr)
        return;

    dragging = true;
    dragStartNorm = norm;
    attachment->beginGesture();
}

void PanelKnob::mouseDrag (const MouseEvent& e)
{
    if (! dragging)
        return;

    const float sens = e.mods.isShiftDown() ? 900.0f : 220.0f;
    const float n = jlimit (0.0f, 1.0f, dragStartNorm - (float) e.getDistanceFromDragStartY() / sens);
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (n));
    if (onTouch) onTouch (*this);
}

void PanelKnob::mouseUp (const MouseEvent&)
{
    if (dragging)
    {
        attachment->endGesture();
        dragging = false;
    }
}

void PanelKnob::mouseDoubleClick (const MouseEvent&)
{
    if (! inert && param != nullptr)
        attachment->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void PanelKnob::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    if (inert || param == nullptr)
        return;

    const float n = jlimit (0.0f, 1.0f, norm + w.deltaY * 0.08f);
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (n));
    if (onTouch) onTouch (*this);
}

//==============================================================================
// PanelSlider: black slot, scale bars either side, wide black cap
PanelSlider::PanelSlider (const String& l) : label (l) {}

void PanelSlider::attach (RangedAudioParameter& p, UndoManager* um)
{
    attachment.reset();
    param = &p;
    attachment = std::make_unique<ParameterAttachment> (p, [this] (float v)
    {
        norm = param != nullptr ? param->convertTo0to1 (v) : 0.0f;
        repaint();
    }, um);
    attachment->sendInitialUpdate();
}

void PanelSlider::detach()
{
    attachment.reset();
    param = nullptr;
    norm = 0.0f;
    repaint();
}

String PanelSlider::getValueText() const
{
    return param != nullptr ? param->getText (norm, 8) : String();
}

Rectangle<float> PanelSlider::track() const
{
    auto b = getLocalBounds().toFloat();
    return b.withSizeKeepingCentre (12.0f, b.getHeight() - 8.0f);
}

float PanelSlider::yToNorm (float y) const
{
    const float top = track().getY() + 12.0f, bottom = track().getBottom() - 12.0f;
    return 1.0f - jlimit (0.0f, 1.0f, (y - top) / jmax (1.0f, bottom - top));
}

void PanelSlider::paint (Graphics& g)
{
    const auto t = track();
    const float cx = t.getCentreX();
    const float top = t.getY() + 12.0f, bottom = t.getBottom() - 12.0f;

    // scale bars
    for (int i = 0; i <= 10; ++i)
    {
        const float y = top + (bottom - top) * (float) i / 10.0f;
        g.setColour (Colour (0xff9ea1a6));
        g.fillRect (cx - 23.0f, y - 1.2f, 13.0f, 2.4f);
        g.fillRect (cx + 10.0f, y - 1.2f, 13.0f, 2.4f);
    }

    // slot
    g.setColour (Colour (0xff040405));
    g.fillRoundedRectangle (t.expanded (1.5f, 0.0f), 3.0f);
    g.setColour (Colour (0xff111214));
    g.fillRoundedRectangle (t.reduced (3.0f, 3.0f), 2.0f);

    // cap
    const float y = top + (bottom - top) * (1.0f - norm);
    const Rectangle<float> cap (cx - 22.0f, y - 8.0f, 44.0f, 16.0f);
    g.setGradientFill (ColourGradient::vertical (Colour (0xff55585e), cap.getY(), Colour (0xff101113), cap.getBottom()));
    g.fillRoundedRectangle (cap, 2.5f);
    g.setColour (Colour (0xff050506));
    g.drawRoundedRectangle (cap, 2.5f, 1.2f);
    g.setColour (Colour (0x44ffffff));
    g.drawLine (cap.getX() + 2.0f, cap.getY() + 1.5f, cap.getRight() - 2.0f, cap.getY() + 1.5f, 1.0f);
    g.setColour (Colour (0xfff0f1f3));
    g.fillRect (cx - 0.9f, cap.getY() + 2.0f, 1.8f, cap.getHeight() - 4.0f);
}

void PanelSlider::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onContext) onContext();
        return;
    }

    if (param == nullptr)
        return;

    dragging = true;
    attachment->beginGesture();
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (yToNorm (e.position.y)));
    if (onTouch) onTouch (*this);
}

void PanelSlider::mouseDrag (const MouseEvent& e)
{
    if (! dragging)
        return;

    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (yToNorm (e.position.y)));
    if (onTouch) onTouch (*this);
}

void PanelSlider::mouseUp (const MouseEvent&)
{
    if (dragging)
    {
        attachment->endGesture();
        dragging = false;
    }
}

void PanelSlider::mouseDoubleClick (const MouseEvent&)
{
    if (param != nullptr)
        attachment->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

//==============================================================================
// PanelButton: round, dark, with a lit state
PanelButton::PanelButton (const String& top, const String& bottom, Face f, const String& text)
    : topLabel (top), bottomLabel (bottom), faceText (text), face (f) {}

void PanelButton::setLed (bool on, Colour c)
{
    if (on != ledOn || c != ledColour)
    {
        ledOn = on;
        ledColour = c;
        repaint();
    }
}

void PanelButton::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float d = jmin (b.getWidth(), b.getHeight()) - 3.0f;
    const auto circle = Rectangle<float> (d, d).withCentre (b.getCentre());

    if (ledOn)
    {
        g.setColour (ledColour.withAlpha (0.20f));
        g.fillEllipse (circle.expanded (d * 0.20f));
    }

    g.setColour (Colour (0xff060708));
    g.fillEllipse (circle.expanded (1.6f));

    if (ring)
    {
        g.setColour (Colour (0xff050506));
        g.fillEllipse (circle);
        g.setColour (ledOn ? ledColour : Colour (0xfff0f1f4));
        g.drawEllipse (circle.reduced (1.4f), 2.4f);
        return;
    }

    const Colour face0 = ledOn ? ledColour : (pressed ? Colour (0xff1a1c26) : Colour (0xff2b2e3b));
    g.setGradientFill (ColourGradient (face0.brighter (0.22f), circle.getCentreX() - d * 0.18f, circle.getY() + d * 0.12f,
                                       face0.darker (0.30f), circle.getCentreX() + d * 0.2f, circle.getBottom(), true));
    g.fillEllipse (circle);

    g.setColour (Colour (0x26ffffff));
    g.drawEllipse (circle.reduced (0.6f), 1.0f);
}

void PanelButton::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onContext) onContext();
        return;
    }

    pressed = true;
    repaint();

    if (onDown) onDown();
}

void PanelButton::mouseUp (const MouseEvent& e)
{
    const bool wasPressed = pressed;
    pressed = false;
    repaint();

    if (! wasPressed)
        return;

    if (onUp) onUp();

    if (onClick && getLocalBounds().contains (e.getPosition()))
        onClick();
}

//==============================================================================
// LcdDisplay: the small violet screen with the four oscillator waveforms
void LcdDisplay::paint (Graphics& g)
{
    const Colour screenBg (0xff3b3358), ink (0xfff4f2fa), barBg (0xffe6e3f2), barInk (0xff2b2440);
    auto b = getLocalBounds().toFloat();

    g.setColour (Colour (0xff050506));
    g.fillRoundedRectangle (b, 5.0f);

    auto s = b.reduced (4.0f);
    g.setColour (screenBg);
    g.fillRect (s);

    auto header = s.removeFromTop (14.0f);
    g.setColour (barBg);
    g.fillRect (header);
    g.setColour (barInk);
    g.setFont (FontOptions (10.5f, Font::bold));
    g.drawText (state.header, header.reduced (4.0f, 0.0f).toNearestInt(), Justification::centredLeft);

    if (state.idle)
    {
        // default oscillator screen: four wave icons, "SEL  SH-4d  WAVE"
        auto foot = s.removeFromBottom (15.0f);
        g.setColour (ink);
        g.setFont (FontOptions (9.5f, Font::bold));
        g.drawText ("SEL", foot.reduced (4.0f, 0.0f).toNearestInt(), Justification::centredLeft);
        g.drawText ("WAVE", foot.reduced (4.0f, 0.0f).toNearestInt(), Justification::centredRight);
        g.setFont (FontOptions (14.0f, Font::bold));
        g.drawText ("SH-4d", foot.toNearestInt(), Justification::centred);

        const float w = s.getWidth() / 4.0f;
        g.setColour (ink);

        for (int i = 0; i < 4; ++i)
        {
            const auto cell = Rectangle<float> (s.getX() + w * (float) i, s.getY(), w, s.getHeight()).reduced (7.0f, 6.0f);
            Path p;

            if (i == 0 || i == 2)          // pulse / square-ish
            {
                p.startNewSubPath (cell.getX(), cell.getBottom());
                p.lineTo (cell.getX(), cell.getY());
                p.lineTo (cell.getCentreX() - 2.0f, cell.getY());
                p.lineTo (cell.getCentreX() - 2.0f, cell.getBottom());
                p.lineTo (cell.getRight(), cell.getBottom());
            }
            else if (i == 1)               // saw
            {
                p.startNewSubPath (cell.getX(), cell.getBottom());
                p.lineTo (cell.getX() + 3.0f, cell.getY());
                p.lineTo (cell.getRight() - 4.0f, cell.getBottom());
                p.lineTo (cell.getRight(), cell.getBottom());
            }

            if (i < 3)
            {
                g.strokePath (p, PathStrokeType (1.8f, PathStrokeType::mitered, PathStrokeType::butt));
            }
            else                           // noise block
            {
                Random rnd (7);
                for (int k = 0; k < 28; ++k)
                    g.fillRect (cell.getX() + (float) (rnd.nextInt (8)) * 2.4f, cell.getY() + (float) (rnd.nextInt (8)) * 2.4f, 2.0f, 2.0f);
            }
        }

        g.fillRect (s.getX() + 6.0f, s.getBottom() - 3.0f, w - 12.0f, 2.0f);       // selected-oscillator underline
        return;
    }

    auto footer = s.removeFromBottom (12.0f);
    g.setColour (ink.withAlpha (0.7f));
    g.setFont (FontOptions (9.5f));
    g.drawText (state.footer, footer.reduced (4.0f, 0.0f).toNearestInt(), Justification::centredLeft);

    auto body = s.reduced (5.0f, 2.0f);

    if (state.bar >= 0.0f)
    {
        auto bar = body.removeFromBottom (5.0f);
        g.setColour (Colour (0xff59507e));
        g.fillRect (bar);
        g.setColour (ink);
        g.fillRect (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, state.bar)));
        body.removeFromBottom (2.0f);
    }

    g.setColour (ink);

    if (state.value.isEmpty())
    {
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawFittedText (state.title, body.toNearestInt(), Justification::centredLeft, 4, 0.9f);
    }
    else
    {
        g.setFont (FontOptions (10.5f, Font::bold));
        g.drawText (state.title, body.removeFromTop (13.0f).toNearestInt(), Justification::centredLeft);
        g.setFont (FontOptions (26.0f, Font::bold));
        g.drawText (state.value, body.toNearestInt(), Justification::centredLeft);
    }
}

//==============================================================================
// PanelPage
PanelKnob* PanelPage::addKnob (const String& label, int cc, bool bipolar)
{
    auto* k = knobs.add (new PanelKnob (label, cc < 0, bipolar));
    knobCc.add (cc < 0 ? -1 : indexOfCcLocal (cc));

    k->onTouch = [this] (PanelKnob& kn)
    {
        if (kn.isInert())
        {
            showMessage (kn.getLabel() + ": no MIDI control on the SH-4d - set it on the unit.");
            return;
        }

        const int idx = knobCc[knobs.indexOf (&kn)];
        touched = { &kn, nullptr, idx >= 0 ? String (kCc[idx].name) : kn.getLabel() };
        message = {};
    };

    if (cc >= 0)
        k->onContext = [this, k]
        {
            const int idx = knobCc[knobs.indexOf (k)];
            if (part < kNumSynthParts)
                showParamContextMenu (proc, k, proc.getCcParamIndex (part, idx), part, idx);
        };

    addAndMakeVisible (k);
    return k;
}

PanelButton* PanelPage::addButton (const String& top, const String& bottom, PanelButton::Face f, const String& faceText)
{
    auto* b = buttons.add (new PanelButton (top, bottom, f, faceText));
    addAndMakeVisible (b);
    return b;
}

void PanelPage::inert (PanelButton& b, const String& what)
{
    b.onClick = [this, what] { showMessage (what); };
}

void PanelPage::showMessage (const String& text)
{
    message = text;
    messageUntil = Time::getMillisecondCounter() + 3200;
    touched = {};
}

PanelPage::PanelPage (SH4dProcessor& p) : proc (p)
{
    addAndMakeVisible (lcd);

    // OSC section
    oscKnobs = { addKnob ("MODEL", -1), addKnob ("PITCH", 21, true), addKnob ("TIMBRE", 77) };

    static const Colour oscLed[4] = { Colour (0xffff86bd), Colour (0xffb9544f), Colour (0xffa6a548), Colour (0xff3fbf6c) };

    for (int i = 0; i < 4; ++i)
    {
        auto* s = sliders.add (new PanelSlider ("OSC " + String (i + 1)));
        const int idx = indexOfCcLocal (85 + i);

        s->onTouch = [this, idx] (PanelSlider& sl)
        {
            touched = { nullptr, &sl, String (kCc[idx].name) };
            message = {};
        };
        s->onContext = [this, s, idx]
        {
            if (part < kNumSynthParts)
                showParamContextMenu (proc, s, proc.getCcParamIndex (part, idx), part, idx);
        };
        addAndMakeVisible (s);

        auto* ob = addButton ({}, {});
        ob->setLed (true, oscLed[i]);
        inert (*ob, "OSC " + String (i + 1) + " button: its function depends on the oscillator model - use the unit.");
        oscButtons.push_back (ob);
    }

    // FILTER / AMP / LFO
    filterRow1 = { addKnob ("HPF", 79), addKnob ("TYPE", -1), addKnob ("CUTOFF", 74), addKnob ("RES", 71), addKnob ("KYBD", 78) };
    filterRow2 = { addKnob ("ATTACK", 82), addKnob ("DECAY", 83), addKnob ("SUSTAIN", 28), addKnob ("RELEASE", 29),
                   addKnob ("ENV", 81, true), addKnob ("DRIVE", 90) };
    ampRow = { addKnob ("ATTACK", 73), addKnob ("DECAY", 75), addKnob ("SUSTAIN", 31), addKnob ("RELEASE", 72),
               addKnob ("PAN", 10, true), addKnob ("LEVEL", 7) };
    lfoRow = { addKnob ("TYPE", -1), addKnob ("RATE", 16), addKnob ("FADE", 20), addKnob ("PITCH", 18),
               addKnob ("FILTER", 19), addKnob ("AMP", 80) };

    // control section, effects, volume
    encoders = { addKnob ("", -1), addKnob ("", -1) };
    fxKnobs  = { addKnob ("TYPE", -1), addKnob ("CTRL1", -1), addKnob ("CTRL2", -1) };
    volumeKnob = addKnob ("", -1);

    soundBtn = addButton ({}, {});
    soundBtn->onClick = [this] { if (requestPage) requestPage (1); };
    shiftBtn = addButton ({}, {});
    shiftBtn->onClick = [this]
    {
        shiftArmed = ! shiftArmed;
        showMessage (shiftArmed ? "SHIFT on: press 1-5 for parts, 10 INIT, 12 UNDO, 16 MENU..." : "SHIFT off");
    };

    upBtn = addButton ({}, {});  downBtn = addButton ({}, {});  leftBtn = addButton ({}, {});
    rightBtn = addButton ({}, {});  enterBtn = addButton ({}, {});  exitBtn = addButton ({}, {});

    for (auto* b : { upBtn, downBtn, leftBtn, rightBtn, enterBtn, exitBtn })
        inert (*b, "Screen navigation happens on the unit - the editor shows every parameter here.");

    for (int i = 0; i < 6; ++i)
    {
        auto* b = addButton ({}, {});
        inert (*b, "Effects are edited on the unit.");
        fxButtons.push_back (b);
    }

    fxButtons[0]->setLed (true, Colour (0xfff2f4fc));        // TONE and ON are lit in the reference photo
    fxButtons[2]->setLed (true, Colour (0xffc9cbe8));

    dmotionBtn = addButton ({}, {});
    dmotionBtn->setRing (true);
    dmotionBtn->onClick = [this] { showMessage ("D-MOTION: tilt the unit. Its aftertouch / pitch bend show up on the MOD tab as sources."); };

    // sequencer row
    startBtn = addButton ({}, {});
    patternBtn = addButton ({}, {});
    pageBtn = addButton ({}, {});

    startBtn->onClick   = [this] { showMessage (shiftArmed ? "REC: the sequencer runs on the unit." : "START: the sequencer runs on the unit."); shiftArmed = false; };
    patternBtn->onClick = [this] { showMessage (shiftArmed ? "EDIT: pattern settings are on the unit." : "PATTERN: the sequencer runs on the unit."); shiftArmed = false; };
    pageBtn->onClick    = [this]
    {
        if (shiftArmed) { shiftArmed = false; if (requestPage) requestPage (3); }       // WRITE -> patch library
        else showMessage ("PAGE/TIE: the sequencer runs on the unit.");
    };

    for (int i = 0; i < 16; ++i)
    {
        auto* b = addButton ({}, {});
        b->onClick = [this, i] { stepPressed (i + 1); };
        stepButtons.push_back (b);
    }

    // keyboard row
    octDown = addButton ({}, {});
    octUp   = addButton ({}, {});
    pitchDown = addButton ({}, {});
    pitchUp   = addButton ({}, {});

    octDown->onClick = [this] { octave = jmax (-3, octave - 1); showMessage ("OCTAVE " + String (octave >= 0 ? "+" : "") + String (octave)); };
    octUp->onClick   = [this] { octave = jmin (3, octave + 1);  showMessage ("OCTAVE " + String (octave >= 0 ? "+" : "") + String (octave)); };
    octDown->onContext = octUp->onContext = [this] { octave = 0; showMessage ("OCTAVE reset"); };

    pitchDown->onDown = [this] { bend (8192 - 4096); };
    pitchDown->onUp   = [this] { bend (8192); };
    pitchUp->onDown   = [this] { bend (8192 + 4095); };
    pitchUp->onUp     = [this] { bend (8192); };

    arpBtn = addButton ({}, {});
    inert (*arpBtn, "ARPEGGIO: the arpeggiator runs on the unit.");

    holdBtn = addButton ({}, {});
    holdBtn->onClick = [this]
    {
        if (shiftArmed) { toggleSwitch (65); showMessage ("PORTAMENTO (shift + HOLD)"); }
        else            { toggleSwitch (64); showMessage ("HOLD pedal (CC 64)"); }
        shiftArmed = false;
    };

    // 16 white buttons (C D E F G A B C D E F G A B C D) and the 10 black ones between them, as on the unit
    static const int whiteOffsets[16] = { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24, 26 };
    static const int blackOffsets[10] = { 1, 3, 6, 8, 10, 13, 15, 18, 20, 22 };

    for (int w = 0; w < 16; ++w) keyOffsets.push_back (whiteOffsets[w]);
    for (int b = 0; b < 10; ++b) keyOffsets.push_back (blackOffsets[b]);

    for (int k = 0; k < 26; ++k)
    {
        auto* b = addButton ({}, {});
        b->onDown = [this, k] { keyEvent (k, true); };
        b->onUp   = [this, k] { keyEvent (k, false); };
        keyButtons.push_back (b);
    }

    partChanged();
}

PanelPage::~PanelPage() {}

void PanelPage::bend (int value14)
{
    proc.sendPitchBend (part, value14);
}

void PanelPage::keyEvent (int keyIndex, bool down)
{
    proc.sendNote (down, part, noteForKey (keyIndex), down ? 100 : 0);
    keyButtons[(size_t) keyIndex]->setLed (down, Colour (0xffe9e6f8));
}

void PanelPage::toggleSwitch (int cc)
{
    if (part >= kNumSynthParts)
        return;

    auto* prm = proc.getCcParam (part, indexOfCcLocal (cc));
    prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
}

void PanelPage::stepPressed (int n)
{
    if (! shiftArmed)
    {
        showMessage ("Step " + String (n) + ": the sequencer runs on the unit. Press SHIFT first for panel functions.");
        return;
    }

    shiftArmed = false;

    switch (n)
    {
        case 1: case 2: case 3: case 4: case 5:
            proc.setSelectedPart (n - 1);
            showMessage (n == 5 ? "RHYTHM part selected" : "PART " + String (n) + " selected");
            break;
        case 6:  if (requestPage) requestPage (1); break;                      // TONE -> sound page
        case 7:  if (requestPage) requestPage (2); break;                      // MATRIX -> modulation
        case 8:  if (requestPage) requestPage (1); break;                      // LFO
        case 9:  showMessage ("TONE EQ: edit it on the unit."); break;
        case 10:
            if (part < kNumSynthParts)
            {
                proc.undoManager.beginNewTransaction ("Init part");
                proc.resetPart (part);
                showMessage ("INIT: editor values reset (Send All to apply)");
            }
            break;
        case 11: showMessage ("TEMPO: set it on the unit."); break;
        case 12: proc.undoManager.undo(); showMessage ("UNDO"); break;
        case 13: case 14: if (requestPage) requestPage (3); break;             // COPY / UTILITY -> library tools
        case 15: showMessage ("CLEAR: the sequencer runs on the unit."); break;
        case 16: if (requestPage) requestPage (4); break;                      // MENU -> setup
        default: break;
    }
}

void PanelPage::partChanged()
{
    part = proc.getSelectedPart();
    const bool synth = part < kNumSynthParts;

    for (int i = 0; i < knobs.size(); ++i)
    {
        auto* k = knobs[i];
        const int idx = knobCc[i];

        if (idx >= 0)
        {
            if (synth) k->attach (*proc.getCcParam (part, idx), &proc.undoManager); else k->detach();
            k->setAlpha (synth ? 1.0f : 0.35f);
        }
    }

    for (int i = 0; i < sliders.size(); ++i)
    {
        if (synth) sliders[i]->attach (*proc.getCcParam (part, indexOfCcLocal (85 + i)), &proc.undoManager);
        else       sliders[i]->detach();
        sliders[i]->setAlpha (synth ? 1.0f : 0.35f);
    }

    touched = {};
    repaint();
}

void PanelPage::tick()
{
    const bool synth = part < kNumSynthParts;

    if (synth)
    {
        for (int i = 0; i < knobs.size(); ++i)
        {
            const int idx = knobCc[i];
            if (idx >= 0)
                knobs[i]->setEffective (proc.isModulated (part, idx) ? (float) proc.getEffectiveValue (part, idx) / 127.0f : -1.0f);
        }
    }

    shiftBtn->setLed (shiftArmed, Colour (0xfff2f4fc));
    holdBtn->setLed (synth && proc.getCcValue (part, indexOfCcLocal (64)) > 63, Colour (0xfff2f4fc));

    const bool tilt = std::abs (proc.getSourceValue (kSrcPitchBend)) > 0.02f || proc.getSourceValue (kSrcAftertouch) > 0.02f;
    dmotionBtn->setLed (tilt, Colour (0xff4fd1c5));

    for (int i = 0; i < 16; ++i)
        stepButtons[(size_t) i]->setLed (shiftArmed && i < 5 && i == part, Colour (0xfffaf7fc));

    // LCD
    LcdDisplay::State st;
    st.header = synth ? "PART " + String (part + 1) + "   CH " + String (proc.getChannelForPart (part))
                      : "RHYTHM   CH " + String (proc.getChannelForPart (part));

    const auto now = Time::getMillisecondCounter();

    if (message.isNotEmpty() && now < messageUntil)
    {
        st.title = message;
    }
    else if (touched.knob != nullptr)
    {
        st.title = touched.name.toUpperCase();
        st.value = touched.knob->getValueText();
        st.bar = touched.knob->getNorm();
    }
    else if (touched.slider != nullptr)
    {
        st.title = touched.name.toUpperCase();
        st.value = touched.slider->getValueText();
        st.bar = touched.slider->getNorm();
    }
    else
    {
        st.idle = true;
    }

    st.footer = String ("OCT ") + (octave >= 0 ? "+" : "") + String (octave) + (shiftArmed ? "    SHIFT" : "");
    lcd.setState (st);
}

//==============================================================================
void PanelPage::resized()
{
    originY = jmax (0, (getHeight() - kPanelH) / 2);

    auto place = [this] (Component& c, double cx, double cy, int w, int h)
    {
        c.setBounds ((int) std::lround (sx (cx) - (float) w * 0.5f), (int) std::lround (sy (cy) + (float) originY - (float) h * 0.5f), w, h);
    };

    auto knob = [this] (PanelKnob* k, double cx, double cy, float r)
    {
        k->setRadius (r);
        const int w = (int) jmax (2.0f * r + 10.0f, 58.0f), h = (int) (2.0f * r + 4.0f + 5.0f + 14.0f);
        k->setBounds ((int) std::lround (sx (cx) - (float) w * 0.5f), (int) std::lround (sy (cy) + (float) originY - (r + 4.0f)), w, h);
    };

    auto button = [&] (PanelButton* b, double cx, double cy, int d) { place (*b, cx, cy, d + 6, d + 6); };

    // LCD (orig x 51..137, y 184..230)
    lcd.setBounds ((int) sx (49), (int) sy (182) + originY, (int) (sx (139) - sx (49)), (int) (sy (232) - sy (182)));

    knob (encoders[0],  62.7, 249.0, 25.0f);
    knob (encoders[1], 122.3, 249.0, 25.0f);

    button (shiftBtn, 51.7, 279.0, 22);
    button (soundBtn, 134.0, 279.0, 22);
    button (upBtn,    92.7, 278.5, 22);
    button (leftBtn,  72.0, 300.0, 22);
    button (rightBtn, 113.0, 300.0, 22);
    button (downBtn,  93.0, 321.0, 22);
    button (exitBtn,  51.7, 324.7, 22);
    button (enterBtn, 134.0, 324.7, 22);
    enterBtn->toFront (false);

    // OSC
    knob (oscKnobs[0], 181.7, 192.3, 24.0f);
    knob (oscKnobs[1], 218.3, 192.3, 24.0f);
    knob (oscKnobs[2], 255.7, 192.3, 24.0f);

    static const double sliderX[4] = { 171.7, 203.2, 235.0, 266.8 };
    for (int i = 0; i < 4; ++i)
    {
        place (*sliders[i], sliderX[i], 268.0, 58, 118);
        button (oscButtons[(size_t) i], sliderX[i], 325.0, 23);
    }

    // FILTER / AMP / LFO
    static const double colX[6] = { 303.0, 338.0, 371.7, 405.0, 438.3, 472.3 };

    knob (filterRow1[0], 303.0, 190.0, 24.0f);
    knob (filterRow1[1], 336.7, 190.0, 24.0f);
    knob (filterRow1[2], 389.0, 191.0, 31.0f);
    knob (filterRow1[3], 438.3, 190.0, 24.0f);
    knob (filterRow1[4], 472.3, 190.0, 24.0f);

    for (size_t i = 0; i < 6; ++i)
    {
        knob (filterRow2[i], colX[i], 226.0, 24.0f);
        knob (ampRow[i],     colX[i], 269.0, 24.0f);
        knob (lfoRow[i],     colX[i], 313.0, 24.0f);
    }

    // VOLUME / EFFECTS
    knob (volumeKnob, 527.7, 190.7, 25.0f);
    knob (fxKnobs[0], 527.7, 233.0, 25.0f);
    knob (fxKnobs[1], 510.3, 268.3, 24.0f);
    knob (fxKnobs[2], 545.0, 268.3, 24.0f);

    static const double fxX[3] = { 504.0, 527.7, 550.7 };
    for (int i = 0; i < 6; ++i)
        button (fxButtons[(size_t) i], fxX[i % 3], i < 3 ? 299.0 : 323.0, 20);

    // sequencer row
    button (startBtn,   51.7, 354.0, 21);
    button (patternBtn, 75.0, 354.0, 21);
    for (int i = 0; i < 16; ++i)
        button (stepButtons[(size_t) i], 104.0 + 27.6 * i, 354.2, 27);
    button (pageBtn, 545.7, 354.0, 21);

    // keyboard row
    button (octDown,   51.0, 389.0, 20);
    button (octUp,     75.0, 389.0, 20);
    button (pitchDown, 51.7, 418.0, 20);
    button (pitchUp,   75.7, 418.0, 20);

    for (int w = 0; w < 16; ++w)
        button (keyButtons[(size_t) w], 104.0 + 27.6 * w, 418.3, 27);

    static const int blackAfter[10] = { 0, 1, 3, 4, 5, 7, 8, 10, 11, 12 };
    for (int b = 0; b < 10; ++b)
        button (keyButtons[(size_t) (16 + b)], 104.0 + 27.6 * ((double) blackAfter[b] + 0.5), 389.3, 27);

    button (arpBtn,     490.0, 390.0, 17);
    button (holdBtn,    518.3, 390.0, 17);
    button (dmotionBtn, 545.7, 390.0, 24);
}

//==============================================================================
void PanelPage::paint (Graphics& g)
{
    g.fillAll (SH4dLookAndFeel::bg);
    g.setOrigin (0, originY);

    auto R = [] (double x1, double y1, double x2, double y2) { return Rectangle<float> (sx (x1), sy (y1), sx (x2) - sx (x1), sy (y2) - sy (y1)); };

    // chassis
    const Rectangle<float> body (0.0f, 0.0f, (float) kPanelW, (float) kPanelH);
    g.setGradientFill (ColourGradient::vertical (Colour (0xff2a2e31), 0.0f, Colour (0xff1c2023), (float) kPanelH));
    g.fillRoundedRectangle (body, 7.0f);
    g.setColour (Colour (0xff3b4044));
    g.drawRoundedRectangle (body.reduced (0.75f), 7.0f, 1.5f);

    auto text = [&g] (const String& t, double x, double y, float size, Justification j = Justification::centred, float alpha = 1.0f)
    {
        g.setColour (kInk.withAlpha (alpha));
        g.setFont (FontOptions (size, Font::bold));
        const auto cx = sx (x), cy = sy (y);

        if (j == Justification::centredLeft)  g.drawText (t, Rectangle<float> (cx, cy - 10.0f, 300.0f, 20.0f), j);
        else                                  g.drawText (t, Rectangle<float> (140.0f, 20.0f).withCentre ({ cx, cy }), j);
    };

    auto hline = [&g] (double x1, double x2, double y)
    {
        g.setColour (kFrame.withAlpha (0.85f));
        g.drawLine (sx (x1), sy (y), sx (x2), sy (y), 1.0f);
    };

    auto vline = [&g] (double x, double y1, double y2)
    {
        g.setColour (kFrame.withAlpha (0.85f));
        g.drawLine (sx (x), sy (y1), sx (x), sy (y2), 1.0f);
    };

    // section frames
    g.setColour (kFrame.withAlpha (0.85f));
    for (auto r : { R (156, 163, 284, 338), R (288, 163, 490, 338), R (494, 163, 562, 207), R (494, 211, 562, 338) })
        g.drawRoundedRectangle (r, 9.0f, 1.2f);

    // headers
    text ("OSC", 220, 167.5, 11.5);
    hline (159, 281, 171.2);  hline (159, 281, 173.2);

    text ("FILTER", 389, 167.5, 11.5);
    hline (291, 487, 171.2);  hline (291, 487, 173.2);

    hline (291, 487, 248.2);  hline (291, 487, 250.2);
    text ("AMP", 389, 253.5, 11.5);
    hline (291, 487, 257.2);

    hline (291, 487, 291.6);  hline (291, 487, 293.6);
    text ("LFO", 389, 297.5, 11.5);
    hline (291, 487, 301.0);

    text ("VOLUME", 528, 167.5, 11.5);
    hline (497, 559, 171.2);  hline (497, 559, 173.2);

    text ("EFFECTS", 528, 215.0, 11.5);
    hline (497, 559, 218.7);  hline (497, 559, 220.7);

    // OSC channel strips
    static const double sliderX[4] = { 171.7, 203.2, 235.0, 266.8 };
    hline (159, 281, 221.5);
    hline (159, 281, 229.0);
    for (int i = 0; i < 4; ++i)
        text (String (i + 1), sliderX[i], 225.3, 10.5);
    for (double x : { 187.4, 219.1, 250.9 })
        vline (x, 221.5, 336.0);

    // filter-type LEDs and icons
    const double ledY[3] = { 182.3, 192.3, 201.3 };
    for (int i = 0; i < 3; ++i)
    {
        const bool lit = i == 0;
        const Point<float> c (sx (357.7), sy (ledY[i]));
        if (lit)
        {
            g.setColour (Colour (0xffff7a2a).withAlpha (0.30f));
            g.fillEllipse (Rectangle<float> (16.0f, 16.0f).withCentre (c));
        }
        g.setColour (lit ? Colour (0xffffb36b) : Colour (0xff7a2430));
        g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre (c));
        g.setColour (kInk.withAlpha (0.85f));
        const float ix = sx (366.5);
        Path ic;
        if (i == 0)      { ic.startNewSubPath (ix, c.y - 3); ic.lineTo (ix + 5, c.y - 3); ic.quadraticTo (ix + 9, c.y - 3, ix + 10, c.y + 3); }
        else if (i == 1) { ic.startNewSubPath (ix, c.y + 3); ic.quadraticTo (ix + 5, c.y - 6, ix + 10, c.y + 3); }
        else             { ic.startNewSubPath (ix, c.y + 3); ic.quadraticTo (ix + 2, c.y - 3, ix + 6, c.y - 3); ic.lineTo (ix + 10, c.y - 3); }
        g.strokePath (ic, PathStrokeType (1.2f));
    }

    // wordmark (the product name only - no manufacturer logo artwork)
    text ("SYNTHESIZER SH-4d", 38, 175.5, 15.0f, Justification::centredLeft);
    text ("EDITOR", 38, 160.0, 10.0f, Justification::centredLeft, 0.55f);

    // control section prints
    text ("SHIFT", 51.7, 268.3, 9.5);   text ("SOUND", 134.0, 268.3, 9.5);
    text ("EXIT", 51.7, 315.0, 9.5);    text ("ENTER", 134.0, 315.0, 9.5);
    text ("1", 62.7, 265.0, 9.0);       text ("2", 122.3, 265.0, 9.0);

    // cursor cluster ring and chevrons
    g.setColour (kFrame.withAlpha (0.8f));
    g.drawEllipse (Rectangle<float> (42.0f * (float) kS, 42.0f * (float) kS).withCentre ({ sx (92.7), sy (300.0) }), 1.2f);

    auto chevron = [&g] (double x, double y, int dir)      // 0 up, 1 left, 2 right, 3 down
    {
        const float cx = sx (x), cy = sy (y), s = 4.5f;
        Path p;
        if (dir == 0)      { p.startNewSubPath (cx - s, cy + s * 0.5f); p.lineTo (cx, cy - s * 0.5f); p.lineTo (cx + s, cy + s * 0.5f); }
        else if (dir == 3) { p.startNewSubPath (cx - s, cy - s * 0.5f); p.lineTo (cx, cy + s * 0.5f); p.lineTo (cx + s, cy - s * 0.5f); }
        else if (dir == 1) { p.startNewSubPath (cx + s * 0.5f, cy - s); p.lineTo (cx - s * 0.5f, cy); p.lineTo (cx + s * 0.5f, cy + s); }
        else               { p.startNewSubPath (cx - s * 0.5f, cy - s); p.lineTo (cx + s * 0.5f, cy); p.lineTo (cx - s * 0.5f, cy + s); }
        g.setColour (kInk);
        g.strokePath (p, PathStrokeType (1.4f));
    };
    chevron (92.7, 268.3, 0);  chevron (62.7, 300.0, 1);  chevron (123.0, 300.0, 2);  chevron (92.7, 331.0, 3);

    // effects button prints
    text ("TONE", 504.0, 309.3, 9.0);  text ("MFX", 527.7, 309.3, 9.0);  text ("ON", 550.7, 309.3, 9.0);
    hline (533.5, 545.0, 309.3);
    text ("REVERB", 504.0, 333.0, 9.0);  text ("CHORUS", 527.7, 333.0, 9.0);  text ("DELAY", 550.7, 333.0, 9.0);

    // sequencer row prints
    text ("START", 51.7, 343.0, 9.0);  text ("PATTERN", 75.0, 343.0, 9.0);
    for (int i = 0; i < 16; ++i)
        text (String (i + 1), 104.0 + 27.6 * i, 343.3, 9.0);
    text ("PAGE/TIE", 545.7, 343.0, 9.0);

    // printed function bar under the step buttons
    g.setColour (kFrame.withAlpha (0.85f));
    g.drawRoundedRectangle (R (36.5, 364.2, 562.5, 373.4), 4.5f, 1.0f);

    static const char* fn[16] = { "PART1", "PART2", "PART3", "PART4", "RHYTHM", "TONE", "MATRIX", "LFO",
                                  "EQ", "INIT", "TEMPO", "UNDO", "COPY", "UTILITY", "CLEAR", "MENU" };
    text ("REC", 51.7, 368.8, 8.5);  text ("EDIT", 75.0, 368.8, 8.5);
    for (int i = 0; i < 16; ++i)
        text (fn[i], 104.0 + 27.6 * i, 368.8, 8.5);
    text ("WRITE", 545.7, 368.8, 8.5);

    for (double x : { 89.5, 228.0, 365.7, 504.7, 532.0 })
        vline (x, 364.6, 373.0);

    // keyboard-row prints
    text ("-  OCTAVE  +", 63.0, 379.3, 8.5);
    text ("-  PITCH  +", 63.7, 408.0, 8.5);
    text ("ARPEGGIO", 490.0, 379.3, 8.5);  text ("HOLD", 518.3, 379.3, 8.5);  text ("D-MOTION", 545.7, 379.3, 8.5);
}
