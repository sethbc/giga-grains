// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/source/SampleBuffer.h"

#include <cmath>
#include <stdexcept>

namespace gg
{
namespace
{
std::size_t checkedSampleCount(uint32_t channels, int64_t frames, double sampleRate)
{
    if (frames < 0)
    {
        throw std::invalid_argument("SampleBuffer: frames must be >= 0");
    }
    if (!(std::isfinite(sampleRate) && sampleRate > 0.0))
    {
        throw std::invalid_argument("SampleBuffer: sampleRate must be finite and > 0");
    }
    // Checked before multiplying, so an oversized shape can't wrap into a small allocation.
    if (channels != 0 && static_cast<uint64_t>(frames) > std::vector<float>().max_size() / channels)
    {
        throw std::length_error("SampleBuffer: channels * frames is too large");
    }
    return static_cast<std::size_t>(channels) * static_cast<std::size_t>(frames);
}
} // namespace

SampleBuffer::SampleBuffer(uint32_t channels, int64_t frames, double sampleRate)
    : channels_(channels), frames_(frames), sampleRate_(sampleRate),
      samples_(checkedSampleCount(channels, frames, sampleRate))
{
}
} // namespace gg
