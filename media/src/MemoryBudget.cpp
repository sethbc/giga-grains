// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/MemoryBudget.h"

#include <algorithm>

namespace gg::media
{
namespace
{
constexpr uint64_t kMiB = uint64_t{1} << 20;
constexpr uint64_t kFloat = sizeof(float);
/// A ring's guard slots past each level's end (LiveSource::kGuardFrames).
constexpr uint64_t kRingGuard = 3;

uint64_t ceilShift(uint64_t frames, uint32_t n) noexcept
{
    return (frames >> n) + ((frames & ((uint64_t{1} << n) - 1)) != 0 ? 1 : 0);
}

/// The most levels counted: a shift past 63 bits would be undefined, and no pyramid comes near.
constexpr uint32_t kMaxLevels = 32;

uint64_t mibUp(uint64_t bytes) noexcept { return bytes / kMiB + (bytes % kMiB != 0 ? 1 : 0); }
} // namespace

uint64_t sampleBytes(int64_t frames, uint32_t channels, bool interleavedCopy,
                     uint32_t levels) noexcept
{
    if (frames <= 0)
    {
        return 0;
    }
    const auto n = static_cast<uint64_t>(frames);
    uint64_t bytes = n * channels * kFloat;
    if (interleavedCopy)
    {
        bytes += n * 2 * kFloat;
    }
    for (uint32_t level = 1; level < std::min(levels, kMaxLevels); ++level)
    {
        bytes += ceilShift(n, level) * channels * kFloat;
    }
    return bytes;
}

uint64_t sourceBytes(const PreparedSource &source) noexcept
{
    const SampleBuffer &buffer = source.buffer();
    return sampleBytes(buffer.frames(), buffer.channels(), source.pairs().frames() > 0,
                       source.levelCount());
}

uint64_t ringBytes(int64_t frames, uint32_t maxBlockSize, uint32_t levels) noexcept
{
    if (frames <= 0)
    {
        return 0;
    }
    const uint64_t capacity = static_cast<uint64_t>(frames) + maxBlockSize;
    uint64_t bytes = (capacity + kRingGuard) * 2 * kFloat;
    for (uint32_t level = 1; level < std::min(levels, kMaxLevels); ++level)
    {
        bytes += (ceilShift(capacity, level) + kRingGuard) * 2 * kFloat;
    }
    return bytes;
}

bool MemoryBudget::fits(uint64_t need) const noexcept
{
    return usedBytes <= capBytes && need <= capBytes - usedBytes;
}

std::string MemoryBudget::refusal(uint64_t need) const
{
    const uint64_t free = usedBytes < capBytes ? capBytes - usedBytes : 0;
    return "over the memory cap: needs " + std::to_string(mibUp(need)) + " MB, " +
           std::to_string(free / kMiB) + " MB free of " + std::to_string(mibUp(capBytes)) + " MB";
}
} // namespace gg::media
