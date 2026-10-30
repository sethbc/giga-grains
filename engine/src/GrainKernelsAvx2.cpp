// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The AVX2 spike's 8-wide kernels (M2 engine spec §6.12): renderSampleSimd and renderLiveSimd for
// each reader with an interior 8 samples wide. Everything else is the shared code of
// GrainKernels.h, called, not copied: the scalar references, the run finders (sampleSimdRun,
// liveInterior) and the 4-wide interior, which renders a run's last whole step of 4 when an odd
// one is left after the steps of 8.
//
// The 8-wide interior is renderInteriorSteps over __m256: the same per-lane scalar arithmetic for
// 8 lanes, then each 4-wide operation as one 8-wide operation, which is the 4-wide one on the two
// halves, lane by lane: add, sub, mul, div, and, or, andnot, the two compares (cmpgt_ps is GT_OQ,
// cmpneq_ps NEQ_UQ: the same truth tables), and moves (loads, stores, the in-half transposes). No
// horizontal operation and no FMA, so every lane's bits are the 4-wide kernel's, which are the
// reference's. The tests hold the two tables to each other and to the reference with memcmp
// ("the avx2 kernels match the sse kernels bit for bit").
//
// Each function here carries GG_AVX2, __attribute__((target("avx2"))): AVX2 without FMA (neither
// GCC nor Clang has AVX2 imply FMA), as -mavx2 -mno-fma would give, and -ffp-contract=off holds as
// everywhere. The file is not compiled with -mavx2: the shared inline functions and templates it
// calls are then compiled for the project's target here as in every other file, so whichever copy
// the linker keeps runs on any x86-64 CPU. Inlined into a GG_AVX2 function they take its target.
// Everything here has internal linkage but the table's accessor.

#include "gg/grain/KernelDispatch.h"

#if GG_KERNELS_AVX2

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <immintrin.h>

#define GG_AVX2 __attribute__((target("avx2")))

namespace gg::kernels
{
namespace
{
using V8 = __m256;

/// The 8 lanes from two 4-lane halves: lanes 0–3 from lo, 4–7 from hi. Moves only.
GG_AVX2 inline V8 join(__m128 lo, __m128 hi) noexcept SST_CPPUTILS_NONBLOCKING
{
    return _mm256_insertf128_ps(_mm256_castps128_ps256(lo), hi, 1);
}

/// detail::transpose4 in each 128-bit half: afterwards r[k]'s low half holds lane k of each old
/// row's low half, and its high half lane k of each old row's high half. It only moves floats.
GG_AVX2 inline void transpose4x2(V8 *r) noexcept SST_CPPUTILS_NONBLOCKING
{
    const V8 t0 = _mm256_unpacklo_ps(r[0], r[1]);
    const V8 t1 = _mm256_unpacklo_ps(r[2], r[3]);
    const V8 t2 = _mm256_unpackhi_ps(r[0], r[1]);
    const V8 t3 = _mm256_unpackhi_ps(r[2], r[3]);
    r[0] = _mm256_shuffle_ps(t0, t1, _MM_SHUFFLE(1, 0, 1, 0)); // movelh_ps(t0, t1) per half
    r[1] = _mm256_shuffle_ps(t0, t1, _MM_SHUFFLE(3, 2, 3, 2)); // movehl_ps(t1, t0) per half
    r[2] = _mm256_shuffle_ps(t2, t3, _MM_SHUFFLE(1, 0, 1, 0));
    r[3] = _mm256_shuffle_ps(t2, t3, _MM_SHUFFLE(3, 2, 3, 2));
}

/// The 4 floats at lo (lanes 0–3's half) and at hi (lanes 4–7's), unaligned.
GG_AVX2 inline V8 loadPair(const float *lo, const float *hi) noexcept SST_CPPUTILS_NONBLOCKING
{
    return join(_mm_loadu_ps(lo), _mm_loadu_ps(hi));
}

/// Each reader's 4-lane reads (read4, read4Pair) for 8 lanes, with the same taps and the same
/// arithmetic per lane.
template <class Reader> struct Wide;

template <> struct Wide<Hermite>
{
    /// Hermite::curve4 in 8 lanes.
    GG_AVX2 static V8 curve(V8 p0, V8 p1, V8 p2, V8 p3, V8 frac) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        const V8 half = _mm256_set1_ps(0.5f);
        const V8 c1 = _mm256_mul_ps(half, _mm256_sub_ps(p2, p0));
        const V8 c2 =
            _mm256_sub_ps(_mm256_add_ps(_mm256_sub_ps(p0, _mm256_mul_ps(_mm256_set1_ps(2.5f), p1)),
                                        _mm256_mul_ps(_mm256_set1_ps(2.0f), p2)),
                          _mm256_mul_ps(half, p3));
        const V8 c3 = _mm256_add_ps(_mm256_mul_ps(half, _mm256_sub_ps(p3, p0)),
                                    _mm256_mul_ps(_mm256_set1_ps(1.5f), _mm256_sub_ps(p1, p2)));
        V8 y = _mm256_add_ps(_mm256_mul_ps(c3, frac), c2);
        y = _mm256_add_ps(_mm256_mul_ps(y, frac), c1);
        return _mm256_add_ps(_mm256_mul_ps(y, frac), p1);
    }

    /// Hermite::read4 in 8 lanes.
    template <int64_t kStride>
    GG_AVX2 static V8 read(const float *data, const int64_t *base, V8 frac,
                           const ReadParams & /*params*/) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        alignas(32) float q0[8];
        alignas(32) float q1[8];
        alignas(32) float q2[8];
        alignas(32) float q3[8];
        for (int lane = 0; lane < 8; ++lane)
        {
            const float *p = data + kStride * base[lane];
            q0[lane] = p[-kStride];
            q1[lane] = p[0];
            q2[lane] = p[kStride];
            q3[lane] = p[2 * kStride];
        }
        return curve(_mm256_load_ps(q0), _mm256_load_ps(q1), _mm256_load_ps(q2), _mm256_load_ps(q3),
                     frac);
    }

    /// Hermite::read4Pair in 8 lanes: lanes k and k + 4 share a row, one in each half.
    GG_AVX2 static void readPair(const float *pairs, const int64_t *base, V8 frac,
                                 const ReadParams & /*params*/, V8 &left,
                                 V8 &right) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        V8 a[4]; // lanes k, k + 4: taps −1 and 0, L R L R
        V8 b[4]; // lanes k, k + 4: taps 1 and 2, L R L R
        for (int lane = 0; lane < 4; ++lane)
        {
            const float *lo = pairs + 2 * (base[lane] - 1);
            const float *hi = pairs + 2 * (base[lane + 4] - 1);
            a[lane] = loadPair(lo, hi);
            b[lane] = loadPair(lo + 4, hi + 4);
        }
        transpose4x2(a); // tap −1 left, tap −1 right, tap 0 left, tap 0 right
        transpose4x2(b); // tap 1 left, tap 1 right, tap 2 left, tap 2 right
        left = curve(a[0], a[2], b[0], b[2], frac);
        right = curve(a[1], a[3], b[1], b[3], frac);
    }
};

template <> struct Wide<Linear>
{
    /// Linear::line4 in 8 lanes: a + (b − a) × t.
    GG_AVX2 static V8 line(V8 a, V8 b, V8 frac) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        return _mm256_add_ps(a, _mm256_mul_ps(_mm256_sub_ps(b, a), frac));
    }

    /// Linear::read4 in 8 lanes.
    template <int64_t kStride>
    GG_AVX2 static V8 read(const float *data, const int64_t *base, V8 frac,
                           const ReadParams & /*params*/) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        alignas(32) float q0[8];
        alignas(32) float q1[8];
        for (int lane = 0; lane < 8; ++lane)
        {
            const float *p = data + kStride * base[lane];
            q0[lane] = p[0];
            q1[lane] = p[kStride];
        }
        return line(_mm256_load_ps(q0), _mm256_load_ps(q1), frac);
    }

    /// Linear::read4Pair in 8 lanes.
    GG_AVX2 static void readPair(const float *pairs, const int64_t *base, V8 frac,
                                 const ReadParams & /*params*/, V8 &left,
                                 V8 &right) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        V8 a[4]; // lanes k, k + 4: taps 0 and 1, L R L R
        for (int lane = 0; lane < 4; ++lane)
        {
            a[lane] = loadPair(pairs + 2 * base[lane], pairs + 2 * base[lane + 4]);
        }
        transpose4x2(a); // tap 0 left, tap 0 right, tap 1 left, tap 1 right
        left = line(a[0], a[2], frac);
        right = line(a[1], a[3], frac);
    }
};

template <> struct Wide<Sinc>
{
    static constexpr int64_t kTaps = Sinc::kTaps;
    static constexpr int64_t kBefore = Sinc::kBefore;

    /// Sinc::coefficients4 in 8 lanes: each lane's c_k from SincTable::at, scalar, and den = Σ c_k
    /// from 0 in k order.
    GG_AVX2 static V8 coefficients(V8 frac, const ReadParams &params,
                                   V8 *c) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        alignas(32) float f[8];
        _mm256_store_ps(f, frac);
        V8 den = _mm256_setzero_ps();
        for (int64_t k = 0; k < kTaps; ++k)
        {
            const auto offset = static_cast<float>(k - kBefore);
            alignas(32) float q[8];
            for (int lane = 0; lane < 8; ++lane)
            {
                q[lane] = params.sinc->at((offset - f[lane]) * params.stretch);
            }
            c[k] = _mm256_load_ps(q);
            den = _mm256_add_ps(den, c[k]);
        }
        return den;
    }

    /// Sinc::sum4 in 8 lanes: (Σ c_k × tap_k) / den, 0 where den is 0 (NEQ_UQ, cmpneq_ps's
    /// predicate, so a NaN den divides as there).
    GG_AVX2 static V8 sum(const V8 *c, V8 den, const V8 *taps) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        const V8 zero = _mm256_setzero_ps();
        V8 num = zero;
        for (int64_t k = 0; k < kTaps; ++k)
        {
            num = _mm256_add_ps(num, _mm256_mul_ps(c[k], taps[k]));
        }
        return _mm256_and_ps(_mm256_cmp_ps(den, zero, _CMP_NEQ_UQ), _mm256_div_ps(num, den));
    }

    /// Sinc::read4 in 8 lanes.
    template <int64_t kStride>
    GG_AVX2 static V8 read(const float *data, const int64_t *base, V8 frac,
                           const ReadParams &params) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        V8 taps[kTaps];
        for (int64_t k = 0; k < kTaps; ++k)
        {
            alignas(32) float q[8];
            for (int lane = 0; lane < 8; ++lane)
            {
                q[lane] = data[kStride * (base[lane] - kBefore + k)];
            }
            taps[k] = _mm256_load_ps(q);
        }
        V8 c[kTaps];
        const V8 den = coefficients(frac, params, c);
        return sum(c, den, taps);
    }

    /// Sinc::read4Pair in 8 lanes.
    GG_AVX2 static void readPair(const float *pairs, const int64_t *base, V8 frac,
                                 const ReadParams &params, V8 &left,
                                 V8 &right) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        V8 tapsL[kTaps];
        V8 tapsR[kTaps];
        for (int64_t m = 0; m < kTaps / 2; ++m)
        {
            V8 a[4]; // lanes k, k + 4: taps 2m and 2m + 1 (from the first), L R L R
            for (int lane = 0; lane < 4; ++lane)
            {
                a[lane] = loadPair(pairs + 2 * (base[lane] - kBefore) + 4 * m,
                                   pairs + 2 * (base[lane + 4] - kBefore) + 4 * m);
            }
            transpose4x2(a); // tap 2m left, tap 2m right, tap 2m + 1 left, tap 2m + 1 right
            tapsL[2 * m] = a[0];
            tapsR[2 * m] = a[1];
            tapsL[2 * m + 1] = a[2];
            tapsR[2 * m + 1] = a[3];
        }
        V8 c[kTaps];
        const V8 den = coefficients(frac, params, c);
        left = sum(c, den, tapsL);
        right = sum(c, den, tapsR);
    }
};

/// detail::addMasked in 8 lanes: dst[0..7] += y where mask is set, the other lanes' bits kept.
GG_AVX2 inline void addMasked(float *dst, V8 y, V8 mask) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    const V8 d = _mm256_loadu_ps(dst);
    const V8 sum = _mm256_add_ps(d, y);
    _mm256_storeu_ps(dst, _mm256_or_ps(_mm256_and_ps(mask, sum), _mm256_andnot_ps(mask, d)));
}

/// detail::BufferTaps for 8 lanes.
template <int64_t kStride> struct BufferTaps8
{
    const float *data[2];

    GG_AVX2 void locate(int64_t *) const noexcept SST_CPPUTILS_NONBLOCKING {}

    template <class Reader, bool kBoth>
    GG_AVX2 void read(const int64_t *base, V8 frac, const ReadParams &params, V8 &s0,
                      V8 &s1) const noexcept SST_CPPUTILS_NONBLOCKING
    {
        s0 = Wide<Reader>::template read<kStride>(data[0], base, frac, params);
        s1 = s0;
        if constexpr (kBoth)
        {
            s1 = Wide<Reader>::template read<kStride>(data[1], base, frac, params);
        }
    }
};

/// detail::InterleavedTaps for 8 lanes.
struct InterleavedTaps8
{
    const float *pairs;

    GG_AVX2 void locate(int64_t *) const noexcept SST_CPPUTILS_NONBLOCKING {}

    template <class Reader, bool kBoth>
    GG_AVX2 void read(const int64_t *base, V8 frac, const ReadParams &params, V8 &s0,
                      V8 &s1) const noexcept SST_CPPUTILS_NONBLOCKING
    {
        if constexpr (kBoth)
        {
            Wide<Reader>::readPair(pairs, base, frac, params, s0, s1);
        }
        else
        {
            s0 = Wide<Reader>::template read<2>(pairs, base, frac, params);
            s1 = s0;
        }
    }
};

/// detail::RingTaps for 8 lanes, by the same argument with 8 consecutive samples of one write()
/// block: the head moves at most 7 frames across them, so every tap of the step is in one span
/// of N + 7 frames, and a step of 8 samples needs a block of at least 8, so N + 7 < N +
/// maxBlockSize = C, and one subtraction of the capacity is enough.
template <class Reader> struct RingTaps8 : InterleavedTaps8
{
    int64_t capacity;

    GG_AVX2 void locate(int64_t *base) const noexcept SST_CPPUTILS_NONBLOCKING
    {
        const int64_t floor0 = base[0];
        const int64_t slot0 = (floor0 - Reader::kBefore) % capacity;
        for (int lane = 0; lane < 8; ++lane)
        {
            int64_t slot = slot0 + (base[lane] - floor0);
            slot -= slot >= capacity ? capacity : 0;
            base[lane] = slot + Reader::kBefore;
        }
    }
};

/// detail::renderInteriorSteps, 8 samples per step: [begin, end) a multiple of 8 samples, each
/// as that function requires.
template <class Reader, bool kTwoOutputs, bool kStereo, bool kWarp, class Taps, class Dec>
GG_AVX2 void renderInteriorSteps8(const GrainRows &rows, uint32_t i, const Taps &taps,
                                  Dec decimation, const Output &out, int64_t begin, int64_t end,
                                  const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    const int64_t onsetSample = rows.onsetSample[i];
    const double onsetFrac = rows.onsetFrac[i];
    const double startPos = rows.startPos[i];
    const double rate = rows.rate[i];
    const double invDuration = rows.invDuration[i];
    const auto invDec = detail::invDecimation(decimation);
    [[maybe_unused]] const EnvelopeShape envelope = detail::envelopeOf(rows, i);
    const V8 gain0 = _mm256_set1_ps(kTwoOutputs ? rows.gainL[i] : rows.gain[i]);
    const V8 gain1 = _mm256_set1_ps(kTwoOutputs ? rows.gainR[i] : 0.0f);
    const V8 zero = _mm256_setzero_ps();
    const ReadParams params = detail::paramsOf(rows, i, tables, decimation);
    const float *const points = tables.hann.points();
    float *const dst0 = out.dst[0];
    float *const dst1 = out.dst[1];
    for (int64_t t = begin; t < end; t += 8)
    {
        const int64_t j = t - out.blockStart;
        alignas(32) float wLo[8];
        alignas(32) float wHi[8];
        alignas(32) float wFrac[8];
        alignas(32) float posFrac[8];
        int64_t base[8];
        for (int lane = 0; lane < 8; ++lane)
        {
            // renderInteriorSteps' lane, expression for expression.
            const double e = static_cast<double>(t + lane - onsetSample) - onsetFrac;
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
            const double pos = (startPos + rate * e) * invDec;
            const double floorPos = std::floor(pos);
            base[lane] = static_cast<int64_t>(floorPos);
            posFrac[lane] = static_cast<float>(pos - floorPos);
        }
        taps.locate(base);
        const V8 lo = _mm256_load_ps(wLo);
        const V8 w = _mm256_add_ps(
            lo, _mm256_mul_ps(_mm256_sub_ps(_mm256_load_ps(wHi), lo), _mm256_load_ps(wFrac)));
        // cmpgt_ps's truth table: w > 0, false for a NaN.
        const V8 sounds = _mm256_cmp_ps(w, zero, _CMP_GT_OQ);
        const V8 frac = _mm256_load_ps(posFrac);
        V8 s0;
        V8 s1;
        taps.template read<Reader, kTwoOutputs && kStereo>(base, frac, params, s0, s1);
        addMasked(dst0 + j, _mm256_mul_ps(s0, _mm256_mul_ps(w, gain0)), sounds);
        if constexpr (kTwoOutputs)
        {
            addMasked(dst1 + j, _mm256_mul_ps(s1, _mm256_mul_ps(w, gain1)), sounds);
        }
    }
}

/// detail::renderInterior over 8-sample steps.
template <class Reader, bool kTwoOutputs, bool kStereo, class Taps, class Dec>
GG_AVX2 void renderInterior8(const GrainRows &rows, uint32_t i, const Taps &taps, Dec decimation,
                             const Output &out, int64_t begin, int64_t end,
                             const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    GG_FP_CONTRACT_OFF
    if (begin >= end)
    {
        return;
    }
    if (isIdentity(detail::envelopeOf(rows, i)))
    {
        renderInteriorSteps8<Reader, kTwoOutputs, kStereo, false>(rows, i, taps, decimation, out,
                                                                  begin, end, tables);
    }
    else
    {
        renderInteriorSteps8<Reader, kTwoOutputs, kStereo, true>(rows, i, taps, decimation, out,
                                                                 begin, end, tables);
    }
}

/// detail::renderSampleInterior over 8-sample steps, with the same choice of taps.
template <class Reader, int64_t kStride, class Dec>
GG_AVX2 void renderSampleInterior8(const GrainRows &rows, uint32_t i, const SampleSource &src,
                                   Dec decimation, const Output &out, int64_t begin, int64_t end,
                                   const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    const BufferTaps8<kStride> taps{{src.data[0], src.data[1]}};
    const Dec d = decimation;
    if (out.dst[1] == nullptr)
    {
        renderInterior8<Reader, false, false>(rows, i, taps, d, out, begin, end, tables);
    }
    else if (src.mono)
    {
        renderInterior8<Reader, true, false>(rows, i, taps, d, out, begin, end, tables);
    }
    else if (src.interleaved != nullptr)
    {
        const InterleavedTaps8 pairs{src.interleaved};
        renderInterior8<Reader, true, true>(rows, i, pairs, d, out, begin, end, tables);
    }
    else
    {
        renderInterior8<Reader, true, true>(rows, i, taps, d, out, begin, end, tables);
    }
}

/// detail::renderSampleSimdAt with the 8-wide interior: the reference before the run, the run's
/// steps of 8 then of 4, and the reference after its last whole step of 4.
template <class Reader, int64_t kStride, class Dec>
GG_AVX2 void renderSampleAvx2At(const GrainRows &rows, uint32_t i, const SampleSource &src,
                                Dec decimation, const Output &out, int64_t first, int64_t last,
                                const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    if (first >= last)
    {
        return;
    }
    const detail::Run run =
        detail::sampleSimdRun<Reader>(rows, i, src.frames, decimation, first, last);
    const int64_t end8 = run.begin + (run.end - run.begin) / 8 * 8;
    const int64_t end4 = run.begin + (run.end - run.begin) / 4 * 4;
    detail::renderSampleRefAt<Reader, kStride>(rows, i, src, decimation, out, first, run.begin,
                                               tables);
    renderSampleInterior8<Reader, kStride>(rows, i, src, decimation, out, run.begin, end8, tables);
    detail::renderSampleInterior<Reader, kStride>(rows, i, src, decimation, out, end8, end4,
                                                  tables);
    detail::renderSampleRefAt<Reader, kStride>(rows, i, src, decimation, out, end4, last, tables);
}

/// renderSampleSimd<Reader> with the 8-wide interior, with renderSampleSimd's choice of stride
/// and decimation (detail::UnitDecimation for level 0).
template <class Reader>
GG_AVX2 void renderSampleAvx2(const GrainRows &rows, uint32_t i, const SampleSource &src,
                              const Output &out, int64_t from, int64_t to,
                              const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    const int64_t first = std::max(from, rows.onsetSample[i]);
    const int64_t last = std::min(to, rows.endSample[i]); // exclusive
    if (src.stride == 2)
    {
        renderSampleAvx2At<Reader, 2>(rows, i, src, src.decimation, out, first, last, tables);
    }
    else if (src.decimation == 1)
    {
        renderSampleAvx2At<Reader, 1>(rows, i, src, detail::UnitDecimation{}, out, first, last,
                                      tables);
    }
    else
    {
        renderSampleAvx2At<Reader, 1>(rows, i, src, src.decimation, out, first, last, tables);
    }
}

/// renderLiveSimd<Reader> with the 8-wide interior, at `decimation` (a uint32_t or
/// detail::UnitDecimation): on each part of liveInterior, its steps of 8 then its last whole step
/// of 4; the reference everywhere else.
template <class Reader, class Dec>
GG_AVX2 void renderLiveAvx2At(const GrainRows &rows, uint32_t i, const LiveSource &live,
                              Dec decimation, const Output &out, int64_t from, int64_t to,
                              const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    const LiveInterior in = liveInterior<Reader>(rows, i, live, decimation, from, to);
    const LiveSource::RingView ring = live.view();
    const RingTaps8<Reader> taps8{{ring.slots}, ring.capacity};
    const detail::RingTaps<Reader> taps4{{ring.slots}, ring.capacity};
    int64_t done = from;
    for (int part = 0; part < 2; ++part)
    {
        const int64_t begin = in.begin[part];
        const int64_t end8 = begin + (in.end[part] - begin) / 8 * 8;
        const int64_t end4 = begin + (in.end[part] - begin) / 4 * 4;
        renderLiveRef<Reader>(rows, i, live, decimation, out, done, begin, tables);
        if (out.dst[1] == nullptr)
        {
            renderInterior8<Reader, false, false>(rows, i, taps8, decimation, out, begin, end8,
                                                  tables);
            detail::renderInterior<Reader, false, false>(rows, i, taps4, decimation, out, end8,
                                                         end4, tables);
        }
        else
        {
            renderInterior8<Reader, true, true>(rows, i, taps8, decimation, out, begin, end8,
                                                tables);
            detail::renderInterior<Reader, true, true>(rows, i, taps4, decimation, out, end8, end4,
                                                       tables);
        }
        done = end4;
    }
    renderLiveRef<Reader>(rows, i, live, decimation, out, done, to, tables);
}

/// renderLiveSimd<Reader> with the 8-wide interior, through detail::UnitDecimation for 1.
template <class Reader>
GG_AVX2 void renderLiveAvx2(const GrainRows &rows, uint32_t i, const LiveSource &live,
                            uint32_t decimation, const Output &out, int64_t from, int64_t to,
                            const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING
{
    if (std::max(from, rows.onsetSample[i]) >= std::min(to, rows.endSample[i]))
    {
        return;
    }
    if (decimation == 1)
    {
        renderLiveAvx2At<Reader>(rows, i, live, detail::UnitDecimation{}, out, from, to, tables);
    }
    else
    {
        renderLiveAvx2At<Reader>(rows, i, live, decimation, out, from, to, tables);
    }
}

constexpr Dispatch kAvx2{
    &renderSampleAvx2<Hermite>,
    &renderSampleAvx2<Linear>,
    &renderSampleAvx2<Sinc>,
    &renderLiveAvx2<Hermite>,
    &renderLiveAvx2<Linear>,
    &renderLiveAvx2<Sinc>,
    8,
};
} // namespace

namespace detail
{
const Dispatch &avx2Table() noexcept { return kAvx2; }
} // namespace detail
} // namespace gg::kernels

#endif // GG_KERNELS_AVX2
