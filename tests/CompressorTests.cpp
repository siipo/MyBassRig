#include "TestSignals.h"
#include "dsp/CompressorPedal/CompressorPedal.h"
#include "dsp/PedalRegistry.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using namespace TestSignals;

namespace
{
    struct CompFixture
    {
        HarnessProcessor host;
        CompressorPedal pedal { host.apvts };

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

    // Two notes at once: a low fundamental and something an octave and a half
    // up. This is the signal that separates a bass compressor from a generic
    // one, because the low note dominates an unfiltered level detector.
    juce::AudioBuffer<float> lowPlusHigh (int numSamples, float lowAmplitude, float highAmplitude)
    {
        auto buffer = sine (41.2f, numSamples, 1, lowAmplitude);
        const auto high = sine (330.0f, numSamples, 1, highAmplitude);

        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (0, i, buffer.getSample (0, i) + high.getSample (0, i));

        return buffer;
    }

    float levelDb (const juce::AudioBuffer<float>& b, int startSample)
    {
        return juce::Decibels::gainToDecibels (b.getRMSLevel (0, startSample,
                                                              b.getNumSamples() - startSample));
    }
}

TEST_CASE ("compressor is transparent when switched off", "[comp]")
{
    CompFixture f;
    f.set (ParamID::compOn, 0.0f);
    f.set (ParamID::compThreshold, -40.0f);   // would squash hard if it ran
    f.set (ParamID::compRatio, 20.0f);
    f.prepare();

    const auto input = sine (220.0f, 16384, 1, 0.5f);
    const auto output = f.run (sine (220.0f, 16384, 1, 0.5f));

    for (int i = 0; i < input.getNumSamples(); ++i)
        REQUIRE (output.getSample (0, i) == input.getSample (0, i));

    REQUIRE (f.pedal.getGainReductionDb() == 0.0f);
}

TEST_CASE ("compressor reduces gain above the threshold", "[comp]")
{
    const auto levelFor = [] (float amplitude, float thresholdDb)
    {
        CompFixture f;
        f.set (ParamID::compOn, 1.0f);
        f.set (ParamID::compThreshold, thresholdDb);
        f.set (ParamID::compRatio, 8.0f);
        f.set (ParamID::compSidechain, 0.0f);   // Off, so the test isolates one thing
        f.prepare();
        return levelDb (f.run (sine (220.0f, 32768, 1, amplitude)), 16384);
    };

    // Well under the threshold: should pass essentially untouched.
    const auto quietIn = juce::Decibels::gainToDecibels (0.02f / std::sqrt (2.0f));
    const auto quietOut = levelFor (0.02f, -12.0f);
    INFO ("quiet: in " << quietIn << " dB, out " << quietOut << " dB");
    REQUIRE (std::abs (quietOut - quietIn) < 1.0f);

    // Well over it: should be pulled down.
    const auto loudIn = juce::Decibels::gainToDecibels (0.8f / std::sqrt (2.0f));
    const auto loudOut = levelFor (0.8f, -24.0f);
    INFO ("loud: in " << loudIn << " dB, out " << loudOut << " dB");
    REQUIRE (loudOut < loudIn - 6.0f);
}

// The reason this pedal has a sidechain filter at all. Feed the detector the
// whole signal and a low B pumps the entire band in sympathy with the bottom
// octave; filter the key input and the compressor responds to the note instead.
TEST_CASE ("the sidechain filter stops the low end driving the compressor", "[comp][sidechain]")
{
    const auto reductionFor = [] (float sidechainIndex)
    {
        CompFixture f;
        f.set (ParamID::compOn, 1.0f);
        f.set (ParamID::compThreshold, -26.0f);
        f.set (ParamID::compRatio, 8.0f);
        f.set (ParamID::compAttack, 5.0f);
        f.set (ParamID::compSidechain, sidechainIndex);
        f.prepare();

        // A dominant low fundamental under a much quieter upper note.
        f.run (lowPlusHigh (32768, 0.7f, 0.08f));
        return f.pedal.getGainReductionDb();
    };

    const auto unfiltered = reductionFor (0.0f);   // Off
    const auto at80 = reductionFor (1.0f);
    const auto at160 = reductionFor (2.0f);

    INFO ("gain reduction: off " << unfiltered << " dB, 80 Hz " << at80
          << " dB, 160 Hz " << at160 << " dB");

    REQUIRE (unfiltered < -1.0f);           // it is compressing at all
    REQUIRE (at80 > unfiltered + 1.0f);     // filtering the key backs it off
    REQUIRE (at160 > at80);                 // and more filtering backs it off further
}

TEST_CASE ("makeup gain restores level", "[comp]")
{
    const auto levelFor = [] (float makeupDb)
    {
        CompFixture f;
        f.set (ParamID::compOn, 1.0f);
        f.set (ParamID::compThreshold, -24.0f);
        f.set (ParamID::compRatio, 6.0f);
        f.set (ParamID::compMakeup, makeupDb);
        f.prepare();
        return levelDb (f.run (sine (220.0f, 32768, 1, 0.5f)), 16384);
    };

    const auto plain = levelFor (0.0f);
    const auto lifted = levelFor (6.0f);

    INFO ("no makeup " << plain << " dB, +6 dB makeup " << lifted << " dB");
    REQUIRE (lifted - plain == Catch::Approx (6.0).margin (0.5));
}

TEST_CASE ("gain reduction meter tracks what the compressor is doing", "[comp]")
{
    CompFixture f;
    f.set (ParamID::compOn, 1.0f);
    f.set (ParamID::compThreshold, -30.0f);
    f.set (ParamID::compRatio, 10.0f);
    f.set (ParamID::compSidechain, 0.0f);
    f.prepare();

    f.run (sine (220.0f, 32768, 1, 0.8f));
    const auto hard = f.pedal.getGainReductionDb();

    CompFixture quiet;
    quiet.set (ParamID::compOn, 1.0f);
    quiet.set (ParamID::compThreshold, -30.0f);
    quiet.set (ParamID::compRatio, 10.0f);
    quiet.set (ParamID::compSidechain, 0.0f);
    quiet.prepare();

    quiet.run (sine (220.0f, 32768, 1, 0.01f));
    const auto soft = quiet.pedal.getGainReductionDb();

    INFO ("loud signal reads " << hard << " dB, quiet signal reads " << soft << " dB");

    REQUIRE (hard < -3.0f);
    REQUIRE (soft > -0.5f);
    REQUIRE (hard <= 0.0f);
}

TEST_CASE ("compressor adds no latency", "[comp][latency]")
{
    CompFixture f;
    f.prepare();
    REQUIRE (f.pedal.getLatencySamples() == 0);
}

TEST_CASE ("compressor stays finite on a hot signal", "[comp][stability]")
{
    CompFixture f;
    f.set (ParamID::compOn, 1.0f);
    f.set (ParamID::compThreshold, -48.0f);
    f.set (ParamID::compRatio, 20.0f);
    f.set (ParamID::compMakeup, 24.0f);
    f.prepare (2);

    const auto out = f.run (sine (41.2f, 16384, 2, 0.95f));

    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
            REQUIRE (std::isfinite (out.getSample (ch, i)));
}

// The registry is what makes a second pedal cheap, so it is worth asserting
// that the second pedal is actually wired through it rather than bolted on.
TEST_CASE ("the rig is built from the registry", "[comp][registry]")
{
    const auto& entries = PedalRegistry::getEntries();

    // The DEFAULT order, which the user can rearrange: gate first so hum is
    // dealt with before the compressor lifts it, then level, then the two
    // pedals that track the signal, then modulation, then drive.
    REQUIRE (entries.size() == 7);
    REQUIRE (juce::String (entries[0].id) == "gate");
    REQUIRE (juce::String (entries[1].id) == "compressor");
    REQUIRE (juce::String (entries[2].id) == "octaver");
    REQUIRE (juce::String (entries[3].id) == "envelope");
    REQUIRE (juce::String (entries[4].id) == "phaser");
    REQUIRE (juce::String (entries[5].id) == "chorus");
    REQUIRE (juce::String (entries[6].id) == "drive");

    for (const auto& entry : entries)
    {
        INFO ("entry: " << entry.id);
        REQUIRE (entry.create != nullptr);
        REQUIRE (entry.addParameters != nullptr);
    }

    HarnessProcessor host;
    auto chain = PedalRegistry::createChain (host.apvts);

    REQUIRE (chain.size() == entries.size());

    for (auto& pedal : chain)
        REQUIRE (pedal != nullptr);
}

// Parameters come through the registry, so a pedal added without its controls
// being registered would fail here rather than silently having dead knobs.
TEST_CASE ("every pedal contributes its parameters", "[comp][registry]")
{
    HarnessProcessor host;

    for (const auto* id : { ParamID::compOn, ParamID::compThreshold, ParamID::compRatio,
                            ParamID::compAttack, ParamID::compRelease, ParamID::compMakeup,
                            ParamID::compSidechain,
                            ParamID::envOn, ParamID::envSens, ParamID::envAttack,
                            ParamID::envRelease, ParamID::envQ, ParamID::envRange,
                            ParamID::envDryHighs, ParamID::envMode,
                            ParamID::chorusOn, ParamID::chorusRate, ParamID::chorusDepth,
                            ParamID::chorusMix, ParamID::chorusCrossover, ParamID::chorusMode,
                            ParamID::phaserOn, ParamID::phaserRate, ParamID::phaserDepth,
                            ParamID::phaserFeedback, ParamID::phaserStages, ParamID::phaserMix,
                            ParamID::phaserInvert,
                            ParamID::gateOn, ParamID::gateThreshold, ParamID::gateAttack,
                            ParamID::gateHold, ParamID::gateRelease, ParamID::gateRange,
                            ParamID::gateSidechain,
                            ParamID::octOn, ParamID::octDirect, ParamID::octSubOne,
                            ParamID::octSubTwo, ParamID::octTone, ParamID::octTrack,
                            ParamID::drive, ParamID::blend, ParamID::bass, ParamID::treble })
    {
        INFO ("parameter: " << id);
        REQUIRE (host.apvts.getParameter (id) != nullptr);
    }
}
