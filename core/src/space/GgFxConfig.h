// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_SPACE_GGFXCONFIG_H
#define GG_CORE_SPACE_GGFXCONFIG_H

// Internal to gg-core: the FXConfig that Space's reverb (spec §6.3), sst-effects' Reverb 2, is
// instantiated with. sst-effects asks its FXConfig, through static functions, for every value
// and fact about its environment (sst/effects/EffectCore.h). This one answers from a plain
// Storage: an array of parameter values and the sample rate. There is no tempo sync, no tuning
// and no randomness.

#include <array>
#include <cmath>
#include <cstddef>

#include "sst/effects/Reverb2.h"

namespace gg::core::space
{
/// sst-effects' FXConfig for Space. Its global, effect and value storage are all one Storage,
/// which the owner keeps alive as long as the effect: construct an effect with
/// `(&storage, &storage, &storage)`.
struct GgFxConfig
{
    /// How many parameter values a Storage holds; Reverb 2 uses 10.
    static constexpr int kMaxParams = 16;

    /// The values an effect reads, in the effect's own units and indexed by its own parameter
    /// enum (Reverb 2's `rev2_params`), and the rate it runs at. Set the values before each
    /// processBlock: the effect reads them at the block's start.
    struct Storage
    {
        std::array<float, kMaxParams> values{};
        double sampleRate{48000.0};
    };

    /// What every effect derives from, besides EffectTemplateBase. It holds nothing: the values
    /// are in the Storage.
    struct Base
    {
        Base(Storage *, Storage *, Storage *) {}
    };

    /// Space's control grid: Reverb 2 processes 32-sample chunks.
    static constexpr int blockSize{32};

    using BaseClass = Base;
    using GlobalStorage = Storage;
    using EffectStorage = Storage;
    using ValueStorage = Storage;
    using BiquadAdapter = GgFxConfig;

    static float floatValueAt(const BaseClass *, const ValueStorage *v, int idx)
    {
        return v->values[static_cast<size_t>(idx)];
    }
    static int intValueAt(const BaseClass *, const ValueStorage *v, int idx)
    {
        return static_cast<int>(std::lround(v->values[static_cast<size_t>(idx)]));
    }

    /// Surge's linear envelope rate: 2^-f, scaled to one block at the running rate.
    static float envelopeRateLinear(GlobalStorage *s, float f)
    {
        return std::pow(2.f, -f) * static_cast<float>(blockSize / s->sampleRate);
    }

    // Never tempo synced, deactivated or extended.
    static float temposyncRatio(GlobalStorage *, EffectStorage *, int) { return 1.f; }
    static bool isTemposynced(EffectStorage *, int) { return false; }
    static bool isDeactivated(EffectStorage *, int) { return false; }
    static bool isExtended(EffectStorage *, int) { return false; }

    /// Reverb 2 never calls it. A constant, so any effect that did would still render the same
    /// on every run (INTENT's determinism rules: no randomness outside the engine's RNG).
    static float rand01(GlobalStorage *) { return 0.f; }

    static double sampleRate(GlobalStorage *s) { return s->sampleRate; }
    static double sampleRateInv(GlobalStorage *s) { return 1.0 / s->sampleRate; }

    /// 12-TET from MIDI note 0 = 1: no tuning.
    static float noteToPitch(GlobalStorage *, float note) { return std::pow(2.f, note / 12.f); }
    static float noteToPitchIgnoringTuning(GlobalStorage *s, float note)
    {
        return noteToPitch(s, note);
    }
    static float noteToPitchInv(GlobalStorage *s, float note) { return 1.f / noteToPitch(s, note); }

    /// Reverb 2's width is in dB; 0 dB (1.0) is full stereo.
    static float dbToLinear(GlobalStorage *, float db) { return std::pow(10.f, db / 20.f); }
};

/// Upstream Reverb 2, unmodified, at sst-effects' pin.
using Rev2 = sst::effects::reverb2::Reverb2<GgFxConfig>;
} // namespace gg::core::space

#endif // GG_CORE_SPACE_GGFXCONFIG_H
