#include "CompressorPedal.h"

namespace
{
    constexpr double smoothingSeconds = 0.02;

    // A soft knee, because a bass compressor that grabs abruptly is audible as
    // grabbing rather than as sustain.
    constexpr float kneeDb = 6.0f;

    int choiceIndex (const std::atomic<float>* p, int numChoices) noexcept
    {
        return juce::jlimit (0, numChoices - 1, static_cast<int> (p->load()));
    }
}

CompressorPedal::CompressorPedal (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    onParam        = apvts.getRawParameterValue (ParamID::compOn);
    thresholdParam = apvts.getRawParameterValue (ParamID::compThreshold);
    ratioParam     = apvts.getRawParameterValue (ParamID::compRatio);
    attackParam    = apvts.getRawParameterValue (ParamID::compAttack);
    releaseParam   = apvts.getRawParameterValue (ParamID::compRelease);
    makeupParam    = apvts.getRawParameterValue (ParamID::compMakeup);
    sidechainParam = apvts.getRawParameterValue (ParamID::compSidechain);
}

void CompressorPedal::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using Range = juce::NormalisableRange<float>;

    // Off by default. A drive at its default setting announces itself the
    // moment you play; a compressor quietly changing dynamics and level does
    // not, and a plugin that squashes on insert without being asked is a
    // surprise. Presets that want it turn it on.
    layout.add (Params::boolParam (ParamID::compOn, "Comp", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::compThreshold, 1 }, "Threshold",
        Range { -48.0f, 0.0f, 0.1f }, -18.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    // Skewed so the useful low ratios are not crammed into the first tenth of
    // the knob, the same problem the drive taper had.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::compRatio, 1 }, "Ratio",
        Range { 1.0f, 20.0f, 0.01f, 0.4f }, 4.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return juce::String (v, 1) + ":1"; })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::compAttack, 1 }, "Attack",
        Range { 1.0f, 100.0f, 0.1f, 0.5f }, 15.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::compRelease, 1 }, "Release",
        Range { 10.0f, 800.0f, 1.0f, 0.4f }, 150.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::compMakeup, 1 }, "Makeup",
        Range { 0.0f, 24.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    // Defaults to 80 Hz rather than Off on purpose: for a bass compressor the
    // filtered key input is the correct behaviour, not an advanced option.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::compSidechain, 1 }, "Sidechain",
        juce::StringArray { "Off", "80 Hz", "160 Hz" }, 1));
}

void CompressorPedal::prepare (const juce::dsp::ProcessSpec& spec)
{
    numChannels = juce::jmax (1, static_cast<int> (spec.numChannels));

    compressor.prepare (spec);

    sidechainFilter.prepare (spec);
    sidechainFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    sidechainFilter.setCutoffFrequency (Params::sidechainHighpassHz[1]);

    keyBuffer.setSize (numChannels, static_cast<int> (spec.maximumBlockSize));

    makeupGain.reset (spec.sampleRate, smoothingSeconds);
    makeupGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (makeupParam->load()));

    reset();
}

void CompressorPedal::reset()
{
    // MonoCompressor has no reset of its own; its two halves do.
    compressor.levelDetector.reset();
    compressor.gainComputer.reset();
    sidechainFilter.reset();
    keyBuffer.clear();
    gainReductionDb.store (0.0f, std::memory_order_relaxed);
}

void CompressorPedal::process (juce::dsp::AudioBlock<float>& block)
{
    const auto blockChannels = juce::jmin (numChannels, static_cast<int> (block.getNumChannels()));
    const auto numSamples    = static_cast<int> (block.getNumSamples());

    if (blockChannels <= 0 || numSamples <= 0)
        return;

    if (onParam->load() < 0.5f)
    {
        gainReductionDb.store (0.0f, std::memory_order_relaxed);
        return;
    }

    compressor.params.attackMs    = attackParam->load();
    compressor.params.releaseMs   = releaseParam->load();
    compressor.params.thresholdDB = thresholdParam->load();
    compressor.params.ratio       = ratioParam->load();
    compressor.params.kneeDB      = kneeDb;
    compressor.params.autoMakeup  = false;

    makeupGain.setTargetValue (juce::Decibels::decibelsToGain (makeupParam->load()));

    // ---- key input -------------------------------------------------------
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

    // ---- measure, compress, measure --------------------------------------
    // Gain reduction is taken from the signal either side of the compressor
    // rather than from its internals, so the number on the meter is the change
    // the listener actually gets.
    float inputPeak = 0.0f;

    for (int ch = 0; ch < blockChannels; ++ch)
        inputPeak = juce::jmax (inputPeak,
                                juce::FloatVectorOperations::findMaximum (
                                    block.getChannelPointer (static_cast<size_t> (ch)), numSamples),
                                -juce::FloatVectorOperations::findMinimum (
                                    block.getChannelPointer (static_cast<size_t> (ch)), numSamples));

    std::array<float*, 2> mainPointers {};

    for (int ch = 0; ch < blockChannels; ++ch)
        mainPointers[(size_t) ch] = block.getChannelPointer (static_cast<size_t> (ch));

    const chowdsp::BufferView<float> mainView { mainPointers.data(), blockChannels, numSamples };
    const chowdsp::BufferView<const float> keyView { keyBuffer.getArrayOfReadPointers(),
                                                     blockChannels, numSamples };

    compressor.processBlock (mainView, keyView);

    float outputPeak = 0.0f;

    for (int ch = 0; ch < blockChannels; ++ch)
        outputPeak = juce::jmax (outputPeak,
                                 juce::FloatVectorOperations::findMaximum (
                                     block.getChannelPointer (static_cast<size_t> (ch)), numSamples),
                                 -juce::FloatVectorOperations::findMinimum (
                                     block.getChannelPointer (static_cast<size_t> (ch)), numSamples));

    if (inputPeak > 1.0e-5f)
    {
        const auto reduction = juce::Decibels::gainToDecibels (outputPeak / inputPeak);
        gainReductionDb.store (juce::jlimit (-40.0f, 0.0f, reduction), std::memory_order_relaxed);
    }
    else
    {
        gainReductionDb.store (0.0f, std::memory_order_relaxed);
    }

    block.multiplyBy (makeupGain);
}
