// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The layer's Drive (color spec C2 to C4, §5.2): eight sst-waveshapers shapes at 2x, level-matched
// at -12 dBFS, with the entry blend, the DC blocker and BusFilter's shape-change rule.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../support/AllocTripwire.h"
#include "fx/Drive.h"
#include "gg/core/ParamIds.h"
#include "gg/metrics/AudioData.h"
#include "gg/metrics/Metrics.h"

namespace
{
using gg::core::DriveShape;
using gg::core::fx::Drive;
using gg::test::AllocTripwire;

constexpr double kRate = 48000.0;
constexpr float kA = 0.25f; ///< -12 dBFS, the level C3 matches at.
constexpr double kClickLimit = 15.0;

constexpr std::array<DriveShape, 8> kShapes{
    DriveShape::Soft, DriveShape::Medium,  DriveShape::Hard, DriveShape::Asymmetric,
    DriveShape::Ojd,  DriveShape::Rectify, DriveShape::Fold, DriveShape::SineFold};

// The alias margins of a 5 kHz sine at 0.25, Drive 1, 2x against the base rate (the power in the
// bins below 5 kHz that aren't the input's). Measured on the first run (dev, Linux x86_64):
//   Soft 9.62 dB, Hard 7.82 dB; each pinned 1 dB under.
constexpr double kAliasMarginSoftDb = 8.6;
constexpr double kAliasMarginHardDb = 6.8;

uint32_t rampOf(double fs)
{
    return static_cast<uint32_t>(std::lround(0.010 * fs)); // 10 ms
}

float sineAt(double hz, double fs, std::size_t n, double phase = 0.0)
{
    return kA * static_cast<float>(
                    std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(n) / fs + phase));
}

struct Stereo
{
    std::vector<float> l, r;
};

/// A sine at 0.25 (the right channel a quarter cycle on) through one Drive at a fixed d.
Stereo render(DriveShape shape, float d, double fs, double hz, double seconds,
              bool oversample = true)
{
    Drive drive;
    drive.prepare(fs, rampOf(fs), oversample);
    drive.jump(shape);
    const auto n = static_cast<std::size_t>(seconds * fs);
    Stereo out{std::vector<float>(n), std::vector<float>(n)};
    for (std::size_t i = 0; i < n; ++i)
    {
        float l = sineAt(hz, fs, i);
        float r = sineAt(hz, fs, i, std::numbers::pi / 2.0);
        if (i % 32 == 0)
        {
            drive.setCell(shape);
        }
        drive.process(l, r, d);
        out.l[i] = l;
        out.r[i] = r;
    }
    return out;
}

double peakOf(const std::vector<float> &x, std::size_t from)
{
    double p = 0.0;
    for (std::size_t i = from; i < x.size(); ++i)
    {
        p = std::max(p, std::fabs(static_cast<double>(x[i])));
    }
    return p;
}

double rmsOf(const std::vector<float> &x, std::size_t from)
{
    double s = 0.0;
    for (std::size_t i = from; i < x.size(); ++i)
    {
        s += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return std::sqrt(s / static_cast<double>(x.size() - from));
}

double rmsDiff(const std::vector<float> &a, const std::vector<float> &b, std::size_t from,
               std::size_t to)
{
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i)
    {
        const double e = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        s += e * e;
    }
    return std::sqrt(s / static_cast<double>(to - from));
}

double db(double ratio) { return 20.0 * std::log10(ratio); }

double clickScore(const Stereo &s, double fs)
{
    gg::metrics::AudioData audio{fs, {s.l, s.r}};
    return gg::metrics::computeMetrics(audio).clickScore;
}

/// An in-place radix-2 complex FFT, tests/metrics/comb_tests.cpp's.
void fft(std::vector<std::complex<double>> &a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
        {
            j ^= bit;
        }
        j ^= bit;
        if (i < j)
        {
            std::swap(a[i], a[j]);
        }
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> step{std::cos(angle), std::sin(angle)};
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w{1.0, 0.0};
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

/// The power in the bins from 20 Hz to 5 kHz, less 100 Hz around the 5 kHz input (its Hann
/// leakage), of 32,768 samples from `start`, Hann-windowed.
double aliasPower(const std::vector<float> &x, std::size_t start)
{
    constexpr std::size_t kN = 32768;
    std::vector<std::complex<double>> a(kN);
    for (std::size_t n = 0; n < kN; ++n)
    {
        const double hann =
            0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / kN);
        a[n] = static_cast<double>(x[start + n]) * hann;
    }
    fft(a);
    double p = 0.0;
    for (std::size_t k = 0; k < kN / 2; ++k)
    {
        const double hz = static_cast<double>(k) * kRate / kN;
        if (hz >= 20.0 && hz < 4900.0)
        {
            p += std::norm(a[k]);
        }
    }
    return p;
}
} // namespace

TEST_CASE("drive at zero leaves the sample untouched", "[core][color]")
{
    const float denormal = std::bit_cast<float>(uint32_t{0x00000123});
    std::vector<float> in{0.0f, -0.0f, denormal, -denormal, 1.0f, -1.0f, 0.5f, -0.25f, 1e-30f};
    for (std::size_t i = 0; i < 2000; ++i)
    {
        in.push_back(sineAt(220.0, kRate, i));
    }
    for (const DriveShape shape : kShapes)
    {
        Drive drive;
        drive.prepare(kRate, rampOf(kRate));
        drive.jump(shape);
        std::size_t mismatches = 0;
        for (std::size_t i = 0; i < in.size(); ++i)
        {
            float l = in[i];
            float r = in[in.size() - 1 - i];
            if (i % 32 == 0)
            {
                drive.setCell(shape);
            }
            drive.process(l, r, 0.0f);
            if (std::bit_cast<uint32_t>(l) != std::bit_cast<uint32_t>(in[i]) ||
                std::bit_cast<uint32_t>(r) != std::bit_cast<uint32_t>(in[in.size() - 1 - i]))
            {
                ++mismatches;
            }
        }
        INFO("shape " << static_cast<int>(shape));
        CHECK(mismatches == 0);
    }
}

TEST_CASE("drive comp table follows the law", "[core][color]")
{
    Drive drive;
    drive.prepare(kRate, rampOf(kRate));
    CHECK(drive.comp(DriveShape::Hard, 0.0f) == 1.0f);
    // Drive 1 is +36 dB: g·a = 15.8 >= 1, so Hard's probes reach 1 and comp is a/1.
    CHECK(drive.comp(DriveShape::Hard, 1.0f) == 0.25f);
    for (const DriveShape shape : kShapes)
    {
        for (int k = 0; k <= 128; ++k)
        {
            const float c = drive.comp(shape, static_cast<float>(k) / 128.0f);
            INFO("shape " << static_cast<int>(shape) << " entry " << k << " comp " << c);
            CHECK(c > 0.0f);
            CHECK(c <= 4.0f);
        }
    }
    // Between the entries it interpolates linearly.
    const float lo = drive.comp(DriveShape::Soft, 64.0f / 128.0f);
    const float hi = drive.comp(DriveShape::Soft, 65.0f / 128.0f);
    const float mid = drive.comp(DriveShape::Soft, 64.5f / 128.0f);
    CHECK(mid == lo + 0.5f * (hi - lo));
}

TEST_CASE("drive level matches at minus 12 dBFS", "[core][color]")
{
    const auto from = static_cast<std::size_t>(0.1 * kRate);
    const double inRms = static_cast<double>(kA) / std::numbers::sqrt2;
    for (const float d : {0.25f, 0.5f, 1.0f})
    {
        for (const DriveShape shape : kShapes)
        {
            const Stereo out = render(shape, d, kRate, 220.0, 1.0);
            const bool peakMatched = shape == DriveShape::Soft || shape == DriveShape::Medium ||
                                     shape == DriveShape::Hard || shape == DriveShape::Ojd;
            for (const auto *ch : {&out.l, &out.r})
            {
                const double peakDb = db(peakOf(*ch, from) / kA);
                const double rmsDb = db(rmsOf(*ch, from) / inRms);
                INFO("shape " << static_cast<int>(shape) << " d " << d << " peak " << peakDb
                              << " dB, rms " << rmsDb << " dB");
                if (peakMatched)
                {
                    CHECK(std::fabs(peakDb) <= 0.5);
                }
                CHECK(std::fabs(rmsDb) <= 9.0);
            }
        }
    }
}

TEST_CASE("drive level matches at 44.1 and 96 kHz", "[core][color]")
{
    for (const double fs : {44100.0, 96000.0})
    {
        const auto from = static_cast<std::size_t>(0.1 * fs);
        for (const DriveShape shape : {DriveShape::Soft, DriveShape::Hard})
        {
            const Stereo out = render(shape, 0.5f, fs, 220.0, 1.0);
            for (const auto *ch : {&out.l, &out.r})
            {
                const double peakDb = db(peakOf(*ch, from) / kA);
                INFO("fs " << fs << " shape " << static_cast<int>(shape) << " peak " << peakDb
                           << " dB");
                CHECK(std::fabs(peakDb) <= 0.5);
            }
        }
    }
}

TEST_CASE("rectify has no dc after one second", "[core][color]")
{
    const Stereo out = render(DriveShape::Rectify, 1.0f, kRate, 220.0, 2.0);
    for (const auto *ch : {&out.l, &out.r})
    {
        double sum = 0.0;
        for (std::size_t i = 48000; i < 96000; ++i)
        {
            sum += static_cast<double>((*ch)[i]);
        }
        const double mean = sum / 48000.0;
        INFO("mean " << mean);
        CHECK(std::fabs(mean) < 1e-3);
    }
}

TEST_CASE("drive oversampling lowers the alias", "[core][color]")
{
    const auto start = static_cast<std::size_t>(0.1 * kRate);
    for (const DriveShape shape : {DriveShape::Soft, DriveShape::Hard})
    {
        const Stereo over = render(shape, 1.0f, kRate, 5000.0, 1.0, true);
        const Stereo base = render(shape, 1.0f, kRate, 5000.0, 1.0, false);
        const double margin =
            10.0 * std::log10(aliasPower(base.l, start) / aliasPower(over.l, start));
        const double pinned = shape == DriveShape::Soft ? kAliasMarginSoftDb : kAliasMarginHardDb;
        INFO("shape " << static_cast<int>(shape) << " alias margin " << margin << " dB");
        CHECK(margin >= pinned);
    }
}

TEST_CASE("drive rising from zero does not click", "[core][color]")
{
    constexpr std::size_t kStart = 48000;
    constexpr std::size_t kN = 96000;
    for (const DriveShape shape : kShapes)
    {
        Drive drive;
        drive.prepare(kRate, rampOf(kRate));
        drive.jump(shape);
        Stereo out{std::vector<float>(kN), std::vector<float>(kN)};
        for (std::size_t i = 0; i < kN; ++i)
        {
            const float d =
                i < kStart ? 0.0f : std::min(1.0f, static_cast<float>(i - kStart + 1) / 480.0f);
            float l = sineAt(220.0, kRate, i);
            float r = sineAt(220.0, kRate, i, std::numbers::pi / 2.0);
            if (i % 32 == 0)
            {
                drive.setCell(shape);
            }
            drive.process(l, r, d);
            out.l[i] = l;
            out.r[i] = r;
        }
        const double score = clickScore(out, kRate);
        INFO("shape " << static_cast<int>(shape) << " click score " << score);
        CHECK(score < kClickLimit);
    }
}

TEST_CASE("drive shape changes crossfade without a click", "[core][color]")
{
    constexpr float kD = 0.6f;

    SECTION("the shape stepped 0 to 7 every 0.25 s")
    {
        constexpr std::size_t kN = 9 * 12000;
        Drive drive;
        drive.prepare(kRate, rampOf(kRate));
        drive.jump(DriveShape::Soft);
        Stereo out{std::vector<float>(kN), std::vector<float>(kN)};
        for (std::size_t i = 0; i < kN; ++i)
        {
            if (i % 32 == 0)
            {
                drive.setCell(kShapes[std::min<std::size_t>(7, i / 12000)]);
            }
            float l = sineAt(220.0, kRate, i);
            float r = sineAt(220.0, kRate, i, std::numbers::pi / 2.0);
            drive.process(l, r, kD);
            out.l[i] = l;
            out.r[i] = r;
        }
        const double score = clickScore(out, kRate);
        INFO("click score " << score);
        CHECK(score < kClickLimit);
    }

    // The shape in force is told from the output: after the fade, the instance under test tracks
    // the fresh instance jumped to that shape (the half-bands' and the DC blocker's transients
    // from the fade decayed), and not the other one. Soft, Hard and Fold hold no shaper state.
    constexpr std::size_t kN = 48000;
    constexpr std::size_t kN0 = 4800; // the fade starts (a cell boundary)
    constexpr std::size_t kSettle = 4800;
    auto run = [&](Drive &x, auto &&cell, Drive &a, Drive &b, Stereo &ox, Stereo &oa, Stereo &ob) {
        for (std::size_t i = 0; i < kN; ++i)
        {
            const float in = sineAt(220.0, kRate, i);
            float l = in, r = in;
            cell(i);
            x.process(l, r, kD);
            ox.l[i] = l;
            ox.r[i] = r;
            l = r = in;
            a.process(l, r, kD);
            oa.l[i] = l;
            oa.r[i] = r;
            l = r = in;
            b.process(l, r, kD);
            ob.l[i] = l;
            ob.r[i] = r;
        }
    };

    SECTION("a change back during the fade turns it")
    {
        Drive x, soft, hard;
        x.prepare(kRate, rampOf(kRate));
        soft.prepare(kRate, rampOf(kRate));
        hard.prepare(kRate, rampOf(kRate));
        x.jump(DriveShape::Soft);
        soft.jump(DriveShape::Soft);
        hard.jump(DriveShape::Hard);
        Stereo ox{std::vector<float>(kN), std::vector<float>(kN)}, os = ox, oh = ox;
        run(
            x,
            [&](std::size_t i) {
                if (i == kN0)
                {
                    x.setCell(DriveShape::Hard);
                }
                else if (i == kN0 + 224)
                {
                    x.setCell(DriveShape::Soft); // mid-fade: back to the outgoing shape
                }
                else if (i % 32 == 0)
                {
                    x.setCell(DriveShape::Soft);
                }
            },
            soft, hard, ox, os, oh);
        // The turned fade ends back on Soft.
        const std::size_t w0 = kN0 + 480 + kSettle;
        const double toSoft = rmsDiff(ox.l, os.l, w0, w0 + kSettle);
        const double toHard = rmsDiff(ox.l, oh.l, w0, w0 + kSettle);
        INFO("rms to soft " << toSoft << ", to hard " << toHard);
        CHECK(toSoft < 0.01 * toHard);
        CHECK(clickScore(ox, kRate) < kClickLimit);
    }

    SECTION("a third shape during the fade waits")
    {
        Drive x, hard, fold;
        x.prepare(kRate, rampOf(kRate));
        hard.prepare(kRate, rampOf(kRate));
        fold.prepare(kRate, rampOf(kRate));
        x.jump(DriveShape::Soft);
        hard.jump(DriveShape::Hard);
        fold.jump(DriveShape::Fold);
        constexpr std::size_t kLater = kN0 + 480 + 2 * kSettle; // a cell boundary
        Stereo ox{std::vector<float>(kN), std::vector<float>(kN)}, oh = ox, of = ox;
        run(
            x,
            [&](std::size_t i) {
                if (i == kN0)
                {
                    x.setCell(DriveShape::Hard);
                }
                else if (i == kN0 + 256)
                {
                    x.setCell(DriveShape::Fold); // during the fade: ignored
                }
                else if (i == kLater)
                {
                    x.setCell(DriveShape::Fold); // asked again after it: taken
                }
            },
            hard, fold, ox, oh, of);
        const std::size_t w0 = kN0 + 480 + kSettle;
        const double toHard = rmsDiff(ox.l, oh.l, w0, w0 + kSettle);
        const double toFold = rmsDiff(ox.l, of.l, w0, w0 + kSettle);
        INFO("after the first fade: rms to hard " << toHard << ", to fold " << toFold);
        CHECK(toHard < 0.01 * toFold);
        const std::size_t w1 = kLater + 480 + kSettle;
        const double laterHard = rmsDiff(ox.l, oh.l, w1, w1 + kSettle);
        const double laterFold = rmsDiff(ox.l, of.l, w1, w1 + kSettle);
        INFO("after the second fade: rms to hard " << laterHard << ", to fold " << laterFold);
        CHECK(laterFold < 0.01 * laterHard);
        CHECK(clickScore(ox, kRate) < kClickLimit);
    }
}

TEST_CASE("a freshly prepared drive does not allocate", "[core][color][rt]")
{
    Drive drive;
    drive.prepare(kRate, rampOf(kRate));
    drive.jump(DriveShape::Soft);
    constexpr std::size_t kN = 8 * 1200;
    std::vector<float> out(kN);
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (std::size_t i = 0; i < kN; ++i)
        {
            if (i % 32 == 0)
            {
                // A new shape every 1,200 samples: each change fades over 480.
                drive.setCell(kShapes[(i / 1200) % 8]);
            }
            float l = sineAt(220.0, kRate, i);
            float r = -l;
            drive.process(l, r, static_cast<float>(i % 4800) / 4800.0f);
            out[i] = l + r;
        }
        drive.reset();
    }
    const uint64_t allocations = AllocTripwire::count();
    if (AllocTripwire::available())
    {
        REQUIRE(allocations == 0);
    }
    REQUIRE(std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v); }));
}
