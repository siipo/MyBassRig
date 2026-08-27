#pragma once

#include "PedalLookAndFeel.h"

#include <chowdsp_presets/chowdsp_presets.h>

// The preset strip on the pedal face.
//
// chowdsp ships a ready-made presets component, and this deliberately does not
// use it: the backend logic is worth borrowing, the visual identity is not.
// Everything below draws in the pedal theme.
class PresetBar final : public juce::Component,
                        private chowdsp::PresetManager::Listener
{
public:
    explicit PresetBar (chowdsp::PresetManager& manager);
    ~PresetBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void presetListUpdated() override        { refresh(); }
    void presetDirtyStatusChanged() override { refresh(); }
    void selectedPresetChanged() override    { refresh(); }

    void refresh();
    void step (int direction);
    void promptToSave();

    chowdsp::PresetManager& presets;

    juce::DrawableButton previous { "previous", juce::DrawableButton::ImageFitted };
    juce::DrawableButton next     { "next",     juce::DrawableButton::ImageFitted };
    juce::TextButton save        { "SAVE" };
    juce::Label name;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};
