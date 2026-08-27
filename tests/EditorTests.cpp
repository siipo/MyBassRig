#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include "ui/RackView.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
    struct EditorFixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        BassRigProcessor proc;
    };

    juce::Image render (BassRigProcessor& proc, int width, int height)
    {
        BassRigEditor editor { proc };
        editor.setSize (width, height);

        juce::Image image { juce::Image::ARGB, width, height, true };
        juce::Graphics g { image };
        editor.paintEntireComponent (g, true);

        return image;
    }

    int countDifferingPixels (const juce::Image& a, const juce::Image& b)
    {
        jassert (a.getWidth() == b.getWidth() && a.getHeight() == b.getHeight());

        int differing = 0;

        for (int y = 0; y < a.getHeight(); y += 2)
            for (int x = 0; x < a.getWidth(); x += 2)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    ++differing;

        return differing;
    }

    void setParam (BassRigProcessor& proc, const char* id, float value)
    {
        auto* p = proc.apvts.getParameter (id);
        jassert (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }
}

TEST_CASE ("editor builds and draws something", "[ui]")
{
    EditorFixture f;
    const auto image = render (f.proc, BassRigEditor::designWidth, BassRigEditor::designHeight);

    // Not a blank canvas: at least the enclosure, plate and knobs painted.
    int distinctish = 0;
    const auto reference = image.getPixelAt (0, 0);

    for (int y = 0; y < image.getHeight(); y += 4)
        for (int x = 0; x < image.getWidth(); x += 4)
            if (image.getPixelAt (x, y) != reference)
                ++distinctish;

    INFO ("non-background samples: " << distinctish);
    REQUIRE (distinctish > 1000);
}

// The point of this one is that the face is bound to the parameters rather than
// being a static drawing. If an attachment is dropped in a refactor, the pixels
// stop responding and this fails.
TEST_CASE ("controls redraw when their parameters change", "[ui]")
{
    EditorFixture f;

    const auto neutral = render (f.proc, BassRigEditor::designWidth, BassRigEditor::designHeight);

    struct Case { const char* id; float value; const char* what; };

    const Case cases[]
    {
        { ParamID::drive,     0.95f, "drive knob" },
        { ParamID::blend,     0.05f, "blend knob" },
        { ParamID::bass,      1.00f, "bass knob" },
        { ParamID::treble,    0.00f, "treble knob" },
        { ParamID::master,   -18.0f, "master knob" },
        { ParamID::attack,     0.0f, "attack switch" },
        { ParamID::grunt,      2.0f, "grunt switch" },
        { ParamID::loMidFreq,  2.0f, "lo-mid frequency switch" },
        { ParamID::bypass,     1.0f, "bypass LED" },
    };

    for (const auto& c : cases)
    {
        BassRigProcessor fresh;
        setParam (fresh, c.id, c.value);

        const auto changed = render (fresh, BassRigEditor::designWidth, BassRigEditor::designHeight);
        const auto differing = countDifferingPixels (neutral, changed);

        INFO (c.what << " (" << c.id << "): " << differing << " differing samples");
        REQUIRE (differing > 20);
    }
}

namespace
{
    RackView* findRack (juce::Component& component)
    {
        if (auto* rack = dynamic_cast<RackView*> (&component))
            return rack;

        for (auto* child : component.getChildren())
            if (auto* found = findRack (*child))
                return found;

        return nullptr;
    }

    juce::Image renderRackTab (BassRigProcessor& proc, int tab)
    {
        BassRigEditor editor { proc };
        editor.setSize (BassRigEditor::designWidth, BassRigEditor::designHeight);

        auto* rack = findRack (editor);
        jassert (rack != nullptr);
        rack->select (tab);

        juce::Image image { juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true };
        juce::Graphics g { image };
        editor.paintEntireComponent (g, true);

        return image;
    }
}

// The rack exists so the enclosure does not grow with the number of effects in
// it, which means most rack pedals are hidden behind an unselected tab at any
// moment. A panel that silently stopped drawing would be easy to miss.
TEST_CASE ("every rack tab shows a different panel", "[ui][rack]")
{
    EditorFixture f;

    BassRigEditor probe { f.proc };
    probe.setSize (BassRigEditor::designWidth, BassRigEditor::designHeight);
    auto* rack = findRack (probe);

    REQUIRE (rack != nullptr);
    REQUIRE (rack->getNumTabs() >= 2);

    const auto first = renderRackTab (f.proc, 0);

    for (int tab = 1; tab < rack->getNumTabs(); ++tab)
    {
        const auto other = renderRackTab (f.proc, tab);
        const auto differing = countDifferingPixels (first, other);

        INFO ("tab " << tab << " against tab 0: " << differing << " differing samples");
        REQUIRE (differing > 200);
    }
}

// Same guard as for the always-visible controls, but for the ones behind a tab.
TEST_CASE ("rack controls are bound to their parameters", "[ui][rack]")
{
    EditorFixture f;
    const auto neutral = renderRackTab (f.proc, 1);   // envelope filter

    struct Case { const char* id; float value; const char* what; };

    const Case cases[]
    {
        { ParamID::envSens,     0.95f, "sensitivity knob" },
        { ParamID::envQ,        11.0f, "Q knob" },
        { ParamID::envRange,     5.0f, "range knob" },
        { ParamID::envDryHighs,  0.9f, "dry highs knob" },
        { ParamID::envMode,      2.0f, "mode switch" },
        { ParamID::envOn,        1.0f, "filter on lamp" },
    };

    for (const auto& c : cases)
    {
        BassRigProcessor fresh;
        setParam (fresh, c.id, c.value);

        const auto changed = renderRackTab (fresh, 1);
        const auto differing = countDifferingPixels (neutral, changed);

        INFO (c.what << " (" << c.id << "): " << differing << " differing samples");
        REQUIRE (differing > 10);
    }
}

TEST_CASE ("face keeps its proportions when resized", "[ui]")
{
    EditorFixture f;
    BassRigEditor editor { f.proc };

    REQUIRE (editor.getWidth() == BassRigEditor::designWidth);
    REQUIRE (editor.getHeight() == BassRigEditor::designHeight);

    // Scaling rather than reflowing: a pedal's control positions carry meaning,
    // so the layout must not rearrange itself at another size.
    for (const auto scale : { 0.6f, 1.0f, 1.75f })
    {
        const auto w = juce::roundToInt (BassRigEditor::designWidth * scale);
        const auto h = juce::roundToInt (BassRigEditor::designHeight * scale);

        editor.setSize (w, h);
        REQUIRE (editor.getWidth() == w);

        const auto image = render (f.proc, w, h);
        REQUIRE (image.getWidth() == w);
        REQUIRE (image.getHeight() == h);
    }
}
