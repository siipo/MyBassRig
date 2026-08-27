#pragma once

#include "dsp/Pedal.h"
#include "params/Parameters.h"

// Octaver: a period tracker driving a phase-locked oscillator.
//
//   in -+- tracking low-pass - crossing detector -> period estimate
//       |                                              |
//       |                                     phase accumulator at f/2
//       |                                              |
//       |                              sine + chosen harmonics x envelope -+
//       |                                                                 |
//       +--------------------- direct ------------------------------------+
//
// This replaces an analogue-style comparator and flip-flop divider. The reasons
// are worth keeping, because each came from playing the thing rather than from
// measuring it. The old design was measurably CORRECT -- it tracked flat across
// the neck and produced the right octave at every pitch -- and it sounded bad in
// three specific ways, all of them structural rather than tuning:
//
//  * GRITTY. A flip-flop makes a hard square, switched on whichever sample
//    boundary is nearest. Its edges are not band limited and its period jitters
//    by up to one sample, which measured as -32 to -44 dB of content that is
//    not a harmonic of the octave (DESIGN.md 3q). The tone control could muffle
//    that but never remove it.
//
//  * WRONG OCTAVE. A flip-flop has memory. One spurious crossing does not cause
//    a blip, it INVERTS the division from that point on until something resets
//    it -- which is why the fault was reported as "it jumps an octave" rather
//    than "it clicks". Three guards were added to stop that happening
//    (DESIGN.md 3o) and none of them addressed why it was possible.
//
//  * RESPONSE. The octave was literally square x envelope, so its dynamics were
//    the envelope's and nothing else's.
//
// A fourth fault surfaced once the first three were fixed, reported as "audible
// crackle when the note fades". The amplitude was gated by a BOOLEAN, so every
// time a decaying note made the tracker drop and re-acquire, the waveform
// stepped by its whole amplitude. The old design had exactly the same boolean
// and nobody heard it, because a hard square is already nothing but
// discontinuities. Cleaning up the waveform is what made an existing fault
// audible. It is now a ramp, and the phase lock pays its correction off over
// 15 ms rather than jumping. See DESIGN.md 3t.
//
// The tracker fixes the second by construction: a bad crossing perturbs a
// period ESTIMATE, which is validated and smoothed, instead of flipping a state
// permanently. There is no state left that can be stuck in the wrong half.
//
// The oscillator fixes the first by construction: every partial is generated at
// a frequency we chose, so nothing folds. A sine at 40 Hz with two harmonics
// has no content above 120 Hz to alias in the first place.
//
// Growl is now what its name always claimed: it sets how much second and third
// harmonic the oscillator generates, rather than boosting a crossover band and
// then wrestling the level back down. Because the amplitudes are chosen rather
// than produced by clipping, the most the level can move is
//
//     sqrt(1 + 0.7^2 + 0.45^2) = 1.30,  i.e. 2.3 dB
//
// bounded by construction. The running level follower and the +3 dB ceiling the
// old design needed (DESIGN.md 3r) are both gone.
//
// Normalising to constant power was tried and is wrong: on a note whose octave
// already sits above 40 Hz it makes Growl add exactly zero audible energy,
// because there is nothing left to redistribute from, and the control's one
// promise is that it can only ever help.
//
// Kept deliberately:
//
//  * Zero latency. The oscillator runs continuously and is corrected on
//    crossings, so nothing waits for confidence before making sound. A pitch
//    tracker that waits two cycles costs 60 ms at 31 Hz, which is unusable.
//
//  * Monophonic. It still stumbles on chords, which is honest for an octaver
//    rather than a defect.
//
//  * Every parameter ID, so existing sessions and presets still load.
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

    // True while the tracker holds a period it believes in, for the UI lamp.
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

    // Two cascaded low-passes: one pole does not leave the detector a clean
    // enough fundamental to time crossings from.
    juce::dsp::StateVariableTPTFilter<float> trackingFilterA, trackingFilterB;

    // Gentle now, and only there to take the edge off the harmonics at high
    // Growl. It is no longer hiding anything.
    juce::dsp::StateVariableTPTFilter<float> toneFilter;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> directGain, subOneGain,
                                                                  subTwoGain, growlAmount;

    // ---- detector ---------------------------------------------------------
    bool  above = false;              // crossing state, carrying no division memory
    int   samplesSinceCrossing = 0;
    float trackedPeriod = 0.0f;       // smoothed, in samples of the input
    float candidatePeriod = 0.0f;     // a reading that disagrees with the current one
    int   agreeingReadings = 0;       // how many times it has disagreed the same way
    int   samplesSinceValid = 0;      // how long since a period we believed

    // ---- oscillator -------------------------------------------------------
    // Wraps at 2, so one turn is a cycle of the first octave down and two turns
    // are a cycle of the second. One accumulator for both means they cannot
    // drift apart however long it runs.
    float phase = 0.0f;

    // The period the oscillator is actually running at. It outlives
    // trackedPeriod so that when tracking drops, what fades out is a continuing
    // sine rather than a frozen sample held at whatever value it stopped on.
    float oscPeriod = 0.0f;

    // A ramp, not a boolean. Gating the amplitude with `hasPitch ? envelope : 0`
    // steps the waveform by its whole amplitude the instant tracking drops or
    // returns, and on a decaying note that happens repeatedly -- which is what
    // "audible crackle when the note fades" was.
    float pitchGate = 0.0f;

    // Phase error waiting to be worked off. Correcting the phase the instant a
    // crossing arrives moves the waveform's VALUE discontinuously, which is a
    // click; this is paid back gradually as a rate change, which is not.
    float pendingPhaseCorrection = 0.0f;

    float envelope = 0.0f;
    int   envelopeHold = 0;
    bool  live = false;               // squelch, with hysteresis

    std::atomic<bool> tracking { false };

    double sampleRate = 48000.0;
    int numChannels = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctaverPedal)
};
