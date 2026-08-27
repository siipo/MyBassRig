#include "TestSignals.h"
#include "dsp/NoiseGatePedal/NoiseGatePedal.h"
#include "dsp/OctaverPedal/OctaverPedal.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_dsp/juce_dsp.h>

using namespace TestSignals;

namespace
{
    constexpr int fftOrder = 15, fftSize = 1 << fftOrder;

    template <typename PedalType>
    struct Fix
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

    // Energy in a narrow band around a frequency, as a share of the total. This
    // is how "is there actually an octave down in there" becomes a number.
    double bandShare (const juce::AudioBuffer<float>& buffer, int startSample, double centreHz)
    {
        std::vector<float> bins ((size_t) fftSize * 2, 0.0f);
        std::copy (buffer.getReadPointer (0) + startSample,
                   buffer.getReadPointer (0) + startSample + fftSize, bins.begin());

        juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
                                                    juce::dsp::WindowingFunction<float>::hann);
        window.multiplyWithWindowingTable (bins.data(), (size_t) fftSize);
        juce::dsp::FFT (fftOrder).performFrequencyOnlyForwardTransform (bins.data());

        const auto binHz = sampleRate / (double) fftSize;
        double inBand = 0.0, total = 0.0;

        for (int k = 1; k < fftSize / 2; ++k)
        {
            const auto hz = (double) k * binHz;
            const auto energy = (double) bins[(size_t) k] * bins[(size_t) k];

            total += energy;

            if (std::abs (hz - centreHz) < centreHz * 0.08)
                inBand += energy;
        }

        return total > 0.0 ? inBand / total : 0.0;
    }

    float levelDb (const juce::AudioBuffer<float>& b, int start, int length)
    {
        return juce::Decibels::gainToDecibels (b.getRMSLevel (0, start, length));
    }
}

//==============================================================================
TEST_CASE ("gate is transparent when switched off", "[gate]")
{
    Fix<NoiseGatePedal> f;
    f.set (ParamID::gateOn, 0.0f);
    f.set (ParamID::gateThreshold, -10.0f);
    f.prepare();

    const auto input = sine (220.0f, 16384, 1, 0.5f);
    const auto output = f.run (sine (220.0f, 16384, 1, 0.5f));

    for (int i = 0; i < input.getNumSamples(); ++i)
        REQUIRE (output.getSample (0, i) == input.getSample (0, i));
}

TEST_CASE ("gate passes a note and shuts down noise", "[gate]")
{
    const auto levelFor = [] (float amplitude)
    {
        Fix<NoiseGatePedal> f;
        f.set (ParamID::gateOn, 1.0f);
        f.set (ParamID::gateThreshold, -40.0f);
        f.set (ParamID::gateRange, -60.0f);
        f.set (ParamID::gateHold, 0.0f);
        f.set (ParamID::gateRelease, 20.0f);
        f.prepare();

        const auto out = f.run (sine (220.0f, 32768, 1, amplitude));
        return levelDb (out, 16384, 16384) - juce::Decibels::gainToDecibels (amplitude / std::sqrt (2.0f));
    };

    const auto loud = levelFor (0.4f);     // well above the threshold
    const auto quiet = levelFor (0.002f);  // well below it

    INFO ("change against input: loud " << loud << " dB, quiet " << quiet << " dB");

    REQUIRE (std::abs (loud) < 1.0f);   // a real note passes untouched
    REQUIRE (quiet < -20.0f);           // noise is pushed a long way down
}

// A single threshold makes a gate chatter as a note decays across it: once
// open, it should stay open until the signal falls well below where it opened.
//
// Note the sidechain is switched off here on purpose. The threshold is measured
// on the FILTERED key, so with the filter in circuit the level that opens the
// gate depends on the note -- an 80 Hz high-pass puts a 110 Hz note at 0.81 of
// its actual level. That is true of any sidechain-filtered gate and is the
// point of the filter, but it makes a test about thresholds ambiguous.
TEST_CASE ("hysteresis keeps the gate open once it has opened", "[gate]")
{
    const auto stillOpenAt = [] (float secondHalfDb)
    {
        Fix<NoiseGatePedal> f;
        f.set (ParamID::gateOn, 1.0f);
        f.set (ParamID::gateThreshold, -30.0f);
        f.set (ParamID::gateSidechain, 0.0f);   // Off
        f.set (ParamID::gateHold, 0.0f);        // isolate hysteresis from hold
        f.set (ParamID::gateRelease, 30.0f);
        f.prepare();

        // Loud enough to open, then dropped to the level under test.
        auto buffer = sine (110.0f, 65536, 1, juce::Decibels::decibelsToGain (-12.0f));
        const auto quieter = juce::Decibels::decibelsToGain (secondHalfDb)
                           / juce::Decibels::decibelsToGain (-12.0f);

        for (int i = buffer.getNumSamples() / 2; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, buffer.getSample (0, i) * quieter);

        f.run (std::move (buffer));
        return f.pedal.getOpenAmount();
    };

    // Below the opening threshold but inside the 6 dB hysteresis gap: a
    // single-threshold gate would close here.
    const auto insideGap = stillOpenAt (-33.0f);

    // Well below the closing threshold: it should genuinely close.
    const auto wellBelow = stillOpenAt (-50.0f);

    INFO ("open amount: -33 dB " << insideGap << ", -50 dB " << wellBelow);

    REQUIRE (insideGap > 0.9f);
    REQUIRE (wellBelow < 0.1f);
}

// Range rather than a hard mute: a gate that slams to silence is more
// noticeable than the noise it removes.
TEST_CASE ("range sets how far down the gate closes", "[gate]")
{
    const auto closedLevelFor = [] (float rangeDb)
    {
        Fix<NoiseGatePedal> f;
        f.set (ParamID::gateOn, 1.0f);
        f.set (ParamID::gateThreshold, -30.0f);
        f.set (ParamID::gateRange, rangeDb);
        f.set (ParamID::gateHold, 0.0f);
        f.set (ParamID::gateRelease, 10.0f);
        f.prepare();

        const auto out = f.run (sine (220.0f, 32768, 1, 0.001f));
        return levelDb (out, 16384, 16384);
    };

    const auto shallow = closedLevelFor (-12.0f);
    const auto deep = closedLevelFor (-70.0f);

    INFO ("closed level: range -12 dB " << shallow << ", range -70 dB " << deep);
    REQUIRE (deep < shallow - 15.0f);
}

TEST_CASE ("gate adds no latency", "[gate][latency]")
{
    Fix<NoiseGatePedal> f;
    f.prepare();
    REQUIRE (f.pedal.getLatencySamples() == 0);
}

//==============================================================================
TEST_CASE ("octaver is transparent when switched off", "[octaver]")
{
    Fix<OctaverPedal> f;
    f.set (ParamID::octOn, 0.0f);
    f.set (ParamID::octSubOne, 1.0f);
    f.prepare();

    const auto input = sine (110.0f, 16384, 1, 0.5f);
    const auto output = f.run (sine (110.0f, 16384, 1, 0.5f));

    for (int i = 0; i < input.getNumSamples(); ++i)
        REQUIRE (output.getSample (0, i) == input.getSample (0, i));
}

// The claim the whole pedal rests on: the flip-flop produces a note an octave
// below the one played. This measures it rather than assuming it.
TEST_CASE ("the divider produces an octave below the note", "[octaver]")
{
    for (const auto note : { 55.0f, 110.0f, 220.0f })
    {
        Fix<OctaverPedal> f;
        f.set (ParamID::octOn, 1.0f);
        f.set (ParamID::octDirect, 0.0f);   // octave only, so the measurement is unambiguous
        f.set (ParamID::octSubOne, 1.0f);
        f.set (ParamID::octSubTwo, 0.0f);
        f.set (ParamID::octTone, 4000.0f);
        f.prepare();

        const auto out = f.run (sine (note, 8192 + fftSize, 1, 0.5f));

        const auto atOctaveDown = bandShare (out, 8192, note * 0.5);
        const auto atOriginal = bandShare (out, 8192, note);

        INFO ("note " << note << " Hz: energy at " << (note * 0.5) << " Hz is "
              << atOctaveDown << ", at " << note << " Hz is " << atOriginal);

        REQUIRE (atOctaveDown > 0.2);
        REQUIRE (atOctaveDown > atOriginal);
    }
}

TEST_CASE ("the second voice is two octaves down", "[octaver]")
{
    constexpr float note = 220.0f;

    Fix<OctaverPedal> f;
    f.set (ParamID::octOn, 1.0f);
    f.set (ParamID::octDirect, 0.0f);
    f.set (ParamID::octSubOne, 0.0f);
    f.set (ParamID::octSubTwo, 1.0f);
    f.set (ParamID::octTone, 4000.0f);
    f.prepare();

    const auto out = f.run (sine (note, 8192 + fftSize, 1, 0.5f));

    const auto atTwoDown = bandShare (out, 8192, note * 0.25);
    const auto atOneDown = bandShare (out, 8192, note * 0.5);

    INFO ("energy at " << (note * 0.25) << " Hz is " << atTwoDown
          << ", at " << (note * 0.5) << " Hz is " << atOneDown);

    REQUIRE (atTwoDown > 0.15);
    REQUIRE (atTwoDown > atOneDown);
}

// Without a squelch the comparator triggers on noise between notes and the
// flip-flops free-run, producing a warbling octave out of silence.
TEST_CASE ("the octaver is silent when nothing is played", "[octaver]")
{
    Fix<OctaverPedal> f;
    f.set (ParamID::octOn, 1.0f);
    f.set (ParamID::octDirect, 0.0f);
    f.set (ParamID::octSubOne, 1.0f);
    f.prepare();

    juce::AudioBuffer<float> silence (1, 16384);
    silence.clear();

    const auto out = f.run (std::move (silence));

    float peak = 0.0f;

    for (int i = 0; i < out.getNumSamples(); ++i)
        peak = juce::jmax (peak, std::abs (out.getSample (0, i)));

    INFO ("peak from silence: " << peak);
    REQUIRE (peak < 1.0e-5f);
    REQUIRE_FALSE (f.pedal.isTracking());
}

// The octave has to follow the playing rather than sit there like a synth
// bolted on top, which means the squares are shaped by the input envelope.
TEST_CASE ("the octave follows playing dynamics", "[octaver]")
{
    const auto levelFor = [] (float amplitude)
    {
        Fix<OctaverPedal> f;
        f.set (ParamID::octOn, 1.0f);
        f.set (ParamID::octDirect, 0.0f);
        f.set (ParamID::octSubOne, 1.0f);
        f.prepare();

        const auto out = f.run (sine (110.0f, 32768, 1, amplitude));
        return out.getRMSLevel (0, 16384, 16384);
    };

    const auto hard = levelFor (0.6f);
    const auto soft = levelFor (0.06f);

    INFO ("octave level: hard " << hard << ", soft " << soft);
    REQUIRE (hard > soft * 3.0f);
}

// The reported problem, and the measurement behind the Growl control.
//
// The divider is not at fault: sub level is flat across the neck. But an octave
// below the low strings lands where speakers do not go. The octave of an A on
// the E string is 27.5 Hz, and 80% of its energy sits below 40 Hz.
//
// Growl lifts the harmonics, which carry the same pitch and are comfortably
// audible. It must ADD rather than trade -- the first version crossfaded and
// cost 6.6 dB on notes whose octave was already audible.
TEST_CASE ("growl makes the low octaves audible", "[octaver][growl]")
{
    const auto audibleEnergy = [] (float note, float growl)
    {
        Fix<OctaverPedal> f;
        f.set (ParamID::octOn, 1.0f);
        f.set (ParamID::octDirect, 0.0f);
        f.set (ParamID::octSubOne, 1.0f);
        f.set (ParamID::octGrowl, growl);
        f.prepare();

        const auto out = f.run (sine (note, 8192 + fftSize, 1, 0.5f));

        std::vector<float> bins ((size_t) fftSize * 2, 0.0f);
        std::copy (out.getReadPointer (0) + 8192, out.getReadPointer (0) + 8192 + fftSize,
                   bins.begin());
        juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
                                                    juce::dsp::WindowingFunction<float>::hann);
        window.multiplyWithWindowingTable (bins.data(), (size_t) fftSize);
        juce::dsp::FFT (fftOrder).performFrequencyOnlyForwardTransform (bins.data());

        const auto binHz = sampleRate / (double) fftSize;
        double audible = 0.0;

        // 40 Hz is roughly where a bass cabinet or a monitor gives up.
        for (int k = 1; k < fftSize / 2; ++k)
            if ((double) k * binHz >= 40.0)
                audible += (double) bins[(size_t) k] * bins[(size_t) k];

        return audible;
    };

    for (const auto note : { 41.2f, 55.0f, 82.4f, 110.0f })
    {
        const auto none = audibleEnergy (note, 0.0f);
        const auto full = audibleEnergy (note, 1.0f);
        const auto gainDb = 10.0 * std::log10 (full / juce::jmax (none, 1.0e-30));

        INFO ("note " << note << " Hz (octave " << (note * 0.5f) << " Hz): growl adds "
              << gainDb << " dB of audible energy");

        // Never negative anywhere: the control can only help.
        REQUIRE (gainDb > 0.0);

        // And it must do real work on the strings that needed it.
        if (note <= 55.0f)
            REQUIRE (gainDb > 6.0);
    }
}

// Reported from playing: the octave jumps UP as a note decays, and worse the
// lower the note. Reproduced at 41.2 Hz, where the sub went 20.5 -> 20.5 ->
// 41.7 -> 164.8 Hz across a decaying note.
//
// The cause was the squelch. It was a single absolute threshold, and the
// envelope follower rippled 67% per cycle at 41 Hz against 8% at 220 Hz -- so
// near the threshold it flickered every cycle, and each flicker reset the
// flip-flops mid-note and scrambled the division. Hence "worse on low notes".
//
// Three things fixed it: a hold on the envelope longer than one cycle of the
// lowest note, hysteresis on the squelch, and only resetting the flip-flops
// after the input has been quiet for a while rather than instantly.
TEST_CASE ("the octave holds through a decaying note", "[octaver][tracking]")
{
    constexpr int order = 15, size = 1 << order;

    // Harmonics decaying faster than the fundamental, as on a real string. A
    // steady tone cannot show this fault at all.
    const auto pluck = [] (float f0, int numSamples, float amplitude)
    {
        juce::AudioBuffer<float> b (1, numSamples);
        b.clear();
        auto* d = b.getWritePointer (0);
        const float level[] { 1.0f, 0.62f, 0.44f, 0.30f, 0.22f, 0.16f, 0.11f, 0.08f };

        for (int h = 1; h <= 8; ++h)
            for (int i = 0; i < numSamples; ++i)
            {
                const auto t = (float) i / (float) sampleRate;
                // Decays fast enough to reach the squelch region inside the
                // measured windows. The first version of this test used a slow
                // decay and a short note, so it never got quiet enough to
                // trigger the fault -- it passed against the broken code.
                d[i] += amplitude * level[h - 1] * std::exp (-t / (1.5f / (float) h))
                      * std::sin (juce::MathConstants<float>::twoPi * (float) h * f0 * t);
            }

        return b;
    };

    // The loudest thing in the sub: what the octave actually is, measured
    // without relying on FFT resolution at 20 Hz being finer than it is.
    const auto dominantHz = [] (const juce::AudioBuffer<float>& buffer, int start)
    {
        std::vector<float> bins ((size_t) size * 2, 0.0f);
        std::copy (buffer.getReadPointer (0) + start,
                   buffer.getReadPointer (0) + start + size, bins.begin());

        juce::dsp::WindowingFunction<float> window ((size_t) size,
                                                    juce::dsp::WindowingFunction<float>::hann);
        window.multiplyWithWindowingTable (bins.data(), (size_t) size);
        juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (bins.data());

        const auto binHz = sampleRate / (double) size;
        double best = 0.0, bestHz = 0.0;

        for (int k = 2; k < size / 2; ++k)
        {
            const auto hz = (double) k * binHz;

            if (hz > 500.0)
                break;

            if ((double) bins[(size_t) k] > best)
            {
                best = (double) bins[(size_t) k];
                bestHz = hz;
            }
        }

        return bestHz;
    };

    for (const auto note : { 41.2f, 55.0f, 82.4f })
    {
        // Loud, and quiet enough that the old squelch flickered.
        for (const auto amplitude : { 0.6f, 0.012f })
        {
            Fix<OctaverPedal> f;
            f.set (ParamID::octOn, 1.0f);
            f.set (ParamID::octDirect, 0.0f);
            f.set (ParamID::octSubOne, 1.0f);
            f.set (ParamID::octGrowl, 0.0f);
            f.prepare();

            const auto out = f.run (pluck (note, size * 4, amplitude));

            for (int window = 0; window + size <= out.getNumSamples(); window += size)
            {
                // Only windows where the octave is actually producing something.
                // Once the note has decayed past the squelch it is meant to be
                // silent, and asking what frequency silence is would be asking
                // the wrong question.
                if (out.getRMSLevel (0, window, size) < 1.0e-4f)
                    continue;

                const auto measured = dominantHz (out, window);
                const auto wanted = (double) note * 0.5;

                INFO ("note " << note << " Hz at amplitude " << amplitude
                      << ", window " << (window / size) << ": sub is " << measured
                      << " Hz, wanted " << wanted);

                REQUIRE (std::abs (measured - wanted) < wanted * 0.15);
            }
        }
    }
}

TEST_CASE ("octaver adds no latency", "[octaver][latency]")
{
    Fix<OctaverPedal> f;
    f.prepare();
    REQUIRE (f.pedal.getLatencySamples() == 0);
}

TEST_CASE ("octaver stays finite on a hot low note", "[octaver][stability]")
{
    Fix<OctaverPedal> f;
    f.set (ParamID::octOn, 1.0f);
    f.set (ParamID::octSubOne, 1.0f);
    f.set (ParamID::octSubTwo, 1.0f);
    f.set (ParamID::octDirect, 1.0f);
    f.prepare (2);

    const auto out = f.run (sine (41.2f, 32768, 2, 0.95f));

    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
            REQUIRE (std::isfinite (out.getSample (ch, i)));
}

// How much of the octaver's output is not a harmonic of the octave it is
// generating.
//
// The divider makes a hard square by flipping on zero crossings, and it can
// only flip on a sample boundary, so the square's period wobbles by up to one
// sample and its edges are not band limited. This is the one place in the
// plugin where a discontinuity is synthesised, and the ADAA discipline applied
// to the drive does not reach it.
//
// f0 is chosen so f0/2 lands exactly on FFT bin m, which puts every legitimate
// partial exactly on a multiple of m. Aliased images land at (N - k*m), and
// 32768 is not a multiple of any m used here, so they can never land back on
// the grid. Off-grid energy is therefore a clean measure of what should not be
// there.
//
// Measured with the tone control wide open, which is the worst case.
//
//     f0        octave    flip-flop   oscillator   margin to -45 dB
//     111 Hz     56 Hz     -38.9 dB    -59.7 dB      14.7
//     220 Hz    110 Hz     -35.6 dB    -55.3 dB      10.3
//     439 Hz    220 Hz     -32.5 dB    -49.3 dB       4.3
//
// The left column is the old comparator-and-flip-flop divider, whose square was
// switched on whichever sample boundary was nearest; the right is the phase
// locked oscillator that replaced it. Between 17 and 21 dB cleaner, and it is
// cleaner by construction rather than by tuning -- every partial is generated
// at a frequency we chose, so there is nothing to fold.
//
// The threshold is -45 dB, which is where the measurements are and not where
// they ought to be. It was -28 dB against the old design; leaving it there
// would have let the octaver get 17 dB dirtier without a word.
//
// What it catches, and what it does not. Crippling the tracking filter moves
// the number from -35.6 dB to +18.5 dB, so a divider that stops dividing is
// caught with 54 dB to spare. Removing the comparator hysteresis does NOT trip
// it, and that is expected rather than a hole: hysteresis exists for the ripple
// on a DECAYING note, and this test feeds a steady sine at constant amplitude.
// That regression is the job of "the octave does not jump up as a note decays".
TEST_CASE ("octaver off-grid energy does not regress", "[octaver][aliasing]")
{
    const auto binHz = sampleRate / (double) fftSize;

    for (int m : { 38, 75, 150 })
    {
        const auto subHz = (double) m * binHz;
        const auto f0    = (float) (2.0 * subHz);

        Fix<OctaverPedal> f;
        f.set (ParamID::octOn, 1.0f);
        f.set (ParamID::octDirect, 0.0f);      // the generated octave alone
        f.set (ParamID::octSubOne, 1.0f);
        f.set (ParamID::octSubTwo, 0.0f);
        f.set (ParamID::octGrowl, 0.0f);
        f.set (ParamID::octTone, 4000.0f);     // wide open, so nothing is hidden

        // Tracking has to suit the note. Left at a default that does not, the
        // comparator never sees a clean fundamental and this measures a
        // tracking failure instead -- which reads as +33 dB and looks
        // catastrophic for entirely the wrong reason.
        f.set (ParamID::octTrack, juce::jlimit (90.0f, 600.0f, f0 * 1.3f));
        f.prepare();

        constexpr int warmup = 16384;
        const auto out = f.run (sine (f0, warmup + fftSize, 1, 0.6f));

        REQUIRE (f.pedal.isTracking());

        std::vector<float> bins ((size_t) fftSize * 2, 0.0f);
        std::copy (out.getReadPointer (0) + warmup,
                   out.getReadPointer (0) + warmup + fftSize, bins.begin());

        juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
            juce::dsp::WindowingFunction<float>::hann);
        window.multiplyWithWindowingTable (bins.data(), (size_t) fftSize);
        juce::dsp::FFT (fftOrder).performFrequencyOnlyForwardTransform (bins.data());

        double onGrid = 0.0, offGrid = 0.0;

        for (int k = 4; k < fftSize / 2; ++k)
        {
            const auto energy  = (double) bins[(size_t) k] * bins[(size_t) k];
            const auto nearest = ((k + m / 2) / m) * m;

            if (nearest > 0 && std::abs (k - nearest) <= 3)
                onGrid += energy;
            else
                offGrid += energy;
        }

        const auto db = 10.0 * std::log10 (juce::jmax (offGrid / juce::jmax (onGrid, 1e-30), 1e-30));

        INFO ("f0 " << f0 << " Hz, octave " << subHz << " Hz, off-grid " << db << " dB");
        REQUIRE (db < -45.0);

        // The octave has to actually be there. Without this the test passes
        // beautifully on silence.
        REQUIRE (onGrid > 1e-6);
    }
}

// Reported from playing: "if I set growl too high it tends to garble sound".
//
// It was not the divider and it was not aliasing -- the off-grid ratio barely
// moves with Growl. It was level. Growl adds pitch * growl * boost with nothing
// holding the total down, which measured as up to four and a half times full
// scale:
//
//     f0        growl 0   growl 1
//     111 Hz     0.95      2.52
//     220 Hz     1.18      4.52
//     439 Hz     0.93      3.69
//
// Arriving at the drive's clippers thirteen decibels hot is an instruction to
// destroy the signal, and that is what was being heard.
//
// This test is the report, written down. It says Growl may colour the octave
// and may not run away with the level.
TEST_CASE ("growl does not run away with the level", "[octaver][growl]")
{
    const auto measure = [] (float note, float growl, float& peak)
    {
        Fix<OctaverPedal> f;
        f.set (ParamID::octOn, 1.0f);
        f.set (ParamID::octDirect, 0.0f);     // the generated octave alone
        f.set (ParamID::octSubOne, 1.0f);     // and at full, which is the worst case
        f.set (ParamID::octGrowl, growl);
        f.set (ParamID::octTrack, juce::jlimit (90.0f, 600.0f, note * 1.3f));
        f.prepare();

        constexpr int warmup = 16384;
        const auto out = f.run (sine (note, warmup + fftSize, 1, 0.6f));

        double sumSq = 0.0;
        peak = 0.0f;

        for (int n = warmup; n < warmup + fftSize; ++n)
        {
            const auto v = out.getSample (0, n);
            peak = juce::jmax (peak, std::abs (v));
            sumSq += (double) v * v;
        }

        return std::sqrt (sumSq / (double) fftSize);
    };

    for (const auto note : { 111.0f, 220.0f, 439.0f })
    {
        float quietPeak = 0.0f, loudPeak = 0.0f;

        const auto quiet = measure (note, 0.0f, quietPeak);
        const auto loud  = measure (note, 1.0f, loudPeak);

        REQUIRE (quiet > 1.0e-3);   // there is an octave here to begin with

        const auto riseDb = 20.0 * std::log10 (loud / quiet);

        INFO ("note " << note << " Hz: growl 0 -> 1 raises RMS by " << riseDb
              << " dB, peak " << quietPeak << " -> " << loudPeak);

        // The ceiling is +3 dB. Half a decibel of slack for the follower, which
        // is deliberately slow and does not settle to the exact ratio.
        REQUIRE (riseDb < 3.5);

        // And the thing that actually caused the garbling: what arrives at the
        // next pedal. Four and a half times full scale was the fault.
        REQUIRE (loudPeak < 2.5f);
    }
}
// The whole reason the octaver was redesigned.
//
// A comparator driving a flip-flop has MEMORY: one spurious crossing does not
// produce a blip, it inverts the division from that point on, so the pedal
// carries on at the wrong octave until something resets it. That is what "it
// jumps an octave" meant, and the three guards in DESIGN.md 3o all worked on
// preventing the spurious crossing rather than on surviving one.
//
// The period tracker cannot do that. A reading which disagrees with the running
// estimate is only believed once the NEXT reading agrees with it, so a lone
// glitch moves nothing, while a real note change is still accepted inside two
// cycles.
//
// Measured by counting zero crossings of the sub across a window containing the
// glitch. Growl is at zero on purpose: the sub is then a pure sine, so the
// count is exactly frequency times duration and nothing else. (Section 3o
// records zero-crossing counting giving a wrong answer once before, when it was
// reading the ring of a crossover filter on a square edge. There is no square
// and no crossover here.)
//
// WHAT THIS GUARDS, AND WHAT IT DOES NOT.
//
// It guards the architecture. Put a flip-flop back and this fails loudly: the
// division inverts and stays inverted, which is a whole octave of error against
// a tolerance of one crossing.
//
// It does NOT guard the two-agreement rule. Setting agreementsForNewNote to 1
// still passes, and that was worth finding out rather than assuming: with a
// single agreement the wrong period survives about one input cycle before the
// next reading corrects it, which is below what this window can see. The
// robustness does not rest on that constant alone -- the period smoothing and
// the phase lock both blunt a lone bad reading too -- so no single-constant
// sabotage of the tracker moves this number.
//
// Four versions of this test were needed to establish even that much. The first
// injected a glitch alternating every sample, 24 kHz, which the 250 Hz tracking
// filter removed before the detector saw it. The second measured a window
// starting long after the glitch, by which time everything had recovered. The
// third dropped the bump at a fixed sample, where it landed in the note's
// negative half and deepened an excursion that already existed instead of
// creating one. Only the fourth actually produces the extra crossing it claims
// to -- and it still does not discriminate against the constant, which is
// recorded here rather than glossed.
TEST_CASE ("a spurious crossing does not move the octave", "[octaver][tracking]")
{
    constexpr float note = 110.0f;      // an octave down is 55 Hz
    constexpr int settle = 8192;
    constexpr int windowLength = 8192;

    const auto crossingsAfterGlitch = [] (bool injectGlitch)
    {
        Fix<OctaverPedal> f;
        f.set (ParamID::octOn, 1.0f);
        f.set (ParamID::octDirect, 0.0f);    // the generated octave alone
        f.set (ParamID::octSubOne, 1.0f);
        f.set (ParamID::octSubTwo, 0.0f);
        f.set (ParamID::octGrowl, 0.0f);     // a pure sine, so counting is exact
        f.prepare();

        auto buffer = sine (note, settle + windowLength + 4096, 1, 0.6f);

        if (injectGlitch)
        {
            // WHERE matters as much as what. Dropped at a fixed sample the bump
            // landed in the note's negative half, where it only deepened an
            // excursion that was already there and produced no extra crossing
            // whatsoever -- which is why two earlier versions of this test could
            // not tell a working tracker from a broken one.
            //
            // Placed at a positive peak, a downward bump has to drag the signal
            // through zero and back, which is exactly the extra crossing a
            // comparator would have counted.
            int glitchAt = settle;

            while (glitchAt < settle + 2048 && buffer.getSample (0, glitchAt) < 0.55f)
                ++glitchAt;

            const auto glitchSamples = juce::roundToInt (sampleRate / 160.0 * 0.5);

            for (int n = 0; n < glitchSamples; ++n)
            {
                const auto at = glitchAt + n;
                const auto bump = 1.6f * std::sin (juce::MathConstants<float>::pi
                                                    * (float) n / (float) glitchSamples);
                buffer.setSample (0, at, buffer.getSample (0, at) - bump);
            }
        }

        const auto out = f.run (std::move (buffer));

        // Counted from the glitch onwards, so a wrong period lasting even a
        // couple of cycles shows up.
        int crossings = 0;

        for (int n = settle + 1; n < settle + windowLength; ++n)
        {
            const auto a = out.getSample (0, n - 1);
            const auto b = out.getSample (0, n);

            if ((a <= 0.0f && b > 0.0f) || (a >= 0.0f && b < 0.0f))
                ++crossings;
        }

        return crossings;
    };

    const auto clean = crossingsAfterGlitch (false);
    const auto glitched = crossingsAfterGlitch (true);

    // A 55 Hz sine crosses zero twice a cycle.
    const auto expected = juce::roundToInt (note * 0.5 * 2.0 * windowLength / sampleRate);

    INFO ("expected " << expected << " crossings, clean " << clean
          << ", after a glitch " << glitched);

    REQUIRE (clean == Catch::Approx ((double) expected).margin (2.0));

    // The glitch is allowed to cost a crossing or two while the detector sees
    // it. It is not allowed to change what the octave IS.
    REQUIRE (glitched == Catch::Approx ((double) clean).margin (2.0));
}

// Reported from playing a real note in a DAW: "there is audible crackle when
// the note fades".
//
// The generated octave is a sine. Its amplitude was gated by a BOOLEAN --
// `shape = hasPitch ? envelope : 0` -- and as a note decays the crossings the
// tracker feeds on get sporadic, so it drops the pitch and re-acquires it,
// repeatedly. Every one of those toggles is a step discontinuity in an
// otherwise smooth waveform, which is exactly what a click is. Enough of them
// in a row is a crackle.
//
// The old flip-flop design had the same boolean and did not crackle audibly,
// because a hard square is already nothing but discontinuities and one more did
// not stand out. Replacing it with a clean waveform is what made this audible:
// a fault that had been there all along, uncovered rather than introduced.
//
// Measured as slew RELATIVE TO LOCAL AMPLITUDE, which is the whole trick. A
// click in a fade is small in absolute terms -- the signal is quiet by then --
// so an absolute slew limit does not see it. The first version of this test
// used one and passed happily against the reported fault, measuring a worst
// step of 0.0166 against a permitted 0.0154, and finding it 0.11 s into the
// note rather than anywhere near the fade.
//
// Against the local level it is obvious: a smooth sine steps by 2*pi*f/fs of
// its own amplitude per sample, about 0.008 at these frequencies, while a gate
// slamming shut steps by the entire amplitude at once. Two orders of magnitude
// apart, at any level.
TEST_CASE ("the octave does not click as a note decays", "[octaver][tracking]")
{
    constexpr float note = 82.4f;       // low E; an octave down is 41.2 Hz

    // Long enough, and decaying far enough, to pass right THROUGH the region
    // where the tracker starts and stops believing what it sees. An earlier
    // version stopped at 0.024, comfortably above the squelch at 0.0018, so the
    // pitch never dropped, the gate never fired, and the test could not tell a
    // boolean gate from a ramped one -- it reported the same figure to six
    // digits either way.
    constexpr int numSamples = 1 << 18; // about 5.5 s

    Fix<OctaverPedal> f;
    f.set (ParamID::octOn, 1.0f);
    f.set (ParamID::octDirect, 0.0f);   // the generated octave alone
    f.set (ParamID::octSubOne, 1.0f);
    f.set (ParamID::octSubTwo, 0.0f);
    f.set (ParamID::octGrowl, 0.35f);
    f.prepare();

    // A plucked note whose harmonics decay faster than its fundamental, as a
    // string does, so it passes THROUGH the region where tracking gets marginal
    // instead of stopping dead.
    juce::AudioBuffer<float> pluck (1, numSamples);
    pluck.clear();

    {
        auto* d = pluck.getWritePointer (0);
        const float level[] { 1.0f, 0.55f, 0.35f, 0.22f };

        for (int h = 1; h <= 4; ++h)
            for (int i = 0; i < numSamples; ++i)
            {
                const auto t = (float) i / (float) sampleRate;
                d[i] += 0.7f * level[h - 1] * std::exp (-t / (0.6f / (float) h))
                            * std::sin (juce::MathConstants<float>::twoPi * note * (float) h * t);
            }

        // A noise floor, because a real instrument has one and it is what makes
        // crossings unreliable rather than merely quiet as a note dies away.
        juce::Random random (4242);

        for (int i = 0; i < numSamples; ++i)
            d[i] += 2.0e-4f * (random.nextFloat() * 2.0f - 1.0f);
    }

    const auto out = f.run (std::move (pluck));

    // The local reference is a CENTRED window, not a follower. A one-sided
    // follower reads a note's onset as a huge relative step -- the signal really
    // is doubling every few samples while it grows out of silence -- and that
    // swamped everything: the first version of this measurement reported 0.43 at
    // sample 684, which is the first cycle of the note, and returned exactly the
    // same figure after two genuine fixes. It was measuring the attack.
    constexpr int blockSize = 64;
    const auto numBlocks = out.getNumSamples() / blockSize;

    std::vector<float> blockPeak ((size_t) numBlocks, 0.0f);

    for (int b = 0; b < numBlocks; ++b)
        for (int n = 0; n < blockSize; ++n)
            blockPeak[(size_t) b] = juce::jmax (blockPeak[(size_t) b],
                                                std::abs (out.getSample (0, b * blockSize + n)));

    float worstRatio = 0.0f;
    int worstAt = 0;

    for (int i = 1; i < numBlocks * blockSize; ++i)
    {
        // Loudest thing within about 5 ms either side.
        const auto centre = i / blockSize;
        float local = 0.0f;

        for (int b = juce::jmax (0, centre - 4); b <= juce::jmin (numBlocks - 1, centre + 4); ++b)
            local = juce::jmax (local, blockPeak[(size_t) b]);

        // Below this the octave is inaudible and the ratio is noise over noise.
        if (local < 1.0e-4f)
            continue;

        const auto slew = std::abs (out.getSample (0, i) - out.getSample (0, i - 1));
        const auto ratio = slew / local;

        if (ratio > worstRatio)
        {
            worstRatio = ratio;
            worstAt = i;
        }
    }

    // What a smooth waveform can manage: the octave's third harmonic under
    // Growl is the fastest thing present.
    const auto highestPartialHz = note * 0.5f * 3.0f;
    const auto smoothRatio = juce::MathConstants<float>::twoPi * highestPartialHz
                                / (float) sampleRate;

    INFO ("smooth step is " << smoothRatio << " of local amplitude; worst was "
          << worstRatio << " at sample " << worstAt
          << " (" << ((double) worstAt / sampleRate) << " s)");

    // Twice the smooth maximum, which is where the measurements are:
    //
    //     ramped gate   0.0101      <- 3.2x of margin
    //     threshold     0.0324
    //     boolean gate  0.0369      <- the reported fault
    //
    // Not a round number chosen in advance. Ten times the smooth step was tried
    // first and let the fault straight through, and there is no point in a
    // threshold that a known bug satisfies.
    REQUIRE (worstRatio < 2.0f * smoothRatio);
}
