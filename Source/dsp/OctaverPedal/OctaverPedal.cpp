#include "OctaverPedal.h"

namespace
{
    constexpr double smoothingSeconds = 0.02;

    // Below this the detector is looking at noise rather than a note. Two
    // thresholds, not one: a single one flickered every cycle as a note decayed
    // past it. It matters far less than it used to -- a flicker now mutes for a
    // moment instead of scrambling a divider -- but a control that chatters is
    // still worse than one that does not.
    constexpr float squelchOpenGain = 0.004f;    // about -48 dBFS
    constexpr float squelchCloseGain = 0.0018f;  // about -55 dBFS

    // The envelope holds its peak for longer than one cycle of the lowest note.
    // Without it the follower ripples 67% per cycle at 41 Hz against 8% at
    // 220 Hz, and everything scaled by it inherits that.
    constexpr float envelopeHoldMs = 40.0f;

    constexpr float envelopeAttackMs = 6.0f;
    constexpr float envelopeReleaseMs = 90.0f;

    // Crossings closer to zero than this are ignored, so ripple on a decaying
    // note does not manufacture extra ones.
    constexpr float crossingHysteresis = 0.08f;

    // The band the tracker will believe. 24 Hz is below a five-string B and
    // 800 Hz is above anything worth an octave pedal; a reading outside this is
    // noise, not a note.
    constexpr float lowestTrackedHz  = 24.0f;
    constexpr float highestTrackedHz = 800.0f;

    // How far a new reading may sit from the running estimate and still be
    // treated as the same note. Vibrato and a decaying note move the period by a
    // few percent; an octave error moves it by 50% or 100%, so there is a wide
    // gap to put the line in.
    constexpr float periodTolerance = 0.35f;

    // ...and how closely two consecutive disagreeing readings must match each
    // other before the tracker accepts that the note really has changed. This is
    // what stops one bad crossing moving the pitch, while still letting a
    // genuine new note through in two cycles -- 25 ms at the bottom of the neck.
    constexpr float candidateTolerance = 0.12f;
    constexpr int   agreementsForNewNote = 2;

    // How quickly the accepted period is followed. Fast enough to settle inside
    // a note, slow enough that a single odd reading barely moves it.
    constexpr float periodSmoothing = 0.25f;

    // How much of the measured phase error each crossing decides to correct.
    constexpr float phaseLockStrength = 0.25f;

    // ...and over how long that correction is then paid back. Applying it at the
    // moment of the crossing moves the waveform's value in one step, which is a
    // click -- the same class of fault as the boolean gate below, and audible
    // for the same reason once the waveform became smooth enough to hear it
    // against. Spread over 15 ms it is a small change of rate instead.
    constexpr float phaseCorrectionMs = 15.0f;

    // With no believable crossing for this long, the note is over as far as the
    // tracker is concerned.
    constexpr float trackingHoldMs = 120.0f;

    // How quickly the octave arrives and leaves once the tracker has an opinion.
    // These exist because the amplitude used to be gated by a boolean, which
    // stepped the waveform by its entire amplitude every time tracking dropped
    // or came back. On a decaying note that happens over and over, and every one
    // of those steps is a click.
    //
    // Opening is quick enough not to soften a pluck. Closing is slow enough that
    // the ear reads it as the note ending rather than as an edit.
    constexpr float gateOpenMs  = 4.0f;
    constexpr float gateCloseMs = 30.0f;

    // How loud the second and third harmonics get at full Growl, relative to the
    // fundamental. Chosen so the top of the control reads as growl rather than
    // as a different instrument.
    constexpr float growlSecond = 0.7f;
    constexpr float growlThird  = 0.45f;

    float onePoleCoeff (float milliseconds, double sampleRate) noexcept
    {
        return std::exp (-1.0f / (0.001f * juce::jmax (0.01f, milliseconds) * (float) sampleRate));
    }
}

OctaverPedal::OctaverPedal (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    onParam     = apvts.getRawParameterValue (ParamID::octOn);
    directParam = apvts.getRawParameterValue (ParamID::octDirect);
    subOneParam = apvts.getRawParameterValue (ParamID::octSubOne);
    subTwoParam = apvts.getRawParameterValue (ParamID::octSubTwo);
    toneParam   = apvts.getRawParameterValue (ParamID::octTone);
    trackParam  = apvts.getRawParameterValue (ParamID::octTrack);
    growlParam  = apvts.getRawParameterValue (ParamID::octGrowl);
}

void OctaverPedal::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using Range = juce::NormalisableRange<float>;

    layout.add (Params::boolParam (ParamID::octOn, "Octave", false));

    layout.add (Params::percentParam (ParamID::octDirect, "Direct", 1.0f));
    layout.add (Params::percentParam (ParamID::octSubOne, "Octave 1", 0.6f));
    layout.add (Params::percentParam (ParamID::octSubTwo, "Octave 2", 0.0f));

    // Gentler than it was, and default higher, because there is no longer a
    // square edge that has to be hidden.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::octTone, 1 }, "Octave Tone",
        Range { 150.0f, 4000.0f, 1.0f, 0.4f }, 1200.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    // How much of the octave is carried by its harmonics rather than its
    // fundamental. An octave below the low strings lands under 40 Hz, where most
    // speakers give up; its harmonics do not. See the note in the header.
    layout.add (Params::percentParam (ParamID::octGrowl, "Growl", 0.35f));

    // Where the detector stops looking. Too high and it times harmonics instead
    // of the fundamental, too low and open strings stop being seen at all.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::octTrack, 1 }, "Tracking",
        Range { 90.0f, 600.0f, 1.0f, 0.5f }, 250.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));
}

void OctaverPedal::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate  = spec.sampleRate;
    numChannels = juce::jmax (1, static_cast<int> (spec.numChannels));

    for (auto* filter : { &trackingFilterA, &trackingFilterB })
    {
        filter->prepare (spec);
        filter->setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        filter->setCutoffFrequency (trackParam->load());
    }

    toneFilter.prepare (spec);
    toneFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    toneFilter.setCutoffFrequency (toneParam->load());

    directGain.reset (spec.sampleRate, smoothingSeconds);
    directGain.setCurrentAndTargetValue (directParam->load());
    subOneGain.reset (spec.sampleRate, smoothingSeconds);
    subOneGain.setCurrentAndTargetValue (subOneParam->load());
    subTwoGain.reset (spec.sampleRate, smoothingSeconds);
    subTwoGain.setCurrentAndTargetValue (subTwoParam->load());
    growlAmount.reset (spec.sampleRate, smoothingSeconds);
    growlAmount.setCurrentAndTargetValue (growlParam->load());

    reset();
}

void OctaverPedal::reset()
{
    trackingFilterA.reset();
    trackingFilterB.reset();
    toneFilter.reset();

    above = false;
    samplesSinceCrossing = 0;
    trackedPeriod = 0.0f;
    candidatePeriod = 0.0f;
    agreeingReadings = 0;
    samplesSinceValid = 0;

    phase = 0.0f;
    oscPeriod = 0.0f;
    pitchGate = 0.0f;
    pendingPhaseCorrection = 0.0f;
    envelope = 0.0f;
    envelopeHold = 0;
    live = false;

    tracking.store (false, std::memory_order_relaxed);
}

void OctaverPedal::process (juce::dsp::AudioBlock<float>& block)
{
    const auto blockChannels = juce::jmin (numChannels, static_cast<int> (block.getNumChannels()));
    const auto numSamples    = static_cast<int> (block.getNumSamples());

    if (blockChannels <= 0 || numSamples <= 0)
        return;

    if (onParam->load() < 0.5f)
    {
        tracking.store (false, std::memory_order_relaxed);
        return;
    }

    const auto trackHz = trackParam->load();
    trackingFilterA.setCutoffFrequency (trackHz);
    trackingFilterB.setCutoffFrequency (trackHz);
    toneFilter.setCutoffFrequency (toneParam->load());

    directGain.setTargetValue (directParam->load());
    subOneGain.setTargetValue (subOneParam->load());
    subTwoGain.setTargetValue (subTwoParam->load());
    growlAmount.setTargetValue (growlParam->load());

    const auto envAttack   = onePoleCoeff (envelopeAttackMs, sampleRate);
    const auto envRelease  = onePoleCoeff (envelopeReleaseMs, sampleRate);
    const auto holdSamples = juce::roundToInt (envelopeHoldMs * 0.001 * sampleRate);
    const auto trackHold   = juce::roundToInt (trackingHoldMs * 0.001 * sampleRate);
    const auto gateOpen    = 1.0f - onePoleCoeff (gateOpenMs, sampleRate);
    const auto gateClose   = 1.0f - onePoleCoeff (gateCloseMs, sampleRate);
    const auto phaseCorrectionRate = 1.0f - onePoleCoeff (phaseCorrectionMs, sampleRate);

    const auto longestPeriod  = (float) sampleRate / lowestTrackedHz;
    const auto shortestPeriod = (float) sampleRate / highestTrackedHz;

    bool trackedThisBlock = false;

    for (int i = 0; i < numSamples; ++i)
    {
        // The tracker is monophonic by nature, so it runs off a mono sum. Two
        // channels tracked independently would produce two different octaves.
        float summed = 0.0f;

        for (int ch = 0; ch < blockChannels; ++ch)
            summed += block.getChannelPointer (static_cast<size_t> (ch))[i];

        summed /= (float) blockChannels;

        const auto detected = trackingFilterB.processSample (0, trackingFilterA.processSample (0, summed));
        const auto rectified = std::abs (summed);

        // Peak with hold rather than a plain one-pole, so the envelope is smooth
        // rather than rippling at the note frequency.
        if (rectified > envelope)
        {
            envelope = rectified + envAttack * (envelope - rectified);
            envelopeHold = holdSamples;
        }
        else if (envelopeHold > 0)
        {
            --envelopeHold;
        }
        else
        {
            envelope = rectified + envRelease * (envelope - rectified);
        }

        if (! live && envelope > squelchOpenGain)
            live = true;
        else if (live && envelope < squelchCloseGain)
            live = false;

        // ---- detect ------------------------------------------------------
        ++samplesSinceCrossing;
        ++samplesSinceValid;

        const auto threshold = crossingHysteresis * envelope;

        if (live && ! above && detected > threshold)
        {
            above = true;

            const auto reading = (float) samplesSinceCrossing;
            samplesSinceCrossing = 0;

            if (reading >= shortestPeriod && reading <= longestPeriod)
            {
                if (trackedPeriod <= 0.0f)
                {
                    // First believable reading of a note: take it whole rather
                    // than easing towards it from nothing.
                    trackedPeriod = reading;
                    samplesSinceValid = 0;
                }
                else if (std::abs (reading - trackedPeriod) <= periodTolerance * trackedPeriod)
                {
                    trackedPeriod += periodSmoothing * (reading - trackedPeriod);
                    agreeingReadings = 0;
                    samplesSinceValid = 0;
                }
                else
                {
                    // Disagrees with what we are tracking. That is either a new
                    // note or a bad crossing, and the difference is whether it
                    // happens twice in a row the same way. This is where the old
                    // design lost -- it acted on every crossing, so a single bad
                    // one changed the octave for good.
                    if (candidatePeriod > 0.0f
                        && std::abs (reading - candidatePeriod) <= candidateTolerance * candidatePeriod)
                    {
                        if (++agreeingReadings >= agreementsForNewNote)
                        {
                            trackedPeriod = reading;
                            agreeingReadings = 0;
                            candidatePeriod = 0.0f;
                            samplesSinceValid = 0;
                        }
                    }
                    else
                    {
                        candidatePeriod = reading;
                        agreeingReadings = 1;
                    }
                }
            }

            // ---- lock ----------------------------------------------------
            // One input cycle is half a turn of the first octave down, so every
            // crossing should land on a multiple of 0.5. Pull towards the
            // nearest one rather than snapping, which keeps it locked without a
            // click and without ever having to decide WHICH half it is in --
            // the ambiguity the flip-flop used to get stuck in.
            if (trackedPeriod > 0.0f)
            {
                const auto nearest = std::round (phase * 2.0f) * 0.5f;
                pendingPhaseCorrection += phaseLockStrength * (nearest - phase);
            }
        }
        else if (above && detected < -threshold)
        {
            above = false;
        }

        if (! live || samplesSinceValid > trackHold)
        {
            trackedPeriod = 0.0f;
            candidatePeriod = 0.0f;
            agreeingReadings = 0;
        }

        // ---- generate ----------------------------------------------------
        const auto hasPitch = trackedPeriod > 0.0f && live;

        if (hasPitch)
        {
            trackedThisBlock = true;
            oscPeriod = trackedPeriod;
        }

        // Keep running while the gate closes. Freezing the phase instead would
        // hold the waveform at whatever value it happened to stop on and fade
        // THAT out, which is a decaying DC offset rather than a note ending.
        if (oscPeriod > 0.0f)
        {
            // Half the input frequency, and the accumulator wraps at 2 so the
            // second octave down is one turn in two.
            phase += 1.0f / (2.0f * oscPeriod);

            // Work off any outstanding lock error as a rate change rather than a
            // jump. Over 15 ms this is far below the oscillator's own rate, so
            // it pulls into alignment without ever moving the waveform.
            const auto correction = pendingPhaseCorrection * phaseCorrectionRate;
            phase += correction;
            pendingPhaseCorrection -= correction;

            while (phase >= 2.0f)
                phase -= 2.0f;

            while (phase < 0.0f)
                phase += 2.0f;
        }

        const auto gateTarget = hasPitch ? 1.0f : 0.0f;
        pitchGate += (hasPitch ? gateOpen : gateClose) * (gateTarget - pitchGate);

        const auto growl = growlAmount.getNextValue();
        const auto shape = envelope * pitchGate;

        // Harmonic amplitudes are chosen, not produced by clipping, so nothing
        // is generated above where it belongs and nothing can fold.
        //
        // They are deliberately NOT normalised to constant power. Exact
        // normalisation was written first and measured wrong: on a note whose
        // octave already sits above 40 Hz it makes Growl add exactly zero
        // audible energy -- -0.0003 dB at a 110 Hz note -- because there is
        // nothing left to redistribute from, which breaks the one promise the
        // control makes.
        //
        // It is not needed either. The old design boosted a whole crossover
        // band by four and ran away by 12 dB, so it had to be reined in with a
        // follower and a ceiling (DESIGN.md 3r). Here the amplitudes are chosen,
        // so the worst the level can rise is
        //
        //     sqrt(1 + 0.7^2 + 0.45^2) = 1.30,  i.e. 2.3 dB
        //
        // at full Growl, bounded by construction and comfortably inside the
        // 3 dB the old design had to work to achieve.
        const auto second = growl * growlSecond;
        const auto third  = growl * growlThird;

        const auto twoPi = juce::MathConstants<float>::twoPi;

        const auto voice = [&] (float turnsPerCycle)
        {
            const auto p = phase / turnsPerCycle;

            return std::sin (twoPi * p)
                    + second * std::sin (twoPi * 2.0f * p)
                    + third  * std::sin (twoPi * 3.0f * p);
        };

        // One turn of the accumulator is a cycle of the first octave down; two
        // turns are a cycle of the second.
        const auto one = shape * voice (1.0f);
        const auto two = shape * voice (2.0f);

        const auto sub = toneFilter.processSample (0, one * subOneGain.getNextValue()
                                                        + two * subTwoGain.getNextValue());

        const auto direct = directGain.getNextValue();

        for (int ch = 0; ch < blockChannels; ++ch)
        {
            auto* samples = block.getChannelPointer (static_cast<size_t> (ch));
            samples[i] = samples[i] * direct + sub;
        }
    }

    tracking.store (trackedThisBlock, std::memory_order_relaxed);
}
