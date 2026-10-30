// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Version.h"

TEST_CASE("engine library links and reports version", "[smoke]")
{
    // The library's version is the build's (spec §6.1): the numeric part, CMake's PROJECT_VERSION
    // "MAJOR.MINOR.PATCH", then "-" and the pre-release suffix unless that is empty.
    REQUIRE(gg::kVersion == GG_VERSION_STRING);
    REQUIRE(gg::kVersion.rfind(GG_VERSION_NUMERIC, 0) == 0);

    const std::string_view numeric{GG_VERSION_NUMERIC};
    REQUIRE(std::count(numeric.begin(), numeric.end(), '.') == 2);
    REQUIRE(std::all_of(numeric.begin(), numeric.end(),
                        [](char c) { return c == '.' || (c >= '0' && c <= '9'); }));

    if (std::string_view(GG_VERSION_PRERELEASE).empty())
        REQUIRE(gg::kVersion == GG_VERSION_NUMERIC);
    else
        REQUIRE(gg::kVersion == std::string(GG_VERSION_NUMERIC) + "-" + GG_VERSION_PRERELEASE);
    STATIC_REQUIRE(sizeof(void *) == 8);
}

TEST_CASE("the build is C++23", "[smoke][build]")
{
    // Beyond C++20: GCC 13 reports 202100L for -std=c++23 (202302L arrived in GCC 14).
    STATIC_REQUIRE(__cplusplus > 202002L);
}
