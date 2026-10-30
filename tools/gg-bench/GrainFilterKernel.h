// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_BENCH_GRAIN_FILTER_KERNEL_H
#define GG_BENCH_GRAIN_FILTER_KERNEL_H

// The per-grain filter spike: INTENT §22's measurement of what a filter per grain costs, against
// the grain buses' filter per bus (M2 extremes spec §6.10). Bench only: the engine and the
// plugins must not depend on it.
//
// renderHermiteSimdGrainFilter is hermite-simd, the frozen renderHermiteSimd of Kernels.h (called,
// never copied, so Kernels.h stays untouched), with one sst::filters::CytomicSVF per grain on
// that grain's contribution before it is added into the output. Each grain renders alone into a
// scratch block, then its filter steps once per output sample, both channels in one
// CytomicSVF::step(that, L, R): a recursive filter can't vectorize over time, so this is a
// scalar step per grain per sample, as a per-grain filter in the engine would be. The filter is
// Low-pass at 1 kHz with resonance 0.5, fixed: what is measured is the step, not a sweep.
//
// The filters live in a GrainFilterBank beside the GrainBatch, one per batch slot, each with its
// own coefficients (a per-grain filter exists to give every grain its own cutoff, so that is the
// footprint it would carry). When the slot's grain respawns, reset() starts the new grain's
// filter from silence.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <sst/basic-blocks/simd/setup.h>
#include <sst/filters/CytomicSVF.h>

#include "Kernels.h"

namespace gg::bench
{
/// One CytomicSVF per grain of a GrainBatch, indexed by the grain's batch slot, and the scratch
/// block one grain renders into.
class GrainFilterBank
{
  public:
    static constexpr float kCutoffHz = 1000.0f;
    static constexpr float kResonance = 0.5f;

    /// grains filters at kCutoffHz and kResonance for a render at sampleRate, in blocks of at
    /// most maxBlockSize samples.
    GrainFilterBank(std::size_t grains, uint32_t maxBlockSize, double sampleRate)
        : filters_(grains, prototype(sampleRate)), scratchL_(maxBlockSize), scratchR_(maxBlockSize)
    {
        one_.resize(1);
    }

    std::size_t size() const noexcept { return filters_.size(); }
    uint32_t maxBlockSize() const noexcept { return static_cast<uint32_t>(scratchL_.size()); }

    /// A new grain in slot g: its filter starts from silence.
    void reset(std::size_t g) noexcept { filters_[g].init(); }

    /// Renders every grain of b, which has size() grains, through its filter and adds it into
    /// outL / outR. n is at most maxBlockSize().
    void render(const GrainBatch &b, const float *srcL, const float *srcR, int64_t frames,
                float *outL, float *outR, uint32_t n) noexcept
    {
        float *l = scratchL_.data();
        float *r = scratchR_.data();
        for (std::size_t g = 0; g < b.size(); ++g)
        {
            one_.pos[0] = b.pos[g];
            one_.rate[0] = b.rate[g];
            one_.envPhase[0] = b.envPhase[g];
            one_.envInc[0] = b.envInc[g];
            one_.gain[0] = b.gain[g];
            std::fill_n(l, n, 0.0f);
            std::fill_n(r, n, 0.0f);
            renderHermiteSimd(one_, srcL, srcR, frames, l, r, n);
            sst::filters::CytomicSVF &filter = filters_[g];
            for (uint32_t i = 0; i < n; ++i)
            {
                float sl = l[i];
                float sr = r[i];
                sst::filters::CytomicSVF::step(filter, sl, sr);
                outL[i] += sl;
                outR[i] += sr;
            }
        }
    }

  private:
    static sst::filters::CytomicSVF prototype(double sampleRate)
    {
        sst::filters::CytomicSVF f{};
        f.setCoeff(sst::filters::CytomicSVF::Mode::Lowpass, kCutoffHz, kResonance,
                   static_cast<float>(1.0 / sampleRate));
        f.init();
        return f;
    }

    std::vector<sst::filters::CytomicSVF> filters_;
    GrainBatch one_; // the grain being rendered, as a batch of one for renderHermiteSimd
    std::vector<float> scratchL_, scratchR_;
};

/// hermite-simd with a filter per grain (see the top of this file): renders each grain of b
/// with renderHermiteSimd, steps it through bank's filter for its slot and adds it into outL /
/// outR, so the caller clears those first. bank has b.size() filters and n is at most its
/// maxBlockSize().
inline void renderHermiteSimdGrainFilter(GrainFilterBank &bank, const GrainBatch &b,
                                         const float *srcL, const float *srcR, int64_t frames,
                                         float *outL, float *outR, uint32_t n) noexcept
{
    bank.render(b, srcL, srcR, frames, outL, outR, n);
}
} // namespace gg::bench

#endif // GG_BENCH_GRAIN_FILTER_KERNEL_H
