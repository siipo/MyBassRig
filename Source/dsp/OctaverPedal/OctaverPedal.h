#pragma once

#include "dsp/Pedal.h"
#include "params/Parameters.h"

// Octaver, built the way the analogue ones are: a comparator and two flip-flops.
//
//   in -+- tracking low-pass - comparator -+- flip-flop /2 -+- x envelope -+
//       |                                  +- flip-flop /4 -+- x envelope -+
//       |                                                                  |
//       +------------------- direct ---------------------- tone low-pass --+
//
// Frequency division rather than pitch tracking, and that is a deliberate
// choice rather than the easy way out. A tracker has to hear a couple of cycles
// before it is confident, and at 31 Hz that is 60 ms of latency -- unusable, and
// this plugin reports latency honestly so it could not be hidden. A divider
// responds on the next zero crossing and costs nothing.
//
// The price is that it only works on one note at a time, and it stumbles on
// chords, hard picking and the first few milliseconds of a note. That is not a
// defect to be fixed. It is what an OC-2 does, and it is most of why people
// like them.
//
// Two bass-specific pieces:
//
//  * The tracking low-pass is steep and exposed. What the comparator sees has
//    to be the fundamental and nothing else, and where that lands depends on
//    how far up the neck you play.
//
//  * A squelch. With no note playing, the comparator triggers on noise and the
//    flip-flops free-run, producing a warbling octave out of silence.
//
//  * Growl, which exists because of a measurement rather than a preference.
//    The divider works perfectly at every pitch -- sub level is flat across the
//    neck -- but an octave below the low strings lands somewhere most speakers
//    cannot go. Measured, with the octave of an A on the E string at 27.5 Hz,
//    80% of its energy sits below 40 Hz and simply is not reproduced. That is
//    physics, not a defect, and no amount of tracking work changes it.
//
//    So Growl lifts the octave's harmonics. A square at 27.5 Hz has harmonics
//    at 82, 137, 192 Hz, all comfortably audible and all carrying the same
//    pitch information, so raising them makes the low strings read as an octave
//    rather than as rumble.
//
//    It ADDS rather than trades. The first attempt crossfaded between the
//    fundamental and the harmonics, which helped the low strings by up to 8 dB
//    but cost 6.6 dB on the notes whose octave was already audible -- it threw
//    away exactly what those notes needed. Boosting the upper half against an
//    all-pass complementary crossover means Growl at zero is the untouched
//    octave, and turning it up can only ever help.
class OctaverPedal final : public Pedal
{
public:
    explicit OctaverPedal (juce::AudioProcessorValueTreeState& state);

    static void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process (juce::dsp::AudioBlock<float>& block) override;

    int getLatencySamples() const override { return 0; }
    const char* getName() const override { return "Octaver"; }

    // True while the divider is actually tracking, for the UI lamp.
    bool isTracking() const noexcept { return tracking.load (std::memory_order_relaxed); }

private:
    juce::AudioProcessorValueTreeState& apvts;

    std::atomic<float>* onParam     = nullptr;
    std::atomic<float>* directParam = nullptr;
    std::atomic<float>* subOneParam = nullptr;
    std::atomic<float>* subTwoParam = nullptr;
    std::atomic<float>* toneParam   = nullptr;
    std::atomic<float>* trackParam  = nullptr;
    std::atomic<float>* growlParam  = nullptr;

    // Two cascaded low-passes: one pole is not steep enough to leave the
    // comparator a clean fundamental.
    juce::dsp::StateVariableTPTFilter<float> trackingFilterA, trackingFilterB;
    juce::dsp::StateVariableTPTFilter<float> toneFilter;

    // Splits the octave into rumble and pitch, so Growl can trade between them.
    juce::dsp::LinkwitzRileyFilter<float> growlCrossover;

    juce::AudioBuffer<float> subBuffer;   // sized in prepare, never on the audio thread

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> directGain, subOneGain,
                                                                  subTwoGain, growlAmount;

    float envelope = 0.0f;
    int envelopeHold = 0;    // keeps the follower from rippling at bass frequencies
    int squelchedFor = 0;    // how long the input has been below the squelch
    bool live = false;       // squelch state, with hysteresis
    bool above = false;      // comparator state
    bool flipOne = false;    // divide by two
    bool flipTwo = false;    // divide by four

    std::atomic<bool> tracking { false };

    double sampleRate = 48000.0;
    int numChannels = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctaverPedal)
};
