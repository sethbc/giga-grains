// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The voice filter's bank (M3 sub-project 9's spec §3a FB1, FB2, FB6, FB11, FB15, FB17; F2, F9):
// BusFilter's law on four channels and 32 lanes. Channels 0 and 1 are BusFilter's bit for bit
// over the same cells, types and fades; a lane assigned during a fade joins it from silence; the
// lanes are taken lowest Free first and merge with the oldest Draining; the free rule; the
// controller's jumps; the sample rate's clamp and cap.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <sst/basic-blocks/simd/setup.h>
#include <sst/filters/CytomicSVF.h>

#include "VoiceFilterBank.h"
#include "fx/BusFilter.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Rng.h"
#include "gg/grain/GrainPool.h"
#include "gg/grain/VoiceFilter.h"
#include "support/AllocTripwire.h"

using gg::kLaneFrames;
using gg::kLaneFreeLevel;
using gg::kNoLane;
using gg::kVoiceLanes;
using gg::LaneState;
using gg::VoiceFilterBank;
using gg::VoiceFilterType;
using gg::core::BusFilterType;
using gg::core::fx::BusFilter;
using gg::test::AllocTripwire;

namespace
{
constexpr double kRate = 48000.0;

// Noise in ±0.5 from gg::rng, one stream per (seed, channel).
float noise(uint64_t seed, uint32_t channel, int64_t i)
{
    return 0.5f * static_cast<float>(gg::rng::bipolar(gg::rng::hash(
                      {seed, 0, channel, gg::rng::Field::SignalNoise, static_cast<uint64_t>(i)})));
}

// A uniform draw in [0, 1) for the cells' random values.
double draw(uint64_t field, uint64_t i)
{
    return gg::rng::uniform01(gg::rng::splitmix64(gg::rng::splitmix64(field) ^ i));
}

int64_t gridCeil(int64_t x) { return ((x + 31) / 32) * 32; }

VoiceFilterType asVoice(BusFilterType t) { return static_cast<VoiceFilterType>(t); }

sst::filters::CytomicSVF::Mode modeOf(VoiceFilterType t)
{
    using Mode = sst::filters::CytomicSVF::Mode;
    switch (t)
    {
    case VoiceFilterType::HighPass:
        return Mode::Highpass;
    case VoiceFilterType::BandPass:
        return Mode::Bandpass;
    case VoiceFilterType::Notch:
        return Mode::Notch;
    default:
        return Mode::Lowpass;
    }
}

struct Cell
{
    BusFilterType type;
    float cutoff;
    float resonance;
};

// 200 cells: a scripted head (Off to Low-pass, then Low-pass to High-pass turned back to
// Low-pass two cells in, Band-pass asked for during that fade, a NaN cutoff), then seeded random
// cells whose type holds for a few cells at a time, so fades start, turn and finish.
std::vector<Cell> cells()
{
    std::vector<Cell> out;
    for (int c = 0; c < 200; ++c)
    {
        const double u = draw(1, static_cast<uint64_t>(c));
        const float cutoff = static_cast<float>(10.0 * std::pow(3000.0, u)); // 10 Hz to 30 kHz
        const float res = static_cast<float>(-0.1 + 1.2 * draw(2, static_cast<uint64_t>(c)));
        BusFilterType type;
        if (c < 20)
        {
            type = BusFilterType::LowPass;
        }
        else if (c < 22)
        {
            type = BusFilterType::HighPass;
        }
        else if (c == 22)
        {
            type = BusFilterType::LowPass; // the turn back
        }
        else if (c < 30)
        {
            type = BusFilterType::BandPass; // a third type: waits at cell 23
        }
        else
        {
            const BusFilterType prev = out.back().type;
            type = draw(3, static_cast<uint64_t>(c)) < 0.3
                       ? static_cast<BusFilterType>(
                             static_cast<int>(draw(4, static_cast<uint64_t>(c)) * 5.0))
                       : prev;
        }
        out.push_back({type, c == 11 ? std::nanf("") : cutoff, res});
    }
    return out;
}

bool allFinite(const std::vector<float> &v)
{
    for (float x : v)
    {
        if (!std::isfinite(x))
        {
            return false;
        }
    }
    return true;
}
} // namespace

TEST_CASE("the bank's channels 0 and 1 are BusFilter's bit for bit", "[engine][voicefilter]")
{
    const uint32_t ramp = GENERATE(480u, 100u);
    // The lane pass's piece length inside each cell: whole cells, or pieces of 7 (and a 0-frame
    // pass) so a fade can end inside a piece.
    const uint32_t piece = GENERATE(32u, 7u);
    CAPTURE(ramp, piece);
    const std::vector<Cell> script = cells();
    constexpr size_t kSamples = 200 * 32;

    BusFilter bus;
    bus.prepare(kRate, ramp);
    std::vector<float> busL(kSamples), busR(kSamples);
    for (size_t c = 0; c < script.size(); ++c)
    {
        bus.setCell(script[c].type, script[c].cutoff, script[c].resonance);
        for (size_t i = c * 32; i < c * 32 + 32; ++i)
        {
            float l = noise(5, 0, static_cast<int64_t>(i));
            float r = noise(5, 1, static_cast<int64_t>(i));
            bus.process(l, r);
            busL[i] = l;
            busR[i] = r;
        }
    }

    VoiceFilterBank bank;
    bank.prepare(kRate, ramp);
    REQUIRE(bank.inForce() == VoiceFilterType::Off);
    const uint8_t lane = bank.assign(0, 0, 20000.0f, 0.0f);
    REQUIRE(lane == 0);
    std::vector<std::array<float, 4>> out(kSamples);
    bool sawTurn = false;
    bool sawWait = false;

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (size_t c = 0; c < script.size(); ++c)
        {
            bank.request(asVoice(script[c].type));
            bank.applyRequest();
            if (c == 22 && bank.fading() && bank.inForce() == VoiceFilterType::HighPass &&
                bank.target() == VoiceFilterType::LowPass)
            {
                sawTurn = true;
            }
            if (c == 23 && bank.fading() && bank.requested() == VoiceFilterType::BandPass &&
                bank.target() == VoiceFilterType::LowPass)
            {
                sawWait = true;
            }
            bank.setCell(lane, script[c].cutoff, script[c].resonance);
            bank.beginCell();
            for (uint32_t at = 0; at < 32;)
            {
                const uint32_t n = std::min(piece, 32u - at);
                for (uint32_t i = 0; i < n; ++i)
                {
                    const auto t = static_cast<int64_t>(c * 32 + at + i);
                    bank.scratch(lane, 0)[i] = noise(5, 0, t);
                    bank.scratch(lane, 1)[i] = noise(5, 1, t);
                    bank.scratch(lane, 2)[i] = noise(6, 2, t);
                    bank.scratch(lane, 3)[i] = noise(6, 3, t);
                }
                bank.filter(lane, n);
                bank.advance(n);
                for (uint32_t i = 0; i < n; ++i)
                {
                    for (uint32_t ch = 0; ch < 4; ++ch)
                    {
                        out[c * 32 + at + i][ch] = bank.scratch(lane, ch)[i];
                    }
                }
                at += n;
                if (piece != 32)
                {
                    bank.filter(lane, 0);
                    bank.advance(0);
                }
            }
        }
    }
    if (AllocTripwire::available())
    {
        REQUIRE(AllocTripwire::count() == 0);
    }
    REQUIRE(sawTurn);
    REQUIRE(sawWait);

    std::vector<float> l(kSamples), r(kSamples), c2(kSamples);
    for (size_t i = 0; i < kSamples; ++i)
    {
        l[i] = out[i][0];
        r[i] = out[i][1];
        c2[i] = out[i][2];
    }
    REQUIRE(std::memcmp(l.data(), busL.data(), kSamples * sizeof(float)) == 0);
    REQUIRE(std::memcmp(r.data(), busR.data(), kSamples * sizeof(float)) == 0);
    REQUIRE(allFinite(c2));
}

TEST_CASE("a lane assigned during a fade joins it", "[engine][voicefilter]")
{
    constexpr uint32_t kRamp = 480;
    VoiceFilterBank bank;
    bank.prepare(kRate, kRamp);
    // Low-pass in force before any lane: a jump (FB15).
    bank.request(VoiceFilterType::LowPass);
    bank.applyRequest();
    REQUIRE(bank.inForce() == VoiceFilterType::LowPass);
    const uint8_t first = bank.assign(0, 0, 800.0f, 0.3f);
    REQUIRE(first == 0);

    using Svf = sst::filters::CytomicSVF;
    std::array<Svf, 2> ref{};
    uint32_t refPos = 0;
    bool joined = false;
    uint8_t second = kNoLane;
    const float srInv = static_cast<float>(1.0 / kRate);
    std::vector<float> got, want;

    for (int c = 0; c < 30; ++c)
    {
        if (c == 3)
        {
            bank.request(VoiceFilterType::HighPass); // the fade, Low-pass to High-pass
        }
        bank.applyRequest();
        if (c == 6)
        {
            REQUIRE(bank.fading());
            second = bank.assign(1, c * 32, 2500.0f, 0.6f);
            REQUIRE(second == 1);
            for (Svf &f : ref)
            {
                f.init();
                f.firstBlock = true;
            }
            refPos = 96; // three cells into the fade
            joined = true;
        }
        const float cut = 2500.0f + 40.0f * static_cast<float>(c);
        bank.setCell(first, 800.0f, 0.3f);
        if (joined)
        {
            bank.setCell(second, cut, 0.6f);
        }
        bank.beginCell();
        const bool refFading = joined && refPos < kRamp;
        if (joined)
        {
            if (refFading)
            {
                ref[0].setCoeffForBlock<32>(modeOf(VoiceFilterType::LowPass), cut, 0.95f * 0.6f,
                                            srInv);
            }
            ref[1].setCoeffForBlock<32>(modeOf(VoiceFilterType::HighPass), cut, 0.95f * 0.6f,
                                        srInv);
        }
        for (uint32_t i = 0; i < 32; ++i)
        {
            const int64_t t = c * 32 + i;
            for (uint32_t ch = 0; ch < 4; ++ch)
            {
                bank.scratch(first, ch)[i] = noise(9, ch, t);
                if (joined)
                {
                    bank.scratch(second, ch)[i] = noise(10, ch, t);
                }
            }
        }
        bank.filter(first, 32);
        if (joined)
        {
            bank.filter(second, 32);
        }
        bank.advance(32);
        if (!joined)
        {
            continue;
        }
        for (uint32_t i = 0; i < 32; ++i)
        {
            const int64_t t = c * 32 + i;
            std::array<float, 4> x{};
            for (uint32_t ch = 0; ch < 4; ++ch)
            {
                x[ch] = noise(10, ch, t);
                got.push_back(bank.scratch(second, ch)[i]);
            }
            if (refPos < kRamp)
            {
                std::array<float, 4> a = x;
                std::array<float, 4> b = x;
                ref[0].processBlockStep(a[0], a[1], a[2], a[3]);
                ref[1].processBlockStep(b[0], b[1], b[2], b[3]);
                ++refPos;
                const float f = static_cast<float>(refPos) / static_cast<float>(kRamp);
                for (uint32_t ch = 0; ch < 4; ++ch)
                {
                    want.push_back(a[ch] + (b[ch] - a[ch]) * f);
                }
            }
            else
            {
                ref[1].processBlockStep(x[0], x[1], x[2], x[3]);
                for (uint32_t ch = 0; ch < 4; ++ch)
                {
                    want.push_back(x[ch]);
                }
            }
        }
    }
    REQUIRE_FALSE(bank.fading());
    REQUIRE(bank.inForce() == VoiceFilterType::HighPass);
    REQUIRE(got.size() == want.size());
    REQUIRE(got.size() == 24u * 32u * 4u);
    REQUIRE(std::memcmp(got.data(), want.data(), got.size() * sizeof(float)) == 0);
}

TEST_CASE("lanes are assigned lowest free first and merge with the oldest draining",
          "[engine][voicefilter]")
{
    VoiceFilterBank bank;
    bank.prepare(kRate, 480);
    for (uint32_t o = 0; o < kVoiceLanes; ++o)
    {
        REQUIRE(bank.assign(o, 0, 1000.0f, 0.0f) == o);
        bank.raiseLastEnd(static_cast<uint8_t>(o), 1000 + o);
        REQUIRE(bank.state(static_cast<uint8_t>(o)) == LaneState::Owned);
        REQUIRE(bank.owner(static_cast<uint8_t>(o)) == o);
    }
    // Every lane Owned: nothing to take.
    REQUIRE(bank.assign(99, 0, 1000.0f, 0.0f) == kNoLane);
    for (uint8_t l = 16; l < 32; ++l)
    {
        bank.drain(l);
        REQUIRE(bank.state(l) == LaneState::Draining);
    }
    for (uint8_t l = 0; l < 16; ++l)
    {
        if (l != 3)
        {
            bank.drain(l);
        }
    }
    REQUIRE(bank.state(3) == LaneState::Owned);

    const uint8_t merged = bank.assign(40, 500, 3000.0f, 0.5f);
    REQUIRE(merged == 0);
    REQUIRE(bank.state(0) == LaneState::Owned);
    REQUIRE(bank.owner(0) == 40);
    REQUIRE(bank.lastEnd(0) == 1000);

    // The next merge takes ordinal 1's lane; a clock past its lastEnd raises it.
    REQUIRE(bank.assign(41, 5000, 3000.0f, 0.5f) == 1);
    REQUIRE(bank.lastEnd(1) == 5000);
    // Ordinal 3's lane is Owned, so it is passed over.
    REQUIRE(bank.assign(42, 0, 3000.0f, 0.5f) == 2);
    REQUIRE(bank.assign(43, 0, 3000.0f, 0.5f) == 4);

    // busyByOwner: ascending owner ordinal (lanes 3, 5 .. 31, then 0, 1, 2, 4).
    std::array<uint8_t, kVoiceLanes> order{};
    REQUIRE(bank.busyByOwner(order) == 32);
    REQUIRE(order[0] == 3);
    for (uint32_t i = 1; i < 28; ++i)
    {
        REQUIRE(order[i] == i + 4);
    }
    REQUIRE(order[28] == 0);
    REQUIRE(order[29] == 1);
    REQUIRE(order[30] == 2);
    REQUIRE(order[31] == 4);

    // A free lane is taken before any Draining one, and starts at the clock.
    bank.freeAll();
    REQUIRE_FALSE(bank.anyBusy());
    REQUIRE(bank.assign(7, 640, 1000.0f, 0.0f) == 0);
    REQUIRE(bank.lastEnd(0) == 640);
    REQUIRE(bank.assign(8, 640, 1000.0f, 0.0f) == 1);
    REQUIRE(bank.busyByOwner(order) == 2);
}

TEST_CASE("a draining lane frees by the rule", "[engine][voicefilter]")
{
    constexpr int64_t kEnd = 1000;
    const int64_t due = gridCeil(kEnd); // 1024

    const auto drained = [&](VoiceFilterBank &bank) {
        bank.prepare(kRate, 480);
        bank.request(VoiceFilterType::LowPass);
        bank.applyRequest();
        const uint8_t lane = bank.assign(0, 0, 1000.0f, 0.5f);
        bank.raiseLastEnd(lane, kEnd);
        bank.drain(lane);
        return lane;
    };

    SECTION("not engaged: due frees it")
    {
        VoiceFilterBank bank;
        const uint8_t lane = drained(bank);
        bank.freeFinished(due - 32, true, false);
        REQUIRE(bank.state(lane) == LaneState::Draining);
        bank.freeFinished(kEnd, false, false); // past lastEnd, short of its grid sample
        REQUIRE(bank.state(lane) == LaneState::Draining);
        bank.freeFinished(due, true, false);
        REQUIRE(bank.state(lane) == LaneState::Free);
        REQUIRE_FALSE(bank.anyBusy());
    }
    SECTION("engaged: grid, no fade and a quiet state free it")
    {
        VoiceFilterBank bank;
        const uint8_t lane = drained(bank);
        REQUIRE(bank.stateMagnitude(lane) < kLaneFreeLevel);
        bank.freeFinished(due - 32, true, true);
        REQUIRE(bank.state(lane) == LaneState::Draining);
        bank.freeFinished(due, false, true);
        REQUIRE(bank.state(lane) == LaneState::Draining);
        bank.freeFinished(due, true, true);
        REQUIRE(bank.state(lane) == LaneState::Free);
    }
    SECTION("engaged: a state above the level keeps it")
    {
        VoiceFilterBank bank;
        const uint8_t lane = drained(bank);
        bank.setCell(lane, 1000.0f, 0.5f);
        bank.beginCell();
        for (uint32_t i = 0; i < 32; ++i)
        {
            for (uint32_t ch = 0; ch < 4; ++ch)
            {
                bank.scratch(lane, ch)[i] = noise(3, ch, i);
            }
        }
        bank.filter(lane, 32);
        bank.advance(32);
        REQUIRE(bank.stateMagnitude(lane) > kLaneFreeLevel);
        bank.freeFinished(due, true, true);
        REQUIRE(bank.state(lane) == LaneState::Draining);
        bank.freeFinished(due, true, false); // not engaged: due alone frees it
        REQUIRE(bank.state(lane) == LaneState::Free);
        REQUIRE(bank.stateMagnitude(lane) == 0.0f); // its filters reset
    }
    SECTION("engaged: a fade keeps it")
    {
        VoiceFilterBank bank;
        const uint8_t lane = drained(bank);
        bank.request(VoiceFilterType::HighPass);
        bank.applyRequest();
        REQUIRE(bank.fading());
        bank.freeFinished(due, true, true);
        REQUIRE(bank.state(lane) == LaneState::Draining);
    }
    SECTION("the cap frees a lane still ringing")
    {
        VoiceFilterBank bank;
        const uint8_t lane = drained(bank);
        const int64_t cap = gridCeil(kEnd + bank.capSamples());
        REQUIRE(bank.capSamples() == 192000);
        const float srInv = static_cast<float>(1.0 / kRate);
        bool freedAtCap = false;
        for (int64_t t = 0; t <= cap; t += 32)
        {
            bank.freeFinished(t, true, true);
            if (t < cap)
            {
                REQUIRE(bank.state(lane) == LaneState::Draining);
            }
            else
            {
                freedAtCap = bank.state(lane) == LaneState::Free;
                break;
            }
            bank.applyRequest();
            bank.setCell(lane, 1000.0f, 1.0f);
            bank.beginCell();
            for (uint32_t i = 0; i < 32; ++i)
            {
                const float x = 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f *
                                                static_cast<float>(t + i) * srInv);
                for (uint32_t ch = 0; ch < 4; ++ch)
                {
                    bank.scratch(lane, ch)[i] = x;
                }
            }
            bank.filter(lane, 32);
            bank.advance(32);
            if (t > due)
            {
                REQUIRE(bank.stateMagnitude(lane) > kLaneFreeLevel);
            }
        }
        REQUIRE(freedAtCap);
    }
}

TEST_CASE("the last lane freed mid-fade ends the fade", "[engine][voicefilter]")
{
    VoiceFilterBank bank;
    bank.prepare(kRate, 480);
    bank.request(VoiceFilterType::LowPass);
    bank.applyRequest();
    const uint8_t a = bank.assign(0, 0, 1000.0f, 0.0f);
    const uint8_t b = bank.assign(1, 0, 1000.0f, 0.0f);
    bank.drain(a);
    bank.drain(b);
    bank.raiseLastEnd(b, 64);
    bank.request(VoiceFilterType::Notch);
    bank.applyRequest();
    REQUIRE(bank.fading());
    REQUIRE(bank.target() == VoiceFilterType::Notch);
    // a's cap passes first: b still busy, so the fade goes on.
    const int64_t capA = gridCeil(bank.capSamples());
    bank.freeFinished(capA, true, true);
    REQUIRE(bank.state(a) == LaneState::Free);
    REQUIRE(bank.state(b) == LaneState::Draining);
    REQUIRE(bank.fading());
    REQUIRE(bank.inForce() == VoiceFilterType::LowPass);
    bank.freeFinished(gridCeil(64 + bank.capSamples()), true, true);
    REQUIRE_FALSE(bank.anyBusy());
    REQUIRE_FALSE(bank.fading());
    REQUIRE(bank.inForce() == bank.requested());
    REQUIRE(bank.inForce() == VoiceFilterType::Notch);
    REQUIRE(bank.target() == VoiceFilterType::Notch);

    SECTION("freeAll jumps to the request too")
    {
        bank.assign(2, 0, 1000.0f, 0.0f);
        bank.request(VoiceFilterType::BandPass);
        bank.applyRequest();
        REQUIRE(bank.fading());
        bank.freeAll();
        REQUIRE_FALSE(bank.anyBusy());
        REQUIRE_FALSE(bank.fading());
        REQUIRE(bank.inForce() == VoiceFilterType::BandPass);
    }
}

TEST_CASE("a request with no busy lane jumps", "[engine][voicefilter]")
{
    VoiceFilterBank bank;
    bank.prepare(kRate, 480);
    REQUIRE(bank.requested() == VoiceFilterType::Off);
    bank.request(VoiceFilterType::BandPass);
    REQUIRE(bank.requested() == VoiceFilterType::BandPass);
    REQUIRE(bank.inForce() == VoiceFilterType::Off); // not applied yet
    bank.applyRequest();
    REQUIRE_FALSE(bank.fading());
    REQUIRE(bank.inForce() == VoiceFilterType::BandPass);
    REQUIRE(bank.target() == VoiceFilterType::BandPass);

    // With a busy lane the same request fades.
    bank.assign(0, 0, 1000.0f, 0.0f);
    bank.request(VoiceFilterType::HighPass);
    bank.applyRequest();
    REQUIRE(bank.fading());
    REQUIRE(bank.inForce() == VoiceFilterType::BandPass);
    REQUIRE(bank.target() == VoiceFilterType::HighPass);

    // reset: every lane Free, Off in force and asked for, no fade.
    bank.reset();
    REQUIRE_FALSE(bank.anyBusy());
    REQUIRE_FALSE(bank.fading());
    REQUIRE(bank.inForce() == VoiceFilterType::Off);
    REQUIRE(bank.requested() == VoiceFilterType::Off);
}

TEST_CASE("the bank follows the sample rate", "[engine][voicefilter]")
{
    const auto render = [](double rate, float cutoff) {
        VoiceFilterBank bank;
        bank.prepare(rate, 100);
        bank.request(VoiceFilterType::LowPass);
        bank.applyRequest();
        const uint8_t lane = bank.assign(0, 0, cutoff, 1.0f);
        std::vector<float> out;
        for (int c = 0; c < 100; ++c)
        {
            bank.setCell(lane, cutoff, 1.0f);
            bank.beginCell();
            for (uint32_t i = 0; i < kLaneFrames; ++i)
            {
                for (uint32_t ch = 0; ch < 4; ++ch)
                {
                    bank.scratch(lane, ch)[i] = noise(12, ch, c * 32 + i);
                }
            }
            bank.filter(lane, kLaneFrames);
            bank.advance(kLaneFrames);
            for (uint32_t ch = 0; ch < 4; ++ch)
            {
                out.insert(out.end(), bank.scratch(lane, ch), bank.scratch(lane, ch) + 32);
            }
        }
        return out;
    };
    const std::vector<float> clamped = render(22050.0, 20000.0f);
    const std::vector<float> exact = render(22050.0, 9922.5f);
    REQUIRE(clamped.size() == exact.size());
    REQUIRE(std::memcmp(clamped.data(), exact.data(), clamped.size() * sizeof(float)) == 0);
    REQUIRE(allFinite(clamped));
    REQUIRE(allFinite(render(192000.0, 20000.0f)));

    VoiceFilterBank fast;
    fast.prepare(192000.0, 1920);
    REQUIRE(fast.capSamples() == 768000);
}
