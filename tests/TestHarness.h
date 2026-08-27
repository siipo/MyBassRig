#pragma once

#include "params/Parameters.h"
#include <juce_audio_processors/juce_audio_processors.h>

// Minimal AudioProcessor so tests can own an APVTS without pulling in the
// plugin wrapper.
class HarnessProcessor final : public juce::AudioProcessor
{
public:
    HarnessProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput  ("In",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "STATE", Params::createLayout()) {}

    const juce::String getName() const override { return "Harness"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    void setParam (const char* id, float value)
    {
        auto* p = apvts.getParameter (id);
        jassert (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    // Must outlive apvts: APVTS runs a Timer, which needs a MessageManager.
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::AudioProcessorValueTreeState apvts;
};
