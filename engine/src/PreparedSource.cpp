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
        }
        catch (const std::bad_alloc &)
        {
            // assign() builds its storage before replacing the empty one, so pairs_ is still
            // empty: the engine reads the planar channels, the same bits, only slower.
        }
    }
    return source;
}
} // namespace gg
