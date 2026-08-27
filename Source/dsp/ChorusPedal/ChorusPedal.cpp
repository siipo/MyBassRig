#include "ChorusPedal.h"

namespace
{
    constexpr double smoothingSeconds = 0.03;

    // Depth is in milliseconds of sweep, not a percentage, because what matters
    // musically is how far the delay actually moves.
    constexpr float maxDepthMs = 7.0f;

    int choiceIndex (const std::atomic<float>* p, int numChoices) noexcept
    {
        return juce::jlimit (0, numChoices - 1, static_cast<int> (p->load()));
    }
}

ChorusPedal::ChorusPedal (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    onParam        = apvts.getRawParameterValue (ParamID::chorusOn);
    rateParam      = apvts.getRawParameterValue (ParamID::chorusRate);
    depthParam     = apvts.getRawParameterValue (ParamID::chorusDepth);
    mixParam       = apvts.getRawParameterValue (ParamID::chorusMix);
    crossoverParam = apvts.getRawParameterValue (ParamID::chorusCrossover);
    modeParam      = apvts.getRawParameterValue (ParamID::chorusMode);
}

void ChorusPedal::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using Range = juce::NormalisableRange<float>;

    layout.add (Params::boolParam (ParamID::chorusOn, "Chorus", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::chorusRate, 1 }, "Chorus Rate",
        Range { 0.05f, 8.0f, 0.01f, 0.4f }, 0.6f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::chorusDepth, 1 }, "Chorus Depth",
        Range { 0.0f, maxDepthMs, 0.01f }, 2.5f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (Params::percentParam (ParamID::chorusMix, "Chorus Mix", 0.5f));

    // The control that makes this a BASS chorus. Below it nothing moves.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::chorusCrossover, 1 }, "Crossover",
        Range { 80.0f, 500.0f, 1.0f, 0.5f }, 200.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::chorusMode, 1 }, "Chorus Mode",
        juce::StringArray { "Chorus", "Vibrato" }, 0));
}

void ChorusPedal::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate  = spec.sampleRate;
    numChannels = juce::jmax (1, static_cast<int> (spec.numChannels));

    crossover.prepare (spec);
    crossover.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    crossover.setCutoffFrequency (crossoverParam->load());

    for (auto& delay : delays)
    {
        delay.prepare (spec);
        delay.setMaximumDelayInSamples (juce::roundToInt ((baseDelayMs + maxDepthMs + 2.0f)
                                                          * 0.001 * sampleRate));
    }

    lowBuffer.setSize (numChannels, static_cast<int> (spec.maximumBlockSize));

    lfo.prepare (spec.sampleRate);

    depthSmoothed.reset (spec.sampleRate, smoothingSeconds);
    depthSmoothed.setCurrentAndTargetValue (depthParam->load());
    mixSmoothed.reset (spec.sampleRate, smoothingSeconds);
    mixSmoothed.setCurrentAndTargetValue (mixParam->load());

    reset();
}

void ChorusPedal::reset()
{
    crossover.reset();

    for (auto& delay : delays)
        delay.reset();

    lowBuffer.clear();
    lfo.reset();
}

void ChorusPedal::process (juce::dsp::AudioBlock<float>& block)
{
    const auto blockChannels = juce::jmin (numChannels, static_cast<int> (block.getNumChannels()));
    const auto numSamples    = static_cast<int> (block.getNumSamples());

    if (blockChannels <= 0 || numSamples <= 0)
        return;

    if (onParam->load() < 0.5f)
        return;

    const auto vibrato = choiceIndex (modeParam, 2) == 1;

    crossover.setCutoffFrequency (crossoverParam->load());
    lfo.setRateHz (rateParam->load());

    depthSmoothed.setTargetValue (depthParam->load());
    mixSmoothed.setTargetValue (vibrato ? 1.0f : mixParam->load());

    const auto samplesPerMs = (float) sampleRate * 0.001f;

    for (int i = 0; i < numSamples; ++i)
    {
        // One accumulator, two voices read half a cycle apart, so they cannot
        // drift out of relationship however long the plugin runs.
        const auto phaseA = lfo.next();
        const auto phaseB = lfo.offsetBy (0.5f);

        const auto depthMs = depthSmoothed.getNextValue();
        const auto mix     = mixSmoothed.getNextValue();

        for (int ch = 0; ch < blockChannels; ++ch)
        {
            auto* samples = block.getChannelPointer (static_cast<size_t> (ch));

            float low = 0.0f, high = 0.0f;
            crossover.processSample (ch, samples[i], low, high);

            const std::array<float, numVoices> phases { phaseA, phaseB };
            float wet = 0.0f;

            for (int v = 0; v < numVoices; ++v)
            {
                delays[(size_t) v].pushSample (ch, high);
                wet += delays[(size_t) v].popSample (
                    ch, (baseDelayMs + depthMs * phases[(size_t) v]) * samplesPerMs, true);
            }

            wet *= 0.5f;

            // Only the high band is crossfaded. The low band is passed through
            // untouched, which is the entire point of the crossover -- in
            // Vibrato the dry high band goes away, but the lows still do not
            // move.
            samples[i] = low + (high * (1.0f - mix) + wet * mix);
        }
    }
}
