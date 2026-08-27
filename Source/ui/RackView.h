#pragma once

#include "PedalControls.h"
#include "GainReductionMeter.h"
#include "dsp/NoiseGatePedal/NoiseGatePedal.h"
#include "dsp/OctaverPedal/OctaverPedal.h"

// The effects rack: one pedal panel visible at a time, chosen with a tab strip.
//
// This exists because stacking pedals vertically does not scale. Compressor,
// EQ and drive as three fixed sections already ran to 870 pixels; adding the
// envelope filter the same way would have made it 1080, and the planned chorus
// and phaser would take it past 1500. Tabs keep the window the same height
// whatever goes in the rack.
//
// The EQ and drive stay permanently visible outside this, because they are the
// pedal's identity rather than effects you switch between.
//
// Each tab carries a lamp showing whether that pedal is switched on, so a rack
// pedal running in the background is visible without selecting its tab.
class RackView final : public juce::Component
{
public:
    RackView (juce::AudioProcessorValueTreeState& state, const CompressorPedal* compressor);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int tabStripHeight = 26;

    // Public so tooling can drive it: the snapshot renderer captures every tab,
    // and the editor tests check that switching tab actually changes pixels.
    void select (int index);
    int getSelectedTab() const noexcept { return selected; }
    int getNumTabs() const noexcept { return (int) tabs.size(); }

private:
    class CompressorPanel final : public juce::Component
    {
    public:
        explicit CompressorPanel (juce::AudioProcessorValueTreeState&, const CompressorPedal*);
        void resized() override;

    private:
        KnobControl threshold, ratio, attack, release, makeup;
        ThreeWaySwitch sidechain;
        SectionToggle on;
        GainReductionMeter meter;
    };

    class EnvelopePanel final : public juce::Component
    {
    public:
        explicit EnvelopePanel (juce::AudioProcessorValueTreeState&);
        void resized() override;

    private:
        KnobControl sens, attack, release, q, range, dryHighs;
        ThreeWaySwitch mode;
        SectionToggle on;
    };

    class PhaserPanel final : public juce::Component
    {
    public:
        explicit PhaserPanel (juce::AudioProcessorValueTreeState&);
        void resized() override;

    private:
        KnobControl rate, depth, feedback, mix, crossover;
        ThreeWaySwitch stages;
        SectionToggle on, invert;
    };

    class GatePanel final : public juce::Component
    {
    public:
        explicit GatePanel (juce::AudioProcessorValueTreeState&);
        void resized() override;

    private:
        KnobControl threshold, attack, hold, release, range;
        ThreeWaySwitch sidechain;
        SectionToggle on;
    };

    class OctaverPanel final : public juce::Component
    {
    public:
        explicit OctaverPanel (juce::AudioProcessorValueTreeState&);
        void resized() override;

    private:
        KnobControl direct, subOne, subTwo, growl, tone, track;
        SectionToggle on;
    };

    class ChorusPanel final : public juce::Component
    {
    public:
        explicit ChorusPanel (juce::AudioProcessorValueTreeState&);
        void resized() override;

    private:
        KnobControl rate, depth, mix, crossover;
        ThreeWaySwitch mode;
        SectionToggle on;
    };

    struct Tab
    {
        juce::String name;
        juce::String onParamID;
        juce::Component* panel;
        juce::Rectangle<int> bounds;
        bool lit = false;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    void mouseDown (const juce::MouseEvent&) override;

    CompressorPanel compressorPanel;
    EnvelopePanel envelopePanel;
    PhaserPanel phaserPanel;
    ChorusPanel chorusPanel;
    GatePanel gatePanel;
    OctaverPanel octaverPanel;

    std::vector<std::unique_ptr<Tab>> tabs;
    int selected = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackView)
};
