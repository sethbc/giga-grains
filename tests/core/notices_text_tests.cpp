// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)
//
// THIRD_PARTY_NOTICES.md carries the notices of M3 sub-project 4's two dependencies.

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

TEST_CASE("the third-party notices carry tuning-library and the MTS-ESP client", "[core][tuning]")
{
    std::ifstream in(std::string(GG_SOURCE_DIR) + "/THIRD_PARTY_NOTICES.md", std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    for (const char *needed :
         {"tuning-library", "d6fcc08", "Copyright 2019-2020, Paul Walker", "core/src/Tuning.cpp",
          "MTS-ESP", "f214739", "Copyright (C) 2021 by ODDSound Ltd.",
          "Permission to use, copy, modify, and/or distribute this software for any purpose with "
          "or without fee is hereby granted.",
          "libMTS", "libs/mts-esp-client/"})
    {
        INFO(needed);
        CHECK(text.find(needed) != std::string::npos);
    }
}
