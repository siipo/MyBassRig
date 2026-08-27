#pragma once

#include "dsp/Pedal.h"
#include "dsp/common/Lfo.h"
#include "params/Parameters.h"

// Bass chorus.
//
// The problem is the one this project keeps running into: modulating the low
// end turns it to soup. A guitar chorus on a bass makes the fundamental wander
// and the note stops sitting still. The answer is the same one the drive pedal
// uses -- split the band and only process the top:
//
//   in -+- low-pass  ------------------------------- unmodulated -+- out
//       |                                                         |
//       +- high-pass -+- [delay 1, modulated] --+-----------------+
//                     +- [delay 2, modulated] --+
//
// The crossover is exposed, because where the low end stops moving is the
// difference between a usable bass chorus and an unusable one.
//
// Two modulated voices rather than one, read from a single LFO at opposite
// points of its cycle: one voice detunes, two thicken.
class ChorusPedal final : public Pedal
{
public:
    explicit ChorusPedal (juce::AudioProcessorValueTreeState& state);

    static void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process (juce::dsp::AudioBlock<float>& block) override;

    int getLatencySamples() const override { return 0; }
    const char* getName() const override { return "Chorus"; }

private:
    static constexpr int numVoices = 2;
    static constexpr float baseDelayMs = 8.0f;

    juce::AudioProcessorValueTreeState& apvts;

    std::atomic<float>* onParam        = nullptr;
    std::atomic<float>* rateParam      = nullptr;
    std::atomic<float>* depthParam     = nullptr;
    std::atomic<float>* mixParam       = nullptr;
    std::atomic<float>* crossoverParam = nullptr;
    std::atomic<float>* modeParam      = nullptr;

    // Linkwitz-Riley because the two bands are summed back together. A plain
    // pair of filters would notch the crossover region on recombination; this
    // one is all-pass complementary, so the sum is flat.
    juce::dsp::LinkwitzRileyFilter<float> crossover;

    // Lagrange interpolation, not linear: a modulated delay read with a poor
    // interpolator is exactly where chorus artefacts come from.
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>,
               numVoices> delays { juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> (96000),
                                   juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> (96000) };

    Lfo lfo;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> depthSmoothed, mixSmoothed;

    juce::AudioBuffer<float> lowBuffer;   // sized in prepare, never on the audio thread

    double sampleRate = 48000.0;
    int numChannels = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusPedal)
};
