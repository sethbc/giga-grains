// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/SpaceReverb.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "space/GgFxConfig.h"
#include "space/Reverb2Patched.h"

namespace gg::core
{
namespace
{
using space::GgFxConfig;
using space::Rev2Patched;

static_assert(SpaceReverb::kChunk == GgFxConfig::blockSize);

/// x clamped to [lo, hi], with NaN taken to lo (std::clamp passes NaN through).
float clampOrLow(float x, float lo, float hi) noexcept
{
    if (!(x >= lo))
        return lo;
    return x > hi ? hi : x;
}

/// The patched Reverb 2 with its mix and width held at their targets, which upstream reaches
/// only by smoothing from 0 over a few dozen blocks.
class WetReverb2 final : public Rev2Patched
{
  public:
    using Rev2Patched::Rev2Patched;

    /// Sets the mix and the width (its side gain) to their targets at once: 100% wet and 0 dB
    /// (dbToLinear(0) = 1). Call it after initialize() and after every completed clear, which
    /// returns both to 0. Each block then re-targets them to the same values, which keeps them
    /// there exactly (set_target_smoothed: 0.25·1 + 0.75·1 = 1).
    void holdWetAndWidth() noexcept
    {
        mix.set_target_instant(1.0f);
        widthS.set_target_instant(1.0f);
    }
};
} // namespace

struct SpaceReverb::Reverb
{
    GgFxConfig::Storage storage; // before reverb: it is constructed with its address
    WetReverb2 reverb{&storage, &storage, &storage};
};

SpaceReverb::SpaceReverb(double sampleRate)
    : reverb_(std::make_unique<Reverb>()), sampleRate_(sampleRate),
      modulationStep_(float(double(kChunk) / (kModulationGlideSeconds * sampleRate)))
{
    assert(std::isfinite(sampleRate) && sampleRate > 0.0);
    auto &s = reverb_->storage;
    s.sampleRate = sampleRate;
    s.values.fill(0.0f);
    s.values[Rev2Patched::rev2_predelay] = -8.0f; // its minimum; the smoothed pre-delay is used
    s.values[Rev2Patched::rev2_diffusion] = 1.0f;
    s.values[Rev2Patched::rev2_buildup] = 1.0f;
    s.values[Rev2Patched::rev2_lf_damping] = 0.2f;
    s.values[Rev2Patched::rev2_mix] = 1.0f;
    s.values[Rev2Patched::rev2_width] = 0.0f; // 0 dB: full stereo
    // The mapped values at Space's defaults; each chunk sets them before it runs.
    s.values[Rev2Patched::rev2_room_size] = 0.0f;
    s.values[Rev2Patched::rev2_decay_time] = 1.0f;
    s.values[Rev2Patched::rev2_hf_damping] = 0.3f;
    s.values[Rev2Patched::rev2_modulation] = 0.25f;
    reverb_->reverb.initialize();
    reverb_->reverb.holdWetAndWidth();
}

SpaceReverb::~SpaceReverb() = default;

void SpaceReverb::reset() noexcept
{
    reverb_->reverb.clear(); // finishes a stepwise clear too
    reverb_->reverb.holdWetAndWidth();
    left_.fill(0.0f);
    right_.fill(0.0f);
    params_ = {};
    modulation_ = 0.0f;
    next_ = 0;
    chunkActive_ = false;
    ran_ = false;
    clearing_ = false;
}

void SpaceReverb::endChunk() noexcept
{
    auto &rev = reverb_->reverb;
    if (!chunkActive_)
    {
        // Idle: no return, and the next chunk returns nothing of this one either. A reverb
        // that has run since it was last empty is cleared a budget at a time.
        if (ran_ || clearing_)
        {
            ran_ = false;
            clearing_ = !rev.clearStep(kClearBudgetFloats);
            if (!clearing_)
                rev.holdWetAndWidth();
        }
        left_.fill(0.0f);
        right_.fill(0.0f);
        return;
    }

    if (clearing_)
    {
        rev.clear(); // the rest of the clear the idle chunks began
        rev.holdWetAndWidth();
        clearing_ = false;
    }

    auto &v = reverb_->storage.values;
    v[Rev2Patched::rev2_room_size] = clampOrLow(params_.roomSize, -1.0f, 1.0f);
    v[Rev2Patched::rev2_hf_damping] = clampOrLow(params_.hfDamping, 0.0f, 1.0f);
    // Modulation glides (the class comment), but takes its value at once in an empty reverb.
    const float modulation = clampOrLow(params_.modulation, 0.0f, 1.0f);
    if (!ran_)
        modulation_ = modulation;
    else if (modulation > modulation_)
        modulation_ = std::min(modulation, modulation_ + modulationStep_);
    else
        modulation_ = std::max(modulation, modulation_ - modulationStep_);
    v[Rev2Patched::rev2_modulation] = modulation_;
    v[Rev2Patched::rev2_decay_time] = clampOrLow(params_.decayLog2, -4.0f, 6.0f);
    rev.setInfinite(params_.infinite);
    rev.setPredelaySamples(static_cast<float>(
        std::max(0.0, double(params_.predelaySeconds) * sampleRate_ - double(kChunk))));
    rev.processBlock(left_.data(), right_.data());
    ran_ = true;
}
} // namespace gg::core
