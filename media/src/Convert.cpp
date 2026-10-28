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
/// Input frames handed to the resampler per call: its MaxInLen.
constexpr int kBlockFrames = 4096;
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
    const int64_t outFrames = std::llround(double(inFrames) * targetRate / sourceRate);
    auto out = std::make_shared<SampleBuffer>(in->channels(), outFrames, targetRate);
    if (outFrames == 0)
    {
        return out;
    }

    std::vector<double> block(kBlockFrames);
    for (uint32_t c = 0; c < in->channels(); ++c)
    {
        // A resampler of its own per channel, so each channel converts as it would alone. Its
        // output starts at the input's frame 0: r8brain consumes the filters' latency itself
        // (getLatency() is 0) before it emits anything.
        r8b::CDSPResampler24 resampler(sourceRate, targetRate, kBlockFrames);
        const float *x = in->channel(c);
        float *y = out->channel(c);
        int64_t read = 0;
        int64_t written = 0;
        while (written < outFrames)
        {
            int frames = kBlockFrames;
            if (read < inFrames)
            {
                frames = static_cast<int>(std::min<int64_t>(kBlockFrames, inFrames - read));
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
