#pragma once

#include <juce_dsp/juce_dsp.h>

// A pedal is pure DSP -- deliberately NOT a juce::AudioProcessor.
//
// Wibeboard made every effect an AudioProcessor, which meant each one carried
// a bus layout, a program list, its own state serialisation and an editor it
// never used. Here there is exactly one AudioProcessor (PluginProcessor), and
// pedals underneath are plain objects. Adding pedal #2 costs a header, a
// source file and one line in PedalRegistry.
class Pedal
{
public:
    virtual ~Pedal() = default;

    virtual void prepare (const juce::dsp::ProcessSpec& spec) = 0;
    virtual void reset() = 0;

    // Processes in place. Called on the audio thread: no allocation, no locks,
    // no logging, nothing that can block.
    virtual void process (juce::dsp::AudioBlock<float>& block) = 0;

    // Latency the pedal introduces, in samples at the host's sample rate.
    // Valid only after prepare().
    virtual int getLatencySamples() const = 0;

    virtual const char* getName() const = 0;
};
