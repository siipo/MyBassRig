#include "TestSignals.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_dsp/juce_dsp.h>

using namespace TestSignals;

namespace
{
    constexpr int fftOrder = 15, fftSize = 1 << fftOrder;   // 32768 bins

    struct Spectrum
    {
        double belowFundamental = 0.0;
        double fundamental      = 0.0;
        double aboveFundamental = 0.0;
        double evenHarmonics    = 0.0;
        double oddHarmonics     = 0.0;

        [[nodiscard]] double aliasingDb() const
        {
            return 10.0 * std::log10 (juce::jmax (belowFundamental / fundamental, 1e-30));
        }

        [[nodiscard]] double harmonicsDb() const
        {
            return 10.0 * std::log10 (juce::jmax (aboveFundamental / fundamental, 1e-30));
        }

        [[nodiscard]] double evenOverOddDb() const
        {
            return 10.0 * std::log10 (juce::jmax (evenHarmonics / juce::jmax (oddHarmonics, 1e-30), 1e-30));
        }
    };

    // Drives the pedal with a pure sine and splits the output spectrum into
    // three parts. The useful one is `belowFundamental`: a memoryless
    // nonlinearity fed a sine puts every harmonic ABOVE the fundamental, so
    // anything below it folded back down from above Nyquist. That is aliasing,
    // measured rather than assumed.
    //
    // The fundamental is chosen off the FFT bin grid on purpose. On an exact
    // bin, aliased partials land back on multiples of the fundamental's bin and
    // hide inside the harmonics.
    Spectrum analyse (float f0, float driveKnob)
    {
        constexpr int warmup = 8192;
        constexpr int total  = warmup + fftSize;

        Fixture f;
        f.host.setParam (ParamID::blend, 1.0f);   // drive path only
        f.host.setParam (ParamID::grunt, 0.0f);
        f.host.setParam (ParamID::drive, driveKnob);
        f.prepare();

        const auto out = f.run (sine (f0, total));

        std::vector<float> bins ((size_t) fftSize * 2, 0.0f);
        std::copy (out.getReadPointer (0) + warmup, out.getReadPointer (0) + total, bins.begin());

        juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
                                                    juce::dsp::WindowingFunction<float>::hann);
        window.multiplyWithWindowingTable (bins.data(), (size_t) fftSize);
        juce::dsp::FFT (fftOrder).performFrequencyOnlyForwardTransform (bins.data());

        const auto binHz = (float) sampleRate / (float) fftSize;
        Spectrum s;

        for (int k = 1; k < fftSize / 2; ++k)
        {
            const auto hz     = (float) k * binHz;
            const auto energy = (double) bins[(size_t) k] * bins[(size_t) k];

            if (std::abs (hz - f0) < 3.0f * binHz)  s.fundamental      += energy;
            else if (hz > 25.0f && hz < 0.8f * f0)  s.belowFundamental += energy;
            else if (hz > 1.2f * f0)                s.aboveFundamental += energy;

            for (int harmonic = 2; harmonic <= 8; ++harmonic)
                if (std::abs (hz - (float) harmonic * f0) < 3.0f * binHz)
                    (harmonic % 2 == 0 ? s.evenHarmonics : s.oddHarmonics) += energy;
        }

        return s;
    }
}

// The reason the pedal can run at 2x instead of the 8x a naive waveshaper needs.
//
// Measured against the identical chain with the ADAA stages swapped for direct
// tanh and polynomial evaluation (build with -DBASSRIG_NAIVE_SHAPERS=ON), at
// full drive:
//
//     f0        ADAA      naive     benefit
//     1493 Hz   -78.0     -58.9     19 dB
//     2999 Hz   -64.7     -42.1     23 dB
//     4001 Hz   -67.7     -51.8     16 dB
//
// -60 dB clears every ADAA figure by at least 4.7 dB. On the failing side the
// margin is uneven: at 2999 Hz the naive build misses by 18 dB, but at 1493 Hz
// only by 1.1 dB. So this catches a total loss of anti-aliasing reliably and a
// partial regression only at the frequencies where 2x oversampling alone is not
// already doing the job. Tightening the threshold further would start flagging
// the ADAA build itself.
TEST_CASE ("nonlinearity does not alias", "[drive][aliasing]")
{
    for (const auto f0 : { 1493.0f, 2999.0f, 4001.0f })
    {
        for (const auto drive : { 0.5f, 1.0f })
        {
            const auto s = analyse (f0, drive);

            INFO ("f0 " << f0 << " Hz, drive " << drive
                  << ": aliasing " << s.aliasingDb() << " dB, harmonics "
                  << s.harmonicsDb() << " dB");

            REQUIRE (s.aliasingDb() < -60.0);
        }
    }
}

// Aliasing suppression is only worth anything if the distortion itself survives.
// Guards against "fixing" the aliasing number by quietly filtering the drive
// away.
TEST_CASE ("the cascade still generates harmonics", "[drive]")
{
    const auto s = analyse (2999.0f, 1.0f);

    INFO ("harmonic content " << s.harmonicsDb() << " dB relative to fundamental");
    REQUIRE (s.harmonicsDb() > -30.0);
}

// Stage A is asymmetric, which is what puts even harmonics in the tone, but
// asymmetry also generates DC. The coupling filter and the measured bias
// cancellation have to remove it, or the blend sits on an offset.
TEST_CASE ("drive path is DC free", "[drive][stability]")
{
    Fixture f;
    f.host.setParam (ParamID::blend, 1.0f);
    f.host.setParam (ParamID::drive, 1.0f);
    f.host.setParam (ParamID::grunt, 0.0f);
    f.prepare();

    // 93.75 Hz is exactly 512 samples per cycle at 48 kHz, and the window below
    // is a whole number of those. Measuring 110 Hz over 8192 samples instead --
    // 18.78 cycles -- reports the leftover partial cycle as if it were DC, which
    // reads as a few milli-units of offset that is not there.
    constexpr float frequency = 93.75f;
    constexpr int   start     = 16384;
    constexpr int   length    = 49152;

    const auto out = f.run (sine (frequency, start + length));

    double sum = 0.0;

    for (int i = start; i < start + length; ++i)
        sum += out.getSample (0, i);

    const auto mean = std::abs (sum / length);

    INFO ("mean output level (DC) = " << mean);
    REQUIRE (mean < 1.0e-3);
}

// Asymmetry is the point of the bias: a symmetric clipper makes odd harmonics
// only and sounds sterile on bass. Note this is measured as even-harmonic
// energy, NOT as a difference in peak height -- once a stage saturates both
// halves reach the same magnitude and peak asymmetry reads ~0 no matter how
// lopsided the waveform is. That mistake initially made a working asymmetric
// stage look symmetric.
TEST_CASE ("stage A produces even harmonics", "[drive]")
{
    const auto s = analyse (1493.0f, 0.8f);

    INFO ("even/odd harmonic ratio = " << s.evenOverOddDb() << " dB");
    REQUIRE (s.evenOverOddDb() > -30.0);
}
