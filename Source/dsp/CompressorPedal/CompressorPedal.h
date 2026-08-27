#pragma once

#include "dsp/Pedal.h"
#include "params/Parameters.h"

#include <chowdsp_compressor/chowdsp_compressor.h>

// Bass compressor, first in the chain.
//
// Built on chowdsp::compressor rather than hand-rolled: the level detector and
// gain computer there are tested DSP, and the interesting decisions for a BASS
// compressor are elsewhere.
//
// The one that matters is the sidechain high-pass. Feed a compressor the whole
// signal and the fundamental of a low B dominates the level detector, so every
// note pumps the whole band in sympathy with the lowest thing playing. Filtering
// the key input means the compressor responds to the note rather than to the
// bottom octave, which is why every serious bass compressor has this and most
// generic ones do not.
class CompressorPedal final : public Pedal
{
public:
    explicit CompressorPedal (juce::AudioProcessorValueTreeState& state);

    static void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process (juce::dsp::AudioBlock<float>& block) override;

    // No lookahead, so the compressor is latency free.
    int getLatencySamples() const override { return 0; }
    const char* getName() const override { return "Compressor"; }

    // Written on the audio thread, read by the meter on the message thread.
    // A relaxed atomic is the right amount of machinery for a number that is
    // only ever displayed.
    float getGainReductionDb() const noexcept
    {
        return gainReductionDb.load (std::memory_order_relaxed);
    }

private:
    using LevelDetector = chowdsp::compressor::CompressorLevelDetector<float, chowdsp::compressor::RMSDetector>;
    using GainComputer  = chowdsp::compressor::GainComputer<float, chowdsp::compressor::FeedForwardCompGainComputer<float>>;

    juce::AudioProcessorValueTreeState& apvts;

    std::atomic<float>* onParam        = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* ratioParam     = nullptr;
    std::atomic<float>* attackParam    = nullptr;
    std::atomic<float>* releaseParam   = nullptr;
    std::atomic<float>* makeupParam    = nullptr;
    std::atomic<float>* sidechainParam = nullptr;

    chowdsp::compressor::MonoCompressor<float, LevelDetector, GainComputer> compressor;

    juce::dsp::StateVariableTPTFilter<float> sidechainFilter;
    juce::AudioBuffer<float> keyBuffer;   // sized in prepare, never on the audio thread

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> makeupGain;

    std::atomic<float> gainReductionDb { 0.0f };
    int numChannels = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorPedal)
};
