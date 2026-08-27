#pragma once

#include "dsp/Pedal.h"
#include "dsp/ToneStack.h"
#include "params/Parameters.h"

#include <chowdsp_waveshapers/chowdsp_waveshapers.h>

// Bass preamp / overdrive.
//
// The class-defining trick, and the thing a generic distortion gets wrong: the
// low end never enters the clipper at full strength. A clean path runs at
// unity alongside a driven path whose bass content is controlled separately
// (Grunt), and the two are blended.
//
//   in - DC block - trim -+- CLEAN ---------- delay(N) ------------+
//                         |                                        +- BLEND - master - out
//                         +- grunt HP - attack shelf - drive - OS -+
//
// Inside the 2x oversampled region the drive path is a two-stage cascade:
//
//   [ADAA tanh] - coupling HP - interstage gain - [ADAA soft clip d5]
//               - output HP - recovery LP
//
// The bias is a fixed offset at the shaper input, AFTER the drive gain. Scaling
// it with the gain instead was tried and is wrong: at full drive the bias alone
// reaches 3.6 and pins the stage near saturation before any signal arrives.
// Fixed, it stays a small offset against a clipping threshold of ~1, and the
// asymmetry it buys shows up as even harmonics -- the clipped waveform's duty
// cycle shifts -- rather than as a difference in peak height.
//
// Two mild stages sound better than one aggressive one, which is why real
// pedals in this family cascade a JFET stage into a CMOS inverter rather than
// slamming a single device. The bias makes the first stage asymmetric (a
// symmetric clipper produces odd harmonics only and sounds sterile on bass);
// the coupling high-pass then removes the DC that asymmetry generates, exactly
// as the coupling capacitor does in the circuit.
//
// The delay on the clean path is not optional. The drive path is oversampled;
// without a matched delay the blend comb filters exactly where bass lives.
class DrivePedal final : public Pedal
{
public:
    explicit DrivePedal (juce::AudioProcessorValueTreeState& state);

    static void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void reset() override;
    void process (juce::dsp::AudioBlock<float>& block) override;
    int  getLatencySamples() const override { return latencySamples; }
    const char* getName() const override { return "Drive"; }

private:
    void updateSmoothedTargets();
    void settleDcPath();

    juce::AudioProcessorValueTreeState& apvts;

    // Raw atomic pointers -- read on the audio thread without locking.
    std::atomic<float>* trimParam   = nullptr;
    std::atomic<float>* driveParam  = nullptr;
    std::atomic<float>* blendParam  = nullptr;
    std::atomic<float>* levelParam  = nullptr;
    std::atomic<float>* masterParam = nullptr;
    std::atomic<float>* gruntParam  = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* bassParam      = nullptr;
    std::atomic<float>* trebleParam    = nullptr;
    std::atomic<float>* loMidParam     = nullptr;
    std::atomic<float>* hiMidParam     = nullptr;
    std::atomic<float>* loMidFreqParam = nullptr;
    std::atomic<float>* hiMidFreqParam = nullptr;

    using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
    Smoothed trimGain, driveGain, makeupGain, blendAmount, levelGain, masterGain;

    juce::dsp::StateVariableTPTFilter<float> dcBlocker;   // 20 Hz HP, both paths
    juce::dsp::StateVariableTPTFilter<float> gruntFilter; // drive path only
    juce::dsp::StateVariableTPTFilter<float> recoveryLpf;  // runs oversampled
    juce::dsp::StateVariableTPTFilter<float> couplingFilter;   // oversampled, between stages
    juce::dsp::StateVariableTPTFilter<float> outputDcBlocker; // oversampled, after stage B

    // Antiderivative anti-aliasing. This is what lets the pedal run at 2x
    // instead of the 8x a naive waveshaper would need -- see DESIGN.md.
    //
    // The tables are large (~11 MB for the pair), so they are shared across
    // every instance in the process via SharedResourcePointer rather than
    // rebuilt per plugin instance. Declared before the shapers so it outlives
    // them.
    //
    // Note the range: chowdsp's lookup tables CLAMP out-of-range inputs. That
    // is harmless for tanh itself, which is flat to within 1e-9 past |x| = 10,
    // but its antiderivatives keep growing, so a clamped input silently
    // corrupts the ADAA maths. shaperInputRange must therefore stay above
    // anything the drive gain can produce, and process() clamps to it.
    static constexpr float shaperInputRange = 25.0f;

    chowdsp::SharedLookupTableCache lutCache;
    chowdsp::ADAATanhClipper<float>      stageA { &lutCache.get(), shaperInputRange, 1 << 17 };
    chowdsp::ADAASoftClipper<float, 5>   stageB { &lutCache.get(), 10.0f, 1 << 16 };

    // Each ADAA stage adds exactly one sample of latency at the oversampled
    // rate, even when bypassed.
    static constexpr int numShaperStages = 2;

    // Attack is a 3-way switch, so all three shelf responses are designed once
    // in prepare() and selected by swapping a refcounted pointer -- no
    // coefficient maths and no allocation on the audio thread.
    //
    // Deliberately NOT a ProcessorDuplicator: the duplicator passes `state` to
    // each per-channel filter when it builds them in prepare(), so assigning
    // `state` later changes the duplicator's pointer and not the filters'. That
    // silently leaves every channel running default (empty) coefficients.
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    std::array<Coeffs::Ptr, 3> attackShelves;
    std::vector<juce::dsp::IIR::Filter<float>> attackFilters; // sized in prepare
    int currentAttackIndex = -1;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> cleanDelay { 64 };

    // Sits after the blend, so it shapes the combined signal the way the EQ
    // section of the real pedal does rather than only the driven half.
    ToneStack toneStack;

    juce::AudioBuffer<float> cleanBuffer; // sized in prepare, never on the audio thread
    int latencySamples = 0;
    int preparedChannels = 0;
    double sampleRate = 44100.0;


};
