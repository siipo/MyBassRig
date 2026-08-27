#include "TestSignals.h"
#include "dsp/ToneStack.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_dsp/juce_dsp.h>

using namespace TestSignals;

namespace
{
    constexpr int fftOrder = 15, fftSize = 1 << fftOrder;

    juce::AudioBuffer<float> sawtooth (float f0, int numSamples, float amplitude = 0.4f)
    {
        juce::AudioBuffer<float> b (1, numSamples);
        b.clear();
        auto* d = b.getWritePointer (0);

        // Built additively so the INPUT is alias free and cannot contaminate a
        // measurement of what the pedal itself does.
        for (int h = 1; (float) h * f0 < 9000.0f; ++h)
            for (int i = 0; i < numSamples; ++i)
                d[i] += amplitude / (float) h
                      * std::sin (juce::MathConstants<float>::twoPi * (float) h * f0
                                  * (float) i / (float) sampleRate);
        return b;
    }

    struct Spectrum { double fundamental = 0, harmonics = 0, aboveTwoK = 0, total = 0; };

    Spectrum analyse (juce::AudioBuffer<float> input, float f0, float driveKnob,
                      int grunt, int attack)
    {
        constexpr int warmup = 8192;

        Fixture f;
        f.host.setParam (ParamID::blend, 1.0f);
        f.host.setParam (ParamID::drive, driveKnob);
        f.host.setParam (ParamID::grunt, (float) grunt);
        f.host.setParam (ParamID::attack, (float) attack);
        f.prepare();

        const auto out = f.run (std::move (input));

        std::vector<float> bins ((size_t) fftSize * 2, 0.0f);
        std::copy (out.getReadPointer (0) + warmup,
                   out.getReadPointer (0) + warmup + fftSize, bins.begin());
        juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
                                                    juce::dsp::WindowingFunction<float>::hann);
        window.multiplyWithWindowingTable (bins.data(), (size_t) fftSize);
        juce::dsp::FFT (fftOrder).performFrequencyOnlyForwardTransform (bins.data());

        const auto binHz = (float) sampleRate / (float) fftSize;
        Spectrum s;

        for (int k = 1; k < fftSize / 2; ++k)
        {
            const auto hz = (float) k * binHz;
            const auto energy = (double) bins[(size_t) k] * bins[(size_t) k];

            s.total += energy;
            if (std::abs (hz - f0) < 3.0f * binHz) s.fundamental += energy;
            else if (hz > 1.2f * f0)               s.harmonics += energy;
            if (hz > 2000.0f)                      s.aboveTwoK += energy;
        }

        return s;
    }

    double ratioDb (double a, double b)
    {
        return 10.0 * std::log10 (juce::jmax (a / juce::jmax (b, 1e-30), 1e-30));
    }

    double harmonicsDb (float driveKnob)
    {
        constexpr float f0 = 220.0f;
        const auto s = analyse (sine (f0, 8192 + fftSize, 1, 0.5f), f0, driveKnob, 0, 1);
        return ratioDb (s.harmonics, s.fundamental);
    }

    float drivePathLevelDb (float driveKnob)
    {
        Fixture f;
        f.host.setParam (ParamID::blend, 1.0f);
        f.host.setParam (ParamID::drive, driveKnob);
        f.host.setParam (ParamID::grunt, 0.0f);
        f.prepare();

        const auto out = f.run (sine (220.0f, 32768, 1, 0.5f));
        return juce::Decibels::gainToDecibels (out.getRMSLevel (0, 8192, 24576)
                                               / (0.5f / std::sqrt (2.0f)));
    }
}

// The old mapping was gain = jmap(knob^2, 1, 20), which measured 17.7 dB of
// harmonic growth over the lower half of the knob and 1.6 dB over the upper
// half: the top half of the control did essentially nothing. The taper table is
// the inverse of the measured curve, so growth is even.
TEST_CASE ("drive knob spreads distortion evenly across its travel", "[voicing][drive]")
{
    std::vector<double> measured;

    for (const auto knob : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        measured.push_back (harmonicsDb (knob));

    const auto span = measured.back() - measured.front();
    INFO ("harmonics across the knob: " << measured[0] << ", " << measured[1] << ", "
          << measured[2] << ", " << measured[3] << ", " << measured[4] << " dB");

    REQUIRE (span > 15.0);

    // Every quarter must carry a real share of the range. The old taper put
    // under 10 percent of it in the top half and would fail here.
    for (size_t i = 1; i < measured.size(); ++i)
    {
        const auto step = measured[i] - measured[i - 1];
        INFO ("quarter " << i << " contributes " << step << " dB of " << span);
        REQUIRE (step > span * 0.12);
    }
}

// Drive is a character control, not a volume control. The compensation used to
// be 1/sqrt(gain), which sagged 5.1 dB by the top of the travel.
TEST_CASE ("drive knob does not change output level", "[voicing][drive]")
{
    const auto reference = drivePathLevelDb (0.0f);

    for (const auto knob : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const auto level = drivePathLevelDb (knob);
        INFO ("knob " << knob << ": " << level << " dB against " << reference << " dB");
        REQUIRE (std::abs (level - reference) < 1.0f);
    }
}

// Blend can only be a balance control if the two paths arrive at comparable
// level. The drive path measured 4.0 dB hotter than clean before the makeup
// table was normalised.
TEST_CASE ("drive and clean paths leave the pedal at matching level", "[voicing][drive]")
{
    const auto clean = steadyRms (0.0f, 220.0f);
    const auto drive = steadyRms (1.0f, 220.0f);

    const auto differenceDb = juce::Decibels::gainToDecibels (drive / clean);
    INFO ("drive path is " << differenceDb << " dB relative to clean");
    REQUIRE (std::abs (differenceDb) < 1.5f);
}

// Grunt decides how much low end reaches the clipper, the control that keeps a
// bass drive from turning the bottom to mud. Each position must do something
// clearly different at the bottom of the range.
TEST_CASE ("grunt positions are distinct on a low note", "[voicing][grunt]")
{
    std::vector<double> distortion;

    for (int position = 0; position < 3; ++position)
    {
        const auto s = analyse (sine (41.2f, 8192 + fftSize, 1, 0.5f), 41.2f, 0.7f, position, 1);
        distortion.push_back (ratioDb (s.harmonics, s.fundamental));
    }

    INFO ("Full " << distortion[0] << " dB, Mid " << distortion[1]
          << " dB, Tight " << distortion[2] << " dB");

    // Tighter settings must let the low fundamental through more cleanly.
    REQUIRE (distortion[0] > distortion[1] + 4.0);
    REQUIRE (distortion[1] > distortion[2] + 4.0);
}

// Attack is pre-emphasis BEFORE the clipper, so it can only act on content the
// input already carries above the shelf. Measuring it with a pure sine shows
// nothing at all and briefly looked like a broken control; a real note has the
// upper harmonics for it to work on.
TEST_CASE ("attack shifts the top end on a harmonically rich note", "[voicing][attack]")
{
    std::vector<double> high;

    for (int position = 0; position < 3; ++position)
    {
        const auto s = analyse (sawtooth (82.4f, 8192 + fftSize), 82.4f, 0.7f, 1, position);
        high.push_back (ratioDb (s.aboveTwoK, s.total));
    }

    INFO ("Boost " << high[0] << " dB, Flat " << high[1] << " dB, Cut " << high[2] << " dB");

    REQUIRE (high[0] > high[1] + 1.0);
    REQUIRE (high[1] > high[2] + 1.0);
}

// The EQ tapers are inverted from measured curves so decibels track knob
// position. A power law cannot do that without an infinite slope at the centre
// detent, which is exactly what the previous fit had.
TEST_CASE ("tone controls spread decibels evenly across their travel", "[voicing][tone]")
{
    const auto gainAt = [] (bool treble, float knob, float frequency)
    {
        ToneStack stack;
        stack.prepare ({ sampleRate, (juce::uint32) blockSize, 1 });
        stack.setParams (treble ? 0.5f : knob, treble ? knob : 0.5f, 0.0f, 0, 0.0f, 1);

        auto buffer = sine (frequency, 32768, 1, 0.25f);

        for (int pos = 0; pos + blockSize <= buffer.getNumSamples(); pos += blockSize)
        {
            auto block = juce::dsp::AudioBlock<float> (buffer)
                             .getSubBlock ((size_t) pos, (size_t) blockSize);
            stack.process (block);
        }

        return juce::Decibels::gainToDecibels (buffer.getRMSLevel (0, 8192, 24576)
                                               / (0.25f / std::sqrt (2.0f)));
    };

    for (const auto treble : { false, true })
    {
        const auto frequency = treble ? 10000.0f : 60.0f;
        const auto name = treble ? "treble" : "bass";

        // Each half of the travel spreads its own range, so the halves are
        // checked separately: this circuit offers far more boost than cut.
        for (const auto half : { 0, 1 })
        {
            const auto a = gainAt (treble, half == 0 ? 0.0f : 0.5f, frequency);
            const auto mid = gainAt (treble, half == 0 ? 0.25f : 0.75f, frequency);
            const auto b = gainAt (treble, half == 0 ? 0.5f : 1.0f, frequency);

            const auto expected = 0.5f * (a + b);
            INFO (name << " half " << half << ": " << a << " / " << mid << " / " << b
                  << " dB, midpoint expected " << expected);

            REQUIRE (std::abs (mid - expected) < 1.0f);
        }
    }
}
