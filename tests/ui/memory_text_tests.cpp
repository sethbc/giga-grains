// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The sample memory readout's text and the cap menu's choices (gg/ui/MemoryText.h).

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "gg/ui/MemoryText.h"

namespace
{
constexpr uint64_t kMiB = 1024ull * 1024ull;
} // namespace

TEST_CASE("formatMemory reads like the refusal", "[ui]")
{
    using gg::ui::formatMemory;
    CHECK(formatMemory(0, 1024) == "0 MB of 1 GB");
    CHECK(formatMemory(312 * kMiB, 1024) == "312 MB of 1 GB");
    CHECK(formatMemory(1, 512) == "1 MB of 512 MB"); // rounded up, as the refusal's need is
    CHECK(formatMemory(312 * kMiB + 1, 1024) == "313 MB of 1 GB");
    CHECK(formatMemory(1024 * kMiB, 2048) == "1 GB of 2 GB");
    CHECK(formatMemory(1536 * kMiB, 4096) == "1.5 GB of 4 GB");
    // Over the cap: the figures say so as they are.
    CHECK(formatMemory(700 * kMiB, 512) == "700 MB of 512 MB");
    CHECK(formatMemory(3 * 1024 * kMiB, 1024) == "3 GB of 1 GB");
    CHECK(formatMemory(16384 * kMiB, 16384) == "16 GB of 16 GB");
    CHECK(formatMemory(1023 * kMiB, 1536) == "1023 MB of 1.5 GB");
}

TEST_CASE("the cap choices", "[ui]")
{
    const auto choices = gg::ui::memoryCapChoicesMiB();
    const std::vector<uint32_t> expected{64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
    CHECK(std::vector<uint32_t>(choices.begin(), choices.end()) == expected);
    CHECK(gg::ui::formatMemorySize(64) == "64 MB");
    CHECK(gg::ui::formatMemorySize(512) == "512 MB");
    CHECK(gg::ui::formatMemorySize(1024) == "1 GB");
    CHECK(gg::ui::formatMemorySize(16384) == "16 GB");
}
