#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>
#include "PluginProcessor.h"

//==============================================================================
class SH4dLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static inline const juce::Colour bg      { 0xff14161a };
    static inline const juce::Colour panel   { 0xff1f232a };
    static inline const juce::Colour track   { 0xff39404d };
    static inline const juce::Colour accent  { 0xffff6a2b };
    static inline const juce::Colour text    { 0xffe6e9ef };
    static inline const juce::Colour textDim { 0xff9aa3b2 };

    SH4dLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

//==============================================================================
class KnobComponent : public juce::Component
{
public:
    explicit KnobComponent (const juce::String& title);
    void attach (juce::RangedAudioParameter&);
    void detach() { attachment.reset(); }
    void resized() override;

private:
    juce::Label  label;
    juce::Slider slider;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;   // must die before 'slider'
};

//==============================================================================
class SwitchComponent : public juce::Component
{
public:
    explicit SwitchComponent (const juce::String& title);
    void attach (juce::RangedAudioParameter&);
    void detach() { attachment.reset(); }
    void resized() override { button.setBounds (getLocalBounds().reduced (3)); }

private:
    juce::ToggleButton button;
    std::unique_ptr<juce::ButtonParameterAttachment> attachment;
};

//==============================================================================
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

//==============================================================================
class SH4dEditor : public juce::AudioProcessorEditor,
                   private juce::Timer
{
public:
    explicit SH4dEditor (SH4dProcessor&);
    ~SH4dEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void setPart (int newPart);
    void rebuildXyBoxes();
    void randomise();
    void resetPart();

    int  getProp (const juce::String& key, int def) const;
    void setProp (const juce::String& key, int value);
    void loadProgramFields();
    void sendCurrentProgram();
    void stepProgram (int delta);

    void placeGroup (sh4d::Group g, juce::Rectangle<int> panelBounds, int columns, int reserveBottom = 0);

    struct Panel { juce::Rectangle<int> bounds; juce::String title; bool synthOnly; };

    SH4dProcessor& proc;
    SH4dLookAndFeel laf;

    int part = 0;

    std::array<std::unique_ptr<KnobComponent>,   (size_t) sh4d::kNumCc> knobs;
    std::array<std::unique_ptr<SwitchComponent>, (size_t) sh4d::kNumCc> switches;

    // header
    std::array<juce::TextButton, (size_t) sh4d::kNumParts> partButtons;
    juce::Label  channelLabel;
    juce::Slider channelSlider;
    std::unique_ptr<juce::SliderParameterAttachment> channelAttachment;
    juce::ToggleButton thruButton;
    std::unique_ptr<juce::ButtonParameterAttachment> thruAttachment;
    juce::TextButton sendAllButton, panicButton;

    // XY
    XYPad xyPad;
    juce::ComboBox xBox, yBox;
    juce::Label xLabel, yLabel;
    int xyX = 0, xyY = 0;   // indices into sh4d::kCc

    // program select
    juce::Label  msbLabel, lsbLabel, pcLabel;
    juce::Slider msbSlider, lsbSlider, pcSlider;
    juce::TextButton sendProgButton, prevButton, nextButton;

    // randomiser
    juce::TextButton randomButton, resetButton;
    juce::Label  amountLabel;
    juce::Slider amountSlider;
    std::unique_ptr<juce::SliderParameterAttachment> amountAttachment;
    std::array<juce::ToggleButton, 5> lockButtons;
    std::array<std::unique_ptr<juce::ButtonParameterAttachment>, 5> lockAttachments;

    juce::Label lockCaption;
    juce::Label rhythmNote;
    juce::Label monitor;

    std::vector<Panel> panels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SH4dEditor)
};
