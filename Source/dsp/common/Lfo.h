#pragma once

#include <juce_dsp/juce_dsp.h>

// A modulation oscillator, shared by the chorus and the phaser.
//
// Deliberately not one of chowdsp's oscillators: those are band-limited for
// audio-rate use, and at 0.05 Hz that machinery buys nothing. What a modulation
// LFO needs instead is a stable phase accumulator and cheap per-sample output.
class Lfo
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        reset();
    }

    void reset() noexcept { phase = 0.0f; }

    void setRateHz (float hz) noexcept
    {
        increment = juce::jlimit (0.0f, 0.5f, hz / (float) sampleRate);
    }

    // Advances one sample and returns a value in [0, 1].
    inline float next() noexcept
    {
        phase += increment;

        if (phase >= 1.0f)
            phase -= 1.0f;

        return unipolarAt (phase);
    }

    // The same waveform read at a fixed offset around the circle, so two voices
    // can share one accumulator instead of drifting apart over time.
    [[nodiscard]] inline float offsetBy (float turns) const noexcept
    {
        auto p = phase + turns;

        while (p >= 1.0f)
            p -= 1.0f;

        return unipolarAt (p);
    }

private:
    static inline float unipolarAt (float p) noexcept
    {
        return 0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi * p);
    }

    double sampleRate = 48000.0;
    float phase = 0.0f;
    float increment = 0.0f;
};
