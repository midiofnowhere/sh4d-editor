#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>
#include "PluginProcessor.h"
#include "PatchLibrary.h"

//==============================================================================
class SH4dLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static inline const juce::Colour bg      { 0xff14161a };
    static inline const juce::Colour panel   { 0xff1f232a };
    static inline const juce::Colour track   { 0xff39404d };
    static inline const juce::Colour accent  { 0xffff6a2b };
    static inline const juce::Colour mod     { 0xff4fd1c5 };
    static inline const juce::Colour text    { 0xffe6e9ef };
    static inline const juce::Colour textDim { 0xff9aa3b2 };

    SH4dLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

//==============================================================================
// Rotary knob with label, value box, right-click menu, modulation ring and learn highlight.
class KnobComponent : public juce::Component
{
public:
    explicit KnobComponent (const juce::String& title);

    void attach (juce::RangedAudioParameter&, juce::UndoManager* um = nullptr);
    void detach() { attachment.reset(); }
    void setLearning (bool);
    void setEffective (float norm);              // < 0 = no modulation ring
    juce::Slider& getSlider() { return slider; }
    std::function<void()> onContext;

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Label  label;
    juce::Slider slider;
    bool learning = false;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;   // must die before 'slider'
};

class SwitchComponent : public juce::Component
{
public:
    explicit SwitchComponent (const juce::String& title);
    void attach (juce::RangedAudioParameter&, juce::UndoManager* um = nullptr);
    void detach() { attachment.reset(); }
    std::function<void()> onContext;

    void resized() override { button.setBounds (getLocalBounds().reduced (3)); }
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::ToggleButton button;
    std::unique_ptr<juce::ButtonParameterAttachment> attachment;
};

class XYPad : public juce::Component
{
public:
    std::function<void (float, float)> onMove;
    std::function<void (bool)>         onGesture;   // true = begin, false = end

    void setPosition (float x, float y);
    bool isDragging() const { return dragging; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void update (const juce::MouseEvent&);
    float px = 0.5f, py = 0.5f;
    bool dragging = false;
};

// Draggable ADSR graph bound to four parameters.
class EnvelopeView : public juce::Component
{
public:
    void setParams (juce::RangedAudioParameter* a, juce::RangedAudioParameter* d,
                    juce::RangedAudioParameter* s, juce::RangedAudioParameter* r);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Geo { juce::Point<float> p0, p1, p2, p3, p4; float unit = 1.0f; juce::Rectangle<float> area; };
    Geo geometry() const;
    float val (int k) const { return prm[(size_t) k] != nullptr ? prm[(size_t) k]->getValue() : 0.0f; }
    void setNorm (int k, float n);

    std::array<juce::RangedAudioParameter*, 4> prm {};
    int dragging = -1;
};

class MeterBar : public juce::Component
{
public:
    void setValue (float v, bool bipolar);
    void paint (juce::Graphics&) override;

private:
    float value = 0.0f;
    bool bi = false;
};

//==============================================================================
struct PanelRect { juce::Rectangle<int> bounds; juce::String title; };

class Page : public juce::Component
{
public:
    ~Page() override = default;
    virtual void partChanged() {}
    virtual void tick() {}
};


void showParamContextMenu (SH4dProcessor&, juce::Component* target, int paramIndex, int part, int ccIndex);

//==============================================================================
// Skeuomorphic hardware widgets for the PANEL page.

// Black rotary with printed tick scale. Controls that have no MIDI CC on the SH-4d are "inert".
class PanelKnob : public juce::Component
{
public:
    PanelKnob (const juce::String& label, bool inert = false, bool bipolar = false);

    void attach (juce::RangedAudioParameter&, juce::UndoManager*);
    void detach();
    void setEffective (float norm);                  // < 0 = no modulation ring
    void setRadius (float r)                  { radius = r; repaint(); }

    bool isInert() const                      { return inert; }
    const juce::String& getLabel() const      { return label; }
    float getNorm() const                     { return norm; }
    juce::String getValueText() const;

    std::function<void (PanelKnob&)> onTouch;
    std::function<void()> onContext;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::String label;
    bool inert, bipolar;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    float norm = 0.0f, effNorm = -1.0f, dragStartNorm = 0.0f, radius = 24.0f;
    bool dragging = false;
};

// Vertical fader with scale and a black cap.
class PanelSlider : public juce::Component
{
public:
    explicit PanelSlider (const juce::String& label);

    void attach (juce::RangedAudioParameter&, juce::UndoManager*);
    void detach();

    const juce::String& getLabel() const   { return label; }
    float getNorm() const                  { return norm; }
    juce::String getValueText() const;

    std::function<void (PanelSlider&)> onTouch;
    std::function<void()> onContext;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    float yToNorm (float y) const;
    juce::Rectangle<float> track() const;

    juce::String label;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    float norm = 0.0f;
    bool dragging = false;
};

// Small rubber button with an LED, a printed label above and/or below.
class PanelButton : public juce::Component
{
public:
    enum class Face { None, Up, Down, Left, Right };

    PanelButton (const juce::String& topLabel = {}, const juce::String& bottomLabel = {}, Face face = Face::None,
                 const juce::String& faceText = {});

    void setLed (bool on, juce::Colour c = juce::Colour (0xffffa31a));
    bool getLed() const { return ledOn; }
    void setRing (bool r) { ring = r; repaint(); }          // ring-style button (D-MOTION)

    std::function<void()> onClick, onDown, onUp, onContext;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String topLabel, bottomLabel, faceText;
    Face face;
    bool ledOn = false, pressed = false, ring = false;
    juce::Colour ledColour { 0xffffa31a };
};

// The colour LCD: header, title, big value and a bar.
class LcdDisplay : public juce::Component
{
public:
    struct State
    {
        juce::String header, title, value, footer;
        float bar = -1.0f;
        bool idle = false;             // true = the default oscillator screen
        bool operator== (const State& o) const
        {
            return idle == o.idle && header == o.header && title == o.title && value == o.value && footer == o.footer && juce::approximatelyEqual (bar, o.bar);
        }
    };

    void setState (const State& s) { if (! (s == state)) { state = s; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    State state;
};

//==============================================================================
class PanelPage : public Page
{
public:
    explicit PanelPage (SH4dProcessor&);
    ~PanelPage() override;

    void partChanged() override;
    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (int)> requestPage;      // 0 panel, 1 sound, 2 mod, 3 library, 4 setup

private:
    PanelKnob* addKnob (const juce::String& label, int cc, bool bipolar = false);
    PanelButton* addButton (const juce::String& top, const juce::String& bottom, PanelButton::Face f = PanelButton::Face::None,
                            const juce::String& faceText = {});
    void inert (PanelButton&, const juce::String& what);
    void showMessage (const juce::String& text);
    void stepPressed (int n);
    void toggleSwitch (int cc);
    void keyEvent (int keyIndex, bool down);
    void bend (int value14);
    int  noteForKey (int keyIndex) const { return 48 + 12 * octave + keyOffsets[(size_t) keyIndex]; }

    SH4dProcessor& proc;
    int part = 0;
    int octave = 0;
    bool shiftArmed = false;
    juce::String message;
    juce::uint32 messageUntil = 0;

    struct Touched { PanelKnob* knob = nullptr; PanelSlider* slider = nullptr; juce::String name; } touched;

    juce::OwnedArray<PanelKnob>   knobs;
    juce::Array<int>              knobCc;
    juce::OwnedArray<PanelSlider> sliders;
    juce::OwnedArray<PanelButton> buttons;

    // named references into the lists above (laid out in resized())
    std::vector<PanelKnob*> filterRow1, filterRow2, ampRow, lfoRow, oscKnobs, encoders, fxKnobs;
    PanelKnob* volumeKnob = nullptr;
    PanelButton *soundBtn = nullptr, *shiftBtn = nullptr, *upBtn = nullptr, *downBtn = nullptr, *leftBtn = nullptr,
                *rightBtn = nullptr, *enterBtn = nullptr, *exitBtn = nullptr;
    std::vector<PanelButton*> oscButtons, fxButtons, stepButtons, keyButtons;
    std::vector<int> keyOffsets;            // semitones above the octave's C, per key button
    PanelButton *startBtn = nullptr, *patternBtn = nullptr, *pageBtn = nullptr, *dmotionBtn = nullptr,
                *octDown = nullptr, *octUp = nullptr, *pitchDown = nullptr, *pitchUp = nullptr, *arpBtn = nullptr, *holdBtn = nullptr;

    LcdDisplay lcd;
    int originY = 0;
};

//==============================================================================
class SoundPage : public Page
{
public:
    explicit SoundPage (SH4dProcessor&);
    ~SoundPage() override;

    void partChanged() override;
    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void placeGroup (sh4d::Group, juce::Rectangle<int>, int columns, int reserveBottom = 0);
    void randomise();
    void loadProgramFields();
    void sendCurrentProgram();
    void stepProgram (int delta);

    SH4dProcessor& proc;
    int part = 0;

    std::array<std::unique_ptr<KnobComponent>,   (size_t) sh4d::kNumCc> knobs;
    std::array<std::unique_ptr<SwitchComponent>, (size_t) sh4d::kNumCc> switches;
    EnvelopeView filterEnv, ampEnv;

    juce::Label  msbLabel, lsbLabel, pcLabel;
    juce::Slider msbSlider, lsbSlider, pcSlider;
    juce::TextButton sendProgButton, prevButton, nextButton;

    juce::TextButton randomButton, resetButton;
    juce::Label  amountLabel, lockCaption, rhythmNote;
    juce::Slider amountSlider;
    std::unique_ptr<juce::SliderParameterAttachment> amountAttachment;
    std::array<juce::ToggleButton, 5> lockButtons;
    std::array<std::unique_ptr<juce::ButtonParameterAttachment>, 5> lockAttachments;

    std::vector<PanelRect> panels;
};

//==============================================================================
class RouteRow : public juce::Component
{
public:
    RouteRow (const sh4d::Route&, std::function<void (RouteRow&)> onChange, std::function<void (RouteRow&)> onRemove);
    sh4d::Route getRoute() const;
    void resized() override;

private:
    juce::ComboBox srcBox, partBox, paramBox;
    juce::Slider depth;
    juce::TextButton removeButton;
    std::function<void (RouteRow&)> changed;
};

class ModPage : public Page
{
public:
    explicit ModPage (SH4dProcessor&);
    ~ModPage() override;

    void partChanged() override;
    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class RoutesList : public juce::Component
    {
    public:
        void layoutRows();
        juce::OwnedArray<RouteRow> rows;
    };

    void rebuildRoutes();
    void commitRow (RouteRow&);
    void removeRow (RouteRow&);
    void rebuildXyBoxes();

    SH4dProcessor& proc;
    int part = 0;

    std::array<std::unique_ptr<KnobComponent>, (size_t) sh4d::kNumMacros> macros;

    struct LfoUi
    {
        juce::ComboBox shape, sync;
        std::unique_ptr<juce::ComboBoxParameterAttachment> shapeAtt, syncAtt;
        std::unique_ptr<KnobComponent> rate;
        MeterBar meter;
    };
    std::array<LfoUi, (size_t) sh4d::kNumLfos> lfos;
    MeterBar aftertouchMeter, pitchBendMeter;
    juce::Label aftertouchLabel, pitchBendLabel, dmotionNote;

    juce::Viewport routesView;
    RoutesList routesList;
    juce::TextButton addRouteButton;
    int seenRoutesVersion = -1;

    XYPad xyPad;
    juce::ComboBox xBox, yBox;
    juce::Label xLabel, yLabel;
    int xyX = 0, xyY = 0;

    std::vector<PanelRect> panels;
};

//==============================================================================
class LibraryPage : public Page, private juce::ListBoxModel
{
public:
    explicit LibraryPage (SH4dProcessor&);
    ~LibraryPage() override;

    void partChanged() override;
    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { loadRow (row); }
    void selectedRowsChanged (int row) override;

    void refreshList();
    void saveCurrent();
    void loadRow (int row);
    void deleteRow (int row);
    void switchAB (int target);

    SH4dProcessor& proc;
    int part = 0;

    juce::TextEditor searchBox, nameBox, categoryBox;
    juce::ListBox list { "patches", this };
    juce::Array<PatchInfo> all, shown;
    juce::TextButton saveButton, loadButton, deleteButton, refreshButton, openFolderButton;
    juce::ToggleButton sendProgToggle;
    juce::Label statusLabel;

    std::array<juce::TextButton, (size_t) sh4d::kNumScenes> sceneButtons;
    juce::ToggleButton storeMode;
    juce::ComboBox morphA, morphB;
    juce::Label morphLabel, morphALabel, morphBLabel;
    juce::Slider morphSlider;
    std::unique_ptr<juce::SliderParameterAttachment> morphAttachment;

    juce::TextButton abButtons[2];
    juce::TextButton copyButtons[sh4d::kNumSynthParts];
    juce::Label copyLabel, folderLabel;
    SH4dProcessor::PartValues abSlot[2] {};
    bool abValid[2] { false, false };
    int abCurrent = 0;

    std::vector<PanelRect> panels;
};

//==============================================================================
class SetupPage : public Page, private juce::ListBoxModel
{
public:
    explicit SetupPage (SH4dProcessor&);
    ~SetupPage() override;

    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return (int) maps.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;

    void rebuildDeviceLists();
    void fillBox (juce::ComboBox&, const juce::StringArray& names, const juce::String& wanted);
    void rebuildLearnList();

    SH4dProcessor& proc;

    juce::ComboBox hwInBox, hwOutBox, ctlInBox, hostOutBox;
    juce::Label hwInLabel, hwOutLabel, ctlInLabel, hostOutLabel;
    std::unique_ptr<juce::ComboBoxParameterAttachment> hostOutAtt;
    juce::TextButton autoButton, refreshButton;
    juce::ToggleButton sendOnConnect, remapCtl, ccThru;
    std::unique_ptr<juce::ButtonParameterAttachment> sendAtt, remapAtt, thruAtt;

    std::array<juce::Label,  (size_t) sh4d::kNumParts> chLabels;
    std::array<juce::Slider, (size_t) sh4d::kNumParts> chSliders;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, (size_t) sh4d::kNumParts> chAtts;

    juce::ListBox learnList { "learn", this };
    std::vector<sh4d::LearnMap> maps;
    juce::TextButton clearSelected, clearAll;
    juce::Label learnStatus, helpText;
    int seenLearnVersion = -1;
    int tickCount = 0;
    juce::StringArray lastInNames, lastOutNames;
    bool lastLed[3] { false, false, false };

    juce::Rectangle<int> ledHwIn, ledHwOut, ledCtl;
    std::vector<PanelRect> panels;
};

//==============================================================================
class SH4dEditor;

class MainContent : public juce::Component,
                    private juce::MidiKeyboardStateListener,
                    private juce::Timer
{
public:
    static constexpr int kWidth = 1020, kHeight = 790;

    explicit MainContent (SH4dProcessor&);
    ~MainContent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float velocity) override;

    void selectPage (int);
    void selectPart (int);

    SH4dProcessor& proc;
    SH4dLookAndFeel laf;

    std::array<juce::TextButton, (size_t) sh4d::kNumParts> partButtons;
    std::array<juce::TextButton, 5> tabButtons;
    juce::TextButton sendAllButton, panicButton, undoButton, redoButton;
    juce::Label monitor, status;

    std::unique_ptr<PanelPage>   panelPage;
    std::unique_ptr<SoundPage>   soundPage;
    std::unique_ptr<ModPage>     modPage;
    std::unique_ptr<LibraryPage> libraryPage;
    std::unique_ptr<SetupPage>   setupPage;
    std::array<Page*, 5> pages {};

    juce::MidiKeyboardState kbState;
    juce::MidiKeyboardComponent keyboard { kbState, juce::MidiKeyboardComponent::horizontalKeyboard };

    int page = 0;
    int shownPart = -1;
};

//==============================================================================
class SH4dEditor : public juce::AudioProcessorEditor
{
public:
    explicit SH4dEditor (SH4dProcessor&);
    ~SH4dEditor() override;

    void resized() override;

private:
    MainContent content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SH4dEditor)
};
