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
} // namespace detail

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
    /// interp::catmullRom's expressions in its order. Frame f is data[kStride × f]: 1 for a planar
    /// channel, 2 for one channel of interleaved pairs.
    template <int64_t kStride = 1>
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
    static void read4Pair(const float *pairs, const int64_t *base, SIMD_M128 frac, SIMD_M128 &left,
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
    /// base[k] + frac[k], as Hermite::read4 (kStride too), through interp::lerp's a + (b − a) × t.
    template <int64_t kStride = 1>
    static SIMD_M128 read4(const float *data, const int64_t *base,
                           SIMD_M128 frac) noexcept SST_CPPUTILS_NONBLOCKING
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
    static void read4Pair(const float *pairs, const int64_t *base, SIMD_M128 frac, SIMD_M128 &left,
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
    void read(const int64_t *base, SIMD_M128 frac, SIMD_M128 &s0,
              SIMD_M128 &s1) const noexcept SST_CPPUTILS_NONBLOCKING
    {
        s0 = Reader::read4(data[0], base, frac);
        s1 = s0;
        if constexpr (kBoth)
        {
            s1 = Reader::read4(data[1], base, frac);
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
    void read(const int64_t *base, SIMD_M128 frac, SIMD_M128 &s0,
              SIMD_M128 &s1) const noexcept SST_CPPUTILS_NONBLOCKING
    {
        if constexpr (kBoth)
        {
            Reader::read4Pair(pairs, base, frac, s0, s1);
        }
        else
        {
            s0 = Reader::template read4<2>(pairs, base, frac);
            s1 = s0;
        }
    }
};

/// The SIMD interior: grain i over [begin, end), a multiple of 4 samples, all with the window's
/// phase in [0, 1] and every tap readable through Taps, 4 consecutive samples per step. Per
/// lane, in double as the reference: e, the window's x = (e × invDuration) × kSize with
/// HannTable::at's index and fraction, and pos with its floor and fraction. taps.locate turns the
/// 4 floors into the buffer indices the reads take taps around (BufferTaps, InterleavedTaps: the
/// floors; the live ring: their slots). Then 4-wide float, in the reference's order: the window
/// lerp, the w > 0 mask, the read (taps.read, through Reader's 4-lane read), read × (w × gain_c),
/// and the masked add. How the taps are laid out changes only where the same floats are loaded
/// from.
///
/// kTwoOutputs: dst[1] is written, with gainL and gainR; otherwise dst[0] alone, with gain.
/// kStereo: with two outputs, channel 1's read feeds dst[1]; otherwise dst[1] reuses channel 0's.
template <class Reader, bool kTwoOutputs, bool kStereo, class Taps>
void renderInterior(const GrainRows &rows, uint32_t i, const Taps &taps, const Output &out,
                    int64_t begin, int64_t end,
                    const HannTable &hann) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    const int64_t onsetSample = rows.onsetSample[i];
    const double onsetFrac = rows.onsetFrac[i];
    const double startPos = rows.startPos[i];
    const double rate = rows.rate[i];
    const double invDuration = rows.invDuration[i];
    // gain_c: the latched gainL and gainR, or with one output the grain's gain alone in gain0
    // (gainR is read only with two outputs).
    const SIMD_M128 gain0 = SIMD_MM(set1_ps)(kTwoOutputs ? rows.gainL[i] : rows.gain[i]);
    const SIMD_M128 gain1 = SIMD_MM(set1_ps)(kTwoOutputs ? rows.gainR[i] : 0.0f);
    const SIMD_M128 zero = SIMD_MM(setzero_ps)();
    const float *const points = hann.points();
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
        taps.template read<Reader, kTwoOutputs && kStereo>(base, frac, s0, s1);
        addMasked(dst0 + j, SIMD_MM(mul_ps)(s0, SIMD_MM(mul_ps)(w, gain0)), sounds);
        if constexpr (kTwoOutputs)
        {
            addMasked(dst1 + j, SIMD_MM(mul_ps)(s1, SIMD_MM(mul_ps)(w, gain1)), sounds);
        }
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
    const detail::BufferTaps taps{{src.data[0], src.data[1]}};
    renderSampleRef<Reader>(rows, i, src, out, first, run.begin, hann);
    if (out.dst[1] == nullptr)
    {
        detail::renderInterior<Reader, false, false>(rows, i, taps, out, run.begin, simdEnd, hann);
    }
    else if (src.mono)
    {
        detail::renderInterior<Reader, true, false>(rows, i, taps, out, run.begin, simdEnd, hann);
    }
    else if (src.interleaved != nullptr)
    {
        const detail::InterleavedTaps pairs{src.interleaved};
        detail::renderInterior<Reader, true, true>(rows, i, pairs, out, run.begin, simdEnd, hann);
    }
    else
    {
        detail::renderInterior<Reader, true, true>(rows, i, taps, out, run.begin, simdEnd, hann);
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
    // gain_c: the latched channel gains, or with one output the grain's gain alone; gainR is
    // read only with two outputs.
    const float gain0 = twoOutputs ? rows.gainL[i] : rows.gain[i];
    const float gain1 = twoOutputs ? rows.gainR[i] : 0.0f;
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
/// The conditions, with e, pos and the phase as the reference computes them:
/// - the window's phase e × invDuration is in [0, 1];
/// - pos >= 1, so the Hermite's first tap, floor(pos) − 1, is a frame >= 0 (not the silence
///   before frame 0, which read() supplies itself);
/// - R − 1 − (pos + 2) >= M + 1 and (pos − 1) − (R − N) >= M + 1: every Hermite tap one frame
///   beyond the fade's margin M at both ends of the window;
/// - and, at the run's two ends, fade(pos, R) == 1.0 exactly and read()'s window bounds hold.
///
/// The phase and pos are monotone in t (as in sampleSimdRun), so they hold between two samples
/// where they hold. On [lo, hi) R and pos are linear in t, so each margin is too, up to rounding
/// of order 2^−12 frames at ring times up to 2^40 (the ring's limit): where a margin is >= M + 1
/// at both ends of a run it is above M + 1 − 2^−11 everywhere between, so the fade's two factors
/// are both clamped to 1 there and every tap is inside the window. Hence f == 1 and the read
/// succeeds on the whole run, for either reader (Linear's taps are a subset of the Hermite's).
/// The rounding grows with ring time, but the one frame of slack between M + 1 and the fade's M
/// would hold to ring times of about 2^50, where it reaches a frame.
///
/// The bounds come from solving the linear conditions in n = t − onsetSample, widened by 2
/// samples; each end then steps inward while the exact test fails, which costs a few tests per
/// end (at most hi − lo). A part where the solution isn't finite is empty.
inline Run liveRun(const GrainRows &rows, uint32_t i, const LiveSource &live, int64_t lo,
                   int64_t hi, int64_t r0, int64_t slope) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    constexpr double kM = static_cast<double>(LiveSource::kHeadMargin);
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
        if (!(phase >= 0.0 && phase <= 1.0 && pos >= 1.0 && pos < 0x1.0p62))
        {
            return false;
        }
        const int64_t head = live.headAt(t);
        const auto r = static_cast<double>(head);
        if (!(r - 1.0 - (pos + 2.0) >= kM + 1.0 && (pos - 1.0) - (r - window) >= kM + 1.0))
        {
            return false;
        }
        const auto base = static_cast<int64_t>(std::floor(pos));
        return live.fade(pos, head) == 1.0 && base - 1 >= head - frames && base + 2 <= head - 1;
    };

    // With R = rho0 + s × n (n = t − onsetSample, rho0 = r0 − s × (lo − onsetSample)) and pos =
    // startPos + rate × (n − onsetFrac), in exact arithmetic:
    // - toward the head:  (s − rate) × n >= M + 4 + startPos − rate × onsetFrac − rho0 =: kHead
    // - from the tail:    (rate − s) × n >= M + 2 − N + rho0 − startPos + rate × onsetFrac =: kTail
    // - pos >= 1:         n >= onsetFrac + (1 − startPos) / rate
    // - the phase:        onsetFrac <= n <= onsetFrac + duration
    const auto s = static_cast<double>(slope);
    const double nLo = static_cast<double>(lo - onsetSample);
    const double nHi = static_cast<double>(hi - onsetSample); // exclusive
    const double rho0 = static_cast<double>(r0) - s * nLo;
    const double c = s - rate;
    const double kHead = kM + 4.0 + startPos - rate * onsetFrac - rho0;
    const double kTail = kM + 2.0 - window + rho0 - startPos + rate * onsetFrac;
    double nFirst = std::max(onsetFrac, onsetFrac + (1.0 - startPos) / rate);
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
/// ring's guard slots make each read's taps contiguous from its first tap's slot. locate finds
/// lane 0's first-tap slot with one division, and each other lane's by adding its frame offset
/// from lane 0 and subtracting capacity once if that passes it; the read's base is that slot +
/// Reader::kBefore. Requires every lane's first tap to be a frame >= 0, in increasing lane order.
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
/// samples detail::liveRun admits. On every sample there the window's phase is in [0, 1], every
/// tap is a frame >= 0 inside the window at R(t), and the reference's fade is exactly 1, so
/// renderLiveRef's amplitude is (w × 1) × gain_c = w × gain_c, the sample kernel's. Empty when
/// there is no ring, or the grain's rate isn't finite and > 0.
inline LiveInterior liveInterior(const GrainRows &rows, uint32_t i, const LiveSource &live,
                                 int64_t from, int64_t to) noexcept SST_CPPUTILS_NONBLOCKING
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
        const detail::Run run = detail::liveRun(rows, i, live, first, kink, live.headAt(first), 1);
        in.begin[0] = run.begin;
        in.end[0] = run.end;
    }
    if (kink < last)
    {
        // The head stands still.
        const detail::Run run = detail::liveRun(rows, i, live, kink, last, live.headAt(kink), 0);
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
                    const HannTable &hann) noexcept SST_CPPUTILS_NONBLOCKING
{
    if (std::max(from, rows.onsetSample[i]) >= std::min(to, rows.endSample[i]))
    {
        return;
    }
    const LiveInterior in = liveInterior(rows, i, live, from, to);
    const LiveSource::RingView ring = live.view();
    const detail::RingTaps<Reader> taps{{ring.slots}, ring.capacity};
    int64_t done = from;
    for (int part = 0; part < 2; ++part)
    {
        const int64_t begin = in.begin[part];
        const int64_t simdEnd = begin + (in.end[part] - begin) / 4 * 4;
        renderLiveRef<Reader>(rows, i, live, out, done, begin, hann);
        if (out.dst[1] == nullptr)
        {
            detail::renderInterior<Reader, false, false>(rows, i, taps, out, begin, simdEnd, hann);
        }
        else
        {
            detail::renderInterior<Reader, true, true>(rows, i, taps, out, begin, simdEnd, hann);
        }
        done = simdEnd;
    }
    renderLiveRef<Reader>(rows, i, live, out, done, to, hann);
}
} // namespace gg::kernels

#endif // GG_GRAIN_GRAINKERNELS_H
