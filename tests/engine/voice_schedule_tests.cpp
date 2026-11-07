// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The voice schedules (VoiceSchedules.h) at five block sizes: bit-identical, finite, and their
// hashes written for the sub-project's at-rest check (M3 sub-project 9, Task 1).

#include <algorithm>
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

float peakOf(const std::vector<float> &v)
{
    float p = 0.0f;
    for (const float x : v)
    {
        p = std::max(p, std::abs(x));
    }
    return p;
}

struct ParsedEvent
{
    unsigned voice{0};
    long long onset{0};
    unsigned bus{0};
};

// A recorded event line's voice, onset and bus (the format of VoiceSchedules.h's recordGrain).
ParsedEvent parse(const std::string &line)
{
    ParsedEvent e;
    REQUIRE(std::sscanf(line.c_str(), "i=%*u voice=%u onset=%lld", &e.voice, &e.onset) == 2);
    const auto at = line.rfind(" bus=");
    REQUIRE(at != std::string::npos);
    REQUIRE(std::sscanf(line.c_str() + at, " bus=%u", &e.bus) == 1);
    REQUIRE(e.bus < 2);
    return e;
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
        // The schedule sounds: every used channel has a peak above 0.
        for (int ch = 0; ch < (s.form == gg::test::ScheduleForm::Mono ? 1 : 2); ++ch)
        {
            REQUIRE(peakOf(ref.out[ch]) > 0.0f);
        }
        if (s.name == "bus-split")
        {
            int onBus[2] = {0, 0};
            for (const auto &e : ref.events)
            {
                ++onBus[parse(e).bus];
            }
            REQUIRE(onBus[0] > 0);
            REQUIRE(onBus[1] > 0);
            REQUIRE((peakOf(ref.bus2[0]) > 0.0f || peakOf(ref.bus2[1]) > 0.0f));
        }
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
            // Slot 0's second start (voice 16) is at 16 * 1500; the render shows voice 0's grains
            // before it, voice 16's from it, and none of voice 0's after.
            constexpr int64_t kReuse = 16 * 1500;
            int oldBefore = 0, oldAfter = 0, newBefore = 0, newAfter = 0;
            for (const auto &e : ref.events)
            {
                const auto g = parse(e);
                if (g.voice == 0)
                {
                    (g.onset < kReuse ? oldBefore : oldAfter)++;
                }
                else if (g.voice == 16)
                {
                    (g.onset < kReuse ? newBefore : newAfter)++;
                }
            }
            REQUIRE(oldBefore > 0);
            REQUIRE(oldAfter == 0);
            REQUIRE(newBefore == 0);
            REQUIRE(newAfter > 0);
        }
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx",
                      static_cast<unsigned long long>(gg::test::fnv1a(ref)));
        file << s.name << ' ' << hex << '\n';
    }
    REQUIRE(file.good());
}
