#include "TestSignals.h"
#include "dsp/EnvelopeFilterPedal/EnvelopeFilterPedal.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_dsp/juce_dsp.h>

using namespace TestSignals;

namespace
{
    constexpr int fftOrder = 14, fftSize = 1 << fftOrder;

    struct EnvFixture
    {
        HarnessProcessor host;
        EnvelopeFilterPedal pedal { host.apvts };

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

    // A note with harmonics, so there is something for a sweeping filter to
    // move across. A pure sine tells you almost nothing about a filter.
    juce::AudioBuffer<float> richNote (float f0, int numSamples, float amplitude)
    {
        juce::AudioBuffer<float> b (1, numSamples);
        b.clear();
        auto* d = b.getWritePointer (0);

        for (int h = 1; (float) h * f0 < 9000.0f; ++h)
            for (int i = 0; i < numSamples; ++i)
                d[i] += amplitude / (float) h
                      * std::sin (juce::MathConstants<float>::twoPi * (float) h * f0
                                  * (float) i / (float) sampleRate);
        return b;
    }

    // Centre of mass of the spectrum. A filter sweeping up drags this up with
    // it, which makes "did the sweep happen, and in which direction" a number
    // rather than an opinion.
    double spectralCentroid (const juce::AudioBuffer<float>& buffer, int startSample)
    {
        std::vector<float> bins ((size_t) fftSize * 2, 0.0f);
        std::copy (buffer.getReadPointer (0) + startSample,
                   buffer.getReadPointer (0) + startSample + fftSize, bins.begin());

        juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
                                                    juce::dsp::WindowingFunction<float>::hann);
        window.multiplyWithWindowingTable (bins.data(), (size_t) fftSize);
        juce::dsp::FFT (fftOrder).performFrequencyOnlyForwardTransform (bins.data());

        const auto binHz = sampleRate / (double) fftSize;
        double weighted = 0.0, total = 0.0;

        for (int k = 1; k < fftSize / 2; ++k)
        {
            const auto magnitude = (double) bins[(size_t) k];
            weighted += magnitude * (double) k * binHz;
            total += magnitude;
        }

        return total > 0.0 ? weighted / total : 0.0;
    }
}

TEST_CASE ("envelope filter is transparent when switched off", "[env]")
{
    EnvFixture f;
    f.set (ParamID::envOn, 0.0f);
    f.set (ParamID::envQ, 12.0f);
    f.prepare();

    const auto input = richNote (110.0f, 8192, 0.4f);
    const auto output = f.run (richNote (110.0f, 8192, 0.4f));

    for (int i = 0; i < input.getNumSamples(); ++i)
        REQUIRE (output.getSample (0, i) == input.getSample (0, i));

    REQUIRE (f.pedal.getEnvelope() == 0.0f);
}

// The whole point of the pedal: a loud note should open the filter further than
// a quiet one, so the tone follows how hard you play.
TEST_CASE ("a harder note opens the filter further", "[env]")
{
    const auto centroidFor = [] (float amplitude)
    {
        EnvFixture f;
        f.set (ParamID::envOn, 1.0f);
        f.set (ParamID::envMode, 0.0f);     // Up
        f.set (ParamID::envDryHighs, 0.0f); // isolate the filter
        f.prepare();
        return spectralCentroid (f.run (richNote (110.0f, 8192 + fftSize, amplitude)), 8192);
    };

    const auto quiet = centroidFor (0.02f);
    const auto loud  = centroidFor (0.6f);

    INFO ("centroid: quiet note " << quiet << " Hz, loud note " << loud << " Hz");
    REQUIRE (loud > quiet * 1.5);
}

// Down mode is the synth-bass voice: the same hard note should close the filter
// rather than open it.
TEST_CASE ("Down mode sweeps the other way", "[env]")
{
    const auto centroidFor = [] (int mode, float amplitude)
    {
        EnvFixture f;
        f.set (ParamID::envOn, 1.0f);
        f.set (ParamID::envMode, (float) mode);
        f.set (ParamID::envDryHighs, 0.0f);
        f.prepare();
        return spectralCentroid (f.run (richNote (110.0f, 8192 + fftSize, amplitude)), 8192);
    };

    const auto upQuiet = centroidFor (0, 0.02f);
    const auto upLoud  = centroidFor (0, 0.6f);
    const auto downQuiet = centroidFor (1, 0.02f);
    const auto downLoud  = centroidFor (1, 0.6f);

    INFO ("Up: " << upQuiet << " -> " << upLoud << " Hz; Down: "
          << downQuiet << " -> " << downLoud << " Hz");

    REQUIRE (upLoud > upQuiet);
    REQUIRE (downLoud < downQuiet);
}

// Hi-Q is the quacky voice: a band-pass rather than a low-pass, so it should
// throw away the low end that Up mode keeps.
TEST_CASE ("Hi-Q is a band-pass voice, not another low-pass", "[env]")
{
    const auto lowEnergyFor = [] (int mode)
    {
        EnvFixture f;
        f.set (ParamID::envOn, 1.0f);
        f.set (ParamID::envMode, (float) mode);
        f.set (ParamID::envDryHighs, 0.0f);
        f.set (ParamID::envSens, 0.8f);
        f.prepare();

        const auto out = f.run (richNote (110.0f, 8192 + fftSize, 0.5f));

        std::vector<float> bins ((size_t) fftSize * 2, 0.0f);
        std::copy (out.getReadPointer (0) + 8192, out.getReadPointer (0) + 8192 + fftSize, bins.begin());
        juce::dsp::WindowingFunction<float> w ((size_t) fftSize,
                                               juce::dsp::WindowingFunction<float>::hann);
        w.multiplyWithWindowingTable (bins.data(), (size_t) fftSize);
        juce::dsp::FFT (fftOrder).performFrequencyOnlyForwardTransform (bins.data());

        const auto binHz = sampleRate / (double) fftSize;
        double low = 0.0, total = 0.0;

        for (int k = 1; k < fftSize / 2; ++k)
        {
            const auto e = (double) bins[(size_t) k] * bins[(size_t) k];
            total += e;
            if ((double) k * binHz < 200.0) low += e;
        }

        return total > 0.0 ? low / total : 0.0;
    };

    const auto up = lowEnergyFor (0);
    const auto hiQ = lowEnergyFor (2);

    INFO ("share of energy below 200 Hz: Up " << up << ", Hi-Q " << hiQ);
    REQUIRE (hiQ < up * 0.7);
}

// The bass-specific control. A resonant low-pass swallows string definition, so
// the mix-in blends filtered dry highs back over the top.
TEST_CASE ("the dry highs control blends definition back in", "[env]")
{
    const auto highEnergyFor = [] (float dryHighs)
    {
        EnvFixture f;
        f.set (ParamID::envOn, 1.0f);
        f.set (ParamID::envMode, 0.0f);
        f.set (ParamID::envSens, 0.15f);     // filter mostly closed
        f.set (ParamID::envDryHighs, dryHighs);
        f.prepare();

        const auto out = f.run (richNote (110.0f, 8192 + fftSize, 0.3f));

        std::vector<float> bins ((size_t) fftSize * 2, 0.0f);
        std::copy (out.getReadPointer (0) + 8192, out.getReadPointer (0) + 8192 + fftSize, bins.begin());
        juce::dsp::WindowingFunction<float> w ((size_t) fftSize,
                                               juce::dsp::WindowingFunction<float>::hann);
        w.multiplyWithWindowingTable (bins.data(), (size_t) fftSize);
        juce::dsp::FFT (fftOrder).performFrequencyOnlyForwardTransform (bins.data());

        const auto binHz = sampleRate / (double) fftSize;
        double high = 0.0;

        for (int k = 1; k < fftSize / 2; ++k)
            if ((double) k * binHz > 2000.0)
                high += (double) bins[(size_t) k] * bins[(size_t) k];

        return high;
    };

    const auto none = highEnergyFor (0.0f);
    const auto blended = highEnergyFor (0.8f);

    INFO ("energy above 2 kHz: none " << none << ", blended " << blended);
    REQUIRE (blended > none * 4.0);
}

TEST_CASE ("attack and release change how fast the sweep follows", "[env]")
{
    const auto envelopeAfter = [] (float attackMs, int samples)
    {
        EnvFixture f;
        f.set (ParamID::envOn, 1.0f);
        f.set (ParamID::envAttack, attackMs);
        f.set (ParamID::envSens, 0.9f);
        f.prepare();
        f.run (richNote (110.0f, samples, 0.5f));
        return f.pedal.getEnvelope();
    };

    // Measured a short way into the note: a fast attack is further along.
    const auto fast = envelopeAfter (2.0f, blockSize * 2);
    const auto slow = envelopeAfter (90.0f, blockSize * 2);

    INFO ("envelope after ~21 ms: fast attack " << fast << ", slow attack " << slow);
    REQUIRE (fast > slow);
}

// High Q over a low fundamental is where a resonant filter runs away. The ARP
// model is used in limit mode specifically to stop that.
TEST_CASE ("high resonance does not run away", "[env][stability]")
{
    EnvFixture f;
    f.set (ParamID::envOn, 1.0f);
    f.set (ParamID::envQ, 12.0f);
    f.set (ParamID::envMode, 2.0f);      // Hi-Q multiplies it further
    f.set (ParamID::envSens, 1.0f);
    f.set (ParamID::envRange, 5.0f);
    f.prepare (2);

    const auto out = f.run (sine (41.2f, 32768, 2, 0.95f));

    float peak = 0.0f;

    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const auto sample = out.getSample (ch, i);
            REQUIRE (std::isfinite (sample));
            peak = juce::jmax (peak, std::abs (sample));
        }

    INFO ("peak with Q at maximum: " << peak);
    REQUIRE (peak < 12.0f);
}

TEST_CASE ("envelope filter adds no latency", "[env][latency]")
{
    EnvFixture f;
    f.prepare();
    REQUIRE (f.pedal.getLatencySamples() == 0);
}
