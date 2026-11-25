#include "PluginEditor.h"

using namespace juce;
using namespace sh4d;

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

    addAndMakeVisible (label);
    addAndMakeVisible (slider);
}

void KnobComponent::attach (RangedAudioParameter& p)
{
    attachment.reset();
    attachment = std::make_unique<SliderParameterAttachment> (p, slider, nullptr);
}

void KnobComponent::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

//==============================================================================
SwitchComponent::SwitchComponent (const String& title)
{
    button.setButtonText (title);
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);
}

void SwitchComponent::attach (RangedAudioParameter& p)
{
    attachment.reset();
    attachment = std::make_unique<ButtonParameterAttachment> (p, button, nullptr);
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

        if (type == 1)
            return "ch" + String (ch) + " PC " + String (d1);

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
}

//==============================================================================
SH4dEditor::SH4dEditor (SH4dProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    auto setupLabel = [this] (Label& l, const String& t, Justification j = Justification::centredLeft)
    {
        l.setText (t, dontSendNotification);
        l.setJustificationType (j);
        l.setColour (Label::textColourId, SH4dLookAndFeel::textDim);
        l.setFont (FontOptions (13.0f));
        addAndMakeVisible (l);
    };

    // knobs & switches
    for (int i = 0; i < kNumCc; ++i)
    {
        if (kCc[i].kind == Kind::Knob)
        {
            knobs[(size_t) i] = std::make_unique<KnobComponent> (kCc[i].shortName);
            addAndMakeVisible (*knobs[(size_t) i]);
        }
        else
        {
            switches[(size_t) i] = std::make_unique<SwitchComponent> (kCc[i].shortName);
            addAndMakeVisible (*switches[(size_t) i]);
        }
    }

    // header
    for (int i = 0; i < kNumParts; ++i)
    {
        auto& b = partButtons[(size_t) i];
        b.setButtonText (i < kNumSynthParts ? "PART " + String (i + 1) : String ("RHYTHM"));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (100);
        b.onClick = [this, i] { if (partButtons[(size_t) i].getToggleState()) setPart (i); };
        addAndMakeVisible (b);
    }

    setupLabel (channelLabel, "CH", Justification::centredRight);
    addAndMakeVisible (channelSlider);
    channelSlider.setSliderStyle (Slider::IncDecButtons);
    channelSlider.setTextBoxStyle (Slider::TextBoxLeft, false, 44, 26);

    thruButton.setButtonText ("CC Thru");
    thruButton.setTooltip ("Forward incoming CCs to the output instead of only following them");
    addAndMakeVisible (thruButton);
    thruAttachment = std::make_unique<ButtonParameterAttachment> (*proc.apvts.getParameter ("ccThru"), thruButton, nullptr);

    sendAllButton.setButtonText ("Send All");
    sendAllButton.setTooltip ("Push every value of all four synth parts to the SH-4d");
    sendAllButton.onClick = [this] { proc.requestSendAll(); };
    addAndMakeVisible (sendAllButton);

    panicButton.setButtonText ("Panic");
    panicButton.onClick = [this] { proc.sendPanic (part); };
    addAndMakeVisible (panicButton);

    // XY pad
    xyX = jlimit (0, kNumCc - 1, getProp ("xyX", indexOfCc (74)));
    xyY = jlimit (0, kNumCc - 1, getProp ("xyY", indexOfCc (71)));

    addAndMakeVisible (xyPad);
    setupLabel (xLabel, "X");
    setupLabel (yLabel, "Y");
    addAndMakeVisible (xBox);
    addAndMakeVisible (yBox);
    xBox.onChange = [this] { xyX = jmax (0, xBox.getSelectedId() - 1); setProp ("xyX", xyX); };
    yBox.onChange = [this] { xyY = jmax (0, yBox.getSelectedId() - 1); setProp ("xyY", xyY); };

    xyPad.onGesture = [this] (bool begin)
    {
        if (part >= kNumSynthParts) return;

        for (int idx : { xyX, xyY })
        {
            if (idx == xyY && xyY == xyX) break;   // same param on both axes: one gesture only
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

    // program select
    setupLabel (msbLabel, "Bank MSB");
    setupLabel (lsbLabel, "Bank LSB");
    setupLabel (pcLabel,  "Program");

    for (auto* s : { &msbSlider, &lsbSlider, &pcSlider })
    {
        s->setSliderStyle (Slider::IncDecButtons);
        s->setTextBoxStyle (Slider::TextBoxLeft, false, 44, 24);
        s->setRange (0.0, 127.0, 1.0);
        addAndMakeVisible (*s);
    }

    msbSlider.onValueChange = [this] { setProp ("msb" + String (part), (int) msbSlider.getValue()); };
    lsbSlider.onValueChange = [this] { setProp ("lsb" + String (part), (int) lsbSlider.getValue()); };
    pcSlider.onValueChange  = [this] { setProp ("pc"  + String (part), (int) pcSlider.getValue()); };

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
    resetButton.onClick = [this] { resetPart(); };
    addAndMakeVisible (randomButton);
    addAndMakeVisible (resetButton);

    setupLabel (amountLabel, "Amount");
    amountSlider.setSliderStyle (Slider::LinearHorizontal);
    amountSlider.setTextBoxStyle (Slider::TextBoxRight, false, 44, 24);
    addAndMakeVisible (amountSlider);
    amountAttachment = std::make_unique<SliderParameterAttachment> (*proc.apvts.getParameter ("randAmount"), amountSlider, nullptr);

    setupLabel (lockCaption, "Locked groups are left alone:");
    static const char* lockText[5] = { "Amp", "Filter", "LFO", "Pitch", "Sliders" };
    for (size_t g = 0; g < 5; ++g)
    {
        lockButtons[g].setButtonText (lockText[g]);
        addAndMakeVisible (lockButtons[g]);
        lockAttachments[g] = std::make_unique<ButtonParameterAttachment> (*proc.apvts.getParameter (kLockIds[g]), lockButtons[g], nullptr);
    }

    setupLabel (rhythmNote, "The rhythm part has no CC parameters on the SH-4d.\nUse Program Select below to pick a kit.", Justification::centred);
    rhythmNote.setFont (FontOptions (16.0f));
    addAndMakeVisible (rhythmNote);

    monitor.setFont (FontOptions (12.5f));
    monitor.setColour (Label::textColourId, SH4dLookAndFeel::textDim);
    monitor.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (monitor);

    const int savedPart = jlimit (0, kNumParts - 1, getProp ("part", 0));
    partButtons[(size_t) savedPart].setToggleState (true, dontSendNotification);
    setPart (savedPart);

    sendLookAndFeelChange();   // rebuild slider text boxes with our colours
    setSize (1020, 700);       // last: resized() needs every child to exist
    startTimerHz (20);
}

SH4dEditor::~SH4dEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
int SH4dEditor::getProp (const String& key, int def) const
{
    return (int) proc.apvts.state.getProperty (key, def);
}

void SH4dEditor::setProp (const String& key, int value)
{
    proc.apvts.state.setProperty (key, value, nullptr);
}

void SH4dEditor::setPart (int newPart)
{
    part = jlimit (0, kNumParts - 1, newPart);
    setProp ("part", part);
    const bool synth = part < kNumSynthParts;

    for (int i = 0; i < kNumCc; ++i)
    {
        if (auto* k = knobs[(size_t) i].get())
        {
            if (synth) k->attach (*proc.getCcParam (part, i)); else k->detach();
            k->setVisible (synth);
        }

        if (auto* s = switches[(size_t) i].get())
        {
            if (synth) s->attach (*proc.getCcParam (part, i)); else s->detach();
            s->setVisible (synth);
        }
    }

    channelAttachment.reset();
    channelAttachment = std::make_unique<SliderParameterAttachment> (*proc.getChannelParam (part), channelSlider, nullptr);

    for (Component* c : std::initializer_list<Component*> { &xyPad, &xBox, &yBox, &xLabel, &yLabel,
                                                            &randomButton, &resetButton, &amountLabel,
                                                            &amountSlider, &lockCaption })
        c->setVisible (synth);

    for (auto& b : lockButtons)
        b.setVisible (synth);

    rhythmNote.setVisible (! synth);

    loadProgramFields();
    rebuildXyBoxes();
    repaint();
}

void SH4dEditor::rebuildXyBoxes()
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

void SH4dEditor::loadProgramFields()
{
    const bool synth = part < kNumSynthParts;
    const int defMsb = synth ? kToneBankMsb : kRhythmBankMsb;
    const int defLsb = synth ? kToneBankLsb : kRhythmBankLsb;

    msbSlider.setValue (getProp ("msb" + String (part), defMsb), dontSendNotification);
    lsbSlider.setValue (getProp ("lsb" + String (part), defLsb), dontSendNotification);
    pcSlider.setValue  (getProp ("pc"  + String (part), 0),      dontSendNotification);
}

void SH4dEditor::sendCurrentProgram()
{
    proc.sendProgram (part, (int) msbSlider.getValue(), (int) lsbSlider.getValue(), (int) pcSlider.getValue());
}

void SH4dEditor::stepProgram (int delta)
{
    int pc = (int) pcSlider.getValue() + delta;
    pc = (pc + 128) % 128;
    pcSlider.setValue (pc, sendNotification);   // onValueChange stores it
    sendCurrentProgram();
}

//==============================================================================
void SH4dEditor::randomise()
{
    if (part >= kNumSynthParts)
        return;

    auto& rnd = Random::getSystemRandom();
    const float amount = proc.apvts.getRawParameterValue ("randAmount")->load() / 100.0f;

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

void SH4dEditor::resetPart()
{
    if (part >= kNumSynthParts)
        return;

    for (int i = 0; i < kNumCc; ++i)
    {
        auto* prm = proc.getCcParam (part, i);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
}

//==============================================================================
void SH4dEditor::timerCallback()
{
    if (part < kNumSynthParts && ! xyPad.isDragging())
        xyPad.setPosition ((float) proc.getCcValue (part, xyX) / 127.0f,
                           (float) proc.getCcValue (part, xyY) / 127.0f);

    monitor.setText ("OUT  " + describe (proc.lastTx.load()) + "  [" + String (proc.txCount.load()) + " sent]"
                     + "        IN  " + describe (proc.lastRx.load()) + "  [" + String (proc.rxCount.load()) + " received]",
                     dontSendNotification);
}

//==============================================================================
void SH4dEditor::paint (Graphics& g)
{
    g.fillAll (SH4dLookAndFeel::bg);

    g.setColour (SH4dLookAndFeel::text);
    g.setFont (FontOptions (22.0f, Font::bold));
    g.drawText ("SH-4d EDITOR", 14, 10, 176, 34, Justification::centredLeft);

    for (const auto& p : panels)
    {
        if (p.synthOnly && part >= kNumSynthParts)
            continue;

        auto r = p.bounds.toFloat();
        g.setColour (SH4dLookAndFeel::panel);
        g.fillRoundedRectangle (r, 8.0f);

        g.setColour (SH4dLookAndFeel::accent);
        g.setFont (FontOptions (13.0f, Font::bold));
        g.drawText (p.title.toUpperCase(), p.bounds.getX() + 12, p.bounds.getY() + 4, p.bounds.getWidth() - 24, 20,
                    Justification::centredLeft);
    }
}

void SH4dEditor::placeGroup (Group g, Rectangle<int> panelBounds, int columns, int reserveBottom)
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

void SH4dEditor::resized()
{
    // ---- header
    for (int i = 0; i < kNumParts; ++i)
        partButtons[(size_t) i].setBounds (190 + i * 84, 12, 80, 30);

    channelLabel.setBounds (618, 12, 30, 30);
    channelSlider.setBounds (650, 12, 112, 30);
    thruButton.setBounds (770, 12, 80, 30);
    sendAllButton.setBounds (858, 12, 74, 30);
    panicButton.setBounds (938, 12, 70, 30);

    // ---- panels
    const Rectangle<int> filterR (12, 60, 606, 262), ampR (626, 60, 382, 262),
                         lfoR (12, 330, 356, 176), pitchR (376, 330, 324, 176), sliderR (708, 330, 300, 176),
                         xyR (12, 514, 356, 156), progR (376, 514, 324, 156), randR (708, 514, 300, 156);

    panels.clear();
    panels.push_back ({ filterR, "Filter",  true });
    panels.push_back ({ ampR,    "Amp",     true });
    panels.push_back ({ lfoR,    "LFO",     true });
    panels.push_back ({ pitchR,  "Pitch / Performance", true });
    panels.push_back ({ sliderR, "Sliders", true });
    panels.push_back ({ xyR,     "XY Pad",  true });
    panels.push_back ({ progR,   "Program Select", false });
    panels.push_back ({ randR,   "Randomiser", true });

    placeGroup (Group::Filter, filterR, 5);
    placeGroup (Group::Amp,    ampR,    3);
    placeGroup (Group::Lfo,    lfoR,    5);
    placeGroup (Group::Pitch,  pitchR,  3, 36);
    placeGroup (Group::Slider, sliderR, 4);

    {
        // the three on/off switches sit in a strip under the pitch knobs
        auto strip = pitchR.reduced (10).removeFromBottom (34);
        int n = 0;
        for (int i = 0; i < kNumCc; ++i)
            if (auto* s = switches[(size_t) i].get())
                s->setBounds (strip.getX() + (n++) * (strip.getWidth() / 3), strip.getY(), strip.getWidth() / 3, strip.getHeight());
    }

    rhythmNote.setBounds (12, 60, 996, 446);

    // ---- XY
    xyPad.setBounds (xyR.getX() + 12, xyR.getY() + 30, 118, 118);
    xLabel.setBounds (xyR.getX() + 142, xyR.getY() + 34, 20, 24);
    xBox.setBounds   (xyR.getX() + 166, xyR.getY() + 34, 178, 24);
    yLabel.setBounds (xyR.getX() + 142, xyR.getY() + 70, 20, 24);
    yBox.setBounds   (xyR.getX() + 166, xyR.getY() + 70, 178, 24);

    // ---- program select
    const int px = progR.getX() + 12;
    int py = progR.getY() + 30;
    msbLabel.setBounds (px, py, 80, 24);  msbSlider.setBounds (px + 84, py, 150, 24);  py += 28;
    lsbLabel.setBounds (px, py, 80, 24);  lsbSlider.setBounds (px + 84, py, 150, 24);  py += 28;
    pcLabel.setBounds  (px, py, 80, 24);  pcSlider.setBounds  (px + 84, py, 150, 24);  py += 30;
    sendProgButton.setBounds (px, py, 90, 26);
    prevButton.setBounds (px + 96, py, 40, 26);
    nextButton.setBounds (px + 142, py, 40, 26);

    // ---- randomiser
    const int rx = randR.getX() + 12;
    const int ry = randR.getY() + 28;
    randomButton.setBounds (rx, ry, 132, 26);
    resetButton.setBounds (rx + 140, ry, 136, 26);
    amountLabel.setBounds (rx, ry + 32, 60, 24);
    amountSlider.setBounds (rx + 62, ry + 32, 214, 24);
    lockCaption.setBounds (rx, ry + 62, 276, 18);

    const int lw = 276 / 5;
    for (int g = 0; g < 5; ++g)
        lockButtons[(size_t) g].setBounds (rx + g * lw, ry + 82, lw - 3, 22);

    monitor.setBounds (14, 674, 990, 20);
}
