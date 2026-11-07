// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PARAMMAP_H
#define GG_CORE_PARAMMAP_H

#include <algorithm>
#include <array>
#include <cstdint>

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

/// A layer's Mode (M3 sub-project 1's spec §4.1): how it plays notes, from the Mode parameter.
enum class LayerMode : uint8_t
{
    Cloud, ///< M2's: the held keys set the Cloud stream's pitches.
    Voice, ///< One voice per note.
};

/// The mode a Mode value means: below 0.5 (or NaN) is Cloud, else Voice.
LayerMode layerModeOf(float value) noexcept;

/// A layer's Clock (M3 sub-project 3's spec §4.1): from the Clock parameter, 0 Free or 1 Pitch.
/// At or above 0.5 is Pitch; below it, or NaN, is Free.
gg::Clock clockOf(float value) noexcept;

/// C4's frequency in Hz, MIDI key 60 (A4 = 440 Hz is 9 semitones above).
inline constexpr double kC4Hz = 261.6255653005986;

/// The frequency in Hz of `notePitch`, in semitones from C4 (a key less 60, tuning and bend
/// included): kC4Hz * exp2Det(notePitch / 12). Unclamped; plain IEEE arithmetic and exp2Det.
double noteFrequencyHz(double notePitch) noexcept;

/// Periods of the note's pitch in one second of Size (the Pitch clock; spec S1).
inline constexpr double kPeriodsPerSizeSecond = gg::CloudScheduler::kPeriodsPerSizeSecond;

/// A grain's length in periods at Size `sizeLog2Seconds` (the parameter's value, log2 seconds):
/// exp2Det(size) * kPeriodsPerSizeSecond.
double sizePeriods(float sizeLog2Seconds) noexcept;

/// The source kind a layer of `kind` reads in `mode`'s product (spec §6.4): Sample and Live are
/// built in wave 1, in both products; Resample, a Live-mode engine whose ring the instance's
/// output feeds, since M2 sub-project 2 (its spec §6.2), and Spectral since M2 sub-project 4, in
/// both products too; the unbuilt kind (Oscillator) falls back to the product's default kind, Live
/// in the FX and Sample in the instrument.
SourceMode builtKind(SourceKind kind, Mode mode) noexcept;

/// What a layer's ring takes from its parameters (M2 sub-project 2's spec §6.3): the values as
/// the parameters give them. The engine clamps Loop Length to the ring's length in effect, and the
/// plugin's main thread sizes the ring from ringSeconds (Ring Length is neither automatable nor
/// modulatable), so the ring in effect may lag it.
struct RingParams
{
    double ringSeconds; ///< Ring Length: exp2Det(its log2 seconds), 1 to kRingMaxSeconds.
    bool loop;          ///< Loop is on above 0.5.
    double loopSeconds; ///< Loop Length: exp2Det(its log2 seconds), kLoopMinSeconds and up.
    float overdub;      ///< Overdub, the gain o in [0, 1], as it is.
    /// Splice, rounded to the nearest integer (halves up) and clamped to 0..kMaxSplices: 0 is the
    /// whole window.
    uint32_t splice;
};

/// Layer `layer`'s ring values, as RingParams documents, from its own slots (layerSlot). `layer`
/// is below kLayers (asserted; the namespace's note).
RingParams toRingParams(const ParamValues &effective, uint8_t layer) noexcept;

/// Whether layer `layer`'s Freeze is on in `values`: above 0.5, the stepped switch's threshold
/// (WetParams::freeze's). `layer` is below kLayers (asserted).
bool freezeOn(const ParamValues &values, uint8_t layer) noexcept;

/// Whether a layer whose Source builds to `kind` (builtKind) reads its live ring: SourceMode::Live
/// (Live or Resample, the kinds whose grains read it), or SourceMode::Spectral without a sample
/// (`hasSample` false), so the Spectral analysis smears the ring (the extremes spec §6.2; the
/// integration review's I2, INTENT §23 #69). A Sample layer's ring, and a Spectral layer's with a
/// sample, record for a later switch but nothing reads them. ringSaved decides by it, and so do
/// the Processor's ring commands (row 381: a layer whose ring isn't read ignores them).
bool readsRing(SourceMode kind, bool hasSample) noexcept;

/// Whether a session saves layer `layer`'s ring (M2 sub-project 2's spec D1), from its base
/// values and its material: the layer's ring is read (readsRing, the kind builtKind gives in
/// `mode`'s product, with a sample unless `material` is SourceMode::Live), and its Freeze or its
/// Loop is on (each above 0.5: freezeOn, RingParams::loop). A ring nothing reads isn't saved.
/// `material` is Processor::layerMaterial's, as the main thread knows it at the save (the layer's
/// sample loaded or not). The plugin's MediaSession and gg-render's --save-state both decide by it.
/// `layer` is below kLayers (asserted).
bool ringSaved(const ParamValues &base, Mode mode, uint8_t layer, SourceMode material) noexcept;

/// Layer `layer`'s Cloud stream controls (`layer` below kLayers, asserted), from its own slots
/// (layerSlot), with D = exp2Det(Density) Hz, S = exp2Det(Size) seconds and s = Smear in [0, 1]
/// (layerSmear; M2 sub-project 4's spec §6.1):
/// - densityHz = D_eff = D + s × (min(400, max(D, T / S)) − D), T = 8 + 24s the target overlap:
///   Smear raises the density toward the one that gives the overlap T, never past the Density
///   parameter's 400 Hz ceiling; at s = 0 it is D, bit for bit;
/// - distribution = Distribution;
/// - grainDurationSec = S;
/// - spray = (2/255)(exp2Det(8 × Spray) − 1) seconds, whatever the kind;
/// - position, by the layer's built kind (builtKind(sourceKindOf(...), mode); spec §6.4), not by
///   the product:
///   - Live: (16/255)(exp2Det(8 × Position) − 1) seconds behind the write head, the live ring's
///     units (ruling M1-k), up to kRingMaxSeconds at kPositionMax (M2 sub-project 2's spec §6.3);
///   - Sample: Position itself, clamped to 1, the fraction of the loaded file (sub-project 3's
///     spec §6.8), with sprayInSeconds set, so the engine spreads the spray's seconds around it
///     at the source's rate;
/// - pitchSemitones = Pitch; stereo = Stereo; texture = Texture (sub-project 5's spec §4);
/// - pitchSpreadSemitones = Pitch Spread; sizeSpreadOctaves = Size Spread (spec §6.6);
/// - smearJitterSec = 0.5 × s × S; smearDetuneSemitones = 0.5 × s (both 0 at s = 0, so the
///   scheduler draws neither);
/// - busSplit = Bus Split (layerBusSplit; the extremes spec §6.3);
/// - quality = Quality rounded to the nearest step (lround) and clamped to 0..2: Eco, Normal or HQ
///   (M2 sub-project 3's spec §6.1); a NaN is Normal;
/// - gain = 1/sqrt(max(1, densityHz × grainDurationSec)), the expected overlap's power
///   normalization (spec §6.4), which each grain latches at its onset: it follows D_eff, so
///   Smear doesn't pump the level.
///
/// At Smear 0 every field but busSplit is M1's and wave 1's, bit for bit.
CloudParams toCloudParams(const ParamValues &effective, Mode mode, uint8_t layer) noexcept;

/// As the three-argument form, with `material` the units of Position for a layer whose built
/// kind reads its material through a stream of its own: the Spectral kind (the extremes spec
/// §6.2), whose Position addresses its material, Sample (the fraction, with sprayInSeconds) or
/// Live (seconds behind the head), while its grains draw their spray one-sided, as Live's (the
/// scheduler's Spectral mode). A Sample or Live layer ignores `material` and takes its own units.
/// The three-argument form passes the kind's own units (Sample's for Sample, Live's otherwise).
CloudParams toCloudParams(const ParamValues &effective, Mode mode, uint8_t layer,
                          SourceMode material) noexcept;

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

/// The weight units one Budget grain is worth: a Normal grain reading a stereo source, X's case
/// (M2 sub-project 3's spec §6.4), so a budget "of N grains" is N × 4 units. It must equal the
/// engine's weight for that grain.
inline constexpr uint32_t kBudgetUnitsPerGrain = 4;

/// Budget's effective value as weight units, the layers' shared budget (M2 sub-project 3's spec
/// §6.4): the count rounded to the nearest integer (lround: halves away from zero) and clamped to
/// 256..16,384, times kBudgetUnitsPerGrain, so 1,024 to 65,536 units; a NaN count is the default,
/// 4,096 (16,384 units).
uint64_t budgetUnits(const ParamValues &effective) noexcept;

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

/// Layer `layer`'s Smear (M2 sub-project 4's spec §6.1), as it is, in [0, 1] (`layer` below
/// kLayers, asserted).
float layerSmear(const ParamValues &effective, uint8_t layer) noexcept;

/// Layer `layer`'s Bus Split (spec §6.3), the share of its grains on bus 2, as it is, in [0, 1]
/// (`layer` below kLayers, asserted).
float layerBusSplit(const ParamValues &effective, uint8_t layer) noexcept;

/// Whether `s` is a modulator's control: LFO 1 Rate to Step 16, in any layer. No route targets
/// one, so its effective value is clamp(base + modulation). The layer parameters after Step 16
/// (Smear, the buses) are not.
bool isModulatorControlSlot(ParamSlot s) noexcept;

/// A layer's two bus filters (spec §6.3), index 0 for bus 1 and 1 for bus 2.
struct BusParams
{
    /// Bus N Filter rounded to the nearest integer (halves up, as sourceKindOf rounds), 0 to 4.
    std::array<BusFilterType, 2> type;
    /// Bus N Cutoff in Hz: exp2Det(slot), the slot being log2 Hz, 20 Hz to 20 kHz.
    std::array<float, 2> cutoffHz;
    /// Bus N Resonance, as it is, in [0, 1].
    std::array<float, 2> resonance;
};

/// Layer `layer`'s bus filters, as BusParams documents (`layer` below kLayers, asserted).
BusParams busParams(const ParamValues &effective, uint8_t layer) noexcept;

/// What the global delay takes from the parameters (spec §6.4).
struct DelayParams
{
    float amount;      ///< Delay Amount, the return's gain, in [0, 1].
    float timeSeconds; ///< Delay Time in seconds: exp2Det(slot), the slot being log2 seconds.
    float feedback;    ///< Delay Feedback, in [0, 1].
    float damping;     ///< Delay Damping, in [0, 1].
    bool pingPong;     ///< Delay Ping-pong is on above 0.5.
};

/// The delay's five parameters, as DelayParams documents.
DelayParams delayParams(const ParamValues &effective) noexcept;

/// The delay damper's one-pole coefficient for Delay Damping (0–1) at sampleRate (spec §6.4):
/// the cutoff runs exponentially from 20 kHz at 0 to 500 Hz at 1, so Damping 0 is about 0.927 at
/// 48 kHz, never 1.
float dampCoefficient(float damping, double sampleRate) noexcept;

/// What the global limiter takes from the parameters (spec §6.5).
struct LimiterParams
{
    /// Limiter Mode rounded to the nearest integer (halves up), 0 to 2.
    LimiterMode mode;
    /// Limiter Ceiling as a linear gain: exp2Det(dB × log2(10) / 20), as Level's gain is made.
    float ceilingGain;
    /// Limiter Release in seconds: exp2Det(slot), the slot being log2 seconds.
    float releaseSeconds;
};

/// The limiter's three parameters, as LimiterParams documents.
LimiterParams limiterParams(const ParamValues &effective) noexcept;

/// Peak's per-sample release coefficient for a release time constant in seconds at sampleRate
/// (spec §6.5).
float releaseCoefficient(float releaseSeconds, double sampleRate) noexcept;
} // namespace gg::core

#endif // GG_CORE_PARAMMAP_H
