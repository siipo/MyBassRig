#include "PedalRegistry.h"
#include "CompressorPedal/CompressorPedal.h"
#include "ChorusPedal/ChorusPedal.h"
#include "NoiseGatePedal/NoiseGatePedal.h"
#include "OctaverPedal/OctaverPedal.h"
#include "EnvelopeFilterPedal/EnvelopeFilterPedal.h"
#include "PhaserPedal/PhaserPedal.h"
#include "DrivePedal/DrivePedal.h"

const std::vector<PedalRegistry::Entry>& PedalRegistry::getEntries()
{
    // The DEFAULT signal order, which the user can rearrange at runtime (see
    // PluginProcessor). It follows how these sit on a real board: the gate
    // first, so hum is dealt with before the compressor amplifies it; then
    // compression, so everything downstream sees an even signal; then the
    // octaver and the envelope filter, both of which track the signal and want
    // it consistent; then modulation; and drive last, so what gets distorted is
    // a moving signal rather than the other way round.
    //
    // Drag the gate to the end instead if what needs gating is drive hiss
    // rather than pickup hum -- that is the whole point of the chain strip.
    static const std::vector<Entry> entries
    {
        { "gate", "Noise Gate",
          [] (juce::AudioProcessorValueTreeState& s)
          { return std::unique_ptr<Pedal> (std::make_unique<NoiseGatePedal> (s)); },
          &NoiseGatePedal::addParameters },

        { "compressor", "Compressor",
          [] (juce::AudioProcessorValueTreeState& s)
          { return std::unique_ptr<Pedal> (std::make_unique<CompressorPedal> (s)); },
          &CompressorPedal::addParameters },

        { "octaver", "Octaver",
          [] (juce::AudioProcessorValueTreeState& s)
          { return std::unique_ptr<Pedal> (std::make_unique<OctaverPedal> (s)); },
          &OctaverPedal::addParameters },

        { "envelope", "Envelope Filter",
          [] (juce::AudioProcessorValueTreeState& s)
          { return std::unique_ptr<Pedal> (std::make_unique<EnvelopeFilterPedal> (s)); },
          &EnvelopeFilterPedal::addParameters },

        { "phaser", "Phaser",
          [] (juce::AudioProcessorValueTreeState& s)
          { return std::unique_ptr<Pedal> (std::make_unique<PhaserPedal> (s)); },
          &PhaserPedal::addParameters },

        { "chorus", "Chorus",
          [] (juce::AudioProcessorValueTreeState& s)
          { return std::unique_ptr<Pedal> (std::make_unique<ChorusPedal> (s)); },
          &ChorusPedal::addParameters },

        { "drive", "Drive",
          [] (juce::AudioProcessorValueTreeState& s)
          { return std::unique_ptr<Pedal> (std::make_unique<DrivePedal> (s)); },
          &DrivePedal::addParameters },
    };

    return entries;
}

std::unique_ptr<Pedal> PedalRegistry::create (int index, juce::AudioProcessorValueTreeState& state)
{
    const auto& entries = getEntries();

    if (juce::isPositiveAndBelow (index, static_cast<int> (entries.size())))
        return entries[(size_t) index].create (state);

    return nullptr;
}

std::vector<std::unique_ptr<Pedal>> PedalRegistry::createChain (juce::AudioProcessorValueTreeState& state)
{
    std::vector<std::unique_ptr<Pedal>> chain;

    for (const auto& entry : getEntries())
        chain.push_back (entry.create (state));

    return chain;
}

void PedalRegistry::addAllParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    for (const auto& entry : getEntries())
    {
        jassert (entry.addParameters != nullptr);
        entry.addParameters (layout);
    }
}
