// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Space's reverb (spec §6.3): sst-effects' Reverb 2 under gg-core's own FXConfig
// (core/src/space/GgFxConfig.h). Tagged [space].

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "space/GgFxConfig.h"

using gg::core::space::GgFxConfig;
using gg::core::space::Rev2;

namespace
{
constexpr int kChunk = GgFxConfig::blockSize;

/// Reverb 2's values for these tests: Surge's defaults, with the shortest pre-delay (2^-8 s, so
/// the tail starts within the first few chunks), Decay 1.0 (2 s) and Mix 100%.
void setRingingValues(GgFxConfig::Storage &storage)
{
    storage.values.fill(0.f);
    storage.values[Rev2::rev2_predelay] = -8.f;
    storage.values[Rev2::rev2_room_size] = 0.f;
    storage.values[Rev2::rev2_decay_time] = 1.f;
    storage.values[Rev2::rev2_diffusion] = 1.f;
    storage.values[Rev2::rev2_buildup] = 1.f;
    storage.values[Rev2::rev2_modulation] = 0.5f;
    storage.values[Rev2::rev2_lf_damping] = 0.2f;
    storage.values[Rev2::rev2_hf_damping] = 0.2f;
    storage.values[Rev2::rev2_width] = 0.f; // 0 dB: full stereo
    storage.values[Rev2::rev2_mix] = 1.f;
}
} // namespace

TEST_CASE("Reverb2 builds and rings with GgFxConfig", "[space]")
{
    GgFxConfig::Storage storage;
    storage.sampleRate = 48000.0;
    setRingingValues(storage);

    // About 14 MB of delay lines: never on the stack.
    auto reverb = std::make_unique<Rev2>(&storage, &storage, &storage);
    reverb->initialize();

    // One chunk holding a unit impulse at sample 0, then silence to 0.5 s. (200 silent chunks
    // end at 0.134 s, before the 0.2 s the tail check needs, so the run goes on to 0.5 s.)
    constexpr int kChunks = 750;
    std::vector<double> chunkEnergy(kChunks, 0.0);
    bool allFinite = true;
    for (int c = 0; c < kChunks; ++c)
    {
        alignas(16) std::array<float, kChunk> left{};
        alignas(16) std::array<float, kChunk> right{};
        if (c == 0)
        {
            left[0] = 1.f;
            right[0] = 1.f;
        }
        reverb->processBlock(left.data(), right.data());
        for (int i = 0; i < kChunk; ++i)
        {
            allFinite = allFinite && std::isfinite(left[i]) && std::isfinite(right[i]);
            chunkEnergy[c] += double(left[i]) * left[i] + double(right[i]) * right[i];
        }
    }
    REQUIRE(allFinite);

    double earlyEnergy = 0.0;
    for (int c = 2; c <= 200; ++c)
    {
        earlyEnergy += chunkEnergy[c];
    }
    REQUIRE(earlyEnergy > 0.0);

    // A tail: some chunk wholly after 0.2 s is non-zero.
    const int firstLateChunk = int(std::ceil(0.2 * storage.sampleRate / kChunk));
    bool tail = false;
    for (int c = firstLateChunk; c < kChunks; ++c)
    {
        tail = tail || chunkEnergy[c] > 0.0;
    }
    REQUIRE(tail);

    // Spec §6.3 budgets Reverb 2's fixed arrays; the report records the measured size.
    CHECK(sizeof(Rev2) > 8u * 1024 * 1024);
}
