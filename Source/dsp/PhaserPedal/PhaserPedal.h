#pragma once

#include "dsp/Pedal.h"
#include "dsp/common/Lfo.h"
#include "params/Parameters.h"

// Phaser.
//
//   in -+- [all-pass x N, modulated] -+- (+ or -) -> out
//       |                             |
//       +----------- dry -------------+
//
// Two things make this a bass phaser rather than a guitar one.
//
// It has a crossover, and only the band above it is phased. A high sweep floor
// was tried first and is not enough, which is worth spelling out because it is
// not obvious: phase shift ACCUMULATES across the all-pass chain. Six stages
// with the sweep floored at 220 Hz still swing about 134 degrees at 47 Hz, and
// measured, the fundamental modulated MORE than the midrange did -- 0.99
// against 0.79. Only splitting the band actually protects the low end, exactly
// as in the chorus.
//
// And the feedback is soft-limited. A phaser with high feedback and a low sweep
// is a resonance like any other, and it will run away given the chance; the
// envelope filter uses the ARP model's limit mode for this, and here a tanh on
// the feedback path does the same job.
//
// Stage count is the character control: 4 is a gentle sweep, 8 is vocal.
class PhaserPedal final : public Pedal
{
public:
    explicit PhaserPedal (juce::AudioProcessorValueTreeState& state);

    static void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process (juce::dsp::AudioBlock<float>& block) override;

    int getLatencySamples() const override { return 0; }
    const char* getName() const override { return "Phaser"; }

private:
    static constexpr int maxStages = 8;
    static constexpr int maxChannels = 2;

    juce::AudioProcessorValueTreeState& apvts;

    std::atomic<float>* onParam       = nullptr;
    std::atomic<float>* rateParam     = nullptr;
    std::atomic<float>* depthParam    = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* stagesParam   = nullptr;
    std::atomic<float>* mixParam      = nullptr;
    std::atomic<float>* invertParam   = nullptr;
    std::atomic<float>* crossoverParam = nullptr;

    // First-order TPT all-passes: modulating a biquad's coefficients per sample
    // misbehaves, the same reason the envelope filter does not use one.
    std::array<juce::dsp::FirstOrderTPTFilter<float>, maxStages> stages;

    // All-pass complementary, so the two bands sum back flat.
    juce::dsp::LinkwitzRileyFilter<float> crossover;
    std::array<float, maxChannels> feedbackState {};

    Lfo lfo;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> depthSmoothed,
                                                                  feedbackSmoothed, mixSmoothed;

    double sampleRate = 48000.0;
    int numChannels = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaserPedal)
};
