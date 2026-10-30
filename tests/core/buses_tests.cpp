// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Each layer's two grain buses and their filters (M2 sub-project 4's spec §6.3): the one-bus
// bytes at the defaults, bus 2 joining the sum, the four filter types, the glide of cutoff and
// resonance, the crossfade of a type change, stability at full resonance, and the real-time
// contract with split buses and a type change every cell.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/LayersFixture.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Processor.h"
#include "gg/core/Rng.h"
#include "gg/metrics/Metrics.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/SampleBuffer.h"
#include "support/AllocTripwire.h"

using gg::core::BusFilterType;
using gg::core::LayerParam;
using gg::core::layerSlot;
using gg::core::Mode;
using gg::core::ParamEvent;
using gg::core::ParamSlot;
using gg::core::Processor;
using gg::test::AllocTripwire;
using namespace gg::test::layers;

namespace
{

ParamSlot A(LayerParam p) { return layerSlot(p, 0); }

float log2Hz(double hz) { return static_cast<float>(std::log2(hz)); }

// A 1 s stereo noise sample in ±0.5, from gg::rng.
std::shared_ptr<const gg::PreparedSource> noiseSample()
{
    constexpr int64_t kFrames = 48000;
    auto buf = std::make_shared<gg::SampleBuffer>(2, kFrames, kRate);
    for (uint32_t c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < kFrames; ++i)
        {
            buf->channel(c)[i] =
                0.5f * static_cast<float>(gg::rng::bipolar(gg::rng::hash(
                           {77, 0, c, gg::rng::Field::SignalNoise, static_cast<uint64_t>(i)})));
        }
    }
    return gg::PreparedSource::make(std::move(buf));
}

// Layer A alone, Sample kind over `src`, 64 grains a second at the default size, Mix 1 (the
// output is the layer alone, the reverb idle).
struct Rig
{
    Processor p;
    Log log;
    explicit Rig(const std::shared_ptr<const gg::PreparedSource> &src, uint32_t maxBlock = 512)
        : p({kRate, maxBlock, 61, Mode::Fx})
    {
        p.setBase(ParamSlot::Mix, 1.0f);
        p.setBase(A(LayerParam::Source), 0.0f);
        p.setBase(A(LayerParam::Density), 6.0f);
        p.setSourceNow(0, src);
        p.setEventSink(&Log::record, &log);
    }
    void bus(int n, BusFilterType type, double hz, float resonance)
    {
        p.setBase(A(n == 1 ? LayerParam::Bus1Filter : LayerParam::Bus2Filter),
                  static_cast<float>(type));
        p.setBase(A(n == 1 ? LayerParam::Bus1Cutoff : LayerParam::Bus2Cutoff), log2Hz(hz));
        p.setBase(A(n == 1 ? LayerParam::Bus1Resonance : LayerParam::Bus2Resonance), resonance);
    }
    Stereo run(int64_t frames, const std::vector<TimedEvent> &events = {})
    {
        Stereo out = silence(frames);
        render(p, nullptr, 0, frames, {512}, events, out);
        return out;
    }
};

// The power of `x` over [from, to) at `hz` (Goertzel), as a mean square.
double powerAt(const std::vector<float> &x, int64_t from, int64_t to, double hz)
{
    const double w = 2.0 * std::numbers::pi * hz / kRate;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    for (int64_t i = from; i < to; ++i)
    {
        const double s = x[static_cast<std::size_t>(i)] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double re = s1 - s2 * std::cos(w);
    const double im = s2 * std::sin(w);
    const double n = static_cast<double>(to - from);
    return (re * re + im * im) / (n * n);
}

double bandPower(const std::vector<float> &x, int64_t from, int64_t to,
                 const std::vector<double> &hz)
{
    double sum = 0.0;
    for (const double f : hz)
    {
        sum += powerAt(x, from, to, f);
    }
    return sum;
}

double dB(double ratio) { return 10.0 * std::log10(ratio); }

float clickScore(const Stereo &s)
{
    const gg::metrics::AudioData audio{kRate, {s.left, s.right}};
    return static_cast<float>(gg::metrics::computeMetrics(audio).clickScore);
}

constexpr float kClickLimit = 15.0f;
} // namespace

TEST_CASE("split 0 and filters off are the one-bus bytes", "[core][buses]")
{
    // The four-layer fixture render, before and after setting Bus Split 0 and the types Off
    // explicitly (with cutoffs and resonances that would matter were a filter on): the same bytes.
    // The goldens' hashes cover the older renders.
    const Stereo input = noise(kInvarianceFrames, 0.5f, 49);
    LayersRender plain;
    renderLayers(input, {}, 4096, {512}, plain);
    REQUIRE(peak(plain.audio.left) > 0.05f);

    LayersSetup setup;
    setup.configure = [](Processor &p) {
        for (uint8_t L = 0; L < 4; ++L)
        {
            p.setBase(layerSlot(LayerParam::BusSplit, L), 0.0f);
            for (const LayerParam f : {LayerParam::Bus1Filter, LayerParam::Bus2Filter})
            {
                p.setBase(layerSlot(f, L), static_cast<float>(BusFilterType::Off));
            }
            p.setBase(layerSlot(LayerParam::Bus1Cutoff, L), log2Hz(300.0));
            p.setBase(layerSlot(LayerParam::Bus2Cutoff, L), log2Hz(3000.0));
            p.setBase(layerSlot(LayerParam::Bus1Resonance, L), 0.9f);
            p.setBase(layerSlot(LayerParam::Bus2Resonance, L), 0.9f);
        }
    };
    LayersRender explicitOff;
    renderLayers(input, {}, 4096, {512}, explicitOff, setup);
    REQUIRE(sameBits(plain.audio.left, explicitOff.audio.left));
    REQUIRE(sameBits(plain.audio.right, explicitOff.audio.right));
}

TEST_CASE("bus 2 joins the sum once a grain has been on it", "[core][buses]")
{
    // Split 0.5 for a second, then 0. Bus 1 is a high-pass at 8 kHz, which leaves almost
    // nothing of its grains; bus 2 is a resonant band-pass at the sample's 110 Hz,
    // which rings on after its last grain. The ring is in the output (the sum kept bus 2 after the
    // split fell to 0), and it is the filter's: with bus 2 a band-pass far from the sample, nothing
    // is left after the last grain.
    constexpr int64_t kSplitOff = 48000; // a grid boundary
    constexpr int64_t kFrames = 96000;
    const std::vector<TimedEvent> events{{kSplitOff, A(LayerParam::BusSplit), false, 0.0}};

    const auto tail = [&](double cutoff2, float resonance2, float &clicks) {
        Rig rig(sine(110.0));
        rig.p.setBase(A(LayerParam::BusSplit), 0.5f);
        rig.bus(1, BusFilterType::HighPass, 8000.0, 0.0f);
        rig.bus(2, BusFilterType::BandPass, cutoff2, resonance2);
        const Stereo out = rig.run(kFrames, events);
        REQUIRE(allFinite(out.left));
        int64_t lastEnd = 0;
        for (const Log::Event &e : rig.log.events)
        {
            if (e.grain.bus == 1)
            {
                lastEnd =
                    std::max(lastEnd, e.grain.onsetSample +
                                          static_cast<int64_t>(std::ceil(e.grain.durationSamples)));
            }
        }
        REQUIRE(lastEnd > 0);
        REQUIRE(lastEnd + 2400 < kFrames);
        clicks = clickScore(out);
        return peakIn(out.left, lastEnd, lastEnd + 2400);
    };
    float clicksRing = 0.0f;
    float clicksFar = 0.0f;
    const float ring = tail(110.0, 1.0f, clicksRing);
    const float none = tail(8000.0, 0.0f, clicksFar);
    INFO("ring " << ring << ", none " << none);
    CHECK(ring > 0.001f);
    CHECK(ring > 20.0f * none);
    CHECK(clicksRing < kClickLimit);
}

TEST_CASE("each type shapes its bus", "[core][buses]")
{
    constexpr int64_t kFrames = 72000;
    constexpr int64_t kFrom = 24000;
    const auto src = noiseSample();
    const auto render1 = [&](int bus, BusFilterType type, double hz, float res) {
        Rig rig(src);
        if (bus == 2)
        {
            rig.p.setBase(A(LayerParam::BusSplit), 1.0f);
        }
        rig.bus(bus, type, hz, res);
        const Stereo out = rig.run(kFrames);
        REQUIRE(allFinite(out.left));
        return out;
    };
    const auto power = [&](const Stereo &s, const std::vector<double> &hz) {
        return bandPower(s.left, kFrom, kFrames, hz);
    };
    const std::vector<double> high{2000, 3000, 4000, 6000, 8000, 12000};

    for (const int bus : {1, 2})
    {
        INFO("bus " << bus);
        const Stereo off = render1(bus, BusFilterType::Off, 1000.0, 0.0f);
        REQUIRE(peak(off.left) > 0.05f);

        {
            const Stereo lp = render1(bus, BusFilterType::LowPass, 200.0, 0.0f);
            const double down = dB(power(off, high) / power(lp, high));
            INFO("low-pass: above 2 kHz down " << down << " dB");
            CHECK(down >= 30.0);
            CHECK(dB(power(lp, {100}) / power(off, {100})) > -3.0); // the passband stays
        }
        {
            const Stereo hp = render1(bus, BusFilterType::HighPass, 5000.0, 0.0f);
            const double down = dB(power(off, {300, 500, 700}) / power(hp, {300, 500, 700}));
            INFO("high-pass: 500 Hz down " << down << " dB");
            CHECK(down >= 30.0);
            CHECK(std::abs(dB(power(hp, {15000}) / power(off, {15000}))) < 3.0);
        }
        {
            const Stereo bp = render1(bus, BusFilterType::BandPass, 2000.0, 0.8f);
            const double low = dB(power(off, {200}) / power(bp, {200}));
            const double top = dB(power(off, {12000}) / power(bp, {12000}));
            INFO("band-pass: 200 Hz down " << low << " dB, 12 kHz down " << top << " dB");
            CHECK(low >= 15.0);
            CHECK(top >= 10.0);
            CHECK(dB(power(off, {2000}) / power(bp, {2000})) < 10.0); // the band stays
        }
        {
            const Stereo notch = render1(bus, BusFilterType::Notch, 1000.0, 0.9f);
            const double at = dB(power(off, {980, 1000, 1020}) / power(notch, {980, 1000, 1020}));
            INFO("notch: 1 kHz down " << at << " dB");
            CHECK(at >= 12.0);
            CHECK(std::abs(dB(power(notch, {5000}) / power(off, {5000}))) < 3.0);
        }
    }
}

TEST_CASE("cutoff and resonance move without a zipper", "[core][buses]")
{
    // A 440 Hz sine cloud through a low-pass whose cutoff sweeps 100 Hz to 8 kHz and whose
    // resonance sweeps 0 to 60% over 2 s, a new value every grid cell (the finest a host's
    // automation can ask for): the coefficients glide across each cell.
    constexpr int64_t kFrames = 96000;
    Rig rig(sine(440.0));
    rig.bus(1, BusFilterType::LowPass, 100.0, 0.0f);
    std::vector<TimedEvent> events;
    for (int64_t t = 32; t < kFrames; t += 32)
    {
        const double u = static_cast<double>(t) / kFrames;
        events.push_back({t, A(LayerParam::Bus1Cutoff), false, std::log2(100.0) + u * 6.32});
        events.push_back({t, A(LayerParam::Bus1Resonance), false, 0.6 * u});
    }
    const Stereo out = rig.run(kFrames, events);
    REQUIRE(allFinite(out.left));
    REQUIRE(peak(out.left) > 0.05f);
    CHECK(clickScore(out) < kClickLimit);
}

TEST_CASE("a type change crossfades", "[core][buses]")
{
    // Off, then Low-pass, Band-pass and Off again, at times off the grid, over a sine cloud the
    // filters change by several dB: each switch is a 10 ms crossfade, not a step.
    constexpr int64_t kFrames = 96000;
    Rig rig(sine(440.0));
    rig.bus(1, BusFilterType::Off, 300.0, 0.5f);
    const auto type = [](int64_t t, BusFilterType f) {
        return TimedEvent{t, A(LayerParam::Bus1Filter), false, static_cast<double>(f)};
    };
    const std::vector<TimedEvent> events{type(24017, BusFilterType::LowPass),
                                         type(48029, BusFilterType::BandPass),
                                         type(72041, BusFilterType::Off)};
    const Stereo out = rig.run(kFrames, events);
    REQUIRE(allFinite(out.left));
    REQUIRE(peak(out.left) > 0.05f);
    CHECK(clickScore(out) < kClickLimit);

    // And the control: the switch is audible (the level changes), so the check can fail.
    Rig on(sine(440.0));
    on.bus(1, BusFilterType::LowPass, 300.0, 0.5f);
    const Stereo filtered = on.run(kFrames);
    Rig off(sine(440.0));
    const Stereo plain = off.run(kFrames);
    CHECK(peakIn(filtered.left, 12000, 24000) < 0.8f * peakIn(plain.left, 12000, 24000));
}

TEST_CASE("resonance 100 percent is stable", "[core][buses]")
{
    constexpr int64_t kFrames = 10 * 48000;
    const auto src = noiseSample();
    for (const BusFilterType type : {BusFilterType::LowPass, BusFilterType::HighPass,
                                     BusFilterType::BandPass, BusFilterType::Notch})
    {
        INFO("type " << static_cast<int>(type));
        Rig rig(src);
        rig.bus(1, type, 1500.0, 1.0f);
        const Stereo out = rig.run(kFrames);
        REQUIRE(allFinite(out.left));
        REQUIRE(allFinite(out.right));
        CHECK(peak(out.left) < 4.0f);
        CHECK(peak(out.right) < 4.0f);
        CHECK(peak(out.left) > 0.01f);
    }
}

TEST_CASE("split buses with a type change every cell do not allocate", "[core][buses][rt]")
{
    // Four layers, every bus split and filtered, and every cell of a 4000-frame call a new type,
    // cutoff and resonance on each bus, in calls of every shape.
    Processor p({kRate, 256, 63, Mode::Fx});
    for (uint8_t L = 0; L < 4; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 1.0f);
        p.setBase(layerSlot(LayerParam::Density, L), 7.0f);
        p.setBase(layerSlot(LayerParam::BusSplit, L), 0.5f);
        p.setSourceNow(L, sine(220.0 * (L + 1)));
    }
    p.setBase(ParamSlot::Mix, 1.0f);
    constexpr uint32_t kFrames = 4000;
    const Stereo input = noise(kFrames, 0.5f, 65);
    const float *in[2] = {input.left.data(), input.right.data()};
    std::vector<float> o0(kFrames), o1(kFrames);
    float *out[2] = {o0.data(), o1.data()};

    std::vector<ParamEvent> events;
    for (uint32_t t = 32; t < kFrames; t += 32)
    {
        for (uint8_t L = 0; L < 4; ++L)
        {
            const uint32_t k = t / 32 + L;
            events.push_back({t, layerSlot(LayerParam::Bus1Filter, L), false, double(k % 5)});
            events.push_back({t, layerSlot(LayerParam::Bus2Filter, L), false, double((k + 2) % 5)});
            events.push_back({t, layerSlot(LayerParam::Bus1Cutoff, L), false, 5.0 + (k % 9)});
            events.push_back({t, layerSlot(LayerParam::Bus2Cutoff, L), false, 13.0 - (k % 9)});
            events.push_back({t, layerSlot(LayerParam::Bus1Resonance, L), false, (k % 11) / 10.0});
            events.push_back({t, layerSlot(LayerParam::Bus2Resonance, L), false, (k % 7) / 6.0});
        }
    }
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        p.process(in, 2, out, 2, kFrames, events);
        for (int i = 0; i < 10; ++i)
        {
            p.process(in, 2, out, 2, 0, {});
            p.process(in, 2, out, 2, 1, {});
            p.process(in, 1, out, 1, 100, {});
            p.process(nullptr, 0, out, 2, 33, {});
        }
    }
    const uint64_t allocations = AllocTripwire::count();
    if (AllocTripwire::available())
    {
        REQUIRE(allocations == 0);
    }
    REQUIRE(allFinite(o0));
}
