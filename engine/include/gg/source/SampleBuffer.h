// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_SOURCE_SAMPLEBUFFER_H
#define GG_SOURCE_SAMPLEBUFFER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gg
{
/// Planar float audio: `channels` separate arrays of `frames` samples recorded at `sampleRate`,
/// zero-initialized. Only the constructor allocates; the accessors are real-time safe.
class SampleBuffer
{
  public:
    /// Throws std::invalid_argument if frames < 0 or sampleRate is not finite and positive, and
    /// std::length_error if channels × frames samples can't be represented.
    SampleBuffer(uint32_t channels, int64_t frames, double sampleRate);

    /// Channel c's `frames` samples, or nullptr if c >= channels().
    float *channel(uint32_t c) noexcept
    {
        return c < channels_ ? samples_.data() + offset(c) : nullptr;
    }
    const float *channel(uint32_t c) const noexcept
    {
        return c < channels_ ? samples_.data() + offset(c) : nullptr;
    }

    uint32_t channels() const noexcept { return channels_; }
    int64_t frames() const noexcept { return frames_; }
    double sampleRate() const noexcept { return sampleRate_; }

  private:
    std::size_t offset(uint32_t c) const noexcept
    {
        return static_cast<std::size_t>(c) * static_cast<std::size_t>(frames_);
    }

    uint32_t channels_;
    int64_t frames_;
    double sampleRate_;
    std::vector<float> samples_;
};
} // namespace gg

#endif // GG_SOURCE_SAMPLEBUFFER_H
