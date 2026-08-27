#include "RackView.h"

namespace
{
    constexpr int knobRowHeight = 76;
    constexpr int lowerRowY = 82;
    constexpr int lowerRowHeight = 60;

    // Lays out n knobs evenly across the full width.
    juce::Rectangle<int> knobColumn (juce::Rectangle<int> area, int index, int count)
    {
        const auto width = area.getWidth() / count;
        return juce::Rectangle<int> (area.getX() + width * index, area.getY(),
                                     width, knobRowHeight).reduced (2, 0);
    }
}

//==============================================================================
RackView::CompressorPanel::CompressorPanel (juce::AudioProcessorValueTreeState& state,
                                            const CompressorPedal* compressor)
    : threshold (state, ParamID::compThreshold, "THRESH"),
      ratio     (state, ParamID::compRatio,     "RATIO"),
      attack    (state, ParamID::compAttack,    "ATTACK"),
      release   (state, ParamID::compRelease,   "RELEASE"),
      makeup    (state, ParamID::compMakeup,    "MAKEUP"),
      sidechain (state, ParamID::compSidechain, "SIDECHAIN"),
      on        (state, ParamID::compOn,        "COMP ON"),
      meter     (compressor)
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &threshold, &ratio, &attack, &release, &makeup, &sidechain, &on, &meter })
        addAndMakeVisible (c);
}

void RackView::CompressorPanel::resized()
{
    auto area = getLocalBounds();

    KnobControl* knobs[] { &threshold, &ratio, &attack, &release, &makeup };

    for (int i = 0; i < 5; ++i)
        knobs[i]->setBounds (knobColumn (area, i, 5));

    on.setBounds        (area.getX() + 4,   lowerRowY + 14, 110, 20);
    sidechain.setBounds (area.getX() + 140, lowerRowY,      110, lowerRowHeight - 4);
    meter.setBounds     (area.getX() + 272, lowerRowY + 12, area.getWidth() - 276, 34);
}

//==============================================================================
RackView::EnvelopePanel::EnvelopePanel (juce::AudioProcessorValueTreeState& state)
    : sens     (state, ParamID::envSens,     "SENS"),
      attack   (state, ParamID::envAttack,   "ATTACK"),
      release  (state, ParamID::envRelease,  "RELEASE"),
      q        (state, ParamID::envQ,        "Q"),
      range    (state, ParamID::envRange,    "RANGE"),
      dryHighs (state, ParamID::envDryHighs, "DRY HI"),
      mode     (state, ParamID::envMode,     "MODE"),
      on       (state, ParamID::envOn,       "FILTER ON")
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &sens, &attack, &release, &q, &range, &dryHighs, &mode, &on })
        addAndMakeVisible (c);
}

void RackView::EnvelopePanel::resized()
{
    auto area = getLocalBounds();

    KnobControl* knobs[] { &sens, &attack, &release, &q, &range, &dryHighs };

    for (int i = 0; i < 6; ++i)
        knobs[i]->setBounds (knobColumn (area, i, 6));

    on.setBounds   (area.getX() + 4,   lowerRowY + 14, 110, 20);
    mode.setBounds (area.getX() + 140, lowerRowY,      110, lowerRowHeight - 4);
}

//==============================================================================
RackView::PhaserPanel::PhaserPanel (juce::AudioProcessorValueTreeState& state)
    : rate      (state, ParamID::phaserRate,      "RATE"),
      depth     (state, ParamID::phaserDepth,     "DEPTH"),
      feedback  (state, ParamID::phaserFeedback,  "FEEDBACK"),
      mix       (state, ParamID::phaserMix,       "MIX"),
      crossover (state, ParamID::phaserCrossover, "XOVER"),
      stages    (state, ParamID::phaserStages,    "STAGES"),
      on        (state, ParamID::phaserOn,        "PHASER ON"),
      invert    (state, ParamID::phaserInvert,    "INVERT")
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &rate, &depth, &feedback, &mix, &crossover, &stages, &on, &invert })
        addAndMakeVisible (c);
}

void RackView::PhaserPanel::resized()
{
    auto area = getLocalBounds();

    KnobControl* knobs[] { &rate, &depth, &feedback, &mix, &crossover };

    for (int i = 0; i < 5; ++i)
        knobs[i]->setBounds (knobColumn (area, i, 5));

    on.setBounds     (area.getX() + 4,   lowerRowY + 4,  110, 20);
    invert.setBounds (area.getX() + 4,   lowerRowY + 28, 110, 20);
    stages.setBounds (area.getX() + 140, lowerRowY,      110, lowerRowHeight - 4);
}

//==============================================================================
RackView::ChorusPanel::ChorusPanel (juce::AudioProcessorValueTreeState& state)
    : rate      (state, ParamID::chorusRate,      "RATE"),
      depth     (state, ParamID::chorusDepth,     "DEPTH"),
      mix       (state, ParamID::chorusMix,       "MIX"),
      crossover (state, ParamID::chorusCrossover, "XOVER"),
      mode      (state, ParamID::chorusMode,      "MODE"),
      on        (state, ParamID::chorusOn,        "CHORUS ON")
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &rate, &depth, &mix, &crossover, &mode, &on })
        addAndMakeVisible (c);
}

void RackView::ChorusPanel::resized()
{
    auto area = getLocalBounds();

    KnobControl* knobs[] { &rate, &depth, &mix, &crossover };

    for (int i = 0; i < 4; ++i)
        knobs[i]->setBounds (knobColumn (area, i, 4));

    on.setBounds   (area.getX() + 4,   lowerRowY + 14, 120, 20);
    mode.setBounds (area.getX() + 150, lowerRowY,      110, lowerRowHeight - 4);
}

//==============================================================================
RackView::GatePanel::GatePanel (juce::AudioProcessorValueTreeState& state)
    : threshold (state, ParamID::gateThreshold, "THRESH"),
      attack    (state, ParamID::gateAttack,    "ATTACK"),
      hold      (state, ParamID::gateHold,      "HOLD"),
      release   (state, ParamID::gateRelease,   "RELEASE"),
      range     (state, ParamID::gateRange,     "RANGE"),
      sidechain (state, ParamID::gateSidechain, "SIDECHAIN"),
      on        (state, ParamID::gateOn,        "GATE ON")
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &threshold, &attack, &hold, &release, &range, &sidechain, &on })
        addAndMakeVisible (c);
}

void RackView::GatePanel::resized()
{
    auto area = getLocalBounds();

    KnobControl* knobs[] { &threshold, &attack, &hold, &release, &range };

    for (int i = 0; i < 5; ++i)
        knobs[i]->setBounds (knobColumn (area, i, 5));

    on.setBounds        (area.getX() + 4,   lowerRowY + 14, 110, 20);
    sidechain.setBounds (area.getX() + 140, lowerRowY,      110, lowerRowHeight - 4);
}

//==============================================================================
RackView::OctaverPanel::OctaverPanel (juce::AudioProcessorValueTreeState& state)
    : direct (state, ParamID::octDirect, "DIRECT"),
      subOne (state, ParamID::octSubOne, "OCT 1"),
      subTwo (state, ParamID::octSubTwo, "OCT 2"),
      growl  (state, ParamID::octGrowl,  "GROWL"),
      tone   (state, ParamID::octTone,   "TONE"),
      track  (state, ParamID::octTrack,  "TRACKING"),
      on     (state, ParamID::octOn,     "OCTAVE ON")
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &direct, &subOne, &subTwo, &growl, &tone, &track, &on })
        addAndMakeVisible (c);
}

void RackView::OctaverPanel::resized()
{
    auto area = getLocalBounds();

    KnobControl* knobs[] { &direct, &subOne, &subTwo, &growl, &tone, &track };

    for (int i = 0; i < 6; ++i)
        knobs[i]->setBounds (knobColumn (area, i, 6));

    on.setBounds (area.getX() + 4, lowerRowY + 14, 120, 20);
}

//==============================================================================
RackView::RackView (juce::AudioProcessorValueTreeState& state, const CompressorPedal* compressor)
    : compressorPanel (state, compressor),
      envelopePanel (state),
      phaserPanel (state),
      chorusPanel (state),
      gatePanel (state),
      octaverPanel (state)
{
    addAndMakeVisible (compressorPanel);
    addAndMakeVisible (envelopePanel);
    addAndMakeVisible (phaserPanel);
    addAndMakeVisible (chorusPanel);
    addAndMakeVisible (gatePanel);
    addAndMakeVisible (octaverPanel);

    const auto addTab = [this, &state] (const juce::String& name, const char* onParamID,
                                        juce::Component* panel)
    {
        auto tab = std::make_unique<Tab>();
        tab->name = name;
        tab->onParamID = onParamID;
        tab->panel = panel;

        if (auto* param = state.getParameter (onParamID))
        {
            auto* raw = tab.get();
            tab->attachment = std::make_unique<juce::ParameterAttachment> (
                *param,
                [this, raw] (float value)
                {
                    raw->lit = value > 0.5f;
                    repaint();
                });
            tab->attachment->sendInitialUpdate();
        }

        tabs.push_back (std::move (tab));
    };

    // Adding a rack pedal is a panel class and one line here.
    addTab ("GATE", ParamID::gateOn, &gatePanel);
    addTab ("COMP", ParamID::compOn, &compressorPanel);
    addTab ("OCTAVE", ParamID::octOn, &octaverPanel);
    addTab ("FILTER", ParamID::envOn, &envelopePanel);
    addTab ("PHASER", ParamID::phaserOn, &phaserPanel);
    addTab ("CHORUS", ParamID::chorusOn, &chorusPanel);

    select (0);
}

void RackView::select (int index)
{
    selected = juce::jlimit (0, (int) tabs.size() - 1, index);

    for (int i = 0; i < (int) tabs.size(); ++i)
        tabs[(size_t) i]->panel->setVisible (i == selected);

    repaint();
}

void RackView::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < (int) tabs.size(); ++i)
        if (tabs[(size_t) i]->bounds.contains (e.getPosition()))
        {
            select (i);
            return;
        }
}

void RackView::paint (juce::Graphics& g)
{
    for (int i = 0; i < (int) tabs.size(); ++i)
    {
        const auto& tab = *tabs[(size_t) i];
        const auto active = (i == selected);
        auto bounds = tab.bounds.toFloat();

        g.setColour (active ? PedalTheme::plate : PedalTheme::enclosureEdge.withAlpha (0.55f));
        g.fillRoundedRectangle (bounds, 5.0f);

        if (active)
        {
            g.setColour (PedalTheme::accentDim);
            g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);
        }

        // The lamp reports whether that pedal is running, which is what makes
        // an unselected tab still informative. Fixed width for the same reason
        // as the chain strip: six tabs across the face do not leave room for a
        // lamp scaled to the row height.
        auto lamp = bounds.removeFromLeft (16.0f);
        const auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre (lamp.getCentre());

        g.setColour (tab.lit ? PedalTheme::accent : PedalTheme::knobBody);
        g.fillEllipse (dot);

        g.setFont (PedalTheme::labelFont (9.5f));
        g.setColour (active ? PedalTheme::text : PedalTheme::textDim);
        g.drawText (tab.name, bounds.toNearestInt(),
                    juce::Justification::centredLeft, false);
    }
}

void RackView::resized()
{
    auto area = getLocalBounds();
    auto strip = area.removeFromTop (tabStripHeight);

    if (! tabs.empty())
    {
        const auto width = strip.getWidth() / (int) tabs.size();

        for (int i = 0; i < (int) tabs.size(); ++i)
            tabs[(size_t) i]->bounds = juce::Rectangle<int> (strip.getX() + width * i, strip.getY(),
                                                             width, strip.getHeight()).reduced (2, 0);
    }

    area.removeFromTop (6);

    for (auto& tab : tabs)
        tab->panel->setBounds (area);
}
