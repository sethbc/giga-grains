// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/Convert.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include <CDSPResampler.h>

namespace gg::media
{
namespace
{
/// The most input frames handed to the resampler per call.
constexpr int kBlockFrames = 4096;
/// The most output frames one call may produce, about: r8brain sizes its buffers as MaxInLen ×
/// the rate ratio, so a fixed MaxInLen at a large ratio (a 1 Hz file played at 768 kHz) would
/// ask for gigabytes, or overflow its int lengths.
constexpr double kMaxOutPerCall = 65536.0;
} // namespace

std::shared_ptr<SampleBuffer> convertRate(std::shared_ptr<SampleBuffer> in, double targetRate)
{
    // The header's preconditions; prepare (gg/media/Sample.h) checks them in every build.
    assert(in != nullptr);
    assert(std::isfinite(targetRate) && targetRate > 0.0);
    const double sourceRate = in->sampleRate();
    if (sourceRate == targetRate)
    {
        return in;
    }
    const int64_t inFrames = in->frames();
    // At least one frame of a buffer that has any: a very short file at a much lower rate would
    // otherwise round to none.
    const int64_t outFrames =
        inFrames == 0
            ? 0
            : std::max<int64_t>(1, std::llround(double(inFrames) * targetRate / sourceRate));
    auto out = std::make_shared<SampleBuffer>(in->channels(), outFrames, targetRate);
    if (outFrames == 0)
    {
        return out;
    }

    // The resampler's MaxInLen: kBlockFrames, or fewer when the rate rises so much that
    // kBlockFrames would give more than kMaxOutPerCall frames out per call.
    // Clamped as a double: at a large fall in rate the product exceeds int.
    const int maxIn = static_cast<int>(
        std::clamp(kMaxOutPerCall * sourceRate / targetRate, 1.0, double(kBlockFrames)));
    std::vector<double> block(static_cast<size_t>(maxIn));
    for (uint32_t c = 0; c < in->channels(); ++c)
    {
        // A resampler of its own per channel, so each channel converts as it would alone. Its
        // output starts at the input's frame 0: r8brain consumes the filters' latency itself
        // (getLatency() is 0) before it emits anything.
        r8b::CDSPResampler24 resampler(sourceRate, targetRate, maxIn);
        const float *x = in->channel(c);
        float *y = out->channel(c);
        int64_t read = 0;
        int64_t written = 0;
        while (written < outFrames)
        {
            int frames = maxIn;
            if (read < inFrames)
            {
                frames = static_cast<int>(std::min<int64_t>(maxIn, inFrames - read));
                std::copy_n(x + read, frames, block.data());
                read += frames;
            }
            else
            {
                // Past the input, silence flushes the filters' tail. Refilled every time:
                // process() may use its input as scratch.
                std::fill(block.begin(), block.end(), 0.0);
            }
            double *produced = nullptr;
            const int got = resampler.process(block.data(), frames, produced);
            const int64_t take = std::min<int64_t>(got, outFrames - written);
            for (int64_t n = 0; n < take; ++n)
            {
                y[written + n] = static_cast<float>(produced[n]);
            }
            written += take;
        }
    }
    return out;
}
} // namespace gg::media
