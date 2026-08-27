#include "ToneStack.h"

#include <array>

namespace
{
    // Measured, not guessed. The Baxandall network is passive, so it loses
    // level even at the flat setting: -20.98 dB at 1 kHz.
    constexpr float baxandallFlatMakeup = 20.98f; // dB, referenced at 1 kHz

    constexpr float midQ = 0.9f;   // broad enough to be a tone control, not a notch

    // Knob position to pot position.
    //
    // Two things happen here, both load-bearing. The pot runs backwards
    // relative to the knob, hence the inversion. And the network's electrically
    // flat point is NOT at pot 0.5 -- measured, centring the pot linearly gives
    // +6.5 dB at 40 Hz relative to 1 kHz, so a knob at noon would not be
    // neutral. The cube-ish skew puts flat near pot 0.90, which is where this
    // circuit actually sits flat. Taken from the upstream BaxandallEQ example.
    //
    // The clamp matters too: at exactly 0 or 1 a pot resistance goes to zero,
    // the R-type adaptor's scattering matrix degenerates, and the stack outputs
    // silence. That is a silent failure, so it is guarded here rather than
    // trusted to the parameter range.
    // Knob position to pot position, as a measured table rather than a formula.
    //
    // Three things make this circuit awkward to map by hand. The pot runs
    // backwards relative to the knob. The network is electrically flat at pot
    // 0.90, not 0.50, so a knob at noon would not be neutral without a skew.
    // And decibels are steeply nonlinear in pot position, differently for each
    // control: bass spans -6.6..+14.9 dB and treble -3.9..+11.0 dB, with most
    // of the change crammed against the far end of the travel.
    //
    // Successive power-law tapers were fitted and all of them failed the same
    // way. Linearising decibels needs an exponent below 1, and any exponent
    // below 1 has infinite slope at the centre detent: the best fit moved the
    // bass control 1.1 dB within one percent of knob travel just above noon,
    // and 4.8 dB at the exponent the optimiser actually wanted. Sampling at
    // quarter points hides that completely, which is how it survived the first
    // attempt.
    //
    // So the tables below are the exact inverse of the measured curve: each
    // half of the travel spreads its own available range linearly in decibels,
    // calibrated at 60 Hz for bass and 10 kHz for treble. Deviation from linear
    // is 0.00 dB and there is no singularity anywhere.
    //
    // Endpoints stop just short of 0 and 1 because a pot resistance of exactly
    // zero degenerates the R-type adaptor and the stack outputs silence.

    constexpr std::array<float, 65> bassTaper
    {
        0.9900f, 0.9889f, 0.9878f, 0.9867f, 0.9855f, 0.9842f,
        0.9830f, 0.9816f, 0.9803f, 0.9788f, 0.9772f, 0.9756f,
        0.9740f, 0.9723f, 0.9705f, 0.9686f, 0.9665f, 0.9644f,
        0.9622f, 0.9598f, 0.9572f, 0.9545f, 0.9515f, 0.9483f,
        0.9449f, 0.9412f, 0.9371f, 0.9326f, 0.9276f, 0.9220f,
        0.9156f, 0.9083f, 0.9000f, 0.8576f, 0.8152f, 0.7728f,
        0.7171f, 0.6515f, 0.5921f, 0.5398f, 0.4937f, 0.4527f,
        0.4157f, 0.3821f, 0.3516f, 0.3237f, 0.2978f, 0.2735f,
        0.2510f, 0.2300f, 0.2099f, 0.1914f, 0.1736f, 0.1567f,
        0.1408f, 0.1254f, 0.1110f, 0.0968f, 0.0835f, 0.0704f,
        0.0578f, 0.0456f, 0.0335f, 0.0218f, 0.0100f,
    };

    constexpr std::array<float, 65> trebleTaper
    {
        0.9900f, 0.9896f, 0.9891f, 0.9887f, 0.9882f, 0.9876f,
        0.9871f, 0.9865f, 0.9860f, 0.9853f, 0.9845f, 0.9838f,
        0.9830f, 0.9820f, 0.9810f, 0.9800f, 0.9788f, 0.9774f,
        0.9761f, 0.9744f, 0.9727f, 0.9706f, 0.9685f, 0.9658f,
        0.9628f, 0.9594f, 0.9553f, 0.9503f, 0.9444f, 0.9370f,
        0.9278f, 0.9158f, 0.9000f, 0.8040f, 0.6747f, 0.5145f,
        0.4000f, 0.3210f, 0.2650f, 0.2232f, 0.1911f, 0.1659f,
        0.1450f, 0.1279f, 0.1139f, 0.1018f, 0.0907f, 0.0825f,
        0.0744f, 0.0662f, 0.0605f, 0.0555f, 0.0505f, 0.0455f,
        0.0405f, 0.0361f, 0.0332f, 0.0303f, 0.0274f, 0.0245f,
        0.0216f, 0.0187f, 0.0158f, 0.0129f, 0.0100f,
    };

    float lookupPot (float knob, const std::array<float, 65>& table) noexcept
    {
        const auto position = juce::jlimit (0.0f, 1.0f, knob) * (float) (table.size() - 1);
        const auto index    = (size_t) position;

        if (index + 1 >= table.size())
            return table.back();

        return table[index] + (position - (float) index) * (table[index + 1] - table[index]);
    }

    float bassKnobToPot (float knob) noexcept   { return lookupPot (knob, bassTaper); }
    float trebleKnobToPot (float knob) noexcept { return lookupPot (knob, trebleTaper); }

    // Re-solving the WDF tree recomputes a six-port scattering matrix, so doing
    // it per sample the way the upstream example does is far too expensive.
    // Doing it once per block instead would step the response audibly while a
    // knob moves. This is the middle: re-solve every 32 samples, and only while
    // a control is actually in motion. Static knobs cost nothing, which is the
    // case that matters.
    constexpr int potUpdateInterval = 32;
}

void BaxandallWDF::prepare (double fs)
{
    Ca.prepare ((float) fs);
    Cb.prepare ((float) fs);
    Cc.prepare ((float) fs);
    Cd.prepare ((float) fs);
    Ce.prepare ((float) fs);
}

void BaxandallWDF::reset()
{
    Ca.reset();
    Cb.reset();
    Cc.reset();
    Cd.reset();
    Ce.reset();
}

void BaxandallWDF::setParams (float bassParam, float trebleParam)
{
    {
        // Defer impedance propagation until every pot has been written,
        // otherwise the tree is re-solved once per component instead of once
        // per change.
        chowdsp::wdft::ScopedDeferImpedancePropagation deferImpedance { P1, S2, S3, S4 };

        Pb_plus.setResistanceValue (Pb * bassParam);
        Pb_minus.setResistanceValue (Pb * (1.0f - bassParam));

        Pt_plus.setResistanceValue (Pt * trebleParam);
        Pt_minus.setResistanceValue (Pt * (1.0f - trebleParam));
    }

    R.propagateImpedanceChange();
}

//==============================================================================
void ToneStack::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate  = spec.sampleRate;
    numChannels = juce::jlimit (1, maxChannels, static_cast<int> (spec.numChannels));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        baxandall[(size_t) ch].prepare (sampleRate);
        loMid[(size_t) ch].prepare (1);
        hiMid[(size_t) ch].prepare (1);
    }

    // Smoothing runs on the pot value rather than the knob value, so the WDF
    // sees a continuous sweep of resistances.
    bassPot.reset (sampleRate, 0.05);
    treblePot.reset (sampleRate, 0.05);

    snapOnNextUpdate = true;
    currentLoMidDb = currentHiMidDb = std::numeric_limits<float>::quiet_NaN();
    currentLoMidFreq = currentHiMidFreq = -1;

    reset();
}

void ToneStack::reset()
{
    for (int ch = 0; ch < numChannels; ++ch)
    {
        baxandall[(size_t) ch].reset();
        loMid[(size_t) ch].reset();
        hiMid[(size_t) ch].reset();
    }
}

void ToneStack::setParams (float bassKnob, float trebleKnob,
                           float loMidDb, int loMidFreqIndex,
                           float hiMidDb, int hiMidFreqIndex)
{
    bassPot.setTargetValue (bassKnobToPot (bassKnob));
    treblePot.setTargetValue (trebleKnobToPot (trebleKnob));

    if (snapOnNextUpdate)
    {
        bassPot.setCurrentAndTargetValue (bassPot.getTargetValue());
        treblePot.setCurrentAndTargetValue (treblePot.getTargetValue());
        snapOnNextUpdate = false;
        applyPots();
    }

    // Peaking coefficients are cheap but not free, and only change when the
    // user moves something.
    if (loMidDb != currentLoMidDb || loMidFreqIndex != currentLoMidFreq)
    {
        currentLoMidDb   = loMidDb;
        currentLoMidFreq = loMidFreqIndex;

        const auto hz = Params::loMidCentreHz[(size_t) juce::jlimit (0, 2, loMidFreqIndex)];

        for (int ch = 0; ch < numChannels; ++ch)
            loMid[(size_t) ch].calcCoefsDB (hz, midQ, loMidDb, (float) sampleRate);
    }

    if (hiMidDb != currentHiMidDb || hiMidFreqIndex != currentHiMidFreq)
    {
        currentHiMidDb   = hiMidDb;
        currentHiMidFreq = hiMidFreqIndex;

        const auto hz = Params::hiMidCentreHz[(size_t) juce::jlimit (0, 2, hiMidFreqIndex)];

        for (int ch = 0; ch < numChannels; ++ch)
            hiMid[(size_t) ch].calcCoefsDB (hz, midQ, hiMidDb, (float) sampleRate);
    }
}

void ToneStack::applyPots()
{
    const auto bass   = bassPot.getCurrentValue();
    const auto treble = treblePot.getCurrentValue();

    for (int ch = 0; ch < numChannels; ++ch)
        baxandall[(size_t) ch].setParams (bass, treble);
}

void ToneStack::process (juce::dsp::AudioBlock<float>& block)
{
    const auto blockChannels = juce::jmin ((int) block.getNumChannels(), numChannels);
    const auto numSamples    = (int) block.getNumSamples();
    const auto makeup        = juce::Decibels::decibelsToGain (baxandallFlatMakeup);

    for (int pos = 0; pos < numSamples; pos += potUpdateInterval)
    {
        const auto n = juce::jmin (potUpdateInterval, numSamples - pos);

        if (bassPot.isSmoothing() || treblePot.isSmoothing())
        {
            bassPot.skip (n);
            treblePot.skip (n);
            applyPots();
        }

        for (int ch = 0; ch < blockChannels; ++ch)
        {
            auto* samples = block.getChannelPointer ((size_t) ch) + pos;
            auto& bax     = baxandall[(size_t) ch];

            for (int i = 0; i < n; ++i)
                samples[i] = bax.processSample (samples[i]) * makeup;
        }
    }

    for (int ch = 0; ch < blockChannels; ++ch)
    {
        auto* samples = block.getChannelPointer ((size_t) ch);
        loMid[(size_t) ch].processBlock (samples, numSamples);
        hiMid[(size_t) ch].processBlock (samples, numSamples);
    }
}
