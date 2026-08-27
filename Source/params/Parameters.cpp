#include "Parameters.h"

#include "dsp/PedalRegistry.h"

namespace
{
    using Range = juce::NormalisableRange<float>;

    auto dbAttributes()
    {
        return juce::AudioParameterFloatAttributes().withLabel ("dB");
    }
}

std::unique_ptr<juce::AudioParameterFloat> Params::gainParam (const char* id, const char* name,
                                                              float min, float max, float def)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, Range { min, max, 0.1f }, def, dbAttributes());
}

// Displayed the way a pedal knob is marked, 0 to 10, centre-detented at 5.
std::unique_ptr<juce::AudioParameterFloat> Params::knobParam (const char* id, const char* name)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, Range { 0.0f, 1.0f, 0.001f }, 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return juce::String (v * 10.0f, 1); }));
}

std::unique_ptr<juce::AudioParameterFloat> Params::percentParam (const char* id, const char* name,
                                                                 float def)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, Range { 0.0f, 1.0f, 0.001f }, def,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int)
                                          { return juce::String (juce::roundToInt (v * 100.0f)); }));
}

juce::AudioProcessorValueTreeState::ParameterLayout Params::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Each pedal contributes its own controls, so a new pedal cannot be added
    // and then quietly forgotten here.
    PedalRegistry::addAllParameters (layout);

    // Declared here rather than in a pedal because it bypasses the whole rig,
    // and so the host gets a proper bypass with correct latency compensation
    // rather than the plugin faking it internally.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::bypass, 1 }, "Bypass", false));

    return layout;
}
