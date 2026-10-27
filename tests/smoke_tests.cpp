// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Version.h"

TEST_CASE("engine library links and reports version", "[smoke]")
{
    // The library's version is the build's (CMake's PROJECT_VERSION), "MAJOR.MINOR.PATCH".
    REQUIRE(gg::kVersion == GG_VERSION_STRING);
    REQUIRE(std::count(gg::kVersion.begin(), gg::kVersion.end(), '.') == 2);
    REQUIRE(std::all_of(gg::kVersion.begin(), gg::kVersion.end(),
                        [](char c) { return c == '.' || (c >= '0' && c <= '9'); }));
    STATIC_REQUIRE(sizeof(void *) == 8);
}
