// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The voice schedules (VoiceSchedules.h) at five block sizes: bit-identical, finite, and their
// hashes written for the sub-project's at-rest check (M3 sub-project 9, Task 1).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include <catch2/catch_test_macros.hpp>

#include "VoiceSchedules.h"

namespace
{
bool sameBits(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() &&
           (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

bool allFinite(const std::vector<float> &v)
{
    for (const float x : v)
    {
        if (!std::isfinite(x))
        {
            return false;
        }
    }
    return true;
}
} // namespace

TEST_CASE("voice schedules write their render hashes", "[engine][voicefilter]")
{
    const std::filesystem::path dir{GG_TEST_OUTPUT_DIR};
    std::filesystem::create_directories(dir);
    std::ofstream file(dir / "voice-schedules.txt", std::ios::trunc);
    REQUIRE(file.good());

    for (const auto &s : gg::test::voiceSchedules())
    {
        INFO(s.name);
        const auto ref = gg::test::renderSchedule(s, 4096);
        REQUIRE(!ref.events.empty());
        for (const uint32_t block : {1u, 32u, 64u, 255u})
        {
            INFO("block " << block);
            const auto r = gg::test::renderSchedule(s, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                REQUIRE(sameBits(r.out[ch], ref.out[ch]));
                REQUIRE(sameBits(r.bus2[ch], ref.bus2[ch]));
            }
            REQUIRE(r.events == ref.events);
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            REQUIRE(allFinite(ref.out[ch]));
            REQUIRE(allFinite(ref.bus2[ch]));
        }
        if (s.name == "steal")
        {
            std::map<uint32_t, int> starts;
            bool reused = false;
            for (const auto &e : s.events)
            {
                if (e.kind == gg::test::ScheduleEvent::Kind::Start && ++starts[e.slot] > 1)
                {
                    reused = true;
                }
            }
            REQUIRE(reused);
        }
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx",
                      static_cast<unsigned long long>(gg::test::fnv1a(ref)));
        file << s.name << ' ' << hex << '\n';
    }
    REQUIRE(file.good());
}
