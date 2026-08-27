#include "GainReductionMeter.h"

GainReductionMeter::GainReductionMeter (const CompressorPedal* pedal)
    : compressor (pedal)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void GainReductionMeter::timerCallback()
{
    if (compressor == nullptr)
        return;

    const auto current = compressor->getGainReductionDb();

    // Snap down to a new reduction, fall back slowly. A meter that tracks the
    // release exactly is unreadable at bass release times.
    displayed = current < displayed ? current
                                    : displayed + (current - displayed) * 0.25f;

    repaint();
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    auto caption = bounds.removeFromTop (13);

    g.setFont (PedalTheme::labelFont (10.0f));
    g.setColour (PedalTheme::textDim);
    g.drawText ("GAIN REDUCTION", caption, juce::Justification::centredLeft, false);

    auto track = bounds.reduced (0, 3).toFloat();

    g.setColour (PedalTheme::enclosureEdge);
    g.fillRoundedRectangle (track, 3.0f);

    const auto proportion = juce::jlimit (0.0f, 1.0f, displayed / floorDb);

    if (proportion > 0.001f)
    {
        // Drawn from the right, because gain reduction is level being taken
        // away rather than added.
        auto filled = track.withTrimmedLeft (track.getWidth() * (1.0f - proportion));

        g.setColour (PedalTheme::accent);
        g.fillRoundedRectangle (filled, 3.0f);
    }

    // Scale ticks every 5 dB, so the bar reads as a measurement.
    g.setColour (PedalTheme::textDim.withAlpha (0.35f));

    for (int db = 5; db < (int) -floorDb; db += 5)
    {
        const auto x = track.getX() + track.getWidth() * (1.0f - (float) db / -floorDb);
        g.drawLine (x, track.getY() + 1.0f, x, track.getBottom() - 1.0f, 1.0f);
    }

    g.setFont (PedalTheme::labelFont (10.0f));
    g.setColour (displayed < -0.1f ? PedalTheme::text : PedalTheme::textDim);
    g.drawText (juce::String (displayed, 1) + " dB", bounds, juce::Justification::centredRight, false);
}
