// Renders the pedal face to a PNG without opening a window, so the UI can be
// looked at (and diffed) as part of an ordinary build rather than by launching
// the plugin in a host.
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "ui/RackView.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
    // The rack shows one pedal at a time, so a single render only ever captures
    // one of them. Walk the tree and drive the tab selection directly.
    RackView* findRack (juce::Component& component)
    {
        if (auto* rack = dynamic_cast<RackView*> (&component))
            return rack;

        for (auto* child : component.getChildren())
            if (auto* found = findRack (*child))
                return found;

        return nullptr;
    }

    void writeSnapshot (BassRigProcessor& proc, const juce::File& file, int width, int height,
                        int rackTab = -1)
    {
        BassRigEditor editor { proc };
        editor.setSize (width, height);

        if (rackTab >= 0)
            if (auto* rack = findRack (editor))
                rack->select (rackTab);

        juce::Image image { juce::Image::ARGB, width, height, true };
        {
            juce::Graphics g { image };
            editor.paintEntireComponent (g, true);
        }

        file.deleteFile();

        if (auto stream = file.createOutputStream())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (image, *stream);
        }

        std::cout << "wrote " << file.getFullPathName() << " (" << width << "x" << height << ")\n";
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File outputDir = argc > 1 ? juce::File (juce::String (argv[1]))
                                          : juce::File::getCurrentWorkingDirectory();
    outputDir.createDirectory();

    BassRigProcessor proc;

    writeSnapshot (proc, outputDir.getChildFile ("pedal-face.png"),
                   BassRigEditor::designWidth, BassRigEditor::designHeight);

    // Every rack tab, so no panel can be quietly broken behind an unselected one.
    const char* tabNames[] { "gate", "comp", "octave", "filter", "phaser", "chorus" };

    for (int tab = 1; tab < (int) std::size (tabNames); ++tab)
        writeSnapshot (proc,
                       outputDir.getChildFile (juce::String ("pedal-face-") + tabNames[tab] + ".png"),
                       BassRigEditor::designWidth, BassRigEditor::designHeight, tab);

    // Half size, to check the face survives being scaled down.
    writeSnapshot (proc, outputDir.getChildFile ("pedal-face-small.png"),
                   BassRigEditor::designWidth / 2, BassRigEditor::designHeight / 2);

    return 0;
}
