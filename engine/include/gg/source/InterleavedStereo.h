// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_SOURCE_INTERLEAVEDSTEREO_H
#define GG_SOURCE_INTERLEAVEDSTEREO_H

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace gg
{
/// A stereo sample source's two channels interleaved, L, R per frame, for the SIMD sample
/// kernel (§16 lever 2): the taps a read takes from both channels are one run of memory, so a
/// lane's Hermite taps are two 4-float loads from one address instead of eight scalar loads from
/// two. kGuardFrames frames of zeros sit before frame 0 and after the last frame.
///
/// It is a copy: with the planar SampleBuffer it doubles a stereo source's memory. The scalar
/// reference keeps reading the planar channels; the SIMD interior reads only frames inside the
/// source, where the copy holds the same floats, so the output's bits don't depend on which it
/// reads. Only assign() and clear() allocate or free; data() is real-time safe.
class InterleavedStereo
{
  public:
    static constexpr int64_t kGuardFrames = 3;

    /// Copies frames frames of left and right. Not real-time safe. frames 0 (or less) clears.
    void assign(const float *left, const float *right, int64_t frames)
    {
        if (frames <= 0)
        {
            clear();
            return;
        }
        const auto n = static_cast<std::size_t>(frames);
        const auto guard = static_cast<std::size_t>(kGuardFrames);
        // A fresh vector, so a smaller source doesn't keep the old one's memory.
        std::vector<float> storage(2 * (n + 2 * guard), 0.0f);
        float *const pairs = storage.data() + 2 * guard;
        for (std::size_t f = 0; f < n; ++f)
        {
            pairs[2 * f] = left[f];
            pairs[2 * f + 1] = right[f];
        }
        storage_ = std::move(storage);
        data_ = storage_.data() + 2 * guard;
        frames_ = frames;
    }

    /// Frees the copy. Not real-time safe.
    void clear()
    {
        std::vector<float>().swap(storage_);
        data_ = nullptr;
        frames_ = 0;
    }

    /// Frame f's left sample is data()[2f] and its right data()[2f + 1], for f in
    /// [−kGuardFrames, frames() + kGuardFrames); the guard frames are 0. Null when empty.
    const float *data() const noexcept { return data_; }
    int64_t frames() const noexcept { return frames_; }

  private:
    std::vector<float> storage_;
    const float *data_{nullptr};
    int64_t frames_{0};
};
} // namespace gg

#endif // GG_SOURCE_INTERLEAVEDSTEREO_H
