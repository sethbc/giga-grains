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
#include "gg/grain/GrainPool.h"
#include "gg/source/LiveSource.h"

/// The grain renderers: each adds one pool grain into the output over output samples
/// [from, to). The scalar reference renderers here define the engine's per-sample arithmetic
/// (spec §3, ruling 1); any faster kernel must produce their output bit for bit.
///
/// For grain g at output sample t, with everything latched at the onset (GrainRows):
///
///     e   = double(t − onsetSample) − onsetFrac    (double)
///     w   = HannTable::at(e × invDuration)         (float)
///     pos = startPos + rate × e                     (double)
///
/// Only samples with w > 0 contribute; elsewhere nothing is read and nothing is added. With two
/// outputs, dst[c][t] += read_c(pos) × (w × gain_c), with gain_c the latched gainL or gainR. One
/// output carries source channel 0 with gain alone: dst[0][t] += read_0(pos) × (w × gain).
///
/// Every sample depends only on t and the grain, never on which call renders it, so splitting
/// [from, to) anywhere gives the same bits.
///
/// A reader policy (Hermite, Linear) gives the scalar read the references use, its reach (a read
/// at pos takes taps floor(pos) − kBefore .. floor(pos) + kAfter), and read4, the same read for 4
/// lanes whose taps are all inside the buffer, which the SIMD kernels use.
namespace gg::kernels
{
/// A sample buffer's channels. data[1] is read only when !mono and there are two outputs; a mono
/// source feeds both outputs from data[0]. frames may be 0 (with null data): every read is then
/// silence.
struct SampleSource
{
    const float *data[2];
    int64_t frames;
    bool mono;
};

/// Where a renderer adds: output sample t is dst[c][t − blockStart]. dst[0] is non-null; dst[1]
/// null means one output.
struct Output
{
    float *dst[2];
    int64_t blockStart;
};

/// The Normal tier's reader: the 4-point Hermite (Catmull-Rom) over taps floor(pos) − 1 ..
/// floor(pos) + 2, from a sample buffer (interp::hermite4) or the live ring (LiveSource::read).
struct Hermite
{
    static constexpr int64_t kBefore = 1;
    static constexpr int64_t kAfter = 2;

    static float read(const float *data, int64_t frames,
                      double pos) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return interp::hermite4(data, frames, pos);
    }

    /// interp::hermite4 for 4 lanes whose taps are all inside data: lane k reads at
    /// base[k] + frac[k], with base[k] = floor(pos) and frac[k] = float(pos − floor(pos)), through
    /// interp::catmullRom's expressions in its order.
    static SIMD_M128 read4(const float *data, const int64_t *base,
                           SIMD_M128 frac) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        alignas(16) float q0[4];
        alignas(16) float q1[4];
        alignas(16) float q2[4];
        alignas(16) float q3[4];
        for (int lane = 0; lane < 4; ++lane)
        {
            const float *p = data + base[lane];
            q0[lane] = p[-1];
            q1[lane] = p[0];
            q2[lane] = p[1];
            q3[lane] = p[2];
        }
        const SIMD_M128 p0 = SIMD_MM(load_ps)(q0);
        const SIMD_M128 p1 = SIMD_MM(load_ps)(q1);
        const SIMD_M128 p2 = SIMD_MM(load_ps)(q2);
        const SIMD_M128 p3 = SIMD_MM(load_ps)(q3);
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
                         LiveSource::Cursor &cursor, float &l,
                         float &r) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return live.read(pos, head, cursor, l, r);
    }
};

/// The Eco tier's reader: linear interpolation over taps floor(pos) and floor(pos) + 1, from a
/// sample buffer (interp::linear) or the live ring (LiveSource::readLinear).
struct Linear
{
    static constexpr int64_t kBefore = 0;
    static constexpr int64_t kAfter = 1;

    static float read(const float *data, int64_t frames,
                      double pos) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return interp::linear(data, frames, pos);
    }

    /// interp::linear for 4 lanes whose taps are all inside data: lane k reads at
    /// base[k] + frac[k], as Hermite::read4, through interp::lerp's a + (b − a) × t.
    static SIMD_M128 read4(const float *data, const int64_t *base,
                           SIMD_M128 frac) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        alignas(16) float q0[4];
        alignas(16) float q1[4];
        for (int lane = 0; lane < 4; ++lane)
        {
            const float *p = data + base[lane];
            q0[lane] = p[0];
            q1[lane] = p[1];
        }
        const SIMD_M128 a = SIMD_MM(load_ps)(q0);
        return SIMD_MM(add_ps)(a, SIMD_MM(mul_ps)(SIMD_MM(sub_ps)(SIMD_MM(load_ps)(q1), a), frac));
    }

    /// False, reading nothing, unless both taps are inside the ring's window at head.
    static bool readLive(const LiveSource &live, double pos, int64_t head,
                         LiveSource::Cursor &cursor, float &l,
                         float &r) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return live.readLinear(pos, head, cursor, l, r);
    }
};

/// The scalar reference over a sample source: grain i of rows, over [from, to), read with
/// Reader::read.
template <class Reader>
void renderSampleRef(const GrainRows &rows, uint32_t i, const SampleSource &src, const Output &out,
                     int64_t from, int64_t to,
                     const HannTable &hann) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    const int64_t onsetSample = rows.onsetSample[i];
    const double onsetFrac = rows.onsetFrac[i];
    const double startPos = rows.startPos[i];
    const double rate = rows.rate[i];
    const double invDuration = rows.invDuration[i];
    const int64_t first = std::max(from, onsetSample);
    const int64_t last = std::min(to, rows.endSample[i]); // exclusive
    float *const dst0 = out.dst[0];
    float *const dst1 = out.dst[1];
    if (dst1 == nullptr)
    {
        const float gain = rows.gain[i];
        for (int64_t t = first; t < last; ++t)
        {
            const double e = static_cast<double>(t - onsetSample) - onsetFrac;
            const float w = hann.at(e * invDuration);
            if (!(w > 0.0f))
            {
                continue;
            }
            const double pos = startPos + rate * e;
            const auto j = static_cast<std::size_t>(t - out.blockStart);
            dst0[j] += Reader::read(src.data[0], src.frames, pos) * (w * gain);
        }
        return;
    }
    const float gainL = rows.gainL[i];
    const float gainR = rows.gainR[i];
    for (int64_t t = first; t < last; ++t)
    {
        const double e = static_cast<double>(t - onsetSample) - onsetFrac;
        const float w = hann.at(e * invDuration);
        if (!(w > 0.0f))
        {
            continue;
        }
        const double pos = startPos + rate * e;
        const auto j = static_cast<std::size_t>(t - out.blockStart);
        const float s0 = Reader::read(src.data[0], src.frames, pos);
        const float s1 = src.mono ? s0 : Reader::read(src.data[1], src.frames, pos);
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
/// inversion, leaves the bounds at [first, last) and the stepping finds the run (or none); it is
/// bounded by last − first.
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

/// The SIMD sample kernel's interior: grain i over [begin, end), a multiple of 4 samples inside
/// sampleSimdRun, 4 consecutive samples per step. Per lane, in double as the reference: e, the
/// window's x = (e × invDuration) × kSize with HannTable::at's index and fraction, and pos with
/// its floor and fraction. Then 4-wide float, in the reference's order: the window lerp, the
/// w > 0 mask, Reader::read4, read × (w × gain_c), and the masked add.
///
/// kTwoOutputs: dst[1] is written, with gainL and gainR; otherwise dst[0] alone, with gain.
/// kStereo: with two outputs, data[1] feeds dst[1]; otherwise dst[1] reuses data[0]'s read.
template <class Reader, bool kTwoOutputs, bool kStereo>
void renderSampleInterior(const GrainRows &rows, uint32_t i, const SampleSource &src,
                          const Output &out, int64_t begin, int64_t end,
                          const HannTable &hann) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    const int64_t onsetSample = rows.onsetSample[i];
    const double onsetFrac = rows.onsetFrac[i];
    const double startPos = rows.startPos[i];
    const double rate = rows.rate[i];
    const double invDuration = rows.invDuration[i];
    const SIMD_M128 gain0 = SIMD_MM(set1_ps)(kTwoOutputs ? rows.gainL[i] : rows.gain[i]);
    const SIMD_M128 gain1 = SIMD_MM(set1_ps)(rows.gainR[i]);
    const SIMD_M128 zero = SIMD_MM(setzero_ps)();
    const float *const points = hann.points();
    const float *const data0 = src.data[0];
    const float *const data1 = src.data[1];
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
            // HannTable::at, with the phase known to be in [0, 1]: phase 1 is the end of the
            // last segment.
            const double x = (e * invDuration) * HannTable::kSize;
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
        const SIMD_M128 lo = SIMD_MM(load_ps)(wLo);
        const SIMD_M128 w =
            SIMD_MM(add_ps)(lo, SIMD_MM(mul_ps)(SIMD_MM(sub_ps)(SIMD_MM(load_ps)(wHi), lo),
                                                SIMD_MM(load_ps)(wFrac)));
        // The reference skips a sample unless w > 0: nothing is added there.
        const SIMD_M128 sounds = SIMD_MM(cmpgt_ps)(w, zero);
        const SIMD_M128 frac = SIMD_MM(load_ps)(posFrac);
        const SIMD_M128 s0 = Reader::read4(data0, base, frac);
        addMasked(dst0 + j, SIMD_MM(mul_ps)(s0, SIMD_MM(mul_ps)(w, gain0)), sounds);
        if constexpr (kTwoOutputs)
        {
            SIMD_M128 s1 = s0;
            if constexpr (kStereo)
            {
                s1 = Reader::read4(data1, base, frac);
            }
            addMasked(dst1 + j, SIMD_MM(mul_ps)(s1, SIMD_MM(mul_ps)(w, gain1)), sounds);
        }
    }
}
} // namespace detail

/// The 4-wide grain-major SIMD kernel over a sample source: renderSampleRef<Reader>'s output, bit
/// for bit. The samples in detail::sampleSimdRun, 4 at a time, go through the SIMD interior;
/// those before the run, and those after its last whole step of 4 (reads near or past the
/// source's ends, the window's edges, and the remainder), go through renderSampleRef itself.
/// Every sample depends only on t, so the pieces give the reference's bits.
template <class Reader>
void renderSampleSimd(const GrainRows &rows, uint32_t i, const SampleSource &src, const Output &out,
                      int64_t from, int64_t to,
                      const HannTable &hann) noexcept SST_CPPUTILS_NONBLOCKING
{
    const int64_t first = std::max(from, rows.onsetSample[i]);
    const int64_t last = std::min(to, rows.endSample[i]); // exclusive
    if (first >= last)
    {
        return;
    }
    const detail::Run run = detail::sampleSimdRun<Reader>(rows, i, src.frames, first, last);
    const int64_t simdEnd = run.begin + (run.end - run.begin) / 4 * 4;
    renderSampleRef<Reader>(rows, i, src, out, first, run.begin, hann);
    if (out.dst[1] == nullptr)
    {
        detail::renderSampleInterior<Reader, false, false>(rows, i, src, out, run.begin, simdEnd,
                                                           hann);
    }
    else if (src.mono)
    {
        detail::renderSampleInterior<Reader, true, false>(rows, i, src, out, run.begin, simdEnd,
                                                          hann);
    }
    else
    {
        detail::renderSampleInterior<Reader, true, true>(rows, i, src, out, run.begin, simdEnd,
                                                         hann);
    }
    renderSampleRef<Reader>(rows, i, src, out, simdEnd, last, hann);
}

/// The scalar reference over the live ring: grain i of rows, over [from, to), read with
/// Reader::readLive. [from, to) must lie in the ring's last write() block, whose R(t) the
/// head-proximity fade and the read use. Per sample, after w > 0:
///
///     f = float(LiveSource::fade(pos, R(t)))
///
/// and the sample contributes only when f > 0 and the read succeeds:
/// dst[c][t] += read_c(pos) × ((w × f) × gain_c), or, with one output, ring channel 0 ×
/// ((w × f) × gain).
template <class Reader>
void renderLiveRef(const GrainRows &rows, uint32_t i, const LiveSource &live, const Output &out,
                   int64_t from, int64_t to,
                   const HannTable &hann) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    const int64_t onsetSample = rows.onsetSample[i];
    const double onsetFrac = rows.onsetFrac[i];
    const double startPos = rows.startPos[i];
    const double rate = rows.rate[i];
    const double invDuration = rows.invDuration[i];
    const int64_t first = std::max(from, onsetSample);
    const int64_t last = std::min(to, rows.endSample[i]); // exclusive
    float *const dst0 = out.dst[0];
    float *const dst1 = out.dst[1];
    const bool twoOutputs = dst1 != nullptr;
    // gain_c: the latched channel gains, or with one output the grain's gain alone.
    const float gain0 = twoOutputs ? rows.gainL[i] : rows.gain[i];
    const float gain1 = rows.gainR[i];
    LiveSource::Cursor cursor;
    for (int64_t t = first; t < last; ++t)
    {
        const double e = static_cast<double>(t - onsetSample) - onsetFrac;
        const float w = hann.at(e * invDuration);
        if (!(w > 0.0f))
        {
            continue;
        }
        // The read position is in ring time; the head is R(t), from this block's write.
        const double pos = startPos + rate * e;
        const int64_t head = live.headAt(t);
        const auto f = static_cast<float>(live.fade(pos, head));
        float l = 0.0f;
        float r = 0.0f;
        // Where the fade is 0 the taps aren't read at all, so stale data can never contribute.
        if (!(f > 0.0f) || !Reader::readLive(live, pos, head, cursor, l, r))
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
} // namespace gg::kernels

#endif // GG_GRAIN_GRAINKERNELS_H
