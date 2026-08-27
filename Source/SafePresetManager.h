#pragma once

#include <chowdsp_presets/chowdsp_presets.h>
#include <juce_audio_processors/juce_audio_processors.h>

// chowdsp::PresetManager, with its dirty flag moved off the audio thread.
//
// The manager registers itself against every parameter, and on any change calls
// setIsDirty, which fans the notification out through a juce::ListenerList
// built with juce::DummyCriticalSection -- a list that does no locking at all.
//
// The VST3 wrapper delivers automation on the AUDIO thread:
//
//     JuceVST3Component::process
//       -> processParameterChanges
//         -> AudioProcessorParameter::setValueNotifyingHost
//           -> APVTS::ParameterAdapter::parameterValueChanged
//             -> PresetManager::parameterChanged
//               -> PresetManager::setIsDirty
//                 -> ListenerList::call
//
// so with a host also moving parameters from the message thread, two threads
// push onto that list's internal iterator vector at once and corrupt the heap.
// That is the crash recorded in DESIGN.md 3s. It is narrow -- parameterChanged
// only calls setIsDirty on the clean-to-dirty transition -- which is why it
// showed up roughly one pluginval run in eight rather than every time.
//
// The obvious fix, unregistering the manager and listening in its place, does
// not compile: PresetManager inherits AudioProcessorValueTreeState::Listener
// PRIVATELY, so it cannot be passed to removeParameterListener. But
// parameterChanged is virtual, and overriding a private virtual is legal and
// dispatches normally -- so the registration stays where it is and simply
// arrives here instead.
//
// This is an upstream bug rather than one of ours. Worth reporting; until then
// this is the containment.
class SafePresetManager : public chowdsp::PresetManager,
                          private juce::AsyncUpdater,
                          private chowdsp::PresetManager::Listener
{
public:
    explicit SafePresetManager (juce::AudioProcessorValueTreeState& state)
        : chowdsp::PresetManager (state)
    {
        addListener (this);
    }

    ~SafePresetManager() override
    {
        removeListener (this);
        cancelPendingUpdate();
    }

private:
    // Called on whatever thread moved the parameter, the audio thread included.
    //
    // On the message thread this behaves exactly as the original did, marking
    // dirty there and then. Only a change arriving from somewhere else is
    // deferred, which keeps the fix confined to the case that was broken: the
    // UI and anything else on the message thread see no change in timing at
    // all, and neither do the tests.
    void parameterChanged (const juce::String&, float) override
    {
        if (getIsDirty())
            return;

        if (juce::MessageManager::existsAndIsCurrentThread())
            setIsDirty (true);
        else
            triggerAsyncUpdate();
    }

    // Message thread, always. This is the only place the dirty flag is raised.
    void handleAsyncUpdate() override
    {
        if (! getIsDirty())
            setIsDirty (true);
    }

    // Loading a preset finishes by clearing the flag. Without this, a parameter
    // change posted while the preset was still loading would land afterwards
    // and dirty a preset that had just been loaded clean -- a fault the
    // synchronous original could not have, because its notification arrived
    // before loadPreset finished rather than after.
    void presetDirtyStatusChanged() override
    {
        if (! getIsDirty())
            cancelPendingUpdate();
    }

    void selectedPresetChanged() override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SafePresetManager)
};
