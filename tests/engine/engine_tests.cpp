// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Rng.h"
#include "gg/engine/Engine.h"

namespace
{
// A sine at `hz` and amplitude `amp` in every channel (channel c is phase-shifted by c/4 turn).
std::shared_ptr<gg::SampleBuffer> makeSine(uint32_t channels, int64_t frames, double sampleRate,
                                           double hz = 440.0, double amp = 1.0)
{
    auto buf = std::make_shared<gg::SampleBuffer>(channels, frames, sampleRate);
    for (uint32_t c = 0; c < channels; ++c)
    {
        for (int64_t i = 0; i < frames; ++i)
        {
            const double phase = 2.0 * std::numbers::pi * (hz * i / sampleRate + 0.25 * c);
            buf->channel(c)[i] = static_cast<float>(amp * std::sin(phase));
        }
    }
    return buf;
}

// Two output channels rendered by repeated process() calls of the given sizes.
struct Render
{
    std::vector<float> left;
    std::vector<float> right;
};

constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();

// The buffers start as NaN, so a sample process() didn't write fails allFinite(), and two renders
// that both wrote nothing can't pass sameBits() unnoticed.
Render render(gg::Engine &engine, int64_t frames, const std::vector<uint32_t> &blockSizes)
{
    Render r{std::vector<float>(static_cast<std::size_t>(frames), kUnwritten),
             std::vector<float>(static_cast<std::size_t>(frames), kUnwritten)};
    int64_t done = 0;
    std::size_t next = 0;
    while (done < frames)
    {
        const uint32_t want = blockSizes[next++ % blockSizes.size()];
        const auto n = static_cast<uint32_t>(std::min<int64_t>(want, frames - done));
        float *out[2] = {r.left.data() + done, r.right.data() + done};
        engine.process(out, 2, n);
        done += n;
    }
    return r;
}

Render render(gg::Engine &engine, int64_t frames, uint32_t blockSize)
{
    return render(engine, frames, std::vector<uint32_t>{blockSize});
}

bool allFinite(const std::vector<float> &v)
{
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

float peak(const std::vector<float> &v)
{
    float p = 0.0f;
    for (const float x : v)
    {
        p = std::max(p, std::abs(x));
    }
    return p;
}

bool sameBits(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

// An event sink is called on the audio thread, so this one records into preallocated storage
// and never allocates (the RTSan build checks that).
struct SinkLog
{
    static constexpr std::size_t kCapacity = 8192;
    std::vector<gg::GrainSpec> grains;
    bool overflowed{false};

    SinkLog() { grains.reserve(kCapacity); }

    static void record(void *ctx, const gg::GrainSpec &g)
    {
        auto *log = static_cast<SinkLog *>(ctx);
        if (log->grains.size() < kCapacity)
        {
            log->grains.push_back(g);
        }
        else
        {
            log->overflowed = true;
        }
    }
};

gg::StreamParams params(double rateHz, double durationSec, double position, double jitter,
                        double pitch, float gain)
{
    return {rateHz, durationSec, position, jitter, pitch, gain};
}
} // namespace

TEST_CASE("no source renders silence", "[engine]")
{
    gg::Engine engine(gg::EngineConfig{});
    REQUIRE(engine.sampleRate() == 48000.0);
    const auto r = render(engine, 48000, 512);
    REQUIRE(std::all_of(r.left.begin(), r.left.end(), [](float x) { return x == 0.0f; }));
    REQUIRE(std::all_of(r.right.begin(), r.right.end(), [](float x) { return x == 0.0f; }));
    REQUIRE(engine.sampleClock() == 48000);
    REQUIRE(engine.activeGrains() == 0);
}

TEST_CASE("grain stream renders non-silent bounded audio", "[engine]")
{
    gg::Engine engine(gg::EngineConfig{});
    engine.setSource(makeSine(1, 96000, 48000.0));
    const auto r = render(engine, 48000, 256);
    REQUIRE(allFinite(r.left));
    REQUIRE(allFinite(r.right));
    REQUIRE(peak(r.left) > 0.05f);
    REQUIRE(peak(r.left) <= 4.0f);
    REQUIRE(peak(r.right) <= 4.0f);
    REQUIRE(sameBits(r.left, r.right)); // a mono source feeds both channels
    REQUIRE(engine.activeGrains() > 0);
    REQUIRE(engine.droppedGrains() == 0);
}

TEST_CASE("extreme and non-finite params stay finite", "[engine]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const auto source = makeSine(2, 48000, 44100.0);
    const gg::StreamParams defaults{};

    // 1/48000 s asks for a 1-sample grain, which the minimum window length overrides.
    for (const double v : {nan, inf, -inf, -1.0, 0.0, 1e9, 1.0 / 48000.0})
    {
        const auto f = static_cast<float>(v);
        std::vector<gg::StreamParams> cases{params(v, v, v, v, v, f)};
        for (int field = 0; field < 6; ++field)
        {
            gg::StreamParams p = defaults;
            switch (field)
            {
            case 0:
                p.grainRateHz = v;
                break;
            case 1:
                p.grainDurationSec = v;
                break;
            case 2:
                p.position = v;
                break;
            case 3:
                p.positionJitter = v;
                break;
            case 4:
                p.pitchSemitones = v;
                break;
            default:
                p.gain = f;
                break;
            }
            cases.push_back(p);
        }
        for (std::size_t i = 0; i < cases.size(); ++i)
        {
            INFO("value = " << v << ", case = " << i);
            gg::Engine engine(gg::EngineConfig{});
            engine.setSource(source);
            engine.setStreamParams(cases[i]);
            SinkLog log;
            engine.setEventSink(&SinkLog::record, &log);
            const auto r = render(engine, 12000, 480);
            REQUIRE(allFinite(r.left));
            REQUIRE(allFinite(r.right));
            // Bounded (ruling R3): a unit-amplitude source, gain clamped to at most 4 and
            // Catmull-Rom overshoot of at most 1.25, so no sample exceeds 5 per grain spawned.
            REQUIRE_FALSE(log.overflowed);
            const float bound = 4.0f * 1.25f * static_cast<float>(log.grains.size());
            REQUIRE(peak(r.left) <= bound);
            REQUIRE(peak(r.right) <= bound);
        }
    }
}

TEST_CASE("oversized and empty blocks", "[engine]")
{
    gg::EngineConfig config;
    config.maxBlockSize = 64;
    const auto source = makeSine(2, 48000, 48000.0);
    const auto p = params(300.0, 0.005, 0.5, 0.3, 3.0, 0.8f);

    gg::Engine whole(config);
    whole.setSource(source);
    whole.setStreamParams(p);

    float sentinel = 7.0f;
    float *empty[2] = {&sentinel, &sentinel};
    whole.process(empty, 2, 0);
    REQUIRE(whole.sampleClock() == 0);
    REQUIRE(sentinel == 7.0f);

    const auto big = render(whole, 1000, 1000); // one call, split internally at 64
    REQUIRE(whole.sampleClock() == 1000);
    whole.process(empty, 2, 0);
    REQUIRE(whole.sampleClock() == 1000);
    REQUIRE(sentinel == 7.0f);

    gg::Engine split(config);
    split.setSource(source);
    split.setStreamParams(p);
    const auto small = render(split, 1000, 64); // 15 x 64 + 40
    REQUIRE(split.sampleClock() == 1000);

    REQUIRE(allFinite(big.left));
    REQUIRE(allFinite(big.right));
    REQUIRE(peak(big.left) > 0.0f);
    REQUIRE(sameBits(big.left, small.left));
    REQUIRE(sameBits(big.right, small.right));
}

TEST_CASE("output channel layouts", "[engine]")
{
    // The plugins pass the host's channel count and pointers straight through, so every layout
    // must render the same schedule as the stereo reference, and write exactly what it promises.
    constexpr int64_t kFrames = 6000;
    constexpr uint32_t kBlock = 500;
    const auto source = makeSine(2, 48000, 48000.0); // stereo: the two outputs differ
    const auto p = params(80.0, 0.03, 0.4, 0.2, 2.0, 0.9f);
    auto makeEngine = [&]() {
        auto engine = std::make_unique<gg::Engine>(gg::EngineConfig{});
        engine->setSource(source);
        engine->setStreamParams(p);
        return engine;
    };

    const auto refEngine = makeEngine();
    const auto ref = render(*refEngine, kFrames, kBlock);
    REQUIRE(allFinite(ref.left));
    REQUIRE(allFinite(ref.right));
    REQUIRE(peak(ref.left) > 0.05f);
    REQUIRE_FALSE(sameBits(ref.left, ref.right));

    // Renders kFrames in kBlock calls into `channels` NaN-filled channels, of which those listed
    // in `absent` are passed as null pointers.
    auto renderLayout = [&](gg::Engine &engine, uint32_t channels,
                            const std::vector<uint32_t> &absent) {
        std::vector<std::vector<float>> buffers(
            channels, std::vector<float>(static_cast<std::size_t>(kFrames), kUnwritten));
        std::vector<float *> out(channels);
        for (int64_t done = 0; done < kFrames; done += kBlock)
        {
            for (uint32_t c = 0; c < channels; ++c)
            {
                const bool isAbsent = std::find(absent.begin(), absent.end(), c) != absent.end();
                out[c] = isAbsent ? nullptr : buffers[c].data() + done;
            }
            engine.process(out.data(), channels, kBlock);
        }
        return buffers;
    };
    auto allZero = [](const std::vector<float> &v) {
        return std::all_of(v.begin(), v.end(), [](float x) { return x == 0.0f; });
    };

    SECTION("one channel carries source channel 0")
    {
        const auto engine = makeEngine();
        const auto out = renderLayout(*engine, 1, {});
        REQUIRE(sameBits(out[0], ref.left));
    }

    SECTION("channels beyond the second are zeroed")
    {
        const auto engine = makeEngine();
        const auto out = renderLayout(*engine, 4, {});
        REQUIRE(sameBits(out[0], ref.left));
        REQUIRE(sameBits(out[1], ref.right));
        REQUIRE(allZero(out[2]));
        REQUIRE(allZero(out[3]));
    }

    SECTION("a null first channel renders nothing, and the second is zeroed")
    {
        const auto engine = makeEngine();
        const auto out = renderLayout(*engine, 2, {0});
        REQUIRE(allZero(out[1]));
        REQUIRE(engine->sampleClock() == kFrames);
    }

    SECTION("null outputs keep the schedule running")
    {
        const auto engine = makeEngine();
        for (int64_t done = 0; done < kFrames / 2; done += kBlock)
        {
            engine->process(nullptr, 2, kBlock);
        }
        REQUIRE(engine->sampleClock() == kFrames / 2);
        // The second half matches the reference's second half: the null calls advanced the
        // schedule and spawned the same grains.
        const auto rest = render(*engine, kFrames / 2, kBlock);
        const auto half = static_cast<std::ptrdiff_t>(kFrames / 2);
        REQUIRE(sameBits(rest.left, std::vector<float>(ref.left.begin() + half, ref.left.end())));
        REQUIRE(
            sameBits(rest.right, std::vector<float>(ref.right.begin() + half, ref.right.end())));
    }
}

TEST_CASE("degenerate sources", "[engine]")
{
    auto oneFrame = std::make_shared<gg::SampleBuffer>(1, 1, 48000.0);
    oneFrame->channel(0)[0] = 1.0f;
    auto tenFrames = makeSine(2, 10, 48000.0, 4800.0);
    auto noFrames = std::make_shared<gg::SampleBuffer>(2, 0, 48000.0);
    auto noChannels = std::make_shared<gg::SampleBuffer>(0, 100, 48000.0);

    for (const auto &source : {oneFrame, tenFrames, noFrames, noChannels})
    {
        for (const double pitch : {-48.0, 0.0, 48.0})
        {
            INFO("channels = " << source->channels() << ", frames = " << source->frames()
                               << ", pitch = " << pitch);
            gg::Engine engine(gg::EngineConfig{});
            engine.setSource(source);
            // 0.1 s at 48 kHz: each grain is 4800 samples, far longer than the source.
            engine.setStreamParams(params(50.0, 0.1, 0.5, 1.0, pitch, 1.0f));
            const auto r = render(engine, 24000, 333);
            REQUIRE(allFinite(r.left));
            REQUIRE(allFinite(r.right));
            // Gain 1 x Catmull-Rom overshoot 1.25 x at most 5 overlapping windows.
            REQUIRE(peak(r.left) <= 1.25f * 5.0f);
            REQUIRE(peak(r.right) <= 1.25f * 5.0f);
        }
    }

    // A source can be replaced or removed between process calls.
    gg::Engine engine(gg::EngineConfig{});
    engine.setSource(makeSine(1, 48000, 48000.0));
    render(engine, 4800, 256);
    engine.setSource(oneFrame);
    REQUIRE(allFinite(render(engine, 4800, 256).left));
    engine.setSource(nullptr);
    REQUIRE(allFinite(render(engine, 4800, 256).left));
}

TEST_CASE("event sink sees every spawn", "[engine]")
{
    SECTION("every scheduled grain is spawned and reported once, in order")
    {
        gg::Engine engine(gg::EngineConfig{});
        engine.setSource(makeSine(1, 96000, 48000.0));
        SinkLog log;
        engine.setEventSink(&SinkLog::record, &log);
        render(engine, 48000, 100); // default 20 Hz: onsets 0, 2400, ..., 45600
        REQUIRE_FALSE(log.overflowed);
        REQUIRE(log.grains.size() == 20);
        REQUIRE(engine.droppedGrains() == 0);
        bool consecutive = true;
        for (uint64_t k = 0; k < log.grains.size(); ++k)
        {
            consecutive = consecutive && log.grains[k].index == k &&
                          log.grains[k].onsetSample == static_cast<int64_t>(k * 2400);
        }
        REQUIRE(consecutive);
    }

    SECTION("dropped grains are counted, not reported")
    {
        gg::EngineConfig config;
        config.grainCapacity = 2;
        gg::Engine engine(config);
        engine.setSource(makeSine(1, 96000, 48000.0));
        engine.setStreamParams(params(100.0, 0.1, 0.5, 0.0, 0.0, 0.5f)); // 10 grains overlap
        SinkLog log;
        engine.setEventSink(&SinkLog::record, &log);
        render(engine, 48000, 100);
        REQUIRE_FALSE(log.overflowed);
        REQUIRE(engine.droppedGrains() > 0);
        REQUIRE(log.grains.size() + engine.droppedGrains() == 100);
        bool increasing = true;
        for (std::size_t i = 1; i < log.grains.size(); ++i)
        {
            increasing = increasing && log.grains[i - 1].index < log.grains[i].index;
        }
        REQUIRE(increasing);

        // Removing the sink stops the reports, not the spawns.
        const auto reported = log.grains.size();
        engine.setEventSink(nullptr, nullptr);
        render(engine, 4800, 100);
        REQUIRE(log.grains.size() == reported);
        REQUIRE(engine.activeGrains() > 0);
    }
}

TEST_CASE("pool saturation is block-size independent", "[engine]")
{
    // Capacity 4 against ~20 overlapping grains: admission must depend on which grains are
    // still sounding at each onset, not on where the block boundaries fall.
    gg::EngineConfig config;
    config.grainCapacity = 4;
    config.seed = 11;
    const auto source = makeSine(2, 48000, 48000.0);
    const auto p = params(400.0, 0.05, 0.5, 0.5, -5.0, 0.7f);

    auto run = [&](const std::vector<uint32_t> &blocks, SinkLog &log, uint64_t &dropped) {
        gg::Engine engine(config);
        engine.setSource(source);
        engine.setStreamParams(p);
        engine.setEventSink(&SinkLog::record, &log);
        auto r = render(engine, 24000, blocks);
        dropped = engine.droppedGrains();
        return r;
    };

    SinkLog refLog;
    uint64_t refDropped = 0;
    const auto ref = run({4096}, refLog, refDropped);
    REQUIRE_FALSE(refLog.overflowed);
    REQUIRE(refDropped > 0);
    REQUIRE(allFinite(ref.left));
    REQUIRE(allFinite(ref.right));
    REQUIRE(peak(ref.left) > 0.0f);

    for (const auto &blocks : {std::vector<uint32_t>{1}, std::vector<uint32_t>{7},
                               std::vector<uint32_t>{64}, std::vector<uint32_t>{1, 500, 3, 129}})
    {
        INFO("first block size = " << blocks.front());
        SinkLog log;
        uint64_t dropped = 0;
        const auto r = run(blocks, log, dropped);
        REQUIRE(dropped == refDropped);
        REQUIRE(log.grains == refLog.grains);
        REQUIRE(sameBits(r.left, ref.left));
        REQUIRE(sameBits(r.right, ref.right));
    }
}

TEST_CASE("reset restarts the render", "[engine]")
{
    gg::Engine engine(gg::EngineConfig{});
    engine.setSource(makeSine(1, 48000, 48000.0));
    engine.setStreamParams(params(60.0, 0.05, 0.3, 0.4, 2.0, 0.6f));
    const auto first = render(engine, 9000, 128);
    REQUIRE(engine.activeGrains() > 0);
    engine.reset();
    REQUIRE(engine.sampleClock() == 0);
    REQUIRE(engine.activeGrains() == 0);
    REQUIRE(engine.droppedGrains() == 0);
    const auto second = render(engine, 9000, 128);
    REQUIRE(allFinite(first.left));
    REQUIRE(peak(first.left) > 0.0f);
    REQUIRE(sameBits(first.left, second.left));
}

TEST_CASE("random param sweep stays finite and bounded", "[engine]")
{
    // Ruling R3: 64 grains, so the bound is max gain 4 x 64 grains x Catmull-Rom overshoot 1.25
    // on a unit-amplitude source.
    gg::EngineConfig config;
    config.grainCapacity = 64;
    config.maxBlockSize = 512;
    gg::Engine engine(config);
    engine.setSource(makeSine(1, 48000, 48000.0, 997.0, 1.0));

    uint64_t counter = 0;
    auto draw = [&counter](double lo, double hi) {
        const double u =
            gg::rng::uniform01(gg::rng::hash({7, 0, 0, gg::rng::Field::SignalNoise, counter++}));
        return lo + (hi - lo) * u;
    };
    // Each field is drawn over twice the width of its legal range, centred on it, so about
    // half the draws are out of range. Durations are "legal" over 0..1 s for this purpose.
    auto wide = [&draw](double lo, double hi) {
        const double w = hi - lo;
        return draw(lo - 0.5 * w, hi + 0.5 * w);
    };

    bool finite = true;
    float worst = 0.0f;
    for (int iteration = 0; iteration < 200; ++iteration)
    {
        engine.setStreamParams({wide(0.0, 4000.0), wide(0.0, 1.0), wide(0.0, 1.0), wide(0.0, 1.0),
                                wide(-48.0, 48.0), static_cast<float>(wide(0.0, 4.0))});
        std::vector<uint32_t> blocks;
        for (int i = 0; i < 64; ++i)
        {
            blocks.push_back(1 + static_cast<uint32_t>(draw(0.0, 512.0)));
        }
        const auto r = render(engine, 12000, blocks);
        finite = finite && allFinite(r.left) && allFinite(r.right);
        worst = std::max({worst, peak(r.left), peak(r.right)});
    }
    REQUIRE(finite);
    REQUIRE(worst <= 320.0f);
}
