#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Every parameter ID in one place. IDs are part of the saved state and of the
// host's automation lanes, so they are frozen once shipped -- rename a control
// in the UI freely, never rename it here.
namespace ParamID
{
    inline constexpr auto trim       = "trim";
    inline constexpr auto drive      = "drive";
    inline constexpr auto blend      = "blend";
    inline constexpr auto level      = "level";
    inline constexpr auto master     = "master";

    inline constexpr auto grunt      = "grunt";      // bass content into the clipper
    inline constexpr auto attack     = "attack";     // treble pre-emphasis

    inline constexpr auto bass       = "bass";
    inline constexpr auto loMid      = "loMid";
    inline constexpr auto hiMid      = "hiMid";
    inline constexpr auto treble     = "treble";
    inline constexpr auto loMidFreq  = "loMidFreq";
    inline constexpr auto hiMidFreq  = "hiMidFreq";

    inline constexpr auto bypass     = "bypass";

    // Compressor. Prefixed so a second pedal can never collide with the first,
    // which is the whole reason these live in one list.
    inline constexpr auto compOn        = "compOn";
    inline constexpr auto compThreshold = "compThreshold";
    inline constexpr auto compRatio     = "compRatio";
    inline constexpr auto compAttack    = "compAttack";
    inline constexpr auto compRelease   = "compRelease";
    inline constexpr auto compMakeup    = "compMakeup";
    inline constexpr auto compSidechain = "compSidechain";

    // Envelope filter.
    inline constexpr auto envOn       = "envOn";
    inline constexpr auto envSens     = "envSens";
    inline constexpr auto envAttack   = "envAttack";
    inline constexpr auto envRelease  = "envRelease";
    inline constexpr auto envQ        = "envQ";
    inline constexpr auto envRange    = "envRange";
    inline constexpr auto envDryHighs = "envDryHighs";
    inline constexpr auto envMode     = "envMode";

    // Chorus.
    inline constexpr auto chorusOn        = "chorusOn";
    inline constexpr auto chorusRate      = "chorusRate";
    inline constexpr auto chorusDepth     = "chorusDepth";
    inline constexpr auto chorusMix       = "chorusMix";
    inline constexpr auto chorusCrossover = "chorusCrossover";
    inline constexpr auto chorusMode      = "chorusMode";

    // Phaser.
    inline constexpr auto phaserOn       = "phaserOn";
    inline constexpr auto phaserRate     = "phaserRate";
    inline constexpr auto phaserDepth    = "phaserDepth";
    inline constexpr auto phaserFeedback = "phaserFeedback";
    inline constexpr auto phaserStages   = "phaserStages";
    inline constexpr auto phaserMix      = "phaserMix";
    inline constexpr auto phaserInvert   = "phaserInvert";
    inline constexpr auto phaserCrossover = "phaserCrossover";

    // Noise gate.
    inline constexpr auto gateOn        = "gateOn";
    inline constexpr auto gateThreshold = "gateThreshold";
    inline constexpr auto gateAttack    = "gateAttack";
    inline constexpr auto gateHold      = "gateHold";
    inline constexpr auto gateRelease   = "gateRelease";
    inline constexpr auto gateRange     = "gateRange";
    inline constexpr auto gateSidechain = "gateSidechain";

    // Octaver.
    inline constexpr auto octOn     = "octOn";
    inline constexpr auto octDirect = "octDirect";
    inline constexpr auto octSubOne = "octSubOne";
    inline constexpr auto octSubTwo = "octSubTwo";
    inline constexpr auto octTone   = "octTone";
    inline constexpr auto octTrack  = "octTrack";
    inline constexpr auto octGrowl  = "octGrowl";
}

namespace Params
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Choice-parameter orderings, shared by the DSP and the UI so the two can
    // never drift apart.
    // "Full" sits at 5 Hz, matching the coupling and output high-pass corners.
    // It must not rotate the drive path away from the clean path at the bottom
    // of the instrument's range, and once the two paths were level matched the
    // cost of a higher corner became measurable: at 10 Hz, blend coherence at
    // 30.9 Hz was 0.897, at 5 Hz it is 0.943. There is already a shared 20 Hz
    // DC blocker upstream of the split, so "Full" here is close to a formality.
    inline constexpr std::array<float, 3> gruntHighpassHz  { 5.0f, 100.0f, 250.0f };
    // Widened from +-6 dB after the drive taper was corrected. With the old taper,
    // drive 0.7 meant a gain of 10.3 and the cascade amplified the shelf; at the
    // measured gain of 2.4 the same shelf moved the top end only 0.9 dB in the cut
    // direction, which is not a control anyone would notice.
    inline constexpr std::array<float, 3> attackShelfDb    { 9.0f, 0.0f, -9.0f };
    inline constexpr std::array<float, 3> loMidCentreHz    { 250.0f, 500.0f, 1000.0f };
    inline constexpr std::array<float, 3> hiMidCentreHz    { 750.0f, 1500.0f, 3000.0f };

    // Index 0 is Off and is never used as a cutoff.
    inline constexpr std::array<float, 3> sidechainHighpassHz { 20.0f, 80.0f, 160.0f };

    // A bool whose value survives being saved.
    //
    // juce::AudioParameterBool stores whatever raw normalised float the host
    // hands it and only thresholds at 0.5 when read, so it can sit at 0.47
    // while reporting false. That is fine in itself. What is not fine is its
    // NormalisableRange, which is { 0, 1, 1 } -- interval 1, so denormalising
    // 0.47 snaps it to 0. APVTS saves the DENORMALISED value, so the 0.47 is
    // thrown away by the save and cannot come back.
    //
    // The host does not forget. A VST3 host caches the value it sent, so after
    // a save-and-restore its cache says 0.47 and the plugin says 0. pluginval's
    // state-restoration test is precisely this comparison, and it fails on any
    // draw further than 0.1 from a step -- about 80% of them.
    //
    // The fix is one character: a continuous range. Normalised and denormalised
    // then agree, the save is lossless, and everything downstream is unchanged.
    // The parameter still reports two steps and still reads as a boolean, so
    // hosts draw it as a switch, and get() thresholds exactly as before.
    //
    // Two other fixes were tried and both were wrong, in the same way -- they
    // treated the divergence rather than the lossy save. Forcing every
    // parameter to match the tree after replaceState only moved the mismatch;
    // snapping on assignment made the plugin silently alter what the host sent,
    // which is the same divergence with the sign flipped. See DESIGN.md 3p.
    //
    // Written out rather than subclassed: juce::AudioParameterBool declares
    // setValue, getValue and its backing float all private, and get() is
    // non-virtual, so a subclass cannot change this behaviour. This mirrors
    // that class with the range changed.
    class LosslessBool final : public juce::RangedAudioParameter
    {
    public:
        LosslessBool (const juce::ParameterID& parameterID, const juce::String& parameterName,
                      bool def)
            : juce::RangedAudioParameter (parameterID, parameterName),
              value (def ? 1.0f : 0.0f),
              valueDefault (def) {}

        bool get() const noexcept       { return value.load() >= 0.5f; }
        operator bool() const noexcept  { return get(); }

        const juce::NormalisableRange<float>& getNormalisableRange() const override
        {
            return range;
        }

    private:
        float getValue() const override        { return value.load(); }
        void setValue (float newValue) override { value.store (newValue); }

        float getDefaultValue() const override { return valueDefault ? 1.0f : 0.0f; }

        // Still two steps and still a boolean as far as the host is concerned.
        // Only the RANGE is continuous, and the range is used for nothing but
        // normalising and denormalising.
        int getNumSteps() const override       { return 2; }
        bool isDiscrete() const override       { return true; }
        bool isBoolean() const override        { return true; }

        juce::String getText (float v, int) const override
        {
            return v >= 0.5f ? TRANS ("On") : TRANS ("Off");
        }

        float getValueForText (const juce::String& text) const override
        {
            const auto lower = text.toLowerCase().trim();

            if (lower == "on" || lower == "yes" || lower == "true")
                return 1.0f;

            if (lower == "off" || lower == "no" || lower == "false")
                return 0.0f;

            return text.getIntValue() != 0 ? 1.0f : 0.0f;
        }

        // The whole fix: no interval, so convertFrom0to1 is the identity and
        // APVTS stores exactly what the host sent.
        inline static const juce::NormalisableRange<float> range { 0.0f, 1.0f };

        std::atomic<float> value;
        bool valueDefault;
    };

    std::unique_ptr<LosslessBool> boolParam (const char* id, const char* name, bool def);

    // Shared parameter constructors, so every pedal builds its controls the
    // same way.
    std::unique_ptr<juce::AudioParameterFloat> gainParam (const char* id, const char* name,
                                                          float min, float max, float def);
    std::unique_ptr<juce::AudioParameterFloat> knobParam (const char* id, const char* name);
    std::unique_ptr<juce::AudioParameterFloat> percentParam (const char* id, const char* name,
                                                             float def);
}
