// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The harness's draws are keyed by CLAP ID, so a parameter appended later moves no existing draw.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gg/core/ParamIds.h"
#include "sweep/SweepDriver.h"

namespace sd = gg::test::sweepdraw;

TEST_CASE("sweep draws are stable when parameters are appended", "[sweep]")
{
    const std::vector<uint32_t> old = sd::slotIds();
    REQUIRE(old.size() == gg::core::kParamCount);
    // The list as a later sub-project leaves it: 20 IDs appended (above every existing one).
    std::vector<uint32_t> grown = old;
    for (uint32_t k = 0; k < 20; ++k)
    {
        grown.push_back(0x00900000u + k);
    }

    // Literal draws, so a change to the hash or the key layout fails visibly (a change moves
    // every case's draws: re-pin the floors then).
    CHECK(sd::byId(2101, 0, 0x000004) == Catch::Approx(0.05558309666093175).margin(1e-12));
    CHECK(sd::byId(2101, 5, 0x000004) == Catch::Approx(0.93484181712987657).margin(1e-12));
    CHECK(sd::byId(2101, 5, 0x10000a) == Catch::Approx(0.4058076046218031).margin(1e-12));
    // The picks by ID (D's 0x18 and B's 0x14), not by slot: a layer row appended to every layer
    // (M2 sub-project 4's) moves the later layers' slots, never an ID.
    CHECK(old[sd::pickById(3101, 0, old)] == 0x040018u);
    CHECK(old[sd::pickById(3101, 1023, old)] == 0x020014u);
    CHECK(sd::eventField(3101, 7, 2) == Catch::Approx(0.22751742639552208).margin(1e-12));

    // A burst's pick: appending IDs changes an event's parameter only to an appended one, and
    // only when its key beats the old winner's; the rest of the event's draws never move.
    constexpr uint64_t kEvents = 16 * 1024;
    uint64_t moved = 0;
    for (uint64_t e = 0; e < kEvents; ++e)
    {
        const std::size_t a = sd::pickById(3101, e, old);
        const std::size_t b = sd::pickById(3101, e, grown);
        if (b != a)
        {
            ++moved;
            INFO("event " << e);
            REQUIRE(b >= old.size()); // taken over by an appended ID, never reshuffled
        }
    }
    // About 20 / (N + 20) of the events go to the new IDs, and the others keep their choice.
    const double expected = static_cast<double>(kEvents) * 20.0 / static_cast<double>(grown.size());
    INFO("moved " << moved << ", expected about " << expected);
    CHECK(static_cast<double>(moved) < 1.5 * expected);
    CHECK(static_cast<double>(moved) > 0.5 * expected);

    // The pick doesn't depend on the order of the list either.
    std::vector<uint32_t> reversed(old.rbegin(), old.rend());
    for (uint64_t e = 0; e < 256; ++e)
    {
        CHECK(old[sd::pickById(3101, e, old)] == reversed[sd::pickById(3101, e, reversed)]);
    }
}
