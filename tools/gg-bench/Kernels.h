// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_BENCH_KERNELS_H
#define GG_BENCH_KERNELS_H

// PROTOTYPES. These grain-rendering kernels exist to measure what one stereo grain-sample costs
// (the M0 kernel benchmark spike, INTENT §16). M1 promotes or rewrites them; the engine and the
// plugins must not depend on them. They favour clarity over cleverness.
//
// Every kernel renders n output samples of each grain in a GrainBatch and *adds* them into
// outL / outR, so the caller clears those first. Sample i of grain g:
//   - reads the source at frame position pos[g] + i·rate[g], a double;
//   - is shaped by the Hann window at phase envPhase[g] + i·envInc[g] (zero outside [0, 1]);
//   - is scaled by gain[g].
// Grains are summed in batch order. Source taps outside [0, frames) read as zero, as in
// gg::interp, so any position (including NaN and ±inf) is safe.
//
// The scalar kernels are the references. The SIMD kernels are grain-major: each 4-wide step
// renders 4 consecutive output samples of one grain. Each lane's source taps are gathered one
// at a time (neither SSE2 nor NEON has a gather), then the interpolation, the window and the
// accumulation run 4-wide. The SIMD kernels evaluate the same expressions in the same order as
// the scalar ones, so under -ffp-contract=off they should agree to the bit; the test allows
// 1e-5 of the peak.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include <sst/basic-blocks/simd/setup.h>

#include "gg/core/Interpolate.h"
#include "gg/core/Window.h"

namespace gg::bench
{
/// A batch of grains as a structure of arrays: entry g of every vector belongs to grain g, and
/// holds its state at the first sample of the block.
struct GrainBatch
{
    std::vector<double> pos;     ///< source read position, in frames
    std::vector<double> rate;    ///< source frames advanced per output sample
    std::vector<float> envPhase; ///< window phase, in [0, 1] while the grain sounds
    std::vector<float> envInc;   ///< window phase advanced per output sample
    std::vector<float> gain;     ///< linear gain

    std::size_t size() const noexcept { return pos.size(); }

    void resize(std::size_t n)
    {
        pos.resize(n);
        rate.resize(n);
        envPhase.resize(n);
        envInc.resize(n);
        gain.resize(n);
    }
};

/// The signature every kernel shares.
using Kernel = void (*)(const GrainBatch &, const float *srcL, const float *srcR, int64_t frames,
                        float *outL, float *outR, uint32_t n);

namespace detail
{
/// One grain's state, copied out of the batch so the inner loops keep it in registers.
struct Grain
{
    double pos, rate;
    float envPhase, envInc, gain;

    double position(uint32_t i) const noexcept { return pos + static_cast<double>(i) * rate; }
    float phase(uint32_t i) const noexcept { return envPhase + static_cast<float>(i) * envInc; }
};

inline Grain grain(const GrainBatch &b, std::size_t g) noexcept
{
    return {b.pos[g], b.rate[g], b.envPhase[g], b.envInc[g], b.gain[g]};
}

/// The window every kernel uses.
inline const HannTable &hann()
{
    static const HannTable table;
    return table;
}

/// HannTable's kSize + 1 points, which the SIMD kernels index 4 lanes at a time. HannTable::at
/// at an exact grid phase returns the stored point, so this copy equals the table.
inline const std::array<float, HannTable::kSize + 1> &hannPoints()
{
    static const auto points = [] {
        std::array<float, HannTable::kSize + 1> p{};
        for (uint32_t i = 0; i <= HannTable::kSize; ++i)
        {
            p[i] = hann().at(static_cast<double>(i) / HannTable::kSize);
        }
        return p;
    }();
    return points;
}

/// Sample i of grain gr through a gg::interp reader, added into the outputs. Shared by the
/// scalar kernels and the SIMD kernels' last n % 4 samples.
template <auto Interp>
inline void addSample(const HannTable &win, const Grain &gr, uint32_t i, const float *srcL,
                      const float *srcR, int64_t frames, float *outL, float *outR) noexcept
{
    const double p = gr.position(i);
    const float amp = win.at(static_cast<double>(gr.phase(i))) * gr.gain;
    outL[i] += amp * Interp(srcL, frames, p);
    outR[i] += amp * Interp(srcR, frames, p);
}

template <auto Interp>
inline void renderScalar(const GrainBatch &b, const float *srcL, const float *srcR, int64_t frames,
                         float *outL, float *outR, uint32_t n) noexcept
{
    const HannTable &win = hann();
    for (std::size_t g = 0; g < b.size(); ++g)
    {
        const Grain gr = grain(b, g);
        for (uint32_t i = 0; i < n; ++i)
        {
            addSample<Interp>(win, gr, i, srcL, srcR, frames, outL, outR);
        }
    }
}

inline float tap(const float *src, int64_t frames, int64_t i) noexcept
{
    return (i >= 0 && i < frames) ? src[i] : 0.0f;
}

/// Coefficients for the 16-tap windowed-sinc reader: kPhases + 1 rows of kTaps, row r for the
/// fractional position r / kPhases. The extra row (fraction 1) lets a read interpolate between
/// rows r and r + 1 without wrapping. Each row is a Blackman-windowed sinc normalised to unity
/// gain at DC. The cutoff is fixed at Nyquist: a production reader would lower it for rates
/// above 1 to stop aliasing, at no extra cost per sample.
struct SincTable
{
    static constexpr int kTaps = 16;
    static constexpr int kPhases = 512;
    /// Tap j of a read at position p is source frame floor(p) - kBefore + j.
    static constexpr int kBefore = kTaps / 2 - 1;

    std::array<float, (kPhases + 1) * kTaps> coef{};

    SincTable()
    {
        constexpr double pi = std::numbers::pi;
        for (int r = 0; r <= kPhases; ++r)
        {
            const double frac = static_cast<double>(r) / kPhases;
            std::array<double, kTaps> h{};
            double sum = 0.0;
            for (int j = 0; j < kTaps; ++j)
            {
                // Distance from the read position to tap j, in [-8, 8]; the window is zero at
                // both ends.
                const double x = static_cast<double>(j - kBefore) - frac;
                const double sinc = x == 0.0 ? 1.0 : std::sin(pi * x) / (pi * x);
                const double u = pi * x / (kTaps / 2);
                const double blackman = 0.42 + 0.5 * std::cos(u) + 0.08 * std::cos(2.0 * u);
                h[j] = sinc * blackman;
                sum += h[j];
            }
            for (int j = 0; j < kTaps; ++j)
            {
                coef[r * kTaps + j] = static_cast<float>(h[j] / sum);
            }
        }
    }
};

inline const SincTable &sincTable()
{
    static const SincTable table;
    return table;
}

/// One stereo windowed-sinc read at pos. The 16 coefficients are interpolated between the two
/// nearest of the 512 phase rows, then applied to both channels.
inline void sinc16(const SincTable &table, const float *srcL, const float *srcR, int64_t frames,
                   double pos, float &outL, float &outR) noexcept
{
    constexpr int kTaps = SincTable::kTaps;
    outL = 0.0f;
    outR = 0.0f;
    // Outside [-8, frames + 7) every tap misses the source. This check also rejects NaN and
    // ±inf before the integer conversion.
    if (!(pos >= -8.0 && pos < static_cast<double>(frames) + 7.0))
    {
        return;
    }
    const double base = std::floor(pos);
    const int64_t first = static_cast<int64_t>(base) - SincTable::kBefore;
    const double rowPos = (pos - base) * SincTable::kPhases; // exact; in [0, kPhases)
    const auto row = static_cast<int>(rowPos);
    const auto u = static_cast<float>(rowPos - row);
    const float *c0 = &table.coef[static_cast<std::size_t>(row) * kTaps];
    const float *c1 = c0 + kTaps;

    float c[kTaps];
    for (int j = 0; j < kTaps; ++j)
    {
        c[j] = c0[j] + (c1[j] - c0[j]) * u;
    }
    float l = 0.0f;
    float r = 0.0f;
    if (first >= 0 && first + kTaps <= frames)
    {
        for (int j = 0; j < kTaps; ++j)
        {
            l += c[j] * srcL[first + j];
            r += c[j] * srcR[first + j];
        }
    }
    else
    {
        for (int j = 0; j < kTaps; ++j)
        {
            l += c[j] * tap(srcL, frames, first + j);
            r += c[j] * tap(srcR, frames, first + j);
        }
    }
    outL = l;
    outR = r;
}

/// True when every read of the block, at gr.position(i) for i in [0, n), has all its taps
/// floor(p) - before .. floor(p) + after inside [0, frames), so they can be loaded unchecked.
/// The positions are monotonic in i, so the first and last decide. A NaN position fails.
/// Needs n >= 1.
inline bool readsInside(const Grain &gr, uint32_t n, int64_t frames, int before, int after) noexcept
{
    const double a = gr.position(0);
    const double z = gr.position(n - 1);
    // If either is NaN, the comparison is false, lo = z and hi = a, so one check below fails.
    const double lo = a < z ? a : z;
    const double hi = a < z ? z : a;
    return lo >= static_cast<double>(before) && hi < static_cast<double>(frames - after);
}

inline SIMD_M128 load4(const float *p) noexcept { return SIMD_MM(load_ps)(p); }

/// Grain gr's window times gain for samples i .. i + 3, 4-wide, with the same arithmetic as
/// HannTable::at (which the scalar kernels call) on the points from hannPoints().
inline SIMD_M128 amp4(const float *points, const Grain &gr, uint32_t i) noexcept
{
    const SIMD_M128 zero = SIMD_MM(setzero_ps)();
    const SIMD_M128 one = SIMD_MM(set1_ps)(1.0f);
    const SIMD_M128 size = SIMD_MM(set1_ps)(static_cast<float>(HannTable::kSize));
    const SIMD_M128 lastSegment = SIMD_MM(set1_ps)(static_cast<float>(HannTable::kSize - 1));

    // gr.phase(i + k) for the 4 lanes. i + k is exact in float.
    const SIMD_M128 k = SIMD_MM(add_ps)(SIMD_MM(set1_ps)(static_cast<float>(i)),
                                        SIMD_MM(set_ps)(3.0f, 2.0f, 1.0f, 0.0f));
    const SIMD_M128 phase = SIMD_MM(add_ps)(SIMD_MM(set1_ps)(gr.envPhase),
                                            SIMD_MM(mul_ps)(k, SIMD_MM(set1_ps)(gr.envInc)));
    // at() is zero outside [0, 1] and for NaN. Zeroing those lanes' x also keeps their index
    // in range.
    const SIMD_M128 valid =
        SIMD_MM(and_ps)(SIMD_MM(cmpge_ps)(phase, zero), SIMD_MM(cmple_ps)(phase, one));
    const SIMD_M128 x = SIMD_MM(and_ps)(SIMD_MM(mul_ps)(phase, size), valid); // exact
    // Phase 1 is the end of the last segment, as in at().
    const SIMD_M128I index = SIMD_MM(cvttps_epi32)(SIMD_MM(min_ps)(x, lastSegment));
    const SIMD_M128 t = SIMD_MM(sub_ps)(x, SIMD_MM(cvtepi32_ps)(index));

    alignas(16) int32_t ix[4];
    alignas(16) float lo[4];
    alignas(16) float hi[4];
    SIMD_MM(store_si128)(reinterpret_cast<SIMD_M128I *>(ix), index);
    for (int lane = 0; lane < 4; ++lane)
    {
        lo[lane] = points[ix[lane]];
        hi[lane] = points[ix[lane] + 1];
    }
    const SIMD_M128 a = load4(lo);
    const SIMD_M128 w = SIMD_MM(add_ps)(a, SIMD_MM(mul_ps)(SIMD_MM(sub_ps)(load4(hi), a), t));
    return SIMD_MM(mul_ps)(SIMD_MM(and_ps)(w, valid), SIMD_MM(set1_ps)(gr.gain));
}

/// Linear interpolation, as gg::interp::linear, on its 2 taps floor(p) .. floor(p) + 1.
struct LinearReader
{
    static constexpr int kBefore = 0;
    static constexpr int kAfter = 1;
    static constexpr auto scalar = gg::interp::linear;

    /// 4 reads whose taps are all inside the source: lane k reads at at[k] + t[k].
    static SIMD_M128 read4(const float *src, const int64_t *at, SIMD_M128 t) noexcept
    {
        alignas(16) float q0[4];
        alignas(16) float q1[4];
        for (int lane = 0; lane < 4; ++lane)
        {
            q0[lane] = src[at[lane]];
            q1[lane] = src[at[lane] + 1];
        }
        const SIMD_M128 p0 = load4(q0);
        return SIMD_MM(add_ps)(p0, SIMD_MM(mul_ps)(SIMD_MM(sub_ps)(load4(q1), p0), t));
    }
};

/// 4-point Catmull-Rom, as gg::interp::hermite4, on its 4 taps floor(p) - 1 .. floor(p) + 2.
struct HermiteReader
{
    static constexpr int kBefore = 1;
    static constexpr int kAfter = 2;
    static constexpr auto scalar = gg::interp::hermite4;

    static SIMD_M128 read4(const float *src, const int64_t *at, SIMD_M128 t) noexcept
    {
        alignas(16) float q0[4];
        alignas(16) float q1[4];
        alignas(16) float q2[4];
        alignas(16) float q3[4];
        for (int lane = 0; lane < 4; ++lane)
        {
            q0[lane] = src[at[lane] - 1];
            q1[lane] = src[at[lane]];
            q2[lane] = src[at[lane] + 1];
            q3[lane] = src[at[lane] + 2];
        }
        const SIMD_M128 p0 = load4(q0);
        const SIMD_M128 p1 = load4(q1);
        const SIMD_M128 p2 = load4(q2);
        const SIMD_M128 p3 = load4(q3);
        const SIMD_M128 half = SIMD_MM(set1_ps)(0.5f);
        // c1 = 0.5·(p2 - p0)
        const SIMD_M128 c1 = SIMD_MM(mul_ps)(half, SIMD_MM(sub_ps)(p2, p0));
        // c2 = p0 - 2.5·p1 + 2·p2 - 0.5·p3, evaluated left to right
        const SIMD_M128 c2 = SIMD_MM(sub_ps)(
            SIMD_MM(add_ps)(SIMD_MM(sub_ps)(p0, SIMD_MM(mul_ps)(SIMD_MM(set1_ps)(2.5f), p1)),
                            SIMD_MM(mul_ps)(SIMD_MM(set1_ps)(2.0f), p2)),
            SIMD_MM(mul_ps)(half, p3));
        // c3 = 0.5·(p3 - p0) + 1.5·(p1 - p2)
        const SIMD_M128 c3 =
            SIMD_MM(add_ps)(SIMD_MM(mul_ps)(half, SIMD_MM(sub_ps)(p3, p0)),
                            SIMD_MM(mul_ps)(SIMD_MM(set1_ps)(1.5f), SIMD_MM(sub_ps)(p1, p2)));
        // ((c3·t + c2)·t + c1)·t + p1
        SIMD_M128 y = SIMD_MM(add_ps)(SIMD_MM(mul_ps)(c3, t), c2);
        y = SIMD_MM(add_ps)(SIMD_MM(mul_ps)(y, t), c1);
        return SIMD_MM(add_ps)(SIMD_MM(mul_ps)(y, t), p1);
    }
};

/// The grain-major SIMD driver: for each grain, 4 consecutive output samples per step.
template <typename Reader>
inline void renderSimd(const GrainBatch &b, const float *srcL, const float *srcR, int64_t frames,
                       float *outL, float *outR, uint32_t n) noexcept
{
    if (n == 0)
    {
        return;
    }
    const HannTable &win = hann();
    const float *points = hannPoints().data();
    const uint32_t steps = n - n % 4;
    for (std::size_t g = 0; g < b.size(); ++g)
    {
        const Grain gr = grain(b, g);
        const bool inside = readsInside(gr, n, frames, Reader::kBefore, Reader::kAfter);
        for (uint32_t i = 0; i < steps; i += 4)
        {
            SIMD_M128 l;
            SIMD_M128 r;
            if (inside)
            {
                // Each lane's first tap and fraction. p >= 0 here, so truncation is floor.
                int64_t at[4];
                alignas(16) float t[4];
                for (uint32_t lane = 0; lane < 4; ++lane)
                {
                    const double p = gr.position(i + lane);
                    at[lane] = static_cast<int64_t>(p);
                    t[lane] = static_cast<float>(p - static_cast<double>(at[lane]));
                }
                const SIMD_M128 tv = load4(t);
                l = Reader::read4(srcL, at, tv);
                r = Reader::read4(srcR, at, tv);
            }
            else
            {
                // Near or past the ends of the source: the bounds-checked scalar reader per lane.
                alignas(16) float sl[4];
                alignas(16) float sr[4];
                for (uint32_t lane = 0; lane < 4; ++lane)
                {
                    const double p = gr.position(i + lane);
                    sl[lane] = Reader::scalar(srcL, frames, p);
                    sr[lane] = Reader::scalar(srcR, frames, p);
                }
                l = load4(sl);
                r = load4(sr);
            }
            const SIMD_M128 amp = amp4(points, gr, i);
            SIMD_MM(storeu_ps)(
                outL + i, SIMD_MM(add_ps)(SIMD_MM(loadu_ps)(outL + i), SIMD_MM(mul_ps)(amp, l)));
            SIMD_MM(storeu_ps)(
                outR + i, SIMD_MM(add_ps)(SIMD_MM(loadu_ps)(outR + i), SIMD_MM(mul_ps)(amp, r)));
        }
        for (uint32_t i = steps; i < n; ++i)
        {
            addSample<Reader::scalar>(win, gr, i, srcL, srcR, frames, outL, outR);
        }
    }
}
} // namespace detail

/// Linear interpolation, scalar reference.
inline void renderLinearScalar(const GrainBatch &b, const float *srcL, const float *srcR,
                               int64_t frames, float *outL, float *outR, uint32_t n) noexcept
{
    detail::renderScalar<gg::interp::linear>(b, srcL, srcR, frames, outL, outR, n);
}

/// 4-point Catmull-Rom (Hermite) interpolation, scalar reference.
inline void renderHermiteScalar(const GrainBatch &b, const float *srcL, const float *srcR,
                                int64_t frames, float *outL, float *outR, uint32_t n) noexcept
{
    detail::renderScalar<gg::interp::hermite4>(b, srcL, srcR, frames, outL, outR, n);
}

/// 16-tap Blackman-windowed sinc with a 512-phase coefficient table, scalar.
inline void renderSincScalar(const GrainBatch &b, const float *srcL, const float *srcR,
                             int64_t frames, float *outL, float *outR, uint32_t n) noexcept
{
    const HannTable &win = detail::hann();
    const detail::SincTable &table = detail::sincTable();
    for (std::size_t g = 0; g < b.size(); ++g)
    {
        const detail::Grain gr = detail::grain(b, g);
        for (uint32_t i = 0; i < n; ++i)
        {
            const float amp = win.at(static_cast<double>(gr.phase(i))) * gr.gain;
            float l;
            float r;
            detail::sinc16(table, srcL, srcR, frames, gr.position(i), l, r);
            outL[i] += amp * l;
            outR[i] += amp * r;
        }
    }
}

/// Linear interpolation, 4-wide grain-major SIMD.
inline void renderLinearSimd(const GrainBatch &b, const float *srcL, const float *srcR,
                             int64_t frames, float *outL, float *outR, uint32_t n) noexcept
{
    detail::renderSimd<detail::LinearReader>(b, srcL, srcR, frames, outL, outR, n);
}

/// 4-point Catmull-Rom (Hermite) interpolation, 4-wide grain-major SIMD.
inline void renderHermiteSimd(const GrainBatch &b, const float *srcL, const float *srcR,
                              int64_t frames, float *outL, float *outR, uint32_t n) noexcept
{
    detail::renderSimd<detail::HermiteReader>(b, srcL, srcR, frames, outL, outR, n);
}
} // namespace gg::bench

#endif // GG_BENCH_KERNELS_H
