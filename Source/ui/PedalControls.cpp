#include "PedalControls.h"

namespace
{
    constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.25f;
    constexpr float rotaryEnd   = juce::MathConstants<float>::pi * 2.75f;

    // A dark line under the text reads as engraving on a painted enclosure.
    void drawEngravedText (juce::Graphics& g, const juce::String& text,
                           juce::Rectangle<int> area, float fontHeight,
                           juce::Colour colour = PedalTheme::text)
    {
        g.setFont (PedalTheme::labelFont (fontHeight));

        g.setColour (PedalTheme::enclosureEdge.withAlpha (0.7f));
        g.drawText (text, area.translated (0, 1), juce::Justification::centred, false);

        g.setColour (colour);
        g.drawText (text, area, juce::Justification::centred, false);
    }
}

//==============================================================================
KnobControl::KnobControl (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                          const juce::String& captionText, Arc arc)
    : caption (captionText), attachment (state, paramID, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters (rotaryStart, rotaryEnd, true);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 15);

    if (auto* param = state.getParameter (paramID))
    {
        const auto defaultValue = param->getDefaultValue();   // already 0..1
        slider.getProperties().set ("arcOrigin",
                                    arc == Arc::fromDefault ? defaultValue : 0.0f);

        // Double-click returns a knob to the position it ships at, which for
        // the EQ and gain controls is the neutral one.
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (defaultValue));
    }

    addAndMakeVisible (slider);
}

void KnobControl::paint (juce::Graphics& g)
{
    drawEngravedText (g, caption, getLocalBounds().removeFromTop (14), 11.0f, PedalTheme::textDim);
}

void KnobControl::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (16);
    slider.setBounds (area);
}

//==============================================================================
ThreeWaySwitch::ThreeWaySwitch (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                                const juce::String& captionText)
    : caption (captionText)
{
    auto* param = state.getParameter (paramID);
    jassert (param != nullptr);

    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (param))
        choices = choiceParam->choices;

    attachment = std::make_unique<juce::ParameterAttachment> (
        *param,
        [this] (float value)
        {
            index = juce::jlimit (0, juce::jmax (0, choices.size() - 1), juce::roundToInt (value));
            repaint();
        });

    attachment->sendInitialUpdate();
}

void ThreeWaySwitch::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    drawEngravedText (g, caption, area.removeFromTop (13), 10.0f, PedalTheme::textDim);

    auto body = area.reduced (0, 2);
    auto labels = body.removeFromRight (juce::jmin (48, body.getWidth() / 2));
    const auto track = body.removeFromRight (juce::jmin (14, body.getWidth())).toFloat();

    g.setColour (PedalTheme::enclosureEdge);
    g.fillRoundedRectangle (track, track.getWidth() * 0.5f);

    const auto count = juce::jmax (1, choices.size());
    const auto slot = track.getHeight() / (float) count;

    for (int i = 0; i < count; ++i)
    {
        const auto row = juce::Rectangle<float> (track.getX(), track.getY() + slot * (float) i,
                                                 track.getWidth(), slot);
        const auto lit = (i == index);

        if (lit)
        {
            g.setColour (PedalTheme::accent);
            g.fillEllipse (row.withSizeKeepingCentre (track.getWidth() - 4.0f,
                                                      juce::jmin (slot - 3.0f, track.getWidth() - 4.0f)));
        }

        const auto labelRow = juce::Rectangle<int> (labels.getX() + 4, juce::roundToInt (row.getY()),
                                                    labels.getWidth() - 4, juce::roundToInt (slot));

        g.setFont (PedalTheme::labelFont (9.0f));
        g.setColour (lit ? PedalTheme::text : PedalTheme::textDim.withAlpha (0.55f));
        g.drawText (choices[i], labelRow, juce::Justification::centredLeft, false);
    }
}

void ThreeWaySwitch::mouseDown (const juce::MouseEvent& e)
{
    auto area = getLocalBounds();
    area.removeFromTop (13);
    const auto body = area.reduced (0, 2);

    if (body.getHeight() <= 0 || choices.isEmpty())
        return;

    const auto relative = (float) (e.getPosition().y - body.getY()) / (float) body.getHeight();
    const auto wanted = juce::jlimit (0, choices.size() - 1,
                                      (int) (relative * (float) choices.size()));

    if (wanted != index)
        attachment->setValueAsCompleteGesture ((float) wanted);
}

//==============================================================================
SectionToggle::SectionToggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                              const juce::String& captionText)
    : caption (captionText)
{
    auto* param = state.getParameter (paramID);
    jassert (param != nullptr);

    attachment = std::make_unique<juce::ParameterAttachment> (
        *param,
        [this] (float value)
        {
            on = value > 0.5f;
            repaint();
        });

    attachment->sendInitialUpdate();
}

void SectionToggle::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    auto lamp = area.removeFromLeft (area.getHeight()).toFloat().reduced (3.0f);

    g.setColour (PedalTheme::enclosureEdge);
    g.fillEllipse (lamp);

    if (on)
    {
        g.setColour (PedalTheme::accent.withAlpha (0.3f));
        g.fillEllipse (lamp.expanded (2.5f));
    }

    g.setColour (on ? PedalTheme::accent : PedalTheme::knobBody);
    g.fillEllipse (lamp.reduced (2.0f));

    g.setFont (PedalTheme::labelFont (10.0f));
    g.setColour (on ? PedalTheme::text : PedalTheme::textDim);
    g.drawText (caption, area.reduced (4, 0), juce::Justification::centredLeft, false);
}

void SectionToggle::mouseDown (const juce::MouseEvent&)
{
    attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

//==============================================================================
Footswitch::Footswitch (juce::AudioProcessorValueTreeState& state, const juce::String& paramID)
{
    auto* param = state.getParameter (paramID);
    jassert (param != nullptr);

    attachment = std::make_unique<juce::ParameterAttachment> (
        *param,
        [this] (float value)
        {
            bypassed = value > 0.5f;
            repaint();
        });

    attachment->sendInitialUpdate();
}

void Footswitch::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    auto ledArea = area.removeFromTop (area.getHeight() / 3).toFloat();
    const auto ledSize = juce::jmin (ledArea.getWidth(), ledArea.getHeight()) * 0.5f;
    const auto led = juce::Rectangle<float> (ledSize, ledSize).withCentre (ledArea.getCentre());

    if (! bypassed)
    {
        g.setColour (PedalTheme::ledOn.withAlpha (0.25f));
        g.fillEllipse (led.expanded (ledSize * 0.8f));
    }

    g.setColour (bypassed ? PedalTheme::ledOff : PedalTheme::ledOn);
    g.fillEllipse (led);
    g.setColour (PedalTheme::enclosureEdge);
    g.drawEllipse (led, 1.5f);

    const auto size = juce::jmin (area.getWidth(), area.getHeight()) * 0.8f;
    const auto ring = juce::Rectangle<float> (size, size).withCentre (area.toFloat().getCentre());

    g.setColour (PedalTheme::enclosureEdge);
    g.fillEllipse (ring);

    g.setGradientFill (juce::ColourGradient (PedalTheme::knobCap, ring.getCentreX(), ring.getY(),
                                             PedalTheme::knobBody, ring.getCentreX(), ring.getBottom(), false));
    g.fillEllipse (ring.reduced (size * 0.14f));

    g.setColour (PedalTheme::knobEdge);
    g.drawEllipse (ring.reduced (size * 0.14f), 1.5f);
}

void Footswitch::mouseDown (const juce::MouseEvent&)
{
    attachment->setValueAsCompleteGesture (bypassed ? 0.0f : 1.0f);
}
