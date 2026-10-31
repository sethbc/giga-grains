// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The live ring source (spec §6.3): the FX writes its input into a 16 s ring before any grain
// reads it. The admission clamp keeps every grain's reads, over its whole life, at least M = 256
// frames behind the write head and M frames inside the oldest data; the head-proximity fade
// covers what admission can't foresee (a freeze that stops the head); freeze ramps the write gain
// over 20 ms and then stops the head.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Interpolate.h"
#include "gg/core/Rng.h"
#include "gg/engine/Engine.h"
#include "gg/metrics/AudioData.h"
#include "gg/metrics/Metrics.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/RingStorage.h"
#include "gg/source/SampleBuffer.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kRingSeconds = 16.0;
// Spec §6.3 and the Global Constraints: the head-proximity margin and fade length.
constexpr double kM = 256.0;

gg::EngineConfig liveConfig(double sampleRate = kSampleRate, uint32_t maxBlockSize = 4096,
                            double liveSeconds = kRingSeconds)
{
    gg::EngineConfig c;
    c.sampleRate = sampleRate;
    c.maxBlockSize = maxBlockSize;
    c.grainCapacity = 4096;
    c.seed = 11;
    c.liveSeconds = liveSeconds;
    return c;
}

// N: ceil(liveSeconds × sampleRate) frames.
int64_t ringFrames(double sampleRate, double liveSeconds = kRingSeconds)
{
    return static_cast<int64_t>(std::ceil(liveSeconds * sampleRate));
}

// The freeze write-gain ramp: round(0.02 × sampleRate) samples.
int64_t rampFrames(double sampleRate) { return std::llround(0.02 * sampleRate); }

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

// Uniform noise in [-amp, amp) from gg::rng, a different stream per channel.
Stereo noiseInput(int64_t frames, uint64_t seed, float amp = 0.5f)
{
    Stereo s{std::vector<float>(static_cast<std::size_t>(frames)),
             std::vector<float>(static_cast<std::size_t>(frames))};
    for (int64_t i = 0; i < frames; ++i)
    {
        using gg::rng::Field;
        const auto at = static_cast<std::size_t>(i);
        s.left[at] = amp * static_cast<float>(gg::rng::bipolar(gg::rng::hash(
                               {seed, 0, 0, Field::SignalNoise, static_cast<uint64_t>(i)})));
        s.right[at] = amp * static_cast<float>(gg::rng::bipolar(gg::rng::hash(
                                {seed, 0, 1, Field::SignalNoise, static_cast<uint64_t>(i)})));
    }
    return s;
}

// A sine at hz, the same in both channels.
Stereo sineInput(int64_t frames, double sampleRate, double hz, float amp)
{
    Stereo s{std::vector<float>(static_cast<std::size_t>(frames)),
             std::vector<float>(static_cast<std::size_t>(frames))};
    for (int64_t i = 0; i < frames; ++i)
    {
        const auto v =
            static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * hz * i / sampleRate));
        s.left[static_cast<std::size_t>(i)] = v;
        s.right[static_cast<std::size_t>(i)] = v;
    }
    return s;
}

// Records spawned grains into preallocated storage: the sink runs inside process().
struct SinkLog
{
    static constexpr std::size_t kCapacity = 1 << 16;
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

// A freeze setting that takes effect at output sample `at`.
struct FreezeAt
{
    int64_t at;
    bool freeze;
};

// Runs the input through the engine in process() calls of `block` frames, each cut short at the
// next freeze change, which is made between calls at its sample. The output buffers start as NaN,
// so a sample process() didn't write fails allFinite().
Stereo run(gg::Engine &engine, const Stereo &input, uint32_t block,
           const std::vector<FreezeAt> &changes = {})
{
    const auto frames = static_cast<int64_t>(input.left.size());
    constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
    Stereo out{std::vector<float>(input.left.size(), kUnwritten),
               std::vector<float>(input.left.size(), kUnwritten)};
    std::size_t next = 0;
    for (int64_t done = 0; done < frames;)
    {
        while (next < changes.size() && changes[next].at == done)
        {
            engine.setFreeze(changes[next++].freeze);
        }
        int64_t end = std::min<int64_t>(done + block, frames);
        if (next < changes.size())
        {
            end = std::min(end, changes[next].at);
        }
        const auto n = static_cast<uint32_t>(end - done);
        const float *in[2] = {input.left.data() + done, input.right.data() + done};
        float *o[2] = {out.left.data() + done, out.right.data() + done};
        engine.process(in, 2, o, 2, n);
        done = end;
    }
    return out;
}

bool allFinite(const std::vector<float> &v)
{
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

float peak(const std::vector<float> &v, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    float p = 0.0f;
    for (std::size_t i = from; i < std::min(to, v.size()); ++i)
    {
        p = std::max(p, std::abs(v[i]));
    }
    return p;
}

double rms(const std::vector<float> &v, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
    {
        sum += static_cast<double>(v[i]) * v[i];
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

bool sameBits(const std::vector<float> &a, const std::vector<float> &b, std::size_t from = 0,
              std::size_t to = SIZE_MAX)
{
    if (a.size() != b.size())
    {
        return false;
    }
    to = std::min(to, a.size());
    return from >= to ||
           std::memcmp(a.data() + from, b.data() + from, (to - from) * sizeof(float)) == 0;
}

// The largest |x[t] − x[t−1]|.
float largestStep(const std::vector<float> &v)
{
    float step = 0.0f;
    for (std::size_t i = 1; i < v.size(); ++i)
    {
        step = std::max(step, std::abs(v[i] - v[i - 1]));
    }
    return step;
}

// The most grains whose windows [onset, onset + duration) overlap at any instant.
std::size_t largestOverlap(const std::vector<gg::GrainSpec> &grains)
{
    std::vector<std::pair<double, int>> edges;
    for (const auto &g : grains)
    {
        const double onset = static_cast<double>(g.onsetSample) + g.onsetFrac;
        edges.emplace_back(onset, +1);
        edges.emplace_back(onset + g.durationSamples, -1);
    }
    std::sort(edges.begin(), edges.end()); // an end sorts before a start at the same instant
    std::size_t now = 0;
    std::size_t most = 0;
    for (const auto &[at, delta] : edges)
    {
        now = delta > 0 ? now + 1 : now - 1;
        most = std::max(most, now);
    }
    return most;
}

// The slack, in frames, of each bound for a read at pos while the head is at R (both in ring
// time): how far the furthest tap ahead (pos + 2) is behind R − 1 − M, and how far the furthest
// tap back (pos − 1) is inside R − N + M. Negative means the read breaks the bound.
struct Slack
{
    double head;
    double tail;
};
Slack slackAt(double pos, double head, double ringFrames)
{
    return {(head - 1.0 - kM) - (pos + 2.0), (pos - 1.0) - (head - ringFrames + kM)};
}
} // namespace

TEST_CASE("the live constants are the spec's", "[live]")
{
    REQUIRE(gg::LiveSource::kHeadMargin == 256);
    REQUIRE(gg::LiveSource::kFreezeRampSeconds == 0.02);
    REQUIRE(gg::LiveSource::freezeRampSamples(48000.0) == 960);
    REQUIRE(gg::LiveSource::freezeRampSamples(44100.0) == 882);
    REQUIRE(gg::LiveSource::freezeRampSamples(96000.0) == 1920);

    gg::Engine engine(liveConfig());
    REQUIRE(engine.liveFrames() == 768000); // 16 s at 48 kHz
    gg::Engine noRing(gg::EngineConfig{});
    REQUIRE(noRing.liveFrames() == 0);
    gg::Engine odd(liveConfig(44100.0, 4096, 0.1));
    REQUIRE(odd.liveFrames() == 4410);

    for (const double bad : {-1.0, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(), 1e9})
    {
        INFO("liveSeconds " << bad);
        REQUIRE_THROWS_AS(gg::Engine(liveConfig(kSampleRate, 4096, bad)), std::invalid_argument);
    }
    // The pool stores 2 × grainCapacity grains in uint32 slots, so the capacity must be below
    // 2^31 (thrown before anything is allocated).
    for (const uint32_t bad : {uint32_t{1} << 31, std::numeric_limits<uint32_t>::max()})
    {
        INFO("grainCapacity " << bad);
        gg::EngineConfig config;
        config.grainCapacity = bad;
        REQUIRE_THROWS_AS(gg::Engine(config), std::invalid_argument);
    }
}

TEST_CASE("the ring stores each frame with the freeze write gain", "[live]")
{
    // LiveSource directly, at 1 kHz: N = 1000 frames, L = 20, blocks of at most 64 frames. A read
    // at a whole-number position returns that frame exactly (Catmull-Rom passes through its
    // second tap at t = 0).
    gg::LiveSource ring;
    ring.prepare(1000.0, 1.0, 64);
    REQUIRE(ring.frames() == 1000);
    const int64_t ramp = gg::LiveSource::freezeRampSamples(1000.0);
    REQUIRE(ramp == 20);
    std::vector<float> ones(100, 1.0f);
    const float *in[1] = {ones.data()};

    ring.write(in, 1, 0, 0, 50); // gain 1
    REQUIRE(ring.head() == 50);
    REQUIRE(std::isinf(ring.headRunAfter(10)));
    ring.setFrozen(true);
    ring.write(in, 1, 0, 50, 30); // 20 frames ramping down, then the head stops
    REQUIRE(ring.head() == 70);
    REQUIRE(ring.headAt(50) == 51);
    REQUIRE(ring.headAt(69) == 70);
    REQUIRE(ring.headAt(79) == 70);
    REQUIRE(ring.headRunAfter(50) == 19.0);
    REQUIRE(ring.headRunAfter(68) == 1.0);
    REQUIRE(ring.headRunAfter(69) == 0.0);
    REQUIRE(ring.headRunAfter(79) == 0.0);
    ring.write(in, 1, 0, 80, 10); // stays stopped
    REQUIRE(ring.head() == 70);
    REQUIRE(ring.headAt(85) == 70);
    ring.setFrozen(false);
    ring.write(in, 1, 0, 90, 10); // resumes at once, ramping up
    REQUIRE(ring.head() == 80);
    REQUIRE(ring.headAt(90) == 71);

    const auto gainOf = [&](int64_t frame) {
        if (frame < 50)
        {
            return 1.0f;
        }
        const int64_t r = frame < 70 ? 69 - frame : frame - 69; // 19/20 .. 0, then 1/20 ..
        return static_cast<float>(r) / static_cast<float>(ramp);
    };
    gg::LiveSource::Cursor cursor;
    bool exact = true;
    for (int64_t frame = 1; frame <= 77; ++frame) // taps frame − 1 .. frame + 2 <= head − 1
    {
        float left = -1.0f;
        float right = -1.0f;
        exact = exact && ring.read(static_cast<double>(frame), 80, cursor, left, right) &&
                left == gainOf(frame) && right == gainOf(frame); // mono fills both channels
    }
    REQUIRE(exact);
    REQUIRE(gainOf(69) == 0.0f);

    SECTION("reads stay inside [head − N, head − 1]")
    {
        float left = 0.0f;
        float right = 0.0f;
        gg::LiveSource::Cursor c;
        REQUIRE(ring.read(77.5, 80, c, left, right));       // taps 76 .. 79
        REQUIRE_FALSE(ring.read(78.0, 80, c, left, right)); // tap 80 is unwritten
        REQUIRE(ring.read(-1.0, 80, c, left, right));       // before frame 0 is silence
        REQUIRE(left == 0.0f);
        REQUIRE_FALSE(ring.read(-1.0, 1500, c, left, right)); // tap −2 < 1500 − 1000
        REQUIRE(ring.read(501.0, 1500, c, left, right));      // taps 500 .. 503
        REQUIRE_FALSE(ring.read(std::numeric_limits<double>::quiet_NaN(), 80, c, left, right));
    }

    SECTION("the head fade is the spec's formula")
    {
        // With the head at R = 5000 and N = 1000: 1 while p + 2 <= R − 1 − M and
        // p − 1 >= R − N + M, falling linearly to 0 at p + 2 = R − 1 and at p − 1 = R − N.
        REQUIRE(ring.fade(4741.0, 5000) == 1.0);
        REQUIRE(ring.fade(4869.0, 5000) == 0.5);
        REQUIRE(ring.fade(4997.0, 5000) == 0.0);
        REQUIRE(ring.fade(5100.0, 5000) == 0.0);
        REQUIRE(ring.fade(4257.0, 5000) == 1.0);
        REQUIRE(ring.fade(4129.0, 5000) == 0.5);
        REQUIRE(ring.fade(4001.0, 5000) == 0.0);
        REQUIRE(ring.fade(3000.0, 5000) == 0.0);
        REQUIRE(ring.fade(std::numeric_limits<double>::quiet_NaN(), 5000) == 0.0);
    }

    SECTION("non-finite input is stored as 0 and huge input is clamped")
    {
        gg::LiveSource fresh;
        fresh.prepare(1000.0, 1.0, 64);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        std::vector<float> l{nan,    inf,   -inf, std::numeric_limits<float>::max(),
                             -3e30f, 0.25f, 1.0f, 1.0f};
        std::vector<float> r{0.5f, -nan, 2e7f, -inf, nan, -0.25f, 1.0f, 1.0f};
        const float *stereo[2] = {l.data(), r.data()};
        fresh.write(stereo, 2, 0, 0, 8);
        const float big = gg::LiveSource::kMaxInputMagnitude;
        const float wantL[] = {0.0f, 0.0f, 0.0f, big, -big, 0.25f};
        const float wantR[] = {0.5f, 0.0f, big, 0.0f, 0.0f, -0.25f};
        gg::LiveSource::Cursor c;
        for (int64_t frame = 1; frame <= 5; ++frame)
        {
            float left = -1.0f;
            float right = -1.0f;
            REQUIRE(fresh.read(static_cast<double>(frame), 8, c, left, right));
            REQUIRE(left == wantL[frame]);
            REQUIRE(right == wantR[frame]);
        }
    }

    SECTION("a block's later frames never overwrite the window of its earlier samples")
    {
        // Ruling M1-o. 3000 frames in 64-frame blocks wrap the ring three times. After each
        // block is written, every sample t of it reads its whole window, including the oldest
        // frame, R(t) − N, which the rest of the block must not have overwritten. Frame f holds
        // f mod 997 + 1, so a frame from a later wrap (f + 1064, or f + 1000 with N slots only)
        // holds a different value.
        constexpr int64_t kBlock = 64;
        gg::LiveSource wrapped;
        wrapped.prepare(1000.0, 1.0, static_cast<uint32_t>(kBlock));
        const auto value = [](int64_t f) { return static_cast<float>(f % 997 + 1); };
        std::vector<float> input(static_cast<std::size_t>(kBlock));
        bool intact = true;
        int64_t checked = 0;
        for (int64_t blockStart = 0; blockStart < 3000; blockStart += kBlock)
        {
            for (int64_t j = 0; j < kBlock; ++j)
            {
                input[static_cast<std::size_t>(j)] = value(blockStart + j);
            }
            const float *mono[1] = {input.data()};
            wrapped.write(mono, 1, 0, blockStart, static_cast<uint32_t>(kBlock));
            for (int64_t t = blockStart; t < blockStart + kBlock; ++t)
            {
                const int64_t head = wrapped.headAt(t);
                const int64_t oldest = head - 1000;
                if (oldest < 0)
                {
                    continue;
                }
                gg::LiveSource::Cursor c;
                float left = 0.0f;
                float right = 0.0f;
                // Taps oldest .. oldest + 3, then head − 4 .. head − 1.
                for (const int64_t first : {oldest, head - 4})
                {
                    const float want = gg::interp::catmullRom(
                        value(first), value(first + 1), value(first + 2), value(first + 3), 0.5f);
                    intact = intact &&
                             wrapped.read(static_cast<double>(first) + 1.5, head, c, left, right) &&
                             left == want && right == want;
                }
                ++checked;
            }
        }
        REQUIRE(checked > 1900);
        REQUIRE(intact);
    }
}

TEST_CASE("the guard slots mirror both channels across the wrap", "[live]")
{
    // Slots C .. C + 14 mirror slots 0 .. 14, so a read's taps are contiguous wherever its first
    // tap falls. The scalar read and the SIMD live kernel take the same taps from those slots, so
    // the kernels' bit-identity tests can't see a wrong mirror; only the written frames can. A
    // small ring, N = 64 frames stored in C = 72 slots (blocks of at most 8), is written with
    // frames whose channels differ until it has wrapped 8 times. After each block, every read whose
    // first tap is in slot C − 3, C − 2 or C − 1, so whose later taps run into the guard slots,
    // must give the written frames bit for bit: at whole-number positions the frame itself, and
    // at fractions the Catmull-Rom (or line) through the frames.
    constexpr int64_t kN = 64;
    constexpr int64_t kBlock = 8;
    constexpr int64_t kC = kN + kBlock;
    gg::LiveSource ring;
    ring.prepare(64.0, 1.0, static_cast<uint32_t>(kBlock)); // 64 Hz × 1 s: N = 64
    REQUIRE(ring.frames() == kN);
    REQUIRE(ring.view().capacity == kC);
    REQUIRE(gg::LiveSource::freezeRampSamples(64.0) == 1); // not frozen, so every gain is 1

    // Left is positive and right negative, so no frame's channel equals any channel of another.
    const auto left = [](int64_t f) { return 1.0f + static_cast<float>(f); };
    const auto right = [](int64_t f) { return -0.5f - 0.75f * static_cast<float>(f); };
    const auto same = [](float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; };
    std::vector<float> inL(static_cast<std::size_t>(kBlock));
    std::vector<float> inR(static_cast<std::size_t>(kBlock));
    const float *in[2] = {inL.data(), inR.data()};
    bool exact = true;
    int64_t hermiteReads = 0;
    int64_t linearReads = 0;
    int64_t intoGuard = 0; // reads whose first tap is in slot C − 1
    for (int64_t blockStart = 0; blockStart < 8 * kC + kBlock; blockStart += kBlock)
    {
        for (int64_t j = 0; j < kBlock; ++j)
        {
            inL[static_cast<std::size_t>(j)] = left(blockStart + j);
            inR[static_cast<std::size_t>(j)] = right(blockStart + j);
        }
        ring.write(in, 2, 0, blockStart, static_cast<uint32_t>(kBlock));
        const int64_t head = ring.head();
        gg::LiveSource::Cursor hermiteCursor;
        gg::LiveSource::Cursor linearCursor;
        for (int64_t f = std::max<int64_t>(0, head - kN); f < head; ++f)
        {
            if (f % kC < kC - 3)
            {
                continue;
            }
            for (const float t : {0.0f, 0.25f, 0.5f, 0.75f})
            {
                float l = 0.0f;
                float r = 0.0f;
                // Hermite: taps f .. f + 3, at pos = f + 1 + t.
                if (f + 3 <= head - 1)
                {
                    const float wantL =
                        gg::interp::catmullRom(left(f), left(f + 1), left(f + 2), left(f + 3), t);
                    const float wantR = gg::interp::catmullRom(right(f), right(f + 1), right(f + 2),
                                                               right(f + 3), t);
                    exact = exact &&
                            (t != 0.0f || (same(wantL, left(f + 1)) && same(wantR, right(f + 1))));
                    exact = exact &&
                            ring.read(static_cast<double>(f + 1) + t, head, hermiteCursor, l, r) &&
                            same(l, wantL) && same(r, wantR);
                    ++hermiteReads;
                    intoGuard += f % kC == kC - 1 ? 1 : 0;
                }
                // Linear: taps f and f + 1, at pos = f + t.
                const float wantL = gg::interp::lerp(left(f), left(f + 1), t);
                const float wantR = gg::interp::lerp(right(f), right(f + 1), t);
                exact = exact && (t != 0.0f || (same(wantL, left(f)) && same(wantR, right(f))));
                if (f + 1 <= head - 1)
                {
                    exact = exact &&
                            ring.readLinear(static_cast<double>(f) + t, head, linearCursor, l, r) &&
                            same(l, wantL) && same(r, wantR);
                    ++linearReads;
                }
            }
        }
    }
    REQUIRE(ring.head() >= 8 * kC);
    REQUIRE(hermiteReads > 200);
    REQUIRE(linearReads > 200);
    REQUIRE(intoGuard > 60);
    REQUIRE(exact);
}

TEST_CASE("the guard mirrors fifteen slots", "[live]")
{
    // The sinc's 16 taps need slots C .. C + 14 to mirror slots 0 .. 14, so its taps are
    // contiguous wherever its first tap falls. The same small ring as above (N = 64, C = 72), with
    // frames whose channels differ, written until it has wrapped 8 times. After each block, every
    // 16-tap gather whose first tap is in one of the last 15 slots, so whose later taps run into
    // the guard, must give the written frames bit for bit; and the guard slots hold slots 0 .. 14.
    constexpr int64_t kN = 64;
    constexpr int64_t kBlock = 8;
    constexpr int64_t kC = kN + kBlock;
    constexpr int64_t kGuard = 15;
    gg::LiveSource ring;
    ring.prepare(64.0, 1.0, static_cast<uint32_t>(kBlock));
    REQUIRE(ring.frames() == kN);
    REQUIRE(ring.view().capacity == kC);
    const auto left = [](int64_t f) { return 1.0f + static_cast<float>(f); };
    const auto right = [](int64_t f) { return -0.5f - 0.75f * static_cast<float>(f); };
    const auto same = [](float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; };
    std::vector<float> inL(static_cast<std::size_t>(kBlock));
    std::vector<float> inR(static_cast<std::size_t>(kBlock));
    const float *in[2] = {inL.data(), inR.data()};
    bool exact = true;
    bool mirrored = true;
    int64_t gathers = 0;
    int64_t intoGuard = 0; // gathers whose first tap is in slot C − 1
    for (int64_t blockStart = 0; blockStart < 8 * kC + kBlock; blockStart += kBlock)
    {
        for (int64_t j = 0; j < kBlock; ++j)
        {
            inL[static_cast<std::size_t>(j)] = left(blockStart + j);
            inR[static_cast<std::size_t>(j)] = right(blockStart + j);
        }
        ring.write(in, 2, 0, blockStart, static_cast<uint32_t>(kBlock));
        const int64_t head = ring.head();
        if (head >= kC + kGuard)
        {
            const gg::LiveSource::RingView view = ring.view();
            for (int64_t s = 0; s < 2 * kGuard; ++s)
            {
                mirrored = mirrored && same(view.slots[2 * kC + s], view.slots[s]);
            }
        }
        gg::LiveSource::Cursor cursor;
        for (int64_t f = std::max<int64_t>(0, head - kN); f + 15 <= head - 1; ++f)
        {
            if (f % kC < kC - kGuard)
            {
                continue;
            }
            float l[16] = {};
            float r[16] = {};
            // Taps f .. f + 15, at pos = f + 7.5.
            exact = exact && ring.gatherTaps<16>(static_cast<double>(f) + 7.5, head, cursor, l, r);
            for (int64_t k = 0; k < 16; ++k)
            {
                exact = exact && same(l[k], left(f + k)) && same(r[k], right(f + k));
            }
            ++gathers;
            intoGuard += f % kC == kC - 1 ? 1 : 0;
        }
    }
    REQUIRE(ring.head() >= 8 * kC);
    REQUIRE(gathers > 100);
    REQUIRE(intoGuard >= 7);
    REQUIRE(exact);
    REQUIRE(mirrored);
}

TEST_CASE("gatherTaps agrees with read and readLinear", "[live]")
{
    // read() and readLinear() are gatherTaps<4> and <2> with their curves applied: over 10,000
    // positions in and around the window of a 10 ms ring (N = 480) through its wraps, the
    // gathered taps through interp::catmullRom and interp::lerp give their bits, and each
    // succeeds exactly where they do. A 16-tap gather succeeds exactly where all 16 taps,
    // floor(pos) − 7 .. floor(pos) + 8, are in the window.
    constexpr int64_t kBlock = 100;
    gg::LiveSource ring;
    ring.prepare(48000.0, 0.01, static_cast<uint32_t>(kBlock));
    const int64_t frames = ring.frames();
    REQUIRE(frames == 480);
    const auto same = [](float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; };
    std::vector<float> inL(static_cast<std::size_t>(kBlock));
    std::vector<float> inR(static_cast<std::size_t>(kBlock));
    const float *in[2] = {inL.data(), inR.data()};
    int64_t positions = 0;
    int64_t hermiteReads = 0;
    int64_t linearReads = 0;
    int64_t sincGathers = 0;
    bool agree = true;
    for (int64_t blockStart = 0; positions < 10000; blockStart += kBlock)
    {
        for (int64_t j = 0; j < kBlock; ++j)
        {
            using gg::rng::Field;
            const auto n = static_cast<uint64_t>(blockStart + j);
            inL[static_cast<std::size_t>(j)] = static_cast<float>(
                gg::rng::bipolar(gg::rng::hash({3, 0, 0, Field::SignalNoise, n})));
            inR[static_cast<std::size_t>(j)] = static_cast<float>(
                gg::rng::bipolar(gg::rng::hash({3, 0, 1, Field::SignalNoise, n})));
        }
        ring.write(in, 2, 0, blockStart, static_cast<uint32_t>(kBlock));
        const int64_t head = ring.head();
        gg::LiveSource::Cursor gather4;
        gg::LiveSource::Cursor gather2;
        gg::LiveSource::Cursor gather16;
        gg::LiveSource::Cursor hermite;
        gg::LiveSource::Cursor linear;
        // 250 positions per block, moving forward from 20 frames before the window to 5 past it.
        for (int k = 0; k < 250; ++k, ++positions)
        {
            const auto u = gg::rng::uniform01(gg::rng::hash(
                {5, 0, 0, gg::rng::Field::SignalNoise, static_cast<uint64_t>(positions)}));
            const double pos =
                static_cast<double>(head - frames - 20) +
                (static_cast<double>(k) + u) * static_cast<double>(frames + 25) / 250.0;
            const double base = std::floor(pos);
            const auto t = static_cast<float>(pos - base);
            float l4[4] = {};
            float r4[4] = {};
            float l2[2] = {};
            float r2[2] = {};
            float l16[16] = {};
            float r16[16] = {};
            float l = 0.0f;
            float r = 0.0f;
            const bool ok4 = ring.gatherTaps<4>(pos, head, gather4, l4, r4);
            const bool okRead = ring.read(pos, head, hermite, l, r);
            agree = agree && ok4 == okRead;
            if (ok4 && okRead)
            {
                agree = agree && same(l, gg::interp::catmullRom(l4[0], l4[1], l4[2], l4[3], t)) &&
                        same(r, gg::interp::catmullRom(r4[0], r4[1], r4[2], r4[3], t));
                ++hermiteReads;
            }
            const bool ok2 = ring.gatherTaps<2>(pos, head, gather2, l2, r2);
            const bool okLinear = ring.readLinear(pos, head, linear, l, r);
            agree = agree && ok2 == okLinear;
            if (ok2 && okLinear)
            {
                agree = agree && same(l, gg::interp::lerp(l2[0], l2[1], t)) &&
                        same(r, gg::interp::lerp(r2[0], r2[1], t));
                ++linearReads;
            }
            const bool ok16 = ring.gatherTaps<16>(pos, head, gather16, l16, r16);
            const auto i = static_cast<int64_t>(base);
            agree = agree && ok16 == (i - 7 >= head - frames && i + 8 <= head - 1);
            sincGathers += ok16;
        }
    }
    REQUIRE(ring.head() > 4 * ring.view().capacity); // through several wraps
    REQUIRE(hermiteReads > 8000);
    REQUIRE(linearReads > 8000);
    REQUIRE(sincGathers > 8000);
    REQUIRE(agree);
    {
        gg::LiveSource::Cursor cursor;
        float l[16] = {};
        float r[16] = {};
        REQUIRE_FALSE(ring.gatherTaps<16>(std::numeric_limits<double>::quiet_NaN(), ring.head(),
                                          cursor, l, r));
        REQUIRE_FALSE(ring.gatherTaps<16>(std::numeric_limits<double>::infinity(), ring.head(),
                                          cursor, l, r));
    }
}

TEST_CASE("live grains never read the future", "[live]")
{
    // Checked analytically from the event sink, with no hook in the engine. A grain at elapsed
    // time e (output samples since its real onset T) reads around p(e) = startPos + e × rate,
    // with Hermite taps from p − 1 to p + 2. With the head at R(τ) at output time τ, every tap
    // must stay at least M frames older than the newest frame, R − 1, and M frames inside the
    // oldest one still in the ring, R − N:
    //
    //     p(e) + 2 <= R(T + e) − 1 − M    and    p(e) − 1 >= R(T + e) − N + M.
    //
    // Not frozen, R(τ) = τ + 1 (frames written through output sample τ). Both bounds are linear
    // in e, so checking e = 0 and e = durationSamples covers the grain's whole life.
    //
    // Frozen from tf, the head advances round(0.02 × sr) more frames and stops:
    // R(τ) = min(τ + 1, tf + ramp). R is then concave in e, so the head bound still holds
    // everywhere when it holds at the ends, but the tail bound must also hold at the kink.
    const double n = static_cast<double>(ringFrames(kSampleRate));
    const int64_t fill = 17 * 48000; // the ring is full (16 s) well before the end
    const int64_t frozenFor = 3 * 48000;
    const auto stop = static_cast<double>(fill + rampFrames(kSampleRate));

    Slack worstFree{std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity()};
    Slack worstFrozen = worstFree;
    std::size_t grainsChecked = 0;
    std::size_t frozenChecked = 0;
    for (const double pitch : {-24.0, 0.0, 12.0, 24.0})
    {
        for (const double size : {0.005, 0.15, 2.0})
        {
            for (const double position : {0.0, 0.25, 15.9, 16.0})
            {
                INFO("pitch " << pitch << ", size " << size << " s, position " << position << " s");
                gg::Engine engine(liveConfig());
                engine.setSourceMode(gg::SourceMode::Live);
                engine.setCloudParams({.densityHz = 30.0,
                                       .distribution = 0.5,
                                       .grainDurationSec = size,
                                       .position = position,
                                       .spray = 0.05,
                                       .pitchSemitones = pitch,
                                       .stereo = 0.5,
                                       .gain = 0.5f});
                SinkLog log;
                engine.setEventSink(&SinkLog::record, &log);
                // Admission doesn't depend on the input or the outputs, so nothing is rendered.
                for (int64_t done = 0; done < fill; done += 4096)
                {
                    engine.process(nullptr, 0, nullptr, 0,
                                   static_cast<uint32_t>(std::min<int64_t>(4096, fill - done)));
                }
                REQUIRE(engine.liveWriteHead() == fill); // R(t) = t + 1
                const std::size_t freeGrains = log.grains.size();
                engine.setFreeze(true);
                for (int64_t done = 0; done < frozenFor; done += 4096)
                {
                    engine.process(
                        nullptr, 0, nullptr, 0,
                        static_cast<uint32_t>(std::min<int64_t>(4096, frozenFor - done)));
                }
                REQUIRE(engine.liveWriteHead() == static_cast<int64_t>(stop));
                REQUIRE_FALSE(log.overflowed);
                REQUIRE(engine.droppedGrains() == 0);
                REQUIRE(freeGrains > 400);
                REQUIRE(log.grains.size() - freeGrains > 50);

                for (std::size_t k = 0; k < log.grains.size(); ++k)
                {
                    const gg::GrainSpec &g = log.grains[k];
                    const double onset = static_cast<double>(g.onsetSample) + g.onsetFrac;
                    const double life = g.durationSamples;
                    if (k < freeGrains)
                    {
                        for (const double e : {0.0, life})
                        {
                            const Slack s = slackAt(g.startPos + e * g.rate, onset + e + 1.0, n);
                            worstFree.head = std::min(worstFree.head, s.head);
                            worstFree.tail = std::min(worstFree.tail, s.tail);
                        }
                        ++grainsChecked;
                    }
                    else
                    {
                        const double kink = std::clamp(stop - 1.0 - onset, 0.0, life);
                        for (const double e : {0.0, life, kink})
                        {
                            const double head = std::min(onset + e + 1.0, stop);
                            const Slack s = slackAt(g.startPos + e * g.rate, head, n);
                            worstFrozen.head = std::min(worstFrozen.head, s.head);
                            worstFrozen.tail = std::min(worstFrozen.tail, s.tail);
                        }
                        ++frozenChecked;
                    }
                }
            }
        }
    }
    INFO(grainsChecked << " free-running and " << frozenChecked << " frozen grains");
    // Positions reach about 1e6 frames, where a double's spacing is about 1e-10: the tolerance
    // is rounding only.
    CHECK(worstFree.head >= -1e-6);
    CHECK(worstFree.tail >= -1e-6);
    CHECK(worstFrozen.head >= -1e-6);
    CHECK(worstFrozen.tail >= -1e-6);
    // Not vacuous: Position 0 puts grains against the head bound, and Position 16 s (the
    // plugin's largest), or 15.9 s with Pitch −24 (reading slower than the head moves), against
    // the tail bound.
    CHECK(worstFree.head < 1.0);
    CHECK(worstFree.tail < 1.0);
    CHECK(worstFrozen.head < 1.0);
    CHECK(worstFrozen.tail < 1.0);
}

TEST_CASE("live output never depends on later input", "[live]")
{
    // The black-box form of the test above, through the renderer and the head fade: two inputs
    // that agree up to a cut give bit-identical output before it. Large blocks write input past
    // the cut into the ring before the samples before the cut are rendered, so any read of a
    // frame from the rendered sample's future would show. A freeze on and off before the cut
    // puts grains against the stopped head.
    struct Case
    {
        double pitch;
        double size;
        double position;
    };
    const int64_t frames = 2 * 48000;
    const auto a = noiseInput(frames, 1);
    for (const Case c : {Case{24.0, 0.15, 0.0}, Case{0.0, 0.005, 0.0}, Case{12.0, 0.5, 0.25},
                         Case{-24.0, 0.15, 0.0}})
    {
        for (const int64_t cut : {int64_t{30001}, int64_t{70007}})
        {
            INFO("pitch " << c.pitch << ", size " << c.size << " s, position " << c.position
                          << " s, cut at " << cut);
            auto b = a;
            const auto later = noiseInput(frames, 2, 1.0f);
            std::copy(later.left.begin() + cut, later.left.end(), b.left.begin() + cut);
            std::copy(later.right.begin() + cut, later.right.end(), b.right.begin() + cut);

            const auto render = [&](const Stereo &input) {
                gg::Engine engine(liveConfig());
                engine.setSourceMode(gg::SourceMode::Live);
                engine.setCloudParams({.densityHz = 60.0,
                                       .distribution = 0.5,
                                       .grainDurationSec = c.size,
                                       .position = c.position,
                                       .spray = 0.01,
                                       .pitchSemitones = c.pitch,
                                       .stereo = 0.5,
                                       .gain = 0.5f});
                return run(engine, input, 4096,
                           {{20000, true}, {26000, false}, {50016, true}, {56000, false}});
            };
            const auto outA = render(a);
            const auto outB = render(b);
            REQUIRE(allFinite(outA.left));
            REQUIRE(peak(outA.left, 0, static_cast<std::size_t>(cut)) > 0.01f);
            const auto upTo = static_cast<std::size_t>(cut);
            REQUIRE(sameBits(outA.left, outB.left, 0, upTo));
            REQUIRE(sameBits(outA.right, outB.right, 0, upTo));
            // And the later input is heard after the cut, so the comparison isn't vacuous.
            REQUIRE_FALSE(sameBits(outA.left, outB.left, upTo));
        }
    }
}

TEST_CASE("live grains never read overwritten data", "[live]")
{
    // Output at t may depend only on frames R(t) − N .. R(t) − 1. Two inputs that differ only in
    // their first 2 s give bit-identical output once those frames have left the ring. At 4 kHz
    // the 16 s ring is 64000 frames, so 25 s is short; M stays 256 frames. Position 15.9 s
    // reads at the oldest edge, Pitch −24 drifts towards it, and a 5 s freeze lets the head
    // resume over grains admitted against the stopped head. R(t) is read back from the engine.
    constexpr double kRate = 4000.0;
    const int64_t n = ringFrames(kRate);
    const int64_t frames = 25 * 4000;
    const int64_t differ = 2 * 4000;
    const auto a = noiseInput(frames, 3);
    auto b = a;
    const auto early = noiseInput(differ, 4, 1.0f);
    std::copy(early.left.begin(), early.left.end(), b.left.begin());
    std::copy(early.right.begin(), early.right.end(), b.right.begin());

    for (const double pitch : {-24.0, 0.0})
    {
        for (const bool freeze : {false, true})
        {
            INFO("pitch " << pitch << (freeze ? ", frozen from 2.5 s to 7.5 s" : ""));
            std::vector<int64_t> head(static_cast<std::size_t>(frames));
            const auto render = [&](const Stereo &input) {
                gg::Engine engine(liveConfig(kRate));
                engine.setSourceMode(gg::SourceMode::Live);
                engine.setCloudParams({.densityHz = 20.0,
                                       .distribution = 0.5,
                                       .grainDurationSec = 2.0,
                                       .position = 15.9,
                                       .spray = 0.05,
                                       .pitchSemitones = pitch,
                                       .stereo = 0.5,
                                       .gain = 0.5f});
                Stereo out{std::vector<float>(input.left.size()),
                           std::vector<float>(input.left.size())};
                for (int64_t t = 0; t < frames; ++t)
                {
                    if (freeze && (t == 10000 || t == 30000))
                    {
                        engine.setFreeze(t == 10000);
                    }
                    const auto i = static_cast<std::size_t>(t);
                    const float *in[2] = {input.left.data() + i, input.right.data() + i};
                    float *o[2] = {out.left.data() + i, out.right.data() + i};
                    engine.process(in, 2, o, 2, 1);
                    head[i] = engine.liveWriteHead();
                }
                return out;
            };
            const auto outA = render(a);
            const auto outB = render(b);
            // The first sample whose oldest readable frame, R(t) − N, is past the difference.
            const auto clean = static_cast<std::size_t>(
                std::find_if(head.begin(), head.end(), [&](int64_t r) { return r - n >= differ; }) -
                head.begin());
            REQUIRE(clean + 4000 <= static_cast<std::size_t>(frames));
            REQUIRE(peak(outA.left, clean) > 0.01f);
            REQUIRE(sameBits(outA.left, outB.left, clean));
            REQUIRE(sameBits(outA.right, outB.right, clean));
        }
    }
}

namespace
{
// After the ring wraps (ruling M1-o). A 1 s ring at 48 kHz (N = 48000) wraps every second.
// Position 1 s, the most the ring allows, with Spray asking for more, keeps grains clamped to
// lo, M frames inside the oldest frame, and Pitch −12 has them read slower than the head moves.
// Freeze on at 2 s and off at 2.5 s (both on the 32-sample grid) wraps the ring again after the
// head resumes. A block writes up to maxBlockSize frames past the sample being rendered, so with
// only N slots those frames would already have overwritten the oldest ones the grain reads.
constexpr double kWrapRingSeconds = 1.0;
constexpr int64_t kWrapFrames = 7 * 24000; // 3.5 s: the ring wraps three times
const std::vector<FreezeAt> kWrapFreeze{{96000, true}, {120000, false}};

gg::CloudParams wrapParams(double pitch)
{
    return {.densityHz = 30.0,
            .distribution = 0.5,
            .grainDurationSec = 0.15,
            .position = kWrapRingSeconds,
            .spray = 0.05,
            .pitchSemitones = pitch,
            .stereo = 0.5,
            .gain = 0.5f};
}

// Runs the input in process() calls of blockSizes in turn (cycling), each cut short at the next
// freeze change, which is made between calls at its sample.
Stereo runSplit(gg::Engine &engine, const Stereo &input, const std::vector<uint32_t> &blockSizes,
                const std::vector<FreezeAt> &changes)
{
    const auto frames = static_cast<int64_t>(input.left.size());
    constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
    Stereo out{std::vector<float>(input.left.size(), kUnwritten),
               std::vector<float>(input.left.size(), kUnwritten)};
    std::size_t nextChange = 0;
    std::size_t nextBlock = 0;
    for (int64_t done = 0; done < frames;)
    {
        while (nextChange < changes.size() && changes[nextChange].at == done)
        {
            engine.setFreeze(changes[nextChange++].freeze);
        }
        int64_t end = std::min<int64_t>(done + blockSizes[nextBlock++ % blockSizes.size()], frames);
        if (nextChange < changes.size())
        {
            end = std::min(end, changes[nextChange].at);
        }
        const auto i = static_cast<std::size_t>(done);
        const float *in[2] = {input.left.data() + i, input.right.data() + i};
        float *o[2] = {out.left.data() + i, out.right.data() + i};
        engine.process(in, 2, o, 2, static_cast<uint32_t>(end - done));
        done = end;
    }
    return out;
}

// Block sizes in 1..4096 from gg::rng (seed 99), enough to cover kWrapFrames.
std::vector<uint32_t> randomBlocks()
{
    std::vector<uint32_t> sizes;
    for (int64_t total = 0; total < kWrapFrames;)
    {
        const double u = gg::rng::uniform01(gg::rng::hash(
            {99, 0, 0, gg::rng::Field::SignalNoise, static_cast<uint64_t>(sizes.size())}));
        sizes.push_back(1 + static_cast<uint32_t>(u * 4096));
        total += sizes.back();
    }
    return sizes;
}
} // namespace

TEST_CASE("live mode is block-size invariant after the ring wraps", "[live][determinism]")
{
    const auto input = noiseInput(kWrapFrames, 12);
    struct Split
    {
        uint32_t maxBlock;
        std::vector<uint32_t> blockSizes;
    };
    const auto render = [&](const Split &split, SinkLog &log) {
        gg::Engine engine(liveConfig(kSampleRate, split.maxBlock, kWrapRingSeconds));
        engine.setSourceMode(gg::SourceMode::Live);
        engine.setCloudParams(wrapParams(-12.0));
        engine.setEventSink(&SinkLog::record, &log);
        return runSplit(engine, input, split.blockSizes, kWrapFreeze);
    };
    // One sample per call can't write ahead of the sample being rendered: the reference.
    SinkLog refLog;
    const auto ref = render({4096, {1}}, refLog);
    REQUIRE(allFinite(ref.left));
    REQUIRE_FALSE(refLog.overflowed);
    // Not vacuous: grains clamped against the oldest frame sound after every wrap, frozen or
    // not.
    for (const int64_t from : {int64_t{48000}, int64_t{96000}, int64_t{120000}, int64_t{144000}})
    {
        INFO("from sample " << from);
        REQUIRE(peak(ref.left, static_cast<std::size_t>(from + 1000),
                     static_cast<std::size_t>(from + 24000)) > 0.05f);
    }

    for (const Split &split : {Split{4096, {7}}, Split{4096, {64}}, Split{4096, {511}},
                               Split{4096, {4096}}, Split{4096, randomBlocks()}, Split{64, {4096}},
                               Split{512, randomBlocks()}, Split{1, {4096}}})
    {
        INFO("maxBlockSize = " << split.maxBlock << ", first block size = "
                               << split.blockSizes.front() << " of " << split.blockSizes.size());
        SinkLog log;
        const auto out = render(split, log);
        REQUIRE(log.grains == refLog.grains);
        REQUIRE(sameBits(out.left, ref.left));
        REQUIRE(sameBits(out.right, ref.right));
    }
}

TEST_CASE("hq live grains at levels 2 and 3 are block-size invariant", "[live][determinism]")
{
    // M2 sub-project 2's spec §6.9, end to end: a Live HQ layer at Pitch +24 (rate 4, level 2)
    // and +36 (rate 8, level 3) over a 2 s ring that wraps and freezes: admission, the level each
    // grain latches and the level kernels give the same grains and the same bits in blocks of 1,
    // 64 and 511.
    constexpr double kRing = 2.0;
    const auto input = noiseInput(kWrapFrames * 2, 14);
    const std::vector<FreezeAt> freeze{{200000, true}, {260000, false}};
    for (const double pitch : {24.0, 36.0})
    {
        const uint8_t level = pitch == 24.0 ? 2 : 3;
        INFO("pitch +" << pitch << ", level " << int(level));
        const auto render = [&](uint32_t block, SinkLog &log) {
            gg::Engine engine(liveConfig(kSampleRate, 4096, kRing));
            engine.setSourceMode(gg::SourceMode::Live);
            engine.setCloudParams({.densityHz = 40.0,
                                   .distribution = 0.5,
                                   .grainDurationSec = 0.04,
                                   .position = 1.5,
                                   .spray = 0.2,
                                   .pitchSemitones = pitch,
                                   .stereo = 0.5,
                                   .gain = 0.5f,
                                   .quality = gg::Tier::HQ});
            engine.setEventSink(&SinkLog::record, &log);
            return runSplit(engine, input, {block}, freeze);
        };
        SinkLog refLog;
        const auto ref = render(1, refLog);
        REQUIRE_FALSE(refLog.overflowed);
        REQUIRE(refLog.grains.size() > 200);
        bool latched = true;
        for (const gg::GrainSpec &g : refLog.grains)
        {
            latched = latched && g.tier == gg::Tier::HQ && g.level == level;
        }
        REQUIRE(latched);
        REQUIRE(allFinite(ref.left));
        REQUIRE(peak(ref.left, 48000, ref.left.size()) > 0.05f);
        for (const uint32_t block : {64u, 511u})
        {
            INFO("blocks of " << block);
            SinkLog log;
            const auto out = render(block, log);
            REQUIRE(log.grains == refLog.grains);
            REQUIRE(sameBits(out.left, ref.left));
            REQUIRE(sameBits(out.right, ref.right));
        }
    }
}

TEST_CASE("live mode never depends on later input after the ring wraps", "[live]")
{
    // 4096-frame blocks put up to 4095 frames of later input into the ring before a sample is
    // rendered. Output before an input cut must equal a render with no cut, at both pitches and
    // with the cut while frozen (its input is only written once the head resumes) or not. The
    // grains read about 1 s behind the head, so 5 s leaves time for every cut's input to be
    // heard after it, which shows the comparison isn't vacuous.
    constexpr int64_t kFrames = 5 * 48000;
    const auto a = noiseInput(kFrames, 13);
    const auto later = noiseInput(kFrames, 14, 1.0f);
    for (const double pitch : {-12.0, 0.0})
    {
        const auto render = [&](const Stereo &input) {
            gg::Engine engine(liveConfig(kSampleRate, 4096, kWrapRingSeconds));
            engine.setSourceMode(gg::SourceMode::Live);
            engine.setCloudParams(wrapParams(pitch));
            return run(engine, input, 4096, kWrapFreeze);
        };
        const auto outA = render(a);
        REQUIRE(allFinite(outA.left));
        for (const int64_t cut : {int64_t{60001}, int64_t{100001}, int64_t{150007}})
        {
            INFO("pitch " << pitch << ", cut at " << cut);
            auto b = a;
            std::copy(later.left.begin() + cut, later.left.end(), b.left.begin() + cut);
            std::copy(later.right.begin() + cut, later.right.end(), b.right.begin() + cut);
            const auto outB = render(b);
            const auto upTo = static_cast<std::size_t>(cut);
            REQUIRE(peak(outA.left, upTo - 4096, upTo) > 0.05f);
            REQUIRE(sameBits(outA.left, outB.left, 0, upTo));
            REQUIRE(sameBits(outA.right, outB.right, 0, upTo));
            REQUIRE_FALSE(sameBits(outA.left, outB.left, upTo));
        }
    }
}

TEST_CASE("live ring sanitizes non-finite input", "[live]")
{
    // Review Focus 2: a broken upstream plugin sends NaN or ±inf. The ring stores 0 instead, so
    // the render equals one of the same input with those samples zeroed, and the output stays
    // finite. Huge finite values are clamped, so they can't overflow a sum of grains either.
    const int64_t frames = 2 * 48000;
    auto bad = sineInput(frames, kSampleRate, 330.0, 0.5f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const auto fill = [](std::vector<float> &v, int64_t from, int64_t to, float x) {
        std::fill(v.begin() + from, v.begin() + to, x);
    };
    fill(bad.left, 10000, 10100, nan);
    fill(bad.right, 10000, 10100, nan);
    fill(bad.left, 20000, 20050, inf);
    fill(bad.right, 30000, 30200, -inf);
    for (int64_t i = 40000; i < 50000; i += 997)
    {
        bad.left[static_cast<std::size_t>(i)] = nan;
        bad.right[static_cast<std::size_t>(i + 1)] = -nan;
    }
    fill(bad.left, 60000, 60010, std::numeric_limits<float>::max());
    fill(bad.right, 60000, 60010, -std::numeric_limits<float>::max());
    auto zeroed = bad;
    for (auto *channel : {&zeroed.left, &zeroed.right})
    {
        for (float &x : *channel)
        {
            x = std::isnan(x) || std::isinf(x) ? 0.0f : x;
        }
    }

    const auto render = [&](const Stereo &input, uint32_t inChannels) {
        gg::Engine engine(liveConfig());
        engine.setSourceMode(gg::SourceMode::Live);
        engine.setCloudParams({.densityHz = 40.0,
                               .distribution = 0.5,
                               .grainDurationSec = 0.1,
                               .position = 0.05,
                               .spray = 0.1,
                               .pitchSemitones = 7.0,
                               .stereo = 1.0,
                               .gain = 0.5f});
        Stereo out{std::vector<float>(input.left.size()), std::vector<float>(input.left.size())};
        for (int64_t done = 0; done < frames; done += 512)
        {
            const auto i = static_cast<std::size_t>(done);
            const float *in[2] = {input.left.data() + i, input.right.data() + i};
            float *o[2] = {out.left.data() + i, out.right.data() + i};
            engine.process(in, inChannels, o, 2,
                           static_cast<uint32_t>(std::min<int64_t>(512, frames - done)));
        }
        return out;
    };
    for (const uint32_t inChannels : {2u, 1u})
    {
        INFO(inChannels << " input channel(s)");
        const auto out = render(bad, inChannels);
        REQUIRE(allFinite(out.left));
        REQUIRE(allFinite(out.right));
        REQUIRE(peak(out.left) > 0.05f);
        const auto ref = render(zeroed, inChannels);
        // Up to the clamp of the huge values, the non-finite samples were stored as 0.
        REQUIRE(sameBits(out.left, ref.left, 0, 60000));
        REQUIRE(sameBits(out.right, ref.right, 0, 60000));
    }
}

TEST_CASE("freeze stops the head after the ramp", "[live]")
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
    {
        INFO("sample rate " << rate);
        const int64_t ramp = rampFrames(rate);
        const auto frames = static_cast<int64_t>(4.0 * rate);
        const auto input = sineInput(frames, rate, 220.0, 0.5f);
        gg::Engine engine(liveConfig(rate));
        engine.setSourceMode(gg::SourceMode::Live);
        engine.setCloudParams({.densityHz = 16.0,
                               .distribution = 0.5,
                               .grainDurationSec = 0.15,
                               .position = 0.25,
                               .spray = 0.05,
                               .stereo = 0.5,
                               .gain = 0.65f});
        SinkLog log;
        engine.setEventSink(&SinkLog::record, &log);
        Stereo out{std::vector<float>(static_cast<std::size_t>(frames)),
                   std::vector<float>(static_cast<std::size_t>(frames))};
        int64_t clock = 0;
        const auto step = [&](int64_t n) {
            const auto i = static_cast<std::size_t>(clock);
            const float *in[2] = {input.left.data() + i, input.right.data() + i};
            float *o[2] = {out.left.data() + i, out.right.data() + i};
            engine.process(in, 2, o, 2, static_cast<uint32_t>(n));
            clock += n;
        };

        const int64_t freezeAt = 32 * static_cast<int64_t>(0.5 * rate / 32); // on the grid
        step(freezeAt);
        REQUIRE(engine.liveWriteHead() == freezeAt);
        engine.setFreeze(true);
        // One sample at a time: the head keeps advancing for exactly `ramp` samples.
        bool advancing = true;
        for (int64_t k = 0; k < ramp; ++k)
        {
            step(1);
            advancing = advancing && engine.liveWriteHead() == freezeAt + k + 1;
        }
        REQUIRE(advancing);
        bool stopped = true;
        for (int64_t k = 0; k < 64; ++k)
        {
            step(1);
            stopped = stopped && engine.liveWriteHead() == freezeAt + ramp;
        }
        REQUIRE(stopped);

        // Frozen for 2 s: the head stays, and new grains keep sounding from the frozen ring.
        const std::size_t grainsBefore = log.grains.size();
        const int64_t holdFrom = clock;
        step(static_cast<int64_t>(2.0 * rate));
        REQUIRE(engine.liveWriteHead() == freezeAt + ramp);
        REQUIRE(log.grains.size() > grainsBefore + 20);
        const auto lastSecond = static_cast<std::size_t>(clock - static_cast<int64_t>(rate));
        REQUIRE(rms(out.left, lastSecond, static_cast<std::size_t>(clock)) > 0.05);
        REQUIRE(rms(out.right, lastSecond, static_cast<std::size_t>(clock)) > 0.05);
        REQUIRE(allFinite(out.left));
        REQUIRE(holdFrom == freezeAt + ramp + 64);

        // Freeze off: the head resumes at once.
        engine.setFreeze(false);
        step(1);
        REQUIRE(engine.liveWriteHead() == freezeAt + ramp + 1);
        step(1);
        REQUIRE(engine.liveWriteHead() == freezeAt + ramp + 2);
    }
}

TEST_CASE("the tail fade is smooth where the resumed head overwrites frozen grains", "[live]")
{
    // Frozen, the head stops and admission puts grains at Position 16 s right against the
    // oldest frame (span 0: lo = R − N + 1 + M). When Freeze turns off, the head and the oldest
    // frame move on at one frame per sample, faster than these grains read (Pitch −12, rate
    // 0.5), and the old, full-level frames under them get overwritten. The fade must take them
    // out over M frames instead of cutting them: the largest output step in the second after the
    // release stays within 1.5× the largest in the frozen second before it, where the same
    // grains play undisturbed. (Next to a stopped head the frames are already ramped to silence
    // by the write gain, so this is where the fade counts.)
    //
    // The periodic grains, 500 samples apart at rate 0.5, read 250 frames apart: one period of
    // the 32 Hz input at 8 kHz, so they add in phase rather than cancel.
    constexpr double kRate = 8000.0; // a 128000-frame ring fills in 16 s
    const int64_t frames = 22 * 8000;
    const int64_t freezeOn = 17 * 8000;
    const int64_t freezeOff = 18 * 8000;
    const auto input = sineInput(frames, kRate, 32.0, 0.5f);
    gg::Engine engine(liveConfig(kRate, 32));
    engine.setSourceMode(gg::SourceMode::Live);
    engine.setCloudParams({.densityHz = 16.0,
                           .grainDurationSec = 0.5,
                           .position = 16.0,
                           .pitchSemitones = -12.0,
                           .gain = static_cast<float>(1.0 / std::sqrt(8.0))});
    SinkLog log;
    engine.setEventSink(&SinkLog::record, &log);
    const auto out = run(engine, input, 32, {{freezeOn, true}, {freezeOff, false}});
    REQUIRE(allFinite(out.left));

    // Grains spawned while frozen sound across the release.
    const auto acrossRelease =
        std::count_if(log.grains.begin(), log.grains.end(), [&](const gg::GrainSpec &g) {
            return g.onsetSample >= freezeOn && g.onsetSample < freezeOff &&
                   grainEndSample(g) > freezeOff + 600;
        });
    REQUIRE(acrossRelease >= 3);

    const auto stepIn = [&](int64_t from, int64_t to) {
        return largestStep(std::vector<float>(out.left.begin() + from, out.left.begin() + to));
    };
    const float frozen = stepIn(freezeOn + 4000, freezeOff);
    const float released = stepIn(freezeOff, freezeOff + 8000);
    INFO("frozen step " << frozen << ", after the release " << released);
    REQUIRE(peak(out.left, static_cast<std::size_t>(freezeOn + 4000),
                 static_cast<std::size_t>(freezeOff)) > 0.1f);
    REQUIRE(peak(out.left, static_cast<std::size_t>(freezeOff),
                 static_cast<std::size_t>(freezeOff + 8000)) > 0.05f);
    CHECK(released <= 1.5f * frozen);
}

TEST_CASE("rapid freeze toggling stays smooth", "[live]")
{
    // Review Focus 4: Freeze flipped every control cell (32 samples) for a second, held for a
    // second, or held just past its ramp (the head stops) and released for one cell. The output
    // stays finite, and no sample-to-sample step exceeds twice the input's largest step times
    // the most grains sounding at once.
    const int64_t frames = 3 * 48000;
    const auto input = sineInput(frames, kSampleRate, 220.0, 0.5f);
    const float inputStep = largestStep(input.left);
    REQUIRE(inputStep > 0.0f);

    struct Pattern
    {
        const char *name;
        int64_t on;  // samples frozen in each cycle
        int64_t off; // samples not frozen
    };
    const int64_t from = 24000;
    const int64_t until = from + 48000;
    for (const Pattern pattern :
         {Pattern{"every 32 samples", 32, 32}, Pattern{"on 1024, off 32", 1024, 32},
          Pattern{"held for 1 s", 48000, 0}})
    {
        for (const double position : {0.25, 0.0})
        {
            INFO(pattern.name << ", position " << position << " s");
            std::vector<FreezeAt> changes;
            for (int64_t t = from; t < until; t += pattern.on + pattern.off)
            {
                changes.push_back({t, true});
                if (pattern.off > 0 && t + pattern.on < until)
                {
                    changes.push_back({t + pattern.on, false});
                }
            }
            changes.push_back({until, false});

            gg::Engine engine(liveConfig(kSampleRate, 32));
            engine.setSourceMode(gg::SourceMode::Live);
            // The plugin's defaults (spec §4), with its overlap gain 1 / sqrt(16 Hz × 0.15 s).
            engine.setCloudParams({.densityHz = 16.0,
                                   .distribution = 0.5,
                                   .grainDurationSec = 0.15,
                                   .position = position,
                                   .spray = 0.05,
                                   .stereo = 0.5,
                                   .gain = static_cast<float>(1.0 / std::sqrt(2.4))});
            SinkLog log;
            engine.setEventSink(&SinkLog::record, &log);
            const auto out = run(engine, input, 32, changes);

            REQUIRE(allFinite(out.left));
            REQUIRE(allFinite(out.right));
            REQUIRE(peak(out.left, static_cast<std::size_t>(from)) > 0.05f);
            REQUIRE_FALSE(log.overflowed);
            const std::size_t overlap = largestOverlap(log.grains);
            const float bound = 2.0f * inputStep * static_cast<float>(overlap);
            INFO("input step " << inputStep << ", overlap " << overlap << ", bound " << bound);
            CHECK(largestStep(out.left) <= bound);
            CHECK(largestStep(out.right) <= bound);
        }
    }
}

TEST_CASE("live input wiring and source mode", "[live]")
{
    const int64_t frames = 48000;
    const auto stereo = noiseInput(frames, 5);
    const gg::CloudParams p{.densityHz = 40.0,
                            .distribution = 0.5,
                            .grainDurationSec = 0.08,
                            .position = 0.1,
                            .spray = 0.05,
                            .pitchSemitones = 3.0,
                            .gain = 0.5f}; // stereo 0: every grain centred, both gains exactly 1

    const auto render = [&](const float *const *in, uint32_t inChannels) {
        gg::Engine engine(liveConfig());
        engine.setSourceMode(gg::SourceMode::Live);
        engine.setCloudParams(p);
        Stereo out{std::vector<float>(static_cast<std::size_t>(frames)),
                   std::vector<float>(static_cast<std::size_t>(frames))};
        for (int64_t done = 0; done < frames; done += 1000)
        {
            const auto i = static_cast<std::size_t>(done);
            const float *offsetIn[2] = {in && in[0] ? in[0] + i : nullptr,
                                        in && in[1] ? in[1] + i : nullptr};
            float *o[2] = {out.left.data() + i, out.right.data() + i};
            engine.process(in ? offsetIn : nullptr, inChannels, o, 2, 1000);
        }
        return out;
    };

    SECTION("a mono input fills both channels")
    {
        const float *mono[2] = {stereo.left.data(), nullptr};
        const auto out = render(mono, 1);
        REQUIRE(peak(out.left) > 0.05f);
        REQUIRE(sameBits(out.left, out.right));
        const float *doubled[2] = {stereo.left.data(), stereo.left.data()};
        const auto same = render(doubled, 2);
        REQUIRE(sameBits(out.left, same.left));
        REQUIRE(sameBits(out.right, same.right));
    }

    SECTION("stereo input keeps its channels")
    {
        const float *in[2] = {stereo.left.data(), stereo.right.data()};
        const auto out = render(in, 2);
        REQUIRE(peak(out.left) > 0.05f);
        REQUIRE(peak(out.right) > 0.05f);
        REQUIRE_FALSE(sameBits(out.left, out.right));
    }

    SECTION("a missing input is silence")
    {
        const float *none[2] = {nullptr, nullptr};
        for (const auto &out : {render(nullptr, 2), render(none, 2), render(none, 0)})
        {
            REQUIRE(peak(out.left) == 0.0f);
            REQUIRE(peak(out.right) == 0.0f);
        }
        // A null second channel is a silent channel.
        const float *leftOnly[2] = {stereo.left.data(), nullptr};
        const auto out = render(leftOnly, 2);
        REQUIRE(peak(out.left) > 0.05f);
        REQUIRE(peak(out.right) == 0.0f);
    }

    SECTION("the M0 overload renders a missing input")
    {
        gg::Engine engine(liveConfig());
        engine.setSourceMode(gg::SourceMode::Live);
        engine.setCloudParams(p);
        std::vector<float> left(4800, 1.0f), right(4800, 1.0f);
        float *o[2] = {left.data(), right.data()};
        for (int i = 0; i < 10; ++i)
        {
            engine.process(o, 2, 4800);
        }
        REQUIRE(engine.liveWriteHead() == 48000);
        REQUIRE(peak(left) == 0.0f);
        REQUIRE(engine.activeGrains() > 0);
    }

    SECTION("live mode needs a ring")
    {
        // Without liveSeconds there is no ring, so Live is ignored and the engine keeps
        // rendering its sample source, bit for bit.
        auto source = std::make_shared<gg::SampleBuffer>(2, 48000, kSampleRate);
        std::copy(stereo.left.begin(), stereo.left.end(), source->channel(0));
        std::copy(stereo.right.begin(), stereo.right.end(), source->channel(1));
        gg::CloudParams sampleParams = p;
        sampleParams.position = 0.5;
        const auto renderSample = [&](bool askLive) {
            gg::Engine engine(gg::EngineConfig{});
            engine.setSource(source);
            engine.setCloudParams(sampleParams);
            if (askLive)
            {
                engine.setSourceMode(gg::SourceMode::Live);
            }
            Stereo out{std::vector<float>(24000), std::vector<float>(24000)};
            float *o[2] = {out.left.data(), out.right.data()};
            const float *in[2] = {stereo.left.data(), stereo.right.data()};
            engine.process(in, 2, o, 2, 24000);
            REQUIRE(engine.sourceMode() == gg::SourceMode::Sample);
            return out;
        };
        const auto asked = renderSample(true);
        const auto plain = renderSample(false);
        REQUIRE(peak(plain.left) > 0.05f);
        REQUIRE(sameBits(asked.left, plain.left));
        REQUIRE(sameBits(asked.right, plain.right));
    }

    SECTION("a mode change applies from the next process call and old grains finish")
    {
        // Spec §6.4 (carry-over row 119): the sample grains sounding at the switch finish on
        // the sample; the grains after it read the ring.
        gg::Engine engine(liveConfig());
        auto source = std::make_shared<gg::SampleBuffer>(1, 48000, kSampleRate);
        std::fill_n(source->channel(0), 48000, 0.25f);
        engine.setSource(source);
        engine.setCloudParams(p);
        std::vector<float> left(4800), right(4800);
        float *o[2] = {left.data(), right.data()};
        engine.process(o, 2, 4800);
        REQUIRE(engine.sourceMode() == gg::SourceMode::Sample);
        REQUIRE(engine.activeGrains() > 0);
        engine.setSourceMode(gg::SourceMode::Live);
        REQUIRE(engine.sourceMode() == gg::SourceMode::Sample); // not until process()
        engine.process(o, 2, 0);
        REQUIRE(engine.sourceMode() == gg::SourceMode::Live);
        REQUIRE(engine.activeGrains() > 0); // kept
        // The old grains, which started before 4,800, end by 4,799 + 3,840 + 2 (grainEndSample):
        // 3,841 samples into this call. The ring holds only silence (the M0 overload), so the
        // live grains are silent and nothing sounds after that.
        engine.process(o, 2, 4800);
        REQUIRE(peak(left) > 0.05f);
        REQUIRE(peak(std::vector<float>(left.begin() + 3841, left.end())) == 0.0f);
        REQUIRE(engine.activeGrains() > 0);
    }
}

TEST_CASE("reset restarts the live ring", "[live]")
{
    // After reset() the ring's old frames are never read: the render equals a fresh engine's.
    const int64_t frames = 48000;
    const auto first = noiseInput(frames, 6);
    const auto second = noiseInput(frames, 7);
    const gg::CloudParams p{.densityHz = 50.0,
                            .distribution = 0.5,
                            .grainDurationSec = 0.1,
                            .position = 0.5,
                            .spray = 0.4,
                            .pitchSemitones = -5.0,
                            .stereo = 0.5,
                            .gain = 0.5f};
    // Reset while frozen (the head stopped, the write gain 0) or not; after it, both renders
    // freeze at 0.5 s. A reset that kept the old gain would ramp the new frames in from 0.
    for (const bool frozen : {false, true})
    {
        INFO((frozen ? "reset while frozen" : "reset while not frozen"));
        gg::Engine used(liveConfig());
        used.setSourceMode(gg::SourceMode::Live);
        used.setCloudParams(p);
        run(used, first, 512, {{24000, frozen}});
        used.reset();
        REQUIRE(used.liveWriteHead() == 0);
        REQUIRE(used.sampleClock() == 0);
        const auto again = run(used, second, 512, {{0, false}, {24000, true}});

        gg::Engine fresh(liveConfig());
        fresh.setSourceMode(gg::SourceMode::Live);
        fresh.setCloudParams(p);
        const auto ref = run(fresh, second, 512, {{24000, true}});
        REQUIRE(peak(ref.left) > 0.05f);
        REQUIRE(fresh.liveWriteHead() == 24000 + 960);
        REQUIRE(used.liveWriteHead() == fresh.liveWriteHead());
        REQUIRE(sameBits(again.left, ref.left));
        REQUIRE(sameBits(again.right, ref.right));
    }

    // The freeze setting survives reset(): frozen before it, the ring freezes again from the
    // first frame after it, its head stopping 20 ms (960 samples) in, as a new engine's would.
    gg::Engine used(liveConfig());
    used.setSourceMode(gg::SourceMode::Live);
    run(used, first, 512, {{24000, true}});
    used.reset();
    run(used, second, 512);
    REQUIRE(used.liveWriteHead() == 960);
}

TEST_CASE("a grain whose read span exceeds the ring is dropped", "[live]")
{
    // A 1 s ring (48000 frames) and 0.5 s grains at +24 semitones read 4 × 24000 frames while
    // the head moves 24000: the span, 72000 frames, can't fit. At pitch 0 it can.
    for (const double pitch : {24.0, 0.0})
    {
        INFO("pitch " << pitch);
        gg::Engine engine(liveConfig(kSampleRate, 4096, 1.0));
        engine.setSourceMode(gg::SourceMode::Live);
        engine.setCloudParams({.densityHz = 20.0,
                               .grainDurationSec = 0.5,
                               .position = 0.5,
                               .pitchSemitones = pitch,
                               .gain = 0.5f});
        SinkLog log;
        engine.setEventSink(&SinkLog::record, &log);
        const auto out = run(engine, noiseInput(48000, 8), 1024);
        if (pitch > 0.0)
        {
            REQUIRE(engine.droppedGrains() == 20); // every scheduled grain
            REQUIRE(log.grains.empty());
            REQUIRE(peak(out.left) == 0.0f);
        }
        else
        {
            REQUIRE(engine.droppedGrains() == 0);
            REQUIRE(log.grains.size() == 20);
        }
    }
}

// M2 sub-project 2 (spec §6.1, §6.5): the ring's window W (a loop or a splice), overdub, splice
// marks and the storage exchange.

namespace
{
// Frame f's value in the storage tests: exact in float for every frame they write.
float rampValue(int64_t f) { return static_cast<float>(f); }

// Writes frames [ring.head(), to) of rampValue into `ring` in blocks of `block`, output sample t
// carrying frame t (not frozen, so R(t) = t + 1).
void writeRamp(gg::LiveSource &ring, int64_t to, int64_t block)
{
    std::vector<float> l(static_cast<std::size_t>(block));
    std::vector<float> r(static_cast<std::size_t>(block));
    for (int64_t start = ring.head(); start < to; start += block)
    {
        const int64_t n = std::min(block, to - start);
        for (int64_t j = 0; j < n; ++j)
        {
            l[static_cast<std::size_t>(j)] = rampValue(start + j);
            r[static_cast<std::size_t>(j)] = -rampValue(start + j);
        }
        const float *in[2] = {l.data(), r.data()};
        ring.write(in, 2, 0, start, static_cast<uint32_t>(n));
    }
}

// Frame f's two channels as the ring stores them (slot f mod capacity).
std::pair<float, float> storedFrame(const gg::LiveSource::RingView &view, int64_t f)
{
    const int64_t s = f % view.capacity;
    return {view.slots[2 * s], view.slots[2 * s + 1]};
}

// A main thread's copy of frames [from, to) from the ring's storage into `next`, frame f at slot
// f mod next.capacity() (spec §6.5).
void copyFrames(const gg::LiveSource &ring, gg::RingStorage &next, int64_t from, int64_t to)
{
    const gg::LiveSource::RingView view = ring.view();
    for (int64_t f = std::max<int64_t>(0, from); f < to; ++f)
    {
        const auto [l, r] = storedFrame(view, f);
        const int64_t s = f % next.capacity();
        next.slots()[2 * s] = l;
        next.slots()[2 * s + 1] = r;
    }
}

// The common Live-mode parameters of the window tests: grains well behind the head, sprayed.
gg::CloudParams windowParams(double position)
{
    return {.densityHz = 80.0,
            .distribution = 0.5,
            .grainDurationSec = 0.05,
            .position = position,
            .spray = 0.3,
            .pitchSemitones = 0.0,
            .stereo = 0.5,
            .gain = 0.5f};
}
} // namespace

TEST_CASE("a loop longer than the ring is the ring", "[engine][live]")
{
    // Review Focus 1: Loop on at a length past N, or a splice selected with no marks, is the
    // identity on M1's render, bit for bit, over 4 s of a 1 s ring (so the ring wraps).
    const auto input = noiseInput(4 * 48000, 21);
    const auto render = [&](int which) {
        gg::Engine engine(liveConfig(kSampleRate, 512, 1.0));
        engine.setSourceMode(gg::SourceMode::Live);
        engine.setCloudParams(windowParams(0.6));
        const int64_t n = engine.liveFrames();
        if (which == 1)
        {
            engine.live().setLoop(true, 2 * n);
        }
        else if (which == 2)
        {
            engine.live().setLoop(true, n);
        }
        else if (which == 3)
        {
            engine.live().selectSplice(1);
        }
        return run(engine, input, 512, {{2 * 48000, true}, {3 * 48000, false}});
    };
    const auto reference = render(0);
    REQUIRE(peak(reference.left) > 0.05f);
    for (const int which : {1, 2, 3})
    {
        INFO("variant " << which);
        const auto out = render(which);
        REQUIRE(sameBits(out.left, reference.left));
        REQUIRE(sameBits(out.right, reference.right));
    }
}

TEST_CASE("a loop narrows the window", "[engine][live]")
{
    // A 4 s ring with Loop on at 0.5 s: Position 2 s asks for audio outside the loop, so the
    // admission clamp puts every grain inside it, at least M frames from both of its ends, as it
    // keeps them inside the ring's N without a loop (the Engine class comment's lo and hi with W
    // for N).
    constexpr int64_t kW = 24000; // 0.5 s
    gg::Engine engine(liveConfig(kSampleRate, 512, 4.0));
    engine.setSourceMode(gg::SourceMode::Live);
    engine.setCloudParams(windowParams(2.0));
    engine.live().setLoop(true, kW);
    SinkLog log;
    engine.setEventSink(&SinkLog::record, &log);
    const auto out = run(engine, noiseInput(5 * 48000, 22), 512);
    REQUIRE(allFinite(out.left));
    REQUIRE(engine.live().windowFrames() == kW);
    REQUIRE(engine.live().frames() == 4 * 48000);
    REQUIRE_FALSE(log.overflowed);
    REQUIRE(log.grains.size() > 300);

    double worstTail = std::numeric_limits<double>::infinity();
    double worstHead = std::numeric_limits<double>::infinity();
    for (const gg::GrainSpec &g : log.grains)
    {
        // Not frozen: at the onset T the head is at T + 1, and it moves with the grain.
        const double rho = static_cast<double>(g.onsetSample) + g.onsetFrac + 1.0;
        const double dur = g.durationSamples;
        const double lo =
            rho - static_cast<double>(kW) + 1.0 + kM + std::max(0.0, dur * (1.0 - g.rate));
        const double hi = rho - 3.0 - kM - std::max(0.0, dur * (g.rate - 1.0));
        worstTail = std::min(worstTail, g.startPos - lo);
        worstHead = std::min(worstHead, hi - g.startPos);
    }
    CHECK(worstTail >= -1e-6);
    CHECK(worstHead >= -1e-6);
    CHECK(worstTail < 1.0); // not vacuous: Position 2 s puts grains against the loop's start

    // The fade and the reads follow the storage, not the loop (ruling SP-T12aa): across the
    // loop's oldest frame the fade stays 1 and the reads succeed, so a grain the loop's start
    // passes keeps reading its frames; only the storage's ends fade, as in M1.
    gg::LiveSource ring;
    ring.prepare(1000.0, 4.0, 64); // N = 4000
    ring.setLoop(true, 1000);
    writeRamp(ring, 6000, 64);
    const int64_t end = ring.head();
    REQUIRE(ring.windowFrames() == 1000);
    REQUIRE(ring.anchorAt(5999) == end);
    REQUIRE(ring.windowOldestAt(5999) == end - 1000);
    const auto oldest = static_cast<double>(end - 1000);
    REQUIRE(ring.fade(oldest - 1.0, end) == 1.0);
    REQUIRE(ring.fade(oldest + 1.0, end) == 1.0);
    const auto stored = static_cast<double>(end - 4000);
    REQUIRE(ring.fade(stored + 1.0, end) == 0.0);
    REQUIRE(ring.fade(stored + 1.0 + kM / 2.0, end) == 0.5);
    REQUIRE(ring.fade(stored + 1.0 + kM, end) == 1.0);
    gg::LiveSource::Cursor c;
    float l = 0.0f;
    float r = 0.0f;
    REQUIRE(ring.read(oldest, end, c, l, r)); // its first tap is the frame before W
    REQUIRE(l == rampValue(end - 1000));
    REQUIRE_FALSE(ring.read(stored, end, c, l, r)); // its first tap is the frame before N
    REQUIRE(ring.read(stored + 1.0, end, c, l, r));
    REQUIRE(l == rampValue(end - 4000 + 1));
}

TEST_CASE("overdub 0 is the M1 write", "[engine][live]")
{
    // write() with nullptr gains and with gains of 0 store the same bits in every slot, the
    // guard slots included, −0 inputs kept as −0, over three wraps of a looped ring.
    constexpr int64_t kBlock = 64;
    gg::LiveSource a;
    gg::LiveSource b;
    for (gg::LiveSource *ring : {&a, &b})
    {
        ring->prepare(1000.0, 1.0, static_cast<uint32_t>(kBlock));
        ring->setLoop(true, 250);
    }
    std::vector<float> l(static_cast<std::size_t>(kBlock));
    std::vector<float> r(static_cast<std::size_t>(kBlock));
    const std::vector<float> zeros(static_cast<std::size_t>(kBlock), 0.0f);
    for (int64_t start = 0; start < 3200; start += kBlock)
    {
        for (int64_t j = 0; j < kBlock; ++j)
        {
            const int64_t f = start + j;
            l[static_cast<std::size_t>(j)] = f % 3 == 0 ? -0.0f : rampValue(f) * 1e-3f;
            r[static_cast<std::size_t>(j)] = f % 5 == 0 ? -0.0f : -rampValue(f) * 1e-3f;
        }
        const float *in[2] = {l.data(), r.data()};
        if (start == 1600)
        {
            a.setFrozen(true); // through the freeze ramp and back
            b.setFrozen(true);
        }
        if (start == 2240)
        {
            a.setFrozen(false);
            b.setFrozen(false);
        }
        a.write(in, 2, 0, start, static_cast<uint32_t>(kBlock));
        b.write(in, 2, 0, start, static_cast<uint32_t>(kBlock), zeros.data());
    }
    REQUIRE(a.head() == b.head());
    const auto va = a.view();
    const auto vb = b.view();
    const auto floats = static_cast<std::size_t>(2 * (va.capacity + 3));
    REQUIRE(std::memcmp(va.slots, vb.slots, floats * sizeof(float)) == 0);
    std::size_t negativeZeros = 0;
    for (std::size_t i = 0; i < floats; ++i)
    {
        negativeZeros += va.slots[i] == 0.0f && std::signbit(va.slots[i]) ? 1 : 0;
    }
    REQUIRE(negativeZeros > 100);
}

TEST_CASE("overdub recycles at the period", "[engine][live]")
{
    // A 1 s ring at 48 kHz, an impulse at frame 100 and Overdub 0.5 on every frame: with Loop on
    // at 0.25 s the impulse comes back every 12000 frames, halved each pass; with Loop off, every
    // N = 48000 frames.
    for (const bool loop : {true, false})
    {
        INFO((loop ? "Loop on at 0.25 s" : "Loop off"));
        const int64_t period = loop ? 12000 : 48000;
        constexpr int64_t kBlock = 512;
        gg::LiveSource ring;
        ring.prepare(48000.0, 1.0, static_cast<uint32_t>(kBlock));
        ring.setLoop(loop, 12000);
        const int64_t total = loop ? 40000 : 48300;
        std::vector<float> in(static_cast<std::size_t>(kBlock));
        const std::vector<float> half(static_cast<std::size_t>(kBlock), 0.5f);
        for (int64_t start = 0; start < total; start += kBlock)
        {
            const int64_t n = std::min(kBlock, total - start);
            for (int64_t j = 0; j < n; ++j)
            {
                in[static_cast<std::size_t>(j)] = start + j == 100 ? 1.0f : 0.0f;
            }
            const float *mono[1] = {in.data()};
            ring.write(mono, 1, 0, start, static_cast<uint32_t>(n), half.data());
        }
        REQUIRE(ring.head() == total);
        const auto view = ring.view();
        REQUIRE(total < view.capacity); // no frame overwritten: each is in slot f
        float want = 1.0f;
        for (int64_t f = 100; f < total; f += period)
        {
            INFO("frame " << f);
            REQUIRE(storedFrame(view, f).first == want);
            REQUIRE(storedFrame(view, f).second == want); // mono fills both channels
            REQUIRE(storedFrame(view, f - 1).first == 0.0f);
            REQUIRE(storedFrame(view, f + 1).first == 0.0f);
            want *= 0.5f;
        }
        REQUIRE(want == (loop ? 0.0625f : 0.25f)); // 4 passes looped, 2 not
        if (!loop)
        {
            REQUIRE(storedFrame(view, 12100).first == 0.0f);
        }
    }
}

TEST_CASE("overdub with feedback sums in order", "[engine][live]")
{
    // Spec §6.1: with Feedback too, frame R is sanitize(in) + fb·limited + o·ring[R − P], summed
    // left to right. The Processor hands the ring sanitize(in) + fb·limited; the ring adds the
    // overdub term. Frame 0's values round differently in another order, so the order is seen.
    gg::LiveSource ring;
    ring.prepare(1000.0, 1.0, 64);
    ring.setLoop(true, 10); // P = 10
    std::vector<float> first(10);
    for (int64_t f = 0; f < 10; ++f)
    {
        first[static_cast<std::size_t>(f)] = 0.05f * static_cast<float>(f + 1);
    }
    const float s[3] = {0.1f, -0.37f, 0.62f};
    const float lim[3] = {0.7f, 0.33f, -0.91f};
    const float back[3] = {0.37f, -0.123f, 0.555f};
    first[0] = back[0];
    first[1] = back[1];
    first[2] = back[2];
    const float fb = 0.85f;
    const float o = 0.9f;
    const float *firstIn[1] = {first.data()};
    ring.write(firstIn, 1, 0, 0, 10);

    float in[3];
    for (int i = 0; i < 3; ++i)
    {
        in[i] = s[i] + fb * lim[i]; // the Processor's sum
    }
    const float gains[3] = {o, o, o};
    const float *mono[1] = {in};
    ring.write(mono, 1, 0, 10, 3, gains);
    const auto view = ring.view();
    for (int i = 0; i < 3; ++i)
    {
        INFO("frame " << 10 + i);
        const float want = (s[i] + fb * lim[i]) + o * back[i];
        REQUIRE(storedFrame(view, 10 + i).first == want);
    }
    REQUIRE(s[0] + (fb * lim[0] + o * back[0]) != (s[0] + fb * lim[0]) + o * back[0]);
}

TEST_CASE("splices are kept ascending and age out", "[engine][live]")
{
    gg::LiveSource ring;
    ring.prepare(1000.0, 1.0, 64); // N = 1000
    writeRamp(ring, 640, 64);
    REQUIRE(ring.head() == 640);
    REQUIRE(ring.addSplice(300));
    REQUIRE(ring.addSplice(100));
    REQUIRE(ring.addSplice(200));
    REQUIRE(ring.spliceCount() == 3);
    REQUIRE(std::vector<int64_t>(ring.splices().begin(), ring.splices().end()) ==
            std::vector<int64_t>{100, 200, 300});
    REQUIRE_FALSE(ring.addSplice(200)); // a duplicate
    REQUIRE_FALSE(ring.addSplice(641)); // past the head
    REQUIRE_FALSE(ring.addSplice(-1));  // before the first frame
    REQUIRE(ring.addSplice(640));       // at the head
    for (int64_t m = 400; ring.spliceCount() < gg::LiveSource::kMaxSplices; ++m)
    {
        REQUIRE(ring.addSplice(m));
    }
    REQUIRE_FALSE(ring.addSplice(50)); // a 33rd
    REQUIRE(std::is_sorted(ring.splices().begin(), ring.splices().end()));
    ring.clearSplices();
    REQUIRE(ring.spliceCount() == 0);
    REQUIRE(ring.splices().empty());

    REQUIRE(ring.addSplice(300));
    REQUIRE(ring.addSplice(100));
    REQUIRE(ring.addSplice(200));
    ring.selectSplice(2); // [200, 300)
    writeRamp(ring, 1088, 64);
    REQUIRE(ring.anchorAt(1087) == 300);
    REQUIRE(ring.windowFrames() == 100);
    // Frame 100 leaves storage once R − N > 100: at R = 1101, sample 1100. From that sample on the
    // numbering shifts under the selection (D6), and splice 2 is the last, [300, head), though
    // the block that writes past it began before.
    writeRamp(ring, 1152, 64);
    for (int64_t t = 1088; t < 1100; ++t)
    {
        REQUIRE(ring.anchorAt(t) == 300);
        REQUIRE(ring.windowOldestAt(t) == 200);
    }
    for (int64_t t = 1100; t < 1152; ++t)
    {
        REQUIRE(ring.anchorAt(t) == t + 1);
        REQUIRE(ring.windowOldestAt(t) == 300);
    }
    REQUIRE(ring.spliceCount() == 2);
    REQUIRE(std::vector<int64_t>(ring.splices().begin(), ring.splices().end()) ==
            std::vector<int64_t>{200, 300});
    REQUIRE(ring.windowFrames() == 1152 - 300);

    // A selection past the count is the last splice, and stays the caller's index: a later mark
    // makes a new last splice, which it then selects.
    ring.selectSplice(5);
    ring.write(nullptr, 0, 0, 1152, 0);
    REQUIRE(ring.anchorAt(1152) == 1152);
    REQUIRE(ring.windowFrames() == 1152 - 300);
    REQUIRE(ring.addSplice(1100));
    ring.write(nullptr, 0, 0, 1152, 0);
    REQUIRE(ring.windowFrames() == 1152 - 1100);
    writeRamp(ring, 1216, 64); // 200 leaves at R = 1201
    REQUIRE(ring.spliceCount() == 2);
    REQUIRE(ring.windowFrames() == 1216 - 1100);

    // Only with no mark left in storage is the window the whole ring again.
    ring.selectSplice(1);
    writeRamp(ring, 2176, 64); // 300 leaves at R = 1301, 1100 at R = 2101
    REQUIRE(ring.spliceCount() == 0);
    REQUIRE(ring.windowFrames() == 1000);
    REQUIRE(ring.anchorAt(2175) == 2176);
    REQUIRE(ring.anchorAt(2099) == 2100); // [1100, head) until 1100 left
    REQUIRE(ring.windowOldestAt(2099) == 1100);
    REQUIRE(ring.windowOldestAt(2100) == 2101 - 1000);
}

TEST_CASE("a splice leaves storage at the same sample at any block size", "[engine][live]")
{
    // Review finding 1: whether a selected splice is in effect, and which, is decided per sample
    // from R(t). A 1000-frame ring at 1 kHz, marks at 200, 300 and 650, splice 1 selected: the
    // window's ends and the admission's run at every sample are the same in blocks of 1, 64 and
    // 511 as marks age out under it, with a freeze stopping the head on the way.
    struct Sample
    {
        int64_t end;
        int64_t oldest;
        double run;
        double fade;
        bool operator==(const Sample &) const = default;
    };
    const auto trace = [](int64_t block) {
        gg::LiveSource ring;
        ring.prepare(1000.0, 1.0, 511);
        writeRamp(ring, 640, 64);
        REQUIRE(ring.addSplice(200));
        REQUIRE(ring.addSplice(300));
        REQUIRE(ring.addSplice(640));
        ring.selectSplice(1);
        std::vector<Sample> samples;
        std::vector<float> in(static_cast<std::size_t>(block), 0.5f);
        const float *mono[1] = {in.data()};
        for (int64_t start = 640; start < 2400;)
        {
            // Frozen over [1500, 1800): the setting changes between write() calls there.
            const int64_t change = start < 1500 ? 1500 : start < 1800 ? 1800 : 2400;
            const int64_t end = std::min(start + block, change);
            ring.setFrozen(start >= 1500 && start < 1800);
            ring.write(mono, 1, 0, start, static_cast<uint32_t>(end - start));
            for (int64_t t = start; t < end; ++t)
            {
                const int64_t anchor = ring.anchorAt(t);
                samples.push_back({anchor, ring.windowOldestAt(t), ring.anchorRunAfter(t),
                                   ring.fade(static_cast<double>(anchor - 300), ring.headAt(t))});
            }
            start = end;
        }
        return samples;
    };
    const auto one = trace(1);
    REQUIRE(one.size() == 2400 - 640);
    // Not vacuous: the window starts at splice 1 = [200, 300), and is the last splice, [640,
    // head), before every mark has gone (640 leaves at R = 1641, after the freeze's resume).
    REQUIRE(one.front() == Sample{300, 200, 0.0, one.front().fade});
    REQUIRE(std::any_of(one.begin(), one.end(),
                        [](const Sample &s) { return s.oldest == 640 && s.end > 640; }));
    REQUIRE(std::isinf(one.back().run)); // no mark left: the whole window, not frozen
    for (const int64_t block : {64, 511})
    {
        INFO("blocks of " << block);
        REQUIRE(trace(block) == one);
    }
}

TEST_CASE("a selected splice anchors the window", "[engine][live]")
{
    // A 4 s ring of a ramp, frozen, marks at 1 s and 2 s: splice 1 is [48000, 96000). Its end
    // anchors the window, which is 1 s long, and holds still.
    gg::LiveSource ring;
    ring.prepare(kSampleRate, 4.0, 512);
    writeRamp(ring, 4 * 48000, 512);
    ring.setFrozen(true);
    ring.write(nullptr, 0, 0, 4 * 48000, 512);
    ring.write(nullptr, 0, 0, 4 * 48000 + 512, 512);
    const int64_t head = ring.head();
    REQUIRE(head == 4 * 48000 + rampFrames(kSampleRate)); // stopped
    REQUIRE(ring.addSplice(48000));
    REQUIRE(ring.addSplice(96000));
    ring.selectSplice(1);
    const int64_t t0 = 4 * 48000 + 1024;
    ring.write(nullptr, 0, 0, t0, 512);
    for (const int64_t t : {t0, t0 + 511})
    {
        REQUIRE(ring.anchorAt(t) == 96000);
        REQUIRE(ring.anchorRunAfter(t) == 0.0);
    }
    REQUIRE(ring.windowFrames() == 48000);

    gg::LiveSource::Cursor c;
    float l = 0.0f;
    float r = 0.0f;
    REQUIRE(ring.read(96000.0 - 3.0, head, c, l, r)); // taps 95996 .. 95999
    REQUIRE(l == rampValue(95997));
    REQUIRE(r == -rampValue(95997));
    // The reads and the fade follow the storage, not the span (ruling SP-T12aa): a grain the
    // window leaves behind keeps reading its frames, and only the storage's ends fade.
    REQUIRE(ring.read(96000.0 + 1.0, head, c, l, r)); // past the span
    REQUIRE(l == rampValue(96001));
    REQUIRE(ring.read(48000.0, head, c, l, r)); // tap 47999 is before it
    REQUIRE(l == rampValue(48000));
    REQUIRE(ring.fade(48001.0, head) == 1.0);
    REQUIRE(ring.fade(96000.0 - 3.0, head) == 1.0);
    const auto newest = static_cast<double>(head);
    REQUIRE(ring.fade(newest - 3.0, head) == 0.0);
    REQUIRE(ring.fade(newest - 3.0 - kM, head) == 1.0);
    REQUIRE_FALSE(ring.read(newest - 2.0, head, c, l, r)); // tap R is past the newest frame

    // Through the engine: once splice 1 is selected, every grain admitted reads inside it.
    gg::Engine engine(liveConfig(kSampleRate, 512, 4.0));
    engine.setSourceMode(gg::SourceMode::Live);
    engine.setCloudParams(windowParams(0.5));
    SinkLog log;
    engine.setEventSink(&SinkLog::record, &log);
    run(engine, noiseInput(5 * 48000, 23), 512, {{4 * 48000, true}});
    REQUIRE(engine.liveWriteHead() == 4 * 48000 + rampFrames(kSampleRate));
    REQUIRE(engine.live().addSplice(48000));
    REQUIRE(engine.live().addSplice(96000));
    engine.live().selectSplice(1);
    const std::size_t before = log.grains.size();
    const auto out = run(engine, noiseInput(48000, 24), 512);
    REQUIRE(allFinite(out.left));
    REQUIRE(peak(out.left) > 0.01f);
    REQUIRE(log.grains.size() - before > 50);
    for (std::size_t k = before; k < log.grains.size(); ++k)
    {
        const gg::GrainSpec &g = log.grains[k];
        INFO("grain " << k << " at " << g.startPos);
        REQUIRE(g.startPos >= 48000.0 + 1.0 + kM);
        REQUIRE(g.startPos + g.durationSamples * g.rate <= 96000.0 - 3.0 - kM + 1e-6);
    }
}

TEST_CASE("adopting storage keeps the window", "[engine][live]")
{
    // A 2 s ring of a ramp, written past a wrap. The main thread copies the window as of R0 into
    // a 4 s storage, the audio thread writes 4096 more frames, and the adoption copies those
    // itself: every frame of the old window then reads as it was, from the new storage.
    constexpr int64_t kN = 96000;
    gg::LiveSource ring;
    ring.prepare(kSampleRate, 2.0, 512);
    writeRamp(ring, 3 * 48000, 512);
    const int64_t r0 = ring.head();

    gg::RingStorage bigger(4 * 48000, 512);
    REQUIRE(bigger.frames() == 4 * 48000);
    REQUIRE(bigger.capacity() == 4 * 48000 + 512);
    // Level 0's slots and guard, then levels 1 to 3's, ceil(C / 2ⁿ) + 15 slots each (spec §6.9).
    std::size_t levelSlots = 0;
    for (uint32_t n = 1; n < 4; ++n)
    {
        levelSlots += ((4 * 48000 + 512 + (1u << n) - 1) >> n) + 15;
    }
    REQUIRE(bigger.levelCount() == 4);
    REQUIRE(bigger.bytes() == 2 * (4 * 48000 + 512 + 15 + levelSlots) * sizeof(float));
    copyFrames(ring, bigger, r0 - kN, r0);
    writeRamp(ring, r0 + 4096, 512);
    const int64_t head = ring.head();

    gg::RingStorage *previous = &bigger; // overwritten
    // A gap past kAdoptCopyFrames, or a storage without a block's slack, is declined.
    REQUIRE_FALSE(
        ring.adoptStorage(&bigger, head - gg::LiveSource::kAdoptCopyFrames - 1, previous));
    gg::RingStorage narrow(4 * 48000, 256);
    REQUIRE_FALSE(ring.adoptStorage(&narrow, r0, previous));
    REQUIRE(ring.frames() == kN);

    REQUIRE(ring.adoptStorage(&bigger, r0, previous));
    REQUIRE(previous == nullptr); // the ring owned its first storage
    REQUIRE(ring.head() == head);
    REQUIRE(ring.frames() == 4 * 48000);
    REQUIRE(ring.view().slots == bigger.slots());
    gg::LiveSource::Cursor c;
    bool same = true;
    for (int64_t f = head - kN + 1; f <= head - 3; ++f)
    {
        float l = 0.0f;
        float r = 0.0f;
        same = same && ring.read(static_cast<double>(f), head, c, l, r) && l == rampValue(f) &&
               r == -rampValue(f);
    }
    REQUIRE(same);
    for (int64_t g = 0; g < gg::RingStorage::kGuardFrames; ++g) // they mirror slots 0 .. 14
    {
        REQUIRE(bigger.slots()[2 * (bigger.capacity() + g)] == bigger.slots()[2 * g]);
        REQUIRE(bigger.slots()[2 * (bigger.capacity() + g) + 1] == bigger.slots()[2 * g + 1]);
    }
    // Ring time continues: the next frames land after the head.
    writeRamp(ring, head + 1000, 512);
    float l = 0.0f;
    float r = 0.0f;
    REQUIRE(ring.read(static_cast<double>(head + 500), ring.head(), c, l, r));
    REQUIRE(l == rampValue(head + 500));

    // A storage the caller owns is handed back when replaced; a 1 s one keeps the newest second.
    gg::RingStorage second(48000, 512);
    copyFrames(ring, second, ring.head() - 48000, ring.head());
    REQUIRE(ring.adoptStorage(&second, ring.head(), previous));
    REQUIRE(previous == &bigger);
    REQUIRE(ring.frames() == 48000);
    const int64_t now = ring.head();
    same = true;
    for (int64_t f = now - 48000 + 1; f <= now - 3; ++f)
    {
        same = same && ring.read(static_cast<double>(f), now, c, l, r) && l == rampValue(f);
    }
    REQUIRE(same);
    REQUIRE_FALSE(ring.read(static_cast<double>(now - 48000), now, c, l, r));
}

TEST_CASE("restoring storage restarts ring time", "[engine][live]")
{
    // A restore (spec §6.6): the storage arrives filled with frames [0, frames), the head at the
    // frame count, the marks with it; not frozen, or held from the first sample.
    const auto filled = [](gg::RingStorage &next) {
        for (int64_t f = 0; f < 48000; ++f)
        {
            next.slots()[2 * f] = rampValue(f);
            next.slots()[2 * f + 1] = -rampValue(f);
        }
    };
    const int64_t marks[] = {12000, 24000};
    const std::vector<float> ones(512, 1.0f);
    const float *mono[1] = {ones.data()};
    for (const bool frozen : {false, true})
    {
        INFO((frozen ? "frozen" : "not frozen"));
        gg::LiveSource ring;
        ring.prepare(kSampleRate, 512);
        REQUIRE_FALSE(ring.enabled());
        ring.setFrozen(!frozen); // the restore sets it
        gg::RingStorage next(48000, 512);
        filled(next);
        gg::RingStorage *previous = &next; // overwritten
        REQUIRE(ring.restoreStorage(&next, 48000, marks, frozen, previous));
        REQUIRE(previous == nullptr); // there was no storage
        REQUIRE(ring.enabled());
        REQUIRE(ring.head() == 48000);
        REQUIRE(ring.frames() == 48000);
        REQUIRE(std::vector<int64_t>(ring.splices().begin(), ring.splices().end()) ==
                std::vector<int64_t>{12000, 24000});
        gg::LiveSource::Cursor c;
        bool same = true;
        for (int64_t f = 1; f <= 47996; ++f)
        {
            float l = 0.0f;
            float r = 0.0f;
            same = same && ring.read(static_cast<double>(f), 48000, c, l, r) && l == rampValue(f) &&
                   r == -rampValue(f);
        }
        REQUIRE(same);
        if (frozen)
        {
            // Held from the first sample: the head never moves and no frame is written, so the
            // restored window renders as the saved session did.
            REQUIRE(ring.headRunAfter(0) == 0.0);
            ring.write(mono, 1, 0, 0, 512);
            REQUIRE(ring.head() == 48000);
            REQUIRE(ring.headRunAfter(511) == 0.0);
            REQUIRE(storedFrame(ring.view(), 47999).first == rampValue(47999));
            REQUIRE(storedFrame(ring.view(), 48000).first == 0.0f); // nothing written
            ring.setFrozen(false); // released, the head resumes, ramping up from 0
            ring.write(mono, 1, 0, 512, 512);
            REQUIRE(ring.head() == 48512);
            REQUIRE(storedFrame(ring.view(), 48000).first == 1.0f / 960.0f);
        }
        else
        {
            // The write gain is 1 and ring time goes on from the head.
            REQUIRE(std::isinf(ring.headRunAfter(0)));
            ring.write(mono, 1, 0, 0, 512);
            REQUIRE(ring.head() == 48512);
            REQUIRE(storedFrame(ring.view(), 48000).first == 1.0f);
        }
    }

    // Refused, changing nothing: a frame count past the capacity or below 0, more marks than
    // kMaxSplices, a storage without a block's slack.
    gg::LiveSource ring;
    ring.prepare(kSampleRate, 512);
    gg::RingStorage next(48000, 512);
    gg::RingStorage *previous = &next;
    REQUIRE_FALSE(ring.restoreStorage(&next, next.capacity() + 1, marks, false, previous));
    REQUIRE(previous == nullptr);
    REQUIRE_FALSE(ring.restoreStorage(&next, -1, marks, false, previous));
    std::vector<int64_t> many(gg::LiveSource::kMaxSplices + 1);
    for (std::size_t k = 0; k < many.size(); ++k)
    {
        many[k] = 1000 + static_cast<int64_t>(k) * 100;
    }
    REQUIRE_FALSE(ring.restoreStorage(&next, 48000, many, false, previous));
    gg::RingStorage narrow(48000, 256);
    REQUIRE_FALSE(ring.restoreStorage(&narrow, 48000, marks, false, previous));
    REQUIRE_FALSE(ring.enabled());
    REQUIRE(ring.head() == 0);
    many.pop_back(); // exactly kMaxSplices
    REQUIRE(ring.restoreStorage(&next, next.capacity(), many, false, previous));
    REQUIRE(ring.head() == next.capacity());
    REQUIRE(ring.spliceCount() == gg::LiveSource::kMaxSplices);
}

namespace
{
// A change made between process() calls at output sample `at`.
struct WindowChange
{
    int64_t at;
    std::function<void(gg::Engine &)> apply;
};

struct LiveRender
{
    Stereo out;
    std::vector<gg::GrainSpec> grains;
};

// A 1 s ring in Live mode over `input`, with `gains` as the overdub gains (one per sample), in
// process() calls of `block` frames cut short at each change, made at its sample.
LiveRender renderWindowed(const Stereo &input, const std::vector<float> &gains, uint32_t block,
                          const std::vector<WindowChange> &changes)
{
    gg::Engine engine(liveConfig(kSampleRate, 511, 1.0));
    engine.setSourceMode(gg::SourceMode::Live);
    engine.setCloudParams(windowParams(0.3));
    SinkLog log;
    engine.setEventSink(&SinkLog::record, &log);
    const auto frames = static_cast<int64_t>(input.left.size());
    LiveRender r{{std::vector<float>(input.left.size()), std::vector<float>(input.left.size())},
                 {}};
    std::size_t next = 0;
    for (int64_t done = 0; done < frames;)
    {
        while (next < changes.size() && changes[next].at == done)
        {
            changes[next++].apply(engine);
        }
        int64_t end = std::min<int64_t>(done + block, frames);
        if (next < changes.size())
        {
            end = std::min(end, changes[next].at);
        }
        const float *in[2] = {input.left.data() + done, input.right.data() + done};
        float *o[2] = {r.out.left.data() + done, r.out.right.data() + done};
        engine.process(in, 2, o, 2, static_cast<uint32_t>(end - done), gains.data() + done);
        done = end;
    }
    REQUIRE_FALSE(log.overflowed);
    r.grains = log.grains;
    return r;
}
} // namespace

TEST_CASE("a loop with overdub and a splice across ageing are block-size invariant",
          "[engine][live][determinism]")
{
    // Spec §7: the window, the overdub and the marks are functions of the sample clock and the
    // changes' samples, so output and grain events are identical in blocks of 1, 32, 64 and 511.
    const int64_t frames = 2 * 48000;
    const auto input = noiseInput(frames, 31);
    std::vector<float> gains(static_cast<std::size_t>(frames));
    for (std::size_t i = 0; i < gains.size(); ++i)
    {
        gains[i] = i % 9000 < 1500 ? 0.0f : 0.25f + 0.5f * static_cast<float>(i % 13) / 13.0f;
    }
    const std::vector<float> none(static_cast<std::size_t>(frames), 0.0f);

    SECTION("a loop with overdub")
    {
        const std::vector<WindowChange> changes = {
            {20000, [](gg::Engine &e) { e.live().setLoop(true, 9000); }},
            {50000, [](gg::Engine &e) { e.setFreeze(true); }},
            {60000, [](gg::Engine &e) { e.setFreeze(false); }},
            {70000, [](gg::Engine &e) { e.live().setLoop(true, 30000); }},
        };
        const LiveRender first = renderWindowed(input, gains, 1, changes);
        REQUIRE(peak(first.out.left) > 0.05f);
        REQUIRE(first.grains.size() > 100);
        for (const uint32_t block : {32u, 64u, 511u})
        {
            INFO("blocks of " << block);
            const LiveRender r = renderWindowed(input, gains, block, changes);
            REQUIRE(sameBits(r.out.left, first.out.left));
            REQUIRE(sameBits(r.out.right, first.out.right));
            REQUIRE(r.grains == first.grains);
        }
    }

    SECTION("a selected splice as its marks age out")
    {
        // Marks at ring times 20000, 30000 and 40000 (R = t + 1, not frozen), splice 1 from sample
        // 60000: [20000, 30000) until R = 68001, [30000, 40000) until 78001, then [40000, head)
        // until 88001, then the whole ring.
        const std::vector<WindowChange> changes = {
            {60000,
             [](gg::Engine &e) {
                 REQUIRE(e.live().addSplice(20000));
                 REQUIRE(e.live().addSplice(30000));
                 REQUIRE(e.live().addSplice(40000));
                 e.live().selectSplice(1);
             }},
        };
        const LiveRender first = renderWindowed(input, none, 1, changes);
        REQUIRE(peak(first.out.left) > 0.05f);
        // Not vacuous: grains admitted in each splice in turn, and after the last mark left.
        const auto admittedIn = [&](int64_t from, int64_t to, double lo, double hi) {
            return std::count_if(first.grains.begin(), first.grains.end(), [&](const auto &g) {
                return g.onsetSample >= from && g.onsetSample < to && g.startPos >= lo &&
                       g.startPos < hi;
            });
        };
        REQUIRE(admittedIn(60000, 68000, 20000.0, 30000.0) > 5);
        REQUIRE(admittedIn(68001, 78000, 30000.0, 40000.0) > 5);
        REQUIRE(admittedIn(78001, 88000, 40000.0, 88001.0) > 5);
        REQUIRE(admittedIn(60000, 68000, 30000.0, 1e9) == 0);
        for (const uint32_t block : {32u, 64u, 511u})
        {
            INFO("blocks of " << block);
            const LiveRender r = renderWindowed(input, none, block, changes);
            REQUIRE(sameBits(r.out.left, first.out.left));
            REQUIRE(sameBits(r.out.right, first.out.right));
            REQUIRE(r.grains == first.grains);
        }
    }
}

namespace
{
// The goldens' click limit (gg::metrics::Tolerances::maxClickScore, metricsVersion 2).
constexpr double kClickLimit = 15.0;

// The goldens' click score of out[from, to), both channels, at 48 kHz.
double clickScoreIn(const Stereo &out, int64_t from, int64_t to)
{
    const auto a = static_cast<std::ptrdiff_t>(std::max<int64_t>(0, from));
    const auto b =
        static_cast<std::ptrdiff_t>(std::min<int64_t>(to, static_cast<int64_t>(out.left.size())));
    const gg::metrics::AudioData audio{
        kSampleRate,
        {std::vector<float>(out.left.begin() + a, out.left.begin() + b),
         std::vector<float>(out.right.begin() + a, out.right.begin() + b)}};
    return gg::metrics::computeMetrics(audio).clickScore;
}

// Dense live grains (32 Hz of 50 ms, nearly periodic: about 1.6 sounding at once, and never a
// gap) at a tier and a pitch, `position`
// seconds behind the window's end with `spray` more, over a 2 s ring of `input`, in process()
// calls of `block` frames cut short at each change, made at its sample.
LiveRender renderDense(const Stereo &input, uint64_t seed, gg::Tier tier, double pitch,
                       double position, double spray, uint32_t block,
                       const std::vector<WindowChange> &changes)
{
    gg::EngineConfig config = liveConfig(kSampleRate, 511, 2.0);
    config.seed = seed;
    gg::Engine engine(config);
    engine.setSourceMode(gg::SourceMode::Live);
    engine.setCloudParams({.densityHz = 32.0,
                           .distribution = 0.1,
                           .grainDurationSec = 0.05,
                           .position = position,
                           .spray = spray,
                           .pitchSemitones = pitch,
                           .stereo = 0.5,
                           .gain = 0.5f,
                           .quality = tier});
    SinkLog log;
    engine.setEventSink(&SinkLog::record, &log);
    const auto frames = static_cast<int64_t>(input.left.size());
    LiveRender r{{std::vector<float>(input.left.size()), std::vector<float>(input.left.size())},
                 {}};
    std::size_t next = 0;
    for (int64_t done = 0; done < frames;)
    {
        while (next < changes.size() && changes[next].at == done)
        {
            changes[next++].apply(engine);
        }
        int64_t end = std::min<int64_t>(done + block, frames);
        if (next < changes.size())
        {
            end = std::min(end, changes[next].at);
        }
        const float *in[2] = {input.left.data() + done, input.right.data() + done};
        float *o[2] = {r.out.left.data() + done, r.out.right.data() + done};
        engine.process(in, 2, o, 2, static_cast<uint32_t>(end - done));
        done = end;
    }
    REQUIRE_FALSE(log.overflowed);
    r.grains = log.grains;
    return r;
}

// How many of the grains are sounding at output sample t.
std::size_t soundingAt(const std::vector<gg::GrainSpec> &grains, int64_t t)
{
    return static_cast<std::size_t>(std::count_if(grains.begin(), grains.end(), [t](const auto &g) {
        const double onset = static_cast<double>(g.onsetSample) + g.onsetFrac;
        const auto at = static_cast<double>(t);
        return onset < at && at < onset + g.durationSamples;
    }));
}
} // namespace

TEST_CASE("a window change under sounding grains doesn't click", "[engine][live][determinism]")
{
    // M2 sub-project 2's spec §3 (no click at a splice change) and ruling SP-T12aa: a grain
    // admitted under one window finishes in its old span when the window narrows, moves or widens,
    // while the grains after it are admitted into the new one. Dense grains over a 220 Hz sine, so
    // every change falls under sounding grains; each change's click score (the goldens', over
    // 0.1 s before it to 0.15 s after) and the whole render's stay below the limit, at every tier
    // and at ring levels 0 to 2, for six seeds (before the fix each render scored 84 to 2,955).
    // Output and grain events are the same in blocks of 1, 64 and 511.
    struct Schedule
    {
        const char *name;
        double position;
        double spray;
        std::vector<WindowChange> changes;
        std::vector<int64_t> windowChanges; // the samples where the window changes
    };
    // Splices on a ring frozen at 1.5 s (its head stops 960 samples later): marks 1.2, 0.8 and
    // 0.4 s behind the head, so three spans of 0.4 s; splice 1, 3, 2, then the whole ring.
    constexpr int64_t kStopped = 72000 + 960;
    const std::vector<WindowChange> splices = {
        {72000, [](gg::Engine &e) { e.setFreeze(true); }},
        {76800,
         [](gg::Engine &e) {
             REQUIRE(e.liveWriteHead() == kStopped);
             for (const int64_t behind : {57600, 38400, 19200})
             {
                 REQUIRE(e.live().addSplice(kStopped - behind));
             }
         }},
        {86400, [](gg::Engine &e) { e.live().selectSplice(1); }},
        {110400, [](gg::Engine &e) { e.live().selectSplice(3); }},
        {134400, [](gg::Engine &e) { e.live().selectSplice(2); }},
        {158400, [](gg::Engine &e) { e.live().selectSplice(0); }},
    };
    // A loop on an unfrozen ring, Position 0.4 s behind: on at 0.4 s, narrowed to 0.2 s (as short
    // as level 2's grains, reading 0.2 s, allow), widened to 1 s, then off.
    const std::vector<WindowChange> loops = {
        {48000, [](gg::Engine &e) { e.live().setLoop(true, 19200); }},
        {76800, [](gg::Engine &e) { e.live().setLoop(true, 9600); }},
        {105600, [](gg::Engine &e) { e.live().setLoop(true, 48000); }},
        {134400, [](gg::Engine &e) { e.live().setLoop(false, 48000); }},
    };
    const std::vector<Schedule> schedules = {
        {"splices, frozen", 0.1, 0.05, splices, {86400, 110400, 134400, 158400}},
        {"a loop, unfrozen", 0.4, 0.2, loops, {48000, 76800, 105600, 134400}},
    };
    struct Tier
    {
        const char *name;
        gg::Tier tier;
        double pitch;
    };
    const std::vector<Tier> tiers = {
        {"Eco", gg::Tier::Eco, 0.0},         {"Normal", gg::Tier::Normal, 0.0},
        {"HQ, level 0", gg::Tier::HQ, 0.0},  {"HQ, level 1", gg::Tier::HQ, 12.0},
        {"HQ, level 2", gg::Tier::HQ, 24.0},
    };
    // Faded in over 10 ms with a raised cosine, as gg-render fades an FX input (ruling M1-v), so
    // the sine's own start doesn't click when the grains reach frame 0.
    Stereo input = sineInput(182400, kSampleRate, 220.0, 0.5f);
    for (int64_t i = 0; i < 480; ++i)
    {
        const double s = std::sin(0.5 * std::numbers::pi * static_cast<double>(i) / 480.0);
        input.left[static_cast<std::size_t>(i)] *= static_cast<float>(s * s);
        input.right[static_cast<std::size_t>(i)] *= static_cast<float>(s * s);
    }
    for (const Schedule &schedule : schedules)
    {
        for (const Tier &tier : tiers)
        {
            INFO(schedule.name << ", " << tier.name);
            for (const uint64_t seed : {2u, 3u, 4u, 5u, 6u, 7u})
            {
                INFO("seed " << seed);
                const LiveRender r =
                    renderDense(input, seed, tier.tier, tier.pitch, schedule.position,
                                schedule.spray, 511, schedule.changes);
                REQUIRE(allFinite(r.out.left));
                REQUIRE(peak(r.out.left) > 0.05f);
                for (const int64_t at : schedule.windowChanges)
                {
                    INFO("the change at " << at);
                    REQUIRE(soundingAt(r.grains, at) > 0); // not vacuous
                    const double score = clickScoreIn(r.out, at - 4800, at + 7200);
                    CHECK(score <= kClickLimit);
                }
                CHECK(clickScoreIn(r.out, 0, 182400) <= kClickLimit);
                if (seed == 3)
                {
                    for (const uint32_t block : {1u, 64u})
                    {
                        INFO("blocks of " << block);
                        const LiveRender b =
                            renderDense(input, seed, tier.tier, tier.pitch, schedule.position,
                                        schedule.spray, block, schedule.changes);
                        REQUIRE(sameBits(b.out.left, r.out.left));
                        REQUIRE(sameBits(b.out.right, r.out.right));
                        REQUIRE(b.grains == r.grains);
                    }
                }
            }
        }
    }
}

namespace
{
// A ring shrink's or grow's run (ruling SP-T12ad): dense live grains (32 Hz of 50 ms, nearly
// periodic) at a tier and pitch, `position` seconds behind the head with `spray` more, over
// `input` in a ring of `ringSeconds`, in process() calls of `block` frames (cut at limitAt). At
// limitAt the engine is given the ring limit `newFrames`, as the Processor gives it a Ring
// Length; between calls the plugin's watcher is played: from limitAt + landingAfter, once the
// reads fit the new length (LiveSource::readFrames), the stable part of the window and its levels
// are copied into a storage of newFrames, which the next call adopts at its start if the engine
// lets it (Engine::liveRingFits, as Processor::adoptRing asks), then ringResized().
struct ShrinkRun
{
    Stereo out;
    std::vector<gg::GrainSpec> grains;
    int64_t adoptedAt{-1};
};
ShrinkRun renderShrink(const Stereo &input, uint64_t seed, gg::Tier tier, double pitch,
                       double position, double spray, uint32_t block, double ringSeconds,
                       int64_t limitAt, int64_t newFrames, int64_t landingAfter)
{
    gg::EngineConfig config = liveConfig(kSampleRate, 512, ringSeconds);
    config.seed = seed;
    gg::Engine engine(config);
    engine.setSourceMode(gg::SourceMode::Live);
    engine.setCloudParams({.densityHz = 32.0,
                           .distribution = 0.1,
                           .grainDurationSec = 0.05,
                           .position = position,
                           .spray = spray,
                           .pitchSemitones = pitch,
                           .stereo = 0.5,
                           .gain = 0.5f,
                           .quality = tier});
    SinkLog log;
    engine.setEventSink(&SinkLog::record, &log);
    const auto frames = static_cast<int64_t>(input.left.size());
    ShrinkRun r{
        {std::vector<float>(input.left.size()), std::vector<float>(input.left.size())}, {}, -1};
    std::unique_ptr<gg::RingStorage> next;
    int64_t copiedTo = 0;
    for (int64_t done = 0; done < frames;)
    {
        if (done == limitAt)
        {
            engine.setRingLimit(newFrames);
        }
        gg::LiveSource &live = engine.live();
        if (next != nullptr && engine.liveRingFits(newFrames))
        {
            gg::RingStorage *previous = nullptr;
            REQUIRE(live.adoptStorage(next.get(), copiedTo, previous));
            REQUIRE(previous == nullptr); // the engine's own, kept until it is destroyed
            engine.ringResized();
            r.adoptedAt = done;
            next.release(); // the engine's from here: held below
        }
        else if (next == nullptr && r.adoptedAt < 0 && done >= limitAt + landingAfter &&
                 live.readFrames() <= newFrames && live.frames() != newFrames)
        {
            next = std::make_unique<gg::RingStorage>(newFrames, 512);
            copiedTo = live.head();
            const gg::RingStorage &from = *live.storage();
            const int64_t first =
                std::max<int64_t>(0, copiedTo - std::min(newFrames, live.frames()));
            for (int64_t f = first; f < copiedTo; ++f)
            {
                const int64_t s = f % from.capacity();
                const int64_t d = f % next->capacity();
                next->slots()[2 * d] = from.slots()[2 * s];
                next->slots()[2 * d + 1] = from.slots()[2 * s + 1];
            }
            gg::copyLevels(from, *next, first, copiedTo);
        }
        int64_t end = std::min<int64_t>(done + block, frames);
        if (done < limitAt)
        {
            end = std::min(end, limitAt);
        }
        const float *in[2] = {input.left.data() + done, input.right.data() + done};
        float *o[2] = {r.out.left.data() + done, r.out.right.data() + done};
        engine.process(in, 2, o, 2, static_cast<uint32_t>(end - done));
        done = end;
    }
    REQUIRE_FALSE(log.overflowed);
    r.grains = log.grains;
    // The adopted storage, which the engine doesn't own, outlives every read: freed here, after
    // the last call.
    if (r.adoptedAt >= 0)
    {
        std::unique_ptr<gg::RingStorage> adopted(
            const_cast<gg::RingStorage *>(engine.live().storage()));
        engine.live().prepare(kSampleRate, 512u); // lets go of it before it is freed
    }
    return r;
}

// A 220 Hz sine faded in over 10 ms with a raised cosine, as gg-render fades an FX input.
Stereo fadedSine(int64_t frames)
{
    Stereo input = sineInput(frames, kSampleRate, 220.0, 0.5f);
    for (int64_t i = 0; i < 480; ++i)
    {
        const double s = std::sin(0.5 * std::numbers::pi * static_cast<double>(i) / 480.0);
        input.left[static_cast<std::size_t>(i)] *= static_cast<float>(s * s);
        input.right[static_cast<std::size_t>(i)] *= static_cast<float>(s * s);
    }
    return input;
}
} // namespace

TEST_CASE("a ring limit narrows admission at once and the reads once the grains allow",
          "[engine][live]")
{
    // Ruling SP-T12ad on a 4 s ring: grains 2 to 2.5 s behind the head, then a limit of 1 s at
    // 3 s. From that call every grain is admitted at least M frames inside the newest second,
    // less kCapSlack, and Position and Spray reach a second; the reads keep the 4 s until the
    // last grain admitted before reaches its end, then narrow to the second, and the engine lets
    // a 1 s ring be adopted only from there.
    constexpr int64_t kLimitAt = 3 * 48000;
    constexpr int64_t kN1 = 48000;
    const Stereo input = noiseInput(4 * 48000, 41);
    gg::Engine engine(liveConfig(kSampleRate, 512, 4.0));
    engine.setSourceMode(gg::SourceMode::Live);
    engine.setCloudParams({.densityHz = 64.0,
                           .grainDurationSec = 0.05,
                           .position = 2.0,
                           .spray = 0.5,
                           .stereo = 0.5,
                           .gain = 0.5f});
    SinkLog log;
    engine.setEventSink(&SinkLog::record, &log);
    const int64_t n = engine.liveFrames();
    run(engine,
        Stereo{std::vector<float>(input.left.begin(), input.left.begin() + kLimitAt),
               std::vector<float>(input.right.begin(), input.right.begin() + kLimitAt)},
        256);
    const std::size_t before = log.grains.size();
    REQUIRE(before > 100);
    // The last end of the grains admitted before the limit: all read farther back than 1 s.
    int64_t lastEnd = 0;
    for (const gg::GrainSpec &g : log.grains)
    {
        lastEnd = std::max(lastEnd, gg::grainEndSample(g));
    }
    REQUIRE(lastEnd > kLimitAt + 256);
    engine.setRingLimit(kN1);
    CHECK(engine.live().readFrames() == n);
    CHECK_FALSE(engine.liveRingFits(kN1));
    for (int64_t done = kLimitAt; done < 4 * 48000; done += 256)
    {
        const auto count = static_cast<uint32_t>(std::min<int64_t>(256, 4 * 48000 - done));
        const float *in[2] = {input.left.data() + done, input.right.data() + done};
        std::vector<float> l(256), r(256);
        float *o[2] = {l.data(), r.data()};
        engine.process(in, 2, o, 2, count);
        const int64_t now = done + count;
        INFO("after the call ending at " << now);
        REQUIRE(engine.live().windowFrames() <= kN1 - gg::LiveSource::kCapSlack);
        if (now == lastEnd)
        {
            continue; // checked at the next call's first sample
        }
        REQUIRE(engine.live().readFrames() == (now < lastEnd ? n : kN1));
        REQUIRE(engine.liveReadLimitPending() == (now < lastEnd));
        REQUIRE(engine.liveRingFits(kN1) == (now > lastEnd));
    }
    REQUIRE(log.grains.size() - before > 40);
    for (std::size_t k = before; k < log.grains.size(); ++k)
    {
        const gg::GrainSpec &g = log.grains[k];
        INFO("grain " << k << " at " << g.startPos);
        // Not frozen: R = onset + 1 at the onset, and the window's oldest frame moves with it.
        const double rho = static_cast<double>(g.onsetSample) + g.onsetFrac + 1.0;
        REQUIRE(g.startPos >=
                rho - static_cast<double>(kN1 - gg::LiveSource::kCapSlack) + 1.0 + kM - 1e-6);
    }
}

TEST_CASE("a ring shrink waits for its grains and lands inaudibly whenever it is offered",
          "[engine][live][determinism]")
{
    // Ruling SP-T12ad: a 16 s ring limited to 4 s at 9 s, and a 60 s ring to 16 s at 25 s, under
    // dense grains 6 to 8 s (20 to 24 s) behind the head, over a faded 220 Hz sine; and a 16 s
    // ring grown to 60 s. The shorter storage is offered as soon as the reads fit it, or 0.5 s
    // later: the output is the same bytes either way, and with the watcher's calls of 64 frames,
    // and its click score stays below the limit from 0.1 s before the limit to 1.5 s after, and
    // over the whole render (before the ruling, an adoption under such grains scored 275 to
    // 2,754), at Eco, Normal and HQ at ring levels 0 and 1, for two seeds.
    struct Case
    {
        const char *name;
        double ringSeconds;
        double position;
        double spray;
        int64_t limitAt;
        int64_t newFrames;
        int64_t frames;
        bool allTiers;
    };
    const std::vector<Case> cases = {
        {"16 s to 4 s", 16.0, 6.0, 2.0, 9 * 48000, 4 * 48000, 11 * 48000, true},
        {"60 s to 16 s", 60.0, 20.0, 4.0, 25 * 48000, 16 * 48000, 27 * 48000, false},
        {"16 s to 60 s", 16.0, 6.0, 2.0, 9 * 48000, 60 * 48000, 11 * 48000, false},
    };
    struct Tier
    {
        const char *name;
        gg::Tier tier;
        double pitch;
    };
    const std::vector<Tier> tiers = {{"Eco", gg::Tier::Eco, 0.0},
                                     {"Normal", gg::Tier::Normal, 0.0},
                                     {"HQ, level 0", gg::Tier::HQ, 0.0},
                                     {"HQ, level 1", gg::Tier::HQ, 12.0}};
    for (const Case &c : cases)
    {
        const Stereo input = fadedSine(c.frames);
        for (const Tier &tier : tiers)
        {
            if (!c.allTiers && tier.tier != gg::Tier::Normal && tier.pitch == 0.0)
            {
                continue; // the long cases at Normal and at HQ level 1
            }
            INFO(c.name << ", " << tier.name);
            for (const uint64_t seed : {2u, 3u})
            {
                INFO("seed " << seed);
                const ShrinkRun first =
                    renderShrink(input, seed, tier.tier, tier.pitch, c.position, c.spray, 512,
                                 c.ringSeconds, c.limitAt, c.newFrames, 0);
                REQUIRE(first.adoptedAt > c.limitAt);
                REQUIRE(peak(first.out.left, static_cast<std::size_t>(c.limitAt - 48000),
                             static_cast<std::size_t>(c.limitAt)) > 0.05f);
                CHECK(clickScoreIn(first.out, c.limitAt - 4800, c.limitAt + 72000) <= kClickLimit);
                CHECK(clickScoreIn(first.out, 0, c.frames) <= kClickLimit);
                const ShrinkRun late =
                    renderShrink(input, seed, tier.tier, tier.pitch, c.position, c.spray, 512,
                                 c.ringSeconds, c.limitAt, c.newFrames, 24000);
                REQUIRE(late.adoptedAt >= c.limitAt + 24000);
                CHECK(sameBits(late.out.left, first.out.left));
                CHECK(sameBits(late.out.right, first.out.right));
                CHECK(late.grains == first.grains);
                if (seed == 2)
                {
                    const ShrinkRun small =
                        renderShrink(input, seed, tier.tier, tier.pitch, c.position, c.spray, 64,
                                     c.ringSeconds, c.limitAt, c.newFrames, 0);
                    CHECK(sameBits(small.out.left, first.out.left));
                    CHECK(sameBits(small.out.right, first.out.right));
                    CHECK(small.grains == first.grains);
                }
            }
        }
    }
}

TEST_CASE("a restore's window holds for every sample until the next write", "[engine][live]")
{
    // Between a restore and the next write() the head is the restored frame count at every output
    // sample, so a query made then (the Processor's AddSplice at a call's first boundary, the
    // admission clamp, a capture) reads the restored window: the last write's block bounds are
    // forgotten with the old ring time.
    const std::vector<float> ones(512, 1.0f);
    const float *mono[1] = {ones.data()};
    const int64_t marks[] = {1000, 9000};
    for (const bool frozen : {false, true})
    {
        INFO((frozen ? "frozen" : "not frozen"));
        gg::LiveSource ring;
        ring.prepare(kSampleRate, kRingSeconds, 512);
        for (int64_t t = 0; t < 100352; t += 512)
        {
            ring.write(mono, 1, 0, t, 512); // the last block starts at 99840
        }
        ring.selectSplice(1);
        gg::RingStorage next(48000, 512);
        gg::RingStorage *previous = &next;
        REQUIRE(ring.restoreStorage(&next, 30000, marks, frozen, previous));
        REQUIRE(ring.windowFrames() == 8000); // splice 1: [1000, 9000)
        for (const int64_t t : {int64_t{0}, int64_t{50}, int64_t{99839}, int64_t{99840},
                                int64_t{100351}, int64_t{200000}})
        {
            INFO("t = " << t);
            CHECK(ring.headAt(t) == 30000);
            CHECK(ring.anchorAt(t) == 9000);
            CHECK(ring.windowOldestAt(t) == 1000);
            CHECK(ring.windowFramesAt(t) == ring.windowFrames());
            CHECK(ring.windowSteadyUntil(t) == std::numeric_limits<int64_t>::max());
            CHECK(ring.headRunAfter(t) == (frozen ? 0.0 : std::numeric_limits<double>::infinity()));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The ring's pyramid (M2 sub-project 2's spec §6.9): levels 1 .. 3 of sub-project 3's half-band,
// written incrementally as level 0 is, and filled by the same function on a resize or restore.

namespace
{
// Level n's frame m of a ring's storage: slot m mod its capacity.
std::pair<float, float> levelFrame(const gg::LiveSource &ring, uint32_t n, int64_t m)
{
    const gg::LiveSource::RingView v = ring.level(n).view;
    const int64_t s = m % v.capacity;
    return {v.slots[2 * s], v.slots[2 * s + 1]};
}

// Two floats with the same bits (std::memcmp: +0 and −0 differ, as do two NaNs' payloads).
bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// A file of the same frames: input frames [0, frames) as a stereo source, with its pyramid.
std::shared_ptr<const gg::PreparedSource> fileOf(const Stereo &input, int64_t frames)
{
    auto buffer = std::make_shared<gg::SampleBuffer>(2, frames, kSampleRate);
    std::copy_n(input.left.begin(), frames, buffer->channel(0));
    std::copy_n(input.right.begin(), frames, buffer->channel(1));
    return gg::PreparedSource::make(std::move(buffer));
}

// Writes input frames [ring.head(), to) into the ring in blocks of `lengths` in turn, output
// sample t carrying frame t (not frozen); calls after() after each block.
template <class After>
void writeInput(gg::LiveSource &ring, const Stereo &input, int64_t to,
                const std::vector<int64_t> &lengths, After &&after)
{
    std::size_t block = 0;
    for (int64_t start = ring.head(); start < to;)
    {
        const int64_t n = std::min(lengths[block++ % lengths.size()], to - start);
        const auto at = static_cast<std::size_t>(start);
        const float *in[2] = {input.left.data() + at, input.right.data() + at};
        ring.write(in, 2, 0, start, static_cast<uint32_t>(n));
        start += n;
        after();
    }
}
void writeInput(gg::LiveSource &ring, const Stereo &input, int64_t to,
                const std::vector<int64_t> &lengths)
{
    writeInput(ring, input, to, lengths, [] {});
}

// Every level-n frame m in [first, end) of the ring equals the file's frame m bit for bit;
// counts the frames compared into `compared`.
bool levelMatchesFile(const gg::LiveSource &ring, const gg::PreparedSource &file, uint32_t n,
                      int64_t first, int64_t end, int64_t &compared)
{
    const gg::PreparedSource::Level &lv = file.level(n);
    for (int64_t m = std::max<int64_t>(0, first); m < end; ++m)
    {
        const auto [l, r] = levelFrame(ring, n, m);
        if (!sameBits(l, lv.data[0][lv.stride * m]) || !sameBits(r, lv.data[1][lv.stride * m]))
        {
            UNSCOPED_INFO("level " << n << " frame " << m << ": " << l << ", " << r << " vs "
                                   << lv.data[0][lv.stride * m] << ", "
                                   << lv.data[1][lv.stride * m]);
            return false;
        }
        ++compared;
    }
    return true;
}

// Every level-n frame m (n in 1 .. 3) of `a` whose ring frames m × 2ⁿ ± levelLag(n) lie in
// [from, to) equals b's, bit for bit; counts the frames compared. With `crossing`, also those
// whose reach crosses `from` (m × 2ⁿ >= from alone).
bool levelsAgree(const gg::LiveSource &a, const gg::LiveSource &b, int64_t from, int64_t to,
                 int64_t &compared, bool crossing = false)
{
    for (uint32_t n = 1; n < 4; ++n)
    {
        const int64_t lag = gg::RingStorage::levelLag(n);
        const int64_t d = int64_t{1} << n;
        const int64_t low = from + (crossing ? 0 : lag);
        for (int64_t m = std::max<int64_t>(0, (low + d - 1) / d); m * d + lag < to; ++m)
        {
            const auto [al, ar] = levelFrame(a, n, m);
            const auto [bl, br] = levelFrame(b, n, m);
            if (!sameBits(al, bl) || !sameBits(ar, br))
            {
                UNSCOPED_INFO("level " << n << " frame " << m);
                return false;
            }
            ++compared;
        }
    }
    return true;
}
} // namespace

TEST_CASE("a ring's level equals a file's for the same frames", "[engine][live]")
{
    // A 4 s ring written with 4 s of stereo noise in blocks of 1, 32 and 511 and in ragged
    // blocks, against PreparedSource::make of the same frames: every complete frame of levels 1
    // to 3 bit for bit. Then 2 s more, past the wrap: the levels' frames still in the window
    // against the file of all 6 s.
    constexpr int64_t kN = 4 * 48000;
    STATIC_REQUIRE(gg::RingStorage::kMaxLevels == 4);
    STATIC_REQUIRE(gg::RingStorage::levelLag(1) == 27);
    STATIC_REQUIRE(gg::RingStorage::levelLag(2) == 81);
    STATIC_REQUIRE(gg::RingStorage::levelLag(3) == 189);
    STATIC_REQUIRE(gg::RingStorage::levelLag(4) == 405); // past M = 256: no level 4
    const Stereo input = noiseInput(6 * 48000, 91);
    const auto file4 = fileOf(input, kN);
    const auto file6 = fileOf(input, 6 * 48000);
    REQUIRE(file4->levelCount() == 5);
    const std::vector<std::vector<int64_t>> splits = {{1}, {32}, {511}, {61, 1, 511, 7, 250, 32}};
    for (const std::vector<int64_t> &lengths : splits)
    {
        INFO("first block " << lengths[0] << " of " << lengths.size() << " lengths");
        gg::LiveSource ring;
        ring.prepare(kSampleRate, 4.0, 511);
        REQUIRE(ring.levelCount() == 4);
        for (uint32_t n = 1; n < 4; ++n)
        {
            const gg::LiveSource::Level lv = ring.level(n);
            CHECK(lv.decimation == (1u << n));
            CHECK(lv.view.capacity == (kN + 511 + (int64_t{1} << n) - 1) >> n);
            CHECK(lv.view.frames == kN >> n);
        }
        CHECK(ring.level(4).decimation == 1); // past the top: level 0
        CHECK(ring.level(4).view.slots == ring.view().slots);

        writeInput(ring, input, kN, lengths);
        int64_t compared = 0;
        for (uint32_t n = 1; n < 4; ++n)
        {
            const int64_t complete = gg::RingStorage::completeLevelFrames(n, ring.head());
            CHECK(complete == (kN - 1 - gg::RingStorage::levelLag(n)) / (1 << n) + 1);
            CHECK(levelMatchesFile(ring, *file4, n, 0, complete, compared));
        }
        CHECK(compared > 150000);

        writeInput(ring, input, 6 * 48000, lengths);
        compared = 0;
        const int64_t head = ring.head();
        for (uint32_t n = 1; n < 4; ++n)
        {
            const int64_t d = int64_t{1} << n;
            CHECK(levelMatchesFile(ring, *file6, n, (head - kN + d - 1) / d,
                                   gg::RingStorage::completeLevelFrames(n, head), compared));
        }
        CHECK(compared > 150000);
    }
}

TEST_CASE("every level is complete to head minus M", "[engine][live]")
{
    // After every block, each level's frames are complete (the file's, bit for bit) through
    // the one at ring frame head − M and beyond: the level's lag, at most 189 frames, is inside
    // M = 256, so any read the admission clamp allows finds its level frames written.
    const Stereo input = noiseInput(3 * 48000, 93);
    const auto file = fileOf(input, 3 * 48000);
    gg::LiveSource ring;
    ring.prepare(kSampleRate, 2.0, 512);
    int64_t compared = 0;
    bool complete = true;
    bool same = true;
    writeInput(ring, input, 3 * 48000, {512, 1, 77, 300, 2}, [&] {
        const int64_t head = ring.head();
        for (uint32_t n = 1; n < 4; ++n)
        {
            const int64_t d = int64_t{1} << n;
            const int64_t done = gg::RingStorage::completeLevelFrames(n, head);
            // The newest level frame at or before ring frame head − M, and the window's oldest.
            const int64_t atMargin = (head - 256) >= 0 ? (head - 256) / d : -1;
            complete = complete && done > atMargin && done * d <= head;
            // The newest complete frames hold the file's values (a file of more frames: the
            // ring's complete frames never read past the head, so they are the same).
            same = same && levelMatchesFile(ring, *file, n, done - 8, done, compared);
        }
    });
    CHECK(complete);
    CHECK(same);
    CHECK(compared > 10000);
}

TEST_CASE("levels freeze with level 0", "[engine][live]")
{
    // Frozen, the head stops after the ramp and so do the levels: no level frame changes while
    // the frozen ring takes more blocks. Released, they continue from where they stopped, as a
    // file of the frames written would have them.
    const Stereo input = noiseInput(2 * 48000, 95);
    gg::LiveSource ring;
    ring.prepare(kSampleRate, 1.0, 256);
    writeInput(ring, input, 30000, {256});
    ring.setFrozen(true);
    writeInput(ring, input, 40000, {256});
    const int64_t stopped = ring.head();
    REQUIRE(stopped == 30000 + gg::LiveSource::freezeRampSamples(kSampleRate));
    std::vector<std::vector<float>> snapshot;
    for (uint32_t n = 1; n < 4; ++n)
    {
        const gg::LiveSource::RingView v = ring.level(n).view;
        snapshot.emplace_back(v.slots, v.slots + 2 * (v.capacity + 15));
    }
    // More frozen blocks: the head stays, and so does every level slot.
    std::vector<float> l(256, 0.75f);
    const float *in[2] = {l.data(), l.data()};
    for (int64_t t = 40000; t < 50000; t += 256)
    {
        ring.write(in, 2, 0, t, 256);
    }
    REQUIRE(ring.head() == stopped);
    for (uint32_t n = 1; n < 4; ++n)
    {
        const gg::LiveSource::RingView v = ring.level(n).view;
        CHECK(std::memcmp(snapshot[n - 1].data(), v.slots, snapshot[n - 1].size() * 4) == 0);
    }
    // Released: the levels advance again, as the frames written (ramps included) give them.
    ring.setFrozen(false);
    std::vector<float> left;
    std::vector<float> right;
    gg::LiveSource copy; // the same frames, written whole, never frozen
    copy.prepare(kSampleRate, 1.0, 256);
    {
        // The frames the frozen ring holds, read back, then the new ones.
        const gg::LiveSource::RingView v = ring.view();
        for (int64_t f = 0; f < stopped; ++f)
        {
            const auto [a, b] = storedFrame(v, f);
            left.push_back(a);
            right.push_back(b);
        }
    }
    const int64_t before = ring.head();
    writeInput(ring, input, before + 9000, {256});
    {
        const gg::LiveSource::RingView v = ring.view();
        for (int64_t f = before; f < ring.head(); ++f)
        {
            const auto [a, b] = storedFrame(v, f);
            left.push_back(a);
            right.push_back(b);
        }
    }
    const Stereo frames{left, right};
    writeInput(copy, frames, static_cast<int64_t>(left.size()), {256});
    REQUIRE(copy.head() == ring.head());
    int64_t compared = 0;
    CHECK(levelsAgree(ring, copy, ring.head() - 48000, ring.head(), compared));
    CHECK(compared > 30000);
}

TEST_CASE("a restored ring's levels match the saved ring's", "[engine][live]")
{
    // A 2 s ring written 1.5 s (nothing has left it) is saved as frames [0, head) and restored
    // into a new storage whose levels the main thread fills (fillLevels, as media::restoreRing
    // does): every level frame then equals the saved ring's, bit for bit, and stays equal as both
    // take the same input after.
    const Stereo input = noiseInput(3 * 48000, 97);
    gg::LiveSource saved;
    saved.prepare(kSampleRate, 2.0, 512);
    writeInput(saved, input, 72000, {512, 3, 129});
    const int64_t head = saved.head();

    gg::LiveSource restored;
    restored.prepare(kSampleRate, 512);
    gg::RingStorage next(2 * 48000, 512);
    REQUIRE(next.levelCount() == 4);
    copyFrames(saved, next, 0, head);
    gg::fillLevels(next, 0, head);
    gg::RingStorage *previous = nullptr;
    REQUIRE(restored.restoreStorage(&next, head, {}, false, previous));
    REQUIRE(restored.levelCount() == 4);
    REQUIRE(restored.level(2).view.slots == next.levelSlots(2));
    int64_t compared = 0;
    CHECK(levelsAgree(restored, saved, 0, head, compared));
    CHECK(compared > 60000);
    // The guard slots of every level mirror their first 15 slots.
    for (uint32_t n = 1; n < 4; ++n)
    {
        const float *p = next.levelSlots(n);
        const int64_t c = next.levelCapacity(n);
        for (int64_t g = 0; g < 15; ++g)
        {
            CHECK(sameBits(p[2 * (c + g)], p[2 * g]));
            CHECK(sameBits(p[2 * (c + g) + 1], p[2 * g + 1]));
        }
    }

    writeInput(saved, input, 2 * 48000 + 40000, {512});
    writeInput(restored, input, 2 * 48000 + 40000, {511, 1});
    compared = 0;
    CHECK(levelsAgree(restored, saved, saved.head() - 2 * 48000, saved.head(), compared));
    CHECK(compared > 80000);
}

TEST_CASE("a resize keeps the levels", "[engine][live]")
{
    // A 2 s ring written past a wrap is resized to 4 s and then to 1 s: the main thread copies
    // the window as of R0 and its level frames (copyLevels, as MediaSession does), the audio
    // thread writes 4096 more frames and adoptStorage copies them and theirs. Every level frame
    // at a ring frame in the copied part of the window, those whose reach crosses its oldest
    // frame included, then equals a 4 s ring's that took the same input and was never resized,
    // bit for bit, and they stay equal as both go on. The copy is what fillLevels would make
    // wherever a level frame's reach is inside the copied frames.
    constexpr int64_t kN = 2 * 48000;
    const Stereo input = noiseInput(8 * 48000, 99);
    gg::LiveSource reference;
    reference.prepare(kSampleRate, 4.0, 512);
    gg::LiveSource ring;
    ring.prepare(kSampleRate, 2.0, 512);
    writeInput(ring, input, 3 * 48000, {512});
    const int64_t r0 = ring.head();

    gg::RingStorage bigger(4 * 48000, 512);
    copyFrames(ring, bigger, r0 - kN, r0);
    gg::copyLevels(*ring.storage(), bigger, r0 - kN, r0);
    // fillLevels makes the same frames from the copied ones wherever their reach is inside them.
    gg::RingStorage filled(4 * 48000, 512);
    copyFrames(ring, filled, r0 - kN, r0);
    gg::fillLevels(filled, r0 - kN, r0);
    bool same = true;
    for (uint32_t n = 1; n < 4; ++n)
    {
        const int64_t d = int64_t{1} << n;
        const int64_t lag = gg::RingStorage::levelLag(n);
        for (int64_t m = (r0 - kN + lag + d - 1) / d; m * d + lag < r0; ++m)
        {
            const int64_t s = m % bigger.levelCapacity(n);
            same = same && sameBits(bigger.levelSlots(n)[2 * s], filled.levelSlots(n)[2 * s]) &&
                   sameBits(bigger.levelSlots(n)[2 * s + 1], filled.levelSlots(n)[2 * s + 1]);
        }
    }
    CHECK(same);
    writeInput(ring, input, r0 + 4096, {512});
    gg::RingStorage *previous = nullptr;
    REQUIRE(ring.adoptStorage(&bigger, r0, previous));
    REQUIRE(ring.levelCount() == 4);
    REQUIRE(ring.level(3).view.slots == bigger.levelSlots(3));
    writeInput(reference, input, ring.head(), {512});
    int64_t compared = 0;
    CHECK(levelsAgree(ring, reference, r0 - kN, ring.head(), compared, true));
    CHECK(compared > 80000);

    writeInput(ring, input, 6 * 48000, {300});
    writeInput(reference, input, 6 * 48000, {512});
    compared = 0;
    CHECK(levelsAgree(ring, reference, r0 - kN, ring.head(), compared, true));
    CHECK(compared > 200000);

    // Smaller: the newest second.
    const int64_t r1 = ring.head();
    gg::RingStorage smaller(48000, 512);
    copyFrames(ring, smaller, r1 - 48000, r1);
    gg::copyLevels(*ring.storage(), smaller, r1 - 48000, r1);
    writeInput(ring, input, r1 + 1000, {512});
    REQUIRE(ring.adoptStorage(&smaller, r1, previous));
    REQUIRE(previous == &bigger);
    writeInput(reference, input, ring.head(), {512});
    compared = 0;
    // The window is the newest second as of the head now: older level frames have been reused.
    CHECK(levelsAgree(ring, reference, ring.head() - 48000, ring.head(), compared, true));
    CHECK(compared > 40000);
}
