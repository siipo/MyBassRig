#include "TestSignals.h"

#include <catch2/catch_test_macros.hpp>

using namespace TestSignals;

namespace { constexpr int numSamples = 8192; }

TEST_CASE ("pedal reports a non-zero latency after prepare", "[latency]")
{
    Fixture f;
    f.prepare();

    REQUIRE (f.pedal.getLatencySamples() > 0);
}

// Guards the arithmetic in prepare(): oversampling latency plus one host sample
// for the two ADAA stages.
//
// This used to be an exact equality, and it no longer can be. The tone stack
// sits after the blend, so the clean path now runs through a modelled analogue
// network that contributes about one sample of minimum-phase group delay --
// measured 51 against a reported 50. That delay is frequency dependent and
// should not be compensated, exactly like the recovery low-pass in the drive
// path. The tolerance is deliberately tight: drop the compensating delay line
// entirely and this lands ~50 samples out, nowhere near passing.
TEST_CASE ("clean path latency matches the reported latency", "[latency][blend]")
{
    Fixture f;
    f.host.setParam (ParamID::blend, 0.0f);   // clean only
    f.prepare();

    const auto out = f.run (impulse (numSamples));
    const auto drift = peakIndex (out) - f.pedal.getLatencySamples();

    INFO ("clean path peak at " << peakIndex (out)
          << ", reported latency " << f.pedal.getLatencySamples());
    REQUIRE (std::abs (drift) <= 2);
}

// The invariant that actually protects the blend: whatever the tone stack does
// downstream, it does to both paths equally, so the two must still arrive
// together.
TEST_CASE ("clean and drive paths arrive together", "[latency][blend]")
{
    const auto peakFor = [] (float blend)
    {
        Fixture f;
        f.host.setParam (ParamID::blend, blend);
        f.host.setParam (ParamID::grunt, 0.0f);
        f.prepare();
        return peakIndex (f.run (impulse (numSamples)));
    };

    const auto cleanPeak = peakFor (0.0f);
    const auto drivePeak = peakFor (1.0f);

    INFO ("clean peak " << cleanPeak << ", drive peak " << drivePeak);
    REQUIRE (std::abs (cleanPeak - drivePeak) <= 3);
}

// The drive path is deliberately NOT sample-exact. Its grunt high-pass, the
// inter-stage coupling filter and the recovery low-pass are all minimum phase,
// so they add a little frequency-dependent group delay that no integer delay
// can compensate and that nobody would want compensated. What must hold is that
// the oversampling and ADAA latency IS compensated -- forget either and this
// lands tens of samples out, far outside the tolerance.
TEST_CASE ("drive path stays aligned with the reported latency", "[latency][blend]")
{
    Fixture f;
    f.host.setParam (ParamID::blend, 1.0f);   // drive only
    f.host.setParam (ParamID::grunt, 0.0f);
    f.prepare();

    const auto out = f.run (impulse (numSamples));
    const auto drift = peakIndex (out) - f.pedal.getLatencySamples();

    INFO ("drive path peak at " << peakIndex (out)
          << ", reported latency " << f.pedal.getLatencySamples());
    REQUIRE (std::abs (drift) <= 4);
}

// The central invariant of the whole pedal, phrased so it measures phase and
// not level.
//
// Measure each path on its own, then blended. If the two are in phase the 50/50
// blend equals the average of the two levels; the more they disagree in phase,
// the further below that it falls. Comparing against the coherent sum rather
// than against the clean path alone makes the test independent of how loud the
// drive path happens to be, which is a voicing decision that will keep changing.
//
// Two separate failures were caught by this formulation and would have been
// missed by an absolute-level check:
//   * no oversampling compensation at all -> combs at fs/(2*latency) ~= 490 Hz;
//   * stacked high-passes in the drive path -> 0.23 coherence at 31 Hz, i.e.
//     the blend was eating the fundamental of the low B.
TEST_CASE ("drive and clean paths stay phase coherent", "[blend][phase]")
{
    int latency = 0;
    {
        Fixture f;
        f.prepare();
        latency = f.pedal.getLatencySamples();
    }
    REQUIRE (latency > 0);

    const auto firstComb = (float) (sampleRate / (2.0 * latency));

    // Open B and E strings, the octave above, then the comb nulls an
    // uncompensated blend would produce -- derived from the reported latency so
    // they stay correct if the oversampling ever changes.
    for (const auto frequency : { 30.9f, 41.2f, 82.4f, 164.8f,
                                  firstComb, firstComb * 3.0f, firstComb * 5.0f })
    {
        const auto cleanOnly = steadyRms (0.0f, frequency);
        const auto driveOnly = steadyRms (1.0f, frequency);
        const auto blended   = steadyRms (0.5f, frequency);
        const auto coherent  = 0.5f * (cleanOnly + driveOnly);

        INFO ("frequency " << frequency << " Hz: clean " << cleanOnly
              << ", drive " << driveOnly << ", blended " << blended
              << ", coherent sum " << coherent
              << ", ratio " << (blended / coherent));

        REQUIRE (cleanOnly > 0.0f);
        REQUIRE (driveOnly > 0.0f);
        REQUIRE (blended >= coherent * 0.9f);
    }
}

// Stage A is biased to make it asymmetric, so the drive path rests at a nonzero
// operating point and settleDcPath() charges it there before the first block.
// Get that wrong and the pedal thumps on startup: measured -21 dBFS decaying
// over ~150 ms when the bias stepped into an uncharged 5 Hz high-pass.
//
// The bound is 1e-6 rather than exact zero, and that is not a fudge. A biased
// stage followed by high-pass DC removal settles to within float precision, not
// to a bit-exact zero; the measured floor is 1.3e-7, roughly one LSB at 24-bit.
// Anything that reintroduces the startup transient is four orders of magnitude
// above this.
TEST_CASE ("silence in, silence out", "[stability]")
{
    Fixture f;
    f.prepare();

    juce::AudioBuffer<float> b (1, numSamples);
    b.clear();

    const auto out = f.run (std::move (b));

    float peak = 0.0f;

    for (int i = 0; i < out.getNumSamples(); ++i)
        peak = juce::jmax (peak, std::abs (out.getSample (0, i)));

    INFO ("silence peak = " << peak);
    REQUIRE (peak < 1.0e-6f);
}

TEST_CASE ("output stays finite at extreme settings", "[stability]")
{
    Fixture f;
    f.host.setParam (ParamID::drive, 1.0f);
    f.host.setParam (ParamID::trim, 12.0f);
    f.host.setParam (ParamID::master, 6.0f);
    f.prepare (2);

    const auto out = f.run (sine (41.2f, numSamples, 2));

    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
            REQUIRE (std::isfinite (out.getSample (ch, i)));
}
