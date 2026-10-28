// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_GRAINKERNELS_H
#define GG_GRAIN_GRAINKERNELS_H

#include <algorithm>
#include <cstddef>
#include <cstdint>

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
    static float read(const float *data, int64_t frames,
                      double pos) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return interp::hermite4(data, frames, pos);
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
    static float read(const float *data, int64_t frames,
                      double pos) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return interp::linear(data, frames, pos);
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
