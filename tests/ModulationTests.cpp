#include "TestSignals.h"
#include "dsp/ChorusPedal/ChorusPedal.h"
#include "dsp/PhaserPedal/PhaserPedal.h"

#include <catch2/catch_test_macros.hpp>

using namespace TestSignals;

namespace
{
    template <typename PedalType>
    struct ModFixture
    {
        HarnessProcessor host;
        PedalType pedal { host.apvts };

        void set (const char* id, float value) { host.setParam (id, value); }

        void prepare (int numChannels = 1)
        {
            pedal.prepare ({ sampleRate, (juce::uint32) blockSize, (juce::uint32) numChannels });
        }

        juce::AudioBuffer<float> run (juce::AudioBuffer<float> buffer)
        {
            for (int pos = 0; pos + blockSize <= buffer.getNumSamples(); pos += blockSize)
            {
                auto block = juce::dsp::AudioBlock<float> (buffer)
                                 .getSubBlock ((size_t) pos, (size_t) blockSize);
                pedal.process (block);
            }

            return buffer;
        }
    };

    // Test tones whose period is a whole number of samples at 48 kHz. This
    // matters: measuring the level of a 45 Hz tone over a fixed 2048-sample
    // window covers 1.9 cycles, and the leftover fraction moves the reading
    // around far more than the effect being measured does. Both of these
    // divide exactly.
    constexpr float lowTone = 46.875f;    // 48000 / 1024
    constexpr float highTone = 1200.0f;   // 48000 / 40
    constexpr float midTone = 750.0f;     // 48000 / 64
    constexpr float sweepTone = 600.0f;   // 48000 / 80
    constexpr float boundaryTone = 187.5f; // 48000 / 256

    // How much the level wobbles over time, as a fraction of its average.
    //
    // This is the measurement that matters for a modulation effect: a modulated
    // band beats against the dry one and its level moves, while an untouched
    // band sits still. It also sidesteps the fact that an all-pass complementary
    // crossover shifts phase even where it does not change magnitude, so
    // comparing samples against the input directly would prove nothing.
    //
    // The window is a whole number of cycles of the tone being measured, so the
    // reading is the modulation rather than the windowing.
    double modulationDepth (const juce::AudioBuffer<float>& buffer, int startSample,
                            float frequency)
    {
        const auto period = juce::roundToInt (sampleRate / (double) frequency);
        // ~1024 samples: long enough for a clean level reading, short enough
        // that a window does not average across a chunk of the LFO cycle and
        // flatten the very thing being measured.
        const auto cycles = juce::jmax (1, 1024 / period);
        const auto window = period * cycles;

        double lowest = 1.0e9, highest = 0.0, sum = 0.0;
        int windows = 0;

        for (int pos = startSample; pos + window <= buffer.getNumSamples(); pos += window)
        {
            const auto rms = (double) buffer.getRMSLevel (0, pos, window);
            lowest = juce::jmin (lowest, rms);
            highest = juce::jmax (highest, rms);
            sum += rms;
            ++windows;
        }

        const auto mean = windows > 0 ? sum / windows : 0.0;
        return mean > 1.0e-9 ? (highest - lowest) / mean : 0.0;
    }
}

//==============================================================================
TEST_CASE ("chorus is transparent when switched off", "[chorus]")
{
    ModFixture<ChorusPedal> f;
    f.set (ParamID::chorusOn, 0.0f);
    f.set (ParamID::chorusDepth, 7.0f);
    f.prepare();

    const auto input = sine (220.0f, 16384, 1, 0.5f);
    const auto output = f.run (sine (220.0f, 16384, 1, 0.5f));

    for (int i = 0; i < input.getNumSamples(); ++i)
        REQUIRE (output.getSample (0, i) == input.getSample (0, i));
}

// The reason this is a bass chorus rather than a guitar one. A guitar chorus on
// a bass makes the fundamental wander and the note stops sitting still; below
// the crossover, nothing here moves at all.
TEST_CASE ("the crossover keeps the low end still", "[chorus][crossover]")
{
    const auto wobbleAt = [] (float frequency)
    {
        ModFixture<ChorusPedal> f;
        f.set (ParamID::chorusOn, 1.0f);
        f.set (ParamID::chorusRate, 4.0f);
        f.set (ParamID::chorusDepth, 6.0f);
        f.set (ParamID::chorusMix, 0.5f);
        f.set (ParamID::chorusCrossover, 200.0f);
        f.prepare();
        return modulationDepth (f.run (sine (frequency, 65536, 1, 0.5f)), 16384, frequency);
    };

    const auto low = wobbleAt (lowTone);     // well below the crossover
    const auto high = wobbleAt (highTone);   // well above it

    INFO ("modulation depth: " << lowTone << " Hz " << low << ", " << highTone << " Hz " << high);

    REQUIRE (high > 0.1);         // the top is genuinely being chorused
    REQUIRE (low < high * 0.25);  // and the bottom is not
}

TEST_CASE ("the crossover control moves the boundary", "[chorus][crossover]")
{
    const auto wobbleWithCrossover = [] (float crossoverHz)
    {
        ModFixture<ChorusPedal> f;
        f.set (ParamID::chorusOn, 1.0f);
        f.set (ParamID::chorusRate, 4.0f);
        f.set (ParamID::chorusDepth, 6.0f);
        f.set (ParamID::chorusCrossover, crossoverHz);
        f.prepare();
        return modulationDepth (f.run (sine (boundaryTone, 65536, 1, 0.5f)), 16384, boundaryTone);
    };

    // The tone is bracketed by the two crossover settings, so it moves from
    // clearly above the boundary to clearly below it. Picking a tone the
    // crossover range cannot get past leaves it partly modulated either way.
    const auto modulated = wobbleWithCrossover (80.0f);
    const auto protectedLow = wobbleWithCrossover (500.0f);

    INFO (boundaryTone << " Hz wobble: crossover 80 Hz " << modulated
          << ", crossover 500 Hz " << protectedLow);
    REQUIRE (modulated > protectedLow * 2.0);
}

TEST_CASE ("chorus stays finite at full depth", "[chorus][stability]")
{
    ModFixture<ChorusPedal> f;
    f.set (ParamID::chorusOn, 1.0f);
    f.set (ParamID::chorusDepth, 7.0f);
    f.set (ParamID::chorusRate, 8.0f);
    f.set (ParamID::chorusMix, 1.0f);
    f.prepare (2);

    const auto out = f.run (sine (41.2f, 32768, 2, 0.9f));

    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
            REQUIRE (std::isfinite (out.getSample (ch, i)));
}

TEST_CASE ("chorus adds no latency", "[chorus][latency]")
{
    ModFixture<ChorusPedal> f;
    f.prepare();
    REQUIRE (f.pedal.getLatencySamples() == 0);
}

//==============================================================================
TEST_CASE ("phaser is transparent when switched off", "[phaser]")
{
    ModFixture<PhaserPedal> f;
    f.set (ParamID::phaserOn, 0.0f);
    f.set (ParamID::phaserFeedback, 1.0f);
    f.prepare();

    const auto input = sine (220.0f, 16384, 1, 0.5f);
    const auto output = f.run (sine (220.0f, 16384, 1, 0.5f));

    for (int i = 0; i < input.getNumSamples(); ++i)
        REQUIRE (output.getSample (0, i) == input.getSample (0, i));
}

TEST_CASE ("phaser sweeps", "[phaser]")
{
    ModFixture<PhaserPedal> f;
    f.set (ParamID::phaserOn, 1.0f);
    f.set (ParamID::phaserRate, 3.0f);
    f.set (ParamID::phaserDepth, 1.0f);
    f.set (ParamID::phaserMix, 0.5f);
    f.prepare();

    // A note inside the sweep range beats against the dry path as the notches
    // move across it.
    const auto depth = modulationDepth (f.run (sine (midTone, 65536, 1, 0.5f)), 16384, midTone);

    INFO ("modulation depth at " << midTone << " Hz: " << depth);
    REQUIRE (depth > 0.15);
}

// The bass-specific decision, reached from a different direction than the
// chorus crossover: notches that wander onto the fundamental hollow the note
// out, so the sweep floor sits well above it.
TEST_CASE ("the sweep floor keeps the notches off the fundamental", "[phaser]")
{
    const auto depthAt = [] (float frequency)
    {
        ModFixture<PhaserPedal> f;
        f.set (ParamID::phaserOn, 1.0f);
        f.set (ParamID::phaserRate, 3.0f);
        f.set (ParamID::phaserDepth, 1.0f);
        f.set (ParamID::phaserMix, 0.5f);
        f.prepare();
        return modulationDepth (f.run (sine (frequency, 65536, 1, 0.5f)), 16384, frequency);
    };

    const auto fundamental = depthAt (lowTone);
    const auto inRange = depthAt (midTone);

    INFO ("modulation depth: " << lowTone << " Hz " << fundamental
          << ", " << midTone << " Hz " << inRange);
    REQUIRE (fundamental < inRange * 0.4);
}

TEST_CASE ("stage count changes the voice", "[phaser]")
{
    const auto renderWith = [] (float stages)
    {
        ModFixture<PhaserPedal> f;
        f.set (ParamID::phaserOn, 1.0f);
        f.set (ParamID::phaserStages, stages);
        f.set (ParamID::phaserRate, 0.5f);
        f.set (ParamID::phaserDepth, 0.8f);
        f.prepare();
        return f.run (sine (sweepTone, 16384, 1, 0.5f));
    };

    const auto four = renderWith (0.0f);
    const auto eight = renderWith (2.0f);

    double difference = 0.0;

    for (int i = 8192; i < four.getNumSamples(); ++i)
        difference += std::abs (four.getSample (0, i) - eight.getSample (0, i));

    INFO ("accumulated difference between 4 and 8 stages: " << difference);
    REQUIRE (difference > 1.0);
}

TEST_CASE ("inverting moves the notches somewhere else", "[phaser]")
{
    const auto renderWith = [] (float invert)
    {
        ModFixture<PhaserPedal> f;
        f.set (ParamID::phaserOn, 1.0f);
        f.set (ParamID::phaserInvert, invert);
        f.set (ParamID::phaserRate, 0.5f);
        f.prepare();
        return f.run (sine (sweepTone, 16384, 1, 0.5f));
    };

    const auto normal = renderWith (0.0f);
    const auto inverted = renderWith (1.0f);

    double difference = 0.0;

    for (int i = 8192; i < normal.getNumSamples(); ++i)
        difference += std::abs (normal.getSample (0, i) - inverted.getSample (0, i));

    INFO ("accumulated difference: " << difference);
    REQUIRE (difference > 1.0);
}

// A phaser with high feedback and a low sweep is a resonance like any other.
// The tanh on the feedback path is what stops it becoming an oscillator.
TEST_CASE ("phaser feedback does not run away", "[phaser][stability]")
{
    ModFixture<PhaserPedal> f;
    f.set (ParamID::phaserOn, 1.0f);
    f.set (ParamID::phaserFeedback, 1.0f);
    f.set (ParamID::phaserDepth, 1.0f);
    f.set (ParamID::phaserStages, 2.0f);   // 8 stages
    f.set (ParamID::phaserMix, 1.0f);
    f.prepare (2);

    const auto out = f.run (sine (110.0f, 65536, 2, 0.9f));

    float peak = 0.0f;

    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const auto sample = out.getSample (ch, i);
            REQUIRE (std::isfinite (sample));
            peak = juce::jmax (peak, std::abs (sample));
        }

    INFO ("peak with feedback at maximum: " << peak);
    REQUIRE (peak < 10.0f);
}

TEST_CASE ("phaser adds no latency", "[phaser][latency]")
{
    ModFixture<PhaserPedal> f;
    f.prepare();
    REQUIRE (f.pedal.getLatencySamples() == 0);
}
