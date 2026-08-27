#include "ChainStrip.h"

#include <numeric>

ChainStrip::ChainStrip (juce::AudioProcessorValueTreeState& state, std::vector<Entry> entries)
    : pedals (std::move (entries))
{
    order.resize (pedals.size());
    std::iota (order.begin(), order.end(), 0);
    lit.assign (pedals.size(), false);

    for (size_t i = 0; i < pedals.size(); ++i)
    {
        if (auto* param = state.getParameter (pedals[i].onParamID))
        {
            auto attachment = std::make_unique<juce::ParameterAttachment> (
                *param,
                [this, i] (float value)
                {
                    lit[i] = value > 0.5f;
                    repaint();
                });

            attachment->sendInitialUpdate();
            attachments.push_back (std::move (attachment));
        }
        else
        {
            // The drive has no on/off of its own -- it is always in circuit --
            // so its chip is simply always lit.
            lit[i] = true;
        }
    }
}

void ChainStrip::setOrder (const std::vector<int>& newOrder)
{
    if (newOrder.size() != pedals.size())
        return;

    order = newOrder;
    repaint();
}

juce::Rectangle<int> ChainStrip::slotBounds (int slot) const
{
    const auto count = juce::jmax (1, (int) pedals.size());
    const auto width = getWidth() / count;

    return juce::Rectangle<int> (width * slot, 0, width, getHeight()).reduced (2, 0);
}

int ChainStrip::slotAt (int x) const
{
    const auto count = juce::jmax (1, (int) pedals.size());
    const auto width = juce::jmax (1, getWidth() / count);

    return juce::jlimit (0, count - 1, x / width);
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    draggingSlot = slotAt (e.getPosition().x);
    dropSlot = draggingSlot;
    dragPosition = e.getPosition();
    dragOffsetX = e.getPosition().x - slotBounds (draggingSlot).getX();
    repaint();
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingSlot < 0)
        return;

    dragPosition = e.getPosition();

    // The drop slot follows the centre of the chip being dragged, not the
    // cursor, so a chip lands where it looks like it will land.
    dropSlot = slotAt (juce::jlimit (0, getWidth() - 1,
                                     e.getPosition().x - dragOffsetX + slotBounds (0).getWidth() / 2));
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent&)
{
    if (draggingSlot >= 0 && dropSlot >= 0 && dropSlot != draggingSlot)
    {
        const auto moved = order[(size_t) draggingSlot];
        order.erase (order.begin() + draggingSlot);
        order.insert (order.begin() + dropSlot, moved);

        if (onOrderChanged != nullptr)
            onOrderChanged (order);
    }

    draggingSlot = -1;
    dropSlot = -1;
    repaint();
}

void ChainStrip::drawChip (juce::Graphics& g, juce::Rectangle<int> bounds, int pedalIndex,
                           bool dragging) const
{
    auto area = bounds.toFloat();

    g.setColour (dragging ? PedalTheme::plate.brighter (0.25f) : PedalTheme::plate);
    g.fillRoundedRectangle (area, 5.0f);

    g.setColour (dragging ? PedalTheme::accent : PedalTheme::enclosureEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

    // Fixed lamp width rather than one derived from the row height: with seven
    // chips across the face, a height-sized lamp left too little room and every
    // label was clipped.
    constexpr float lampColumn = 15.0f;

    auto lamp = area.removeFromLeft (lampColumn);
    const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre (lamp.getCentre());

    g.setColour (lit[(size_t) pedalIndex] ? PedalTheme::accent : PedalTheme::knobBody);
    g.fillEllipse (dot);

    g.setFont (PedalTheme::labelFont (9.0f));
    g.setColour (lit[(size_t) pedalIndex] ? PedalTheme::text : PedalTheme::textDim);
    g.drawText (pedals[(size_t) pedalIndex].name, area.toNearestInt(),
                juce::Justification::centredLeft, false);
}

void ChainStrip::paint (juce::Graphics& g)
{
    // While dragging, the other chips show where they will end up rather than
    // where they currently are.
    auto display = order;

    if (draggingSlot >= 0 && dropSlot >= 0 && dropSlot != draggingSlot)
    {
        const auto moved = display[(size_t) draggingSlot];
        display.erase (display.begin() + draggingSlot);
        display.insert (display.begin() + dropSlot, moved);
    }

    for (int slot = 0; slot < (int) display.size(); ++slot)
    {
        const auto pedalIndex = display[(size_t) slot];

        if (draggingSlot >= 0 && pedalIndex == order[(size_t) draggingSlot])
            continue;   // drawn last, following the cursor

        drawChip (g, slotBounds (slot), pedalIndex, false);

        // Arrows between the chips, so the strip reads as signal flow rather
        // than as a row of buttons.
        if (slot + 1 < (int) display.size())
        {
            const auto gap = slotBounds (slot).getRight();
            const auto y = (float) getHeight() * 0.5f;

            g.setColour (PedalTheme::textDim.withAlpha (0.5f));
            g.drawLine ((float) gap - 1.0f, y, (float) gap + 3.0f, y, 1.0f);
        }
    }

    if (draggingSlot >= 0)
    {
        auto bounds = slotBounds (draggingSlot)
                          .withX (juce::jlimit (0, getWidth() - slotBounds (0).getWidth(),
                                                dragPosition.x - dragOffsetX));
        drawChip (g, bounds, order[(size_t) draggingSlot], true);
    }
}

void ChainStrip::resized()
{
    repaint();
}
