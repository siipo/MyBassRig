// A host that exists to make one crash reproducible and print where it happened.
//
// pluginval segfaults in its "Parameter thread safety" test about one run in
// eight (DESIGN.md 3s). The plugin's own processor, hammered the same way, runs
// clean -- so the VST3 wrapper is needed to provoke it, and that is what this
// loads. pluginval itself is a black box with no debugger on this machine, so
// this reproduces the same scenario in a process we control, with JUCE's crash
// handler installed to print a backtrace when it goes.
//
//     BassRigCrashHunt <path-to-vst3> [iterations]
//
// It mirrors pluginval's test deliberately: parameters are set from the MESSAGE
// thread while processBlock runs on another, because that is the arrangement
// that crashes. Setting them from an ordinary worker thread instead does not
// reproduce it, which is the whole reason this exists.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace
{
    constexpr double hostSampleRate = 44100.0;
    constexpr int    hostBlockSize  = 32;

    // Matches pluginval: 500 blocks of audio against 500 sweeps of every
    // parameter.
    constexpr int blocksPerIteration = 500;

    void crashHandler (void*)
    {
        // Deliberately std::cerr and not juce::Logger: by the time this runs the
        // process is already in an undefined state and the fewer allocations the
        // better.
        std::cerr << "\n=============== CRASHED ===============\n"
                  << juce::SystemStats::getStackBacktrace()
                  << "\n=======================================" << std::endl;

        std::_Exit (139);
    }

    std::unique_ptr<juce::AudioPluginInstance> load (const juce::String& path, juce::String& error)
    {
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> found;

        format.findAllTypesForFile (found, path);

        if (found.isEmpty())
        {
            error = "no plugin types found in " + path;
            return {};
        }

        return format.createInstanceFromDescription (*found.getFirst(), hostSampleRate,
                                                     hostBlockSize, error);
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: BassRigCrashHunt <path-to-vst3> [iterations]\n";
        return 2;
    }

    const juce::String path { argv[1] };
    const auto iterations = argc > 2 ? juce::String (argv[2]).getIntValue() : 50;

    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::SystemStats::setApplicationCrashHandler (crashHandler);

    juce::String error;
    auto plugin = load (path, error);

    if (plugin == nullptr)
    {
        std::cerr << "could not load: " << error << "\n";
        return 2;
    }

    std::cout << "loaded " << plugin->getName() << ", "
              << plugin->getParameters().size() << " parameters\n"
              << "running " << iterations << " iterations\n" << std::flush;

    std::atomic<bool> finished { false };
    std::atomic<int>  completed { 0 };

    // The worker plays the part of pluginval's test thread. The main thread
    // stays the message thread and does nothing but dispatch, so that the
    // parameter sweeps really do run where pluginval runs them.
    std::thread worker ([&]
    {
        auto& params = plugin->getParameters();

        const auto channels = juce::jmax (plugin->getTotalNumInputChannels(),
                                          plugin->getTotalNumOutputChannels());
        juce::AudioBuffer<float> buffer (juce::jmax (1, channels), 1024);
        juce::MidiBuffer midi;
        juce::Random random (20260827);

        // pluginval runs its whole battery in one process, and the crash lands
        // in the LAST of a sequence. The two tests immediately before it are
        // "Editor" and "Editor Automation", so an editor has been created,
        // driven and destroyed by the time it happens. Reproducing the crash
        // without reproducing that history was the first thing this harness got
        // wrong -- 30 clean iterations of the parameter sweep alone.
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            juce::WaitableEvent prepared, started, editorMade, editorGone;

            // Sample rate and block size vary in pluginval too, and the crash
            // was seen with the defaults rather than the reduced set.
            const double rates[] = { 44100.0, 48000.0, 96000.0 };
            const int sizes[] = { 64, 128, 256, 512, 1024 };
            const auto rate = rates[iteration % 3];
            const auto size = sizes[iteration % 5];

            juce::MessageManager::callAsync ([&]
            {
                plugin->releaseResources();
                plugin->prepareToPlay (rate, size);
                prepared.signal();
            });

            prepared.wait();

            // Editor open, parameters moving underneath it, then torn down --
            // which is what "Editor Automation" followed by its destructor does.
            juce::MessageManager::callAsync ([&]
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());

                if (editor != nullptr)
                {
                    editor->setSize (editor->getWidth(), editor->getHeight());

                    juce::Random editorRandom (iteration + 1);

                    for (int i = 0; i < 64; ++i)
                        for (auto* p : plugin->getParameters())
                            p->setValueNotifyingHost (editorRandom.nextFloat());
                }

                editor.reset();
                plugin->editorBeingDeleted (nullptr);
                editorGone.signal();
            });

            editorGone.wait();
            juce::ignoreUnused (editorMade);

            juce::MessageManager::callAsync ([&]
            {
                plugin->releaseResources();
                plugin->prepareToPlay (rate, size);
                prepared.signal();
            });

            prepared.wait();

            juce::MessageManager::callAsync ([&, sweepRandom = random]() mutable
            {
                started.signal();

                for (int i = 0; i < blocksPerIteration; ++i)
                    for (auto* p : params)
                        p->setValueNotifyingHost (sweepRandom.nextFloat());
            });

            started.wait();

            juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(),
                                            buffer.getNumChannels(), 0, size);

            for (int i = 0; i < blocksPerIteration; ++i)
            {
                for (int ch = 0; ch < block.getNumChannels(); ++ch)
                    for (int n = 0; n < size; ++n)
                        block.setSample (ch, n, 0.25f * std::sin (0.05f * (float) n));

                plugin->processBlock (block, midi);
            }

            completed.store (iteration + 1);
            std::cout << "." << std::flush;
        }

        finished.store (true);
    });

    while (! finished.load())
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    worker.join();

    // Anything still queued would otherwise run against a destroyed plugin.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    std::cout << "\nsurvived " << completed.load() << " iterations\n";

    plugin.reset();
    return 0;
}
