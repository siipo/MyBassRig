#pragma once

#include "dsp/Pedal.h"
#include "params/Parameters.h"
#include "SafePresetManager.h"
#include "Presets.h"
#include <juce_audio_processors/juce_audio_processors.h>

class BassRigProcessor final : public juce::AudioProcessor,
                               private juce::ValueTree::Listener
{
public:
    BassRigProcessor();
    ~BassRigProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // Presets are exposed through the host program API as well as the plugin
    // UI, so a DAW can see and automate them.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- chain order -----------------------------------------------------
    //
    // The rig can be rearranged at runtime, which the design originally ruled
    // out on the grounds that swapping pedals needs a lock shared with the
    // audio callback. That was answering the wrong question. The SET of pedals
    // never changes -- every one is built once and lives for the life of the
    // plugin -- so reordering only changes the order they are visited in.
    //
    // That fits in a single word: four bits per slot, packed into one atomic.
    // The audio thread does one acquire load per block and the message thread
    // one release store. No lock, no allocation, and the order can only ever be
    // seen whole, never half applied.
    //
    // It lives as a property on the APVTS tree rather than as a parameter,
    // because a permutation is not something a host should be able to automate
    // into an invalid state. It still travels with the session and with presets.
    static constexpr int maxPedals = 8;
    static const juce::Identifier chainOrderProperty;

    void setChainOrder (const std::vector<int>& order);
    std::vector<int> getChainOrder() const;
    int getNumPedals() const noexcept { return (int) chain.size(); }
    const Pedal* getPedal (int index) const;

    // Looked up by name so the editor does not have to know the chain order.
    const Pedal* findPedal (const char* name) const;

    SafePresetManager presetManager { apvts };

private:
    // The whole rig, in signal order. Every pedal runs on every block; each
    // one has its own bypass rather than being swapped in and out, which is
    // what keeps the audio thread free of locks.
    std::vector<std::unique_ptr<Pedal>> chain;
    Params::LosslessBool* bypassParam = nullptr;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void updatePackedOrder();

    std::atomic<juce::uint32> packedOrder { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassRigProcessor)
};
