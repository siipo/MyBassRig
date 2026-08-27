#include "Presets.h"

namespace
{
    // Choice parameters are stored in the APVTS state as their index.
    constexpr float gruntFull = 0.0f, gruntMid = 1.0f, gruntTight = 2.0f;
    constexpr float attackBoost = 0.0f, attackFlat = 1.0f, attackCut = 2.0f;
    constexpr float loMid250 = 0.0f, loMid500 = 1.0f, loMid1k = 2.0f;
    constexpr float hiMid750 = 0.0f, hiMid1k5 = 1.0f, hiMid3k = 2.0f;

    // Tone knobs are positions, not decibels: 0.5 is flat.
    constexpr float flat = 0.5f;

    constexpr float on = 1.0f, off = 0.0f;
    constexpr float scOff = 0.0f, sc80 = 1.0f, sc160 = 2.0f;
    constexpr float envUp = 0.0f, envDown = 1.0f, envHiQ = 2.0f;
    constexpr float scGateOff = 0.0f, scGate80 = 1.0f, scGate160 = 2.0f;
}

chowdsp::Preset Presets::make (const juce::String& name,
                               const juce::String& category,
                               const juce::AudioProcessor& processor,
                               std::initializer_list<Setting> settings,
                               const std::vector<int>& chainOrder)
{
    // Built from the processor's own parameter list rather than from a captured
    // snapshot, so a preset can never silently omit a parameter added later --
    // anything not named here lands on its documented default.
    juce::XmlElement state { "STATE" };

    for (auto* parameter : processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);

        if (ranged == nullptr)
            continue;

        auto value = ranged->convertFrom0to1 (ranged->getDefaultValue());

        for (const auto& [id, overrideValue] : settings)
            if (ranged->paramID == id)
                value = overrideValue;

        auto* element = state.createNewChildElement ("PARAM");
        element->setAttribute ("id", ranged->paramID);
        element->setAttribute ("value", value);
    }

    // The order pedals run in is part of a sound, so a preset can carry one.
    // Anything that leaves this empty simply inherits whatever is loaded.
    if (! chainOrder.empty())
    {
        juce::StringArray parts;

        for (const auto index : chainOrder)
            parts.add (juce::String (index));

        state.setAttribute ("chainOrder", parts.joinIntoString (","));
    }

    return { name, vendor, state, category };
}

chowdsp::Preset Presets::makeDefault (const juce::AudioProcessor& processor)
{
    return make (defaultPresetName, "Clean", processor, {});
}

std::vector<chowdsp::Preset> Presets::createFactoryPresets (const juce::AudioProcessor& processor)
{
    std::vector<chowdsp::Preset> presets;

    presets.push_back (makeDefault (processor));

    // The pedal as a clean preamp and DI: no drive path at all, EQ flat.
    presets.push_back (make ("Clean DI", "Clean", processor,
                             { { ParamID::blend, 0.0f },
                               { ParamID::drive, 0.0f } }));

    // The compressor on its own, with the drive stage out of the way: what the
    // pedal does as a levelling front end rather than as a distortion.
    presets.push_back (make ("Studio Comp", "Clean", processor,
                             { { ParamID::blend, 0.0f },
                               { ParamID::drive, 0.0f },
                               { ParamID::compOn, on },
                               { ParamID::compThreshold, -22.0f },
                               { ParamID::compRatio, 3.5f },
                               { ParamID::compAttack, 25.0f },
                               { ParamID::compRelease, 200.0f },
                               { ParamID::compMakeup, 5.0f },
                               { ParamID::compSidechain, sc80 } }));

    // Just enough grit to thicken a line without reading as distortion. The
    // clean path still carries the fundamental.
    presets.push_back (make ("Warm Up", "Drive", processor,
                             { { ParamID::drive, 0.30f },
                               { ParamID::blend, 0.35f },
                               { ParamID::grunt, gruntFull },
                               { ParamID::attack, attackCut },
                               { ParamID::loMid, 2.0f },
                               { ParamID::loMidFreq, loMid500 },
                               { ParamID::compOn, on },
                               { ParamID::compThreshold, -20.0f },
                               { ParamID::compRatio, 3.0f },
                               { ParamID::compMakeup, 3.0f } }));

    // The sound this class of pedal exists for: the low end stays clean because
    // Grunt keeps it out of the clipper, while everything above it is driven
    // hard. Blend near the middle so both halves are audible.
    presets.push_back (make ("Clean Lows, Dirty Top", "Drive", processor,
                             { { ParamID::drive, 0.75f },
                               { ParamID::blend, 0.55f },
                               { ParamID::grunt, gruntTight },
                               { ParamID::attack, attackBoost },
                               { ParamID::hiMid, 3.0f },
                               { ParamID::hiMidFreq, hiMid1k5 },
                               // Compressed before the drive, so the clipper
                               // sees an even signal and the grind stays
                               // consistent across the neck.
                               { ParamID::compOn, on },
                               { ParamID::compThreshold, -16.0f },
                               { ParamID::compRatio, 4.0f },
                               { ParamID::compAttack, 10.0f },
                               { ParamID::compMakeup, 4.0f },
                               { ParamID::compSidechain, sc160 } }));

    // Scooped and aggressive: mids pulled down, top pushed up, low end kept out
    // of the clipper so it stays defined under the distortion.
    presets.push_back (make ("Modern Scoop", "Drive", processor,
                             { { ParamID::drive, 0.85f },
                               { ParamID::blend, 0.70f },
                               { ParamID::grunt, gruntTight },
                               { ParamID::attack, attackBoost },
                               { ParamID::bass, 0.72f },
                               { ParamID::loMid, -5.0f },
                               { ParamID::loMidFreq, loMid500 },
                               { ParamID::hiMid, 2.0f },
                               { ParamID::hiMidFreq, hiMid3k },
                               { ParamID::treble, 0.68f } }));

    // The opposite voicing: mids pushed forward so the bass cuts through a
    // dense mix rather than sitting under it.
    presets.push_back (make ("Mid Push", "Drive", processor,
                             { { ParamID::drive, 0.55f },
                               { ParamID::blend, 0.50f },
                               { ParamID::grunt, gruntMid },
                               { ParamID::attack, attackFlat },
                               { ParamID::loMid, 4.0f },
                               { ParamID::loMidFreq, loMid1k },
                               { ParamID::hiMid, 3.0f },
                               { ParamID::hiMidFreq, hiMid750 } }));

    // The envelope filter as the pedal it is modelled on does it: a wah that
    // tracks the picking hand, over a clean signal.
    presets.push_back (make ("Auto Wah", "Filter", processor,
                             { { ParamID::blend, 0.0f },
                               { ParamID::drive, 0.0f },
                               { ParamID::envOn, on },
                               { ParamID::envMode, envHiQ },
                               { ParamID::envSens, 0.55f },
                               { ParamID::envAttack, 8.0f },
                               { ParamID::envRelease, 160.0f },
                               { ParamID::envQ, 6.0f },
                               { ParamID::envRange, 3.5f },
                               { ParamID::envDryHighs, 0.25f },
                               { ParamID::compOn, on },
                               { ParamID::compThreshold, -20.0f },
                               { ParamID::compRatio, 3.0f },
                               { ParamID::compMakeup, 3.0f } }));

    // Down mode with the filter closing as you dig in, and drive underneath:
    // the synth-bass voice.
    presets.push_back (make ("Synth Bass", "Filter", processor,
                             { { ParamID::drive, 0.7f },
                               { ParamID::blend, 0.65f },
                               { ParamID::grunt, gruntMid },
                               { ParamID::envOn, on },
                               { ParamID::envMode, envDown },
                               { ParamID::envSens, 0.7f },
                               { ParamID::envAttack, 4.0f },
                               { ParamID::envRelease, 300.0f },
                               { ParamID::envQ, 8.0f },
                               { ParamID::envRange, 4.0f },
                               { ParamID::envDryHighs, 0.1f },
                               { ParamID::bass, 0.62f } }));

    // Chain order 4,0,1,2,3 puts the DRIVE first, so the compressor is levelling
    // an already distorted signal and the filter is tracking it. On a physical
    // board this is a patch nobody plans and everybody stumbles into.
    presets.push_back (make ("Backwards Rig", "Filter", processor,
                             { { ParamID::drive, 0.8f },
                               { ParamID::blend, 0.8f },
                               { ParamID::grunt, gruntMid },
                               { ParamID::compOn, on },
                               { ParamID::compThreshold, -26.0f },
                               { ParamID::compRatio, 6.0f },
                               { ParamID::compMakeup, 6.0f },
                               { ParamID::envOn, on },
                               { ParamID::envMode, envHiQ },
                               { ParamID::envSens, 0.6f },
                               { ParamID::envQ, 7.0f } },
                             { 6, 0, 1, 2, 3, 4, 5 }));

    // Modulation after the drive rather than before it, which smears the
    // distortion instead of grinding a moving signal.
    presets.push_back (make ("Wash", "Modulation", processor,
                             { { ParamID::drive, 0.45f },
                               { ParamID::blend, 0.5f },
                               { ParamID::chorusOn, on },
                               { ParamID::chorusRate, 0.35f },
                               { ParamID::chorusDepth, 4.0f },
                               { ParamID::chorusMix, 0.6f },
                               { ParamID::phaserOn, on },
                               { ParamID::phaserRate, 0.2f },
                               { ParamID::phaserStages, 2.0f },
                               { ParamID::phaserMix, 0.4f } },
                             { 0, 1, 2, 3, 6, 4, 5 }));

    // The octave divider under a driven signal, which is the classic use: the
    // sub is generated clean and then distorted along with everything else.
    presets.push_back (make ("Sub Grind", "Octave", processor,
                             { { ParamID::octOn, on },
                               { ParamID::octDirect, 1.0f },
                               { ParamID::octSubOne, 0.7f },
                               { ParamID::octSubTwo, 0.0f },
                               { ParamID::octTone, 600.0f },
                               { ParamID::octTrack, 250.0f },
                               // Lifted, because this preset is aimed at the
                               // low strings where a pure octave is inaudible.
                               { ParamID::octGrowl, 0.6f },
                               { ParamID::drive, 0.6f },
                               { ParamID::blend, 0.55f },
                               { ParamID::grunt, gruntMid },
                               { ParamID::gateOn, on },
                               { ParamID::gateThreshold, -50.0f },
                               { ParamID::gateSidechain, scGate80 } }));

    // Everything into the clipper including the fundamental, which is the one
    // setting where the low end is deliberately allowed to break up.
    presets.push_back (make ("Full Grind", "Fuzz", processor,
                             { { ParamID::drive, 1.0f },
                               { ParamID::blend, 0.90f },
                               { ParamID::grunt, gruntFull },
                               { ParamID::attack, attackFlat },
                               { ParamID::bass, 0.60f },
                               { ParamID::treble, 0.42f },
                               { ParamID::level, -2.0f } }));

    return presets;
}
