#include "TestSignals.h"
#include "dsp/ToneStack.h"

#include <catch2/catch_test_macros.hpp>

using namespace TestSignals;

namespace
{
    // Magnitude response of the tone stack alone, in dB relative to input.
    float toneGainDb (float bassKnob, float trebleKnob, float frequency,
                      float loMidDb = 0.0f, int loMidFreq = 0,
                      float hiMidDb = 0.0f, int hiMidFreq = 1)
    {
        ToneStack stack;
        stack.prepare ({ sampleRate, (juce::uint32) blockSize, 1 });
        stack.setParams (bassKnob, trebleKnob, loMidDb, loMidFreq, hiMidDb, hiMidFreq);

        constexpr int numSamples = 32768, warmup = 8192;
        constexpr float amplitude = 0.25f;

        auto buffer = sine (frequency, numSamples, 1, amplitude);

        for (int pos = 0; pos + blockSize <= numSamples; pos += blockSize)
        {
            auto block = juce::dsp::AudioBlock<float> (buffer)
                             .getSubBlock ((size_t) pos, (size_t) blockSize);
            stack.process (block);
        }

        const auto out = buffer.getRMSLevel (0, warmup, numSamples - warmup);
        return juce::Decibels::gainToDecibels (out / (amplitude / std::sqrt (2.0f)));
    }
}

// The knob-to-pot mapping exists because this network is NOT flat at pot 0.5 --
// centring the pot linearly measures +6.5 dB at 40 Hz relative to 1 kHz. Flat
// lives near pot 0.90, and the taper puts it under the knob at noon.
TEST_CASE ("tone stack is flat and unity with both knobs centred", "[tone]")
{
    for (const auto frequency : { 40.0f, 100.0f, 250.0f, 1000.0f, 3000.0f, 8000.0f })
    {
        const auto dB = toneGainDb (0.5f, 0.5f, frequency);

        INFO ("at " << frequency << " Hz: " << dB << " dB");
        REQUIRE (std::abs (dB) < 0.5f);
    }
}

// A passive Baxandall has real insertion loss -- about -21 dB here -- so the
// makeup is not cosmetic. Without it the EQ section is a volume drop.
TEST_CASE ("insertion loss is compensated", "[tone]")
{
    REQUIRE (std::abs (toneGainDb (0.5f, 0.5f, 1000.0f)) < 0.5f);
}

TEST_CASE ("bass knob raises and lowers the low end", "[tone]")
{
    const auto low = toneGainDb (0.0f, 0.5f, 60.0f);
    const auto mid = toneGainDb (0.5f, 0.5f, 60.0f);
    const auto high = toneGainDb (1.0f, 0.5f, 60.0f);

    INFO ("60 Hz: min " << low << ", centre " << mid << ", max " << high);

    REQUIRE (low < mid);
    REQUIRE (mid < high);
    REQUIRE (high - low > 12.0f);          // measured ~21 dB of swing

    // A bass control has no business moving 1 kHz around.
    REQUIRE (std::abs (toneGainDb (1.0f, 0.5f, 1000.0f)) < 2.0f);
}

TEST_CASE ("treble knob raises and lowers the top end", "[tone]")
{
    const auto low = toneGainDb (0.5f, 0.0f, 10000.0f);
    const auto mid = toneGainDb (0.5f, 0.5f, 10000.0f);
    const auto high = toneGainDb (0.5f, 1.0f, 10000.0f);

    INFO ("10 kHz: min " << low << ", centre " << mid << ", max " << high);

    REQUIRE (low < mid);
    REQUIRE (mid < high);
    REQUIRE (high - low > 10.0f);          // measured ~15 dB of swing

    REQUIRE (std::abs (toneGainDb (0.5f, 1.0f, 1000.0f)) < 2.0f);
}

// Upstream's taper is clamp(1 - knob^3.333, 0.01, 0.99), which clamps for every
// knob below 0.251: the bottom quarter of both controls did nothing at all, and
// knob 0.00 and knob 0.25 measured identical. This is that guard.
TEST_CASE ("no dead zone at either end of the knob travel", "[tone]")
{
    const auto probes = { 0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f };

    float previousBass = -1.0e9f, previousTreble = -1.0e9f;

    for (const auto knob : probes)
    {
        const auto bass = toneGainDb (knob, 0.5f, 60.0f);
        const auto treble = toneGainDb (0.5f, knob, 10000.0f);

        INFO ("knob " << knob << ": bass " << bass << " dB, treble " << treble << " dB");

        // Strictly increasing, so no step of the travel is inert.
        REQUIRE (bass > previousBass + 0.15f);
        REQUIRE (treble > previousTreble + 0.05f);

        previousBass = bass;
        previousTreble = treble;
    }
}

// A pot resistance of exactly zero leaves the R-type adaptor with a degenerate
// scattering matrix and the whole stack outputs silence. That is a silent
// failure -- no NaN, no assertion, just nothing -- so it is worth a test.
TEST_CASE ("knob extremes do not collapse the WDF tree", "[tone][stability]")
{
    for (const auto bass : { 0.0f, 1.0f })
    {
        for (const auto treble : { 0.0f, 1.0f })
        {
            const auto dB = toneGainDb (bass, treble, 100.0f);

            INFO ("bass " << bass << ", treble " << treble << ": " << dB << " dB");
            REQUIRE (dB > -40.0f);
            REQUIRE (std::isfinite (dB));
        }
    }
}

TEST_CASE ("mid bands hit their nominal gain at their centre frequency", "[tone]")
{
    const auto flatAt = [] (float f) { return toneGainDb (0.5f, 0.5f, f); };

    for (int index = 0; index < 3; ++index)
    {
        const auto loHz = Params::loMidCentreHz[(size_t) index];
        const auto hiHz = Params::hiMidCentreHz[(size_t) index];

        const auto boost = toneGainDb (0.5f, 0.5f, loHz, 6.0f, index, 0.0f, 1) - flatAt (loHz);
        const auto cut   = toneGainDb (0.5f, 0.5f, hiHz, 0.0f, 0, -6.0f, index) - flatAt (hiHz);

        INFO ("index " << index << ": lo-mid " << loHz << " Hz -> " << boost
              << " dB, hi-mid " << hiHz << " Hz -> " << cut << " dB");

        REQUIRE (std::abs (boost - 6.0f) < 0.6f);
        REQUIRE (std::abs (cut + 6.0f) < 0.6f);
    }
}

TEST_CASE ("tone stack stays finite under extreme settings", "[tone][stability]")
{
    ToneStack stack;
    stack.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    stack.setParams (1.0f, 1.0f, 12.0f, 0, 12.0f, 2);

    auto buffer = sine (41.2f, 16384, 2, 0.9f);

    for (int pos = 0; pos + blockSize <= buffer.getNumSamples(); pos += blockSize)
    {
        auto block = juce::dsp::AudioBlock<float> (buffer).getSubBlock ((size_t) pos, (size_t) blockSize);
        stack.process (block);
    }

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (std::isfinite (buffer.getSample (ch, i)));
}
