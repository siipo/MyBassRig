#include "EnvelopeFilterPedal.h"

namespace
{
    constexpr double smoothingSeconds = 0.02;

    // Bottom of the sweep. Fixed rather than exposed, matching the hardware,
    // which has no base-frequency control either: Range sets the top and the
    // bottom stays where a bass fundamental lives.
    constexpr float baseHz = 100.0f;

    // Corner of the high-pass on the blended-back dry signal. Above the range
    // the filter sweeps, so the mix-in adds string definition rather than
    // simply undoing the effect.
    constexpr float dryHighpassHz = 1600.0f;

    // Hi-Q is the quacky voice, so it multiplies whatever Q is dialled in.
    constexpr float hiQMultiplier = 2.2f;

    int choiceIndex (const std::atomic<float>* p, int numChoices) noexcept
    {
        return juce::jlimit (0, numChoices - 1, static_cast<int> (p->load()));
    }

    // Sensitivity is gain into the detector, not a threshold: more sensitivity
    // means a quieter note reaches the top of the sweep.
    float sensitivityGain (float knob) noexcept
    {
        return juce::jmap (knob * knob, 1.0f, 60.0f);
    }
}

EnvelopeFilterPedal::EnvelopeFilterPedal (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    onParam       = apvts.getRawParameterValue (ParamID::envOn);
    sensParam     = apvts.getRawParameterValue (ParamID::envSens);
    attackParam   = apvts.getRawParameterValue (ParamID::envAttack);
    releaseParam  = apvts.getRawParameterValue (ParamID::envRelease);
    qParam        = apvts.getRawParameterValue (ParamID::envQ);
    rangeParam    = apvts.getRawParameterValue (ParamID::envRange);
    dryHighsParam = apvts.getRawParameterValue (ParamID::envDryHighs);
    modeParam     = apvts.getRawParameterValue (ParamID::envMode);
}

void EnvelopeFilterPedal::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using Range = juce::NormalisableRange<float>;

    // Off by default, like the compressor: an effect this audible should be
    // something you switch on, not something a fresh instance does at you.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::envOn, 1 }, "Filter", false));

    layout.add (Params::percentParam (ParamID::envSens, "Sens", 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::envAttack, 1 }, "Env Attack",
        Range { 1.0f, 100.0f, 0.1f, 0.5f }, 10.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::envRelease, 1 }, "Env Release",
        Range { 20.0f, 800.0f, 1.0f, 0.4f }, 180.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::envQ, 1 }, "Q",
        Range { 0.5f, 12.0f, 0.01f, 0.6f }, 4.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::envRange, 1 }, "Range",
        Range { 1.0f, 5.0f, 0.01f }, 3.0f,
        juce::AudioParameterFloatAttributes().withLabel ("oct")));

    layout.add (Params::percentParam (ParamID::envDryHighs, "Dry Hi", 0.3f));

    // The three voices of the pedal this is modelled on.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::envMode, 1 }, "Filter Mode",
        juce::StringArray { "Up", "Down", "Hi-Q" }, 0));
}

void EnvelopeFilterPedal::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate  = spec.sampleRate;
    numChannels = juce::jmax (1, static_cast<int> (spec.numChannels));

    filter.prepare (spec);
    filter.setLimitMode (true);

    dryHighpass.prepare (spec);
    dryHighpass.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    dryHighpass.setCutoffFrequency (dryHighpassHz);

    dryBuffer.setSize (numChannels, static_cast<int> (spec.maximumBlockSize));

    dryHighsGain.reset (spec.sampleRate, smoothingSeconds);
    dryHighsGain.setCurrentAndTargetValue (dryHighsParam->load());

    reset();
}

void EnvelopeFilterPedal::reset()
{
    filter.reset();
    dryHighpass.reset();
    dryBuffer.clear();
    envelope = 0.0f;
    displayEnvelope.store (0.0f, std::memory_order_relaxed);
}

void EnvelopeFilterPedal::process (juce::dsp::AudioBlock<float>& block)
{
    const auto blockChannels = juce::jmin (numChannels, static_cast<int> (block.getNumChannels()));
    const auto numSamples    = static_cast<int> (block.getNumSamples());

    if (blockChannels <= 0 || numSamples <= 0)
        return;

    if (onParam->load() < 0.5f)
    {
        displayEnvelope.store (0.0f, std::memory_order_relaxed);
        return;
    }

    const auto mode  = choiceIndex (modeParam, 3);
    const auto range = rangeParam->load();
    const auto sens  = sensitivityGain (sensParam->load());

    const auto attackCoeff  = std::exp (-1.0f / (0.001f * attackParam->load() * (float) sampleRate));
    const auto releaseCoeff = std::exp (-1.0f / (0.001f * releaseParam->load() * (float) sampleRate));

    const auto q = qParam->load() * (mode == 2 ? hiQMultiplier : 1.0f);
    filter.setQValue (juce::jlimit (0.5f, 30.0f, q));

    dryHighsGain.setTargetValue (dryHighsParam->load());

    // ---- keep a filtered copy of the dry signal to blend back -------------
    for (int ch = 0; ch < blockChannels; ++ch)
        juce::FloatVectorOperations::copy (dryBuffer.getWritePointer (ch),
                                           block.getChannelPointer (static_cast<size_t> (ch)),
                                           numSamples);

    {
        auto dryBlock = juce::dsp::AudioBlock<float> (dryBuffer)
                            .getSubsetChannelBlock (0, static_cast<size_t> (blockChannels))
                            .getSubBlock (0, static_cast<size_t> (numSamples));
        juce::dsp::ProcessContextReplacing<float> dryContext { dryBlock };
        dryHighpass.process (dryContext);
    }

    // ---- sweep -----------------------------------------------------------
    // The envelope is monophonic. A bass is a mono source, and letting the two
    // channels sweep independently would smear the effect across the stereo
    // image rather than moving it as one.
    for (int i = 0; i < numSamples; ++i)
    {
        float rectified = 0.0f;

        for (int ch = 0; ch < blockChannels; ++ch)
            rectified = juce::jmax (rectified,
                                    std::abs (block.getChannelPointer (static_cast<size_t> (ch))[i]));

        const auto target = juce::jlimit (0.0f, 1.0f, rectified * sens);
        const auto coeff  = target > envelope ? attackCoeff : releaseCoeff;
        envelope = target + coeff * (envelope - target);

        const auto sweep = mode == 1 ? (1.0f - envelope) : envelope;   // Down runs backwards
        const auto cutoff = juce::jlimit (30.0f, (float) sampleRate * 0.45f,
                                          baseHz * std::pow (2.0f, range * sweep));

        filter.setCutoffFrequency (cutoff);

        const auto dryAmount = dryHighsGain.getNextValue();

        for (int ch = 0; ch < blockChannels; ++ch)
        {
            auto* samples = block.getChannelPointer (static_cast<size_t> (ch));

            const auto filtered = mode == 2
                ? filter.processSample<chowdsp::ARPFilterType::Bandpass> (samples[i], 0.0f, ch)
                : filter.processSample<chowdsp::ARPFilterType::Lowpass> (samples[i], 0.0f, ch);

            samples[i] = filtered + dryAmount * dryBuffer.getSample (ch, i);
        }
    }

    displayEnvelope.store (envelope, std::memory_order_relaxed);
}
