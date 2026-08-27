#include "PresetBar.h"

namespace
{
    std::unique_ptr<juce::Drawable> makeArrow (bool pointingRight, juce::Colour colour)
    {
        juce::Path path;

        if (pointingRight)
        {
            path.startNewSubPath (0.0f, 0.0f);
            path.lineTo (6.0f, 5.0f);
            path.lineTo (0.0f, 10.0f);
        }
        else
        {
            path.startNewSubPath (6.0f, 0.0f);
            path.lineTo (0.0f, 5.0f);
            path.lineTo (6.0f, 10.0f);
        }

        auto drawable = std::make_unique<juce::DrawablePath>();
        drawable->setPath (path);
        drawable->setFill (juce::FillType {});
        drawable->setStrokeFill (colour);
        drawable->setStrokeThickness (1.8f);

        return drawable;
    }
}

PresetBar::PresetBar (chowdsp::PresetManager& manager)
    : presets (manager)
{
    for (auto* button : { &previous, &next })
    {
        button->setColour (juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
        button->setColour (juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (button);
    }

    previous.setImages (makeArrow (false, PedalTheme::text).get());
    next.setImages (makeArrow (true, PedalTheme::text).get());

    previous.onClick = [this] { step (-1); };
    next.onClick     = [this] { step (1); };

    name.setJustificationType (juce::Justification::centred);
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);

    save.setColour (juce::TextButton::buttonColourId, PedalTheme::plate);
    save.setColour (juce::TextButton::textColourOffId, PedalTheme::textDim);
    save.onClick = [this] { promptToSave(); };
    addAndMakeVisible (save);

    presets.addListener (this);
    refresh();
}

PresetBar::~PresetBar()
{
    presets.removeListener (this);
}

void PresetBar::refresh()
{
    const auto* current = presets.getCurrentPreset();

    // A trailing asterisk is the usual shorthand for "edited since loaded", and
    // it matters here: without it a tweaked factory preset looks like the
    // factory preset, and saving over it would be a surprise.
    name.setText (current != nullptr ? current->getName() + (presets.getIsDirty() ? " *" : "")
                                     : juce::String ("No Preset"),
                  juce::dontSendNotification);

    name.setColour (juce::Label::textColourId,
                    presets.getIsDirty() ? PedalTheme::accent : PedalTheme::text);

    const auto canStep = presets.getNumPresets() > 1;
    previous.setEnabled (canStep);
    next.setEnabled (canStep);
}

void PresetBar::step (int direction)
{
    const auto count = presets.getNumPresets();

    if (count <= 0)
        return;

    const auto index = (presets.getCurrentPresetIndex() + direction + count) % count;
    presets.loadPresetFromIndex (index);
}

void PresetBar::promptToSave()
{
    chooser = std::make_unique<juce::FileChooser> ("Save preset",
                                                   presets.getUserPresetPath(),
                                                   "*.preset");

    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file != juce::File{})
            presets.saveUserPreset (file.withFileExtension ("preset"));
    });
}

void PresetBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (PedalTheme::plate);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (PedalTheme::enclosureEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    name.setFont (PedalTheme::labelFont (12.0f));
    save.setColour (juce::TextButton::textColourOffId, PedalTheme::textDim);
}

void PresetBar::resized()
{
    auto area = getLocalBounds().reduced (4, 3);

    previous.setBounds (area.removeFromLeft (24));
    save.setBounds (area.removeFromRight (46).reduced (0, 1));
    next.setBounds (area.removeFromRight (24));
    name.setBounds (area);
}
