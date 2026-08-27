#pragma once

#include "dsp/Pedal.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <vector>

// The pedals in the rig, in signal order.
//
// Every pedal in the list is instantiated and runs on every block. There is
// deliberately no mechanism for swapping a pedal at runtime: that would need
// either a lock shared with the audio callback or a lock-free handover, and a
// rig does not need one. A pedal that should be out of circuit has its own
// bypass, which costs an atomic load.
//
// Parameters come through here too, so adding a pedal really is a header, a
// source file and one line in getEntries() -- the parameter layout follows
// automatically rather than having to be remembered separately.
namespace PedalRegistry
{
    struct Entry
    {
        const char* id;
        const char* displayName;
        std::function<std::unique_ptr<Pedal> (juce::AudioProcessorValueTreeState&)> create;
        void (*addParameters) (juce::AudioProcessorValueTreeState::ParameterLayout&);
    };

    const std::vector<Entry>& getEntries();

    std::unique_ptr<Pedal> create (int index, juce::AudioProcessorValueTreeState& state);

    // The whole rig, in signal order.
    std::vector<std::unique_ptr<Pedal>> createChain (juce::AudioProcessorValueTreeState& state);

    // Every pedal's parameters, aggregated.
    void addAllParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
}
