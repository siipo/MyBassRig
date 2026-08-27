#include "OctaverPedal.h"

namespace
{
    constexpr double smoothingSeconds = 0.02;

    // Below this the comparator is looking at noise rather than a note, and the
    // flip-flops would free-run and warble out of silence.
    //
    // Two thresholds, not one. A single one flickered every cycle as a note
    // decayed past it, and each flicker reset the flip-flops mid-note, which
    // scrambled the division and sounded like the octave jumping up.
    //
    // This and the two guards below are deliberately redundant. Tested by
    // reverting them one at a time: the fault needs ALL THREE weaknesses at
    // once, and any single one of these prevents it. None of them is "the" fix,
    // so none of them should be removed as unnecessary on the grounds that the
    // others cover it.
    constexpr float squelchOpenGain = 0.004f;    // about -48 dBFS
    constexpr float squelchCloseGain = 0.0018f;  // about -55 dBFS

    // How long the input has to stay squelched before the flip-flops are reset.
    // Resetting is right at the start of a NEW note and wrong in the middle of a
    // decaying one.
    constexpr float squelchResetMs = 120.0f;

    // The envelope follower holds its peak for longer than one cycle of the
    // lowest note. Without this it ripples 67% per cycle at 41 Hz -- against 8%
    // at 220 Hz -- and everything downstream that scales with it inherits that
    // ripple. This is why the fault got worse the lower the note.
    constexpr float envelopeHoldMs = 40.0f;

    // The comparator ignores crossings this close to zero, so the ripple on a
    // decaying note does not trigger extra edges and drop the octave an extra
    // octave.
    constexpr float comparatorHysteresis = 0.08f;

    constexpr float envelopeAttackMs = 3.0f;
    constexpr float envelopeReleaseMs = 60.0f;

    // Where the octave stops being felt and starts being heard.
    constexpr float growlCrossoverHz = 60.0f;

    // How much Growl can lift the audible half. Additive rather than a
    // crossfade, and that distinction is load bearing: a crossfade at full
    // Growl DISCARDS the low half, which measured -6.6 dB of audible energy on
    // the notes whose octave was already above the crossover. Boosting instead
    // of trading means the control can only ever help.
    constexpr float growlBoost = 3.0f;

    // Time constant for the Growl level match. Long enough that it follows the
    // note rather than the waveform -- at 80 ms it is well below the lowest
    // octave the divider produces -- and short enough to settle early in a note.
    constexpr float growlLevelMs = 80.0f;

    // Below this the octave is silent and the ratio is noise over noise. In
    // mean-square terms, so it is the square of a level.
    constexpr float growlLevelFloor = 1.0e-10f;

    // The most level Growl may add, whatever the note: +3 dB.
    //
    // Chosen by measuring both things it has to satisfy. Audible gain at full
    // Growl, against the 6 dB the low strings are required to get:
    //
    //     ceiling   41.2 Hz   55 Hz   82.4 Hz   110 Hz
    //     1.2        6.38     7.01     1.89      1.58
    //     1.4        7.50     8.35     3.23      2.92
    //     1.6        7.50     9.06     4.37      3.60
    //
    // 1.2 clears the requirement by 0.38 dB, which is not a margin. 1.4 clears
    // it by 1.5 dB and leaves the two lowest notes untouched -- their natural
    // ratio is already under 1.4, so the ceiling never engages there and Growl
    // does exactly what it did before. It only binds further up the neck, which
    // is where the level was running away.
    //
    // Peak at full Growl, before and after:
    //
    //     f0        was      now
    //     111 Hz    2.52     2.13
    //     220 Hz    4.52     1.80
    //     439 Hz    3.69     1.24
    constexpr float growlMaxGain = 1.4f;

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

    // A raw square is unpleasant, so the generated octaves are rounded off.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::octTone, 1 }, "Octave Tone",
        Range { 150.0f, 4000.0f, 1.0f, 0.4f }, 700.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    // How much of the octave is carried by its harmonics rather than its
    // fundamental. See the note in the header: below the A on the E string,
    // most of a pure octave is under 40 Hz and inaudible on most speakers.
    layout.add (Params::percentParam (ParamID::octGrowl, "Growl", 0.35f));

    // Exposed because where the fundamental sits depends on how far up the neck
    // you are: too high and the comparator sees harmonics and jumps an octave,
    // too low and open strings stop tracking at all.
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

    // All-pass complementary, so at Growl centred the two halves sum back to
    // the octave unaltered rather than notching it.
    growlCrossover.prepare (spec);
    growlCrossover.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    growlCrossover.setCutoffFrequency (growlCrossoverHz);

    subBuffer.setSize (numChannels, static_cast<int> (spec.maximumBlockSize));

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
    growlCrossover.reset();
    subBuffer.clear();

    envelope = 0.0f;
    growlPlainLevel = 0.0f;
    growlBoostedLevel = 0.0f;
    envelopeHold = 0;
    squelchedFor = 0;
    live = false;
    above = false;
    flipOne = false;
    flipTwo = false;
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

    const auto growlLevelCoeff = 1.0f - onePoleCoeff (growlLevelMs, sampleRate);

    const auto envAttack  = onePoleCoeff (envelopeAttackMs, sampleRate);
    const auto envRelease = onePoleCoeff (envelopeReleaseMs, sampleRate);
    const auto holdSamples = juce::roundToInt (envelopeHoldMs * 0.001 * sampleRate);
    const auto resetSamples = juce::roundToInt (squelchResetMs * 0.001 * sampleRate);

    bool trackedThisBlock = false;

    for (int i = 0; i < numSamples; ++i)
    {
        // The divider is monophonic by nature, so it runs off a mono sum. Two
        // channels dividing independently would produce two different octaves.
        float summed = 0.0f;

        for (int ch = 0; ch < blockChannels; ++ch)
            summed += block.getChannelPointer (static_cast<size_t> (ch))[i];

        summed /= (float) blockChannels;

        const auto tracked = trackingFilterB.processSample (0, trackingFilterA.processSample (0, summed));

        const auto rectified = std::abs (summed);

        // Peak with hold, not a plain one-pole. The hold spans more than one
        // cycle of the lowest note, so the envelope is smooth rather than
        // rippling at the note frequency.
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

        // Hysteresis, so a decaying note does not flicker in and out.
        if (! live && envelope > squelchOpenGain)
            live = true;
        else if (live && envelope < squelchCloseGain)
            live = false;

        if (! live)
        {
            // Only reset once the input has been quiet for a while. Resetting is
            // right at the start of a new note and wrong partway through a
            // decaying one -- doing it immediately is what broke the division.
            if (squelchedFor < resetSamples)
            {
                ++squelchedFor;
            }
            else
            {
                above = false;
                flipOne = false;
                flipTwo = false;
            }
        }
        else
        {
            squelchedFor = 0;
            trackedThisBlock = true;

            const auto threshold = comparatorHysteresis * envelope;

            if (! above && tracked > threshold)
            {
                above = true;

                // Rising edge: divide by two, and the second flip-flop divides
                // that again.
                flipOne = ! flipOne;

                if (flipOne)
                    flipTwo = ! flipTwo;
            }
            else if (above && tracked < -threshold)
            {
                above = false;
            }
        }

        // The squares follow the playing dynamics, which is what stops the
        // octave sounding like a synth bolted on top.
        const auto shape = live ? envelope : 0.0f;
        const auto one = (flipOne ? 1.0f : -1.0f) * shape;
        const auto two = (flipTwo ? 1.0f : -1.0f) * shape;

        const auto shaped = toneFilter.processSample (0, one * subOneGain.getNextValue()
                                                          + two * subTwoGain.getNextValue());

        // Lift the octave's harmonics without giving up its fundamental. At
        // Growl zero the two halves sum back to the octave untouched, because
        // the crossover is all-pass complementary.
        float rumble = 0.0f, pitch = 0.0f;
        growlCrossover.processSample (0, shaped, rumble, pitch);

        const auto growl = growlAmount.getNextValue();

        // rumble + pitch is exactly `shaped` again, so boosting the upper half
        // is the same as adding pitch * growl * boost to the whole octave.
        const auto boosted = shaped + pitch * growl * growlBoost;

        // ...and that addition is up to +12 dB of LEVEL, not just of harmonics.
        // Measured with the octave alone: peak went from 1.18 to 4.52 at a
        // 110 Hz octave as Growl was swept, and 0.93 to 3.69 at 220 Hz. Four and
        // a half times full scale arrives at the drive's clippers as an
        // instruction to destroy the signal, which is what "turning Growl up
        // garbles it" was.
        //
        // How much level it adds depends on the note, because the share of the
        // octave sitting above the 60 Hz crossover does: about 0.55 of it at a
        // 56 Hz octave, essentially all of it at 220 Hz. So a fixed makeup
        // taper cannot fix this the way the drive's does -- it would be right
        // for one note and wrong either side of it.
        //
        // Level matching against the unboosted octave handles every note with
        // one rule, and turns Growl into what it was always described as: a
        // control over where the octave's energy sits, not how much of it there
        // is. The follower is slow enough not to chase the waveform and fast
        // enough to settle within a note.
        // Mean SQUARE, not mean absolute. Boosting the harmonics makes the
        // waveform spikier, and for a spikier wave those two diverge -- matching
        // the rectified average left RMS drifting up 2.5 dB across the control.
        // Matching power holds loudness where it should be.
        growlPlainLevel   += growlLevelCoeff * (shaped  * shaped  - growlPlainLevel);
        growlBoostedLevel += growlLevelCoeff * (boosted * boosted - growlBoostedLevel);

        // A ceiling, not a level match. Holding the power exactly constant was
        // tried first and is wrong: Growl can then only REDISTRIBUTE energy, so
        // on a note whose octave already sits mostly above 40 Hz there is
        // nothing to redistribute from, and the measured audible gain fell to
        // -0.004 dB. A control described as one that can only ever help must
        // not go negative anywhere.
        //
        // So Growl is allowed to add level, and simply not allowed to add more
        // than growlMaxGain of it. Below the ceiling nothing happens at all and
        // the control behaves exactly as it always did.
        const auto plainLevel = juce::jmax (growlPlainLevel, growlLevelFloor);
        const auto ratio = std::sqrt (juce::jmax (growlBoostedLevel, 0.0f) / plainLevel);

        const auto compensation = ratio > growlMaxGain ? growlMaxGain / ratio : 1.0f;

        const auto sub = boosted * compensation;

        const auto direct = directGain.getNextValue();

        for (int ch = 0; ch < blockChannels; ++ch)
        {
            auto* samples = block.getChannelPointer (static_cast<size_t> (ch));
            samples[i] = samples[i] * direct + sub;
        }
    }

    tracking.store (trackedThisBlock, std::memory_order_relaxed);
}
