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

// Every entry starts here. ParamMetaData's quanta-only mode, a thread-local that a
// default-constructed ParamMetaData copies, would drop the names and display text; this table
// always carries them, whichever thread builds it.
md_t base(ParamSlot slot)
{
    md_t m;
    m.quantaOnly = false;
    return m.withID(paramId(slot)).withFlags(kAutomatableModulatable);
}

// Global Constraints' parameter table, for `mode`'s product. The bounds and defaults are
// literals: none is computed with libm (INTENT §14).
std::array<md_t, kParamCount> buildTable(Mode mode)
{
    std::array<md_t, kParamCount> t{};
    auto at = [&t](ParamSlot s) -> md_t & { return t[static_cast<size_t>(s)]; };

    // dry/wet, 0..1.
    at(ParamSlot::Mix) = base(ParamSlot::Mix).withName("Mix").asPercent().withDefault(0.5f);

    // wet gain in dB.
    at(ParamSlot::Level) =
        base(ParamSlot::Level).withName("Level").asDecibelWithRange(-60.f, 12.f, 0.f);

    // The instrument's (sub-project 3's spec §6.8): 0..1, the fraction of the loaded file, shown
    // as 0–100%; 0% by default.
    // The FX's: 0..1; seconds behind the write head = (16/255)(2^(8v) - 1), 0 to 16 s; 0.25 s by
    // default. Global Constraints names withATwoToTheBPlusCFormatting(16/255, 8, -16/255), but at
    // our sst-basic-blocks pin that puts C in the exponent (A 2^(Bv + C)); the A 2^(Bv + C) + D
    // form with C = 0 and D = -A displays the mapping above.
    at(ParamSlot::Position) =
        mode == Mode::Instrument
            ? base(ParamSlot::Position).withName("Position").asPercent().withDefault(0.0f)
            : base(ParamSlot::Position)
                  .withName("Position")
                  .asFloat()
                  .withRange(0.f, 1.f)
                  .withDefault(0.28967658f)
                  .withATwoToTheBPlusCPlusDFormatting(16.f / 255.f, 8.f, 0.f, -16.f / 255.f, "s");

    // log2 seconds: 5 ms (-7.643856...) to 2 s (1), 150 ms by default.
    at(ParamSlot::Size) = base(ParamSlot::Size)
                              .withName("Size")
                              .asLog2SecondsRange(-7.643856189774724f, 1.f, -2.736965594166206f)
                              .withMilisecondsBelowOneSecond();

    // log2 grains per second: 0.25 Hz (-2) to 400 Hz (8.643856...), 16 Hz by default.
    at(ParamSlot::Density) = base(ParamSlot::Density)
                                 .withName("Density")
                                 .asFloat()
                                 .withRange(-2.f, 8.643856189774725f)
                                 .withDefault(4.0f)
                                 .withATwoToTheBFormatting(1.f, 1.f, "Hz");

    // regular (0) to random (1).
    at(ParamSlot::Distribution) =
        base(ParamSlot::Distribution).withName("Distribution").asPercent().withDefault(0.5f);

    // semitones.
    at(ParamSlot::Pitch) =
        base(ParamSlot::Pitch).withName("Pitch").asSemitoneRange(-24.f, 24.f).withDefault(0.0f);

    // 0..1; read-position spread in seconds = (2/255)(2^(8v) - 1), 0 to 2 s; 50 ms by default.
    // The D form for the reason given at Position.
    at(ParamSlot::Spray) =
        base(ParamSlot::Spray)
            .withName("Spray")
            .asFloat()
            .withRange(0.f, 1.f)
            .withDefault(0.36033038f)
            .withATwoToTheBPlusCPlusDFormatting(2.f / 255.f, 8.f, 0.f, -2.f / 255.f, "s");

    // per-grain pan spread, 0..1.
    at(ParamSlot::Stereo) =
        base(ParamSlot::Stereo).withName("Stereo").asPercent().withDefault(0.5f);

    // off (0) / on (1): frozen when v > 0.5.
    at(ParamSlot::Freeze) = base(ParamSlot::Freeze)
                                .withName("Freeze")
                                .asOnOffBool()
                                .withDefault(0.0f)
                                .withFlags(kAutomatableModulatable | CLAP_PARAM_IS_STEPPED);

    // Space (sub-project 4's spec §6.5).
    // the diffuser's crossfade, 0..1.
    at(ParamSlot::Diffusion) =
        base(ParamSlot::Diffusion).withName("Diffusion").asPercent().withDefault(0.0f);

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

    // The Perform UI (sub-project 5's spec §3.3, §4, §5).
    // the grain envelope's shape, 0..1: 0 is today's Hann window.
    at(ParamSlot::Texture) =
        base(ParamSlot::Texture).withName("Texture").asPercent().withDefault(0.0f);

    // the limited wet signal fed back into the live ring, 0..kFeedbackMax, shown as %. The
    // instrument keeps it, hidden and with no effect, as it does Mix.
    at(ParamSlot::Feedback) = base(ParamSlot::Feedback)
                                  .withName("Feedback")
                                  .asPercent()
                                  .withRange(0.f, kFeedbackMax)
                                  .withDefault(0.0f);

    // The eight macros, 0..1 each: at rest (0.5 for a bipolar one, 0 for a unipolar one) a macro
    // adds nothing to its targets. Macro 6 is a switch, like Freeze; Macros 7 and 8 differ by
    // product.
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
        mode == Mode::Instrument
            ? base(ParamSlot::Macro7).withName("Macro Stereo").asPercent().withDefault(0.5f)
            : base(ParamSlot::Macro7).withName("Macro Feedback").asPercent().withDefault(0.0f);
    at(ParamSlot::Macro8) = base(ParamSlot::Macro8)
                                .withName(mode == Mode::Instrument ? "Macro Level" : "Macro Mix")
                                .asPercent()
                                .withDefault(0.5f);
    return t;
}
} // namespace

const sst::basic_blocks::params::ParamMetaData &paramMeta(ParamSlot slot, Mode mode)
{
    static const std::array<md_t, kParamCount> instrument = buildTable(Mode::Instrument);
    static const std::array<md_t, kParamCount> fx = buildTable(Mode::Fx);
    const auto i = static_cast<size_t>(slot);
    assert(i < kParamCount);
    return (mode == Mode::Instrument ? instrument : fx)[std::min(i, kParamCount - 1)];
}
} // namespace gg::core
