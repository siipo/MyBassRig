#include "TestHarness.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

TEST_CASE ("every parameter ID resolves", "[params]")
{
    HarnessProcessor host;

    for (const auto* id : { ParamID::trim, ParamID::drive, ParamID::blend, ParamID::level,
                            ParamID::master, ParamID::grunt, ParamID::attack, ParamID::bass,
                            ParamID::loMid, ParamID::hiMid, ParamID::treble,
                            ParamID::loMidFreq, ParamID::hiMidFreq, ParamID::bypass })
    {
        INFO ("parameter: " << id);
        REQUIRE (host.apvts.getParameter (id) != nullptr);
    }
}

TEST_CASE ("state survives a save and load round trip", "[params][state]")
{
    HarnessProcessor a;
    a.setParam (ParamID::drive, 0.8f);
    a.setParam (ParamID::blend, 0.25f);
    a.setParam (ParamID::treble, 0.75f);   // knob position, not dB

    const auto saved = a.apvts.copyState().toXmlString();

    HarnessProcessor b;
    b.apvts.replaceState (juce::ValueTree::fromXml (saved));

    REQUIRE (b.apvts.getRawParameterValue (ParamID::drive)->load()  == Catch::Approx (0.8).margin (1e-4));
    REQUIRE (b.apvts.getRawParameterValue (ParamID::blend)->load()  == Catch::Approx (0.25).margin (1e-4));
    REQUIRE (b.apvts.getRawParameterValue (ParamID::treble)->load() == Catch::Approx (0.75).margin (1e-3));
}
