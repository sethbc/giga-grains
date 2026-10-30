// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_GRAINKERNELS_H
#define GG_GRAIN_GRAINKERNELS_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <sst/basic-blocks/simd/setup.h>
#include <sst/cpputils/rtsan_support.h>

#include "gg/core/FpContract.h"
#include "gg/core/Interpolate.h"
#include "gg/core/Window.h"
#include "gg/grain/Envelope.h"
#include "gg/grain/GrainPool.h"
#include "gg/grain/SincTable.h"
#include "gg/source/LiveSource.h"

/// The grain renderers: each adds one pool grain into the output over output samples
/// [from, to). The scalar reference renderers here define the engine's per-sample arithmetic
/// (spec §3, ruling 1); any faster kernel must produce their output bit for bit.
///
/// For grain g at output sample t, with everything latched at the onset (GrainRows):
///
///     e   = double(t − onsetSample) − onsetFrac    (double)
///     w   = HannTable::at(warp(e × invDuration))   (float)
///     pos = startPos + rate × e                     (double)
///
/// warp is warpPhase with the grain's latched Texture envelope (Envelope.h; the rows' env*
/// arrays), the identity at texture 0, which maps [0, 1] onto [0, 1]: the window is zero wherever
/// e × invDuration is outside [0, 1], as without it, so the SIMD kernels' runs, which test
/// the unwarped phase, are unchanged.
///
/// Only samples with w > 0 contribute; elsewhere nothing is read and nothing is added. With two
/// outputs, dst[c][t] += read_c(pos) × (w × gain_c), with gain_c the latched gainL or gainR. One
/// output carries source channel 0 with gain alone: dst[0][t] += read_0(pos) × (w × gain).
///
/// Every sample depends only on t and the grain, never on which call renders it, so splitting
/// [from, to) anywhere gives the same bits.
///
/// A reader policy (Hermite, Linear, Sinc) gives the scalar read the references use, its reach (a
/// read at pos takes taps floor(pos) − kBefore .. floor(pos) + kAfter), read4, the same read for 4
/// lanes whose taps are all inside the buffer, which the SIMD kernels use, and the live ring's
/// read and head-proximity fade, with the reach the fade is for (kFadeBefore, kFadeAfter). Every
/// read takes the grain's ReadParams, latched per grain: the sinc's table and stretch, which only
/// Sinc reads.
namespace gg::kernels
{
/// What a reader takes beside its taps: the sinc's table and the grain's stretch (spec §6.2).
/// Hermite and Linear ignore both.
struct ReadParams
{
    const SincTable *sinc;
    float stretch;
};

/// The stretch of a grain whose residual rate is r (spec §6.2): s = min(1, 1 / r), one IEEE
/// division, so the sinc's cutoff follows an upward rate and stays at the source's Nyquist
/// otherwise. A rate that isn't above 1, NaN included, gives 1.
inline float stretchOf(double residualRate) noexcept
{
    GG_FP_CONTRACT_OFF
    return residualRate > 1.0 ? static_cast<float>(1.0 / residualRate) : 1.0f;
}

/// The tables the renderers read: the grain window, and the HQ tier's windowed sinc.
struct Tables
{
    const HannTable &hann;
    const SincTable &sinc;
};

/// A sample buffer's channels. data[1] is read only when !mono and there are two outputs; a mono
/// source feeds both outputs from data[0]. frames may be 0 (with null data): every read is then
/// silence.
///
/// interleaved is null, or, for a stereo source, the same frames interleaved
/// (InterleavedStereo::data()): frame f of channel c is interleaved[2f + c] as well as
/// data[c][f]. With two outputs, the SIMD interior then reads both channels' taps from it; the
/// reference always reads data.
struct SampleSource
{
    const float *data[2];
    int64_t frames;
    bool mono;
    const float *interleaved;
};

/// Where a renderer adds: output sample t is dst[c][t − blockStart]. dst[0] is non-null; dst[1]
/// null means one output.
struct Output
{
    float *dst[2];
    int64_t blockStart;
};

namespace detail
{
/// Transposes the 4×4 matrix whose rows are r[0..3], in place: afterwards r[k] holds lane k of
/// each old row. It only moves floats, so every bit is kept.
inline void transpose4(SIMD_M128 *r) noexcept SST_CPPUTILS_NONBLOCKING
{
    const SIMD_M128 t0 = SIMD_MM(unpacklo_ps)(r[0], r[1]); // r0[0] r1[0] r0[1] r1[1]
    const SIMD_M128 t1 = SIMD_MM(unpacklo_ps)(r[2], r[3]); // r2[0] r3[0] r2[1] r3[1]
    const SIMD_M128 t2 = SIMD_MM(unpackhi_ps)(r[0], r[1]); // r0[2] r1[2] r0[3] r1[3]
    const SIMD_M128 t3 = SIMD_MM(unpackhi_ps)(r[2], r[3]); // r2[2] r3[2] r2[3] r3[3]
    r[0] = SIMD_MM(movelh_ps)(t0, t1);
    r[1] = SIMD_MM(movehl_ps)(t1, t0);
    r[2] = SIMD_MM(movelh_ps)(t2, t3);
    r[3] = SIMD_MM(movehl_ps)(t3, t2);
}

/// clamp(x, 0, 1), with NaN read as 0: LiveSource::fade's clamp.
inline double unit(double x) noexcept { return x > 0.0 ? (x < 1.0 ? x : 1.0) : 0.0; }
} // namespace detail

/// The Normal tier's reader: the 4-point Hermite (Catmull-Rom) over taps floor(pos) − 1 ..
/// floor(pos) + 2, from a sample buffer (interp::hermite4) or the live ring (LiveSource::read).
struct Hermite
{
    static constexpr int64_t kBefore = 1;
    static constexpr int64_t kAfter = 2;
    /// The reach LiveSource::fade is for: the Hermite's own.
    static constexpr int64_t kFadeBefore = 1;
    static constexpr int64_t kFadeAfter = 2;

    static float read(const float *data, int64_t frames, double pos,
                      const ReadParams & /*params*/) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return interp::hermite4(data, frames, pos);
    }

    /// interp::hermite4 for 4 lanes whose taps are all inside data: lane k reads at
    /// base[k] + frac[k], with base[k] = floor(pos) and frac[k] = float(pos − floor(pos)), through
    /// interp::catmullRom's expressions in its order. Frame f is data[kStride × f]: 1 for a planar
    /// channel, 2 for one channel of interleaved pairs.
    template <int64_t kStride = 1>
    static SIMD_M128 read4(const float *data, const int64_t *base, SIMD_M128 frac,
                           const ReadParams & /*params*/) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        alignas(16) float q0[4];
        alignas(16) float q1[4];
        alignas(16) float q2[4];
        alignas(16) float q3[4];
        for (int lane = 0; lane < 4; ++lane)
        {
            const float *p = data + kStride * base[lane];
            q0[lane] = p[-kStride];
            q1[lane] = p[0];
            q2[lane] = p[kStride];
            q3[lane] = p[2 * kStride];
        }
        return curve4(SIMD_MM(load_ps)(q0), SIMD_MM(load_ps)(q1), SIMD_MM(load_ps)(q2),
                      SIMD_MM(load_ps)(q3), frac);
    }

    /// read4 for both channels of interleaved pairs (frame f's channel c at pairs[2f + c]), whose
    /// taps are all inside: a lane's 4 taps of both channels are the 8 floats from
    /// pairs + 2 × (base − 1), two 4-float loads, transposed into each channel's taps.
    static void read4Pair(const float *pairs, const int64_t *base, SIMD_M128 frac,
                          const ReadParams & /*params*/, SIMD_M128 &left,
                          SIMD_M128 &right) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        SIMD_M128 a[4]; // lane k: taps −1 and 0, L R L R
        SIMD_M128 b[4]; // lane k: taps 1 and 2, L R L R
        for (int lane = 0; lane < 4; ++lane)
        {
            const float *p = pairs + 2 * (base[lane] - 1);
            a[lane] = SIMD_MM(loadu_ps)(p);
            b[lane] = SIMD_MM(loadu_ps)(p + 4);
        }
        detail::transpose4(a); // tap −1 left, tap −1 right, tap 0 left, tap 0 right
        detail::transpose4(b); // tap 1 left, tap 1 right, tap 2 left, tap 2 right
        left = curve4(a[0], a[2], b[0], b[2], frac);
        right = curve4(a[1], a[3], b[1], b[3], frac);
    }

    /// interp::catmullRom(p0, p1, p2, p3, t) in 4 lanes, through its expressions in its order.
    static SIMD_M128 curve4(SIMD_M128 p0, SIMD_M128 p1, SIMD_M128 p2, SIMD_M128 p3,
                            SIMD_M128 frac) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        const SIMD_M128 half = SIMD_MM(set1_ps)(0.5f);
        // c1 = 0.5 × (p2 − p0)
        const SIMD_M128 c1 = SIMD_MM(mul_ps)(half, SIMD_MM(sub_ps)(p2, p0));
        // c2 = ((p0 − 2.5 × p1) + 2 × p2) − 0.5 × p3
        const SIMD_M128 c2 = SIMD_MM(sub_ps)(
            SIMD_MM(add_ps)(SIMD_MM(sub_ps)(p0, SIMD_MM(mul_ps)(SIMD_MM(set1_ps)(2.5f), p1)),
                            SIMD_MM(mul_ps)(SIMD_MM(set1_ps)(2.0f), p2)),
            SIMD_MM(mul_ps)(half, p3));
        // c3 = 0.5 × (p3 − p0) + 1.5 × (p1 − p2)
        const SIMD_M128 c3 =
            SIMD_MM(add_ps)(SIMD_MM(mul_ps)(half, SIMD_MM(sub_ps)(p3, p0)),
                            SIMD_MM(mul_ps)(SIMD_MM(set1_ps)(1.5f), SIMD_MM(sub_ps)(p1, p2)));
        // ((c3 × t + c2) × t + c1) × t + p1
        SIMD_M128 y = SIMD_MM(add_ps)(SIMD_MM(mul_ps)(c3, frac), c2);
        y = SIMD_MM(add_ps)(SIMD_MM(mul_ps)(y, frac), c1);
        return SIMD_MM(add_ps)(SIMD_MM(mul_ps)(y, frac), p1);
    }

    /// False, reading nothing, unless every tap is inside the ring's window at head.
    static bool readLive(const LiveSource &live, double pos, int64_t head,
                         LiveSource::Cursor &cursor, const ReadParams & /*params*/, float &l,
                         float &r) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return live.read(pos, head, cursor, l, r);
    }

    /// The head-proximity fade for this reach: LiveSource::fade.
    static double fade(const LiveSource &live, double pos,
                       int64_t head) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return live.fade(pos, head);
    }
};

/// The Eco tier's reader: linear interpolation over taps floor(pos) and floor(pos) + 1, from a
/// sample buffer (interp::linear) or the live ring (LiveSource::readLinear).
struct Linear
{
    static constexpr int64_t kBefore = 0;
    static constexpr int64_t kAfter = 1;
    /// The reach LiveSource::fade is for: the Hermite's, a frame wider than the line's at each
    /// end, so the fade is conservative for it (LiveSource::readLinear).
    static constexpr int64_t kFadeBefore = 1;
    static constexpr int64_t kFadeAfter = 2;

    static float read(const float *data, int64_t frames, double pos,
                      const ReadParams & /*params*/) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return interp::linear(data, frames, pos);
    }

    /// interp::linear for 4 lanes whose taps are all inside data: lane k reads at
    /// base[k] + frac[k], as Hermite::read4 (kStride too), through interp::lerp's a + (b − a) × t.
    template <int64_t kStride = 1>
    static SIMD_M128 read4(const float *data, const int64_t *base, SIMD_M128 frac,
                           const ReadParams & /*params*/) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        alignas(16) float q0[4];
        alignas(16) float q1[4];
        for (int lane = 0; lane < 4; ++lane)
        {
            const float *p = data + kStride * base[lane];
            q0[lane] = p[0];
            q1[lane] = p[kStride];
        }
        return line4(SIMD_MM(load_ps)(q0), SIMD_MM(load_ps)(q1), frac);
    }

    /// read4 for both channels of interleaved pairs, as Hermite::read4Pair: a lane's 2 taps of
    /// both channels are the 4 floats from pairs + 2 × base, one load, transposed.
    static void read4Pair(const float *pairs, const int64_t *base, SIMD_M128 frac,
                          const ReadParams & /*params*/, SIMD_M128 &left,
                          SIMD_M128 &right) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        SIMD_M128 a[4]; // lane k: taps 0 and 1, L R L R
        for (int lane = 0; lane < 4; ++lane)
        {
            a[lane] = SIMD_MM(loadu_ps)(pairs + 2 * base[lane]);
        }
        detail::transpose4(a); // tap 0 left, tap 0 right, tap 1 left, tap 1 right
        left = line4(a[0], a[2], frac);
        right = line4(a[1], a[3], frac);
    }

    /// interp::lerp(a, b, t) in 4 lanes: a + (b − a) × t.
    static SIMD_M128 line4(SIMD_M128 a, SIMD_M128 b,
                           SIMD_M128 frac) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        return SIMD_MM(add_ps)(a, SIMD_MM(mul_ps)(SIMD_MM(sub_ps)(b, a), frac));
    }

    /// False, reading nothing, unless both taps are inside the ring's window at head.
    static bool readLive(const LiveSource &live, double pos, int64_t head,
                         LiveSource::Cursor &cursor, const ReadParams & /*params*/, float &l,
                         float &r) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return live.readLinear(pos, head, cursor, l, r);
    }

    /// The head-proximity fade: LiveSource::fade, for the Hermite's reach (kFadeBefore,
    /// kFadeAfter).
    static double fade(const LiveSource &live, double pos,
                       int64_t head) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return live.fade(pos, head);
    }
};

/// The HQ tier's reader (spec §6.2): the 16-tap windowed sinc over taps floor(pos) − 7 ..
/// floor(pos) + 8, stretched by the grain's stretch s (stretchOf). For a read at pos with
/// frac = float(pos − floor(pos)), in float and in k order:
///
///     c_k = SincTable::at((float(k) − frac) × s)        for k = −7 … 8
///     y   = (Σ c_k × tap_k) / (Σ c_k), or 0 where Σ c_k == 0
///
/// The normalization keeps DC exact and the gain independent of s. From a sample buffer a tap
/// outside [0, frames) reads 0; from the live ring the read needs all 16 taps inside the window
/// (LiveSource::gatherTaps<16>), and the fade is for the sinc's own reach.
struct Sinc
{
    static constexpr int64_t kTaps = SincTable::kTaps;
    static constexpr int64_t kBefore = SincTable::kBefore;
    static constexpr int64_t kAfter = SincTable::kAfter;
    static constexpr int64_t kFadeBefore = kBefore;
    static constexpr int64_t kFadeAfter = kAfter;

    /// The read above at pos, with frame f of the channel at data[kStride × f]: 1 for a planar
    /// channel, 2 for one channel of interleaved pairs. 0 unless pos is in (−9, frames + 8),
    /// outside which every tap is outside the source; that test also rejects NaN and ±inf.
    template <int64_t kStride = 1>
    static float read(const float *data, int64_t frames, double pos,
                      const ReadParams &params) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        if (!(pos > -9.0 && pos < static_cast<double>(frames) + 8.0))
        {
            return 0.0f;
        }
        const double base = std::floor(pos);
        const auto i = static_cast<int64_t>(base);
        const auto frac = static_cast<float>(pos - base);
        float num = 0.0f;
        float den = 0.0f;
        for (int64_t k = -kBefore; k <= kAfter; ++k)
        {
            const float c = params.sinc->at((static_cast<float>(k) - frac) * params.stretch);
            const int64_t f = i + k;
            const float tap = f >= 0 && f < frames ? data[kStride * f] : 0.0f;
            num += c * tap;
            den += c;
        }
        return den != 0.0f ? num / den : 0.0f;
    }

    /// read for 4 lanes whose taps are all inside data: lane k reads at base[k] + frac[k], as
    /// Hermite::read4 (kStride too), with the scalar read's arithmetic lane by lane.
    template <int64_t kStride = 1>
    static SIMD_M128 read4(const float *data, const int64_t *base, SIMD_M128 frac,
                           const ReadParams &params) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        SIMD_M128 taps[kTaps];
        for (int64_t k = 0; k < kTaps; ++k)
        {
            alignas(16) float q[4];
            for (int lane = 0; lane < 4; ++lane)
            {
                q[lane] = data[kStride * (base[lane] - kBefore + k)];
            }
            taps[k] = SIMD_MM(load_ps)(q);
        }
        SIMD_M128 c[kTaps];
        const SIMD_M128 den = coefficients4(frac, params, c);
        return sum4(c, den, taps);
    }

    /// read4 for both channels of interleaved pairs, whose taps are all inside: a lane's 16 taps
    /// of both channels are the 32 floats from pairs + 2 × (base − 7), eight 4-float loads, each
    /// transposed across the lanes into two taps of each channel. Both channels share the lanes'
    /// coefficients, which the scalar read computes the same for each.
    static void read4Pair(const float *pairs, const int64_t *base, SIMD_M128 frac,
                          const ReadParams &params, SIMD_M128 &left,
                          SIMD_M128 &right) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        SIMD_M128 tapsL[kTaps];
        SIMD_M128 tapsR[kTaps];
        for (int64_t m = 0; m < kTaps / 2; ++m)
        {
            SIMD_M128 a[4]; // lane k: taps 2m and 2m + 1 (from the first), L R L R
            for (int lane = 0; lane < 4; ++lane)
            {
                a[lane] = SIMD_MM(loadu_ps)(pairs + 2 * (base[lane] - kBefore) + 4 * m);
            }
            detail::transpose4(a); // tap 2m left, tap 2m right, tap 2m + 1 left, tap 2m + 1 right
            tapsL[2 * m] = a[0];
            tapsR[2 * m] = a[1];
            tapsL[2 * m + 1] = a[2];
            tapsR[2 * m + 1] = a[3];
        }
        SIMD_M128 c[kTaps];
        const SIMD_M128 den = coefficients4(frac, params, c);
        left = sum4(c, den, tapsL);
        right = sum4(c, den, tapsR);
    }

    /// The 4 lanes' coefficients, c[k + 7] holding c_k: each lane's from SincTable::at, scalar,
    /// with the scalar read's x. Returns den = Σ c_k, from 0 in k order, as the scalar read sums
    /// it.
    static SIMD_M128 coefficients4(SIMD_M128 frac, const ReadParams &params,
                                   SIMD_M128 *c) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        alignas(16) float f[4];
        SIMD_MM(store_ps)(f, frac);
        SIMD_M128 den = SIMD_MM(setzero_ps)();
        for (int64_t k = 0; k < kTaps; ++k)
        {
            const auto offset = static_cast<float>(k - kBefore);
            alignas(16) float q[4];
            for (int lane = 0; lane < 4; ++lane)
            {
                q[lane] = params.sinc->at((offset - f[lane]) * params.stretch);
            }
            c[k] = SIMD_MM(load_ps)(q);
            den = SIMD_MM(add_ps)(den, c[k]);
        }
        return den;
    }

    /// (Σ c_k × tap_k) / den in 4 lanes, the sum from 0 in k order, and 0 in a lane whose den is
    /// 0: the scalar read's last two lines.
    static SIMD_M128 sum4(const SIMD_M128 *c, SIMD_M128 den,
                          const SIMD_M128 *taps) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        const SIMD_M128 zero = SIMD_MM(setzero_ps)();
        SIMD_M128 num = zero;
        for (int64_t k = 0; k < kTaps; ++k)
        {
            num = SIMD_MM(add_ps)(num, SIMD_MM(mul_ps)(c[k], taps[k]));
        }
        // den != 0 is true for a NaN den too, where the scalar read divides as well.
        return SIMD_MM(and_ps)(SIMD_MM(cmpneq_ps)(den, zero), SIMD_MM(div_ps)(num, den));
    }

    /// Both channels at pos from the live ring: the read above over the 16 taps
    /// LiveSource::gatherTaps<16> gathers. False, reading nothing, unless all 16 are inside the
    /// ring's window at head.
    static bool readLive(const LiveSource &live, double pos, int64_t head,
                         LiveSource::Cursor &cursor, const ReadParams &params, float &l,
                         float &r) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        float tapsL[kTaps];
        float tapsR[kTaps];
        if (!live.gatherTaps<static_cast<int>(kTaps)>(pos, head, cursor, tapsL, tapsR))
        {
            return false;
        }
        const double base = std::floor(pos); // gatherTaps accepted pos: it is finite
        const auto frac = static_cast<float>(pos - base);
        float numL = 0.0f;
        float numR = 0.0f;
        float den = 0.0f;
        for (int64_t k = 0; k < kTaps; ++k)
        {
            const float c =
                params.sinc->at((static_cast<float>(k - kBefore) - frac) * params.stretch);
            numL += c * tapsL[k];
            numR += c * tapsR[k];
            den += c;
        }
        l = den != 0.0f ? numL / den : 0.0f;
        r = den != 0.0f ? numR / den : 0.0f;
        return true;
    }

    /// The HQ head-proximity fade (spec §6.2): LiveSource::fade's expression with the sinc's
    /// reach,
    ///
    ///     g = clamp((R − 1 − (pos + 8)) / M, 0, 1) × clamp(((pos − 7) − (R − N)) / M, 0, 1)
    ///
    /// so g is 0 before any of the 16 taps leaves the window [R − N, R − 1].
    static double fade(const LiveSource &live, double pos,
                       int64_t head) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        constexpr auto kMargin = static_cast<double>(LiveSource::kHeadMargin);
        const auto r = static_cast<double>(head);
        const double toHead = (r - 1.0 - (pos + static_cast<double>(kAfter))) / kMargin;
        const double fromTail =
            ((pos - static_cast<double>(kBefore)) - (r - static_cast<double>(live.frames()))) /
            kMargin;
        return detail::unit(toHead) * detail::unit(fromTail);
    }
};

namespace detail
{
/// Grain i's latched Texture envelope (GrainRows' env* arrays), read once per grain or call.
inline EnvelopeShape envelopeOf(const GrainRows &rows, uint32_t i) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    return {rows.envAttackEnd[i], rows.envReleaseStart[i], rows.envAttackScale[i],
            rows.envReleaseScale[i]};
}
} // namespace detail

namespace detail
{
/// Grain i's ReadParams: the sinc's table and the stretch of its rate (stretchOf).
inline ReadParams paramsOf(const GrainRows &rows, uint32_t i,
                           const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    return {&tables.sinc, stretchOf(rows.rate[i])};
}
} // namespace detail

/// The scalar reference over a sample source: grain i of rows, over [from, to), read with
/// Reader::read and the grain's ReadParams.
template <class Reader>
void renderSampleRef(const GrainRows &rows, uint32_t i, const SampleSource &src, const Output &out,
                     int64_t from, int64_t to,
                     const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    const int64_t onsetSample = rows.onsetSample[i];
    const double onsetFrac = rows.onsetFrac[i];
    const double startPos = rows.startPos[i];
    const double rate = rows.rate[i];
    const double invDuration = rows.invDuration[i];
    const int64_t first = std::max(from, onsetSample);
    const int64_t last = std::min(to, rows.endSample[i]); // exclusive
    if (first >= last)
    {
        return; // nothing to render (as at most edges of the SIMD kernels' runs)
    }
    const EnvelopeShape envelope = detail::envelopeOf(rows, i);
    const ReadParams params = detail::paramsOf(rows, i, tables);
    const HannTable &hann = tables.hann;
    float *const dst0 = out.dst[0];
    float *const dst1 = out.dst[1];
    if (dst1 == nullptr)
    {
        const float gain = rows.gain[i];
        for (int64_t t = first; t < last; ++t)
        {
            const double e = static_cast<double>(t - onsetSample) - onsetFrac;
            const float w = hann.at(warpPhase(e * invDuration, envelope));
            if (!(w > 0.0f))
            {
                continue;
            }
            const double pos = startPos + rate * e;
            const auto j = static_cast<std::size_t>(t - out.blockStart);
            dst0[j] += Reader::read(src.data[0], src.frames, pos, params) * (w * gain);
        }
        return;
    }
    const float gainL = rows.gainL[i];
    const float gainR = rows.gainR[i];
    for (int64_t t = first; t < last; ++t)
    {
        const double e = static_cast<double>(t - onsetSample) - onsetFrac;
        const float w = hann.at(warpPhase(e * invDuration, envelope));
        if (!(w > 0.0f))
        {
            continue;
        }
        const double pos = startPos + rate * e;
        const auto j = static_cast<std::size_t>(t - out.blockStart);
        const float s0 = Reader::read(src.data[0], src.frames, pos, params);
        const float s1 = src.mono ? s0 : Reader::read(src.data[1], src.frames, pos, params);
        dst0[j] += s0 * (w * gainL);
        dst1[j] += s1 * (w * gainR);
    }
}

namespace detail
{
/// Output samples [begin, end).
struct Run
{
    int64_t begin;
    int64_t end;
};

/// The run of grain i's samples in [first, last) that the SIMD sample kernel renders: exactly
/// the samples t whose window phase e × invDuration is in [0, 1] and whose read position pos(t)
/// has every tap inside [0, frames), so kBefore <= pos < frames − kAfter. Requires first < last.
///
/// Folding the phase range into the run (accepted ruling, Task 6) keeps HannTable::at's range
/// check out of the interior's lanes: a grain's first sample (e = −onsetFrac, phase < 0 unless
/// onsetFrac is 0) and any sample past its window go through the reference instead, a few
/// samples per grain.
///
/// e and the phase never decrease with t, and pos is monotone in t (non-decreasing when rate >
/// 0): each is t's integer offset converted, then a subtraction, a product by a constant and a
/// sum, all rounded monotonically. So each condition holds on an interval of t, and so does their
/// conjunction. The bounds come from inverting the definitions, widened by 2 samples against
/// rounding; then each end steps inward while the exact per-sample test fails, so the run holds
/// only samples the test admits, and all of them. A rate that isn't positive, or a non-finite
/// inversion, leaves the bounds at [first, last) and the stepping finds the run (or none), testing
/// every sample: bounded by last − first, but no longer a few tests per end. The engine never
/// makes such a row: every grain's rate is the pitch ratio, exp2Det of a pitch clamped to ±48
/// semitones, times the quotient of two finite, positive sample rates (Engine::sampleGrain), so
/// it is finite and positive for any sample rates short of about 2^1000 apart.
template <class Reader>
Run sampleSimdRun(const GrainRows &rows, uint32_t i, int64_t frames, int64_t first,
                  int64_t last) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    const int64_t onsetSample = rows.onsetSample[i];
    const double onsetFrac = rows.onsetFrac[i];
    const double startPos = rows.startPos[i];
    const double rate = rows.rate[i];
    const double invDuration = rows.invDuration[i];
    const auto lo = static_cast<double>(Reader::kBefore);
    const auto hi = static_cast<double>(frames - Reader::kAfter);
    // The reference's e, phase and pos at t, and the SIMD condition on them.
    const auto simdable = [&](int64_t t) noexcept {
        GG_FP_CONTRACT_OFF
        const double e = static_cast<double>(t - onsetSample) - onsetFrac;
        const double phase = e * invDuration;
        const double pos = startPos + rate * e;
        return phase >= 0.0 && phase <= 1.0 && pos >= lo && pos < hi;
    };

    // In n = t − onsetSample: the phase is in [0, 1] for n in [onsetFrac, onsetFrac + duration],
    // and pos in [lo, hi) for n in [onsetFrac + (lo − startPos) / rate, onsetFrac + (hi −
    // startPos) / rate).
    const double nFirst = static_cast<double>(first - onsetSample);
    const double nLast = static_cast<double>(last - onsetSample);
    double nBegin = nFirst;
    double nEnd = nLast;
    if (rate > 0.0)
    {
        nBegin = std::ceil(std::max(onsetFrac, onsetFrac + (lo - startPos) / rate)) - 2.0;
        nEnd = std::ceil(std::min(onsetFrac + rows.durationSamples[i],
                                  onsetFrac + (hi - startPos) / rate)) +
               2.0;
    }
    // Clamp into [nFirst, nLast], NaN included, before converting.
    if (!(nBegin > nFirst))
    {
        nBegin = nFirst;
    }
    if (!(nBegin < nLast))
    {
        nBegin = nLast;
    }
    if (!(nEnd < nLast))
    {
        nEnd = nLast;
    }
    if (!(nEnd > nBegin))
    {
        nEnd = nBegin;
    }
    int64_t begin = std::clamp(onsetSample + static_cast<int64_t>(nBegin), first, last);
    int64_t end = std::clamp(onsetSample + static_cast<int64_t>(nEnd), begin, last);
    while (begin < end && !simdable(begin))
    {
        ++begin;
    }
    while (end > begin && !simdable(end - 1))
    {
        --end;
    }
    return {begin, end};
}

/// dst[0..3] += y in the lanes where mask is set; the other lanes keep their bits exactly. A blend,
/// not an add of +0: it is exactly the reference's skip for any dst, a −0, a NaN or a denormal
/// under DAZ included (accepted ruling, Task 6).
inline void addMasked(float *dst, SIMD_M128 y, SIMD_M128 mask) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    const SIMD_M128 d = SIMD_MM(loadu_ps)(dst);
    const SIMD_M128 sum = SIMD_MM(add_ps)(d, y);
    SIMD_MM(storeu_ps)(dst,
                       SIMD_MM(or_ps)(SIMD_MM(and_ps)(mask, sum), SIMD_MM(andnot_ps)(mask, d)));
}

/// Where the SIMD interior finds a sample buffer's planar taps: frame f of channel c is
/// data[c][f], so a lane's read4 base is floor(pos) itself. read gives channel 0's read in s0
/// and, when kBoth, channel 1's in s1; otherwise s1 is s0.
struct BufferTaps
{
    const float *data[2];

    void locate(int64_t *) const noexcept SST_CPPUTILS_NONBLOCKING {}

    template <class Reader, bool kBoth>
    void read(const int64_t *base, SIMD_M128 frac, const ReadParams &params, SIMD_M128 &s0,
              SIMD_M128 &s1) const noexcept SST_CPPUTILS_NONBLOCKING
    {
        s0 = Reader::read4(data[0], base, frac, params);
        s1 = s0;
        if constexpr (kBoth)
        {
            s1 = Reader::read4(data[1], base, frac, params);
        }
    }
};

/// Where the SIMD interior finds interleaved taps (InterleavedStereo, or the live ring's slots):
/// frame f's channel c is pairs[2f + c], and a lane's read4 base is floor(pos). read is
/// BufferTaps::read's: with kBoth, both channels from the lane's one run of pairs
/// (Reader::read4Pair); without, channel 0 alone, at a stride of 2.
struct InterleavedTaps
{
    const float *pairs;

    void locate(int64_t *) const noexcept SST_CPPUTILS_NONBLOCKING {}

    template <class Reader, bool kBoth>
    void read(const int64_t *base, SIMD_M128 frac, const ReadParams &params, SIMD_M128 &s0,
              SIMD_M128 &s1) const noexcept SST_CPPUTILS_NONBLOCKING
    {
        if constexpr (kBoth)
        {
            Reader::read4Pair(pairs, base, frac, params, s0, s1);
        }
        else
        {
            s0 = Reader::template read4<2>(pairs, base, frac, params);
            s1 = s0;
        }
    }
};

/// The SIMD interior: grain i over [begin, end), a multiple of 4 samples, all with the window's
/// phase in [0, 1] and every tap readable through Taps, 4 consecutive samples per step. Per
/// lane, in double as the reference: e, the window's x = warp(e × invDuration) × kSize with
/// HannTable::at's index and fraction, and pos with its floor and fraction. taps.locate turns the
/// 4 floors into the buffer indices the reads take taps around (BufferTaps, InterleavedTaps: the
/// floors; the live ring: their slots). Then 4-wide float, in the reference's order: the window
/// lerp, the w > 0 mask, the read (taps.read, through Reader's 4-lane read with the grain's
/// ReadParams), read × (w × gain_c), and the masked add. How the taps are laid out changes only
/// where the same floats are loaded from.
///
/// kTwoOutputs: dst[1] is written, with gainL and gainR; otherwise dst[0] alone, with gain.
/// kStereo: with two outputs, channel 1's read feeds dst[1]; otherwise dst[1] reuses channel 0's.
/// kWarp: the phase goes through warpPhase; renderInterior passes false only for a grain whose
/// envelope isIdentity, where warpPhase returns every phase in [0, 1] unchanged, so both give
/// the same bits and a plain-Hann grain (texture 0) skips the warp's cost.
template <class Reader, bool kTwoOutputs, bool kStereo, bool kWarp, class Taps>
void renderInteriorSteps(const GrainRows &rows, uint32_t i, const Taps &taps, const Output &out,
                         int64_t begin, int64_t end,
                         const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    const int64_t onsetSample = rows.onsetSample[i];
    const double onsetFrac = rows.onsetFrac[i];
    const double startPos = rows.startPos[i];
    const double rate = rows.rate[i];
    const double invDuration = rows.invDuration[i];
    [[maybe_unused]] const EnvelopeShape envelope = detail::envelopeOf(rows, i);
    // gain_c: the latched gainL and gainR, or with one output the grain's gain alone in gain0
    // (gainR is read only with two outputs).
    const SIMD_M128 gain0 = SIMD_MM(set1_ps)(kTwoOutputs ? rows.gainL[i] : rows.gain[i]);
    const SIMD_M128 gain1 = SIMD_MM(set1_ps)(kTwoOutputs ? rows.gainR[i] : 0.0f);
    const SIMD_M128 zero = SIMD_MM(setzero_ps)();
    const ReadParams params = paramsOf(rows, i, tables);
    const float *const points = tables.hann.points();
    float *const dst0 = out.dst[0];
    float *const dst1 = out.dst[1];
    for (int64_t t = begin; t < end; t += 4)
    {
        const int64_t j = t - out.blockStart;
        alignas(16) float wLo[4];
        alignas(16) float wHi[4];
        alignas(16) float wFrac[4];
        alignas(16) float posFrac[4];
        int64_t base[4];
        for (int lane = 0; lane < 4; ++lane)
        {
            const double e = static_cast<double>(t + lane - onsetSample) - onsetFrac;
            // HannTable::at of the warped phase, with the phase known to be in [0, 1], and so
            // the warped one too: phase 1 is the end of the last segment.
            double phase = e * invDuration;
            if constexpr (kWarp)
            {
                phase = warpPhase(phase, envelope);
            }
            const double x = phase * HannTable::kSize;
            const uint32_t k = std::min(static_cast<uint32_t>(x), HannTable::kSize - 1);
            wFrac[lane] = static_cast<float>(x - k);
            wLo[lane] = points[k];
            wHi[lane] = points[k + 1];
            // interp's floor and fraction.
            const double pos = startPos + rate * e;
            const double floorPos = std::floor(pos);
            base[lane] = static_cast<int64_t>(floorPos);
            posFrac[lane] = static_cast<float>(pos - floorPos);
        }
        taps.locate(base);
        const SIMD_M128 lo = SIMD_MM(load_ps)(wLo);
        const SIMD_M128 w =
            SIMD_MM(add_ps)(lo, SIMD_MM(mul_ps)(SIMD_MM(sub_ps)(SIMD_MM(load_ps)(wHi), lo),
                                                SIMD_MM(load_ps)(wFrac)));
        // The reference skips a sample unless w > 0: nothing is added there.
        const SIMD_M128 sounds = SIMD_MM(cmpgt_ps)(w, zero);
        const SIMD_M128 frac = SIMD_MM(load_ps)(posFrac);
        SIMD_M128 s0;
        SIMD_M128 s1;
        taps.template read<Reader, kTwoOutputs && kStereo>(base, frac, params, s0, s1);
        addMasked(dst0 + j, SIMD_MM(mul_ps)(s0, SIMD_MM(mul_ps)(w, gain0)), sounds);
        if constexpr (kTwoOutputs)
        {
            addMasked(dst1 + j, SIMD_MM(mul_ps)(s1, SIMD_MM(mul_ps)(w, gain1)), sounds);
        }
    }
}

/// The SIMD interior (renderInteriorSteps), with the warp only where the grain's envelope isn't
/// the identity.
template <class Reader, bool kTwoOutputs, bool kStereo, class Taps>
void renderInterior(const GrainRows &rows, uint32_t i, const Taps &taps, const Output &out,
                    int64_t begin, int64_t end,
                    const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    // No arithmetic here: the guard is for uniformity with every other kernel function.
    GG_FP_CONTRACT_OFF
    if (begin >= end)
    {
        return;
    }
    if (isIdentity(envelopeOf(rows, i)))
    {
        renderInteriorSteps<Reader, kTwoOutputs, kStereo, false>(rows, i, taps, out, begin, end,
                                                                 tables);
    }
    else
    {
        renderInteriorSteps<Reader, kTwoOutputs, kStereo, true>(rows, i, taps, out, begin, end,
                                                                tables);
    }
}
} // namespace detail

/// The 4-wide grain-major SIMD kernel over a sample source: renderSampleRef<Reader>'s output, bit
/// for bit. The samples in detail::sampleSimdRun, 4 at a time, go through the SIMD interior;
/// those before the run, and those after its last whole step of 4 (reads near or past the
/// source's ends, the window's edges, and the remainder), go through renderSampleRef itself.
/// Every sample depends only on t, so the pieces give the reference's bits. A stereo source into
/// two outputs reads src.interleaved when it is set, and the planar channels otherwise; the
/// interior's taps are all inside the source, where both hold the same floats.
template <class Reader>
void renderSampleSimd(const GrainRows &rows, uint32_t i, const SampleSource &src, const Output &out,
                      int64_t from, int64_t to,
                      const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    const int64_t first = std::max(from, rows.onsetSample[i]);
    const int64_t last = std::min(to, rows.endSample[i]); // exclusive
    if (first >= last)
    {
        return;
    }
    const detail::Run run = detail::sampleSimdRun<Reader>(rows, i, src.frames, first, last);
    const int64_t simdEnd = run.begin + (run.end - run.begin) / 4 * 4;
    const detail::BufferTaps taps{{src.data[0], src.data[1]}};
    renderSampleRef<Reader>(rows, i, src, out, first, run.begin, tables);
    if (out.dst[1] == nullptr)
    {
        detail::renderInterior<Reader, false, false>(rows, i, taps, out, run.begin, simdEnd,
                                                     tables);
    }
    else if (src.mono)
    {
        detail::renderInterior<Reader, true, false>(rows, i, taps, out, run.begin, simdEnd, tables);
    }
    else if (src.interleaved != nullptr)
    {
        const detail::InterleavedTaps pairs{src.interleaved};
        detail::renderInterior<Reader, true, true>(rows, i, pairs, out, run.begin, simdEnd, tables);
    }
    else
    {
        detail::renderInterior<Reader, true, true>(rows, i, taps, out, run.begin, simdEnd, tables);
    }
    renderSampleRef<Reader>(rows, i, src, out, simdEnd, last, tables);
}

/// The scalar reference over the live ring: grain i of rows, over [from, to), read with
/// Reader::readLive and the grain's ReadParams. [from, to) must lie in the ring's last write()
/// block, whose R(t) the head-proximity fade and the read use. Per sample, after w > 0:
///
///     f = float(Reader::fade(pos, R(t)))
///
/// (LiveSource::fade for Hermite and Linear, Sinc::fade for the sinc's reach), and the sample
/// contributes only when f > 0 and the read succeeds:
/// dst[c][t] += read_c(pos) × ((w × f) × gain_c), or, with one output, ring channel 0 ×
/// ((w × f) × gain).
template <class Reader>
void renderLiveRef(const GrainRows &rows, uint32_t i, const LiveSource &live, const Output &out,
                   int64_t from, int64_t to, const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    const int64_t onsetSample = rows.onsetSample[i];
    const double onsetFrac = rows.onsetFrac[i];
    const double startPos = rows.startPos[i];
    const double rate = rows.rate[i];
    const double invDuration = rows.invDuration[i];
    const int64_t first = std::max(from, onsetSample);
    const int64_t last = std::min(to, rows.endSample[i]); // exclusive
    if (first >= last)
    {
        return; // nothing to render (as at most edges of the SIMD kernels' runs)
    }
    const EnvelopeShape envelope = detail::envelopeOf(rows, i);
    const ReadParams params = detail::paramsOf(rows, i, tables);
    const HannTable &hann = tables.hann;
    float *const dst0 = out.dst[0];
    float *const dst1 = out.dst[1];
    const bool twoOutputs = dst1 != nullptr;
    // gain_c: the latched channel gains, or with one output the grain's gain alone; gainR is
    // read only with two outputs.
    const float gain0 = twoOutputs ? rows.gainL[i] : rows.gain[i];
    const float gain1 = twoOutputs ? rows.gainR[i] : 0.0f;
    LiveSource::Cursor cursor;
    for (int64_t t = first; t < last; ++t)
    {
        const double e = static_cast<double>(t - onsetSample) - onsetFrac;
        const float w = hann.at(warpPhase(e * invDuration, envelope));
        if (!(w > 0.0f))
        {
            continue;
        }
        // The read position is in ring time; the head is R(t), from this block's write.
        const double pos = startPos + rate * e;
        const int64_t head = live.headAt(t);
        const auto f = static_cast<float>(Reader::fade(live, pos, head));
        float l = 0.0f;
        float r = 0.0f;
        // Where the fade is 0 the taps aren't read at all, so stale data can never contribute.
        if (!(f > 0.0f) || !Reader::readLive(live, pos, head, cursor, params, l, r))
        {
            continue;
        }
        const float wf = w * f;
        const auto j = static_cast<std::size_t>(t - out.blockStart);
        dst0[j] += l * (wf * gain0);
        if (twoOutputs)
        {
            dst1[j] += r * (wf * gain1);
        }
    }
}

/// The samples of grain i where the SIMD live kernel may stand in for renderLiveRef: up to two
/// runs, part 0 before the last write() block's blockKink() (the head advancing) and part 1 from
/// it on (the head still). begin[p] <= end[p]; an empty part has begin[p] == end[p].
struct LiveInterior
{
    int64_t begin[2];
    int64_t end[2];
};

namespace detail
{
/// One part of liveInterior: the samples of [lo, hi) where R(t) = r0 + slope × (t − lo), with
/// slope 1 or 0, satisfies the interior's conditions. Requires lo < hi, live.enabled(), and a
/// finite rate > 0 and invDuration > 0.
///
/// The conditions, with e, pos and the phase as the reference computes them, and b and a the
/// reach Reader::fade is for (Reader::kFadeBefore and kFadeAfter: 1 and 2 for Hermite and
/// Linear, 7 and 8 for Sinc):
/// - the window's phase e × invDuration is in [0, 1];
/// - pos >= b, so the reach's first tap, floor(pos) − b, is a frame >= 0 (not the silence
///   before frame 0, which the reader supplies itself);
/// - R − 1 − (pos + a) >= M + 1 and (pos − b) − (R − N) >= M + 1: every tap of the reach one
///   frame beyond the fade's margin M at both ends of the window;
/// - and, at the run's two ends, Reader::fade(pos, R) == 1.0 exactly and the reach's window
///   bounds hold.
///
/// The phase and pos are monotone in t (as in sampleSimdRun), so they hold between two samples
/// where they hold. On [lo, hi) R and pos are linear in t, so each margin is too, up to rounding
/// of order 2^−12 frames at ring times up to 2^40 frames (over 260 days at 48 kHz): where a margin
/// is >= M + 1 at both ends of a run it is above M + 1 − 2^−11 everywhere between, so the fade's
/// two factors are both clamped to 1 there and every tap is inside the window. Hence f == 1 and the
/// read succeeds on the whole run (Linear's taps are a subset of its fade's reach, the Hermite's).
/// The rounding grows with ring time, but the one frame of slack between M + 1 and the fade's M
/// would hold to ring times of about 2^50, where it reaches a frame.
///
/// The bounds come from solving the linear conditions in n = t − onsetSample, widened by 2
/// samples; each end then steps inward while the exact test fails, which costs a few tests per
/// end (at most hi − lo). A part where the solution isn't finite is empty.
template <class Reader>
Run liveRun(const GrainRows &rows, uint32_t i, const LiveSource &live, int64_t lo, int64_t hi,
            int64_t r0, int64_t slope) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    constexpr double kM = static_cast<double>(LiveSource::kHeadMargin);
    constexpr int64_t kBefore = Reader::kFadeBefore;
    constexpr int64_t kAfter = Reader::kFadeAfter;
    constexpr auto kB = static_cast<double>(kBefore);
    constexpr auto kA = static_cast<double>(kAfter);
    const int64_t onsetSample = rows.onsetSample[i];
    const double onsetFrac = rows.onsetFrac[i];
    const double startPos = rows.startPos[i];
    const double rate = rows.rate[i];
    const double invDuration = rows.invDuration[i];
    const int64_t frames = live.frames();
    const auto window = static_cast<double>(frames); // N
    // The exact test at t, in the reference's expressions.
    const auto inside = [&](int64_t t) noexcept {
        GG_FP_CONTRACT_OFF
        const double e = static_cast<double>(t - onsetSample) - onsetFrac;
        const double phase = e * invDuration;
        const double pos = startPos + rate * e;
        if (!(phase >= 0.0 && phase <= 1.0 && pos >= kB && pos < 0x1.0p62))
        {
            return false;
        }
        const int64_t head = live.headAt(t);
        const auto r = static_cast<double>(head);
        if (!(r - 1.0 - (pos + kA) >= kM + 1.0 && (pos - kB) - (r - window) >= kM + 1.0))
        {
            return false;
        }
        const auto base = static_cast<int64_t>(std::floor(pos));
        return Reader::fade(live, pos, head) == 1.0 && base - kBefore >= head - frames &&
               base + kAfter <= head - 1;
    };

    // With R = rho0 + s × n (n = t − onsetSample, rho0 = r0 − s × (lo − onsetSample)) and pos =
    // startPos + rate × (n − onsetFrac), in exact arithmetic (a + 2 is 4 for the Hermite's reach,
    // b + 1 is 2):
    // - toward the head:  (s − rate) × n >= M + a + 2 + startPos − rate × onsetFrac − rho0 =: kHead
    // - from the tail:    (rate − s) × n >= M + b + 1 − N + rho0 − startPos + rate × onsetFrac
    //                                                                                     =: kTail
    // - pos >= b:         n >= onsetFrac + (b − startPos) / rate
    // - the phase:        onsetFrac <= n <= onsetFrac + duration
    const auto s = static_cast<double>(slope);
    const double nLo = static_cast<double>(lo - onsetSample);
    const double nHi = static_cast<double>(hi - onsetSample); // exclusive
    const double rho0 = static_cast<double>(r0) - s * nLo;
    const double c = s - rate;
    const double kHead = kM + static_cast<double>(kAfter + 2) + startPos - rate * onsetFrac - rho0;
    const double kTail =
        kM + static_cast<double>(kBefore + 1) - window + rho0 - startPos + rate * onsetFrac;
    double nFirst = std::max(onsetFrac, onsetFrac + (kB - startPos) / rate);
    double nLast = onsetFrac + rows.durationSamples[i];
    if (c > 0.0)
    {
        nFirst = std::max(nFirst, kHead / c);
        nLast = std::min(nLast, -kTail / c);
    }
    else if (c < 0.0)
    {
        nFirst = std::max(nFirst, kTail / -c);
        nLast = std::min(nLast, kHead / c);
    }
    else if (!(kHead <= 0.0 && kTail <= 0.0))
    {
        return {lo, lo}; // a margin that doesn't move and doesn't hold
    }
    if (!(std::isfinite(nFirst) && std::isfinite(nLast)))
    {
        return {lo, lo};
    }
    // Widen, then clamp into [nLo, nHi] before converting.
    double nBegin = std::ceil(nFirst) - 2.0;
    double nEnd = std::floor(nLast) + 3.0;
    nBegin = std::clamp(nBegin, nLo, nHi);
    nEnd = std::clamp(nEnd, nBegin, nHi);
    int64_t begin = onsetSample + static_cast<int64_t>(nBegin);
    int64_t end = onsetSample + static_cast<int64_t>(nEnd);
    while (begin < end && !inside(begin))
    {
        ++begin;
    }
    while (end > begin && !inside(end - 1))
    {
        --end;
    }
    return {begin, end};
}

/// The live ring's taps for the SIMD interior: frame f is in slot f mod capacity, whose two
/// channels are interleaved (LiveSource::RingView::slots, read through InterleavedTaps), and the
/// ring's 15 guard slots make each read's taps (up to the sinc's 16) contiguous from its first
/// tap's slot. locate finds lane 0's first-tap slot with one division, and each other lane's by
/// adding its frame offset from lane 0 and subtracting capacity once if that passes it; the read's
/// base is that slot + Reader::kBefore. Requires every lane's first tap to be a frame >= 0, in
/// increasing lane order.
///
/// One subtraction is enough because every offset is below the capacity C: the 4 lanes are
/// consecutive samples of one write() block, so the head R(t) moves at most 3 frames across them,
/// and each lane's taps are inside its own window [R(t) − N, R(t) − 1]. Every tap of the step is
/// therefore in one span of N + 3 frames, and a step of 4 samples needs a block of at least 4,
/// so N + 3 < N + maxBlockSize = C.
template <class Reader> struct RingTaps : InterleavedTaps
{
    int64_t capacity;

    void locate(int64_t *base) const noexcept SST_CPPUTILS_NONBLOCKING
    {
        const int64_t floor0 = base[0];
        const int64_t slot0 = (floor0 - Reader::kBefore) % capacity;
        for (int lane = 0; lane < 4; ++lane)
        {
            int64_t slot = slot0 + (base[lane] - floor0);
            slot -= slot >= capacity ? capacity : 0;
            base[lane] = slot + Reader::kBefore;
        }
    }
};
} // namespace detail

/// The live interior of grain i over [from, to), which must lie in the ring's last write()
/// block: [max(from, onset), min(to, endSample)) split at blockKink(), and on each side the
/// samples detail::liveRun<Reader> admits. On every sample there the window's phase is in [0, 1],
/// every tap is a frame >= 0 inside the window at R(t), and the reference's fade is exactly 1, so
/// renderLiveRef's amplitude is (w × 1) × gain_c = w × gain_c, the sample kernel's. Empty when
/// there is no ring, or the grain's rate isn't finite and > 0.
template <class Reader>
LiveInterior liveInterior(const GrainRows &rows, uint32_t i, const LiveSource &live, int64_t from,
                          int64_t to) noexcept SST_CPPUTILS_NONBLOCKING
{
    const int64_t first = std::max(from, rows.onsetSample[i]);
    const int64_t last = std::max(first, std::min(to, rows.endSample[i]));
    const int64_t kink = std::clamp(live.blockKink(), first, last);
    LiveInterior in{{first, kink}, {first, kink}};
    const double rate = rows.rate[i];
    const double invDuration = rows.invDuration[i];
    if (!live.enabled() || !(rate > 0.0 && rate < 0x1.0p62) ||
        !(invDuration > 0.0 && invDuration < 0x1.0p62))
    {
        return in;
    }
    if (first < kink)
    {
        // The head advances by one per sample: R(t) = R(first) + (t − first).
        const detail::Run run =
            detail::liveRun<Reader>(rows, i, live, first, kink, live.headAt(first), 1);
        in.begin[0] = run.begin;
        in.end[0] = run.end;
    }
    if (kink < last)
    {
        // The head stands still.
        const detail::Run run =
            detail::liveRun<Reader>(rows, i, live, kink, last, live.headAt(kink), 0);
        in.begin[1] = run.begin;
        in.end[1] = run.end;
    }
    return in;
}

/// The 4-wide grain-major SIMD kernel over the live ring: renderLiveRef<Reader>'s output, bit for
/// bit, with the same requirements. Each part of liveInterior, 4 samples at a time, goes through
/// the SIMD interior, reading the ring's interleaved slots from view(); every other sample (the
/// fades near either end of the window, reads outside it or before frame 0, the window's edges, and
/// each part's remainder) goes through renderLiveRef itself. Every sample depends only on t, so the
/// pieces give the reference's bits.
template <class Reader>
void renderLiveSimd(const GrainRows &rows, uint32_t i, const LiveSource &live, const Output &out,
                    int64_t from, int64_t to,
                    const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    if (std::max(from, rows.onsetSample[i]) >= std::min(to, rows.endSample[i]))
    {
        return;
    }
    const LiveInterior in = liveInterior<Reader>(rows, i, live, from, to);
    const LiveSource::RingView ring = live.view();
    const detail::RingTaps<Reader> taps{{ring.slots}, ring.capacity};
    int64_t done = from;
    for (int part = 0; part < 2; ++part)
    {
        const int64_t begin = in.begin[part];
        const int64_t simdEnd = begin + (in.end[part] - begin) / 4 * 4;
        renderLiveRef<Reader>(rows, i, live, out, done, begin, tables);
        if (out.dst[1] == nullptr)
        {
            detail::renderInterior<Reader, false, false>(rows, i, taps, out, begin, simdEnd,
                                                         tables);
        }
        else
        {
            detail::renderInterior<Reader, true, true>(rows, i, taps, out, begin, simdEnd, tables);
        }
        done = simdEnd;
    }
    renderLiveRef<Reader>(rows, i, live, out, done, to, tables);
}
} // namespace gg::kernels

#endif // GG_GRAIN_GRAINKERNELS_H
