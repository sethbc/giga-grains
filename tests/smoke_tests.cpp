// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Version.h"

TEST_CASE("engine library links and reports version", "[smoke]")
{
    REQUIRE(gg::kVersion == "0.0.1");
    STATIC_REQUIRE(sizeof(void *) == 8);
}
