// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The drive's 2x half-band (color spec C4): a sample-by-sample port of sst-filters'
// HalfRateFilter(4, true), compared bit for bit with the library's own block processors.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include <sst/filters/HalfRateFilter.h>

#include "fx/HalfBand2x.h"
#include "gg/core/Rng.h"

namespace
{
using Reference = sst::filters::HalfRate::HalfRateFilter;

constexpr int kBase = 1024;

// Uniform in +-0.5, from the engine's counter-based RNG.
float noise(uint32_t channel, uint64_t i)
{
    return 0.5f * static_cast<float>(gg::rng::bipolar(
                      gg::rng::hash({91, 0, channel, gg::rng::Field::SignalNoise, i})));
}
} // namespace

TEST_CASE("half band up equals HalfRateFilter process_block_U2 bit for bit",
          "[core][color][determinism]")
{
    alignas(16) static float inL[kBase], inR[kBase], refL[2 * kBase], refR[2 * kBase];
    float gotL[2 * kBase], gotR[2 * kBase];
    for (int i = 0; i < kBase; ++i)
    {
        inL[i] = noise(0, static_cast<uint64_t>(i));
        inR[i] = noise(1, static_cast<uint64_t>(i));
    }
    Reference ref(4, true);
    for (int i = 0; i < kBase; i += 8)
        ref.process_block_U2(inL + i, inR + i, refL + 2 * i, refR + 2 * i, 16);

    gg::core::fx::HalfBand2x hb;
    hb.reset();
    for (int i = 0; i < kBase; ++i)
        hb.up(inL[i], inR[i], gotL + 2 * i, gotR + 2 * i);

    REQUIRE(std::memcmp(gotL, refL, sizeof gotL) == 0);
    REQUIRE(std::memcmp(gotR, refR, sizeof gotR) == 0);
}

TEST_CASE("half band down equals HalfRateFilter process_block_D2 bit for bit",
          "[core][color][determinism]")
{
    alignas(16) static float inL[2 * kBase], inR[2 * kBase];
    alignas(16) static float refL[2 * kBase], refR[2 * kBase];
    float gotL[kBase], gotR[kBase];
    for (int i = 0; i < 2 * kBase; ++i)
    {
        inL[i] = noise(2, static_cast<uint64_t>(i));
        inR[i] = noise(3, static_cast<uint64_t>(i));
    }
    std::memcpy(refL, inL, sizeof inL);
    std::memcpy(refR, inR, sizeof inR);
    Reference ref(4, true);
    // In place: each call of 16 leaves 8 outputs at the block's start.
    for (int i = 0; i < 2 * kBase; i += 16)
    {
        float *l = refL + i, *r = refR + i;
        ref.process_block_D2(l, r, 16);
    }
    // Gather the in-place results (each 16-block's 8 outputs sit at its start).
    // The in-place output of block b is 8 floats at refL + 16 b; copy it out in order.
    float wantL[kBase], wantR[kBase];
    for (int b = 0; b < kBase / 8; ++b)
    {
        std::memcpy(wantL + 8 * b, refL + 16 * b, 8 * sizeof(float));
        std::memcpy(wantR + 8 * b, refR + 16 * b, 8 * sizeof(float));
    }

    gg::core::fx::HalfBand2x hb;
    hb.reset();
    for (int i = 0; i < kBase; ++i)
        hb.down(inL + 2 * i, inR + 2 * i, gotL[i], gotR[i]);

    REQUIRE(std::memcmp(gotL, wantL, sizeof gotL) == 0);
    REQUIRE(std::memcmp(gotR, wantR, sizeof gotR) == 0);
}

TEST_CASE("half band handles single samples after reset", "[core][color]")
{
    gg::core::fx::HalfBand2x fresh;
    float fl[2], fr[2];
    fresh.up(0.25f, -0.5f, fl, fr);

    gg::core::fx::HalfBand2x used;
    float sl[2], sr[2];
    for (int i = 0; i < 100; ++i)
        used.up(noise(0, static_cast<uint64_t>(i)), noise(1, static_cast<uint64_t>(i)), sl, sr);
    used.reset();
    used.up(0.25f, -0.5f, sl, sr);

    REQUIRE(std::memcmp(fl, sl, sizeof fl) == 0);
    REQUIRE(std::memcmp(fr, sr, sizeof fr) == 0);
}
