#pragma once

#include "TestHarness.h"
#include "dsp/DrivePedal/DrivePedal.h"

namespace TestSignals
{
    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;

    // Parameters are set on the harness BEFORE prepare() so the smoothers snap
    // to their targets instead of ramping through the measurement.
    struct Fixture
    {
        HarnessProcessor host;
        DrivePedal pedal { host.apvts };

        void prepare (int numChannels = 1)
        {
            pedal.prepare ({ sampleRate, (juce::uint32) blockSize, (juce::uint32) numChannels });
        }

        juce::AudioBuffer<float> run (juce::AudioBuffer<float> buffer)
        {
            for (int pos = 0; pos + blockSize <= buffer.getNumSamples(); pos += blockSize)
            {
                auto block = juce::dsp::AudioBlock<float> (buffer)
                                 .getSubBlock ((size_t) pos, (size_t) blockSize);
                pedal.process (block);
            }

            return buffer;
        }
    };

    inline juce::AudioBuffer<float> sine (float frequency, int numSamples,
                                          int numChannels = 1, float amplitude = 0.5f)
    {
        juce::AudioBuffer<float> b (numChannels, numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                b.setSample (ch, i, amplitude * std::sin (juce::MathConstants<float>::twoPi
                                                          * frequency * (float) i / (float) sampleRate));
        return b;
    }

    inline juce::AudioBuffer<float> impulse (int numSamples, int numChannels = 1)
    {
        juce::AudioBuffer<float> b (numChannels, numSamples);
        b.clear();

        for (int ch = 0; ch < numChannels; ++ch)
            b.setSample (ch, 0, 1.0f);

        return b;
    }

    inline int peakIndex (const juce::AudioBuffer<float>& b, int channel = 0)
    {
        int best = 0;
        float bestValue = -1.0f;

        for (int i = 0; i < b.getNumSamples(); ++i)
            if (const auto v = std::abs (b.getSample (channel, i)); v > bestValue)
            {
                bestValue = v;
                best = i;
            }

        return best;
    }

    inline float rms (const juce::AudioBuffer<float>& b, int startSample)
    {
        return b.getRMSLevel (0, startSample, b.getNumSamples() - startSample);
    }

    // RMS of the pedal's output for a steady sine at a given blend setting.
    inline float steadyRms (float blend, float frequency, int numSamples = 8192)
    {
        Fixture f;
        f.host.setParam (ParamID::blend, blend);
        f.host.setParam (ParamID::grunt, 0.0f);
        f.prepare();
        return rms (f.run (sine (frequency, numSamples)), 2048);
    }
}
