// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PARAMMAP_H
#define GG_CORE_PARAMMAP_H

#include <algorithm>

#include "gg/core/ParamIds.h"
#include "gg/grain/CloudScheduler.h"

/// The patch's values to the engine's, the wet chain's and Space's parameters: the only place the
/// tapers of Global Constraints' parameter table are applied (spec §5; sub-project 4's spec §6.5).
/// Each function takes the effective values, clamp(base + modulation + macro offsets), as the
/// Processor holds them; macroOffsets gives the macros' share (sub-project 5's spec §3).
///
/// Every value that reaches the grain-event log is plain IEEE arithmetic, exp2Det and std::sqrt
/// (correctly rounded), never another libm call, so it is bit-identical on every platform
/// (INTENT §14).
namespace gg::core
{
/// What the Processor's wet chain and freeze take from the parameters.
struct WetParams
{
    float levelGain; ///< Level as a linear gain: exp2Det(dB × log2(10) / 20).
    float mix;       ///< Mix, the dry/wet crossfade m in [0, 1].
    bool freeze;     ///< Freeze is on above 0.5.
    /// Feedback, fb in [0, kFeedbackMax]: the share of the limited wet signal the FX writes back
    /// into its live ring (sub-project 5's spec §5). Always 0 in the instrument, which has no ring.
    float feedback;
};

/// What Space, the diffuser and the reverb after the wet chain, takes from the parameters
/// (sub-project 4's spec §6.5). The reverb's fields are Reverb 2's parameter values.
struct SpaceParams
{
    float diffusion; ///< Diffusion, the diffuser's crossfade δ in [0, 1].
    float amount;    ///< Reverb Amount, the return's gain a in [0, 1].
    float drySend;   ///< Dry to Reverb, s in [0, 1]: the FX's; always 0 in the instrument.
    /// Reverb Decay in log2 seconds, Reverb 2's rev2_decay_time. Meaningful only when finite.
    float decayLog2;
    bool infinite;         ///< Decay is ∞: decayLog2 >= kDecayInfinite.
    float roomSize;        ///< 2 × Reverb Size − 1, in [−1, 1]: Reverb 2's rev2_room_size.
    float hfDamping;       ///< Reverb Damping, in [0, 1]: Reverb 2's rev2_hf_damping.
    float modulation;      ///< Reverb Modulation, in [0, 1]: Reverb 2's rev2_modulation.
    float predelaySeconds; ///< Reverb Pre-delay, in seconds, 0 to 0.5.
};

/// The Cloud stream's controls in `mode`'s product:
/// - densityHz = exp2Det(Density);
/// - distribution = Distribution;
/// - grainDurationSec = exp2Det(Size);
/// - spray = (2/255)(exp2Det(8 × Spray) − 1) seconds, in both products;
/// - position:
///   - FX: (16/255)(exp2Det(8 × Position) − 1) seconds behind the write head, the live ring's
///     units (ruling M1-k);
///   - instrument: Position itself, the fraction of the loaded file (sub-project 3's spec
///     §6.8), with sprayInSeconds set, so the engine spreads the spray's seconds around it at
///     the source's rate;
/// - pitchSemitones = Pitch; stereo = Stereo; texture = Texture (sub-project 5's spec §4);
/// - gain = 1/sqrt(max(1, densityHz × grainDurationSec)), the expected overlap's power
///   normalization (spec §6.4), which each grain latches at its onset.
CloudParams toCloudParams(const ParamValues &effective, Mode mode) noexcept;

/// Level, Mix, Freeze and Feedback, as WetParams documents. Feedback is 0 in the instrument,
/// whatever its value: it has no live ring.
WetParams toWetParams(const ParamValues &effective, Mode mode) noexcept;

/// Space's eight parameters, as SpaceParams documents. Dry to Reverb is 0 in the instrument,
/// whatever its value: it has no dry input.
SpaceParams toSpaceParams(const ParamValues &effective, Mode mode) noexcept;

/// The wet chain's soft limiter (spec §6.4): the identity up to the knee, |x| <= 0.5 (-6 dBFS),
/// and above it sign(x) × (0.5 + 0.5 × z/(1 + z)) with z = (|x| − 0.5)/0.5, which saturates
/// towards ±1. Continuous with a continuous slope (1 at the knee), odd, monotonic, and below 1
/// in magnitude for every input: an infinite one gives ±(the largest float below 1), and NaN,
/// which nothing upstream produces, gives 0.
float softLimit(float x) noexcept;

/// The smoothstep s²(3 − 2s), s = clamp((x − e0)/(e1 − e0), 0, 1): 0 up to e0, 1 from e1, 0.5
/// half-way, with a flat slope at both ends. Plain float arithmetic, no libm (INTENT §14).
/// e1 must be above e0.
constexpr float smoothstep(float e0, float e1, float x) noexcept
{
    const float s = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return s * s * (3.0f - 2.0f * s);
}

/// Each parameter's total macro offset, in its CLAP value domain (spec §3.2), from the macros'
/// effective values (each in 0..1). Only the eight macro slots of `effectiveMacros` are read.
/// With v a macro's value, b = 2v − 1 (bipolar), b⁺ = max(b, 0) and u = v (unipolar):
/// - Macro 1 (Size): Size +3b, Density −1.5b;
/// - Macro 2 (Density): Density +4b, Distribution +0.25·b⁺;
/// - Macro 3 (Pitch): Pitch +24b;
/// - Macro 4 (Spray): Spray +0.5b, Stereo +0.3·b⁺;
/// - Macro 5 (Texture): Texture +min(u/0.75, 1), Diffusion +smoothstep(0.75, 1, u);
/// - Macro 6 (Freeze): Freeze +u;
/// - Macro 7: the FX's Feedback +0.9u²; the instrument's Stereo +0.5b;
/// - Macro 8: the FX's Mix +0.5b; the instrument's Level +12b dB.
///
/// A target's offsets add in macro order. Every entry, the macros' own included, is exactly
/// +0.0f when every macro rests (the bipolar ones at 0.5, the unipolar ones, 5, 6 and the FX's 7,
/// at 0), so base + mod + offset is then base + mod, bit for bit (but for a −0 sum, which becomes
/// +0). Plain float arithmetic, no libm.
ParamValues macroOffsets(const ParamValues &effectiveMacros, Mode mode) noexcept;
} // namespace gg::core

#endif // GG_CORE_PARAMMAP_H
