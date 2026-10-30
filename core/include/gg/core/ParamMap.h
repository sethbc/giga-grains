// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PARAMMAP_H
#define GG_CORE_PARAMMAP_H

#include <algorithm>

#include "gg/core/ModSources.h"
#include "gg/core/ParamIds.h"
#include "gg/grain/CloudScheduler.h"
#include "gg/source/SourceMode.h"

/// The patch's values to the engine's, the wet chain's and Space's parameters: the only place the
/// tapers of Global Constraints' parameter table are applied (spec §5; sub-project 4's spec §6.5).
/// Each function takes the effective values, clamp(base + modulation + route offsets), as the
/// Processor holds them; the mod matrix (gg/core/ModMatrix.h) gives the routes' share, the macros'
/// included (sub-project 5's spec §3; M2 sub-project 1's spec §6.8).
///
/// **The layer argument.** Every layer-aware function takes `layer` below kLayers. A larger one
/// asserts, and a release build reads the last parameter's slot rather than out of bounds (the
/// slot helpers give ParamSlot::Count for it), as Processor::base() does for ParamSlot::Count.
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
    /// Feedback, fb in [0, kFeedbackMax]: the share of the layer's limited wet signal written back
    /// into its live ring (sub-project 5's spec §5). 0 unless the layer's built kind is
    /// SourceMode::Live (M2 sub-project 1's spec §6.4): a Sample layer has no ring to feed.
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

/// Layer `layer`'s Source kind (M2 sub-project 1's spec §6.4): its Source value rounded to the
/// nearest integer (halves up), 0 to 4. `layer` is below kLayers (asserted; the namespace's note).
SourceKind sourceKindOf(const ParamValues &effective, uint8_t layer) noexcept;

/// The source kind a layer of `kind` reads in `mode`'s product (spec §6.4): Sample and Live are
/// built in wave 1, in both products; the unbuilt kinds (Resample, Spectral, Oscillator) fall back
/// to the product's default kind, Live in the FX and Sample in the instrument. The one function
/// sub-projects 2 and 4 edit when their kinds arrive.
SourceMode builtKind(SourceKind kind, Mode mode) noexcept;

/// Layer `layer`'s Cloud stream controls (`layer` below kLayers, asserted), from its own slots
/// (layerSlot):
/// - densityHz = exp2Det(Density);
/// - distribution = Distribution;
/// - grainDurationSec = exp2Det(Size);
/// - spray = (2/255)(exp2Det(8 × Spray) − 1) seconds, whatever the kind;
/// - position, by the layer's built kind (builtKind(sourceKindOf(...), mode); spec §6.4), not by
///   the product:
///   - Live: (16/255)(exp2Det(8 × Position) − 1) seconds behind the write head, the live ring's
///     units (ruling M1-k);
///   - Sample: Position itself, the fraction of the loaded file (sub-project 3's spec §6.8),
///     with sprayInSeconds set, so the engine spreads the spray's seconds around it at the
///     source's rate;
/// - pitchSemitones = Pitch; stereo = Stereo; texture = Texture (sub-project 5's spec §4);
/// - pitchSpreadSemitones = Pitch Spread; sizeSpreadOctaves = Size Spread (spec §6.6);
/// - gain = 1/sqrt(max(1, densityHz × grainDurationSec)), the expected overlap's power
///   normalization (spec §6.4), which each grain latches at its onset.
CloudParams toCloudParams(const ParamValues &effective, Mode mode, uint8_t layer) noexcept;

/// Layer A's: toCloudParams(effective, mode, 0). At the products' default Source (Live in the FX,
/// Sample in the instrument) it is M1's mapping, bit for bit.
CloudParams toCloudParams(const ParamValues &effective, Mode mode) noexcept;

/// Layer `layer`'s wet chain values, as WetParams documents (`layer` below kLayers, asserted): its
/// Level (levelSlot: Level for layer A, Level B to D for the others), the global Mix, its Freeze
/// and its Feedback, which is 0 unless the layer's built kind is Live.
WetParams toWetParams(const ParamValues &effective, Mode mode, uint8_t layer) noexcept;

/// Layer A's: toWetParams(effective, mode, 0). At the products' default Source it is M1's: the
/// instrument's Feedback is 0, as its layer A reads a sample.
WetParams toWetParams(const ParamValues &effective, Mode mode) noexcept;

/// Layer `layer`'s Diffusion, the diffuser's crossfade δ in [0, 1] (`layer` below kLayers,
/// asserted).
float layerDiffusion(const ParamValues &effective, uint8_t layer) noexcept;

/// Whether layer `layer`'s Layer On switch is on: its value above 0.5 (`layer` below kLayers,
/// asserted).
bool layerOn(const ParamValues &effective, uint8_t layer) noexcept;

/// The morph pad's position (spec §6.3), Morph X and Morph Y, each in [0, 1].
struct MorphParams
{
    float x;
    float y;
};

/// Morph X and Morph Y, as they are.
MorphParams toMorphParams(const ParamValues &effective) noexcept;

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

/// Layer `layer`'s LFO `which` (0 for LFO 1, 1 for LFO 2; asserted) as its modulator takes it
/// (M2 sub-project 1's spec §6.8, §6.9): the rate exp2Det(LFO Rate) Hz, its slot being log2 Hz,
/// and the shape LFO Shape rounded to the nearest integer (halves up, as sourceKindOf rounds),
/// 0 to 4. `layer` is below kLayers (asserted; the namespace's note).
LfoParams lfoParams(const ParamValues &effective, uint8_t layer, int which) noexcept;

/// Layer `layer`'s envelope `which` (0 for Env 1, 1 for Env 2; asserted): Attack, Decay and
/// Release as exp2Det(slot) seconds, their slots being log2 seconds, and Sustain as it is.
AdsrParams envParams(const ParamValues &effective, uint8_t layer, int which) noexcept;

/// Layer `layer`'s random source `which` (0 for Random 1, 1 for Random 2; asserted): the rate
/// exp2Det(Random Rate) Hz and Smooth as it is.
RandomParams randomParams(const ParamValues &effective, uint8_t layer, int which) noexcept;

/// Layer `layer`'s step sequencer: the rate exp2Det(Step Rate) Hz, Step Length rounded to the
/// nearest integer (halves up) and clamped to 1..16, and Step 1 to Step 16 as they are.
StepParams stepParams(const ParamValues &effective, uint8_t layer) noexcept;
} // namespace gg::core

#endif // GG_CORE_PARAMMAP_H
