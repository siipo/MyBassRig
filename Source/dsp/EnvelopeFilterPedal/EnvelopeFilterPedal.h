#pragma once

#include "dsp/Pedal.h"
#include "params/Parameters.h"

#include <chowdsp_filters/chowdsp_filters.h>

// Envelope filter, in the spirit of the EBS BassIQ.
//
// Not a clone of it -- no schematic was traced -- but the control set and the
// three voices come from that pedal, because it is the one that got envelope
// filtering right for bass. Two things it does that generic auto-wahs do not:
//
//  * Three distinct voices rather than one. Up and Down sweep a low-pass in
//    opposite directions; Hi-Q sweeps a band-pass and is where the quack lives.
//  * A high-pass mix-in. A resonant low-pass swallows string definition, so a
//    filtered copy of the dry signal is blended back over the top. On the
//    hardware this is an internal trim; here it is a knob, because a plugin
//    does not need a screwdriver.
//
// The filter is chowdsp's ARP 4072 emulation rather than a plain SVF, chiefly
// for its limit mode: resonance soft-limits instead of running away, which
// matters when a high Q sits over a low B.
class EnvelopeFilterPedal final : public Pedal
{
public:
    explicit EnvelopeFilterPedal (juce::AudioProcessorValueTreeState& state);

    static void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process (juce::dsp::AudioBlock<float>& block) override;

    int getLatencySamples() const override { return 0; }
    const char* getName() const override { return "Envelope Filter"; }

    // Drives the sweep display. Written on the audio thread, read on the
    // message thread; relaxed because it is only ever drawn.
    float getEnvelope() const noexcept { return displayEnvelope.load (std::memory_order_relaxed); }

private:
    juce::AudioProcessorValueTreeState& apvts;

    std::atomic<float>* onParam       = nullptr;
    std::atomic<float>* sensParam     = nullptr;
    std::atomic<float>* attackParam   = nullptr;
    std::atomic<float>* releaseParam  = nullptr;
    std::atomic<float>* qParam        = nullptr;
    std::atomic<float>* rangeParam    = nullptr;
    std::atomic<float>* dryHighsParam = nullptr;
    std::atomic<float>* modeParam     = nullptr;

    chowdsp::ARPFilter<float> filter;

    // The dry signal that gets blended back over the filtered one.
    juce::dsp::StateVariableTPTFilter<float> dryHighpass;
    juce::AudioBuffer<float> dryBuffer;   // sized in prepare, never on the audio thread

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dryHighsGain;

    double sampleRate = 48000.0;
    int numChannels = 1;
    float envelope = 0.0f;

    std::atomic<float> displayEnvelope { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeFilterPedal)
};
