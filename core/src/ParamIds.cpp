// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/ParamIds.h"

#include <algorithm>
#include <cassert>

#include <clap/ext/params.h>

#include "sst/basic-blocks/params/ParamMetadata.h"

namespace gg::core
{
namespace
{
using md_t = sst::basic_blocks::params::ParamMetaData;

constexpr uint32_t kAutomatableModulatable = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;

// Reverb Decay's "∞" label covers |v - kDecayMax| < tolerance × (kDecayMax - (-2)), in float, as
// ParamMetaData tests it. 2% of the travel, the ∞ region, nudged up by a few ulp: at exactly 0.02
// the float kDecayInfinite (rounded down from log2 60) would fall a hair outside. With this
// tolerance the label starts at kDecayInfinite and not a float below, the same boundary as
// SpaceParams::infinite; patch_tests.cpp checks it float by float.
constexpr float kDecayInfiniteLabelTolerance = 0.02000007f;

// The instrument's Position reads "100%", the file's end, over (1, kPositionMax], where
// ParamMetaData tests |v - kPositionMax| < tolerance × kPositionMax in float. This tolerance times
// kPositionMax rounds to exactly kPositionMax - 1, so 1 itself shows "100.00 %" and the float above
// it "100%"; parammap_tests.cpp checks the boundary.
constexpr float kPositionEndLabelTolerance = 0.34552154f;

// log2 seconds: 300 s, the longest ring (kRingMaxSeconds), the top of Ring Length and Loop Length;
// 50 ms (kLoopMinSeconds), the bottom of Loop Length (M2 sub-project 2's spec §6.3).
constexpr float kLog2RingMaxSeconds = 8.228818690495881f;
constexpr float kLog2LoopMinSeconds = -4.321928094887363f;

// Every entry starts here. ParamMetaData's quanta-only mode, a thread-local that a
// default-constructed ParamMetaData copies, would drop the names and display text; this table
// always carries them, whichever thread builds it.
md_t fresh()
{
    md_t m;
    m.quantaOnly = false;
    return m.withFlags(kAutomatableModulatable);
}

md_t base(ParamSlot slot) { return fresh().withID(paramId(slot)); }

// The layers' displayed names: "A Position" ... "D Quality" (spec §6.9).
#define GG_LAYER_NAME_A(name, index, display) "A " display,
#define GG_LAYER_NAME_B(name, index, display) "B " display,
#define GG_LAYER_NAME_C(name, index, display) "C " display,
#define GG_LAYER_NAME_D(name, index, display) "D " display,
constexpr std::array<std::array<const char *, kLayerParamCount>, kLayers> kLayerNames{{
    {GG_LAYER_PARAMS(GG_LAYER_NAME_A)},
    {GG_LAYER_PARAMS(GG_LAYER_NAME_B)},
    {GG_LAYER_PARAMS(GG_LAYER_NAME_C)},
    {GG_LAYER_PARAMS(GG_LAYER_NAME_D)},
}};
#undef GG_LAYER_NAME_A
#undef GG_LAYER_NAME_B
#undef GG_LAYER_NAME_C
#undef GG_LAYER_NAME_D

constexpr std::array<const char *, kLayers> kLayerModules{"Layer A", "Layer B", "Layer C",
                                                          "Layer D"};

// log2 Hz: 0.01 Hz (log2 0.01 = -6.643856...) to 50 Hz (5.643856...), shown in Hz as Density is:
// the modulators' rates (spec §6.9).
md_t rateHz(float defaultLog2Hz)
{
    return fresh()
        .asFloat()
        .withRange(-6.643856189774724f, 5.643856189774724f)
        .withDefault(defaultLog2Hz)
        .withATwoToTheBFormatting(1.f, 1.f, "Hz");
}

// log2 seconds: 1 ms (-9.965784...) to 10 s (3.321928...), shown in ms below 1 s: the envelopes'
// times (spec §6.9).
md_t envelopeTime(float defaultLog2Seconds)
{
    return fresh()
        .asLog2SecondsRange(-9.965784284662087f, 3.321928094887362f, defaultLog2Seconds)
        .withMilisecondsBelowOneSecond();
}

// An LFO's shape, stepped 0..4 (LfoShape).
md_t lfoShape(LfoShape defaultShape)
{
    return fresh()
        .asInt()
        .withRange(0.f, 4.f)
        .withDefault(static_cast<float>(defaultShape))
        .withUnorderedMapFormatting(
            {{0, "Sine"}, {1, "Triangle"}, {2, "Saw Up"}, {3, "Saw Down"}, {4, "Square"}})
        .withFlags(kAutomatableModulatable | CLAP_PARAM_IS_STEPPED);
}

// A layer parameter's metadata in `mode`'s product, the same in every layer, but for its ID and
// name. The bounds and defaults are literals: none is computed with libm (INTENT §14).
md_t layerMeta(LayerParam p, Mode mode)
{
    if (p >= LayerParam::Step1 && p <= LayerParam::Step16)
    {
        // the sequencer's steps, 0..1.
        return fresh().asPercent().withDefault(0.0f);
    }
    switch (p)
    {
    case LayerParam::Position:
        // 0..kPositionMax in both products, so the state format is shared (M2 sub-project 2's
        // spec §6.3: the top reaches the longest ring).
        // The instrument's (sub-project 3's spec §6.8): the fraction of the loaded file, shown as
        // 0–100% and "100%" above 1, where a Sample layer's fraction stops; 0% by default.
        // The FX's: seconds behind the write head = (16/255)(2^(8v) - 1), 0 to 300 s; 0.25 s by
        // default. Global Constraints names withATwoToTheBPlusCFormatting(16/255, 8, -16/255),
        // but at our sst-basic-blocks pin that puts C in the exponent (A 2^(Bv + C)); the
        // A 2^(Bv + C) + D form with C = 0 and D = -A displays the mapping above.
        return mode == Mode::Instrument
                   ? fresh()
                         .asPercent()
                         .withRange(0.f, kPositionMax)
                         .withDefault(0.0f)
                         .withCustomValueDisplay("100%", kPositionMax, kPositionEndLabelTolerance)
                   : fresh()
                         .asFloat()
                         .withRange(0.f, kPositionMax)
                         .withDefault(0.28967658f)
                         .withATwoToTheBPlusCPlusDFormatting(16.f / 255.f, 8.f, 0.f, -16.f / 255.f,
                                                             "s");

    case LayerParam::Size:
        // log2 seconds: 5 ms (-7.643856...) to 2 s (1), 150 ms by default.
        return fresh()
            .asLog2SecondsRange(-7.643856189774724f, 1.f, -2.736965594166206f)
            .withMilisecondsBelowOneSecond();

    case LayerParam::Density:
        // log2 grains per second: 0.25 Hz (-2) to 400 Hz (8.643856...), 16 Hz by default.
        return fresh()
            .asFloat()
            .withRange(-2.f, 8.643856189774725f)
            .withDefault(4.0f)
            .withATwoToTheBFormatting(1.f, 1.f, "Hz");

    case LayerParam::Distribution:
        // regular (0) to random (1).
        return fresh().asPercent().withDefault(0.5f);

    case LayerParam::Pitch:
        // semitones.
        return fresh().asSemitoneRange(-24.f, 24.f).withDefault(0.0f);

    case LayerParam::Spray:
        // 0..1; read-position spread in seconds = (2/255)(2^(8v) - 1), 0 to 2 s; 50 ms by
        // default. The D form for the reason given at Position.
        return fresh()
            .asFloat()
            .withRange(0.f, 1.f)
            .withDefault(0.36033038f)
            .withATwoToTheBPlusCPlusDFormatting(2.f / 255.f, 8.f, 0.f, -2.f / 255.f, "s");

    case LayerParam::Stereo:
        // per-grain pan spread, 0..1.
        return fresh().asPercent().withDefault(0.5f);

    case LayerParam::Freeze:
        // off (0) / on (1): frozen when v > 0.5.
        return fresh().asOnOffBool().withDefault(0.0f).withFlags(kAutomatableModulatable |
                                                                 CLAP_PARAM_IS_STEPPED);

    case LayerParam::Texture:
        // The Perform UI (sub-project 5's spec §3.3, §4, §5): the grain envelope's shape, 0..1;
        // 0 is the Hann window.
        return fresh().asPercent().withDefault(0.0f);

    case LayerParam::Feedback:
        // the limited wet signal fed back into the live ring, 0..kFeedbackMax, shown as %.
        return fresh().asPercent().withRange(0.f, kFeedbackMax).withDefault(0.0f);

    case LayerParam::Diffusion:
        // Space (sub-project 4's spec §6.5): the diffuser's crossfade, 0..1.
        return fresh().asPercent().withDefault(0.0f);

    // M2 sub-project 1's spec §6.9.
    case LayerParam::Source:
        // what the grains read, stepped 0..4 (SourceKind): Live in the FX, Sample in the
        // instrument.
        return fresh()
            .asInt()
            .withRange(0.f, 4.f)
            .withDefault(mode == Mode::Instrument ? 0.0f : 1.0f)
            .withUnorderedMapFormatting(
                {{0, "Sample"}, {1, "Live"}, {2, "Resample"}, {3, "Spectral"}, {4, "Oscillator"}})
            .withFlags(kAutomatableModulatable | CLAP_PARAM_IS_STEPPED);

    case LayerParam::PitchSpread:
        // each grain's pitch spread, 0 to 24 semitones (D4).
        return fresh().asFloat().withRange(0.f, 24.f).withDefault(0.0f).withLinearScaleFormatting(
            "st");

    case LayerParam::SizeSpread:
        // each grain's size spread, 0 to 2 octaves (D5).
        return fresh().asFloat().withRange(0.f, 2.f).withDefault(0.0f).withLinearScaleFormatting(
            "oct");

    case LayerParam::Lfo1Rate:
        return rateHz(0.0f); // 1 Hz
    case LayerParam::Lfo1Shape:
        return lfoShape(LfoShape::Sine);
    case LayerParam::Lfo2Rate:
        return rateHz(-2.0f); // 0.25 Hz
    case LayerParam::Lfo2Shape:
        return lfoShape(LfoShape::Triangle);

    case LayerParam::Env1Attack:
        return envelopeTime(-6.643856189774724f); // 10 ms
    case LayerParam::Env1Decay:
        return envelopeTime(-2.321928094887362f); // 200 ms
    case LayerParam::Env1Sustain:
        return fresh().asPercent().withDefault(1.0f);
    case LayerParam::Env1Release:
        return envelopeTime(-2.321928094887362f); // 200 ms
    case LayerParam::Env2Attack:
        return envelopeTime(-1.0f); // 500 ms
    case LayerParam::Env2Decay:
        return envelopeTime(0.0f); // 1 s
    case LayerParam::Env2Sustain:
        return fresh().asPercent().withDefault(0.5f);
    case LayerParam::Env2Release:
        return envelopeTime(0.0f); // 1 s

    case LayerParam::Random1Rate:
        return rateHz(1.0f); // 2 Hz
    case LayerParam::Random1Smooth:
        return fresh().asPercent().withDefault(0.0f);
    case LayerParam::Random2Rate:
        return rateHz(-1.0f); // 0.5 Hz
    case LayerParam::Random2Smooth:
        return fresh().asPercent().withDefault(0.5f);

    case LayerParam::StepRate:
        return rateHz(2.0f); // 4 Hz
    case LayerParam::StepLength:
        // the sequencer's length, stepped 1..16.
        return fresh()
            .asInt()
            .withRange(1.f, 16.f)
            .withDefault(16.0f)
            .withDimensionlessFormatting()
            .withFlags(kAutomatableModulatable | CLAP_PARAM_IS_STEPPED);

    // M2 sub-project 2's spec §6.3.
    case LayerParam::RingLength:
        // log2 seconds: 1 s (0) to 300 s, 16 s by default. Neither automatable nor modulatable:
        // the plugin's main thread resizes the ring (spec §5), so no host may move it per block.
        return fresh().asLog2SecondsRange(0.f, kLog2RingMaxSeconds, 4.0f).withFlags(0u);
    case LayerParam::Loop:
        // off (0) / on (1): the window loops when v > 0.5.
        return fresh().asOnOffBool().withDefault(0.0f).withFlags(kAutomatableModulatable |
                                                                 CLAP_PARAM_IS_STEPPED);
    case LayerParam::LoopLength:
        // log2 seconds: 50 ms to 300 s, shown in ms below 1 s; 1 s by default.
        return fresh().asLog2SecondsRange(kLog2LoopMinSeconds, kLog2RingMaxSeconds, 0.0f);
    case LayerParam::Overdub:
        // the gain o of what the ring held, 0..1.
        return fresh().asPercent().withDefault(0.0f);
    case LayerParam::Splice:
        // stepped 0..32 (kMaxSplices): 0 is the whole window.
        return fresh()
            .asInt()
            .withRange(0.f, 32.f)
            .withDefault(0.0f)
            .withDimensionlessFormatting()
            .withFlags(kAutomatableModulatable | CLAP_PARAM_IS_STEPPED);

    // M2 sub-project 3's spec §6.1, §6.8.
    case LayerParam::Quality:
        // the grains' interpolation tier, stepped 0..2 (gg::Tier): Normal, M1's, by default.
        return fresh()
            .asInt()
            .withRange(0.f, 2.f)
            .withDefault(1.0f)
            .withUnorderedMapFormatting({{0, "Eco"}, {1, "Normal"}, {2, "HQ"}})
            .withFlags(kAutomatableModulatable | CLAP_PARAM_IS_STEPPED);

    default:
        break; // the steps, above; LayerParam::Count isn't a parameter
    }
    return fresh();
}

// Global Constraints' parameter table, for `mode`'s product. The bounds and defaults are
// literals: none is computed with libm (INTENT §14).
std::array<md_t, kParamCount> buildTable(Mode mode)
{
    std::array<md_t, kParamCount> t{};
    auto at = [&t](ParamSlot s) -> md_t & { return t[static_cast<size_t>(s)]; };

    // The layer parameters, the same in every layer but for the letter in their names.
    for (uint8_t layer = 0; layer < kLayers; ++layer)
    {
        for (size_t i = 0; i < kLayerParamCount; ++i)
        {
            const auto p = static_cast<LayerParam>(i);
            const ParamSlot slot = layerSlot(p, layer);
            at(slot) = layerMeta(p, mode).withID(paramId(slot)).withName(kLayerNames[layer][i]);
        }
    }

    // dry/wet, 0..1.
    at(ParamSlot::Mix) = base(ParamSlot::Mix).withName("Mix").asPercent().withDefault(0.5f);

    // wet gain in dB: layer A's, and layers B to D's (spec §6.9).
    at(ParamSlot::Level) =
        base(ParamSlot::Level).withName("Level").asDecibelWithRange(-60.f, 12.f, 0.f);
    at(ParamSlot::LevelB) =
        base(ParamSlot::LevelB).withName("Level B").asDecibelWithRange(-60.f, 12.f, 0.f);
    at(ParamSlot::LevelC) =
        base(ParamSlot::LevelC).withName("Level C").asDecibelWithRange(-60.f, 12.f, 0.f);
    at(ParamSlot::LevelD) =
        base(ParamSlot::LevelD).withName("Level D").asDecibelWithRange(-60.f, 12.f, 0.f);

    // Space (sub-project 4's spec §6.5).
    // the reverb return's gain, 0..1.
    at(ParamSlot::ReverbAmount) =
        base(ParamSlot::ReverbAmount).withName("Reverb Amount").asPercent().withDefault(0.0f);
    // log2 seconds: 0.25 s (-2) to 60 s (kDecayInfinite), then ∞ up to kDecayMax; 2 s by
    // default. Values below 1 s show in ms, but a bare number typed in is seconds ("500 ms"
    // still parses).
    at(ParamSlot::ReverbDecay) =
        base(ParamSlot::ReverbDecay)
            .withName("Reverb Decay")
            .asLog2SecondsRange(-2.f, kDecayMax, 1.0f)
            .withAlternateAsDefaultFromStringUnit(false)
            .withCustomValueDisplay("∞", kDecayMax, kDecayInfiniteLabelTolerance);

    // Reverb 2's room size is 2v - 1; 0..1 here, 50% by default.
    at(ParamSlot::ReverbSize) =
        base(ParamSlot::ReverbSize).withName("Reverb Size").asPercent().withDefault(0.5f);

    // Reverb 2's HF damping, 0..1.
    at(ParamSlot::ReverbDamping) =
        base(ParamSlot::ReverbDamping).withName("Reverb Damping").asPercent().withDefault(0.3f);

    // seconds, 0 to 0.5, linear; every value is below 1 s, so it shows in ms (and a bare number
    // typed in is ms).
    at(ParamSlot::ReverbPredelay) = base(ParamSlot::ReverbPredelay)
                                        .withName("Reverb Pre-delay")
                                        .asFloat()
                                        .withRange(0.f, 0.5f)
                                        .withDefault(0.0f)
                                        .withLinearScaleFormatting("s")
                                        .withMilisecondsBelowOneSecond();

    // Reverb 2's modulation depth, 0..1.
    at(ParamSlot::ReverbModulation) = base(ParamSlot::ReverbModulation)
                                          .withName("Reverb Modulation")
                                          .asPercent()
                                          .withDefault(0.25f);

    // the FX's input sent to the reverb, 0..1. The instrument keeps it, hidden and with no
    // effect, as it does Mix.
    at(ParamSlot::DryToReverb) =
        base(ParamSlot::DryToReverb).withName("Dry to Reverb").asPercent().withDefault(0.0f);

    // The eight macros, 0..1 each: at rest (0.5 for a bipolar one, 0 for a unipolar one) a macro
    // adds nothing to its targets. Macro 6 is a switch, like Freeze; Macro 8 differs by product
    // (spec §6.9).
    at(ParamSlot::Macro1) =
        base(ParamSlot::Macro1).withName("Macro Size").asPercent().withDefault(0.5f);
    at(ParamSlot::Macro2) =
        base(ParamSlot::Macro2).withName("Macro Density").asPercent().withDefault(0.5f);
    at(ParamSlot::Macro3) =
        base(ParamSlot::Macro3).withName("Macro Pitch").asPercent().withDefault(0.5f);
    at(ParamSlot::Macro4) =
        base(ParamSlot::Macro4).withName("Macro Spray").asPercent().withDefault(0.5f);
    at(ParamSlot::Macro5) =
        base(ParamSlot::Macro5).withName("Macro Texture").asPercent().withDefault(0.0f);
    at(ParamSlot::Macro6) = base(ParamSlot::Macro6)
                                .withName("Macro Freeze")
                                .asOnOffBool()
                                .withDefault(0.0f)
                                .withFlags(kAutomatableModulatable | CLAP_PARAM_IS_STEPPED);
    at(ParamSlot::Macro7) =
        base(ParamSlot::Macro7).withName("Macro Feedback").asPercent().withDefault(0.0f);
    at(ParamSlot::Macro8) = base(ParamSlot::Macro8)
                                .withName(mode == Mode::Instrument ? "Macro Level" : "Macro Mix")
                                .asPercent()
                                .withDefault(0.5f);

    // The layers' globals (spec §6.9).
    // the morph pad's position, 0..1 each: (0.5, 0.5) weighs the rendering layers equally.
    at(ParamSlot::MorphX) =
        base(ParamSlot::MorphX).withName("Morph X").asPercent().withDefault(0.5f);
    at(ParamSlot::MorphY) =
        base(ParamSlot::MorphY).withName("Morph Y").asPercent().withDefault(0.5f);
    // the layers' switches: layer A on, B to D off.
    for (uint8_t layer = 0; layer < kLayers; ++layer)
    {
        static constexpr std::array<const char *, kLayers> kNames{"Layer A On", "Layer B On",
                                                                  "Layer C On", "Layer D On"};
        const ParamSlot slot = layerOnSlot(layer);
        at(slot) = base(slot)
                       .withName(kNames[layer])
                       .asOnOffBool()
                       .withDefault(layer == 0 ? 1.0f : 0.0f)
                       .withFlags(kAutomatableModulatable | CLAP_PARAM_IS_STEPPED);
    }

    // M2 sub-project 3's spec §6.4, §6.8: the count of Normal stereo grains the rendering layers
    // share, stepped 256..16,384; 4,096 by default, in both products.
    at(ParamSlot::Budget) = base(ParamSlot::Budget)
                                .withName("Budget")
                                .asInt()
                                .withRange(256.f, 16384.f)
                                .withDefault(4096.0f)
                                .withFlags(kAutomatableModulatable | CLAP_PARAM_IS_STEPPED);
    return t;
}
} // namespace

const char *paramModule(ParamSlot slot) noexcept
{
    const auto ref = layerOf(slot);
    return ref ? kLayerModules[ref->layer] : "";
}

const sst::basic_blocks::params::ParamMetaData &paramMeta(ParamSlot slot, Mode mode)
{
    static const std::array<md_t, kParamCount> instrument = buildTable(Mode::Instrument);
    static const std::array<md_t, kParamCount> fx = buildTable(Mode::Fx);
    const auto i = static_cast<size_t>(slot);
    assert(i < kParamCount);
    return (mode == Mode::Instrument ? instrument : fx)[std::min(i, kParamCount - 1)];
}
} // namespace gg::core
