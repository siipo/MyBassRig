#include "PedalLookAndFeel.h"

juce::Font PedalTheme::labelFont (float height)
{
    return juce::Font (juce::FontOptions (height).withStyle ("Bold"));
}

juce::Font PedalTheme::titleFont (float height)
{
    return juce::Font (juce::FontOptions (height).withStyle ("Bold"));
}

PedalLookAndFeel::PedalLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, PedalTheme::text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, PedalTheme::text);
    setColour (juce::TooltipWindow::backgroundColourId, PedalTheme::enclosureEdge);
    setColour (juce::TooltipWindow::textColourId, PedalTheme::text);
}

void PedalLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                         juce::Slider& slider)
{
    const auto arcOrigin = (float) slider.getProperties().getWithDefault ("arcOrigin", 0.0);
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const auto trackRadius = radius * 0.92f;
    const auto trackWidth  = radius * 0.13f;

    // Value arc, drawn behind the knob so the body sits on top of it.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (PedalTheme::enclosureEdge);
    g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    const auto originAngle = rotaryStartAngle + arcOrigin * (rotaryEndAngle - rotaryStartAngle);

    if (std::abs (angle - originAngle) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                             juce::jmin (originAngle, angle), juce::jmax (originAngle, angle), true);
        g.setColour (PedalTheme::accent);
        g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // A tick at the neutral point, so it is obvious where the knob returns to.
    if (arcOrigin > 0.01f)
    {
        juce::Path notch;
        notch.addRectangle (-1.0f, -trackRadius - trackWidth * 0.5f, 2.0f, trackWidth);
        notch.applyTransform (juce::AffineTransform::rotation (originAngle).translated (centre));
        g.setColour (PedalTheme::textDim);
        g.fillPath (notch);
    }

    const auto bodyRadius = radius * 0.70f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    // Slight vertical gradient so the knob reads as a physical cap rather than
    // a flat disc.
    g.setGradientFill (juce::ColourGradient (PedalTheme::knobCap, centre.x, body.getY(),
                                             PedalTheme::knobBody, centre.x, body.getBottom(), false));
    g.fillEllipse (body);

    g.setColour (PedalTheme::knobEdge);
    g.drawEllipse (body, juce::jmax (1.0f, radius * 0.06f));

    // Pointer.
    juce::Path pointer;
    const auto pointerWidth  = juce::jmax (1.6f, radius * 0.10f);
    const auto pointerLength = bodyRadius * 0.78f;
    pointer.addRoundedRectangle (-pointerWidth * 0.5f, -pointerLength,
                                 pointerWidth, pointerLength, pointerWidth * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));

    g.setColour (PedalTheme::pointer);
    g.fillPath (pointer);
}

juce::Label* PedalLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setJustificationType (juce::Justification::centred);
    label->setColour (juce::Label::outlineWhenEditingColourId, PedalTheme::accent);
    return label;
}

juce::Font PedalLookAndFeel::getLabelFont (juce::Label& label)
{
    return PedalTheme::labelFont (juce::jlimit (9.0f, 14.0f, label.getHeight() * 0.8f));
}
