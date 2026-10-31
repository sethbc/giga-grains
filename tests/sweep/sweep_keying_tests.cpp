// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The harness's draws are keyed by CLAP ID, so a parameter appended later moves no existing draw.

#include <cstddef>
#include <cstdint>
#include <vector>

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

    // A corner's draw for an ID depends on the ID alone.
    for (uint64_t corner = 0; corner < 32; ++corner)
    {
        for (const uint32_t id : old)
        {
            CHECK(sd::byId(2101, corner, id) == sd::byId(2101, corner, id));
        }
    }
    CHECK(sd::byId(2101, 0, old[0]) != sd::byId(2101, 0, old[1]));
    CHECK(sd::byId(2101, 0, old[0]) != sd::byId(2101, 1, old[0]));

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
        for (uint64_t f = 0; f < 4; ++f)
        {
            REQUIRE(sd::eventField(3101, e, f) == sd::eventField(3101, e, f));
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
