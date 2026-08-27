#pragma once

#include "PedalLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

// A knob plus its engraved caption and value readout.
class KnobControl final : public juce::Component
{
public:
    // Where the value arc grows from. A drive or blend knob reads naturally
    // from zero; a gain or EQ knob whose neutral point is 0 dB reads as a
    // deviation from that point, so its arc starts at the default.
    enum class Arc { fromMinimum, fromDefault };

    KnobControl (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                 const juce::String& caption, Arc arc = Arc::fromMinimum);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::String caption;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobControl)
};

// Three-position toggle, a physical lever on the real pedal. Clicking picks the
// nearest detent rather than cycling, so any position is one click away instead
// of up to three.
class ThreeWaySwitch final : public juce::Component
{
public:
    ThreeWaySwitch (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                    const juce::String& caption);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String caption;
    juce::StringArray choices;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int index = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThreeWaySwitch)
};

// A small labelled on/off, for a section that can be taken out of circuit
// without a footswitch of its own.
class SectionToggle final : public juce::Component
{
public:
    SectionToggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                   const juce::String& caption);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String caption;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    bool on = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionToggle)
};

// The stomp switch and its indicator. The LED is lit when the pedal is ENGAGED,
// which is the opposite of what the bypass parameter stores.
class Footswitch final : public juce::Component
{
public:
    Footswitch (juce::AudioProcessorValueTreeState& state, const juce::String& paramID);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    std::unique_ptr<juce::ParameterAttachment> attachment;
    bool bypassed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Footswitch)
};
