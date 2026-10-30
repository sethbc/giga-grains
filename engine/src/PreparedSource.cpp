// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/source/PreparedSource.h"

#include <new>
#include <utility>

namespace gg
{
PreparedSource::PreparedSource(std::shared_ptr<const SampleBuffer> buffer) noexcept
    : buffer_(std::move(buffer))
{
    // Level 0: the planar channels. A buffer without channels keeps the empty default, frames 0,
    // so the interpolators never touch its (null) data.
    const SampleBuffer &b = *buffer_;
    if (b.channels() > 0)
    {
        Level &level0 = levels_[0];
        level0.mono = b.channels() == 1;
        level0.data[0] = b.channel(0);
        level0.data[1] = level0.mono ? level0.data[0] : b.channel(1);
        level0.frames = b.frames();
    }
}

void PreparedSource::buildLevel(uint32_t n)
{
    const Level &above = levels_[n - 1];
    const bool mono = above.mono;
    const int64_t channels = mono ? 1 : 2;
    const int64_t frames = mipLevelFrames(above.frames, 1);
    const int64_t guard = InterleavedStereo::kGuardFrames;
    // Zeroed, so the guard frames before and after the level's frames read 0.
    std::vector<float> storage(static_cast<std::size_t>(channels * (frames + 2 * guard)), 0.0f);
    float *const lv = storage.data() + channels * guard;
    for (int64_t c = 0; c < channels; ++c)
    {
        decimateHalfband(above.data[c], above.frames, above.stride, lv + c, frames, channels);
    }
    storage_[n - 1] = std::move(storage); // moving keeps lv valid
    Level &level = levels_[n];
    level.data[0] = lv;
    level.data[1] = mono ? lv : lv + 1;
    level.frames = frames;
    level.stride = channels;
    level.interleaved = mono ? nullptr : lv;
    level.mono = mono;
    level.decimation = 1u << n;
}

std::shared_ptr<const PreparedSource>
PreparedSource::make(std::shared_ptr<const SampleBuffer> buffer)
{
    if (buffer == nullptr)
    {
        return nullptr;
    }
    // The constructor is private, so not std::make_shared.
    std::shared_ptr<PreparedSource> source(new PreparedSource(std::move(buffer)));
    const SampleBuffer &b = *source->buffer_;
    if (b.channels() >= 2)
    {
        // The SIMD sample kernel's stereo reads (§16 lever 2); a mono source stays planar.
        try
        {
            source->pairs_.assign(b.channel(0), b.channel(1), b.frames());
            source->levels_[0].interleaved = source->pairs_.data();
        }
        catch (const std::bad_alloc &)
        {
            // assign() builds its storage before replacing the empty one, so pairs_ is still
            // empty: the engine reads the planar channels, the same bits, only slower.
        }
    }
    if (b.channels() > 0)
    {
        // The pyramid (spec §6.3), each level from the one above.
        const uint32_t count = mipLevelCount(b.frames());
        for (uint32_t n = 1; n < count; ++n)
        {
            try
            {
                source->buildLevel(n);
            }
            catch (const std::bad_alloc &)
            {
                break; // the levels built so far stand; the rest read level 0
            }
            source->levelCount_ = n + 1;
        }
    }
    return source;
}

std::size_t PreparedSource::bytes() const noexcept
{
    const SampleBuffer &b = *buffer_;
    std::size_t floats =
        static_cast<std::size_t>(b.channels()) * static_cast<std::size_t>(b.frames());
    if (pairs_.data() != nullptr)
    {
        floats +=
            2 * static_cast<std::size_t>(pairs_.frames() + 2 * InterleavedStereo::kGuardFrames);
    }
    for (const std::vector<float> &level : storage_)
    {
        floats += level.size();
    }
    return floats * sizeof(float);
}
} // namespace gg
