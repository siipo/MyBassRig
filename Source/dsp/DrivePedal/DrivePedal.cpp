#include "DrivePedal.h"

#include <array>

namespace
{
    constexpr int    oversamplingStages = 1;    // 2x
    constexpr int    oversamplingFactor = 1 << oversamplingStages;
    constexpr float  dcBlockHz          = 20.0f;
    constexpr float  recoveryLpfHz      = 5000.0f;
    // Every high-pass in the drive path that the clean path does not also have
    // rotates the two out of phase, and the damage is worst exactly where a
    // bass guitar's fundamental lives. Measured: coupling at 30 Hz plus an
    // output DC blocker at 20 Hz put the paths ~90 degrees apart at 41 Hz and
    // the blend lost 70% of the low E. Hence 5 Hz -- low enough to be phase
    // transparent across the whole instrument range, still low enough to stop
    // DC.
    constexpr float  couplingHz         = 5.0f;   // inter-stage coupling "capacitor"
    constexpr double smoothingSeconds   = 0.02;

    // Operating-point offset of the first stage, in shaper-input units. Small
    // against a clipping threshold near 1: enough to shift the duty cycle of
    // the clipped waveform and put even harmonics in the tone, not enough to
    // bias the stage into saturation on its own.
    constexpr float  stageABias         = 0.18f;

    // Stage A saturates at +-1, so stage B needs to be driven above unity to
    // do anything at all.
    constexpr float  interstageGain     = 1.8f;

    // Drive knob (0..1) to clipper input gain. Exponential so the useful range
    // isn't crammed into the last tenth of the sweep.
    // Drive knob to clipper input gain, as a measured table rather than a curve.
    //
    // The previous mapping was gain = jmap(knob^2, 1, 20), and measurement
    // showed the top half of the control was nearly inert: harmonic content
    // rose 17.7 dB over the lower half of the knob and only 1.6 dB over the
    // upper half. The cascade saturates long before the gain runs out.
    //
    // The table below is the inverse of the measured harmonics-versus-gain
    // curve, so harmonic content rises linearly with knob position: even 2.3 dB
    // steps from -26.2 dB to -7.8 dB across the travel.
    //
    // Gain is capped at 8 rather than 20 on the same evidence. Going from 8 to
    // 20 buys under 1 dB of extra harmonic content while making the last eighth
    // of the knob swing gain by 15 instead of 4. It also keeps the shaper input
    // well clear of shaperInputRange, so the lookup-table clamp effectively
    // never engages for normal signal levels.
    //
    // Regenerate from calibration/drive-measured.csv if the cascade changes.
    constexpr std::array<float, 65> driveTaper
    {
        1.0000f, 1.0242f, 1.0494f, 1.0751f, 1.0997f, 1.1243f,
        1.1488f, 1.1733f, 1.1978f, 1.2222f, 1.2465f, 1.2708f,
        1.2951f, 1.3195f, 1.3438f, 1.3681f, 1.3924f, 1.4168f,
        1.4413f, 1.4658f, 1.4903f, 1.5148f, 1.5403f, 1.5667f,
        1.5932f, 1.6196f, 1.6461f, 1.6729f, 1.7011f, 1.7293f,
        1.7575f, 1.7857f, 1.8139f, 1.8455f, 1.8793f, 1.9131f,
        1.9470f, 1.9808f, 2.0193f, 2.0626f, 2.1059f, 2.1493f,
        2.1942f, 2.2517f, 2.3091f, 2.3666f, 2.4336f, 2.5092f,
        2.5848f, 2.6735f, 2.7716f, 2.8744f, 3.0054f, 3.1414f,
        3.2980f, 3.4721f, 3.6670f, 3.9057f, 4.1822f, 4.5202f,
        4.9229f, 5.4150f, 6.0433f, 6.8626f, 8.0000f,
    };

    float driveKnobToGain (float knob) noexcept
    {
        const auto position = juce::jlimit (0.0f, 1.0f, knob) * (float) (driveTaper.size() - 1);
        const auto index    = (size_t) position;

        if (index + 1 >= driveTaper.size())
            return driveTaper.back();

        return driveTaper[index]
             + (position - (float) index) * (driveTaper[index + 1] - driveTaper[index]);
    }

    // Output compensation, so Drive stays a character control rather than a
    // volume control.
    //
    // This was 1/sqrt(gain), which held level within 0.7 dB over the first
    // sixty percent of the knob and then dropped 5.1 dB by the top, because the
    // cascade saturates and output stops tracking gain. No single exponent
    // fixes that: correcting the top over-compensates the middle.
    //
    // So this table is measured too, derived from the output level of a 220 Hz
    // tone at half scale across the whole travel. It is a voicing calibration
    // at one reference level, not a physical law -- a much hotter or quieter
    // input will not track it exactly -- but level now holds flat instead of
    // sagging where the pedal is driven hardest.
    //
    // The table is also normalised so the drive path leaves this stage at
    // unity. It measured 4.0 dB hotter than the clean path, which made a 50/50
    // Blend anything but even -- the driven signal simply dominated. Blend is
    // now a genuine balance between the two paths, with Level there to trim.
    //
    // Regenerate from calibration/makeup-measured.csv alongside the drive taper.
    constexpr std::array<float, 65> driveMakeupTaper
    {
        0.6302f, 0.6180f, 0.6063f, 0.5947f, 0.5846f, 0.5748f,
        0.5655f, 0.5569f, 0.5486f, 0.5408f, 0.5336f, 0.5266f,
        0.5201f, 0.5138f, 0.5079f, 0.5024f, 0.4971f, 0.4920f,
        0.4873f, 0.4828f, 0.4785f, 0.4746f, 0.4707f, 0.4668f,
        0.4632f, 0.4599f, 0.4568f, 0.4538f, 0.4509f, 0.4481f,
        0.4456f, 0.4431f, 0.4408f, 0.4384f, 0.4359f, 0.4337f,
        0.4317f, 0.4297f, 0.4277f, 0.4253f, 0.4234f, 0.4216f,
        0.4196f, 0.4175f, 0.4153f, 0.4135f, 0.4115f, 0.4093f,
        0.4074f, 0.4054f, 0.4031f, 0.4013f, 0.3987f, 0.3967f,
        0.3947f, 0.3922f, 0.3902f, 0.3883f, 0.3856f, 0.3836f,
        0.3807f, 0.3788f, 0.3769f, 0.3737f, 0.3718f,
    };

    float driveKnobToMakeup (float knob) noexcept
    {
        const auto position = juce::jlimit (0.0f, 1.0f, knob) * (float) (driveMakeupTaper.size() - 1);
        const auto index    = (size_t) position;

        if (index + 1 >= driveMakeupTaper.size())
            return driveMakeupTaper.back();

        return driveMakeupTaper[index]
             + (position - (float) index) * (driveMakeupTaper[index + 1] - driveMakeupTaper[index]);
    }

    int choiceIndex (const std::atomic<float>* p, int numChoices) noexcept
    {
        return juce::jlimit (0, numChoices - 1, static_cast<int> (p->load()));
    }
}

DrivePedal::DrivePedal (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    trimParam   = apvts.getRawParameterValue (ParamID::trim);
    driveParam  = apvts.getRawParameterValue (ParamID::drive);
    blendParam  = apvts.getRawParameterValue (ParamID::blend);
    levelParam  = apvts.getRawParameterValue (ParamID::level);
    masterParam = apvts.getRawParameterValue (ParamID::master);
    gruntParam  = apvts.getRawParameterValue (ParamID::grunt);
    attackParam = apvts.getRawParameterValue (ParamID::attack);
    bassParam      = apvts.getRawParameterValue (ParamID::bass);
    trebleParam    = apvts.getRawParameterValue (ParamID::treble);
    loMidParam     = apvts.getRawParameterValue (ParamID::loMid);
    hiMidParam     = apvts.getRawParameterValue (ParamID::hiMid);
    loMidFreqParam = apvts.getRawParameterValue (ParamID::loMidFreq);
    hiMidFreqParam = apvts.getRawParameterValue (ParamID::hiMidFreq);
}

void DrivePedal::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add (Params::gainParam (ParamID::trim,   "Trim",   -12.0f, 12.0f, 0.0f));

    layout.add (Params::percentParam (ParamID::drive, "Drive", 0.35f));
    layout.add (Params::percentParam (ParamID::blend, "Blend", 0.5f));

    layout.add (Params::gainParam (ParamID::level,  "Level",  -24.0f, 6.0f, 0.0f));
    layout.add (Params::gainParam (ParamID::master, "Master", -24.0f, 6.0f, 0.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::grunt, 1 }, "Grunt",
        juce::StringArray { "Full", "Mid", "Tight" }, 1));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::attack, 1 }, "Attack",
        juce::StringArray { "Boost", "Flat", "Cut" }, 1));

    // Bass and Treble are knob positions, not decibels, because they drive a
    // modelled passive Baxandall network whose response is neither symmetric
    // nor constant across frequency. Measured swing is about 19 dB at 60 Hz for
    // Bass and 12 dB at 10 kHz for Treble, and the curve differs at every
    // frequency in between -- a dB label on either would be fiction. The mid
    // bands are peaking sections, where dB is exactly what the control does.
    layout.add (Params::knobParam (ParamID::bass,   "Bass"));
    layout.add (Params::knobParam (ParamID::treble, "Treble"));

    layout.add (Params::gainParam (ParamID::loMid,  "Lo-Mid", -12.0f, 12.0f, 0.0f));
    layout.add (Params::gainParam (ParamID::hiMid,  "Hi-Mid", -12.0f, 12.0f, 0.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::loMidFreq, 1 }, "Lo-Mid Freq",
        juce::StringArray { "250 Hz", "500 Hz", "1 kHz" }, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::hiMidFreq, 1 }, "Hi-Mid Freq",
        juce::StringArray { "750 Hz", "1.5 kHz", "3 kHz" }, 1));
}

void DrivePedal::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    preparedChannels = static_cast<int> (spec.numChannels);

    // useIntegerLatency = true so the clean path can be delayed by a whole
    // number of samples and the null test below is exact rather than approximate.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        spec.numChannels,
        oversamplingStages,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        true,   // max quality
        true);  // integer latency

    oversampler->initProcessing (spec.maximumBlockSize);

    // Total drive-path latency = oversampling filter + the ADAA cascade. The
    // shapers each cost one sample at the OVERSAMPLED rate, so two stages at 2x
    // is exactly one sample at the host's rate. Change the stage count or the
    // oversampling factor and this stops being a whole number -- the clean
    // delay line is integer-only, so that would need a fractional delay.
    static_assert (numShaperStages % oversamplingFactor == 0,
                   "ADAA cascade latency is not a whole number of host samples");

    latencySamples = juce::roundToInt (oversampler->getLatencyInSamples())
                   + numShaperStages / oversamplingFactor;

    dcBlocker.prepare (spec);
    dcBlocker.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    dcBlocker.setCutoffFrequency (dcBlockHz);

    gruntFilter.prepare (spec);
    gruntFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    gruntFilter.setCutoffFrequency (Params::gruntHighpassHz[1]);

    auto osSpec = spec;
    osSpec.sampleRate      = spec.sampleRate * (1 << oversamplingStages);
    osSpec.maximumBlockSize = spec.maximumBlockSize * (1 << oversamplingStages);
    recoveryLpf.prepare (osSpec);
    recoveryLpf.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    recoveryLpf.setCutoffFrequency (recoveryLpfHz);

    couplingFilter.prepare (osSpec);
    couplingFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    couplingFilter.setCutoffFrequency (couplingHz);

    outputDcBlocker.prepare (osSpec);
    outputDcBlocker.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    outputDcBlocker.setCutoffFrequency (couplingHz);

    // Allocates and fills the lookup tables if this is the first instance in
    // the process. Blocks until they are ready -- fine here, never on audio.
    toneStack.prepare (spec);

    stageA.prepare (static_cast<int> (spec.numChannels));
    stageB.prepare (static_cast<int> (spec.numChannels));

    for (size_t i = 0; i < attackShelves.size(); ++i)
        attackShelves[i] = Coeffs::makeHighShelf (spec.sampleRate, 2000.0f,
                                                  0.707f,
                                                  juce::Decibels::decibelsToGain (Params::attackShelfDb[i]));
    attackFilters.clear();
    attackFilters.resize (spec.numChannels);

    for (auto& f : attackFilters)
    {
        f.coefficients = attackShelves[1]; // Flat; process() applies the real choice
        f.prepare (spec);
    }

    currentAttackIndex = -1;

    cleanDelay.setMaximumDelayInSamples (latencySamples + 8);
    cleanDelay.prepare (spec);
    cleanDelay.setDelay (static_cast<float> (latencySamples));

    cleanBuffer.setSize (static_cast<int> (spec.numChannels),
                         static_cast<int> (spec.maximumBlockSize));

    for (auto* s : { &trimGain, &driveGain, &makeupGain, &blendAmount, &levelGain, &masterGain })
        s->reset (spec.sampleRate, smoothingSeconds);

    updateSmoothedTargets();
    for (auto* s : { &trimGain, &driveGain, &makeupGain, &blendAmount, &levelGain, &masterGain })
        s->setCurrentAndTargetValue (s->getTargetValue());

    reset();
}

void DrivePedal::reset()
{
    dcBlocker.reset();
    gruntFilter.reset();
    recoveryLpf.reset();
    couplingFilter.reset();
    outputDcBlocker.reset();
    stageA.reset();
    stageB.reset();
    for (auto& f : attackFilters)
        f.reset();
    cleanDelay.reset();
    toneStack.reset();
    cleanBuffer.clear();

    if (oversampler != nullptr)
        oversampler->reset();

    settleDcPath();
}

// Charges the drive path to the operating point a silent input produces, by
// running the resting bias through stage A and the coupling filter until both
// settle.
//
// Without this the pedal thumps on the first block: the bias arrives as a step
// into an uncharged 5 Hz high-pass, which measured -21 dBFS and took ~150 ms to
// decay. A real pedal does not do that because it is already powered up. The
// alternative -- subtracting the offset analytically -- was tried and is worse:
// tanh(bias) is only the correct offset for a SILENT input, so it over-corrects
// the moment a note is playing, and it disagrees with the lookup table's own
// answer by enough to leave DC behind anyway.
void DrivePedal::settleDcPath()
{
    if (preparedChannels <= 0)
        return;

    constexpr auto restingBias = stageABias;   // drive-independent: bias is post-gain
    const auto osRate      = sampleRate * oversamplingFactor;

    constexpr int chunk = 512;
    const auto totalSamples = juce::roundToInt (osRate * 0.5); // ~8 time constants at 5 Hz

    juce::AudioBuffer<float> scratch (preparedChannels, chunk);

    for (int done = 0; done < totalSamples; done += chunk)
    {
        for (int ch = 0; ch < preparedChannels; ++ch)
        {
            auto* samples = scratch.getWritePointer (ch);
            std::fill (samples, samples + chunk, restingBias);
            stageA.process (samples, samples, chunk, ch);
        }

        juce::dsp::AudioBlock<float> block { scratch };
        juce::dsp::ProcessContextReplacing<float> context { block };
        couplingFilter.process (context);
    }
}

void DrivePedal::updateSmoothedTargets()
{
    trimGain  .setTargetValue (juce::Decibels::decibelsToGain (trimParam->load()));
    levelGain .setTargetValue (juce::Decibels::decibelsToGain (levelParam->load()));
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterParam->load()));
    blendAmount.setTargetValue (blendParam->load());


    const auto driveKnob = driveParam->load();
    driveGain .setTargetValue (driveKnobToGain (driveKnob));
    makeupGain.setTargetValue (driveKnobToMakeup (driveKnob));
}

void DrivePedal::process (juce::dsp::AudioBlock<float>& block)
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples  = block.getNumSamples();

    updateSmoothedTargets();

    // Switch-position controls: applied per block, allocation-free.
    gruntFilter.setCutoffFrequency (Params::gruntHighpassHz[(size_t) choiceIndex (gruntParam, 3)]);

    toneStack.setParams (bassParam->load(), trebleParam->load(),
                         loMidParam->load(), choiceIndex (loMidFreqParam, 3),
                         hiMidParam->load(), choiceIndex (hiMidFreqParam, 3));

    if (const auto attackIndex = choiceIndex (attackParam, 3); attackIndex != currentAttackIndex)
    {
        currentAttackIndex = attackIndex;

        // Pointer swap only -- the filters keep their state, so flicking the
        // switch mid-note does not click.
        for (auto& f : attackFilters)
            f.coefficients = attackShelves[(size_t) attackIndex];
    }

    juce::dsp::ProcessContextReplacing<float> context { block };
    dcBlocker.process (context);
    block.multiplyBy (trimGain);

    // ---- split -----------------------------------------------------------
    auto clean = juce::dsp::AudioBlock<float> (cleanBuffer)
                     .getSubsetChannelBlock (0, numChannels)
                     .getSubBlock (0, numSamples);
    clean.copyFrom (block);

    // ---- drive path ------------------------------------------------------
    gruntFilter.process (context);

    for (size_t ch = 0; ch < numChannels; ++ch)
    {
        auto channelBlock = block.getSingleChannelBlock (ch);
        juce::dsp::ProcessContextReplacing<float> channelContext { channelBlock };
        attackFilters[ch].process (channelContext);
    }

    block.multiplyBy (driveGain);

    {
        auto upsampled = oversampler->processSamplesUp (block);
        const auto osChannels = upsampled.getNumChannels();
        const auto osSamples  = static_cast<int> (upsampled.getNumSamples());

        juce::dsp::ProcessContextReplacing<float> osContext { upsampled };

        // Stage A: asymmetric soft clip. The bias is added here rather than at
        // base rate so the grunt high-pass upstream cannot strip it back out.
        for (size_t ch = 0; ch < osChannels; ++ch)
        {
            auto* samples = upsampled.getChannelPointer (ch);

            // Bias goes in HERE, inside the oversampled region, not at base
            // rate. Added before the upsampler it would be a DC step ramping
            // through the oversampling filter, which is exactly the transient
            // settleDcPath() exists to avoid -- measured -21 dBFS on the first
            // block. Added here, the oversampler never sees DC at all.
            //
            // The clamp only keeps the lookup tables in range; tanh is flat to
            // within 1e-9 out there, so it is audibly inert.
            for (int i = 0; i < osSamples; ++i)
                samples[i] = juce::jlimit (-shaperInputRange, shaperInputRange,
                                           samples[i] + stageABias);

#if BASSRIG_NAIVE_SHAPERS
            for (int i = 0; i < osSamples; ++i)
                samples[i] = std::tanh (samples[i]);
#else
            stageA.process (samples, samples, osSamples, static_cast<int> (ch));
#endif

        }

        // Coupling capacitor: strips the DC that the asymmetry just created, so
        // stage B stays centred on its own operating point.
        couplingFilter.process (osContext);

        // Stage B: firmer polynomial knee, closer to a CMOS inverter squaring
        // off than to a brick-wall hard clip.
        for (size_t ch = 0; ch < osChannels; ++ch)
        {
            auto* samples = upsampled.getChannelPointer (ch);

            for (int i = 0; i < osSamples; ++i)
                samples[i] *= interstageGain;

#if BASSRIG_NAIVE_SHAPERS
            for (int i = 0; i < osSamples; ++i)
            {
                constexpr float nf = 0.8f, inf = 1.25f;   // degree 5, matching chowdsp
                const auto xn = samples[i] * nf;
                samples[i] = std::abs (xn) > 1.0f ? (xn > 0.0f ? 1.0f : -1.0f)
                                                  : (xn - std::pow (xn, 5.0f) / 5.0f) * inf;
            }
#else
            stageB.process (samples, samples, osSamples, static_cast<int> (ch));
#endif
        }

        // Stage B generates DC of its own, which is easy to talk yourself out
        // of: it is an odd function, so a DC-free input should give a DC-free
        // output. But DC-free is not the same as symmetric. Stage A hands it a
        // zero-mean waveform whose two halves have different shapes, and
        // clipping that asymmetrically leaves a constant offset -- measured at
        // -52 dBFS with no blocker here. Same 5 Hz corner as the coupling
        // filter, low enough to stay phase transparent.
        outputDcBlocker.process (osContext);

        recoveryLpf.process (osContext);
        oversampler->processSamplesDown (block);
    }

    // ---- clean delay, then blend ----------------------------------------
    // Both paths now carry identical latency, so the crossfade is phase
    // coherent. Guarded by BlendLatencyTests.
    for (size_t ch = 0; ch < numChannels; ++ch)
    {
        auto* cleanSamples = clean.getChannelPointer (ch);

        for (size_t i = 0; i < numSamples; ++i)
        {
            cleanDelay.pushSample (static_cast<int> (ch), cleanSamples[i]);
            cleanSamples[i] = cleanDelay.popSample (static_cast<int> (ch));
        }
    }

    for (size_t i = 0; i < numSamples; ++i)
    {
        const auto blend  = blendAmount.getNextValue();
        const auto level  = levelGain.getNextValue();
        const auto makeup = makeupGain.getNextValue();

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            const auto dry = clean.getSample (static_cast<int> (ch), static_cast<int> (i));
            const auto wet = block.getSample (static_cast<int> (ch), static_cast<int> (i));

            block.setSample (static_cast<int> (ch), static_cast<int> (i),
                             dry * (1.0f - blend) + wet * makeup * level * blend);
        }
    }

    toneStack.process (block);
    block.multiplyBy (masterGain);
}
