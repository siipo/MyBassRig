#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/PedalRegistry.h"

#if BASSRIG_CRASH_HANDLER
 #include <iostream>
 #include <mutex>
 #include <windows.h>
 #include <dbghelp.h>

namespace
{
    // Diagnostic only, and off unless -DBASSRIG_CRASH_HANDLER=ON. There is no
    // debugger on the machine this was built on, so the plugin installs a
    // handler in the HOST's process and reports where it died from the inside.
    //
    // Know its limit before reaching for it: it catches access violations and
    // NOT heap corruption, which Windows kills with __fastfail, past every
    // exception handler. Chasing the crash in DESIGN.md 3s it reported once and
    // then went silent, and the silence turned out to be the more useful half
    // of the answer. AddressSanitizer (-DBASSRIG_ASAN=ON) named that bug on the
    // first run; try it first.
    //
    // A plugin has no business installing a process-global crash handler in a
    // shipping build. That is exactly why this is behind a flag that defaults
    // to off.
    // Deliberately minimal. An earlier version called SymInitialize with
    // symbol loading and died inside the handler before it could write
    // anything -- doing a lot of work in an already-broken process is a good
    // way to learn nothing. Module plus offset is enough to say WHICH binary
    // faulted, which is the question.
    juce::String backtraceWithModules()
    {
        void* frames[64] = {};
        const auto captured = CaptureStackBackTrace (0, 64, frames, nullptr);

        juce::String out;

        for (int i = 0; i < (int) captured; ++i)
        {
            out << juce::String (i).paddedLeft (' ', 3) << ": ";

            HMODULE module {};
            wchar_t modulePath[MAX_PATH] = {};

            if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCWSTR) frames[i], &module)
                && GetModuleFileNameW (module, modulePath, MAX_PATH))
            {
                const juce::File file { juce::String (modulePath) };
                out << file.getFileName().paddedRight (' ', 24) << " +0x"
                    << juce::String::toHexString ((juce::int64) ((char*) frames[i] - (char*) module));
            }
            else
            {
                out << "(unknown module) " << juce::String::toHexString ((juce::int64) frames[i]);
            }

            out << juce::newLine;
        }

        return out;
    }

    void bassRigCrashHandler (void*)
    {
        const auto trace = backtraceWithModules();

        juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("bassrig-crash.txt")
            .appendText ("=== BassRig crash ===\n" + trace + "\n");

        std::cerr << "\n=== BassRig crash ===\n" << trace << std::endl;

        std::_Exit (139);
    }
}
#endif

#include <numeric>

BassRigProcessor::BassRigProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("In",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", Params::createLayout())
{
#if BASSRIG_CRASH_HANDLER
    static std::once_flag installed;
    std::call_once (installed, []
                    { juce::SystemStats::setApplicationCrashHandler (bassRigCrashHandler); });
#endif

    bypassParam = dynamic_cast<Params::LosslessBool*> (apvts.getParameter (ParamID::bypass));
    jassert (bypassParam != nullptr);

    chain = PedalRegistry::createChain (apvts);
    jassert (! chain.empty());
    jassert ((int) chain.size() <= maxPedals);   // four bits per slot

    updatePackedOrder();
    apvts.state.addListener (this);


    auto factoryPresets = Presets::createFactoryPresets (*this);
    presetManager.addPresets (factoryPresets);

    presetManager.setDefaultPreset (Presets::makeDefault (*this));
    presetManager.setUserPresetConfigFile ("BassRig/UserPresetPath.txt");
    presetManager.setUserPresetName ("User");

    // setDefaultPreset only registers it. Without this the plugin opens with no
    // preset selected at all and the name box reads "No Preset".
    presetManager.loadDefaultPreset();
}

BassRigProcessor::~BassRigProcessor() = default;

// A bass guitar is a mono source. Wibeboard hard-required stereo in and out,
// which made it useless on a mono track; all three sensible layouts work here.
bool BassRigProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    const auto mono   = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    return (in == mono   && out == mono)
        || (in == mono   && out == stereo)
        || (in == stereo && out == stereo);
}

void BassRigProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32> (samplesPerBlock),
        static_cast<juce::uint32> (juce::jmax (1, getTotalNumOutputChannels()))
    };

    int totalLatency = 0;

    for (auto& pedal : chain)
    {
        pedal->prepare (spec);
        totalLatency += pedal->getLatencySamples();
    }

    setLatencySamples (totalLatency);
}

void BassRigProcessor::releaseResources()
{
    for (auto& pedal : chain)
        pedal->reset();
}

void BassRigProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    // Mono in, stereo out: fan the input out before processing so both sides
    // see the same signal and the pedal never has to care about the layout.
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, buffer.getNumSamples());

    if (bypassParam != nullptr && bypassParam->get())
        return;

    if (chain.empty())
        return;

    // JUCE guarantees the buffer carries max(numIn, numOut) channels, so the
    // jmin is belt and braces -- but without it a caller that breaks that
    // contract walks off the end of the channel array and takes the process
    // down, which is a bad way to find out.
    const auto channelsToProcess = juce::jmin (numOut, buffer.getNumChannels());

    juce::dsp::AudioBlock<float> block { buffer };
    auto activeBlock = block.getSubsetChannelBlock (0, static_cast<size_t> (channelsToProcess));

    // One acquire load, then the pedals are visited in whatever order the user
    // has arranged. The order cannot change halfway through a block.
    const auto order = packedOrder.load (std::memory_order_acquire);
    const auto count = (int) chain.size();

    for (int slot = 0; slot < count; ++slot)
    {
        const auto index = (int) ((order >> (slot * 4)) & 0xF);

        if (juce::isPositiveAndBelow (index, count))
            chain[(size_t) index]->process (activeBlock);
    }
}

const juce::Identifier BassRigProcessor::chainOrderProperty { "chainOrder" };

void BassRigProcessor::setChainOrder (const std::vector<int>& order)
{
    juce::StringArray parts;

    for (const auto index : order)
        parts.add (juce::String (index));

    apvts.state.setProperty (chainOrderProperty, parts.joinIntoString (","), nullptr);
}

std::vector<int> BassRigProcessor::getChainOrder() const
{
    const auto packed = packedOrder.load (std::memory_order_acquire);

    std::vector<int> order;

    for (int slot = 0; slot < (int) chain.size(); ++slot)
        order.push_back ((int) ((packed >> (slot * 4)) & 0xF));

    return order;
}

const Pedal* BassRigProcessor::getPedal (int index) const
{
    return juce::isPositiveAndBelow (index, (int) chain.size()) ? chain[(size_t) index].get()
                                                                : nullptr;
}

void BassRigProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == apvts.state && property == chainOrderProperty)
        updatePackedOrder();
}

// Both paths that load a whole state go through here rather than through
// valueTreePropertyChanged. AudioProcessorValueTreeState::replaceState assigns
// the tree (`state = newState`), which REDIRECTS it: no property-changed
// callbacks fire for the new contents, only this one. Preset loading calls
// replaceState directly, so without this a preset carrying a chain order had
// the order silently ignored -- which is exactly what happened.
void BassRigProcessor::valueTreeRedirected (juce::ValueTree& tree)
{
    if (tree == apvts.state)
        updatePackedOrder();
}

void BassRigProcessor::updatePackedOrder()
{
    const auto count = (int) chain.size();

    std::vector<int> order;

    const auto text = apvts.state.getProperty (chainOrderProperty).toString();

    for (const auto& piece : juce::StringArray::fromTokens (text, ",", {}))
        if (piece.trim().isNotEmpty())
            order.push_back (piece.getIntValue());

    // Anything that is not a clean permutation is discarded rather than patched
    // up: a half-valid order would silently drop or double a pedal, which is
    // far harder to notice than simply reverting to the default.
    auto valid = ((int) order.size() == count);

    if (valid)
    {
        std::vector<bool> seen ((size_t) count, false);

        for (const auto index : order)
        {
            if (! juce::isPositiveAndBelow (index, count) || seen[(size_t) index])
            {
                valid = false;
                break;
            }

            seen[(size_t) index] = true;
        }
    }

    if (! valid)
    {
        order.resize ((size_t) count);
        std::iota (order.begin(), order.end(), 0);
    }

    juce::uint32 packed = 0;

    for (int slot = 0; slot < count; ++slot)
        packed |= (juce::uint32) (order[(size_t) slot] & 0xF) << (slot * 4);

    packedOrder.store (packed, std::memory_order_release);
}

const Pedal* BassRigProcessor::findPedal (const char* name) const
{
    for (const auto& pedal : chain)
        if (juce::String (pedal->getName()) == name)
            return pedal.get();

    return nullptr;
}

juce::AudioProcessorEditor* BassRigProcessor::createEditor()
{
    return new BassRigEditor (*this);
}

int BassRigProcessor::getNumPrograms()
{
    return juce::jmax (1, presetManager.getNumPresets());
}

int BassRigProcessor::getCurrentProgram()
{
    return juce::jmax (0, presetManager.getCurrentPresetIndex());
}

void BassRigProcessor::setCurrentProgram (int index)
{
    if (juce::isPositiveAndBelow (index, presetManager.getNumPresets()))
        presetManager.loadPresetFromIndex (index);
}

const juce::String BassRigProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, presetManager.getNumPresets()))
        return presetManager.getPresetName (index);

    return {};
}

void BassRigProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    auto xml = state.createXml();

    if (xml == nullptr)
        return;

    // Which preset is loaded, and whether it has been edited, is part of the
    // session: reopening a project should not silently claim the user is still
    // on a factory preset they have since moved away from.
    if (auto presetState = presetManager.saveXmlState())
        xml->addChildElement (presetState.release());

    copyXmlToBinary (*xml, destData);
}

void BassRigProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    std::unique_ptr<juce::XmlElement> presetState;

    if (auto* child = xml->getChildByName (chowdsp::PresetManager::presetStateTag))
        presetState = std::make_unique<juce::XmlElement> (*child);

    // The preset node is not a parameter, so it must come out before the tree
    // is handed to the APVTS.
    xml->removeChildElement (xml->getChildByName (chowdsp::PresetManager::presetStateTag), true);

    // Order matters and is easy to get backwards. loadXmlState does not merely
    // record which preset was selected -- it calls loadPreset, which writes that
    // preset's values over every parameter. Restoring the saved parameters first
    // and the preset second therefore throws the saved values away, silently:
    // a session reopened after editing a preset came back on the unedited
    // preset. The saved parameters are the truth, so they land last.
    if (presetState != nullptr)
        presetManager.loadXmlState (presetState.get());

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // The redirect callback above picks the new order up; this is belt and
    // braces for the case where the tree object happens not to change.
    updatePackedOrder();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BassRigProcessor();
}
