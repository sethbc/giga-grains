// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/MemoryText.h"

#include <array>

namespace gg::ui
{
namespace
{
constexpr uint64_t kMiB = 1024ull * 1024ull;

constexpr std::array<uint32_t, 9> kChoices{64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
} // namespace

std::string formatMemorySize(uint64_t mib)
{
    if (mib < 1024)
    {
        return std::to_string(mib) + " MB";
    }
    // GB with one decimal, in integers: tenths of a GB, rounded to nearest.
    const uint64_t tenths = (mib * 10 + 512) / 1024;
    std::string text = std::to_string(tenths / 10);
    if (tenths % 10 != 0)
    {
        text += "." + std::to_string(tenths % 10);
    }
    return text + " GB";
}

std::string formatMemory(uint64_t usedBytes, uint32_t capMiB)
{
    const uint64_t usedMiB = usedBytes / kMiB + (usedBytes % kMiB != 0 ? 1 : 0);
    return formatMemorySize(usedMiB) + " of " + formatMemorySize(capMiB);
}

std::span<const uint32_t> memoryCapChoicesMiB() noexcept { return kChoices; }
} // namespace gg::ui
