// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/source/RingStorage.h"

#include <algorithm>
#include <stdexcept>

namespace gg
{
RingStorage::RingStorage(int64_t frames, uint32_t maxBlockSize)
{
    const auto block = static_cast<int64_t>(std::max<uint32_t>(1, maxBlockSize));
    if (frames < 1)
    {
        throw std::invalid_argument("RingStorage: frames must be >= 1");
    }
    if (frames > kMaxRingFrames - block)
    {
        throw std::invalid_argument("RingStorage: the ring would exceed 2^40 frames");
    }
    frames_ = frames;
    capacity_ = frames + block;
    slots_.assign(2 * static_cast<std::size_t>(capacity_ + kGuardFrames), 0.0f);
}
} // namespace gg
