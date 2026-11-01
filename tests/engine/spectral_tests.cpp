// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The Spectral Smear source (extremes spec §6.2): an STFT of N ≈ 85 ms at hop N/4 over the
// layer's material, every bin's phase drawn from the counter RNG, overlap-added into a 5 s ring
// of its own. Its stream is a function of the sample clock, so it is the same bytes at any block
// split.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Rng.h"
#include "gg/source/LiveSource.h"
#include "gg/spectral/SpectralSource.h"
#include "support/AllocTripwire.h"

using gg::SpectralMaterial;
using gg::SpectralSource;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr uint32_t kMaxBlock = 4096;

// Counter-keyed noise in [-0.5, 0.5): channel c of frame i.
float noise(uint64_t seed, uint32_t c, int64_t i)
{
    const gg::rng::Key key{seed, 0, c, gg::rng::Field::SignalNoise, static_cast<uint64_t>(i)};
    return static_cast<float>(0.5 * gg::rng::bipolar(gg::rng::hash(key)));
}

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

Stereo noiseSample(int64_t frames, uint64_t seed = 3)
{
    Stereo s{std::vector<float>(static_cast<std::size_t>(frames)),
             std::vector<float>(static_cast<std::size_t>(frames))};
    for (int64_t i = 0; i < frames; ++i)
    {
        s.left[static_cast<std::size_t>(i)] = noise(seed, 0, i);
        s.right[static_cast<std::size_t>(i)] = noise(seed, 1, i);
    }
    return s;
}

SpectralMaterial sampleMaterial(const Stereo &s)
{
    SpectralMaterial m;
    m.sample[0] = s.left.data();
    m.sample[1] = s.right.data();
    m.sampleFrames = static_cast<int64_t>(s.left.size());
    m.sampleMono = false;
    m.sampleRate = kSampleRate;
    return m;
}

// The ring's frames [0, frames), interleaved L R, read through its view.
std::vector<float> ringFrames(const SpectralSource &source, int64_t frames)
{
    const auto view = source.ring().view();
    std::vector<float> out(2 * static_cast<std::size_t>(frames));
    for (int64_t f = 0; f < frames; ++f)
    {
        const int64_t slot = f % view.capacity;
        out[2 * static_cast<std::size_t>(f)] = view.slots[2 * slot];
        out[2 * static_cast<std::size_t>(f) + 1] = view.slots[2 * slot + 1];
    }
    return out;
}

// Runs source over [0, total) in blocks of the sizes given (cycled), with sample material.
void renderSplit(SpectralSource &source, const SpectralMaterial &m, double position, uint64_t seed,
                 uint32_t layer, int64_t total, const std::vector<uint32_t> &sizes)
{
    int64_t t = 0;
    std::size_t i = 0;
    while (t < total)
    {
        const auto n =
            static_cast<uint32_t>(std::min<int64_t>(sizes[i++ % sizes.size()], total - t));
        source.process(m, position, seed, layer, t, n);
        t += n;
    }
}

// Block sizes in [0, kMaxBlock], counter-keyed, a 0 now and then.
std::vector<uint32_t> randomSizes(uint64_t seed)
{
    std::vector<uint32_t> sizes;
    for (uint64_t i = 0; i < 64; ++i)
    {
        const gg::rng::Key key{seed, 0, 0, gg::rng::Field::SignalNoise, i};
        const double u = gg::rng::uniform01(gg::rng::hash(key));
        sizes.push_back(i % 9 == 4 ? 0u : static_cast<uint32_t>(u * kMaxBlock));
    }
    return sizes;
}

bool allFinite(const std::vector<float> &v)
{
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

float peak(const std::vector<float> &v, std::size_t from = 0)
{
    float p = 0.0f;
    for (std::size_t i = from; i < v.size(); ++i)
    {
        p = std::max(p, std::abs(v[i]));
    }
    return p;
}
} // namespace

TEST_CASE("the fft size follows the rate", "[engine][spectral]")
{
    REQUIRE(SpectralSource::fftSizeFor(22050.0) == 2048);
    REQUIRE(SpectralSource::fftSizeFor(44100.0) == 4096);
    REQUIRE(SpectralSource::fftSizeFor(48000.0) == 4096);
    REQUIRE(SpectralSource::fftSizeFor(96000.0) == 8192);
    REQUIRE(SpectralSource::fftSizeFor(192000.0) == 16384);
    for (const double rate : {22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
    {
        INFO("rate " << rate);
        const SpectralSource source(rate, 64);
        REQUIRE(source.fftSize() == SpectralSource::fftSizeFor(rate));
        REQUIRE(source.hop() == source.fftSize() / 4);
        REQUIRE(source.hop() % 32 == 0);
        REQUIRE(source.ring().frames() ==
                static_cast<int64_t>(std::ceil(SpectralSource::kRingSeconds * rate)));
    }
}

TEST_CASE("the smear is the same bytes at any block split", "[engine][spectral]")
{
    const Stereo s = noiseSample(96000);
    const SpectralMaterial m = sampleMaterial(s);
    constexpr int64_t kTotal = 24000; // 0.5 s: 23 hops
    std::vector<std::vector<uint32_t>> splits{{1},    {32},           {64},          {511},
                                              {4096}, randomSizes(7), randomSizes(8)};

    SECTION("sample material")
    {
        SpectralSource reference(kSampleRate, kMaxBlock);
        reference.setActive(true);
        renderSplit(reference, m, 0.5, 21, 1, kTotal, {4096});
        const std::vector<float> expected = ringFrames(reference, kTotal);
        REQUIRE(peak(expected) > 0.01f);
        for (const auto &sizes : splits)
        {
            INFO("first block " << sizes.front());
            SpectralSource source(kSampleRate, kMaxBlock);
            source.setActive(true);
            renderSplit(source, m, 0.5, 21, 1, kTotal, sizes);
            const std::vector<float> got = ringFrames(source, kTotal);
            REQUIRE(std::memcmp(got.data(), expected.data(), got.size() * sizeof(float)) == 0);
        }
    }

    SECTION("ring material, the head read at each hop")
    {
        // The material ring is written block by block before the smear reads it, as the engine
        // does, so its head at a hop is the same at any split.
        auto run = [&](const std::vector<uint32_t> &sizes) {
            gg::LiveSource live;
            live.prepare(kSampleRate, 1.0, kMaxBlock);
            SpectralSource source(kSampleRate, kMaxBlock);
            source.setActive(true);
            SpectralMaterial ringMaterial;
            ringMaterial.ring = &live;
            int64_t t = 0;
            std::size_t i = 0;
            while (t < kTotal)
            {
                const auto n =
                    static_cast<uint32_t>(std::min<int64_t>(sizes[i++ % sizes.size()], kTotal - t));
                const float *in[2] = {s.left.data(), s.right.data()};
                live.write(in, 2, static_cast<uint32_t>(t), t, n);
                source.process(ringMaterial, 0.02, 21, 1, t, n);
                t += n;
            }
            return ringFrames(source, kTotal);
        };
        const std::vector<float> expected = run({4096});
        REQUIRE(peak(expected) > 0.01f);
        for (const auto &sizes : splits)
        {
            INFO("first block " << sizes.front());
            const std::vector<float> got = run(sizes);
            REQUIRE(std::memcmp(got.data(), expected.data(), got.size() * sizeof(float)) == 0);
        }
    }
}

TEST_CASE("the smear is keyed by the seed and the layer", "[engine][spectral]")
{
    const Stereo s = noiseSample(48000);
    const SpectralMaterial m = sampleMaterial(s);
    constexpr int64_t kTotal = 16384;
    auto run = [&](uint64_t seed, uint32_t layer) {
        SpectralSource source(kSampleRate, kMaxBlock);
        source.setActive(true);
        renderSplit(source, m, 0.5, seed, layer, kTotal, {512});
        return ringFrames(source, kTotal);
    };
    const std::vector<float> a = run(5, 0);
    REQUIRE(peak(a) > 0.01f);
    REQUIRE(run(5, 0) == a);
    REQUIRE(run(6, 0) != a);
    REQUIRE(run(5, 1) != a);

    // The two channels draw their phases from streams of their own.
    std::vector<float> left, right;
    for (std::size_t f = 0; f < a.size() / 2; ++f)
    {
        left.push_back(a[2 * f]);
        right.push_back(a[2 * f + 1]);
    }
    REQUIRE(left != right);
}

TEST_CASE("a steady material keeps its rms within 1 dB", "[engine][spectral]")
{
    const Stereo s = noiseSample(96000);
    double materialSum = 0.0;
    for (std::size_t i = 0; i < s.left.size(); ++i)
    {
        materialSum += static_cast<double>(s.left[i]) * s.left[i];
        materialSum += static_cast<double>(s.right[i]) * s.right[i];
    }
    const double materialRms = std::sqrt(materialSum / (2.0 * static_cast<double>(s.left.size())));

    SpectralSource source(kSampleRate, kMaxBlock);
    source.setActive(true);
    renderSplit(source, sampleMaterial(s), 0.5, 9, 0, 96000, {1024});
    const std::vector<float> out = ringFrames(source, 96000);
    double sum = 0.0;
    for (std::size_t i = 2 * 48000; i < out.size(); ++i)
    {
        sum += static_cast<double>(out[i]) * out[i];
    }
    const double rms = std::sqrt(sum / static_cast<double>(out.size() - 2 * 48000));
    const double db = 20.0 * std::log10(rms / materialRms);
    INFO("material RMS " << materialRms << ", smear RMS " << rms << ", " << db << " dB");
    REQUIRE(std::abs(db) <= 1.0);
}

TEST_CASE("inactive writes silence, active starts under the margin", "[engine][spectral]")
{
    const Stereo s = noiseSample(48000);
    const SpectralMaterial m = sampleMaterial(s);
    SpectralSource source(kSampleRate, kMaxBlock);
    const int64_t n = source.fftSize();
    const int64_t h = source.hop();
    REQUIRE_FALSE(source.active());

    constexpr int64_t kT0 = 10000; // not a hop boundary
    renderSplit(source, m, 0.5, 4, 0, kT0, {1000});
    std::vector<float> out = ringFrames(source, kT0);
    REQUIRE(peak(out) == 0.0f);

    source.setActive(true);
    const int64_t activeEnd = kT0 + n + 3 * h;
    for (int64_t t = kT0; t < activeEnd;)
    {
        const auto len = static_cast<uint32_t>(std::min<int64_t>(700, activeEnd - t));
        source.process(m, 0.5, 4, 0, t, len);
        t += len;
    }
    out = ringFrames(source, activeEnd);
    // The first frame is at the next hop boundary; its first sample comes out N later.
    const auto firstHop = (kT0 + h - 1) / h * h;
    for (int64_t t = 0; t < firstHop + n; ++t)
    {
        INFO("frame " << t);
        REQUIRE(out[2 * static_cast<std::size_t>(t)] == 0.0f);
        REQUIRE(out[2 * static_cast<std::size_t>(t) + 1] == 0.0f);
    }
    REQUIRE(firstHop + n >= kT0 + n);
    REQUIRE(peak(out, 2 * static_cast<std::size_t>(firstHop + n)) > 0.01f);

    // Switching away drains the frames already computed: the last, at the last hop boundary
    // before the switch, comes out until 2N after its boundary, and from there on the ring takes
    // silence.
    source.setActive(false);
    const int64_t lastHop = (activeEnd - 1) / h * h;
    const int64_t drainEnd = lastHop + 2 * n;
    const int64_t silentEnd = drainEnd + 2000;
    for (int64_t t = activeEnd; t < silentEnd;)
    {
        const auto len = static_cast<uint32_t>(std::min<int64_t>(700, silentEnd - t));
        source.process(m, 0.5, 4, 0, t, len);
        t += len;
    }
    out = ringFrames(source, silentEnd);
    const std::vector<float> tail(out.begin() + 2 * activeEnd, out.begin() + 2 * drainEnd);
    REQUIRE(peak(tail) > 0.01f);
    // The last hop's frames still sound in the drain's last hop.
    REQUIRE(peak(out, 2 * static_cast<std::size_t>(drainEnd - h)) > 0.0f);
    REQUIRE(peak(out, 2 * static_cast<std::size_t>(drainEnd)) == 0.0f);
}

namespace
{
// Runs source over [0, total) in blocks of the sizes given (cycled), split at the toggles, where
// it switches active on (toggles[0]), off, on again, and so on.
void renderToggled(SpectralSource &source, const SpectralMaterial &m, int64_t total,
                   const std::vector<uint32_t> &sizes, const std::vector<int64_t> &toggles)
{
    int64_t t = 0;
    std::size_t i = 0;
    std::size_t next = 0;
    while (t < total)
    {
        while (next < toggles.size() && toggles[next] <= t)
        {
            source.setActive(next % 2 == 0);
            ++next;
        }
        int64_t end = next < toggles.size() ? std::min(total, toggles[next]) : total;
        end = std::min<int64_t>(end, t + sizes[i++ % sizes.size()]);
        source.process(m, 0.5, 6, 2, t, static_cast<uint32_t>(end - t));
        t = end;
    }
}
} // namespace

TEST_CASE("the drain is the same bytes at any block split", "[engine][spectral]")
{
    // Switched off, the pending frames drain out: a function of the switch frames alone. On at
    // 0, off at kOff (inside a hop), and in the second case on again halfway through the drain,
    // adding new frames onto it, and off once more. Every split gives the same ring.
    const Stereo s = noiseSample(96000);
    const SpectralMaterial m = sampleMaterial(s);
    SpectralSource probe(kSampleRate, kMaxBlock);
    const int64_t n = probe.fftSize();
    const int64_t h = probe.hop();
    constexpr int64_t kOff = 30000;
    const int64_t drainEnd = (kOff - 1) / h * h + 2 * n;
    for (const bool back : {false, true})
    {
        INFO((back ? "on again during the drain" : "off"));
        std::vector<int64_t> toggles{0, kOff};
        if (back)
        {
            toggles.push_back(kOff + n / 2);
            toggles.push_back(kOff + 3 * n);
        }
        const int64_t total = toggles.back() + 3 * n;
        SpectralSource reference(kSampleRate, kMaxBlock);
        renderToggled(reference, m, total, {kMaxBlock}, toggles);
        const std::vector<float> ref = ringFrames(reference, total);
        REQUIRE(allFinite(ref));
        REQUIRE(peak(std::vector<float>(ref.begin() + 2 * kOff, ref.begin() + 2 * drainEnd)) >
                0.01f);
        if (!back)
        {
            REQUIRE(peak(ref, 2 * static_cast<std::size_t>(drainEnd)) == 0.0f);
        }
        else
        {
            // The new frames sound again, and the last switch off drains too.
            REQUIRE(peak(std::vector<float>(ref.begin() + 2 * drainEnd,
                                            ref.begin() + 2 * (toggles[3] - 1))) > 0.01f);
            const int64_t lastEnd = (toggles[3] - 1) / h * h + 2 * n;
            REQUIRE(peak(std::vector<float>(ref.begin() + 2 * toggles[3],
                                            ref.begin() + 2 * lastEnd)) > 0.01f);
            REQUIRE(peak(ref, 2 * static_cast<std::size_t>(lastEnd)) == 0.0f);
        }
        for (const std::vector<uint32_t> &sizes :
             {std::vector<uint32_t>{1}, {7}, {1024}, {333}, randomSizes(19)})
        {
            INFO("first block " << sizes.front() << " of " << sizes.size());
            SpectralSource split(kSampleRate, kMaxBlock);
            renderToggled(split, m, total, sizes, toggles);
            const std::vector<float> got = ringFrames(split, total);
            REQUIRE(std::memcmp(got.data(), ref.data(), ref.size() * sizeof(float)) == 0);
        }
    }
}

TEST_CASE("reset clears the drain at once", "[engine][spectral]")
{
    const Stereo s = noiseSample(48000);
    const SpectralMaterial m = sampleMaterial(s);
    SpectralSource source(kSampleRate, kMaxBlock);
    source.setActive(true);
    renderSplit(source, m, 0.5, 4, 0, 20000, {512});
    source.setActive(false);
    source.reset();
    renderSplit(source, m, 0.5, 4, 0, 20000, {512});
    REQUIRE(peak(ringFrames(source, 20000)) == 0.0f);
}

TEST_CASE("the window clamps at the sample's ends", "[engine][spectral]")
{
    // A sample shorter than the window, and a long one, each exactly as long as its vectors, so
    // that a read past either end is a heap overflow under ASan.
    for (const int64_t frames : {int64_t{1000}, int64_t{48000}})
    {
        const Stereo s = noiseSample(frames);
        SpectralMaterial m = sampleMaterial(s);
        for (const double position : {0.0, 1.0, -1.0, 2.0, std::numeric_limits<double>::quiet_NaN(),
                                      std::numeric_limits<double>::infinity()})
        {
            INFO("frames " << frames << ", position " << position);
            SpectralSource source(kSampleRate, kMaxBlock);
            source.setActive(true);
            renderSplit(source, m, position, 2, 0, 20000, {4096});
            const std::vector<float> out = ringFrames(source, 20000);
            REQUIRE(allFinite(out));
            REQUIRE(peak(out) > 0.001f); // the window's half inside the sample is read
        }
    }

    // A mono sample: channel 1 copies channel 0.
    const Stereo s = noiseSample(48000);
    SpectralMaterial mono = sampleMaterial(s);
    mono.sampleMono = true;
    SpectralSource source(kSampleRate, kMaxBlock);
    source.setActive(true);
    renderSplit(source, mono, 0.5, 2, 0, 20000, {4096});
    const std::vector<float> out = ringFrames(source, 20000);
    REQUIRE(peak(out) > 0.01f);
    for (std::size_t f = 0; f < out.size() / 2; ++f)
    {
        REQUIRE(out[2 * f] == out[2 * f + 1]);
    }
}

TEST_CASE("ring material reads only valid frames", "[engine][spectral]")
{
    // Stale frames from before a reset() hold 1000s; no frame of the smear's window may read
    // them. After the reset only 100 frames of 0.25 are written, then silence.
    gg::LiveSource live;
    live.prepare(kSampleRate, 0.25, kMaxBlock);
    std::vector<float> junk(kMaxBlock, 1000.0f);
    const float *junkIn[2] = {junk.data(), junk.data()};
    for (int64_t t = 0; t < 4 * kMaxBlock; t += kMaxBlock)
    {
        live.write(junkIn, 2, 0, t, kMaxBlock);
    }
    live.reset();

    std::vector<float> block(kMaxBlock, 0.0f);
    std::fill(block.begin(), block.begin() + 100, 0.25f);
    std::vector<float> silence(kMaxBlock, 0.0f);

    SpectralSource source(kSampleRate, kMaxBlock);
    source.setActive(true);
    SpectralMaterial m;
    m.ring = &live;
    constexpr int64_t kTotal = 32768;
    for (const double position : {0.0, 10.0})
    {
        INFO("position " << position);
        live.reset();
        source.reset();
        for (int64_t t = 0; t < kTotal; t += 512)
        {
            const float *in[2] = {t == 0 ? block.data() : silence.data(),
                                  t == 0 ? block.data() : silence.data()};
            live.write(in, 2, 0, t, 512);
            source.process(m, position, 3, 0, t, 512);
        }
        const std::vector<float> out = ringFrames(source, kTotal);
        REQUIRE(allFinite(out));
        // The 100 frames of 0.25 are read (position 0 reaches them; 10 s behind the head is
        // clamped to the oldest frames, which also reach them while they are in the ring), and
        // nothing near the junk's level is.
        REQUIRE(peak(out) > 1e-4f);
        REQUIRE(peak(out) < 1.0f);
    }

    // No ring at all, or a ring without frames, is silence.
    gg::LiveSource none;
    none.prepare(kSampleRate, 0.0, kMaxBlock);
    const gg::LiveSource *const rings[] = {nullptr, &none};
    for (const gg::LiveSource *ring : rings)
    {
        SpectralSource silent(kSampleRate, kMaxBlock);
        silent.setActive(true);
        SpectralMaterial empty;
        empty.ring = ring;
        renderSplit(silent, empty, 0.0, 3, 0, 12000, {4096});
        REQUIRE(peak(ringFrames(silent, 12000)) == 0.0f);
    }
}

TEST_CASE("spectral source process does not allocate", "[engine][rt]")
{
    using gg::test::AllocTripwire;
    const Stereo s = noiseSample(48000);
    const SpectralMaterial sample = sampleMaterial(s);
    gg::LiveSource live;
    live.prepare(kSampleRate, 1.0, kMaxBlock);
    SpectralMaterial ringMaterial;
    ringMaterial.ring = &live;

    SpectralSource a(kSampleRate, kMaxBlock);
    SpectralSource b(kSampleRate, kMaxBlock);
    a.setActive(true);
    b.setActive(true);
    std::vector<float> in(kMaxBlock, 0.1f);
    const float *inputs[2] = {in.data(), in.data()};

    constexpr uint32_t kBlock = 300; // not a divisor of the hop, so hops fall mid-block
    int64_t t = 0;
    auto step = [&] {
        live.write(inputs, 2, 0, t, kBlock);
        a.process(sample, 0.5, 1, 0, t, kBlock);
        b.process(ringMaterial, 0.1, 1, 1, t, kBlock);
        t += kBlock;
    };
    for (int i = 0; i < 20; ++i) // warm up past the first frames
    {
        step();
    }
    const int64_t armedStart = t;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int i = 0; i < 40; ++i) // 12,000 frames: 11 hop boundaries
        {
            step();
        }
        a.setActive(false);
        a.process(sample, 0.5, 1, 0, t, kBlock);
        a.setActive(true);
        a.process(sample, 0.5, 1, 0, t + kBlock, 0);
        a.process(sample, 0.5, 1, 0, t + kBlock, 1);
    }
    const uint64_t allocations = AllocTripwire::count();
    REQUIRE(t - armedStart >= 4 * static_cast<int64_t>(a.hop()));
    REQUIRE(peak(ringFrames(a, t)) > 0.0f);
    if (AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}
