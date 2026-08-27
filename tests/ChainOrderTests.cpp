#include "PluginProcessor.h"
#include "TestSignals.h"
#include "Presets.h"
#include "dsp/PedalRegistry.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct ChainFixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        BassRigProcessor proc;

        void set (const char* id, float value)
        {
            auto* p = proc.apvts.getParameter (id);
            jassert (p != nullptr);
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        }

        // Renders a note through the whole rig as a host would.
        juce::AudioBuffer<float> render (float frequency = 110.0f, float amplitude = 0.6f)
        {
            proc.prepareToPlay (TestSignals::sampleRate, TestSignals::blockSize);

            const auto numChannels = juce::jmax (1, proc.getTotalNumOutputChannels());
            auto buffer = TestSignals::sine (frequency, TestSignals::blockSize * 24,
                                             numChannels, amplitude);
            juce::MidiBuffer midi;

            for (int pos = 0; pos + TestSignals::blockSize <= buffer.getNumSamples();
                 pos += TestSignals::blockSize)
            {
                juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), numChannels,
                                                pos, TestSignals::blockSize);
                proc.processBlock (block, midi);
            }

            return buffer;
        }
    };

    double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                       int startSample)
    {
        double total = 0.0;

        for (int i = startSample; i < a.getNumSamples(); ++i)
            total += std::abs ((double) a.getSample (0, i) - (double) b.getSample (0, i));

        return total;
    }
}

// A preset can carry an order, because an unusual arrangement is part of a
// sound. This goes through a different path than a session restore: loadPreset
// calls replaceState directly, which REDIRECTS the value tree rather than
// changing a property on it, so a listener watching only for property changes
// never hears about it and the preset's order is silently ignored.
TEST_CASE ("a preset carries its chain order", "[chain][presets]")
{
    ChainFixture f;

    juce::StringArray names;

    for (int i = 0; i < f.proc.presetManager.getNumPresets(); ++i)
        names.add (f.proc.presetManager.getPresetName (i));

    const auto index = names.indexOf ("Backwards Rig");
    REQUIRE (index >= 0);

    f.proc.presetManager.loadPresetFromIndex (index);

    const std::vector<int> expected { 6, 0, 1, 2, 3, 4, 5 };
    const auto actual = f.proc.getChainOrder();

    juce::String description;

    for (const auto slot : actual)
        description << slot << " ";

    INFO ("order after loading the preset: " << description);
    REQUIRE (actual == expected);
}

// And a preset without one must not inherit the previous preset's arrangement.
TEST_CASE ("a preset without an order resets to the default", "[chain][presets]")
{
    ChainFixture f;

    juce::StringArray names;

    for (int i = 0; i < f.proc.presetManager.getNumPresets(); ++i)
        names.add (f.proc.presetManager.getPresetName (i));

    f.proc.presetManager.loadPresetFromIndex (names.indexOf ("Backwards Rig"));
    f.proc.presetManager.loadPresetFromIndex (names.indexOf ("Clean DI"));

    std::vector<int> identity ((size_t) f.proc.getNumPedals());
    std::iota (identity.begin(), identity.end(), 0);

    REQUIRE (f.proc.getChainOrder() == identity);
}

TEST_CASE ("the chain defaults to the registry order", "[chain]")
{
    ChainFixture f;
    const auto order = f.proc.getChainOrder();

    REQUIRE ((int) order.size() == f.proc.getNumPedals());
    REQUIRE ((int) order.size() == (int) PedalRegistry::getEntries().size());

    for (int i = 0; i < (int) order.size(); ++i)
        REQUIRE (order[(size_t) i] == i);
}

// The point of the whole feature. Compressing a distorted signal and distorting
// a compressed one are different sounds, and this is the test that says so.
TEST_CASE ("reordering the chain changes the sound", "[chain]")
{
    const auto renderWithOrder = [] (const std::vector<int>& order)
    {
        ChainFixture f;
        f.set (ParamID::compOn, 1.0f);
        f.set (ParamID::compThreshold, -30.0f);
        f.set (ParamID::compRatio, 10.0f);
        f.set (ParamID::drive, 0.9f);
        f.set (ParamID::blend, 1.0f);
        f.proc.setChainOrder (order);
        return f.render();
    };

    const auto entries = PedalRegistry::getEntries();
    std::vector<int> forwards ((size_t) entries.size());
    std::iota (forwards.begin(), forwards.end(), 0);

    auto backwards = forwards;
    std::reverse (backwards.begin(), backwards.end());

    const auto normal = renderWithOrder (forwards);
    const auto reversed = renderWithOrder (backwards);

    const auto delta = difference (normal, reversed, TestSignals::blockSize * 8);

    INFO ("accumulated difference between forwards and reversed: " << delta);
    REQUIRE (delta > 1.0);
}

TEST_CASE ("a chosen order is what actually runs", "[chain]")
{
    ChainFixture f;

    const auto count = f.proc.getNumPedals();
    std::vector<int> order ((size_t) count);
    std::iota (order.begin(), order.end(), 0);
    std::reverse (order.begin(), order.end());

    f.proc.setChainOrder (order);

    REQUIRE (f.proc.getChainOrder() == order);
}

// An order that is not a clean permutation would silently drop or double a
// pedal, which is much harder to notice than falling back to the default.
TEST_CASE ("a malformed order falls back to the default", "[chain]")
{
    const auto count = ChainFixture{}.proc.getNumPedals();

    std::vector<int> identity ((size_t) count);
    std::iota (identity.begin(), identity.end(), 0);

    SECTION ("too few entries")
    {
        ChainFixture f;
        f.proc.setChainOrder ({ 0, 1 });
        REQUIRE (f.proc.getChainOrder() == identity);
    }

    SECTION ("a duplicated pedal")
    {
        ChainFixture f;
        std::vector<int> duplicated ((size_t) count, 0);
        f.proc.setChainOrder (duplicated);
        REQUIRE (f.proc.getChainOrder() == identity);
    }

    SECTION ("an index that does not exist")
    {
        ChainFixture f;
        auto broken = identity;
        broken.back() = 99;
        f.proc.setChainOrder (broken);
        REQUIRE (f.proc.getChainOrder() == identity);
    }
}

// The order is part of the sound, so it has to survive being saved. It lives as
// a property on the APVTS tree rather than as a parameter, and replaceState
// swaps that tree wholesale -- which is exactly where this could quietly break.
TEST_CASE ("the chain order survives a session round trip", "[chain][state]")
{
    juce::MemoryBlock saved;
    std::vector<int> savedOrder;

    {
        ChainFixture a;
        savedOrder.resize ((size_t) a.proc.getNumPedals());
        std::iota (savedOrder.begin(), savedOrder.end(), 0);
        std::reverse (savedOrder.begin(), savedOrder.end());

        a.proc.setChainOrder (savedOrder);
        a.proc.getStateInformation (saved);
    }

    ChainFixture b;
    b.proc.setStateInformation (saved.getData(), (int) saved.getSize());

    INFO ("restored order size " << b.proc.getChainOrder().size());
    REQUIRE (b.proc.getChainOrder() == savedOrder);
}

// Reordering must keep working after a state restore, which means the tree
// listener has to be re-attached to the new tree rather than left on the old
// one.
TEST_CASE ("reordering still works after a state restore", "[chain][state]")
{
    juce::MemoryBlock saved;

    {
        ChainFixture a;
        a.proc.getStateInformation (saved);
    }

    ChainFixture b;
    b.proc.setStateInformation (saved.getData(), (int) saved.getSize());

    std::vector<int> order ((size_t) b.proc.getNumPedals());
    std::iota (order.begin(), order.end(), 0);
    std::reverse (order.begin(), order.end());

    b.proc.setChainOrder (order);

    REQUIRE (b.proc.getChainOrder() == order);
}

// Latency is the sum of the pedals, which does not depend on their order.
TEST_CASE ("reordering does not change reported latency", "[chain][latency]")
{
    ChainFixture f;
    f.proc.prepareToPlay (TestSignals::sampleRate, TestSignals::blockSize);
    const auto before = f.proc.getLatencySamples();

    std::vector<int> order ((size_t) f.proc.getNumPedals());
    std::iota (order.begin(), order.end(), 0);
    std::reverse (order.begin(), order.end());

    f.proc.setChainOrder (order);
    f.proc.prepareToPlay (TestSignals::sampleRate, TestSignals::blockSize);

    INFO ("latency before " << before << ", after " << f.proc.getLatencySamples());
    REQUIRE (f.proc.getLatencySamples() == before);
}

TEST_CASE ("every order produces finite audio", "[chain][stability]")
{
    ChainFixture probe;
    const auto count = probe.proc.getNumPedals();

    std::vector<int> order ((size_t) count);
    std::iota (order.begin(), order.end(), 0);

    // Every rotation, with everything switched on, since an arrangement nobody
    // anticipated is the entire point of the feature.
    for (int rotation = 0; rotation < count; ++rotation)
    {
        std::rotate (order.begin(), order.begin() + 1, order.end());

        ChainFixture f;
        f.set (ParamID::compOn, 1.0f);
        f.set (ParamID::envOn, 1.0f);
        f.set (ParamID::chorusOn, 1.0f);
        f.set (ParamID::phaserOn, 1.0f);
        f.set (ParamID::drive, 0.85f);
        f.proc.setChainOrder (order);

        const auto out = f.render (55.0f, 0.8f);

        juce::String description;

        for (const auto index : order)
            description << index << " ";

        INFO ("order: " << description);

        float peak = 0.0f;

        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                const auto sample = out.getSample (ch, i);
                REQUIRE (std::isfinite (sample));
                peak = juce::jmax (peak, std::abs (sample));
            }

        INFO ("peak " << peak);
        REQUIRE (peak < 16.0f);
    }
}
