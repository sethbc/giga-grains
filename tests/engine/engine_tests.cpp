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
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Interpolate.h"
#include "gg/core/MathDet.h"
#include "gg/core/Rng.h"
#include "gg/core/Window.h"
#include "gg/engine/Engine.h"
#include "gg/grain/GrainKernels.h"
#include "gg/grain/GrainPool.h"
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
    const gg::kernels::Output out{{r.left.data(), r.right.data()}, 0};
    for (uint32_t i = 0; i < pool.size(); ++i)
    {
        const gg::SampleBuffer &buf = sourceOf(grains[i]);
        REQUIRE(buf.channels() == 1);
        const gg::kernels::SampleSource src{
            {buf.channel(0), buf.channel(0)}, buf.frames(), true, nullptr};
        gg::kernels::renderSampleRef<gg::kernels::Hermite>(pool.rows(), i, src, out, 0, frames,
                                                           hann);
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
