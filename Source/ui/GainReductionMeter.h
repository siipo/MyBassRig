#pragma once

#include "PedalLookAndFeel.h"
#include "dsp/CompressorPedal/CompressorPedal.h"

// Gain reduction readout.
//
// A compressor without a meter is a guessing game: threshold and ratio only
// mean something once you can see how much the thing is actually doing.
//
// Polled on a timer rather than pushed from the audio thread, and the value it
// reads is a relaxed atomic, so nothing here can block the audio callback. The
// displayed value falls back slowly so short peaks stay readable instead of
// flickering past.
class GainReductionMeter final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit GainReductionMeter (const CompressorPedal* pedal);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    const CompressorPedal* compressor;
    float displayed = 0.0f;

    static constexpr float floorDb = -20.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainReductionMeter)
};
