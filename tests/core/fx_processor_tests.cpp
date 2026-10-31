// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The global delay and limiter in the Processor's serial tail (M2 sub-project 4's spec §6.4–§6.6):
// the stages leave the output as it was at Amount 0 and Mode Off, the delay reveals what already
// played and glides its time without a step, the limiter holds its ceiling in both modes, starts
// limited from a loaded state's first sample and fades its mode changes, and none of it
// allocates.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/LayersFixture.h"
#include "fx/BusFilter.h"
#include "gg/core/Delay.h"
#include "gg/core/Limiter.h"
#include "gg/core/MathDet.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Processor.h"
#include "gg/metrics/Metrics.h"
#include "support/AllocTripwire.h"

using gg::core::Mode;
using gg::core::ParamEvent;
using gg::core::ParamSlot;
using gg::core::ParamValues;
using gg::core::Processor;
using gg::test::AllocTripwire;
using namespace gg::test::layers;

namespace
{
constexpr double kLog2Of10 = 3.321928094887362;

float ceilingGain(float dB)
{
    return static_cast<float>(gg::exp2Det(static_cast<double>(dB) * kLog2Of10 / 20.0));
}

// Four Sample layers over four sines at the pad's centre, Mix 1, hot (Level +12 dB): the wet sum
// is well past 0.5 and the reverb idle.
void hotLayers(Processor &p)
{
    p.setBase(ParamSlot::Mix, 1.0f);
    const std::array<double, 4> hz{196.0, 293.66, 440.0, 659.25};
    for (uint8_t L = 0; L < 4; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 1.0f);
        p.setBase(gg::core::layerSlot(gg::core::LayerParam::Source, L), 0.0f);
        p.setBase(gg::core::layerSlot(gg::core::LayerParam::Position, L), 0.5f);
        p.setBase(gg::core::layerSlot(gg::core::LayerParam::Density, L), 6.0f);
        p.setBase(gg::core::layerSlot(gg::core::LayerParam::Size, L), -3.0f);
        p.setBase(gg::core::levelSlot(L), 12.0f);
        p.setSourceNow(L, sine(hz[L]));
    }
}

float largestStep(const std::vector<float> &v, int64_t from, int64_t to)
{
    float step = 0.0f;
    for (int64_t i = from + 1; i < to; ++i)
    {
        step = std::max(step, std::abs(v[static_cast<size_t>(i)] - v[static_cast<size_t>(i - 1)]));
    }
    return step;
}
} // namespace

TEST_CASE("the tail at rest is the wave-1 bytes", "[core][fx]")
{
    // Amount 0 and Mode Off: the stages are the identity, whatever the other six globals say.
    // (The goldens' WAV hashes prove the M1 bytes themselves.)
    constexpr int64_t kFrames = 48000;
    const Stereo input = noise(kFrames, 0.5f, 5);
    const auto renderWith = [&](float time, float feedback, float damping, float pingPong,
                                float ceiling, float release) {
        Processor p({kRate, 4096, 21, Mode::Fx});
        p.setBase(ParamSlot::Mix, 0.6f);
        p.setBase(ParamSlot::ReverbAmount, 0.3f);
        p.setBase(ParamSlot::DelayTime, time);
        p.setBase(ParamSlot::DelayFeedback, feedback);
        p.setBase(ParamSlot::DelayDamping, damping);
        p.setBase(ParamSlot::DelayPingPong, pingPong);
        p.setBase(ParamSlot::LimiterCeiling, ceiling);
        p.setBase(ParamSlot::LimiterRelease, release);
        Stereo out = silence(kFrames);
        render(p, &input, 0, kFrames, {512}, {}, out);
        return out;
    };
    const Stereo a = renderWith(-1.4f, 0.4f, 0.3f, 0.0f, -1.0f, -3.3f);
    const Stereo b = renderWith(0.5f, 0.95f, 1.0f, 1.0f, -20.0f, 0.0f);
    REQUIRE(peak(a.left) > 0.05f);
    CHECK(sameBits(a.left, b.left));
    CHECK(sameBits(a.right, b.right));

    // An all-off instrument is +0, not -0, with the delay's time gliding about.
    Processor q({kRate, 4096, 22, Mode::Instrument});
    q.setBase(ParamSlot::LayerAOn, 0.0f);
    std::vector<TimedEvent> moves;
    for (int64_t t = 100; t < 4000; t += 97)
    {
        moves.push_back({t, ParamSlot::DelayTime, false, t % 2 == 0 ? -8.0 : 0.5});
    }
    Stereo z = silence(4800);
    render(q, nullptr, 0, 4800, {256}, moves, z);
    for (size_t i = 0; i < z.left.size(); ++i)
    {
        REQUIRE(std::bit_cast<uint32_t>(z.left[i]) == 0u);
        REQUIRE(std::bit_cast<uint32_t>(z.right[i]) == 0u);
    }
}

TEST_CASE("delay amount reveals the echoes of what already played", "[core][fx]")
{
    // Layer A plays over a sine, then is switched off at kOff: the wet side is silent from
    // kOff + 480 on. A delay of 100 ms with the amount at 0 writes its line all the while; the
    // amount raised at kRaise (> 100 ms after the sound stopped) is heard at once, as echoes of
    // sound that played before.
    constexpr int64_t kOff = 12000, kRaise = 14400, kFrames = 24000;
    const auto renderWith = [&](bool raise) {
        Processor p({kRate, 4096, 23, Mode::Fx});
        p.setBase(ParamSlot::Mix, 1.0f);
        p.setBase(ParamSlot::Source, 0.0f);
        p.setBase(ParamSlot::Position, 0.5f);
        p.setBase(ParamSlot::Density, 6.0f);
        p.setBase(ParamSlot::Level, 6.0f);
        p.setBase(ParamSlot::DelayTime, -3.3219281f); // 100 ms
        p.setBase(ParamSlot::DelayFeedback, 0.0f);
        p.setSourceNow(0, sine(330.0));
        std::vector<TimedEvent> events{{kOff, ParamSlot::LayerAOn, false, 0.0}};
        if (raise)
        {
            events.push_back({kRaise, ParamSlot::DelayAmount, false, 0.9});
        }
        Stereo out = silence(kFrames);
        render(p, nullptr, 0, kFrames, {4096}, events, out);
        return out;
    };
    const Stereo off = renderWith(false);
    const Stereo on = renderWith(true);
    CHECK(peakIn(off.left, 0, kOff) > 0.05f);
    // Before the raise the bits are the same; after the raise the amount-0 render is silent and
    // the other plays the echoes.
    for (int64_t i = 0; i < kRaise; ++i)
    {
        REQUIRE(off.left[static_cast<size_t>(i)] == on.left[static_cast<size_t>(i)]);
    }
    CHECK(peakIn(off.left, kOff + 480, kFrames) == 0.0f);
    CHECK(peakIn(on.left, kRaise + 480, kRaise + 2400) > 0.02f);
    CHECK(allFinite(on.left));
}

TEST_CASE("delay time automated every cell stays bounded", "[core][fx]")
{
    // Review Focus 3. Delay Time between 1 ms and 2 s every 32 samples for 2 s, at Feedback 0.95
    // and Amount 1, in ping-pong: finite and bounded (four layers below 1 each, plus the line's
    // loop, which the soft limiter holds below 1/(1 - feedback)).
    Processor p({kRate, 4096, 24, Mode::Fx});
    hotLayers(p);
    p.setBase(ParamSlot::DelayAmount, 1.0f);
    p.setBase(ParamSlot::DelayFeedback, 0.95f);
    p.setBase(ParamSlot::DelayPingPong, 1.0f);
    std::vector<TimedEvent> events;
    for (int64_t t = 0, k = 0; t < kInvarianceFrames; t += 32, ++k)
    {
        events.push_back({t, ParamSlot::DelayTime, false, k % 2 == 0 ? -9.9657843 : 1.0});
    }
    const Stereo input = noise(kInvarianceFrames, 0.5f, 6);
    Stereo out = silence(kInvarianceFrames);
    render(p, &input, 0, kInvarianceFrames, {1000}, events, out);
    CHECK(allFinite(out.left));
    CHECK(allFinite(out.right));
    CHECK(peak(out.left) < 4.0f / (1.0f - 0.95f) + 4.0f);
    CHECK(peak(out.left) > 0.1f);

    // The read itself: its delay moves at most 0.5 samples a sample, whatever the target does.
    gg::core::Delay delay;
    delay.prepare(kRate);
    delay.setTimeSamples(48.0);
    float l = 0.0f, r = 0.0f;
    delay.process(l, r, 0.0f, 0.0f, 1.0f, 0.0f);
    double last = delay.timeSamples();
    for (int64_t i = 0; i < 4000; ++i)
    {
        delay.setTimeSamples(i / 32 % 2 == 0 ? 48.0 : 96000.0);
        l = r = 0.25f;
        delay.process(l, r, 1.0f, 0.5f, 0.5f, 0.0f);
        const double now = delay.timeSamples();
        REQUIRE(std::abs(now - last) <= 0.5);
        last = now;
    }
}

TEST_CASE("the delay's fractional read moves without a step", "[core][fx]")
{
    // A 440 Hz sine through the delay at Amount 1 and Feedback 0: the echo is the line read at a
    // fractional delay that glides from 100 to 400 samples. The echo (output minus input) has a
    // slope of at most (1 + 0.5) times the sine's, so no step: a jump of the read would be a step
    // of about the amplitude.
    gg::core::Delay delay;
    delay.prepare(kRate);
    delay.setTimeSamples(100.0);
    constexpr int64_t kFrames = 4000;
    std::vector<float> echo(kFrames);
    for (int64_t i = 0; i < kFrames; ++i)
    {
        if (i == 800)
        {
            delay.setTimeSamples(400.0);
        }
        const float x = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 *
                                                           static_cast<double>(i) / kRate));
        float l = x, r = x;
        delay.process(l, r, 1.0f, 0.0f, 1.0f, 0.0f);
        echo[static_cast<size_t>(i)] = l - x;
    }
    CHECK(delay.timeSamples() == 400.0);
    const double slope = 0.5 * 2.0 * std::numbers::pi * 440.0 / kRate;
    CHECK(largestStep(echo, 500, kFrames) < static_cast<float>(1.5 * slope * 1.05));
    CHECK(peakIn(echo, 1000, kFrames) > 0.4f);
}

TEST_CASE("a ping-pong switch does not click", "[core][fx]")
{
    // Four layers over sines at Stereo 1, so the wet sum's two channels differ, into a 250 ms
    // delay at feedback 0.5 and Damping 0.3. Ping-pong goes on, then off: the damper's input
    // crosses between the channels' reads over the 10 ms ramp, so the echo of each switch (one
    // Delay Time later, where the line plays what the damper wrote) is no rougher than the
    // steady sound around it. A hard switch (delay_tests' negative control) steps there.
    constexpr int64_t kFrames = 144000, kTime = 12000, kRamp = 480;
    const auto renderWith = [&](bool toggle) {
        Processor p({kRate, 4096, 31, Mode::Fx});
        hotLayers(p);
        for (uint8_t L = 0; L < 4; ++L)
        {
            p.setBase(gg::core::levelSlot(L), 0.0f);
            p.setBase(gg::core::layerSlot(gg::core::LayerParam::Stereo, L), 1.0f);
        }
        p.setBase(ParamSlot::DelayAmount, 0.5f);
        p.setBase(ParamSlot::DelayTime, -2.0f); // 250 ms
        p.setBase(ParamSlot::DelayFeedback, 0.5f);
        p.setBase(ParamSlot::DelayDamping, 0.3f);
        std::vector<TimedEvent> events;
        if (toggle)
        {
            events = {{48001, ParamSlot::DelayPingPong, false, 1.0},
                      {96001, ParamSlot::DelayPingPong, false, 0.0}};
        }
        Stereo out = silence(kFrames);
        render(p, nullptr, 0, kFrames, {512}, events, out);
        return out;
    };
    const auto roughest = [](const Stereo &s, int64_t from, int64_t to) {
        float worst = 0.0f;
        for (const std::vector<float> *v : {&s.left, &s.right})
        {
            for (int64_t i = from + 2; i < to; ++i)
            {
                const auto j = static_cast<size_t>(i);
                worst = std::max(worst, std::abs((*v)[j] - 2.0f * (*v)[j - 1] + (*v)[j - 2]));
            }
        }
        return worst;
    };
    const Stereo out = renderWith(true);
    const Stereo still = renderWith(false);
    REQUIRE(peak(out.left) > 0.1f);
    REQUIRE_FALSE(sameBits(out.left, still.left)); // not vacuous: the switch moved the echoes
    for (const int64_t t : {int64_t{48001}, int64_t{96001}})
    {
        const int64_t echo = t + kTime;
        const float around = roughest(out, echo - 64, echo + kRamp + 64);
        const float before = roughest(out, echo - 4800, echo - 64);
        const float after = roughest(out, echo + 2 * kRamp, echo + 4800);
        INFO("switch at " << t);
        CAPTURE(around, before, after);
        CHECK(around <= 1.5f * std::max(before, after));
    }
}

TEST_CASE("soft limits four hot layers below the ceiling", "[core][fx]")
{
    constexpr int64_t kFrames = 48000;
    const float c = ceilingGain(-6.0f);
    const auto renderWith = [&](float mode) {
        Processor p({kRate, 4096, 25, Mode::Fx});
        hotLayers(p);
        p.setBase(ParamSlot::LimiterCeiling, -6.0f);
        p.setBase(ParamSlot::LimiterMode, mode);
        Stereo out = silence(kFrames);
        render(p, nullptr, 0, kFrames, {512}, {}, out);
        return out;
    };
    const Stereo off = renderWith(0.0f);
    const Stereo soft = renderWith(1.0f);
    REQUIRE(peak(off.left) > c);
    // Past the 10 ms fade the output is c × softLimit(x / c): below c.
    CHECK(peakIn(soft.left, 480, kFrames) < c);
    CHECK(peakIn(soft.right, 480, kFrames) < c);
    CHECK(peakIn(soft.left, 480, kFrames) > 0.3f * c);
}

TEST_CASE("peak holds the ceiling", "[core][fx]")
{
    constexpr int64_t kFrames = 48000;
    const float c = ceilingGain(-6.0f);
    Processor p({kRate, 4096, 26, Mode::Fx});
    hotLayers(p);
    p.setBase(ParamSlot::LimiterCeiling, -6.0f);
    p.setBase(ParamSlot::LimiterMode, 2.0f);
    Stereo out = silence(kFrames);
    render(p, nullptr, 0, kFrames, {512}, {}, out);
    CHECK(peakIn(out.left, 480, kFrames) <= c);
    CHECK(peakIn(out.right, 480, kFrames) <= c);
    CHECK(peakIn(out.left, 480, kFrames) > 0.5f * c);
}

TEST_CASE("a loaded Peak state is limited from its first sample", "[core][fx]")
{
    // The mode's own fade is 10 ms from Off: a state loaded (setBases) in Peak jumps to it
    // instead, so not one sample of the wet side passes the ceiling. The sound is already hot
    // when the state arrives (Off for half a second first), as a session's is.
    constexpr int64_t kWarm = 24000;
    constexpr int64_t kRampFrames = 480;
    const float c = ceilingGain(-24.0f);
    const auto firstTenMs = [&](bool load) {
        Processor p({kRate, 4096, 27, Mode::Fx});
        hotLayers(p);
        Stereo out = silence(kWarm + kRampFrames);
        render(p, nullptr, 0, kWarm, {256}, {}, out);
        ParamValues v{};
        for (size_t s = 0; s < gg::core::kParamCount; ++s)
        {
            v[s] = p.base(static_cast<ParamSlot>(s));
        }
        v[static_cast<size_t>(ParamSlot::LimiterMode)] = 2.0f;
        v[static_cast<size_t>(ParamSlot::LimiterCeiling)] = -24.0f;
        std::vector<TimedEvent> events;
        if (load)
        {
            p.setBases(v);
        }
        else
        {
            // The same state reached through process()'s events fades in.
            p.setBase(ParamSlot::LimiterCeiling, -24.0f);
            events.push_back({kWarm, ParamSlot::LimiterMode, false, 2.0});
        }
        render(p, nullptr, kWarm, kWarm + kRampFrames, {256}, events, out);
        return std::max(peakIn(out.left, kWarm, kWarm + kRampFrames),
                        peakIn(out.right, kWarm, kWarm + kRampFrames));
    };
    CHECK(firstTenMs(true) <= c);
    // The same state reached by an event fades in, and passes the ceiling meanwhile.
    CHECK(firstTenMs(false) > c);
}

TEST_CASE("a loaded state's filter, ping-pong and limiter apply from its first sample",
          "[core][fx][buses]")
{
    // The rule that every new unit snaps where the ramps jump (the extremes' final review): a
    // fresh instance given a state (setBases) with layer A's Bus 1 Filter a 300 Hz low-pass,
    // Delay Ping-pong on (a 2 ms delay at feedback 0.5, so its echoes fall inside the first
    // 10 ms) and the limiter in Peak at -30 dB renders, from its first sample, the three units
    // settled. The reference applies the units themselves, settled from the start, to the same
    // render with the three at rest (filter Off, Amount 0, Mode Off: the identity, so it is the
    // layer's own wet). The same values reached through process()'s events, which fade and ramp,
    // don't (the negative control; the filter's own fade is the snap's RED run).
    constexpr int64_t kFrames = 2400;
    constexpr int64_t kTen = 480;
    constexpr float kCutoffLog2 = 8.2288187f; // 300 Hz
    constexpr float kTimeLog2 = -8.9657843f;  // 2 ms
    const auto setUp = [](Processor &p) {
        p.setBase(ParamSlot::Mix, 1.0f);
        p.setBase(ParamSlot::Source, 0.0f);
        p.setBase(ParamSlot::Position, 0.5f);
        p.setBase(ParamSlot::Density, 6.0f);
        p.setBase(ParamSlot::Size, -4.0f);
        p.setBase(ParamSlot::Stereo, 1.0f); // the channels differ, so ping-pong shows
        p.setBase(gg::core::layerSlot(gg::core::LayerParam::Bus1Cutoff, 0), kCutoffLog2);
        p.setBase(ParamSlot::DelayTime, kTimeLog2);
        p.setBase(ParamSlot::DelayFeedback, 0.5f);
        p.setBase(ParamSlot::DelayDamping, 0.3f);
        p.setBase(ParamSlot::LimiterCeiling, -30.0f);
        p.setSourceNow(0, sine(659.25));
    };
    const auto withState = [](Processor &p) {
        ParamValues v{};
        for (size_t s = 0; s < gg::core::kParamCount; ++s)
        {
            v[s] = p.base(static_cast<ParamSlot>(s));
        }
        v[static_cast<size_t>(gg::core::layerSlot(gg::core::LayerParam::Bus1Filter, 0))] = 1.0f;
        v[static_cast<size_t>(ParamSlot::DelayAmount)] = 1.0f;
        v[static_cast<size_t>(ParamSlot::DelayPingPong)] = 1.0f;
        v[static_cast<size_t>(ParamSlot::LimiterMode)] = 2.0f;
        return v;
    };

    Stereo raw = silence(kFrames);
    {
        Processor p({kRate, 4096, 33, Mode::Fx});
        setUp(p);
        render(p, nullptr, 0, kFrames, {256}, {}, raw);
    }
    REQUIRE(peakIn(raw.left, 0, kTen) > 0.05f);
    REQUIRE_FALSE(sameBits(raw.left, raw.right));

    // The three units settled from the start, over the raw render, in the Processor's order.
    Stereo expected = raw;
    {
        Processor p({kRate, 4096, 33, Mode::Fx});
        setUp(p);
        const ParamValues v = withState(p);
        const gg::core::BusParams bp = gg::core::busParams(v, 0);
        const gg::core::DelayParams dp = gg::core::delayParams(v);
        const gg::core::LimiterParams lp = gg::core::limiterParams(v);
        gg::core::fx::BusFilter filter;
        filter.prepare(kRate, static_cast<uint32_t>(kTen));
        filter.jump(bp.type[0]);
        gg::core::Delay delay;
        delay.prepare(kRate);
        delay.setTimeSamples(static_cast<double>(dp.timeSeconds) * kRate);
        gg::core::Limiter limiter;
        limiter.prepare(kRate, static_cast<uint32_t>(kTen));
        limiter.jump(lp.mode);
        const float damp = gg::core::dampCoefficient(dp.damping, kRate);
        const float release = gg::core::releaseCoefficient(lp.releaseSeconds, kRate);
        for (int64_t i = 0; i < kFrames; ++i)
        {
            const auto j = static_cast<size_t>(i);
            if (i % 32 == 0)
            {
                filter.setCell(bp.type[0], bp.cutoffHz[0], bp.resonance[0]);
            }
            float l = expected.left[j], r = expected.right[j];
            filter.process(l, r);
            delay.process(l, r, dp.amount, dp.feedback, damp, 1.0f);
            limiter.process(l, r, lp.mode, lp.ceilingGain, release);
            expected.left[j] = l;
            expected.right[j] = r;
        }
    }

    const auto largestDifference = [&](const Stereo &a) {
        float worst = 0.0f;
        for (size_t j = 0; j < static_cast<size_t>(kTen); ++j)
        {
            worst = std::max({worst, std::abs(a.left[j] - expected.left[j]),
                              std::abs(a.right[j] - expected.right[j])});
        }
        return worst;
    };
    const float c = ceilingGain(-30.0f);

    Stereo loaded = silence(kFrames);
    {
        Processor p({kRate, 4096, 33, Mode::Fx});
        setUp(p);
        p.setBases(withState(p));
        render(p, nullptr, 0, kFrames, {256}, {}, loaded);
    }
    Stereo evented = silence(kFrames);
    {
        Processor p({kRate, 4096, 33, Mode::Fx});
        setUp(p);
        p.setBase(ParamSlot::DelayAmount, 1.0f);
        const std::vector<TimedEvent> events{{0, ParamSlot::DelayPingPong, false, 1.0},
                                             {0, ParamSlot::LimiterMode, false, 2.0}};
        render(p, nullptr, 0, kFrames, {256}, events, evented);
    }
    const float loadedError = largestDifference(loaded);
    const float eventedError = largestDifference(evented);
    CAPTURE(loadedError, eventedError);
    CHECK(loadedError <= 1e-6f);
    CHECK(std::max(peakIn(loaded.left, 0, kTen), peakIn(loaded.right, 0, kTen)) <= c);
    CHECK(eventedError > 100.0f * std::max(loadedError, 1e-7f));
}

TEST_CASE("a route that moves the limiter's mode snaps it", "[core][fx]")
{
    // setRoutes (a loaded state's matrix) and a macro's setBase jump every ramped value they
    // move, and the limiter's mode with them: a route from Macro 1 to Limiter Mode, given while
    // the sound is hot, limits the very first sample. The same mode change through process()'s
    // events fades in over 10 ms (the negative control).
    constexpr int64_t kWarm = 24000;
    constexpr int64_t kRampFrames = 480;
    const float c = ceilingGain(-24.0f);
    enum class How
    {
        Routes,
        Macro,
        Event
    };
    const auto firstTenMs = [&](How how) {
        Processor p({kRate, 4096, 34, Mode::Fx});
        hotLayers(p);
        p.setBase(ParamSlot::LimiterCeiling, -24.0f);
        gg::core::ModRoutes routes = gg::core::factoryRoutes(Mode::Fx);
        routes[12] = route(gg::core::ModSource::Macro1, ParamSlot::LimiterMode,
                           gg::core::kAllLayers, 2.0f, gg::core::ModCurve::Unipolar);
        if (how == How::Macro)
        {
            p.setBase(ParamSlot::Macro1, 0.0f); // its default is 0.5, which would be Soft
            p.setRoutes(routes);                // Macro 1 at 0: the mode stays Off
        }
        Stereo out = silence(kWarm + kRampFrames);
        render(p, nullptr, 0, kWarm, {256}, {}, out);
        std::vector<TimedEvent> events;
        switch (how)
        {
        case How::Routes:
            p.setBase(ParamSlot::Macro1, 1.0f); // no route yet: moves nothing
            p.setRoutes(routes);
            break;
        case How::Macro:
            p.setBase(ParamSlot::Macro1, 1.0f);
            break;
        case How::Event:
            events.push_back({kWarm, ParamSlot::LimiterMode, false, 2.0});
            break;
        }
        render(p, nullptr, kWarm, kWarm + kRampFrames, {256}, events, out);
        return std::max(peakIn(out.left, kWarm, kWarm + kRampFrames),
                        peakIn(out.right, kWarm, kWarm + kRampFrames));
    };
    CHECK(firstTenMs(How::Routes) <= c);
    CHECK(firstTenMs(How::Macro) <= c);
    CHECK(firstTenMs(How::Event) > c);
}

TEST_CASE("reset leaves a Peak instance limited from the first sample", "[core][fx]")
{
    const float c = ceilingGain(-6.0f);
    Processor p({kRate, 4096, 28, Mode::Fx});
    hotLayers(p);
    ParamValues v{};
    for (size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        v[s] = p.base(static_cast<ParamSlot>(s));
    }
    v[static_cast<size_t>(ParamSlot::LimiterMode)] = 2.0f;
    v[static_cast<size_t>(ParamSlot::LimiterCeiling)] = -6.0f;
    p.setBases(v);
    Stereo warm = silence(9600);
    render(p, nullptr, 0, 9600, {256}, {}, warm);
    p.reset();
    Stereo out = silence(4800);
    render(p, nullptr, 0, 4800, {256}, {}, out);
    CHECK(peak(out.left) <= c);
    CHECK(peak(out.left) > 0.1f * c);
}

TEST_CASE("a limiter mode change does not click", "[core][fx]")
{
    // Off to Soft to Peak to Off at ceiling -12 dB over a hot, steady sound. Each change is a
    // 10 ms crossfade, so the largest step of the output around it is no more than the largest
    // step either mode makes on its own a little way off (Peak's instant gain has kinks of its
    // own, so a click score of the whole render would measure it, not the change), and the
    // Off to Soft and Soft to Off round trip scores under the goldens' limit.
    constexpr int64_t kFrames = 96000;
    constexpr int64_t kFade = 480;
    Processor p({kRate, 4096, 29, Mode::Fx});
    hotLayers(p);
    for (uint8_t L = 0; L < 4; ++L)
    {
        p.setBase(gg::core::levelSlot(L), 0.0f);
    }
    p.setBase(ParamSlot::LimiterCeiling, -12.0f);
    const std::array<int64_t, 3> changes{24001, 48001, 72001};
    const std::vector<TimedEvent> events{{changes[0], ParamSlot::LimiterMode, false, 1.0},
                                         {changes[1], ParamSlot::LimiterMode, false, 2.0},
                                         {changes[2], ParamSlot::LimiterMode, false, 0.0}};
    Stereo out = silence(kFrames);
    render(p, nullptr, 0, kFrames, {512}, events, out);
    REQUIRE(peak(out.left) > 0.2f);
    for (const int64_t t : changes)
    {
        const float around = std::max(largestStep(out.left, t - 1, t + kFade + 1),
                                      largestStep(out.right, t - 1, t + kFade + 1));
        const float before = std::max(largestStep(out.left, t - 4800, t - 1),
                                      largestStep(out.right, t - 4800, t - 1));
        const float after = std::max(largestStep(out.left, t + 2 * kFade, t + 4800),
                                     largestStep(out.right, t + 2 * kFade, t + 4800));
        INFO("change at " << t);
        CHECK(around <= 1.5f * std::max(before, after));
    }
    Stereo trip = silence(48000);
    {
        Processor q({kRate, 4096, 29, Mode::Fx});
        hotLayers(q);
        q.setBase(ParamSlot::LimiterCeiling, -12.0f);
        const std::vector<TimedEvent> soft{{12001, ParamSlot::LimiterMode, false, 1.0},
                                           {30001, ParamSlot::LimiterMode, false, 0.0}};
        render(q, nullptr, 0, 48000, {512}, soft, trip);
    }
    const gg::metrics::AudioData audio{kRate, {trip.left, trip.right}};
    CHECK(gg::metrics::computeMetrics(audio).clickScore < 15.0);
}

TEST_CASE("the delay and limiter do not allocate", "[core][fx][rt]")
{
    // Every delay and limiter parameter moved in each product, with ping-pong and each mode, in
    // 0-, 1-, odd and oversized calls, all armed.
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        Processor p({kRate, 256, 30, mode});
        hotLayers(p);
        p.setBase(ParamSlot::DelayAmount, 0.5f);
        p.setBase(ParamSlot::DelayFeedback, 0.8f);
        constexpr uint32_t kFrames = 1000;
        const Stereo input = noise(kFrames, 0.5f, 7);
        const float *in[2] = {input.left.data(), input.right.data()};
        std::vector<float> o0(kFrames), o1(kFrames);
        float *out[2] = {o0.data(), o1.data()};
        std::vector<ParamEvent> events;
        for (uint32_t k = 0; k < 40; ++k)
        {
            const uint32_t at = 25 * k;
            events.push_back({at, ParamSlot::DelayTime, k % 2 == 0, k % 3 == 0 ? -9.9 : 0.8});
            events.push_back({at, ParamSlot::DelayAmount, false, 0.1 * (k % 10)});
            events.push_back({at, ParamSlot::DelayFeedback, false, 0.1 * (k % 10)});
            events.push_back({at, ParamSlot::DelayDamping, false, 0.1 * (k % 10)});
            events.push_back({at, ParamSlot::DelayPingPong, false, k % 2 == 0 ? 1.0 : 0.0});
            events.push_back({at, ParamSlot::LimiterMode, false, static_cast<double>(k % 3)});
            events.push_back({at, ParamSlot::LimiterCeiling, false, -24.0 + k % 25});
            events.push_back({at, ParamSlot::LimiterRelease, false, -6.0 + 0.15 * (k % 40)});
        }
        const std::array<ParamEvent, 1> none{{{0, ParamSlot::LimiterMode, false, 2.0}}};
        AllocTripwire::resetCount();
        {
            AllocTripwire::Scope armed;
            p.process(in, 2, out, 2, kFrames, events);
            for (int i = 0; i < 10; ++i)
            {
                p.process(in, 2, out, 2, 0, none);
                p.process(in, 2, out, 2, 1, none);
                p.process(in, 2, out, 1, 77, events);
                p.process(in, 2, out, 2, kFrames, events);
            }
        }
        const uint64_t allocations = AllocTripwire::count();
        REQUIRE(allFinite(o0));
        if (AllocTripwire::available())
        {
            REQUIRE(allocations == 0);
        }
    }
}
