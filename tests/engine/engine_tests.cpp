// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Interpolate.h"
#include "gg/core/MathDet.h"
#include "gg/core/Rng.h"
#include "gg/core/Window.h"
#include "gg/engine/Engine.h"
#include "gg/grain/Budget.h"
#include "gg/grain/Envelope.h"
#include "gg/grain/GrainKernels.h"
#include "gg/grain/GrainPool.h"
#include "gg/grain/SincTable.h"
#include "gg/metrics/AudioData.h"
#include "gg/metrics/Metrics.h"
#include "gg/source/Mipmap.h"
#include "gg/source/PreparedSource.h"

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
// and never allocates (the RTSan build checks that). The capacity holds a grain per sample, the
// density ceiling, for the 12,000 frames of "extreme and non-finite params stay finite".
struct SinkLog
{
    static constexpr std::size_t kCapacity = 16384;
    std::vector<gg::GrainSpec> grains;
    bool overflowed{false};

    SinkLog() { grains.reserve(kCapacity); }

    static void record(void *ctx, uint32_t, const gg::GrainSpec &g)
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

// Periodic (distribution 0) and centred (stereo 0), as M0's stream was.
gg::CloudParams params(double densityHz, double durationSec, double position, double spray,
                       double pitch, float gain)
{
    return {.densityHz = densityHz,
            .grainDurationSec = durationSec,
            .position = position,
            .spray = spray,
            .pitchSemitones = pitch,
            .gain = gain};
}

// The largest pan gain, sqrt(2), rounded up.
constexpr float kMaxPanGain = 1.4143f;
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
    const gg::CloudParams defaults{};

    // 1/48000 s asks for a 1-sample grain, which the minimum window length overrides.
    for (const double v : {nan, inf, -inf, -1.0, 0.0, 1e9, 1.0 / 48000.0})
    {
        const auto f = static_cast<float>(v);
        std::vector<gg::CloudParams> cases{{v, v, v, v, v, v, v, f}};
        for (int field = 0; field < 8; ++field)
        {
            gg::CloudParams p = defaults;
            switch (field)
            {
            case 0:
                p.densityHz = v;
                break;
            case 1:
                p.distribution = v;
                break;
            case 2:
                p.grainDurationSec = v;
                break;
            case 3:
                p.position = v;
                break;
            case 4:
                p.spray = v;
                break;
            case 5:
                p.pitchSemitones = v;
                break;
            case 6:
                p.stereo = v;
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
            engine.setCloudParams(cases[i]);
            SinkLog log;
            engine.setEventSink(&SinkLog::record, &log);
            const auto r = render(engine, 12000, 480);
            REQUIRE(allFinite(r.left));
            REQUIRE(allFinite(r.right));
            // Bounded (ruling R3): a unit-amplitude source, gain clamped to at most 4,
            // Catmull-Rom overshoot of at most 1.25 and a pan gain of at most sqrt(2), so no
            // sample exceeds about 7.1 per grain spawned.
            REQUIRE_FALSE(log.overflowed);
            const float bound = 4.0f * 1.25f * kMaxPanGain * static_cast<float>(log.grains.size());
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
    whole.setCloudParams(p);

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
    split.setCloudParams(p);
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
        engine->setCloudParams(p);
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
            engine.setCloudParams(params(50.0, 0.1, 0.5, 1.0, pitch, 1.0f));
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
        render(engine, 48000, 100); // default 16 Hz, periodic: onsets 0, 3000, ..., 45000
        REQUIRE_FALSE(log.overflowed);
        REQUIRE(log.grains.size() == 16);
        REQUIRE(engine.droppedGrains() == 0);
        bool consecutive = true;
        for (uint64_t k = 0; k < log.grains.size(); ++k)
        {
            consecutive = consecutive && log.grains[k].index == k &&
                          log.grains[k].onsetSample == static_cast<int64_t>(k * 3000);
        }
        REQUIRE(consecutive);
    }

    SECTION("dropped grains are counted, not reported")
    {
        gg::EngineConfig config;
        config.grainCapacity = 2;
        gg::Engine engine(config);
        engine.setSource(makeSine(1, 96000, 48000.0));
        engine.setCloudParams(params(100.0, 0.1, 0.5, 0.0, 0.0, 0.5f)); // 10 grains overlap
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
        engine.setCloudParams(p);
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
    engine.setCloudParams(params(60.0, 0.05, 0.3, 0.4, 2.0, 0.6f));
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
    // x the largest pan gain sqrt(2) on a unit-amplitude source.
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
        engine.setCloudParams({wide(0.0, 4000.0), wide(0.0, 1.0), wide(0.0, 1.0), wide(0.0, 1.0),
                               wide(0.0, 1.0), wide(-48.0, 48.0), wide(0.0, 1.0),
                               static_cast<float>(wide(0.0, 4.0))});
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
    REQUIRE(worst <= 4.0f * 64.0f * 1.25f * kMaxPanGain);
}

TEST_CASE("pan follows equal power with unity center", "[engine]")
{
    // The law: gL = sqrt(1 - pan) and gR = sqrt(1 + pan), so gL^2 + gR^2 = 2. Both are exactly 1
    // at the centre, so a centred grain's latched channel gains are its gain, and its output 0
    // is the one-output render's bit for bit, as is its output 1 from a mono source (ruling
    // M1-d; spec §3).
    const gg::PanGains centre = gg::panGains(0.0f);
    REQUIRE(centre.left == 1.0f);
    REQUIRE(centre.right == 1.0f);
    bool onLaw = true;
    for (int i = -1000; i <= 1000; ++i)
    {
        const float pan = static_cast<float>(i) / 1000.0f;
        const gg::PanGains g = gg::panGains(pan);
        onLaw = onLaw && std::abs(g.left - std::sqrt(1.0 - pan)) <= 1e-7 &&
                std::abs(g.right - std::sqrt(1.0 + pan)) <= 1e-7;
    }
    REQUIRE(onLaw);

    // One grain: onset 0, 0.1 s long, and the next one not due for 1 s. It is rendered at
    // stereo 0 and at stereo 1 with the same seed, from a mono and from a stereo source.
    constexpr int64_t kFrames = 6000;
    for (const uint32_t channels : {1u, 2u})
    {
        INFO("source channels = " << channels);
        const auto source = makeSine(channels, 48000, 48000.0, 330.0, 0.9);
        auto makeEngine = [&](double stereo, SinkLog &log) {
            gg::EngineConfig config;
            config.seed = 17;
            auto engine = std::make_unique<gg::Engine>(config);
            engine->setSource(source);
            gg::CloudParams p = params(1.0, 0.1, 0.5, 0.3, 3.0, 0.8f);
            p.stereo = stereo;
            engine->setCloudParams(p);
            engine->setEventSink(&SinkLog::record, &log);
            return engine;
        };
        SinkLog centreLog;
        const auto centred = render(*makeEngine(0.0, centreLog), kFrames, 256);
        SinkLog wideLog;
        const auto wide = render(*makeEngine(1.0, wideLog), kFrames, 256);
        REQUIRE(centreLog.grains.size() == 1);
        REQUIRE(wideLog.grains.size() == 1);
        const gg::GrainSpec &g = centreLog.grains[0];
        REQUIRE(g.pan == 0.0f);
        REQUIRE_FALSE(std::signbit(g.pan)); // +0, never -0, so the event log reads "pan":0

        // The spec's §3 arithmetic for this grain, added to silence: where the window w is
        // positive, each channel is the Hermite read times (w times the channel's gain).
        const gg::HannTable hann;
        const auto expected = [&](const gg::GrainSpec &grain, const float (&channelGain)[2]) {
            std::array<std::vector<float>, 2> out;
            const double invDuration = 1.0 / grain.durationSamples;
            for (uint32_t c = 0; c < 2; ++c)
            {
                const float *data = source->channel(channels == 1 ? 0 : c);
                out[c].assign(static_cast<std::size_t>(kFrames), 0.0f);
                const int64_t end = std::min(kFrames, gg::grainEndSample(grain));
                for (int64_t t = grain.onsetSample; t < end; ++t)
                {
                    const double e = static_cast<double>(t - grain.onsetSample) - grain.onsetFrac;
                    const float w = hann.at(e * invDuration);
                    if (w > 0.0f)
                    {
                        out[c][static_cast<std::size_t>(t)] +=
                            gg::interp::hermite4(data, source->frames(),
                                                 grain.startPos + grain.rate * e) *
                            (w * channelGain[c]);
                    }
                }
            }
            return out;
        };
        // Stereo 0: both channel gains are the grain's gain.
        const std::array<std::vector<float>, 2> unpannedOut = expected(g, {g.gain, g.gain});
        REQUIRE(peak(unpannedOut[0]) > 0.1f);
        REQUIRE(sameBits(centred.left, unpannedOut[0]));
        REQUIRE(sameBits(centred.right, unpannedOut[1]));

        // Stereo 1: the same grain, only its pan differs, and each channel's gain is the grain's
        // gain times its pan gain, latched once.
        gg::GrainSpec unpanned = wideLog.grains[0];
        const float pan = unpanned.pan;
        unpanned.pan = 0.0f;
        REQUIRE(unpanned == g);
        INFO("pan = " << pan);
        REQUIRE(std::abs(pan) > 0.1f); // seed 17 puts the grain clearly off centre
        const gg::PanGains gains = gg::panGains(pan);
        REQUIRE(std::abs(gains.left - std::sqrt(1.0 - pan)) <= 1e-7);
        REQUIRE(std::abs(gains.right - std::sqrt(1.0 + pan)) <= 1e-7);
        const std::array<std::vector<float>, 2> pannedOut =
            expected(wideLog.grains[0], {g.gain * gains.left, g.gain * gains.right});
        REQUIRE(sameBits(wide.left, pannedOut[0]));
        REQUIRE(sameBits(wide.right, pannedOut[1]));

        // A single output channel has no stereo placement: it carries source channel 0 unpanned.
        SinkLog monoLog;
        const auto monoEngine = makeEngine(1.0, monoLog);
        std::vector<float> mono(static_cast<std::size_t>(kFrames), kUnwritten);
        for (int64_t done = 0; done < kFrames; done += 500)
        {
            float *out[1] = {mono.data() + done};
            monoEngine->process(out, 1, 500);
        }
        REQUIRE(monoLog.grains == wideLog.grains);
        REQUIRE(sameBits(mono, unpannedOut[0]));
    }
}

TEST_CASE("held keys and the note gate reach the scheduler", "[engine]")
{
    // setHeldKeys and setNoteGate apply from the next process() call's first frame.
    gg::Engine engine(gg::EngineConfig{});
    engine.setSource(makeSine(1, 96000, 48000.0));
    engine.setCloudParams(params(100.0, 0.05, 0.5, 0.0, 3.0, 0.5f));
    engine.setNoteGate(true);
    SinkLog log;
    engine.setEventSink(&SinkLog::record, &log);
    render(engine, 9600, 64);
    REQUIRE(log.grains.empty());
    REQUIRE(engine.activeGrains() == 0);

    const std::array<uint8_t, 1> held{72};
    engine.setHeldKeys(held);
    render(engine, 960, 64);
    REQUIRE(log.grains.size() == 2);
    CHECK(log.grains[0].onsetSample == 9600);
    CHECK(log.grains[1].onsetSample == 10080);
    for (const gg::GrainSpec &g : log.grains)
    {
        CHECK(g.rate == gg::exp2Det(15.0 / 12.0));
    }

    engine.setHeldKeys({});
    render(engine, 4800, 64);
    CHECK(log.grains.size() == 2);
    REQUIRE_FALSE(log.overflowed);
}

namespace
{
// A mono source of `frames` frames at 48 kHz, every sample `value`.
std::shared_ptr<gg::SampleBuffer> makeConstant(int64_t frames, float value)
{
    auto buf = std::make_shared<gg::SampleBuffer>(1, frames, 48000.0);
    std::fill_n(buf->channel(0), frames, value);
    return buf;
}

// The scalar reference's render of `grains`, every grain Normal-tier, over [0, frames) of both
// outputs, each grain read from the mono buffer sourceOf(grain) returns. Each grain adds to every
// sample before the next starts, so each sample sums its grains in spawn order, as the engine
// does.
template <class SourceOf>
Render referenceRender(const std::vector<gg::GrainSpec> &grains, int64_t frames, SourceOf sourceOf)
{
    Render r{std::vector<float>(static_cast<std::size_t>(frames), 0.0f),
             std::vector<float>(static_cast<std::size_t>(frames), 0.0f)};
    gg::GrainPool pool(static_cast<uint32_t>(std::max<std::size_t>(1, grains.size())));
    for (const gg::GrainSpec &g : grains)
    {
        REQUIRE(g.tier == gg::Tier::Normal);
        REQUIRE(pool.spawn(g));
    }
    const gg::HannTable hann;
    const gg::SincTable sinc;
    const gg::kernels::Output out{{r.left.data(), r.right.data()}, 0};
    for (uint32_t i = 0; i < pool.size(); ++i)
    {
        const gg::SampleBuffer &buf = sourceOf(grains[i]);
        REQUIRE(buf.channels() == 1);
        const gg::kernels::SampleSource src{
            {buf.channel(0), buf.channel(0)}, buf.frames(), true, nullptr};
        gg::kernels::renderSampleRef<gg::kernels::Hermite>(pool.rows(), i, src, out, 0, frames,
                                                           {hann, sinc});
    }
    return r;
}

void append(Render &to, const Render &more)
{
    to.left.insert(to.left.end(), more.left.begin(), more.left.end());
    to.right.insert(to.right.end(), more.right.begin(), more.right.end());
}

bool sameBitsIn(const std::vector<float> &a, const std::vector<float> &b, int64_t from, int64_t to)
{
    return from <= to && to <= static_cast<int64_t>(std::min(a.size(), b.size())) &&
           std::memcmp(a.data() + from, b.data() + from,
                       static_cast<std::size_t>(to - from) * sizeof(float)) == 0;
}

// The spec with its source slot cleared, for comparing the grains of renders that read their
// sources from different slots.
gg::GrainSpec withoutSlot(gg::GrainSpec g)
{
    g.sourceSlot = 0;
    return g;
}
} // namespace

TEST_CASE("a new source applies from the next onset and old grains finish on the old one",
          "[engine]")
{
    // Source A is all +0.5 and B all -0.5, so a grain's sign says which it read. 100 Hz of
    // 200 ms grains, periodic and centred: about 20 overlap, and the grains started before the
    // swap at 0.5 s sound on for up to 200 ms after it. One onset falls on the swap sample
    // itself (24,000 = 50 x 480), and it belongs to the new source.
    constexpr int64_t kSwap = 24000;
    constexpr int64_t kFrames = 48000;
    const auto bufA = makeConstant(48000, 0.5f);
    const auto bufB = makeConstant(48000, -0.5f);
    const auto a = gg::PreparedSource::make(bufA);
    const auto b = gg::PreparedSource::make(bufB);
    REQUIRE(&a->buffer() == bufA.get());
    REQUIRE(a->pairs().data() == nullptr); // mono stays planar
    const gg::CloudParams p = params(100.0, 0.2, 0.5, 0.0, 0.0, 0.5f);

    // Adopts `first`, renders up to kSwap, adopts `second` and renders the rest.
    auto run = [&](const gg::PreparedSource *first, const gg::PreparedSource *second,
                   SinkLog &log) {
        gg::Engine engine(gg::EngineConfig{});
        engine.setEventSink(&SinkLog::record, &log);
        engine.setCloudParams(p);
        REQUIRE(engine.adoptSource(first));
        Render r = render(engine, kSwap, 256);
        REQUIRE(engine.adoptSource(second)); // the second adoption of `first` changes nothing
        append(r, render(engine, kFrames - kSwap, 256));
        return r;
    };
    SinkLog swapLog;
    const Render swapped = run(a.get(), b.get(), swapLog);
    SinkLog aLog;
    const Render aOnly = run(a.get(), a.get(), aLog);
    SinkLog bLog;
    const Render bOnly = run(b.get(), b.get(), bLog);
    REQUIRE_FALSE(swapLog.overflowed);
    REQUIRE(allFinite(swapped.left));
    REQUIRE(allFinite(swapped.right));

    // The same grains in all three renders: the schedule doesn't depend on what the source
    // holds. Only the swap moves grains to another source slot.
    REQUIRE(swapLog.grains.size() == 100);
    REQUIRE(aLog.grains.size() == swapLog.grains.size());
    REQUIRE(bLog.grains.size() == swapLog.grains.size());
    bool sameGrains = true;
    for (std::size_t k = 0; k < swapLog.grains.size(); ++k)
    {
        sameGrains = sameGrains && withoutSlot(swapLog.grains[k]) == withoutSlot(aLog.grains[k]) &&
                     aLog.grains[k] == bLog.grains[k] &&
                     aLog.grains[k].sourceSlot == aLog.grains[0].sourceSlot;
    }
    REQUIRE(sameGrains);

    // Old grains (onset before the swap) are in one slot, new ones in the other.
    int64_t firstNewOnset = kFrames;
    int64_t lastOldEnd = 0;
    const uint8_t oldSlot = swapLog.grains.front().sourceSlot;
    const uint8_t newSlot = swapLog.grains.back().sourceSlot;
    REQUIRE(oldSlot != newSlot);
    bool slotsByOnset = true;
    for (const gg::GrainSpec &g : swapLog.grains)
    {
        const bool old = g.onsetSample < kSwap;
        slotsByOnset = slotsByOnset && g.sourceSlot == (old ? oldSlot : newSlot);
        if (old)
        {
            lastOldEnd = std::max(lastOldEnd, gg::grainEndSample(g));
        }
        else
        {
            firstNewOnset = std::min(firstNewOnset, g.onsetSample);
        }
    }
    REQUIRE(slotsByOnset);
    REQUIRE(firstNewOnset == kSwap);
    REQUIRE(lastOldEnd > kSwap + 9000); // the last old grain sounds ~200 ms past the swap

    // Every old grain reads A to its end and every new grain reads B: the render is the
    // reference's, grain by grain, bit for bit.
    const Render expected = referenceRender(swapLog.grains, kFrames, [&](const gg::GrainSpec &g) {
        return g.onsetSample < kSwap ? std::cref(*bufA) : std::cref(*bufB);
    });
    REQUIRE(sameBits(swapped.left, expected.left));
    REQUIRE(sameBits(swapped.right, expected.right));

    // So before the first new grain the render is A's, and once the last old grain has ended it
    // is B's; between them both signs sound.
    REQUIRE(sameBitsIn(swapped.left, aOnly.left, 0, firstNewOnset));
    REQUIRE(sameBitsIn(swapped.right, aOnly.right, 0, firstNewOnset));
    REQUIRE(sameBitsIn(swapped.left, bOnly.left, lastOldEnd, kFrames));
    REQUIRE(sameBitsIn(swapped.right, bOnly.right, lastOldEnd, kFrames));
    REQUIRE(*std::max_element(swapped.left.begin(), swapped.left.begin() + kSwap) > 0.1f);
    REQUIRE(*std::min_element(swapped.left.begin(), swapped.left.begin() + kSwap) >= 0.0f);
    REQUIRE(*std::min_element(swapped.left.begin() + lastOldEnd, swapped.left.end()) < -0.1f);
    REQUIRE(*std::max_element(swapped.left.begin() + lastOldEnd, swapped.left.end()) <= 0.0f);
    REQUIRE_FALSE(sameBitsIn(swapped.left, aOnly.left, kSwap, lastOldEnd));
    REQUIRE_FALSE(sameBitsIn(swapped.left, bOnly.left, kSwap, lastOldEnd));
}

TEST_CASE("a third source waits", "[engine]")
{
    // A, then B at 0.5 s: A's grains sound until lastAEnd, up to ~200 ms later. Until then a
    // third source, C, is refused, changing nothing; at the first block end at or past lastAEnd
    // the engine retires A, and C is accepted. A is handed back exactly once.
    constexpr int64_t kSwap = 24000;
    const auto a = gg::PreparedSource::make(makeConstant(48000, 0.5f));
    const auto b = gg::PreparedSource::make(makeConstant(48000, -0.5f));
    const auto c = gg::PreparedSource::make(makeConstant(48000, 0.25f));
    const gg::CloudParams p = params(100.0, 0.2, 0.5, 0.0, 0.0, 0.5f);

    gg::Engine engine(gg::EngineConfig{});
    SinkLog log;
    engine.setEventSink(&SinkLog::record, &log);
    engine.setCloudParams(p);
    REQUIRE(engine.takeRetiredSource() == nullptr);
    REQUIRE(engine.adoptSource(a.get()));
    // A replaced no source, so nothing is handed back.
    REQUIRE(engine.takeRetiredSource() == nullptr);
    Render waited = render(engine, kSwap, 256);
    REQUIRE(engine.adoptSource(b.get()));
    int64_t lastAEnd = 0;
    for (const gg::GrainSpec &g : log.grains)
    {
        lastAEnd = std::max(lastAEnd, gg::grainEndSample(g));
    }
    REQUIRE(lastAEnd > kSwap + 9000);

    while (engine.sampleClock() < lastAEnd)
    {
        REQUIRE_FALSE(engine.adoptSource(c.get()));
        REQUIRE(engine.takeRetiredSource() == nullptr);
        append(waited, render(engine, 64, 64));
    }
    const int64_t cAt = engine.sampleClock();
    REQUIRE(cAt < lastAEnd + 64);
    REQUIRE(engine.adoptSource(c.get()));
    REQUIRE(engine.takeRetiredSource() == a.get());
    REQUIRE(engine.takeRetiredSource() == nullptr);

    // The refusals changed nothing: an engine that never asked renders the same bits.
    gg::Engine plain(gg::EngineConfig{});
    plain.setCloudParams(p);
    REQUIRE(plain.adoptSource(a.get()));
    Render direct = render(plain, kSwap, 256);
    REQUIRE(plain.adoptSource(b.get()));
    append(direct, render(plain, cAt - kSwap, 64));
    REQUIRE(sameBits(waited.left, direct.left));
    REQUIRE(sameBits(waited.right, direct.right));

    // B's grains, the ones started in [kSwap, cAt), drain within 200 ms; then B is handed back.
    const std::size_t grainsBeforeC = log.grains.size();
    render(engine, 12000, 256);
    REQUIRE(log.grains.size() > grainsBeforeC);
    REQUIRE(log.grains.back().sourceSlot != log.grains[grainsBeforeC - 1].sourceSlot);
    REQUIRE(engine.takeRetiredSource() == b.get());
    REQUIRE(engine.takeRetiredSource() == nullptr);
}

TEST_CASE("adopted sources are handed back by reset and setSource", "[engine]")
{
    const auto a = gg::PreparedSource::make(makeConstant(48000, 0.5f));
    const auto b = gg::PreparedSource::make(makeConstant(48000, -0.5f));
    const gg::CloudParams p = params(100.0, 0.2, 0.5, 0.0, 0.0, 0.5f);

    SECTION("reset silences A's grains, so A retires at once")
    {
        gg::Engine engine(gg::EngineConfig{});
        engine.setCloudParams(p);
        REQUIRE(engine.adoptSource(a.get()));
        render(engine, 24000, 256);
        REQUIRE(engine.adoptSource(b.get()));
        REQUIRE(engine.takeRetiredSource() == nullptr);
        engine.reset();
        REQUIRE(engine.takeRetiredSource() == a.get());
        REQUIRE(engine.takeRetiredSource() == nullptr);
        REQUIRE(engine.adoptSource(a.get())); // B, which had no grains since reset, retires
        REQUIRE(engine.takeRetiredSource() == b.get());
    }

    SECTION("setSource replaces both, oldest first, and every grain reads its source")
    {
        gg::Engine engine(gg::EngineConfig{});
        engine.setCloudParams(p);
        REQUIRE(engine.adoptSource(a.get()));
        render(engine, 24000, 256);
        REQUIRE(engine.adoptSource(b.get()));
        render(engine, 2400, 256); // A's and B's grains both sound
        const uint32_t sounding = engine.activeGrains();
        REQUIRE(sounding > 10);
        engine.setSource(makeConstant(48000, 0.25f));
        REQUIRE(engine.activeGrains() == sounding);
        REQUIRE(engine.takeRetiredSource() == a.get());
        REQUIRE(engine.takeRetiredSource() == b.get());
        REQUIRE(engine.takeRetiredSource() == nullptr);
        // Every grain, old or new, now reads +0.25.
        const Render r = render(engine, 4800, 256);
        REQUIRE(*std::min_element(r.left.begin(), r.left.end()) >= 0.0f);
        REQUIRE(*std::max_element(r.left.begin(), r.left.end()) > 0.1f);
        // The engine owns setSource's source: it is never handed back.
        REQUIRE(engine.adoptSource(a.get()));
        render(engine, 12000, 256);
        REQUIRE(engine.takeRetiredSource() == nullptr);
    }
}

TEST_CASE("replaceSource replaces as setSource does, and its source is handed back", "[engine]")
{
    // Processor::setSourceNow's engine call: the caller owns the source, so, unlike setSource's,
    // it comes back through takeRetiredSource once a later adoption replaces it and it drains.
    const auto a = gg::PreparedSource::make(makeConstant(48000, 0.5f));
    const auto b = gg::PreparedSource::make(makeConstant(48000, -0.5f));
    const auto c = gg::PreparedSource::make(makeConstant(48000, 0.25f));
    const gg::CloudParams p = params(100.0, 0.2, 0.5, 0.0, 0.0, 0.5f);

    gg::Engine engine(gg::EngineConfig{});
    engine.setCloudParams(p);
    engine.setSource(makeConstant(48000, 0.75f)); // owned by the engine: let go, not handed back
    REQUIRE(engine.adoptSource(a.get()));
    render(engine, 24000, 256);
    REQUIRE(engine.adoptSource(b.get()));
    render(engine, 2400, 256); // A's and B's grains both sound
    const uint32_t sounding = engine.activeGrains();
    REQUIRE(sounding > 10);

    engine.replaceSource(c.get());
    REQUIRE(engine.activeGrains() == sounding);
    REQUIRE(engine.takeRetiredSource() == a.get());
    REQUIRE(engine.takeRetiredSource() == b.get());
    REQUIRE(engine.takeRetiredSource() == nullptr);
    // Every grain, old or new, now reads +0.25.
    const Render r = render(engine, 4800, 256);
    REQUIRE(*std::min_element(r.left.begin(), r.left.end()) >= 0.0f);
    REQUIRE(*std::max_element(r.left.begin(), r.left.end()) > 0.1f);

    // C is adopted-like: a later adoption drains it, and then it is handed back.
    REQUIRE(engine.adoptSource(a.get()));
    REQUIRE(engine.takeRetiredSource() == nullptr); // C's grains still sound
    render(engine, 12000, 256);
    REQUIRE(engine.takeRetiredSource() == c.get());
    REQUIRE(engine.takeRetiredSource() == nullptr);

    // The same output bits as setPreparedSource, which owns its source.
    gg::Engine replaced(gg::EngineConfig{});
    gg::Engine set(gg::EngineConfig{});
    for (gg::Engine *e : {&replaced, &set})
    {
        e->setCloudParams(p);
    }
    replaced.replaceSource(c.get());
    set.setPreparedSource(c);
    const Render r1 = render(replaced, 12000, 256);
    const Render r2 = render(set, 12000, 256);
    REQUIRE(peak(r1.left) > 0.1f);
    REQUIRE(sameBits(r1.left, r2.left));
    REQUIRE(sameBits(r1.right, r2.right));
}

TEST_CASE("replacing the source with none ends the grains still sounding", "[engine]")
{
    // With no source the grains still sounding would read nothing. Kept, they would hold the
    // current slot until their ends, and two adoptions within Size would flip back into that
    // slot and hand them the second source mid-window.
    const auto a = gg::PreparedSource::make(makeConstant(48000, 0.5f));
    const auto b = gg::PreparedSource::make(makeConstant(48000, -0.5f));
    const auto c = gg::PreparedSource::make(makeConstant(48000, 0.25f));
    gg::Engine engine(gg::EngineConfig{});
    engine.setCloudParams(params(100.0, 0.2, 0.5, 0.0, 0.0, 0.5f));
    REQUIRE(engine.adoptSource(a.get()));
    render(engine, 2400, 256);
    REQUIRE(engine.activeGrains() > 0);

    engine.replaceSource(nullptr);
    CHECK(engine.activeGrains() == 0);
    CHECK(engine.takeRetiredSource() == a.get());
    CHECK(engine.takeRetiredSource() == nullptr);
    REQUIRE(engine.adoptSource(b.get()));
    REQUIRE(engine.adoptSource(c.get()));
    CHECK(engine.takeRetiredSource() == b.get()); // no grain read it
    CHECK(engine.activeGrains() == 0);
}

TEST_CASE("setSource renders as adopting its prepared source", "[engine]")
{
    // setSource is PreparedSource::make plus the adoption; a stereo source reads its interleaved
    // copy.
    for (const uint32_t channels : {1u, 2u})
    {
        INFO("channels = " << channels);
        const auto buffer = makeSine(channels, 48000, 48000.0, 330.0, 0.9);
        const auto prepared = gg::PreparedSource::make(buffer);
        REQUIRE((prepared->pairs().data() != nullptr) == (channels == 2));
        REQUIRE(prepared->pairs().frames() == (channels == 2 ? 48000 : 0));
        const gg::CloudParams p = params(80.0, 0.05, 0.4, 0.5, 3.0, 0.6f);
        gg::Engine set(gg::EngineConfig{});
        set.setSource(buffer);
        set.setCloudParams(p);
        SinkLog setLog;
        set.setEventSink(&SinkLog::record, &setLog);
        gg::Engine adopted(gg::EngineConfig{});
        REQUIRE(adopted.adoptSource(prepared.get()));
        adopted.setCloudParams(p);
        SinkLog adoptedLog;
        adopted.setEventSink(&SinkLog::record, &adoptedLog);
        const Render r1 = render(set, 24000, 100);
        const Render r2 = render(adopted, 24000, 100);
        REQUIRE(peak(r1.left) > 0.1f);
        REQUIRE(sameBits(r1.left, r2.left));
        REQUIRE(sameBits(r1.right, r2.right));
        REQUIRE(setLog.grains.size() == adoptedLog.grains.size());
        bool same = true;
        for (std::size_t k = 0; k < setLog.grains.size(); ++k)
        {
            same = same && withoutSlot(setLog.grains[k]) == withoutSlot(adoptedLog.grains[k]);
        }
        REQUIRE(same);
    }
    REQUIRE(gg::PreparedSource::make(nullptr) == nullptr);
}

namespace
{
// `frames` frames of mono noise at half scale, from gg::rng (seed `seed`).
std::vector<float> noiseInput(int64_t frames, uint64_t seed)
{
    std::vector<float> in(static_cast<std::size_t>(frames));
    for (int64_t i = 0; i < frames; ++i)
    {
        const auto k = static_cast<uint64_t>(i);
        in[static_cast<std::size_t>(i)] =
            0.5f * static_cast<float>(gg::rng::bipolar(
                       gg::rng::hash({seed, 0, 0, gg::rng::Field::SignalNoise, k})));
    }
    return in;
}

// Renders input[from, from + frames) through the engine as a mono input, in blocks of
// blockSize; the outputs start as NaN, as render()'s do.
Render renderInput(gg::Engine &engine, const std::vector<float> &input, int64_t from,
                   int64_t frames, uint32_t blockSize)
{
    Render r{std::vector<float>(static_cast<std::size_t>(frames), kUnwritten),
             std::vector<float>(static_cast<std::size_t>(frames), kUnwritten)};
    for (int64_t done = 0; done < frames;)
    {
        const auto n = static_cast<uint32_t>(std::min<int64_t>(blockSize, frames - done));
        const float *in[1] = {input.data() + from + done};
        float *out[2] = {r.left.data() + done, r.right.data() + done};
        engine.process(in, 1, out, 2, n);
        done += n;
    }
    return r;
}

gg::EngineConfig liveConfig()
{
    gg::EngineConfig config;
    config.liveSeconds = 16.0;
    return config;
}
} // namespace

TEST_CASE("the layer field keys the draws", "[engine][layers]")
{
    // Spec §6.2: EngineConfig::layer is the layer field of every draw's key, and layer 0 (the
    // default) keys them as M1 did. Poisson onsets, spray and stereo spread put every draw in
    // the output.
    REQUIRE(gg::EngineConfig{}.layer == 0);
    const auto source = makeSine(2, 96000, 48000.0, 330.0);
    const gg::CloudParams p{.densityHz = 40.0,
                            .distribution = 1.0,
                            .grainDurationSec = 0.08,
                            .position = 0.5,
                            .spray = 0.4,
                            .stereo = 0.8,
                            .gain = 0.5f};
    const auto run = [&](uint32_t layer, bool setLayer, SinkLog &log) {
        gg::EngineConfig config;
        config.seed = 7;
        if (setLayer)
        {
            config.layer = layer;
        }
        gg::Engine engine(config);
        engine.setSource(source);
        engine.setCloudParams(p);
        engine.setEventSink(&SinkLog::record, &log);
        return render(engine, 48000, 256);
    };
    SinkLog defaultLog;
    const Render byDefault = run(0, false, defaultLog);
    REQUIRE_FALSE(defaultLog.overflowed);
    REQUIRE(defaultLog.grains.size() > 20);
    REQUIRE(peak(byDefault.left) > 0.05f);

    SinkLog log0;
    const Render layer0 = run(0, true, log0);
    REQUIRE(log0.grains == defaultLog.grains);
    REQUIRE(sameBits(layer0.left, byDefault.left));
    REQUIRE(sameBits(layer0.right, byDefault.right));

    for (const uint32_t layer : {1u, 2u, 3u})
    {
        INFO("layer " << layer);
        SinkLog log;
        const Render other = run(layer, true, log);
        REQUIRE_FALSE(log.overflowed);
        REQUIRE(log.grains != defaultLog.grains);
        REQUIRE_FALSE(sameBits(other.left, byDefault.left));
        REQUIRE_FALSE(sameBits(other.right, byDefault.right));
    }
}

TEST_CASE("a layer above 3 is refused", "[engine][layers]")
{
    REQUIRE_THROWS_AS(gg::Engine(gg::EngineConfig{.layer = 4}), std::invalid_argument);
    REQUIRE_THROWS_AS(gg::Engine(gg::EngineConfig{.layer = 0xFFFFFFFFu}), std::invalid_argument);
    REQUIRE_NOTHROW(gg::Engine(gg::EngineConfig{.layer = 3}));
}

TEST_CASE("disabled, the engine spawns nothing but writes its ring", "[engine][layers]")
{
    // Spec §6.2: off, the scheduler emits nothing, but the ring is written and the clock
    // advances, so a layer switched on later reads recent audio.
    gg::Engine engine(liveConfig());
    engine.setSourceMode(gg::SourceMode::Live);
    engine.setCloudParams(params(100.0, 0.1, 0.1, 0.0, 0.0, 0.5f));
    SinkLog log;
    engine.setEventSink(&SinkLog::record, &log);
    REQUIRE(engine.enabled());
    engine.setEnabled(false);
    REQUIRE(engine.enabled()); // applied at the next process() call

    const auto input = noiseInput(48000, 3);
    const Render r = renderInput(engine, input, 0, 48000, 512);
    REQUIRE_FALSE(engine.enabled());
    REQUIRE(engine.sourceMode() == gg::SourceMode::Live);
    REQUIRE(log.grains.empty());
    REQUIRE(engine.activeGrains() == 0);
    REQUIRE(engine.droppedGrains() == 0);
    REQUIRE(engine.sampleClock() == 48000);
    REQUIRE(engine.liveWriteHead() == 48000);
    REQUIRE(allFinite(r.left));
    REQUIRE(peak(r.left) == 0.0f);
    REQUIRE(peak(r.right) == 0.0f);
    // The ring holds the input: its newest frame, in both channels.
    const gg::LiveSource::RingView view = engine.live().view();
    const int64_t slot = 47999 % view.capacity;
    REQUIRE(input[47999] != 0.0f);
    REQUIRE(view.slots[2 * slot] == input[47999]);
    REQUIRE(view.slots[2 * slot + 1] == input[47999]);
}

TEST_CASE("enabling forces an onset at that frame", "[engine][layers]")
{
    // 0.25 Hz, periodic: grain 0 at 0, and the next pending at 4 s (192,000). Disabled from
    // 0.5 s to 2 s; enabled again at 96,000, the pending onset moves to that frame, as a first
    // held key moves it (spec §6.2, §9). Disabling ends the 1 s grain still sounding.
    const auto source = makeSine(1, 96000, 48000.0);
    const gg::CloudParams p = params(0.25, 1.0, 0.5, 0.0, 0.0, 0.5f);
    gg::Engine engine(gg::EngineConfig{});
    engine.setSource(source);
    engine.setCloudParams(p);
    SinkLog log;
    engine.setEventSink(&SinkLog::record, &log);
    render(engine, 24000, 512);
    REQUIRE(log.grains.size() == 1);
    REQUIRE(engine.activeGrains() == 1);

    engine.setEnabled(false);
    const Render off = render(engine, 72000, 512);
    REQUIRE(engine.activeGrains() == 0);
    REQUIRE(peak(off.left) == 0.0f);
    REQUIRE(log.grains.size() == 1);

    engine.setEnabled(true);
    const Render on = render(engine, 48000, 512);
    REQUIRE(engine.enabled());
    REQUIRE(log.grains.size() == 2);
    CHECK(log.grains[1].index == 1);
    CHECK(log.grains[1].onsetSample == 96000);
    CHECK(log.grains[1].onsetFrac == 0.0);
    CHECK(peak(on.left) > 0.05f);

    // Not vacuous: never disabled, grain 1 waits for 4 s.
    gg::Engine always(gg::EngineConfig{});
    always.setSource(source);
    always.setCloudParams(p);
    SinkLog alwaysLog;
    always.setEventSink(&SinkLog::record, &alwaysLog);
    render(always, 144000, 512);
    REQUIRE(alwaysLog.grains.size() == 1);
}

TEST_CASE("a source kind switch lets sounding grains finish", "[engine][layers]")
{
    // Spec §6.4 (carry-over row 119): at a switch from Sample to Live the scheduler draws for
    // the ring from that frame, and the grains still sounding finish on the sample. The
    // expectation is an engine that never switches but turns its stream off at the switch frame
    // (density 0, ruling M1-a): what it renders from there is exactly the old grains finishing.
    // The ring takes silence, so the new grains add nothing.
    constexpr int64_t kSwitch = 24000;
    constexpr int64_t kAfter = 48000; // the old grains, 1 s long, end within it
    const auto source = makeSine(1, 96000, 48000.0);
    const gg::CloudParams p = params(10.0, 1.0, 0.5, 0.0, 0.0, 0.25f);
    gg::Engine switched(liveConfig());
    gg::Engine reference(liveConfig());
    for (gg::Engine *e : {&switched, &reference})
    {
        e->setSource(source);
        e->setCloudParams(p);
    }
    SinkLog log;
    switched.setEventSink(&SinkLog::record, &log);
    const Render before = render(switched, kSwitch, 256);
    const Render referenceBefore = render(reference, kSwitch, 256);
    REQUIRE(sameBits(before.left, referenceBefore.left));
    REQUIRE(switched.activeGrains() == 5); // onsets 0, 4800, ..., 19200
    const std::size_t spawnedBefore = log.grains.size();

    switched.setSourceMode(gg::SourceMode::Live);
    gg::CloudParams off = p;
    off.densityHz = 0.0;
    reference.setCloudParams(off);
    const Render after = render(switched, kAfter, 256);
    const Render expected = render(reference, kAfter, 256);
    REQUIRE(switched.sourceMode() == gg::SourceMode::Live);
    REQUIRE(peak(expected.left) > 0.05f);
    REQUIRE(sameBits(after.left, expected.left));
    REQUIRE(sameBits(after.right, expected.right));

    // The grains before the switch read sample slots; every one after it reads the ring.
    REQUIRE_FALSE(log.overflowed);
    REQUIRE(log.grains.size() > spawnedBefore);
    bool slots = true;
    for (std::size_t k = 0; k < log.grains.size(); ++k)
    {
        const gg::GrainSpec &g = log.grains[k];
        slots = slots && (k < spawnedBefore
                              ? g.sourceSlot < gg::Engine::kLiveSlot && g.onsetSample < kSwitch
                              : g.sourceSlot == gg::Engine::kLiveSlot && g.onsetSample >= kSwitch);
    }
    REQUIRE(slots);
}

TEST_CASE("a sample replaced while ring grains sound leaves them on the ring", "[engine][layers]")
{
    // Replacing the sample moves the sample grains still sounding onto it (setSource), never a
    // grain reading the ring. Sample grains of 0.1 s until 0.5 s, then Live with 1 s grains over
    // noise; at 0.8 s, with only ring grains sounding, back to Sample with the stream off, and
    // one engine replaces its sample. The ring grains then render the same in both.
    const auto input = noiseInput(96000, 11);
    const auto first = makeSine(1, 96000, 48000.0);
    const auto second = makeConstant(96000, 0.5f);
    gg::Engine replaced(liveConfig());
    gg::Engine kept(liveConfig());
    for (gg::Engine *e : {&replaced, &kept})
    {
        e->setSource(first);
        e->setCloudParams(params(20.0, 0.1, 0.5, 0.0, 0.0, 0.5f));
        renderInput(*e, input, 0, 24000, 256);
        e->setSourceMode(gg::SourceMode::Live);
        e->setCloudParams(params(20.0, 1.0, 0.2, 0.0, 0.0, 0.5f));
        renderInput(*e, input, 24000, 14400, 256);
        REQUIRE(e->activeGrains() > 0);
        e->setSourceMode(gg::SourceMode::Sample);
        e->setCloudParams(params(0.0, 1.0, 0.2, 0.0, 0.0, 0.5f));
    }
    replaced.setSource(second);
    const Render a = renderInput(replaced, input, 38400, 57600, 256);
    const Render b = renderInput(kept, input, 38400, 57600, 256);
    REQUIRE(peak(b.left) > 0.05f);
    REQUIRE(sameBits(a.left, b.left));
    REQUIRE(sameBits(a.right, b.right));
}

TEST_CASE("the level accessor returns level 0 for any index past the count", "[engine][layers]")
{
    // Spec §6.3: level 0 is the source as the kernels read it, and any index past the last
    // level reads level 0.
    for (const uint32_t channels : {1u, 2u})
    {
        INFO("channels = " << channels);
        const auto source = gg::PreparedSource::make(makeSine(channels, 4800, 48000.0));
        REQUIRE(source->levelCount() == gg::mipLevelCount(4800));
        REQUIRE(source->levelCount() == 5);
        const gg::PreparedSource::Level &level0 = source->level(0);
        REQUIRE(level0.data[0] == source->buffer().channel(0));
        REQUIRE(level0.data[1] == source->buffer().channel(channels == 1 ? 0 : 1));
        REQUIRE(level0.frames == 4800);
        REQUIRE(level0.stride == 1);
        REQUIRE(level0.interleaved == source->pairs().data());
        REQUIRE(level0.mono == (channels == 1));
        REQUIRE(level0.decimation == 1);
        for (const uint32_t n : {5u, 7u, 0xFFFFFFFFu})
        {
            REQUIRE(&source->level(n) == &level0);
        }
    }

    // The ring's levels (M2 sub-project 2's spec §6.9): 1 + 3 for a 1 s ring; any index past
    // them reads level 0, as does a ring of one level (64 frames: no level holds 64).
    gg::LiveSource live;
    live.prepare(48000.0, 1.0, 256);
    REQUIRE(live.levelCount() == 4);
    gg::LiveSource small;
    small.prepare(64.0, 1.0, 8);
    REQUIRE(small.levelCount() == 1);
    REQUIRE(small.level(1).decimation == 1);
    REQUIRE(small.level(1).view.slots == small.view().slots);
    const gg::LiveSource::RingView view = live.view();
    for (const uint32_t n : {0u, 4u, 7u, 0xFFFFFFFFu})
    {
        INFO("level " << n);
        const gg::LiveSource::Level level = live.level(n);
        REQUIRE(level.view.slots == view.slots);
        REQUIRE(level.view.capacity == view.capacity);
        REQUIRE(level.view.frames == view.frames);
        REQUIRE(level.decimation == 1);
    }
}

TEST_CASE("process splits the overdub gains like the input", "[engine][live]")
{
    // M2 sub-project 2's spec §6.1: process(…, overdubGain) hands the ring one gain per frame,
    // offset like the input when a long call is split into maxBlockSize blocks, so one 3000-frame
    // call and calls of 100 store the same ring bits. Without gains, it is the M1 process().
    gg::EngineConfig config;
    config.maxBlockSize = 256;
    config.liveSeconds = 1.0;
    const auto input = noiseInput(3000, 4);
    std::vector<float> gains(3000);
    for (std::size_t i = 0; i < gains.size(); ++i)
    {
        gains[i] = i % 700 < 350 ? 0.0f : 0.25f + static_cast<float>(i % 7) * 0.1f;
    }
    const auto ringOf = [](const gg::Engine &e) {
        const gg::LiveSource::RingView v = e.live().view();
        return std::vector<float>(v.slots, v.slots + 2 * (v.capacity + 3));
    };
    std::vector<float> left(3000);
    std::vector<float> right(3000);
    float *out[2] = {left.data(), right.data()};

    gg::Engine whole(config);
    whole.live().setLoop(true, 640);
    const float *in[1] = {input.data()};
    whole.process(in, 1, out, 2, 3000, gains.data());

    gg::Engine pieces(config);
    pieces.live().setLoop(true, 640);
    for (std::size_t done = 0; done < 3000; done += 100)
    {
        const float *at[1] = {input.data() + done};
        float *o[2] = {left.data() + done, right.data() + done};
        pieces.process(at, 1, o, 2, 100, gains.data() + done);
    }
    REQUIRE(whole.liveWriteHead() == 3000);
    REQUIRE(ringOf(whole) == ringOf(pieces));

    // The overdub is heard in the ring: it differs from the same input written without gains.
    gg::Engine plain(config);
    plain.live().setLoop(true, 640);
    plain.process(in, 1, out, 2, 3000);
    gg::Engine nullGains(config);
    nullGains.live().setLoop(true, 640);
    nullGains.process(in, 1, out, 2, 3000, nullptr);
    REQUIRE(ringOf(plain) == ringOf(nullGains));
    REQUIRE(ringOf(plain) != ringOf(whole));
}

namespace
{
// The kernels' view of one level of a prepared source, as Engine::slotFor makes it.
gg::kernels::SampleSource viewOf(const gg::PreparedSource::Level &level)
{
    return {{level.data[0], level.data[1]},
            level.frames,
            level.mono,
            level.interleaved,
            level.stride,
            level.decimation};
}

// The scalar reference's render of `grains`, every one HQ, over [0, frames) of both outputs, each
// grain read through renderSampleRef<Sinc> from viewOf(grain, false) over [0, split) and from
// viewOf(grain, true) over [split, frames). Each grain adds to every sample before the next
// starts, as the engine sums them.
template <class ViewOf>
Render hqReference(const std::vector<gg::GrainSpec> &grains, int64_t frames, int64_t split,
                   ViewOf viewOf)
{
    Render r{std::vector<float>(static_cast<std::size_t>(frames), 0.0f),
             std::vector<float>(static_cast<std::size_t>(frames), 0.0f)};
    gg::GrainPool pool(static_cast<uint32_t>(std::max<std::size_t>(1, grains.size())));
    for (const gg::GrainSpec &g : grains)
    {
        REQUIRE(g.tier == gg::Tier::HQ);
        REQUIRE(pool.spawn(g));
    }
    const gg::HannTable hann;
    const gg::SincTable sinc;
    const gg::kernels::Output out{{r.left.data(), r.right.data()}, 0};
    for (uint32_t i = 0; i < pool.size(); ++i)
    {
        gg::kernels::renderSampleRef<gg::kernels::Sinc>(pool.rows(), i, viewOf(grains[i], false),
                                                        out, 0, split, {hann, sinc});
        gg::kernels::renderSampleRef<gg::kernels::Sinc>(pool.rows(), i, viewOf(grains[i], true),
                                                        out, split, frames, {hann, sinc});
    }
    return r;
}
} // namespace

TEST_CASE("HQ grains latch the level of their rate and Normal grains level 0", "[engine][mip]")
{
    // Spec §6.3: level floor(log2 rate), clamped to the source's levels, at HQ alone (D5).
    struct Case
    {
        double rate;
        uint8_t level;
    };
    constexpr Case kCases[] = {{0.5, 0}, {1.0, 0}, {1.9, 0},  {2.0, 1},
                               {3.0, 1}, {4.0, 2}, {16.0, 4}, {40.0, 4}};
    for (const Case &c : kCases)
    {
        INFO("rate " << c.rate);
        REQUIRE(gg::mipLevelFor(c.rate, 5) == c.level);
    }
    // The boundaries are exact, and the clamps hold.
    REQUIRE(gg::mipLevelFor(std::nextafter(2.0, 0.0), 5) == 0);
    REQUIRE(gg::mipLevelFor(std::nextafter(16.0, 0.0), 5) == 3);
    REQUIRE(gg::mipLevelFor(8.0, 2) == 1);
    REQUIRE(gg::mipLevelFor(8.0, 1) == 0);
    REQUIRE(gg::mipLevelFor(8.0, 0) == 0);
    REQUIRE(gg::mipLevelFor(0x1.0p100, 5) == 4);
    REQUIRE(gg::mipLevelFor(0x1.0p100, 200) == 4); // never past kMaxMipLevels − 1
    REQUIRE(gg::mipLevelFor(0.0, 5) == 0);
    REQUIRE(gg::mipLevelFor(-4.0, 5) == 0);
    REQUIRE(gg::mipLevelFor(std::numeric_limits<double>::quiet_NaN(), 5) == 0);
    REQUIRE(gg::mipLevelFor(std::numeric_limits<double>::infinity(), 5) == 4);
    REQUIRE(gg::mipLevelFor(std::numeric_limits<double>::infinity(), 3) == 2);

    // Through the engine: a stereo source of 5 levels whose sample rate makes every grain's rate
    // exactly c.rate at pitch 0 (the pitch ratio 1, times the source's rate over 48 kHz).
    for (const Case &c : kCases)
    {
        for (const gg::Tier tier : {gg::Tier::HQ, gg::Tier::Normal})
        {
            INFO("rate " << c.rate << ", tier " << static_cast<int>(tier));
            const auto source = gg::PreparedSource::make(makeSine(2, 4800, 48000.0 * c.rate));
            REQUIRE(source->levelCount() == 5);
            gg::Engine engine(gg::EngineConfig{});
            engine.setPreparedSource(source);
            gg::CloudParams p = params(100.0, 0.02, 0.0, 0.0, 0.0, 0.5f);
            p.quality = tier;
            engine.setCloudParams(p);
            SinkLog log;
            engine.setEventSink(&SinkLog::record, &log);
            const Render r = render(engine, 4800, 256);
            REQUIRE(allFinite(r.left));
            REQUIRE(allFinite(r.right));
            REQUIRE(peak(r.left) > 0.0f);
            REQUIRE_FALSE(log.overflowed);
            REQUIRE(log.grains.size() == 10);
            for (const gg::GrainSpec &g : log.grains)
            {
                REQUIRE(g.rate == c.rate);
                REQUIRE(g.tier == tier);
                REQUIRE(g.level == (tier == gg::Tier::HQ ? c.level : 0));
            }
        }
    }

    SECTION("an HQ draw the ladder lowers latches level 0")
    {
        // The quota test's 16 Hz of 0.1 s grains, at rate 2: 11 units fit two Normal stereo
        // grains but no HQ one, so every draw is lowered to Normal, and reads level 0.
        const auto source = gg::PreparedSource::make(makeSine(2, 96000, 96000.0));
        gg::CloudParams p = params(16.0, 0.1, 0.0, 0.0, 0.0, 0.5f);
        p.quality = gg::Tier::HQ;
        for (const uint64_t quota : {gg::kUnlimitedQuota, uint64_t{11}})
        {
            INFO("quota " << quota);
            gg::Engine engine(gg::EngineConfig{});
            engine.setPreparedSource(source);
            engine.setCloudParams(p);
            engine.setQuota(quota);
            SinkLog log;
            engine.setEventSink(&SinkLog::record, &log);
            const Render r = render(engine, 48000, 256);
            REQUIRE(allFinite(r.left));
            REQUIRE(log.grains.size() == 16);
            const bool lowered = quota != gg::kUnlimitedQuota;
            REQUIRE(engine.loweredGrains() == (lowered ? 16 : 0));
            for (const gg::GrainSpec &g : log.grains)
            {
                REQUIRE(g.rate == 2.0);
                REQUIRE(g.tier == (lowered ? gg::Tier::Normal : gg::Tier::HQ));
                REQUIRE(g.level == (lowered ? 0 : 1));
            }
        }
    }
}

TEST_CASE("an HQ grain at rate 2 reads level 1 at its half positions", "[engine][mip]")
{
    // Spec §6.3: an HQ grain at rate 2 latches level 1 and reads it at posL = pos / 2, residual
    // rate 1, unstretched. A 96 kHz source at pitch 0 into 48 kHz gives rate 2 exactly. The
    // engine's render is the reference's over level 1, bit for bit, and not its level-0 render.
    for (const uint32_t channels : {1u, 2u})
    {
        INFO("channels = " << channels);
        const auto source =
            gg::PreparedSource::make(makeSine(channels, 48000, 96000.0, 3000.0, 0.8));
        REQUIRE(source->levelCount() == 5);
        const gg::PreparedSource::Level &level1 = source->level(1);
        REQUIRE(level1.decimation == 2);
        REQUIRE(level1.frames == 24000);
        REQUIRE(level1.stride == (channels == 2 ? 2 : 1));
        gg::Engine engine(gg::EngineConfig{});
        engine.setPreparedSource(source);
        gg::CloudParams p = params(50.0, 0.05, 0.4, 0.3, 0.0, 0.5f);
        p.quality = gg::Tier::HQ;
        p.stereo = 0.7;
        engine.setCloudParams(p);
        SinkLog log;
        engine.setEventSink(&SinkLog::record, &log);
        constexpr int64_t kFrames = 24000;
        const Render r = render(engine, kFrames, 256);
        REQUIRE(allFinite(r.left));
        REQUIRE(allFinite(r.right));
        REQUIRE(peak(r.left) > 0.05f);
        REQUIRE_FALSE(log.overflowed);
        REQUIRE(log.grains.size() == 25);
        for (const gg::GrainSpec &g : log.grains)
        {
            REQUIRE(g.rate == 2.0);
            REQUIRE(g.level == 1);
        }
        // The spec's arithmetic, written out over level 1's floats: pos / 2, the sinc unstretched.
        Render expected{std::vector<float>(static_cast<std::size_t>(kFrames), 0.0f),
                        std::vector<float>(static_cast<std::size_t>(kFrames), 0.0f)};
        gg::GrainPool pool(64);
        const gg::HannTable hann;
        const gg::SincTable sinc;
        const gg::kernels::ReadParams unstretched{&sinc, 1.0f};
        const auto read = [&](const float *data, double pos) {
            return level1.stride == 2
                       ? gg::kernels::Sinc::read<2>(data, level1.frames, pos, unstretched)
                       : gg::kernels::Sinc::read<1>(data, level1.frames, pos, unstretched);
        };
        for (const gg::GrainSpec &g : log.grains)
        {
            REQUIRE(pool.spawn(g));
        }
        const gg::GrainRows &rows = pool.rows();
        for (uint32_t i = 0; i < pool.size(); ++i)
        {
            for (int64_t t = rows.onsetSample[i]; t < std::min(kFrames, rows.endSample[i]); ++t)
            {
                const double e = static_cast<double>(t - rows.onsetSample[i]) - rows.onsetFrac[i];
                const float w = hann.at(e * rows.invDuration[i]); // texture 0: the plain Hann
                if (!(w > 0.0f))
                {
                    continue;
                }
                const double half = (rows.startPos[i] + rows.rate[i] * e) / 2.0;
                const float s0 = read(level1.data[0], half);
                const float s1 = level1.mono ? s0 : read(level1.data[1], half);
                const auto j = static_cast<std::size_t>(t);
                expected.left[j] += s0 * (w * rows.gainL[i]);
                expected.right[j] += s1 * (w * rows.gainR[i]);
            }
        }
        REQUIRE(sameBits(r.left, expected.left));
        REQUIRE(sameBits(r.right, expected.right));
        const Render atLevel0 =
            hqReference(log.grains, kFrames, kFrames,
                        [&](const gg::GrainSpec &, bool) { return viewOf(source->level(0)); });
        REQUIRE_FALSE(sameBits(r.left, atLevel0.left));
    }
}

TEST_CASE("a swap to a source with fewer levels reads its top level", "[engine][mip]")
{
    // Review Focus 2 (spec §6.3: the sounding grains keep their level index through a swap). HQ
    // grains at rate 8 latch level 3 of a 5-level source A (a 384 kHz source at pitch 0).
    // Mid-grain, replaceSource hands every sounding grain B, 200 frames and so 2 levels: a grain
    // latched at level 3 then reads B's top level, level 1 (100 frames at decimation 2), at pos
    // / 2. The swap falls 12 samples after an onset, so that grain's posL is still inside B's level
    // 1 (pos 96, posL 48, rising by 4 a sample). The grains from the swap on latch B's own level
    // (rate 1: level 0).
    const auto a = gg::PreparedSource::make(makeSine(2, 4096, 384000.0, 4000.0, 0.8));
    const auto b = gg::PreparedSource::make(makeSine(2, 200, 48000.0, 1000.0, 0.8));
    REQUIRE(a->levelCount() == 5);
    REQUIRE(b->levelCount() == 2);
    gg::CloudParams p = params(200.0, 0.004, 0.0, 0.0, 0.0, 0.5f);
    p.quality = gg::Tier::HQ;
    constexpr int64_t kFrames = 4800;

    // Where the onsets fall: a run on A alone.
    SinkLog dry;
    {
        gg::Engine engine(gg::EngineConfig{});
        engine.setPreparedSource(a);
        engine.setCloudParams(p);
        engine.setEventSink(&SinkLog::record, &dry);
        render(engine, kFrames, 64);
    }
    const auto onset = std::find_if(dry.grains.begin(), dry.grains.end(),
                                    [](const gg::GrainSpec &g) { return g.onsetSample >= 2000; });
    REQUIRE(onset != dry.grains.end());
    const int64_t swap = onset->onsetSample + 12;

    gg::Engine engine(gg::EngineConfig{});
    engine.setPreparedSource(a);
    engine.setCloudParams(p);
    SinkLog log;
    engine.setEventSink(&SinkLog::record, &log);
    Render r = render(engine, swap, 64);
    engine.replaceSource(b.get());
    append(r, render(engine, kFrames - swap, 64));
    REQUIRE(allFinite(r.left));
    REQUIRE(allFinite(r.right));
    REQUIRE_FALSE(log.overflowed);

    bool spansSwap = false;
    for (const gg::GrainSpec &g : log.grains)
    {
        const bool before = g.onsetSample < swap;
        REQUIRE(g.rate == (before ? 8.0 : 1.0));
        REQUIRE(g.level == (before ? 3 : 0));
        spansSwap = spansSwap || (before && gg::grainEndSample(g) > swap);
    }
    REQUIRE(spansSwap);

    // Before the swap each grain reads its level of A; from it on, its level of B, or B's top.
    const Render expected =
        hqReference(log.grains, kFrames, swap, [&](const gg::GrainSpec &g, bool after) {
            return after ? viewOf(b->level(std::min<uint32_t>(g.level, b->levelCount() - 1)))
                         : viewOf(a->level(g.level));
        });
    REQUIRE(sameBits(r.left, expected.left));
    REQUIRE(sameBits(r.right, expected.right));
    // The old grain reads B's level 1 audibly in the samples after the swap: not silence, and
    // not what reading B's level 0 at decimation 1 would give.
    REQUIRE(peak(std::vector<float>(r.left.begin() + swap, r.left.begin() + swap + 12)) > 0.01f);
    const Render atLevel0 =
        hqReference(log.grains, kFrames, swap, [&](const gg::GrainSpec &g, bool after) {
            return after ? viewOf(b->level(0)) : viewOf(a->level(g.level));
        });
    REQUIRE_FALSE(sameBits(r.left, atLevel0.left));
}

namespace
{
// The steals and spawns in the order the engine reports them, recorded without allocating.
struct OrderLog
{
    struct Event
    {
        bool steal;
        uint64_t index;
        int64_t onset;
        gg::Tier tier;
    };
    static constexpr std::size_t kCapacity = 4096;
    std::vector<Event> events;
    bool overflowed{false};

    OrderLog() { events.reserve(kCapacity); }

    void push(const Event &e)
    {
        if (events.size() < kCapacity)
        {
            events.push_back(e);
        }
        else
        {
            overflowed = true;
        }
    }
    static void spawn(void *ctx, uint32_t, const gg::GrainSpec &g)
    {
        static_cast<OrderLog *>(ctx)->push({false, g.index, g.onsetSample, g.tier});
    }
    static void steal(void *ctx, uint32_t, uint64_t index, int64_t onset)
    {
        static_cast<OrderLog *>(ctx)->push({true, index, onset, gg::Tier::Normal});
    }
    uint64_t count(bool steals) const
    {
        return static_cast<uint64_t>(std::count_if(
            events.begin(), events.end(), [steals](const Event &e) { return e.steal == steals; }));
    }
};

// 200 Hz of one-second grains, periodic and centred: an onset every 240 samples, each grain
// sounding 48,002 samples (durationSamples 48,000), so 200 are wanted at once.
gg::CloudParams overload() { return params(200.0, 1.0, 0.5, 0.0, 0.0, 0.25f); }
} // namespace

TEST_CASE("the quota lowers the tier before it drops", "[engine][budget]")
{
    // 16 Hz of 0.1 s grains: an onset every 3,000 samples and each grain 4,802 long, so exactly
    // one other grain sounds at every onset after the first. The draws are HQ; no quota below
    // 27 units fits an HQ stereo grain, so each is lowered to the highest tier that fits.
    gg::CloudParams p = params(16.0, 0.1, 0.5, 0.0, 0.0, 0.5f);
    p.quality = gg::Tier::HQ;
    const auto run = [&](uint32_t channels, uint64_t quota, SinkLog &log) {
        gg::Engine engine(gg::EngineConfig{});
        engine.setSource(makeSine(channels, 96000, 48000.0));
        engine.setCloudParams(p);
        engine.setQuota(quota);
        engine.setEventSink(&SinkLog::record, &log);
        const Render r = render(engine, 96000, 256);
        REQUIRE(allFinite(r.left));
        REQUIRE(peak(r.left) > 0.0f);
        REQUIRE_FALSE(log.overflowed);
        REQUIRE(log.grains.size() == 32);
        REQUIRE(engine.loweredGrains() == 32);
        REQUIRE(engine.droppedGrains() == 0);
        REQUIRE(engine.stolenGrains() == 0);
    };

    SECTION("stereo, 11 units: two Normal grains fit, one HQ grain doesn't")
    {
        SinkLog log;
        run(2, 11, log);
        for (const gg::GrainSpec &g : log.grains)
        {
            REQUIRE(g.tier == gg::Tier::Normal);
            REQUIRE(g.weight == gg::grainWeight(gg::Tier::Normal, true));
        }
    }
    SECTION("stereo, 7 units: Normal, then Eco beside it, in turn")
    {
        // Grain 0 finds 0 units sounding (Normal, 4), grain 1 finds 4 (Normal would be 8, Eco is
        // 7), grain 2 finds grain 1's 3 (Normal, 7), and so on.
        SinkLog log;
        run(2, 7, log);
        for (std::size_t k = 0; k < log.grains.size(); ++k)
        {
            INFO("grain " << k);
            const gg::Tier t = k % 2 == 0 ? gg::Tier::Normal : gg::Tier::Eco;
            REQUIRE(log.grains[k].tier == t);
            REQUIRE(log.grains[k].weight == gg::grainWeight(t, true));
        }
    }
    SECTION("mono, 8 units: a mono HQ grain is 20, two mono Normal grains 6")
    {
        SinkLog log;
        run(1, 8, log);
        for (const gg::GrainSpec &g : log.grains)
        {
            REQUIRE(g.tier == gg::Tier::Normal);
            REQUIRE(g.weight == gg::grainWeight(gg::Tier::Normal, false));
        }
    }
}

TEST_CASE("a spent quota drops while the oldest grain is young and steals after half",
          "[engine][budget]")
{
    // A quota of 10 Normal stereo grains (40 units). Grains 0 to 9 fill it; Eco (3) doesn't fit
    // beside them. Grains 10 to 99 find the oldest grain, grain 0, less than half-way, and drop.
    // Grain 100, at 24,000, finds grain 0 exactly half-way and steals it: grain 0 now ends at
    // 24,256 and keeps its weight in the ledger until then (spec §6.5), so grain 101, at 24,240,
    // finds 44 units, more than rungs 1 and 2 allow; but rung 3 leaves the 4 fading units out
    // (ruling ES-T2a), so it steals grain 1. So grains 100 to 109 steal grains 0 to 9, one each,
    // at every onset; then the oldest unstolen grain is grain 100, young again, and grains 110
    // to 199 drop.
    gg::Engine engine(gg::EngineConfig{});
    engine.setSource(makeSine(2, 96000, 48000.0));
    engine.setCloudParams(overload());
    engine.setQuota(40);
    OrderLog log;
    engine.setEventSink(&OrderLog::spawn, &log);
    engine.setStealSink(&OrderLog::steal, &log);

    render(engine, 24000, 100);
    REQUIRE(log.count(false) == 10);
    REQUIRE(engine.droppedGrains() == 90);
    REQUIRE(engine.stolenGrains() == 0);

    const Render r = render(engine, 24000, 100);
    REQUIRE(allFinite(r.left));
    REQUIRE_FALSE(log.overflowed);
    REQUIRE(log.count(false) == 20);
    REQUIRE(log.count(true) == 10);
    REQUIRE(engine.stolenGrains() == 10);
    REQUIRE(engine.droppedGrains() == 180);
    REQUIRE(engine.loweredGrains() == 0);
    // Each steal is one admission's: grain 100 + j steals grain j at its onset.
    std::size_t j = 0;
    for (std::size_t e = 0; e < log.events.size(); ++e)
    {
        if (log.events[e].steal)
        {
            INFO("steal " << j);
            REQUIRE(log.events[e].index == j);
            REQUIRE(log.events[e].onset == static_cast<int64_t>(24000 + 240 * j));
            REQUIRE(e + 1 < log.events.size());
            REQUIRE_FALSE(log.events[e + 1].steal);
            REQUIRE(log.events[e + 1].index == 100 + j);
            REQUIRE(log.events[e + 1].tier == gg::Tier::Normal);
            ++j;
        }
    }
    REQUIRE(j == 10);

    engine.reset();
    REQUIRE(engine.stolenGrains() == 0);
    REQUIRE(engine.loweredGrains() == 0);
    REQUIRE(engine.droppedGrains() == 0);
}

TEST_CASE("under a spent quota the steals after half run at the onset rate", "[engine][budget]")
{
    // A quota of 50 Normal stereo grains (200 units): grains 0 to 49 fill it by 11,760, and
    // grains 50 to 99 drop. Over the 0.25 s from 0.5 s, each of the 50 onsets finds the oldest
    // unstolen grain half-way and steals it, although the grains stolen in the last 256 samples
    // still fade in the ledger (ruling ES-T2a): one steal per admission, at the onset rate, and
    // no drop.
    gg::Engine engine(gg::EngineConfig{});
    engine.setSource(makeSine(2, 96000, 48000.0));
    engine.setCloudParams(overload());
    engine.setQuota(200);
    render(engine, 24000, 128);
    REQUIRE(engine.stolenGrains() == 0);
    REQUIRE(engine.droppedGrains() == 50);
    SinkLog log;
    engine.setEventSink(&SinkLog::record, &log);
    const uint64_t droppedBefore = engine.droppedGrains();
    render(engine, 12000, 128);
    REQUIRE_FALSE(log.overflowed);
    REQUIRE(log.grains.size() == 50);
    REQUIRE(engine.stolenGrains() == log.grains.size());
    REQUIRE(engine.droppedGrains() == droppedBefore);
    REQUIRE(engine.loweredGrains() == 0);
}

TEST_CASE("the capacity drops or steals like a spent quota", "[engine][budget]")
{
    // Capacity 32, quota unlimited: grains 0 to 31 fill the pool and 32 to 99 drop while grain 0
    // is young. From grain 100 each onset steals the oldest unstolen grain: a stolen grain still
    // sounds, but rung 3 doesn't ask for capacity, only for weight, and the quota is unlimited.
    // So grains 100 to 131 steal grains 0 to 31, one each, and 132 to 199 drop.
    gg::EngineConfig config;
    config.grainCapacity = 32;
    gg::Engine engine(config);
    engine.setSource(makeSine(2, 96000, 48000.0));
    engine.setCloudParams(overload());
    OrderLog log;
    engine.setEventSink(&OrderLog::spawn, &log);
    engine.setStealSink(&OrderLog::steal, &log);

    render(engine, 24000, 64);
    REQUIRE(log.count(false) == 32);
    REQUIRE(engine.droppedGrains() == 68);
    REQUIRE(engine.stolenGrains() == 0);

    render(engine, 24000, 64);
    REQUIRE_FALSE(log.overflowed);
    REQUIRE(log.count(false) == 64);
    REQUIRE(engine.stolenGrains() == 32);
    REQUIRE(engine.droppedGrains() == 136);
    REQUIRE(engine.loweredGrains() == 0);
}

TEST_CASE("a steal sink reports each steal before the spawn", "[engine][budget]")
{
    // The capacity case above: every steal is reported, with the stolen grain's index and the
    // onset of the grain it makes room for, immediately before that grain's spawn.
    gg::EngineConfig config;
    config.grainCapacity = 32;
    config.layer = 2;
    gg::Engine engine(config);
    engine.setSource(makeSine(2, 96000, 48000.0));
    engine.setCloudParams(overload());
    OrderLog log;
    engine.setEventSink(&OrderLog::spawn, &log);
    struct Layers
    {
        OrderLog *log;
        bool layerTwo{true};
        static void steal(void *ctx, uint32_t layer, uint64_t index, int64_t onset)
        {
            auto *self = static_cast<Layers *>(ctx);
            self->layerTwo = self->layerTwo && layer == 2;
            OrderLog::steal(self->log, layer, index, onset);
        }
    } layers{&log};
    engine.setStealSink(&Layers::steal, &layers);
    render(engine, 48000, 1000);
    REQUIRE_FALSE(log.overflowed);
    REQUIRE(layers.layerTwo);

    uint64_t steals = 0;
    for (std::size_t e = 0; e < log.events.size(); ++e)
    {
        if (!log.events[e].steal)
        {
            continue;
        }
        INFO("event " << e);
        REQUIRE(log.events[e].index == steals);
        REQUIRE(e + 1 < log.events.size());
        REQUIRE_FALSE(log.events[e + 1].steal);
        REQUIRE(log.events[e + 1].onset == log.events[e].onset);
        REQUIRE(log.events[e + 1].index == 100 + steals);
        ++steals;
    }
    REQUIRE(steals == 32);
    REQUIRE(engine.stolenGrains() == 32);

    // Removing the sink stops the reports, not the steals.
    engine.setStealSink(nullptr, nullptr);
    engine.reset();
    const std::size_t reported = log.events.size();
    engine.setEventSink(nullptr, nullptr);
    render(engine, 48000, 1000);
    REQUIRE(log.events.size() == reported);
    REQUIRE(engine.stolenGrains() == 32);
}

TEST_CASE("grainCapacity above 16384 is refused", "[engine][budget]")
{
    REQUIRE(gg::kMaxGrainCapacity == 16384);
    for (const uint32_t bad : {gg::kMaxGrainCapacity + 1, uint32_t{1} << 20})
    {
        INFO("grainCapacity " << bad);
        gg::EngineConfig config;
        config.grainCapacity = bad;
        REQUIRE_THROWS_AS(gg::Engine(config), std::invalid_argument);
    }
    gg::EngineConfig config;
    config.grainCapacity = gg::kMaxGrainCapacity;
    REQUIRE_NOTHROW(gg::Engine(config));
}

TEST_CASE("an unlimited quota admits as M1", "[engine][budget]")
{
    // The M1 saturation test's stream (400 Hz of 50 ms grains, about 20 wanted at once).
    const auto source = makeSine(2, 48000, 48000.0);
    const auto p = params(400.0, 0.05, 0.5, 0.5, -5.0, 0.7f);
    struct Run
    {
        Render audio;
        SinkLog log;
        uint64_t dropped{0}, lowered{0}, stolen{0};
    };
    const auto run = [&](uint32_t capacity, bool setUnlimited, Run &r) {
        gg::EngineConfig config;
        config.grainCapacity = capacity;
        config.seed = 11;
        gg::Engine engine(config);
        engine.setSource(source);
        engine.setCloudParams(p);
        if (setUnlimited)
        {
            engine.setQuota(gg::kUnlimitedQuota);
        }
        engine.setEventSink(&SinkLog::record, &r.log);
        r.audio = render(engine, 24000, 333);
        r.dropped = engine.droppedGrains();
        r.lowered = engine.loweredGrains();
        r.stolen = engine.stolenGrains();
    };

    SECTION("with room, every scheduled grain is admitted at its draw's tier")
    {
        Run r;
        run(4096, false, r);
        REQUIRE_FALSE(r.log.overflowed);
        REQUIRE(r.log.grains.size() == 200); // 0.5 s at 400 Hz
        for (std::size_t k = 0; k < r.log.grains.size(); ++k)
        {
            REQUIRE(r.log.grains[k].index == k);
            REQUIRE(r.log.grains[k].tier == gg::Tier::Normal);
            REQUIRE(r.log.grains[k].weight == gg::grainWeight(gg::Tier::Normal, true));
        }
        REQUIRE(r.dropped == 0);
        REQUIRE(r.lowered == 0);
        REQUIRE(r.stolen == 0);
    }
    SECTION("no quota set is the unlimited quota, at room and at capacity")
    {
        for (const uint32_t capacity : {4096u, 4u})
        {
            INFO("capacity " << capacity);
            Run byDefault;
            Run unlimited;
            run(capacity, false, byDefault);
            run(capacity, true, unlimited);
            REQUIRE(sameBits(byDefault.audio.left, unlimited.audio.left));
            REQUIRE(sameBits(byDefault.audio.right, unlimited.audio.right));
            REQUIRE(byDefault.log.grains == unlimited.log.grains);
            REQUIRE(byDefault.dropped == unlimited.dropped);
            REQUIRE(byDefault.stolen == unlimited.stolen);
            REQUIRE(byDefault.lowered == 0);
            REQUIRE(unlimited.lowered == 0);
        }
    }
}

namespace
{
// The goldens' click limit (gg::metrics::Tolerances::maxClickScore, metricsVersion 2).
constexpr double kClickLimit = 15.0;

// The goldens' click detector over a stereo render at 48 kHz.
double clickScore(const Render &r)
{
    const gg::metrics::AudioData audio{48000.0, {r.left, r.right}};
    return gg::metrics::computeMetrics(audio).clickScore;
}

// The lowest RMS, in dBFS, of any whole window of `window` samples of v from its start.
double minWindowRmsDb(const std::vector<float> &v, int64_t window)
{
    double lowest = std::numeric_limits<double>::infinity();
    for (int64_t w = 0; w + window <= static_cast<int64_t>(v.size()); w += window)
    {
        double sum = 0.0;
        for (int64_t t = w; t < w + window; ++t)
        {
            const double x = v[static_cast<std::size_t>(t)];
            sum += x * x;
        }
        lowest = std::min(lowest, 10.0 * std::log10(sum / static_cast<double>(window)));
    }
    return lowest;
}

// The overload ladder's exit case (spec §6.5) at a size: `densityHz` periodic onsets of
// `sizeSec` grains into one engine of capacity `capacity` at a quota of `quotaGrains` Normal
// stereo grains, over a full-scale 220 Hz stereo sine of 4 s, for `frames` samples, its RMS
// floor taken over windows of `window` samples. The grains read from 0.3 to 0.7 of the source
// (spray 0.2), so their sine phases differ and their sum doesn't cancel; at rate 1 none reads
// past its end.
struct ExitCase
{
    uint32_t capacity;
    uint64_t quotaGrains;
    double densityHz;
    double sizeSec;
    int64_t frames;
    int64_t window;
};

struct ExitRender
{
    Render audio;
    uint64_t dropped{0};
    uint64_t lowered{0};
    uint64_t stolen{0};
};

ExitRender renderExit(const ExitCase &c, const std::shared_ptr<gg::SampleBuffer> &source,
                      uint32_t blockSize)
{
    gg::EngineConfig config;
    config.grainCapacity = c.capacity;
    gg::Engine engine(config);
    engine.setSource(source);
    engine.setCloudParams(params(c.densityHz, c.sizeSec, 0.5, 0.2, 0.0, 0.05f));
    engine.setQuota(c.quotaGrains * gg::grainWeight(gg::Tier::Normal, true));
    ExitRender r;
    r.audio = render(engine, c.frames, blockSize);
    r.dropped = engine.droppedGrains();
    r.lowered = engine.loweredGrains();
    r.stolen = engine.stolenGrains();
    return r;
}

// Every sample finite, every window's RMS above −60 dBFS, the click score below the goldens'
// limit, and the same bytes and counts at block sizes 1, 32, 64, 511 and 4096.
void checkExitCase(const ExitCase &c)
{
    const auto source = makeSine(2, 4 * 48000, 48000.0, 220.0, 1.0);
    const ExitRender ref = renderExit(c, source, 4096);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(ref.stolen > 0);
    const double floorDb = std::min(minWindowRmsDb(ref.audio.left, c.window),
                                    minWindowRmsDb(ref.audio.right, c.window));
    const double click = clickScore(ref.audio);
    INFO("window RMS floor " << floorDb << " dBFS, click score " << click << ", stolen "
                             << ref.stolen << ", dropped " << ref.dropped);
    REQUIRE(floorDb > -60.0);
    REQUIRE(click < kClickLimit);
    for (const uint32_t blockSize : {1u, 32u, 64u, 511u})
    {
        INFO("blocks of " << blockSize);
        const ExitRender r = renderExit(c, source, blockSize);
        REQUIRE(sameBits(r.audio.left, ref.audio.left));
        REQUIRE(sameBits(r.audio.right, ref.audio.right));
        REQUIRE(r.dropped == ref.dropped);
        REQUIRE(r.lowered == ref.lowered);
        REQUIRE(r.stolen == ref.stolen);
    }
}

// The steals the engine reports, recorded without allocating.
struct StealLog
{
    struct Steal
    {
        uint64_t index;
        int64_t onset;
    };
    std::vector<Steal> steals;
    bool overflowed{false};

    StealLog() { steals.reserve(4096); }

    static void record(void *ctx, uint32_t, uint64_t index, int64_t onset)
    {
        auto *log = static_cast<StealLog *>(ctx);
        if (log->steals.size() < log->steals.capacity())
        {
            log->steals.push_back({index, onset});
        }
        else
        {
            log->overflowed = true;
        }
    }
};
} // namespace

// The exit case at full size (spec §6.5): 16,384 one-second grains a second into capacity 16,384
// at a quota of 1,024 grains, for 2 s, its floor over 100 ms windows. Five renders of over a
// thousand grains each take a minute or two in Debug, so it is slow; its small sibling below
// always runs.
TEST_CASE("the exit case never clicks or drops out", "[engine][budget][slow]")
{
    checkExitCase({gg::kMaxGrainCapacity, 1024, 16384.0, 1.0, 96000, 4800});
}

TEST_CASE("the exit case never clicks or drops out at a small size", "[engine][budget]")
{
    // 4,096 grains of 100 ms a second into capacity 4,096 at a quota of 256 grains, for 0.5 s,
    // its floor over 50 ms windows.
    checkExitCase({4096, 256, 4096.0, 0.1, 24000, 2400});
}

TEST_CASE("stealing a short grain keeps its end and does not fade", "[engine][budget]")
{
    // Review Focus 4, ruling ES-T6a. 375 Hz of 5 ms grains (240 samples, each sounding 242) at a
    // quota of one Normal stereo grain: an onset every 128 samples, past the sounding grain's
    // half-way (120), so each grain after the first steals the one before, which has 242 − 128 =
    // 114 samples left. That is no more than kStealFade, so the stolen grain already ends no
    // later than a fade would: it keeps its natural end and its window, and no fade factor is
    // applied. The steal still counts, is reported, and the draw is admitted. The grains read
    // from 0.3 to 0.7 of a full-scale 220 Hz sine (spray 0.2), so the steals fall on many phases.
    constexpr float kGain = 0.5f;
    constexpr int64_t kFrames = 24000;
    const auto source = makeSine(2, 96000, 48000.0, 220.0, 1.0);
    gg::Engine engine(gg::EngineConfig{});
    engine.setSource(source);
    engine.setCloudParams(params(375.0, 0.005, 0.5, 0.2, 0.0, kGain));
    engine.setQuota(gg::grainWeight(gg::Tier::Normal, true));
    SinkLog spawns;
    StealLog log;
    engine.setEventSink(&SinkLog::record, &spawns);
    engine.setStealSink(&StealLog::record, &log);
    const Render r = render(engine, kFrames, 64);
    REQUIRE(allFinite(r.left));
    REQUIRE(allFinite(r.right));
    REQUIRE_FALSE(spawns.overflowed);
    REQUIRE_FALSE(log.overflowed);
    REQUIRE(spawns.grains.size() == static_cast<std::size_t>((kFrames + 127) / 128));
    REQUIRE(engine.droppedGrains() == 0);
    REQUIRE(engine.loweredGrains() == 0);
    REQUIRE(engine.stolenGrains() == spawns.grains.size() - 1);
    REQUIRE(log.steals.size() == engine.stolenGrains());

    // Through the event log: grain s is stolen at grain s + 1's onset, with 114 samples left.
    for (std::size_t s = 0; s < log.steals.size(); ++s)
    {
        INFO("steal " << s);
        REQUIRE(log.steals[s].index == s);
        REQUIRE(spawns.grains[s].index == s);
        REQUIRE(spawns.grains[s + 1].onsetSample == log.steals[s].onset);
        REQUIRE(gg::grainEndSample(spawns.grains[s]) - log.steals[s].onset == 114);
    }

    // The whole render written out from the log, every grain to its natural end with no fade
    // factor: e, w = hann(e × (1 / D)) (skip unless > 0), pos = startPos + rate × e, then
    // dst[c][t] += hermite4(channel c, pos) × (w × gain_c), in spawn order. The engine's output
    // is that, bit for bit.
    const gg::HannTable hann;
    Render expected{std::vector<float>(static_cast<std::size_t>(kFrames), 0.0f),
                    std::vector<float>(static_cast<std::size_t>(kFrames), 0.0f)};
    for (const gg::GrainSpec &g : spawns.grains)
    {
        const gg::PanGains pan = gg::panGains(g.pan);
        const float gains[2] = {g.gain * pan.left, g.gain * pan.right};
        const double invDuration = 1.0 / g.durationSamples;
        const gg::EnvelopeShape envelope = gg::envelopeShape(g.texture, g.durationSamples);
        for (int64_t t = g.onsetSample; t < std::min(gg::grainEndSample(g), kFrames); ++t)
        {
            const double e = static_cast<double>(t - g.onsetSample) - g.onsetFrac;
            const float w = hann.at(gg::warpPhase(e * invDuration, envelope));
            if (!(w > 0.0f))
            {
                continue;
            }
            const double pos = g.startPos + g.rate * e;
            const auto j = static_cast<std::size_t>(t);
            expected.left[j] +=
                gg::interp::hermite4<>(source->channel(0), source->frames(), pos) * (w * gains[0]);
            expected.right[j] +=
                gg::interp::hermite4<>(source->channel(1), source->frames(), pos) * (w * gains[1]);
        }
    }
    REQUIRE(sameBits(r.left, expected.left));
    REQUIRE(sameBits(r.right, expected.right));

    // The output: at most two grains sound at once, each of gain kGain at the centre, with a
    // window whose step per sample is at most π / D, reading a sine whose own step is at most
    // ω = 2π × 220 / 48,000. So no output sample moves by more than 2 × kGain × (π / D + ω): no
    // grain steps, at its steal or its end.
    const double omega = 2.0 * std::numbers::pi * 220.0 / 48000.0;
    const double stepBound = 2.0 * kGain * (std::numbers::pi / 240.0 + omega) * 1.01;
    double largest = 0.0;
    for (const std::vector<float> *channel : {&r.left, &r.right})
    {
        for (std::size_t t = 1; t < channel->size(); ++t)
        {
            largest = std::max(largest, std::abs(static_cast<double>((*channel)[t]) -
                                                 static_cast<double>((*channel)[t - 1])));
        }
    }
    INFO("largest step " << largest << ", bound " << stepBound);
    REQUIRE(largest <= stepBound);
    const double click = clickScore(r);
    INFO("click score " << click);
    REQUIRE(click < kClickLimit);
}
