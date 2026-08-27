#include "PhaserPedal.h"

namespace
{
    constexpr double smoothingSeconds = 0.03;

    // Bottom and top of the notch sweep. The floor is high, but the crossover
    // is what actually keeps the low end out of it -- see the note in the
    // header.
    constexpr float sweepFloorHz = 220.0f;
    constexpr float sweepCeilingHz = 3200.0f;

    constexpr std::array<int, 3> stageCounts { 4, 6, 8 };

    int choiceIndex (const std::atomic<float>* p, int numChoices) noexcept
    {
        return juce::jlimit (0, numChoices - 1, static_cast<int> (p->load()));
    }
}

PhaserPedal::PhaserPedal (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    onParam       = apvts.getRawParameterValue (ParamID::phaserOn);
    rateParam     = apvts.getRawParameterValue (ParamID::phaserRate);
    depthParam    = apvts.getRawParameterValue (ParamID::phaserDepth);
    feedbackParam = apvts.getRawParameterValue (ParamID::phaserFeedback);
    stagesParam   = apvts.getRawParameterValue (ParamID::phaserStages);
    mixParam      = apvts.getRawParameterValue (ParamID::phaserMix);
    invertParam   = apvts.getRawParameterValue (ParamID::phaserInvert);
    crossoverParam = apvts.getRawParameterValue (ParamID::phaserCrossover);
}

void PhaserPedal::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using Range = juce::NormalisableRange<float>;

    layout.add (Params::boolParam (ParamID::phaserOn, "Phaser", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::phaserRate, 1 }, "Phaser Rate",
        Range { 0.05f, 8.0f, 0.01f, 0.4f }, 0.4f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (Params::percentParam (ParamID::phaserDepth, "Phaser Depth", 0.7f));
    layout.add (Params::percentParam (ParamID::phaserFeedback, "Feedback", 0.4f));

    // Stage count is the voice of a phaser, not a technicality.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::phaserStages, 1 }, "Stages",
        juce::StringArray { "4", "6", "8" }, 1));

    layout.add (Params::percentParam (ParamID::phaserMix, "Phaser Mix", 0.5f));

    // Summing the dry and wet paths in antiphase moves the notches somewhere
    // else entirely, so this is a second voice rather than a polarity nicety.
    layout.add (Params::boolParam (ParamID::phaserInvert, "Invert", false));

    // Below this, the signal is not phased at all.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::phaserCrossover, 1 }, "Phaser Crossover",
        Range { 60.0f, 500.0f, 1.0f, 0.5f }, 160.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));
}

void PhaserPedal::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate  = spec.sampleRate;
    numChannels = juce::jlimit (1, maxChannels, static_cast<int> (spec.numChannels));

    for (auto& stage : stages)
    {
        stage.prepare (spec);
        stage.setType (juce::dsp::FirstOrderTPTFilterType::allpass);
        stage.setCutoffFrequency (sweepFloorHz);
    }

    crossover.prepare (spec);
    crossover.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    crossover.setCutoffFrequency (crossoverParam->load());

    lfo.prepare (spec.sampleRate);

    depthSmoothed.reset (spec.sampleRate, smoothingSeconds);
    depthSmoothed.setCurrentAndTargetValue (depthParam->load());
    feedbackSmoothed.reset (spec.sampleRate, smoothingSeconds);
    feedbackSmoothed.setCurrentAndTargetValue (feedbackParam->load());
    mixSmoothed.reset (spec.sampleRate, smoothingSeconds);
    mixSmoothed.setCurrentAndTargetValue (mixParam->load());

    reset();
}

void PhaserPedal::reset()
{
    for (auto& stage : stages)
        stage.reset();

    crossover.reset();
    feedbackState.fill (0.0f);
    lfo.reset();
}

void PhaserPedal::process (juce::dsp::AudioBlock<float>& block)
{
    const auto blockChannels = juce::jmin (numChannels, static_cast<int> (block.getNumChannels()));
    const auto numSamples    = static_cast<int> (block.getNumSamples());

    if (blockChannels <= 0 || numSamples <= 0)
        return;

    if (onParam->load() < 0.5f)
        return;

    const auto numStages = stageCounts[(size_t) choiceIndex (stagesParam, 3)];
    const auto invert = invertParam->load() > 0.5f;

    crossover.setCutoffFrequency (crossoverParam->load());
    lfo.setRateHz (rateParam->load());
    depthSmoothed.setTargetValue (depthParam->load());
    feedbackSmoothed.setTargetValue (feedbackParam->load());
    mixSmoothed.setTargetValue (mixParam->load());

    for (int i = 0; i < numSamples; ++i)
    {
        const auto sweep = lfo.next();
        const auto depth = depthSmoothed.getNextValue();
        const auto feedback = feedbackSmoothed.getNextValue();
        const auto mix = mixSmoothed.getNextValue();

        // Sweep exponentially: notches move in octaves, not in hertz.
        const auto cutoff = sweepFloorHz
                          * std::pow (sweepCeilingHz / sweepFloorHz, depth * sweep);

        for (int s = 0; s < numStages; ++s)
            stages[(size_t) s].setCutoffFrequency (cutoff);

        for (int ch = 0; ch < blockChannels; ++ch)
        {
            auto* samples = block.getChannelPointer (static_cast<size_t> (ch));

            float low = 0.0f, high = 0.0f;
            crossover.processSample (ch, samples[i], low, high);

            // tanh on the feedback path, so high feedback saturates rather than
            // diverging. Without it this is an oscillator waiting to happen.
            auto x = high + std::tanh (feedback * feedbackState[(size_t) ch]);

            for (int s = 0; s < numStages; ++s)
                x = stages[(size_t) s].processSample (ch, x);

            feedbackState[(size_t) ch] = x;

            const auto wet = invert ? -x : x;
            samples[i] = low + (high * (1.0f - mix) + wet * mix);
        }
    }
}
