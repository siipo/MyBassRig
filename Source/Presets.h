#pragma once

#include "params/Parameters.h"

#include <chowdsp_presets/chowdsp_presets.h>

namespace Presets
{
    inline constexpr auto vendor = "BassRig";

    // A preset is a complete APVTS state, so a factory preset only has to name
    // the parameters it cares about; everything else takes its default.
    using Setting = std::pair<const char*, float>;

    chowdsp::Preset make (const juce::String& name,
                          const juce::String& category,
                          const juce::AudioProcessor& processor,
                          std::initializer_list<Setting> settings,
                          const std::vector<int>& chainOrder = {});

    std::vector<chowdsp::Preset> createFactoryPresets (const juce::AudioProcessor& processor);

    // The preset the plugin opens on. It carries no overrides at all, so it is
    // exactly the parameter defaults -- which means the name box agrees with
    // what a knob does when you double click it to reset.
    inline constexpr auto defaultPresetName = "Default";

    chowdsp::Preset makeDefault (const juce::AudioProcessor& processor);
}
