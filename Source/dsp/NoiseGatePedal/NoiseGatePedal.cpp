#include "NoiseGatePedal.h"

namespace
{
    // How far below the opening threshold the gate has to fall before it
    // closes. Without this gap the gate chatters as a note decays across the
    // threshold.
    constexpr float hysteresisDb = 6.0f;

    // The detector follows the signal quickly upward and slowly downward, so a
    // transient opens the gate immediately but the ripple inside a plucked note
    // does not close it.
    constexpr float detectorAttackMs = 0.5f;
    constexpr float detectorReleaseMs = 40.0f;

    int choiceIndex (const std::atomic<float>* p, int numChoices) noexcept
    {
        return juce::jlimit (0, numChoices - 1, static_cast<int> (p->load()));
    }

    float onePoleCoeff (float milliseconds, double sampleRate) noexcept
    {
        return std::exp (-1.0f / (0.001f * juce::jmax (0.01f, milliseconds) * (float) sampleRate));
    }
}

NoiseGatePedal::NoiseGatePedal (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    onParam        = apvts.getRawParameterValue (ParamID::gateOn);
    thresholdParam = apvts.getRawParameterValue (ParamID::gateThreshold);
    attackParam    = apvts.getRawParameterValue (ParamID::gateAttack);
    holdParam      = apvts.getRawParameterValue (ParamID::gateHold);
    releaseParam   = apvts.getRawParameterValue (ParamID::gateRelease);
    rangeParam     = apvts.getRawParameterValue (ParamID::gateRange);
    sidechainParam = apvts.getRawParameterValue (ParamID::gateSidechain);
}

void NoiseGatePedal::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using Range = juce::NormalisableRange<float>;

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::gateOn, 1 }, "Gate", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::gateThreshold, 1 }, "Gate Threshold",
        Range { -80.0f, -10.0f, 0.1f }, -55.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::gateAttack, 1 }, "Gate Attack",
        Range { 0.1f, 50.0f, 0.1f, 0.4f }, 2.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    // Bass notes ripple as they decay; without hold the gate shuts in the dips.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::gateHold, 1 }, "Hold",
        Range { 0.0f, 500.0f, 1.0f, 0.5f }, 80.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::gateRelease, 1 }, "Gate Release",
        Range { 5.0f, 1000.0f, 1.0f, 0.4f }, 200.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    // Not a hard mute: a gate that slams to silence draws more attention than
    // the noise it removes.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::gateRange, 1 }, "Range",
        Range { -80.0f, -6.0f, 0.1f }, -40.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    // Same reasoning as the compressor: a decaying fundamental is the loudest
    // thing left, and it holds the gate open long after the note is useful.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::gateSidechain, 1 }, "Gate Sidechain",
        juce::StringArray { "Off", "80 Hz", "160 Hz" }, 1));
}

void NoiseGatePedal::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate  = spec.sampleRate;
    numChannels = juce::jmax (1, static_cast<int> (spec.numChannels));

    sidechainFilter.prepare (spec);
    sidechainFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    sidechainFilter.setCutoffFrequency (Params::sidechainHighpassHz[1]);

    keyBuffer.setSize (numChannels, static_cast<int> (spec.maximumBlockSize));

    reset();
}

void NoiseGatePedal::reset()
{
    sidechainFilter.reset();
    keyBuffer.clear();

    detector = 0.0f;
    holdCounter = 0;
    open = false;

    // Starts open rather than closed, so the first note is not chopped while
    // the detector is still catching up.
    gain = 1.0f;
    displayOpen.store (1.0f, std::memory_order_relaxed);
}

void NoiseGatePedal::process (juce::dsp::AudioBlock<float>& block)
{
    const auto blockChannels = juce::jmin (numChannels, static_cast<int> (block.getNumChannels()));
    const auto numSamples    = static_cast<int> (block.getNumSamples());

    if (blockChannels <= 0 || numSamples <= 0)
        return;

    if (onParam->load() < 0.5f)
    {
        displayOpen.store (1.0f, std::memory_order_relaxed);
        return;
    }

    const auto openThreshold  = juce::Decibels::decibelsToGain (thresholdParam->load());
    const auto closeThreshold = juce::Decibels::decibelsToGain (thresholdParam->load() - hysteresisDb);
    const auto floorGain      = juce::Decibels::decibelsToGain (rangeParam->load());

    const auto attackCoeff  = onePoleCoeff (attackParam->load(), sampleRate);
    const auto releaseCoeff = onePoleCoeff (releaseParam->load(), sampleRate);
    const auto holdSamples  = juce::roundToInt (holdParam->load() * 0.001f * sampleRate);

    const auto detectAttack  = onePoleCoeff (detectorAttackMs, sampleRate);
    const auto detectRelease = onePoleCoeff (detectorReleaseMs, sampleRate);

    // ---- key signal ------------------------------------------------------
    const auto sidechainIndex = choiceIndex (sidechainParam, 3);

    for (int ch = 0; ch < blockChannels; ++ch)
        juce::FloatVectorOperations::copy (keyBuffer.getWritePointer (ch),
                                           block.getChannelPointer (static_cast<size_t> (ch)),
                                           numSamples);

    if (sidechainIndex > 0)
    {
        sidechainFilter.setCutoffFrequency (Params::sidechainHighpassHz[(size_t) sidechainIndex]);

        auto keyBlock = juce::dsp::AudioBlock<float> (keyBuffer)
                            .getSubsetChannelBlock (0, static_cast<size_t> (blockChannels))
                            .getSubBlock (0, static_cast<size_t> (numSamples));
        juce::dsp::ProcessContextReplacing<float> keyContext { keyBlock };
        sidechainFilter.process (keyContext);
    }

    // ---- gate ------------------------------------------------------------
    for (int i = 0; i < numSamples; ++i)
    {
        float rectified = 0.0f;

        for (int ch = 0; ch < blockChannels; ++ch)
            rectified = juce::jmax (rectified, std::abs (keyBuffer.getSample (ch, i)));

        const auto detectCoeff = rectified > detector ? detectAttack : detectRelease;
        detector = rectified + detectCoeff * (detector - rectified);

        // Two thresholds, not one. Crossing up opens; only falling well below
        // closes, and only after the hold has run out.
        if (! open && detector > openThreshold)
        {
            open = true;
            holdCounter = holdSamples;
        }
        else if (open && detector < closeThreshold)
        {
            if (holdCounter > 0)
                --holdCounter;
            else
                open = false;
        }
        else if (open)
        {
            holdCounter = holdSamples;
        }

        const auto target = open ? 1.0f : 0.0f;
        const auto coeff  = target > gain ? attackCoeff : releaseCoeff;
        gain = target + coeff * (gain - target);

        const auto applied = floorGain + (1.0f - floorGain) * gain;

        for (int ch = 0; ch < blockChannels; ++ch)
            block.getChannelPointer (static_cast<size_t> (ch))[i] *= applied;
    }

    displayOpen.store (gain, std::memory_order_relaxed);
}
