#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Colour and type for the pedal face. One place, so the enclosure, the knobs
// and the switches cannot drift apart.
namespace PedalTheme
{
    const juce::Colour enclosure      { 0xff1b2430 };
    const juce::Colour enclosureEdge  { 0xff0d1219 };
    const juce::Colour plate          { 0xff222d3b };
    const juce::Colour accent         { 0xffe0a44c };
    const juce::Colour accentDim      { 0xff8c6530 };
    const juce::Colour knobBody       { 0xff2f3a49 };
    const juce::Colour knobEdge       { 0xff10161e };
    const juce::Colour knobCap        { 0xff3c4959 };
    const juce::Colour pointer        { 0xfff2f4f7 };
    const juce::Colour text           { 0xffc8d0da };
    const juce::Colour textDim        { 0xff7d8794 };
    const juce::Colour ledOn          { 0xffff5b3d };
    const juce::Colour ledOff         { 0xff3a2320 };

    juce::Font labelFont (float height);
    juce::Font titleFont (float height);
}

// Rotary knobs are drawn here rather than in each component so every knob on
// the face is identical: same body, same pointer, same arc.
class PedalLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PedalLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;
};
