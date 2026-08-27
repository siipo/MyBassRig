#include "PluginProcessor.h"
#include "Presets.h"
#include "TestSignals.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

namespace
{
    struct PresetFixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        BassRigProcessor proc;

        float value (const char* id) const
        {
            return proc.apvts.getRawParameterValue (id)->load();
        }

        void setValue (const char* id, float v)
        {
            auto* p = proc.apvts.getParameter (id);
            jassert (p != nullptr);
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        }

        juce::StringArray presetNames()
        {
            juce::StringArray names;

            for (int i = 0; i < proc.presetManager.getNumPresets(); ++i)
                names.add (proc.presetManager.getPresetName (i));

            return names;
        }
    };
}

TEST_CASE ("factory presets are registered exactly once", "[presets]")
{
    PresetFixture f;
    const auto names = f.presetNames();

    INFO ("presets: " << names.joinIntoString (", "));

    // setDefaultPreset adds the preset if it is not already present, and the
    // default is also in the factory list, so this is really a duplication check.
    REQUIRE (f.proc.presetManager.getNumPresets() == 13);

    for (const auto& expected : { "Default", "Clean DI", "Studio Comp", "Warm Up", "Clean Lows, Dirty Top",
                                  "Modern Scoop", "Mid Push", "Auto Wah", "Synth Bass",
                                  "Backwards Rig", "Wash", "Sub Grind", "Full Grind" })
    {
        INFO ("looking for " << expected);
        REQUIRE (names.contains (expected));
    }

    juce::StringArray unique;
    unique.addArray (names);
    unique.removeDuplicates (false);
    REQUIRE (unique.size() == names.size());
}

// The plugin should open on a named preset rather than on nothing, and that
// preset has to agree with the parameter defaults -- otherwise double clicking
// a knob to reset it would move away from the preset the box claims is loaded.
TEST_CASE ("the plugin opens on the default preset", "[presets]")
{
    PresetFixture f;

    const auto* current = f.proc.presetManager.getCurrentPreset();
    REQUIRE (current != nullptr);
    REQUIRE (current->getName() == Presets::defaultPresetName);
    REQUIRE_FALSE (f.proc.presetManager.getIsDirty());

    for (auto* parameter : f.proc.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);

        if (ranged == nullptr)
            continue;

        const auto expected = ranged->convertFrom0to1 (ranged->getDefaultValue());
        INFO (ranged->paramID << ": " << ranged->convertFrom0to1 (ranged->getValue())
              << " against default " << expected);
        REQUIRE (ranged->convertFrom0to1 (ranged->getValue()) == Catch::Approx (expected).margin (1e-4));
    }
}

TEST_CASE ("loading a preset changes the parameters", "[presets]")
{
    PresetFixture f;
    const auto names = f.presetNames();
    const auto index = names.indexOf ("Full Grind");
    REQUIRE (index >= 0);

    f.proc.presetManager.loadPresetFromIndex (index);

    REQUIRE (f.value (ParamID::drive) == Catch::Approx (1.0f).margin (1e-3));
    REQUIRE (f.value (ParamID::blend) == Catch::Approx (0.90f).margin (1e-3));
    REQUIRE (f.value (ParamID::level) == Catch::Approx (-2.0f).margin (1e-3));
    REQUIRE (f.proc.presetManager.getCurrentPreset()->getName() == "Full Grind");
}

TEST_CASE ("editing a parameter marks the preset dirty", "[presets]")
{
    PresetFixture f;
    REQUIRE_FALSE (f.proc.presetManager.getIsDirty());

    f.setValue (ParamID::drive, 0.9f);

    INFO ("dirty flag after editing drive");
    REQUIRE (f.proc.presetManager.getIsDirty());
}

// Every factory preset is a set of numbers typed by hand, so each one is worth
// actually running: a value outside its parameter range, or one that drives the
// cascade somewhere unstable, would otherwise only show up in use.
TEST_CASE ("every factory preset produces finite audio", "[presets][stability]")
{
    PresetFixture f;

    for (int index = 0; index < f.proc.presetManager.getNumPresets(); ++index)
    {
        f.proc.presetManager.loadPresetFromIndex (index);

        const auto name = f.proc.presetManager.getPresetName (index);
        INFO ("preset: " << name);

        f.proc.prepareToPlay (TestSignals::sampleRate, TestSignals::blockSize);

        // The buffer must carry as many channels as the configured bus layout,
        // the way a host would supply it.
        const auto numChannels = juce::jmax (1, f.proc.getTotalNumOutputChannels());
        auto buffer = TestSignals::sine (110.0f, TestSignals::blockSize * 8, numChannels, 0.7f);
        juce::MidiBuffer midi;

        for (int pos = 0; pos + TestSignals::blockSize <= buffer.getNumSamples();
             pos += TestSignals::blockSize)
        {
            juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), numChannels,
                                            pos, TestSignals::blockSize);
            f.proc.processBlock (block, midi);
        }

        float peak = 0.0f;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto sample = buffer.getSample (0, i);
            REQUIRE (std::isfinite (sample));
            peak = juce::jmax (peak, std::abs (sample));
        }

        INFO ("peak " << peak);
        REQUIRE (peak > 0.0f);
        REQUIRE (peak < 8.0f);
    }
}

// The host program API is how a DAW sees presets, and it is easy to leave
// returning a single hard-coded program.
TEST_CASE ("presets are visible through the host program API", "[presets]")
{
    PresetFixture f;

    REQUIRE (f.proc.getNumPrograms() == f.proc.presetManager.getNumPresets());

    for (int i = 0; i < f.proc.getNumPrograms(); ++i)
    {
        f.proc.setCurrentProgram (i);
        INFO ("program " << i << ": " << f.proc.getProgramName (i));

        REQUIRE (f.proc.getProgramName (i).isNotEmpty());
        REQUIRE (f.proc.getCurrentProgram() == i);
    }
}

// The selected preset and its dirty state are part of the session. Without this
// a reopened project would claim to be sitting on a factory preset the user had
// already edited away from.
TEST_CASE ("the loaded preset survives a session round trip", "[presets][state]")
{
    juce::MemoryBlock saved;
    juce::String savedName;

    {
        PresetFixture a;
        const auto index = a.presetNames().indexOf ("Mid Push");
        REQUIRE (index >= 0);

        a.proc.presetManager.loadPresetFromIndex (index);
        a.setValue (ParamID::treble, 0.8f);   // and edit it, so it is dirty

        savedName = a.proc.presetManager.getCurrentPreset()->getName();
        REQUIRE (a.proc.presetManager.getIsDirty());

        a.proc.getStateInformation (saved);
    }

    PresetFixture b;
    b.proc.setStateInformation (saved.getData(), (int) saved.getSize());

    REQUIRE (b.proc.presetManager.getCurrentPreset() != nullptr);
    REQUIRE (b.proc.presetManager.getCurrentPreset()->getName() == savedName);
    REQUIRE (b.value (ParamID::treble) == Catch::Approx (0.8f).margin (1e-3));
    REQUIRE (b.proc.presetManager.getIsDirty());
}

// The other half of the round trip, and the one that is easy to get wrong once
// the restore order is arranged to make edits survive: a session saved on an
// UNEDITED preset must not come back wearing a dirty marker.
TEST_CASE ("an unedited preset comes back clean", "[presets][state]")
{
    juce::MemoryBlock saved;

    {
        PresetFixture a;
        const auto index = a.presetNames().indexOf ("Modern Scoop");
        REQUIRE (index >= 0);

        a.proc.presetManager.loadPresetFromIndex (index);
        REQUIRE_FALSE (a.proc.presetManager.getIsDirty());

        a.proc.getStateInformation (saved);
    }

    PresetFixture b;
    b.proc.setStateInformation (saved.getData(), (int) saved.getSize());

    REQUIRE (b.proc.presetManager.getCurrentPreset()->getName() == "Modern Scoop");
    INFO ("dirty after restoring an unedited preset");
    REQUIRE_FALSE (b.proc.presetManager.getIsDirty());
}

TEST_CASE ("parameter state still round trips with presets in the mix", "[presets][state]")
{
    juce::MemoryBlock saved;

    {
        PresetFixture a;
        a.setValue (ParamID::drive, 0.62f);
        a.setValue (ParamID::grunt, 2.0f);
        a.setValue (ParamID::hiMid, -8.5f);
        a.proc.getStateInformation (saved);
    }

    PresetFixture b;
    b.proc.setStateInformation (saved.getData(), (int) saved.getSize());

    REQUIRE (b.value (ParamID::drive) == Catch::Approx (0.62f).margin (1e-3));
    REQUIRE (b.value (ParamID::grunt) == Catch::Approx (2.0f).margin (1e-3));
    REQUIRE (b.value (ParamID::hiMid) == Catch::Approx (-8.5f).margin (1e-3));
}

// ---------------------------------------------------------------------------
// Found by pluginval at strictness 10, not by anything here.
//
// juce::AudioParameterBool has a NormalisableRange of { 0, 1, 1 }. APVTS saves
// the DENORMALISED value, and denormalising through an interval of 1 snaps, so
// a parameter sitting at 0.47 is saved as 0 and the 0.47 is gone. The host does
// not forget: a VST3 host caches the value it sent, so after a restore its
// cache and the plugin disagree. Params::LosslessBool uses a continuous range
// instead, which makes the save exact.
//
// The plugin SOUNDED right throughout, because every read goes through get(),
// which thresholds. What was wrong was the host's view of the parameter -- wrong
// in an automation lane, and wrong again the next time the host saves.
//
// Two earlier attempts are recorded in DESIGN.md 3p because both LOOKED
// correct: one forced parameters to match the tree after replaceState, the
// other snapped values on assignment. Both left the same divergence, and the
// second made the plugin silently alter what the host had sent. A companion
// test for AudioParameterChoice was also written and deleted -- it passed
// against the broken build, so it was testing nothing.
TEST_CASE ("bool parameters survive a state round trip exactly", "[presets][state]")
{
    PresetFixture fx;

    const char* bools[] = {
        ParamID::gateOn, ParamID::compOn,   ParamID::octOn,        ParamID::envOn,
        ParamID::phaserOn, ParamID::chorusOn, ParamID::phaserInvert, ParamID::bypass,
    };

    // Fractions well away from 0 and 1, since those are the ones a snapping
    // range destroys. 0.471294 is the value pluginval happened to fail on.
    const float values[] = { 0.0f, 0.10f, 0.31f, 0.471294f, 0.5f, 0.62f, 0.897f, 1.0f };

    for (const char* id : bools)
    {
        auto* param = fx.proc.apvts.getParameter (id);
        REQUIRE (param != nullptr);

        for (float sent : values)
        {
            INFO ("parameter: " << id << ", value: " << sent);

            param->setValueNotifyingHost (sent);

            // Held verbatim. A host that sent this value must read it back.
            REQUIRE (param->getValue() == Catch::Approx (sent).margin (1.0e-6f));

            juce::MemoryBlock state;
            fx.proc.getStateInformation (state);

            // Scribble over it with something on the other side of the
            // threshold, so a restore that does nothing cannot pass.
            param->setValueNotifyingHost (sent < 0.5f ? 0.93f : 0.07f);

            fx.proc.setStateInformation (state.getData(), (int) state.getSize());

            // Exactly what was sent, not merely something that reads the same
            // way. This is the assertion pluginval makes.
            REQUIRE (param->getValue() == Catch::Approx (sent).margin (1.0e-6f));

            // And the logical value the DSP reads must still agree.
            REQUIRE ((param->getValue() >= 0.5f) == (sent >= 0.5f));
        }
    }
}
