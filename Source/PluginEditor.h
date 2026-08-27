#pragma once

#include "PluginProcessor.h"
#include "ui/PedalControls.h"
#include "ui/PedalLookAndFeel.h"
#include "ui/ChainStrip.h"
#include "ui/PresetBar.h"
#include "ui/RackView.h"

#include <juce_audio_utils/juce_audio_utils.h>

// The pedal face.
//
// Laid out at a fixed design size and scaled with an AffineTransform rather than
// reflowing, because a pedal is a physical object: the control positions carry
// meaning and rearranging them at a different window size would make it a
// different pedal. The host can resize freely; the face keeps its proportions.
class BassRigEditor final : public juce::AudioProcessorEditor
{
public:
    explicit BassRigEditor (BassRigProcessor&);
    ~BassRigEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth  = 460;
    static constexpr int designHeight = 930;

private:
    // Everything lives on this child, which is sized to the design size and
    // then transformed. Painting the enclosure happens here too.
    class Face final : public juce::Component
    {
    public:
        explicit Face (BassRigProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        BassRigProcessor& proc;

        ChainStrip chainStrip;
        RackView rack;

        KnobControl bass, loMid, hiMid, treble;
        KnobControl drive, blend, level, master, trim;
        ThreeWaySwitch loMidFreq, hiMidFreq, attack, grunt;
        Footswitch footswitch;
        PresetBar presetBar;
    };

    PedalLookAndFeel lookAndFeel;
    Face face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassRigEditor)
};
