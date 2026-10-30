// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_MODSOURCES_H
#define GG_CORE_MODSOURCES_H

#include <array>
#include <cstdint>

#include "gg/core/MathDet.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Rng.h"

/// The mod matrix's sources (M2 spec §6.8): small state machines, each stepped once per 32-sample
/// grid cell (`cellSeconds` is 32 / sampleRate, fixed per Processor) and giving a value in [0, 1].
///
/// Every value is a function of the cell clock, the parameter history and, for the random source,
/// the engine's counter-based RNG, so it is the same at any host block size and on every platform:
/// every `step` and `reset` is noexcept, allocates nothing and calls no libm function but the exact
/// std::floor and std::fabs (plain IEEE arithmetic and exp2Det; INTENT §14).
namespace gg::core
{
/// sineDet and its coefficients live in the engine's MathDet.h (gg::sineDet), which gg-engine's
/// own code can call; these names keep them reachable here, so callers spell them as before.
namespace detail
{
using gg::detail::kSineDetCoeffs;
} // namespace detail
using gg::sineDet;

/// An LFO's controls (spec §6.9): its rate in Hz and its shape.
struct LfoParams
{
    float rateHz;
    LfoShape shape;
};

/// An LFO: a phase in [0, 1), 0 at reset, accumulating rate × cellSeconds per cell, so a rate
/// change bends the phase's slope but never jumps it.
class Lfo
{
  public:
    void reset() noexcept;
    /// The shape's value at the current phase, in [0, 1] (Sine 0.5 + 0.5 sineDet(p); Triangle
    /// 0 at p = 0 rising to 1 at 0.5; SawUp p; SawDown 1 − p; Square 1 below p = 0.5, else 0),
    /// then advances the phase and wraps it into [0, 1). A non-finite or negative rate holds it.
    float step(const LfoParams &params, double cellSeconds) noexcept;
    double phase() const noexcept { return phase_; }

  private:
    double phase_{0.0};
};

/// An envelope's controls (spec §6.9): times in seconds, sustain a level in [0, 1].
struct AdsrParams
{
    float attackSec, decaySec, sustain, releaseSec;
};

/// An ADSR with linear segments, gated by the instance's held set: a rising gate attacks from the
/// current level toward 1 at 1 / attack per second; at 1 it decays toward sustain at
/// (1 − sustain) / decay; at sustain it holds (following the sustain control); a falling gate, at
/// any stage, releases toward 0 at level_at_release / release; at 0 it idles. Each time is clamped
/// below at one cell (a non-finite one counts as one cell).
class Adsr
{
  public:
    void reset() noexcept;
    /// The level at the start of the cell, in [0, 1]; then takes the gate's change, if any, and
    /// advances the level by one cell.
    float step(const AdsrParams &params, bool gate, double cellSeconds) noexcept;

  private:
    enum class Stage : uint8_t
    {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release,
    };

    // Double, so that a segment's increments sum to its end within a few ulp however long it is.
    double level_{0.0};
    double releaseFrom_{0.0};
    Stage stage_{Stage::Idle};
    bool gate_{false};
};

/// A random source's controls (spec §6.9): new values per second, and the glide's Smooth.
struct RandomParams
{
    float rateHz;
    float smooth;
};

/// A random source: target k is uniform01(hash({seed, layer, 0, field, k})), k counting the
/// periods of 1 / rate s from reset (field is rng::Field::ModRandom1 or ModRandom2). The output
/// glides toward the target, out += (target − out) × min(1, cellSeconds / τ) per cell with
/// τ = smooth / rate seconds; smooth 0 jumps to each target at once.
class RandomSource
{
  public:
    void reset() noexcept;
    /// The output for this cell, in [0, 1] (target 0 is drawn on the first step after a reset,
    /// and the output starts there); then advances the step counter's phase, draws the new
    /// target when it crosses a period, and glides one cell toward it.
    float step(const RandomParams &params, double cellSeconds, uint64_t seed, uint32_t layer,
               rng::Field field) noexcept;

  private:
    double phase_{0.0};
    uint64_t k_{0};
    float target_{0.0f};
    float out_{0.0f};
    bool started_{false};
};

/// A step sequencer's controls (spec §6.9): steps per second, how many of the 16 steps cycle, and
/// the steps' values.
struct StepParams
{
    float rateHz;
    uint8_t length;
    std::array<float, 16> steps;
};

/// A step sequencer: step k advances every 1 / rate s from reset; the value is
/// steps[k mod length], length clamped to 1..16.
class StepSequencer
{
  public:
    void reset() noexcept;
    /// The current step's value, then advances the phase (and k when it crosses a step).
    float step(const StepParams &params, double cellSeconds) noexcept;

  private:
    double phase_{0.0};
    uint64_t k_{0};
};

/// A peak follower of the instance's input: a 5 ms attack and 200 ms release one-pole on the
/// cell's peak |x|, clamped to 1. Its coefficients, exp(−cellSeconds / τ) computed as
/// exp2Det(−cellSeconds / (τ ln 2)), are fixed in the constructor.
class EnvFollower
{
  public:
    explicit EnvFollower(double sampleRate) noexcept;
    void reset() noexcept;
    /// Takes the peak |x| over every channel and frame of the cell's input (a non-finite sample
    /// counts as 0, and a null channel or a null `in` as silence), moves the envelope toward it
    /// and returns min(envelope, 1).
    float step(const float *const *in, uint32_t channels, uint32_t frames) noexcept;

  private:
    float attack_;
    float release_;
    float env_{0.0f};
};

/// The global MIDI sources, each in [0, 1]: the last CC 1, channel pressure and pitch bend (centre
/// 0.5) and the last note-on's velocity and key / 127. Plain values the Processor holds and sets.
struct MidiSources
{
    float modWheel{0}, aftertouch{0}, pitchBend{0.5f}, velocity{0}, key{0};
};

/// The host's transport as its last transport event gave it.
struct Transport
{
    bool hasBeats{false};
    double beatPosition{0};
    double tempoBpm{120};
    bool playing{false};
    int beatsPerBar{4};
};

/// The transport sources: the host's beat phase, bar phase and playing flag, advanced between the
/// host's events while it plays and held when it doesn't; 0 before any event.
class TransportSource
{
  public:
    /// Takes the host's transport, as its event gave it.
    void set(const Transport &t) noexcept;
    /// While playing, beatPosition += tempo × cellSeconds / 60.
    void step(double cellSeconds) noexcept;
    /// The position's fraction of a beat, in [0, 1]; 0 without beats.
    float beatPhase() const noexcept;
    /// The position's fraction of a bar of beatsPerBar (at least 1) beats, in [0, 1]; 0 without
    /// beats.
    float barPhase() const noexcept;
    /// 1 while the host plays, else 0.
    float playing() const noexcept;

  private:
    Transport t_{};
};

/// One layer's modulators (spec §6.8, D3): two LFOs, two envelopes, two random sources and the
/// step sequencer.
struct LayerModulators
{
    Lfo lfo[2];
    Adsr env[2];
    RandomSource random[2];
    StepSequencer stepSeq;
    void reset() noexcept;
};
} // namespace gg::core

#endif // GG_CORE_MODSOURCES_H
