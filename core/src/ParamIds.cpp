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
