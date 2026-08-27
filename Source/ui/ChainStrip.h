#pragma once

#include "PedalLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <vector>

// The signal chain, shown as a row of chips in order, dragged to rearrange.
//
// This is the visible half of the reordering feature: without it the order is a
// hidden property nobody would ever discover. Each chip carries a lamp for
// whether that pedal is switched on, so the strip reads as the state of the rig
// rather than only its wiring.
//
// Dragging commits nothing until the mouse is released, so a drag that changes
// its mind costs nothing -- and, more importantly, the audio thread is not
// handed a new order on every mouse move.
class ChainStrip final : public juce::Component
{
public:
    struct Entry
    {
        juce::String name;
        juce::String onParamID;
    };

    ChainStrip (juce::AudioProcessorValueTreeState& state, std::vector<Entry> entries);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Order is a list of pedal indices in the order they should run.
    void setOrder (const std::vector<int>& newOrder);
    std::vector<int> getOrder() const { return order; }

    std::function<void (const std::vector<int>&)> onOrderChanged;

private:
    int slotAt (int x) const;
    juce::Rectangle<int> slotBounds (int slot) const;
    void drawChip (juce::Graphics&, juce::Rectangle<int>, int pedalIndex, bool dragging) const;

    std::vector<Entry> pedals;
    std::vector<int> order;
    std::vector<bool> lit;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> attachments;

    int draggingSlot = -1;
    int dropSlot = -1;
    int dragOffsetX = 0;
    juce::Point<int> dragPosition;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainStrip)
};
