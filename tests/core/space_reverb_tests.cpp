// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Space's reverb (spec §6.3): sst-effects' Reverb 2 under gg-core's own FXConfig
// (core/src/space/GgFxConfig.h). Tagged [space].

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Rng.h"
#include "space/GgFxConfig.h"
#include "space/Reverb2Patched.h"

using gg::core::space::GgFxConfig;
using gg::core::space::Rev2;
using gg::core::space::Rev2Patched;

namespace
{
constexpr int kChunk = GgFxConfig::blockSize;

/// Reverb 2's values for these tests: Surge's defaults, with the shortest pre-delay (2^-8 s, so
/// the tail starts within the first few chunks), Decay 1.0 (2 s) and Mix 100%.
void setRingingValues(GgFxConfig::Storage &storage)
{
    storage.values.fill(0.f);
    storage.values[Rev2::rev2_predelay] = -8.f;
    storage.values[Rev2::rev2_room_size] = 0.f;
    storage.values[Rev2::rev2_decay_time] = 1.f;
    storage.values[Rev2::rev2_diffusion] = 1.f;
    storage.values[Rev2::rev2_buildup] = 1.f;
    storage.values[Rev2::rev2_modulation] = 0.5f;
    storage.values[Rev2::rev2_lf_damping] = 0.2f;
    storage.values[Rev2::rev2_hf_damping] = 0.2f;
    storage.values[Rev2::rev2_width] = 0.f; // 0 dB: full stereo
    storage.values[Rev2::rev2_mix] = 1.f;
}
} // namespace

TEST_CASE("Reverb2 builds and rings with GgFxConfig", "[space]")
{
    GgFxConfig::Storage storage;
    storage.sampleRate = 48000.0;
    setRingingValues(storage);

    // About 14 MB of delay lines: never on the stack.
    auto reverb = std::make_unique<Rev2>(&storage, &storage, &storage);
    reverb->initialize();

    // One chunk holding a unit impulse at sample 0, then silence to 0.5 s. (200 silent chunks
    // end at 0.134 s, before the 0.2 s the tail check needs, so the run goes on to 0.5 s.)
    constexpr int kChunks = 750;
    std::vector<double> chunkEnergy(kChunks, 0.0);
    bool allFinite = true;
    for (int c = 0; c < kChunks; ++c)
    {
        alignas(16) std::array<float, kChunk> left{};
        alignas(16) std::array<float, kChunk> right{};
        if (c == 0)
        {
            left[0] = 1.f;
            right[0] = 1.f;
        }
        reverb->processBlock(left.data(), right.data());
        for (int i = 0; i < kChunk; ++i)
        {
            allFinite = allFinite && std::isfinite(left[i]) && std::isfinite(right[i]);
            chunkEnergy[c] += double(left[i]) * left[i] + double(right[i]) * right[i];
        }
    }
    REQUIRE(allFinite);

    double earlyEnergy = 0.0;
    for (int c = 2; c <= 200; ++c)
    {
        earlyEnergy += chunkEnergy[c];
    }
    REQUIRE(earlyEnergy > 0.0);

    // A tail: some chunk wholly after 0.2 s is non-zero.
    const int firstLateChunk = int(std::ceil(0.2 * storage.sampleRate / kChunk));
    bool tail = false;
    for (int c = firstLateChunk; c < kChunks; ++c)
    {
        tail = tail || chunkEnergy[c] > 0.0;
    }
    REQUIRE(tail);

    // Spec §6.3 budgets Reverb 2's fixed arrays; the report records the measured size.
    CHECK(sizeof(Rev2) > 8u * 1024 * 1024);
}

// ---------------------------------------------------------------------------------------------
// The patched Reverb 2 (core/src/space/Reverb2Patched.h): infinite decay (S7), the smoothed
// pre-delay (S8) and clear().

namespace
{
constexpr double kRate = 48000.0;
constexpr uint64_t kNoiseSeed = 0x7370616365726576ull; // "spacerev"

/// Seeded noise in [-amplitude, amplitude), from the engine's counter RNG.
std::vector<float> noise(size_t frames, uint32_t stream, float amplitude)
{
    std::vector<float> v(frames);
    for (size_t i = 0; i < frames; ++i)
    {
        v[i] = static_cast<float>(
            amplitude * gg::rng::bipolar(gg::rng::hash(
                            {kNoiseSeed, 0, stream, gg::rng::Field::SignalNoise, uint64_t(i)})));
    }
    return v;
}

size_t frames(double seconds) { return size_t(std::llround(seconds * kRate)); }

/// A stereo signal, the length of a whole number of chunks.
struct Stereo
{
    std::vector<float> left, right;
    explicit Stereo(size_t n) : left(n, 0.f), right(n, 0.f) {}
};

/// Renders `in` through `reverb` chunk by chunk. Before each chunk, `beforeChunk(c)` may change
/// the values in `storage` or call the patch's setters. Returns the output.
template <typename Reverb, typename BeforeChunk>
Stereo render(Reverb &reverb, const Stereo &in, BeforeChunk &&beforeChunk)
{
    REQUIRE(in.left.size() % kChunk == 0);
    Stereo out(in.left.size());
    for (size_t c = 0; c < in.left.size() / kChunk; ++c)
    {
        beforeChunk(c);
        alignas(16) std::array<float, kChunk> left{};
        alignas(16) std::array<float, kChunk> right{};
        std::copy_n(in.left.begin() + c * kChunk, kChunk, left.begin());
        std::copy_n(in.right.begin() + c * kChunk, kChunk, right.begin());
        reverb.processBlock(left.data(), right.data());
        std::copy_n(left.begin(), kChunk, out.left.begin() + c * kChunk);
        std::copy_n(right.begin(), kChunk, out.right.begin() + c * kChunk);
    }
    return out;
}

/// The chunk that holds the sample at `seconds`.
size_t chunkAt(double seconds) { return frames(seconds) / kChunk; }

/// Stereo noise (independent channels) for `noiseSeconds`, then silence to `totalSeconds`,
/// rounded down to whole chunks.
Stereo noiseThenSilence(double noiseSeconds, double totalSeconds, float amplitude)
{
    Stereo s(chunkAt(totalSeconds) * kChunk);
    const size_t n = std::min(frames(noiseSeconds), s.left.size());
    const auto l = noise(n, 1, amplitude);
    const auto r = noise(n, 2, amplitude);
    std::copy(l.begin(), l.end(), s.left.begin());
    std::copy(r.begin(), r.end(), s.right.begin());
    return s;
}

bool sameBits(const Stereo &a, const Stereo &b)
{
    return a.left.size() == b.left.size() &&
           std::memcmp(a.left.data(), b.left.data(), a.left.size() * sizeof(float)) == 0 &&
           std::memcmp(a.right.data(), b.right.data(), a.right.size() * sizeof(float)) == 0;
}

/// A one-pole low-pass at `hz`, in double.
struct OnePole
{
    double a, y{0.0};
    explicit OnePole(double hz) : a(1.0 - std::exp(-2.0 * std::numbers::pi * hz / kRate)) {}
    double lp(double x) { return y += a * (x - y); }
    double hp(double x) { return x - lp(x); }
};

/// The left channel split into three bands with cascades of two one-poles each: low below
/// 200 Hz, mid between 200 Hz and 4 kHz, high above 4 kHz.
struct Bands
{
    std::vector<double> low, mid, high;
};

Bands splitBands(const std::vector<float> &x)
{
    Bands b;
    b.low.reserve(x.size());
    b.mid.reserve(x.size());
    b.high.reserve(x.size());
    OnePole l1(200), l2(200), m1(200), m2(200), m3(4000), m4(4000), h1(4000), h2(4000);
    for (float v : x)
    {
        b.low.push_back(l2.lp(l1.lp(v)));
        b.mid.push_back(m4.lp(m3.lp(m2.hp(m1.hp(v)))));
        b.high.push_back(h2.hp(h1.hp(v)));
    }
    return b;
}

double rms(const std::vector<double> &x, double fromSeconds, double toSeconds)
{
    double sum = 0.0;
    const size_t a = frames(fromSeconds), b = frames(toSeconds);
    for (size_t i = a; i < b; ++i)
    {
        sum += x[i] * x[i];
    }
    return std::sqrt(sum / double(b - a));
}

double rms(const std::vector<float> &x, double fromSeconds, double toSeconds)
{
    return rms(std::vector<double>(x.begin(), x.end()), fromSeconds, toSeconds);
}

double dB(double ratio) { return 20.0 * std::log10(ratio); }

/// The level change (dB) of each band from seconds 1-2 to seconds 29-30.
struct BandChange
{
    double low, mid, high;
};

BandChange bandChange(const std::vector<float> &x)
{
    const auto b = splitBands(x);
    return {dB(rms(b.low, 29, 30) / rms(b.low, 1, 2)), dB(rms(b.mid, 29, 30) / rms(b.mid, 1, 2)),
            dB(rms(b.high, 29, 30) / rms(b.high, 1, 2))};
}

/// Renders 0.5 s of noise at 0.25 (below loopClip's knee), then silence to 30 s, at infinite
/// decay with no modulation (a modulated read interpolates, which loses highs slowly) and the
/// given HF damping. Returns the band changes of the left output.
BandChange infiniteBandChange(float hfDamping)
{
    GgFxConfig::Storage storage;
    storage.sampleRate = kRate;
    setRingingValues(storage);
    storage.values[Rev2::rev2_modulation] = 0.f;
    storage.values[Rev2::rev2_hf_damping] = hfDamping;
    auto reverb = std::make_unique<Rev2Patched>(&storage, &storage, &storage);
    reverb->initialize();
    reverb->setInfinite(true);
    const auto out = render(*reverb, noiseThenSilence(0.5, 30.0, 0.25f), [](size_t) {});
    REQUIRE(
        std::all_of(out.left.begin(), out.left.end(), [](float v) { return std::isfinite(v); }));
    const auto change = bandChange(out.left);
    UNSCOPED_INFO("hf damping " << hfDamping << ": low " << change.low << " dB, mid " << change.mid
                                << " dB, high " << change.high << " dB");
    return change;
}

/// The largest |out[n] - out[n - 1]| over both channels, from 0.25 s on. (Reverb 2 starts with
/// its mix at 0 and smooths it toward 1 block by block, so the dry input shows at first.)
float maxStep(const Stereo &s)
{
    float m = 0.f;
    for (size_t i = frames(0.25); i < s.left.size(); ++i)
    {
        m = std::max(
            {m, std::fabs(s.left[i] - s.left[i - 1]), std::fabs(s.right[i] - s.right[i - 1])});
    }
    return m;
}
} // namespace

TEST_CASE("patched Reverb2 with no patch matches upstream bit for bit", "[space]")
{
    GgFxConfig::Storage storage;
    storage.sampleRate = kRate;
    setRingingValues(storage);
    storage.values[Rev2::rev2_predelay] = -5.f; // 2^-5 s: 1,500 samples
    auto upstream = std::make_unique<Rev2>(&storage, &storage, &storage);
    auto patched = std::make_unique<Rev2Patched>(&storage, &storage, &storage);
    upstream->initialize();
    patched->initialize();

    const auto in = noiseThenSilence(2.0, 2.0, 0.5f);
    // Both read the same storage: set it before each chunk, the same for both.
    auto values = [&](size_t c) {
        const bool late = c >= chunkAt(0.5);
        storage.values[Rev2::rev2_room_size] = late ? 0.6f : -0.3f;
        storage.values[Rev2::rev2_decay_time] = late ? 2.5f : 0.5f;
    };
    const auto a = render(*upstream, in, values);
    const auto b = render(*patched, in, values);
    REQUIRE(rms(a.left, 0.0, 2.0) > 0.01);
    REQUIRE(sameBits(a, b));
}

TEST_CASE("patched Reverb2 infinite with no damping holds its level", "[space]")
{
    const auto change = infiniteBandChange(0.f);
    CHECK(std::fabs(change.low) < 1.0);
    CHECK(std::fabs(change.mid) < 1.0);
    CHECK(std::fabs(change.high) < 1.0);
}

TEST_CASE("patched Reverb2 infinite with damping fades only highs", "[space]")
{
    // Ruling P2: the HF damper's one-pole also takes a little of the mids on each pass, so only
    // the lows are asserted to hold.
    const auto change = infiniteBandChange(0.3f);
    CHECK(std::fabs(change.low) < 1.5);
    CHECK(change.high < -20.0);
    CHECK(change.mid > change.high);
}

TEST_CASE("patched Reverb2 leaving infinite decays", "[space]")
{
    GgFxConfig::Storage storage;
    storage.sampleRate = kRate;
    setRingingValues(storage); // Decay 1.0: 2 s
    auto reverb = std::make_unique<Rev2Patched>(&storage, &storage, &storage);
    reverb->initialize();

    // Infinite for the first 2 s (0.5 s of noise, then silence), finite from then on.
    const auto out = render(*reverb, noiseThenSilence(0.5, 8.0, 0.25f),
                            [&](size_t c) { reverb->setInfinite(c < chunkAt(2.0)); });
    const double held = rms(out.left, 1.5, 2.0);
    const double after = rms(out.left, 7.5, 8.0);
    UNSCOPED_INFO("RMS 1.5-2 s " << held << ", 7.5-8 s " << after << ": " << dB(after / held)
                                 << " dB");
    REQUIRE(held > 0.01);
    CHECK(dB(after / held) < -40.0);
}

namespace
{
/// Reverb 2's values for the pre-delay test: no diffusion or buildup (their allpasses become
/// plain delays), no modulation or damping (the LF damper at 0 still high-passes near 77 Hz),
/// and the shortest decay, so the loop's feedback is negligible.
void setOneCopyValues(GgFxConfig::Storage &storage)
{
    setRingingValues(storage);
    storage.values[Rev2::rev2_decay_time] = -4.f; // 1/16 s
    storage.values[Rev2::rev2_diffusion] = 0.f;
    storage.values[Rev2::rev2_buildup] = 0.f;
    storage.values[Rev2::rev2_modulation] = 0.f;
    storage.values[Rev2::rev2_lf_damping] = 0.f;
    storage.values[Rev2::rev2_hf_damping] = 0.f;
}

/// A probe for the pre-delay test: Reverb 2 with only its first delay's taps sounding. With
/// setOneCopyValues, its output is then one delayed copy of what leaves the pre-delay. With all
/// four taps it is a comb of four copies, whose gain differs between 220 Hz and the pitches a
/// glide passes through, and whose copies carry different pitches while a glide starts or
/// stops: either would move the largest step by more than the glide's own 1.5x.
template <typename Reverb> struct OneTap : Reverb
{
    using Reverb::Reverb;
    void initializeOneTap()
    {
        this->initialize(); // sets the tap gains
        for (int b = 1; b < Reverb::NUM_BLOCKS; ++b)
        {
            this->_tap_gainL[b] = 0.f;
            this->_tap_gainR[b] = 0.f;
        }
    }
};
} // namespace

TEST_CASE("patched Reverb2 predelay glide has no click", "[space]")
{
    // A steady 220 Hz tone for 4 s, starting at its peak (a cosine), so that it is at a peak
    // whenever the pre-delay starts to move: at 1 s and 2.5 s, whole numbers of its periods.
    Stereo in(chunkAt(4.0) * kChunk);
    for (size_t i = 0; i < in.left.size(); ++i)
    {
        in.left[i] = in.right[i] =
            float(0.5 * std::cos(2.0 * std::numbers::pi * 220.0 * double(i) / kRate));
    }
    // The pre-delay moves from 0 to 24,109 samples at 1 s (a glide of 48,218 samples, done by
    // 2.0 s) and back to 0 at 2.5 s (done by 3.5 s). That is 110.5 periods of the tone, so a
    // jump instead of a glide would flip its phase, from one peak to the other. (At 24,000
    // samples, 110 periods exactly, a jump wouldn't show.)
    constexpr float kAway = 24109.f;
    auto away = [](size_t chunk) { return chunk >= chunkAt(1.0) && chunk < chunkAt(2.5); };
    auto run = [&](bool moving) {
        GgFxConfig::Storage storage;
        storage.sampleRate = kRate;
        setOneCopyValues(storage);
        auto reverb = std::make_unique<OneTap<Rev2Patched>>(&storage, &storage, &storage);
        reverb->initializeOneTap();
        return render(*reverb, in, [&](size_t c) {
            reverb->setPredelaySamples(moving && away(c) ? kAway : 0.f);
        });
    };
    const float heldStep = maxStep(run(false));
    const float glideStep = maxStep(run(true));
    UNSCOPED_INFO("largest step: held " << heldStep << ", glide " << glideStep << " ("
                                        << glideStep / heldStep << "x)");
    REQUIRE(heldStep > 0.f);
    CHECK(glideStep <= 1.6f * heldStep);

    // The test can fail: the same moves through Reverb 2's own pre-delay, which jumps, click.
    GgFxConfig::Storage storage;
    storage.sampleRate = kRate;
    setOneCopyValues(storage);
    auto jumping = std::make_unique<OneTap<Rev2>>(&storage, &storage, &storage);
    jumping->initializeOneTap();
    const float jumpStep = maxStep(render(*jumping, in, [&](size_t c) {
        storage.values[Rev2::rev2_predelay] =
            away(c) ? float(std::log2(kAway / kRate)) : -8.f; // -8: its shortest, 187 samples
    }));
    UNSCOPED_INFO("largest step with jumps: " << jumpStep << " (" << jumpStep / heldStep << "x)");
    CHECK(jumpStep > 1.6f * heldStep);
}

TEST_CASE("patched Reverb2 clear matches a fresh instance", "[space]")
{
    // Before the clear: 1 s (shorter than every line's sweep) or 3 s (longer than the delay
    // lines' 131,072-sample sweep) of noise with Size moving, a stretch at infinite decay, and
    // the smoothed pre-delay gliding. After it: 2 s of other noise, rendered by the cleared
    // instance and by a fresh one, both either left unpatched or both driven the same way.
    for (const double before : {1.0, 3.0})
    {
        for (const bool patchAfter : {false, true})
        {
            GgFxConfig::Storage storage;
            storage.sampleRate = kRate;
            setRingingValues(storage);
            auto used = std::make_unique<Rev2Patched>(&storage, &storage, &storage);
            used->initialize();
            render(*used, noiseThenSilence(before, before, 0.5f), [&](size_t c) {
                const double t = double(c * kChunk) / kRate;
                storage.values[Rev2::rev2_room_size] = t < 0.4 ? 1.f : (t < 0.7 ? -1.f : 0.3f);
                used->setInfinite(t >= 0.2 && t < 0.6);
                used->setPredelaySamples(t < 0.5 ? 3000.f : 500.f);
            });

            used->clear();
            auto fresh = std::make_unique<Rev2Patched>(&storage, &storage, &storage);
            fresh->initialize();

            Stereo after(chunkAt(2.0) * kChunk);
            after.left = noise(after.left.size(), 3, 0.5f);
            after.right = noise(after.right.size(), 4, 0.5f);
            auto drive = [&](Rev2Patched &r) {
                return [&r, &storage, patchAfter](size_t c) {
                    storage.values[Rev2::rev2_room_size] = c < chunkAt(1.0) ? 0.2f : 0.8f;
                    if (patchAfter)
                    {
                        r.setInfinite(c >= chunkAt(1.0));
                        r.setPredelaySamples(c < chunkAt(0.5) ? 2000.f : 100.f);
                    }
                };
            };
            const auto a = render(*used, after, drive(*used));
            const auto b = render(*fresh, after, drive(*fresh));
            INFO("before " << before << " s, patched after: " << patchAfter);
            REQUIRE(rms(b.left, 0.0, 2.0) > 0.01);
            REQUIRE(sameBits(a, b));
        }
    }

    // One block before the clear, then the longest pre-delay, whose reads start at the oldest
    // samples in its buffer, just ahead of the write position: where that block's writes were.
    GgFxConfig::Storage storage;
    storage.sampleRate = kRate;
    setRingingValues(storage);
    auto used = std::make_unique<Rev2Patched>(&storage, &storage, &storage);
    used->initialize();
    render(*used, noiseThenSilence(0.001, 0.001, 0.5f), [](size_t) {}); // one chunk
    used->clear();
    auto fresh = std::make_unique<Rev2Patched>(&storage, &storage, &storage);
    fresh->initialize();
    const auto after = noiseThenSilence(0.1, 0.1, 0.5f);
    auto longest = [](Rev2Patched &r) {
        return [&r](size_t) { r.setPredelaySamples(1.e9f); }; // clamped to the buffer
    };
    REQUIRE(sameBits(render(*used, after, longest(*used)), render(*fresh, after, longest(*fresh))));
}
