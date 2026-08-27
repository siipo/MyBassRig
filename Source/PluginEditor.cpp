#include "PluginEditor.h"

namespace
{
    constexpr int margin = 22;

    // Screw heads in the corners, the small detail that makes an enclosure read
    // as an enclosure.
    void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius)
    {
        g.setColour (PedalTheme::enclosureEdge);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));

        g.setColour (PedalTheme::knobCap.withAlpha (0.55f));
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 1.0f);

        g.setColour (PedalTheme::enclosureEdge.brighter (0.35f));
        g.drawLine (centre.x - radius * 0.6f, centre.y - radius * 0.6f,
                    centre.x + radius * 0.6f, centre.y + radius * 0.6f, 1.4f);
    }
}

//==============================================================================
BassRigEditor::Face::Face (BassRigProcessor& p)
    : proc (p),
      chainStrip (p.apvts, { { "GATE", ParamID::gateOn },
                             { "COMP", ParamID::compOn },
                             { "OCT", ParamID::octOn },
                             { "FILTER", ParamID::envOn },
                             { "PHASER", ParamID::phaserOn },
                             { "CHORUS", ParamID::chorusOn },
                             { "DRIVE", {} } }),
      rack (p.apvts, dynamic_cast<const CompressorPedal*> (p.findPedal ("Compressor"))),
      bass   (p.apvts, ParamID::bass,   "BASS",   KnobControl::Arc::fromDefault),
      loMid  (p.apvts, ParamID::loMid,  "LO MID", KnobControl::Arc::fromDefault),
      hiMid  (p.apvts, ParamID::hiMid,  "HI MID", KnobControl::Arc::fromDefault),
      treble (p.apvts, ParamID::treble, "TREBLE", KnobControl::Arc::fromDefault),
      drive  (p.apvts, ParamID::drive,  "DRIVE"),
      blend  (p.apvts, ParamID::blend,  "BLEND"),
      level  (p.apvts, ParamID::level,  "LEVEL",  KnobControl::Arc::fromDefault),
      master (p.apvts, ParamID::master, "MASTER", KnobControl::Arc::fromDefault),
      trim   (p.apvts, ParamID::trim,   "TRIM",   KnobControl::Arc::fromDefault),
      loMidFreq (p.apvts, ParamID::loMidFreq, "LO FREQ"),
      hiMidFreq (p.apvts, ParamID::hiMidFreq, "HI FREQ"),
      attack    (p.apvts, ParamID::attack,    "ATTACK"),
      grunt     (p.apvts, ParamID::grunt,     "GRUNT"),
      footswitch (p.apvts, ParamID::bypass),
      presetBar (p.presetManager)
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &bass, &loMid, &hiMid, &treble, &drive, &blend, &level, &master, &trim,
             &loMidFreq, &hiMidFreq, &attack, &grunt, &footswitch, &presetBar, &rack,
             &chainStrip })
        addAndMakeVisible (c);

    // Dragging a chip rewrites the order the processor runs its pedals in.
    chainStrip.setOrder (proc.getChainOrder());
    chainStrip.onOrderChanged = [this] (const std::vector<int>& order)
    {
        proc.setChainOrder (order);
    };
}

void BassRigEditor::Face::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Enclosure.
    g.setGradientFill (juce::ColourGradient (PedalTheme::enclosure.brighter (0.12f), bounds.getCentreX(), 0.0f,
                                             PedalTheme::enclosure.darker (0.25f), bounds.getCentreX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 16.0f);

    g.setColour (PedalTheme::enclosureEdge);
    g.drawRoundedRectangle (bounds.reduced (0.75f), 16.0f, 1.5f);

    // Header plate.
    auto header = bounds.reduced ((float) margin, 0.0f)
                        .withTop (14.0f).withHeight (58.0f);

    g.setColour (PedalTheme::plate);
    g.fillRoundedRectangle (header, 8.0f);
    g.setColour (PedalTheme::accentDim);
    g.drawRoundedRectangle (header, 8.0f, 1.0f);

    auto headerText = header.toNearestInt().reduced (12, 6);
    g.setFont (PedalTheme::titleFont (26.0f));
    g.setColour (PedalTheme::accent);
    g.drawText ("BASSRIG", headerText.removeFromTop (30), juce::Justification::centredLeft, false);

    g.setFont (PedalTheme::labelFont (11.0f));
    g.setColour (PedalTheme::textDim);
    g.drawText ("COMP  /  PREAMP  /  DRIVE", headerText, juce::Justification::centredLeft, false);

    // One divider above each section, so the signal order reads top to bottom.
    g.setFont (PedalTheme::labelFont (9.0f));

    const auto section = [&] (const char* label, int y, bool rule)
    {
        if (rule)
        {
            g.setColour (PedalTheme::enclosureEdge.withAlpha (0.8f));
            g.drawLine (bounds.getX() + margin, (float) y - 8.0f,
                        bounds.getRight() - margin, (float) y - 8.0f, 1.0f);
        }

        g.setColour (PedalTheme::textDim.withAlpha (0.8f));
        g.drawText (label, juce::Rectangle<int> (margin, y, 160, 12),
                    juce::Justification::centredLeft, false);
    };

    section ("SIGNAL CHAIN", 84, false);
    section ("EQUALISER", 336, true);
    section ("DRIVE", 538, true);

    const auto screwInset = 13.0f;
    for (auto corner : { bounds.getTopLeft(), bounds.getTopRight(),
                         bounds.getBottomLeft(), bounds.getBottomRight() })
        drawScrew (g, { corner.x + (corner.x < bounds.getCentreX() ? screwInset : -screwInset),
                        corner.y + (corner.y < bounds.getCentreY() ? screwInset : -screwInset) },
                   5.0f);
}

void BassRigEditor::Face::resized()
{
    constexpr int knobSize = 86;
    constexpr int switchHeight = 66;

    const auto area = getLocalBounds().reduced (margin, 0);
    const auto columnWidth = area.getWidth() / 4;

    const auto column = [&] (int index, int y, int height, int width = 0)
    {
        const auto w = width > 0 ? width : columnWidth;
        return juce::Rectangle<int> (area.getX() + columnWidth * index, y, w, height).reduced (4, 0);
    };

    // One rack pedal at a time, so the enclosure does not grow with the number
    // of effects in it.
    // The chain in signal order, draggable, above the rack it orders.
    chainStrip.setBounds (area.getX(), 98, area.getWidth(), 30);

    // Tab strip, a gap, then the tallest panel: 26 + 6 + 142. Sized to the
    // panel rather than guessed, because guessing clipped the compressor.
    rack.setBounds (area.getX(), 140, area.getWidth(),
                    RackView::tabStripHeight + 6 + 142);

    KnobControl* eqKnobs[] { &bass, &loMid, &hiMid, &treble };

    for (int i = 0; i < 4; ++i)
        eqKnobs[i]->setBounds (column (i, 352, knobSize));

    // Frequency selectors sit under the two mid knobs they belong to.
    loMidFreq.setBounds (column (1, 450, switchHeight));
    hiMidFreq.setBounds (column (2, 450, switchHeight));

    KnobControl* driveKnobs[] { &drive, &blend, &level, &master };

    for (int i = 0; i < 4; ++i)
        driveKnobs[i]->setBounds (column (i, 554, knobSize));

    // Voicing switches and input trim, evenly across three columns rather than
    // bunched to one side with a hole in the middle.
    const auto thirdWidth = area.getWidth() / 3;
    const auto third = [&] (int index, int y, int height)
    {
        return juce::Rectangle<int> (area.getX() + thirdWidth * index, y, thirdWidth, height).reduced (6, 0);
    };

    attack.setBounds (third (0, 656, switchHeight));
    grunt.setBounds  (third (1, 656, switchHeight));
    trim.setBounds   (third (2, 650, knobSize));

    // The preset strip sits between the controls and the footswitch, where a
    // pedal would carry its model name.
    presetBar.setBounds (area.getX(), 750, area.getWidth(), 30);

    footswitch.setBounds (getLocalBounds().withTop (getHeight() - 148).reduced (0, 14));
}

//==============================================================================
BassRigEditor::BassRigEditor (BassRigProcessor& p)
    : AudioProcessorEditor (&p), face (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (face);

    setResizable (true, true);
    setResizeLimits (designWidth / 2, designHeight / 2, designWidth * 2, designHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    setSize (designWidth, designHeight);
}

BassRigEditor::~BassRigEditor()
{
    setLookAndFeel (nullptr);
}

void BassRigEditor::paint (juce::Graphics& g)
{
    // Shows only in the sliver the fixed aspect ratio cannot fill.
    g.fillAll (PedalTheme::enclosureEdge);
}

void BassRigEditor::resized()
{
    const auto scale = juce::jmin ((float) getWidth()  / (float) designWidth,
                                   (float) getHeight() / (float) designHeight);

    face.setBounds (0, 0, designWidth, designHeight);
    face.setTransform (juce::AffineTransform::scale (scale)
                           .translated (((float) getWidth()  - designWidth  * scale) * 0.5f,
                                        ((float) getHeight() - designHeight * scale) * 0.5f));
}
