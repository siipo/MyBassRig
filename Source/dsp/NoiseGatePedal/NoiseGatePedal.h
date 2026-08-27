#pragma once

#include "dsp/Pedal.h"
#include "params/Parameters.h"

// Noise gate.
//
// Three things separate a usable gate from an irritating one, and all three are
// about not being fooled by the signal.
//
//  * Hysteresis. A single threshold makes the gate chatter around it as a note
//    decays. Opening and closing thresholds are separated, so once it is open
//    it stays open until the note is genuinely gone.
//
//  * Hold. Bass notes are not smooth -- the envelope of a plucked string
//    ripples. Without a hold time the gate slams shut in the dips.
//
//  * A sidechain filter, for the same reason the compressor has one: a decaying
//    low fundamental holds the gate open long after the note has stopped being
//    useful, because the fundamental is the loudest thing left.
//
//    Worth knowing: the threshold is measured on the FILTERED key, so the level
//    that actually opens the gate depends on the note. At the 80 Hz setting a
//    110 Hz note reaches the detector at 0.81 of its real level, so it opens
//    about 2 dB later than the dial says. That is the filter doing its job, not
//    a calibration error, but it is why the dial is best set by ear.
//
// Range rather than a hard mute, because a gate that slams to silence is more
// noticeable than the noise it removes.
class NoiseGatePedal final : public Pedal
{
public:
    explicit NoiseGatePedal (juce::AudioProcessorValueTreeState& state);

    static void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process (juce::dsp::AudioBlock<float>& block) override;

    int getLatencySamples() const override { return 0; }
    const char* getName() const override { return "Noise Gate"; }

    // For the meter: 1 when fully open, 0 when fully closed.
    float getOpenAmount() const noexcept { return displayOpen.load (std::memory_order_relaxed); }

private:
    juce::AudioProcessorValueTreeState& apvts;

    std::atomic<float>* onParam        = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* attackParam    = nullptr;
    std::atomic<float>* holdParam      = nullptr;
    std::atomic<float>* releaseParam   = nullptr;
    std::atomic<float>* rangeParam     = nullptr;
    std::atomic<float>* sidechainParam = nullptr;

    juce::dsp::StateVariableTPTFilter<float> sidechainFilter;
    juce::AudioBuffer<float> keyBuffer;   // sized in prepare, never on the audio thread

    float detector = 0.0f;   // envelope of the key signal
    float gain = 0.0f;       // 0 closed, 1 open
    int holdCounter = 0;
    bool open = false;

    std::atomic<float> displayOpen { 0.0f };

    double sampleRate = 48000.0;
    int numChannels = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseGatePedal)
};
