// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Space's reverb (spec §6.3-§6.4): sst-effects' Reverb 2 under gg-core's own FXConfig
// (core/src/space/GgFxConfig.h), the patch, and SpaceReverb, which runs it on the grid. Tagged
// [space].

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/ParamIds.h"
#include "gg/core/Rng.h"
#include "gg/core/SpaceReverb.h"
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
    // The pre-delay's target: 0, then 23,891 samples at 1 s, then, mid-glide at 1.5 s (where
    // the delay has reached 12,000), 6,000, so the glide turns round; then 0 again at 2.5 s
    // (done by 2.75 s). 23,891 samples is 109.5 periods of the tone, so a jump there instead
    // of a glide would flip its phase, from one peak to the other. (At 24,000 samples, 110
    // periods exactly, a jump wouldn't show.)
    constexpr float kAway = 23891.f;
    constexpr float kBack = 6000.f;
    auto targetAt = [](size_t chunk) {
        if (chunk < chunkAt(1.0) || chunk >= chunkAt(2.5))
            return 0.f;
        return chunk < chunkAt(1.5) ? kAway : kBack;
    };
    auto run = [&](bool moving) {
        GgFxConfig::Storage storage;
        storage.sampleRate = kRate;
        setOneCopyValues(storage);
        auto reverb = std::make_unique<OneTap<Rev2Patched>>(&storage, &storage, &storage);
        reverb->initializeOneTap();
        return render(*reverb, in,
                      [&](size_t c) { reverb->setPredelaySamples(moving ? targetAt(c) : 0.f); });
    };
    const float heldStep = maxStep(run(false));
    const float glideStep = maxStep(run(true));
    UNSCOPED_INFO("largest step: held " << heldStep << ", glide " << glideStep << " ("
                                        << glideStep / heldStep << "x)");
    REQUIRE(heldStep > 0.f);
    // Why 1.6: at kPredelayMaxRate (0.5 samples per sample) the read advances at between 0.5
    // and 1.5 samples per sample, so a glide multiplies the tone's frequency, and with it its
    // largest step, by at most 1.5. The LF damper's ~77 Hz high-pass passes 330 Hz about 3%
    // louder than 220 Hz (1.5 x 1.03 = 1.55), and turning a glide round or stopping it only
    // changes the rate. 1.6 leaves room for that (measured here: about 1.58, the largest in
    // the stretch gliding at 1.5); a jump is many times larger (below).
    CHECK(glideStep <= 1.6f * heldStep);

    // The test can fail: the same moves through Reverb 2's own pre-delay, which jumps, click.
    GgFxConfig::Storage storage;
    storage.sampleRate = kRate;
    setOneCopyValues(storage);
    auto jumping = std::make_unique<OneTap<Rev2>>(&storage, &storage, &storage);
    jumping->initializeOneTap();
    const float jumpStep = maxStep(render(*jumping, in, [&](size_t c) {
        const float target = targetAt(c);
        // -8: its shortest, 187 samples
        storage.values[Rev2::rev2_predelay] =
            target > 0.f ? float(std::log2(target / kRate)) : -8.f;
    }));
    UNSCOPED_INFO("largest step with jumps: " << jumpStep << " (" << jumpStep / heldStep << "x)");
    CHECK(jumpStep > 1.6f * heldStep);
}

namespace
{
/// A patched Reverb 2, used for `seconds` before a clear: noise, with Size moving, a stretch at
/// infinite decay, and the smoothed pre-delay gliding.
std::unique_ptr<Rev2Patched> usedReverb(GgFxConfig::Storage &storage, double seconds)
{
    auto r = std::make_unique<Rev2Patched>(&storage, &storage, &storage);
    r->initialize();
    render(*r, noiseThenSilence(seconds, seconds, 0.5f), [&](size_t c) {
        const double t = double(c * kChunk) / kRate;
        storage.values[Rev2::rev2_room_size] = t < 0.4 ? 1.f : (t < 0.7 ? -1.f : 0.3f);
        r->setInfinite(t >= 0.2 && t < 0.6);
        r->setPredelaySamples(t < 0.5 ? 3000.f : 500.f);
    });
    return r;
}

/// What a cleared and a fresh instance render after the clear: `seconds` of other noise, with
/// Size moving and, with `patch`, infinite decay and the smoothed pre-delay at `predelay`.
Stereo renderAfter(Rev2Patched &r, GgFxConfig::Storage &storage, double seconds, bool patch,
                   float predelay)
{
    Stereo after(chunkAt(seconds) * kChunk);
    after.left = noise(after.left.size(), 3, 0.5f);
    after.right = noise(after.right.size(), 4, 0.5f);
    return render(r, after, [&](size_t c) {
        storage.values[Rev2::rev2_room_size] = c < chunkAt(1.0) ? 0.2f : 0.8f;
        if (patch)
        {
            r.setInfinite(c >= chunkAt(1.0));
            r.setPredelaySamples(c < chunkAt(0.5) ? predelay : 100.f);
        }
    });
}

Stereo renderFresh(GgFxConfig::Storage &storage, double seconds, bool patch, float predelay)
{
    auto fresh = std::make_unique<Rev2Patched>(&storage, &storage, &storage);
    fresh->initialize();
    return renderAfter(*fresh, storage, seconds, patch, predelay);
}
} // namespace

TEST_CASE("patched Reverb2 clear matches a fresh instance", "[space]")
{
    // Used for 1 s (no line swept yet), 2.6 s (the delay lines' writes have reached the
    // window a fresh run reads first, 131,072 - ~17,400, but not swept round: the partial
    // clear) or 3 s (swept round). Then cleared, and rendered the same as a fresh instance,
    // both left unpatched or both patched.
    for (const double before : {1.0, 2.6, 3.0})
    {
        for (const bool patchAfter : {false, true})
        {
            GgFxConfig::Storage storage;
            storage.sampleRate = kRate;
            setRingingValues(storage);
            auto used = usedReverb(storage, before);
            used->clear();
            const auto a = renderAfter(*used, storage, 2.0, patchAfter, 2000.f);
            const auto b = renderFresh(storage, 2.0, patchAfter, 2000.f);
            INFO("before " << before << " s, patched after: " << patchAfter);
            REQUIRE(rms(b.left, 0.0, 2.0) > 0.01);
            REQUIRE(sameBits(a, b));
        }
    }

    // Used for 31.9 s: the pre-delay's writes have reached the window that its longest delay
    // (0.5 s) reads first, without sweeping round. Then the longest pre-delay.
    GgFxConfig::Storage storage;
    storage.sampleRate = kRate;
    setRingingValues(storage);
    auto used = usedReverb(storage, 31.9);
    used->clear();
    const float longest = 1.e9f; // clamped to kMaxPredelaySeconds
    REQUIRE(sameBits(renderAfter(*used, storage, 1.0, true, longest),
                     renderFresh(storage, 1.0, true, longest)));
}

TEST_CASE("patched Reverb2 clear in steps matches clear at once", "[space]")
{
    // Two instances used alike for 3 s; one cleared at once, one 4,096 floats at a time.
    GgFxConfig::Storage storage;
    storage.sampleRate = kRate;
    setRingingValues(storage);
    auto atOnce = usedReverb(storage, 3.0);
    auto inSteps = usedReverb(storage, 3.0);
    atOnce->clear();
    int steps = 1;
    while (!inSteps->clearStep(4096))
    {
        ++steps;
        REQUIRE(steps < 100000);
    }
    UNSCOPED_INFO("clearStep(4096) took " << steps << " calls");
    CHECK(steps > 10);

    const auto a = renderAfter(*atOnce, storage, 2.0, true, 2000.f);
    const auto b = renderAfter(*inSteps, storage, 2.0, true, 2000.f);
    const auto fresh = renderFresh(storage, 2.0, true, 2000.f);
    REQUIRE(rms(fresh.left, 0.0, 2.0) > 0.01);
    REQUIRE(sameBits(a, fresh));
    REQUIRE(sameBits(b, fresh));
}

TEST_CASE("patched Reverb2 loopClip values", "[space]")
{
    const auto clip = [](float x) { return Rev2Patched::loopClip(x); };
    CHECK(clip(0.f) == 0.f);
    CHECK(clip(0.5f) == 0.5f);
    CHECK(clip(-0.5f) == -0.5f);
    CHECK(clip(1.f) == 1.f);
    CHECK(clip(-1.f) == -1.f);
    CHECK(clip(2.f) == 1.5f);
    CHECK(clip(-2.f) == -1.5f);
    CHECK(clip(4.f) == 1.75f);
    CHECK(clip(1.e6f) < 2.f);
    CHECK(clip(1.e30f) <= 2.f);
    CHECK(clip(-1.e30f) >= -2.f);
    CHECK(clip(std::numeric_limits<float>::infinity()) == 2.f);
    // Continuous with slope 1 at the knee: just past 1, 2 - 1/x ~ x - (x - 1)^2.
    for (const float x : {1.f, std::nextafter(1.f, 2.f), 1.001f, 1.01f})
    {
        CHECK(std::fabs(clip(x) - x) <= (x - 1.f) * (x - 1.f) * 1.01f + 1e-7f);
        CHECK(std::fabs(clip(-x) + x) <= (x - 1.f) * (x - 1.f) * 1.01f + 1e-7f);
    }
    // Increasing.
    float last = clip(-100.f);
    for (float x = -99.f; x <= 100.f; x += 0.25f)
    {
        CHECK(clip(x) > last);
        last = clip(x);
    }
}

namespace
{
/// A probe that reads the loop's state between blocks: the signal that went round the loop.
struct LoopProbe : Rev2Patched
{
    using Rev2Patched::Rev2Patched;
    float loopState() const { return this->_state; }
};
} // namespace

TEST_CASE("patched Reverb2 infinite stays bounded under full-scale input", "[space]")
{
    // At infinity with no damping, the largest room and full modulation, 10 s of full-scale
    // noise or a 0 dBFS 110 Hz sine keep pushing the lossless loop. loopClip bounds what
    // enters each delay below 2, so what leaves it (the loop state) is within 2 and each output
    // channel, four taps whose gains sum to 1.125, within 2.25.
    for (const bool sine : {false, true})
    {
        Stereo in(chunkAt(10.0) * kChunk);
        if (sine)
        {
            for (size_t i = 0; i < in.left.size(); ++i)
            {
                in.left[i] = in.right[i] =
                    float(std::sin(2.0 * std::numbers::pi * 110.0 * double(i) / kRate));
            }
        }
        else
        {
            in.left = noise(in.left.size(), 5, 1.f);
            in.right = noise(in.right.size(), 6, 1.f);
        }
        GgFxConfig::Storage storage;
        storage.sampleRate = kRate;
        setRingingValues(storage);
        storage.values[Rev2::rev2_room_size] = 1.f;
        storage.values[Rev2::rev2_modulation] = 1.f;
        storage.values[Rev2::rev2_hf_damping] = 0.f;
        auto reverb = std::make_unique<LoopProbe>(&storage, &storage, &storage);
        reverb->initialize();
        reverb->setInfinite(true);
        float peakState = 0.f;
        const auto out = render(*reverb, in, [&](size_t) {
            peakState = std::max(peakState, std::fabs(reverb->loopState()));
        });
        float peakOut = 0.f;
        bool finite = true;
        // From 0.25 s on: Reverb 2's mix starts at 0 and smooths toward 1, so the dry input
        // shows at first.
        for (size_t i = frames(0.25); i < out.left.size(); ++i)
        {
            finite = finite && std::isfinite(out.left[i]) && std::isfinite(out.right[i]);
            peakOut = std::max({peakOut, std::fabs(out.left[i]), std::fabs(out.right[i])});
        }
        UNSCOPED_INFO((sine ? "sine" : "noise")
                      << ": loop state peak " << peakState << ", output peak " << peakOut);
        REQUIRE(finite);
        CHECK(peakState <= 2.f);
        CHECK(peakOut <= 2.25f);
        CHECK(peakState > 1.f); // the input did drive the loop past loopClip's knee
    }
}

// ---------------------------------------------------------------------------------------------
// SpaceReverb (gg/core/SpaceReverb.h): the patched Reverb 2 on the absolute 32-sample grid, with
// Space's parameters mapped onto it and the idle rule.

namespace
{
using gg::core::SpaceParams;
using gg::core::SpaceReverb;

/// Space's parameters at their defaults (spec §6.5), but with Amount 1 (SpaceReverb ignores
/// it) and the given pre-delay.
SpaceParams spaceDefaults(float predelaySeconds = 0.f)
{
    return {.diffusion = 0.f,
            .amount = 1.f,
            .drySend = 0.f,
            .decayLog2 = 1.f,
            .infinite = false,
            .roomSize = 0.f,
            .hfDamping = 0.3f,
            .modulation = 0.25f,
            .predelaySeconds = predelaySeconds};
}

/// Drives `reverb` one sample at a time over `in`, from absolute time `t0`, with the
/// parameters `params(t)` and the activity `active(t)`. Returns the output.
template <typename Params, typename Active>
Stereo drive(SpaceReverb &reverb, const Stereo &in, Params &&params, Active &&active,
             int64_t t0 = 0)
{
    Stereo out(in.left.size());
    for (size_t i = 0; i < in.left.size(); ++i)
    {
        const int64_t t = t0 + int64_t(i);
        reverb.processSample(t, in.left[i], in.right[i], params(t), active(t), out.left[i],
                             out.right[i]);
    }
    return out;
}

/// The same, always active, at fixed parameters.
Stereo drive(SpaceReverb &reverb, const Stereo &in, const SpaceParams &p)
{
    return drive(reverb, in, [&p](int64_t) { return p; }, [](int64_t) { return true; });
}

/// A unit impulse on both channels at sample 0, then silence: `n` samples.
Stereo impulse(size_t n)
{
    Stereo s(n);
    s.left[0] = 1.f;
    s.right[0] = 1.f;
    return s;
}

/// The first sample at which either channel exceeds 1e-6 in magnitude, or -1.
int64_t onset(const Stereo &s)
{
    for (size_t i = 0; i < s.left.size(); ++i)
    {
        if (std::fabs(s.left[i]) > 1e-6f || std::fabs(s.right[i]) > 1e-6f)
            return int64_t(i);
    }
    return -1;
}

bool allFinite(const Stereo &s)
{
    const auto finite = [](float v) { return std::isfinite(v); };
    return std::all_of(s.left.begin(), s.left.end(), finite) &&
           std::all_of(s.right.begin(), s.right.end(), finite);
}

/// Every sample of [from, to) is +0.0f on both channels.
bool positiveZeros(const Stereo &s, size_t from, size_t to)
{
    for (size_t i = from; i < to; ++i)
    {
        if (std::bit_cast<uint32_t>(s.left[i]) != 0u || std::bit_cast<uint32_t>(s.right[i]) != 0u)
            return false;
    }
    return true;
}

/// Samples [from, from + n) of `s`.
Stereo slice(const Stereo &s, size_t from, size_t n)
{
    Stereo r(n);
    std::copy_n(s.left.begin() + std::ptrdiff_t(from), n, r.left.begin());
    std::copy_n(s.right.begin() + std::ptrdiff_t(from), n, r.right.begin());
    return r;
}

/// Stereo noise at `amplitude` (independent channels, streams `stream` and `stream + 1`).
Stereo stereoNoise(size_t n, uint32_t stream, float amplitude)
{
    Stereo s(n);
    s.left = noise(n, stream, amplitude);
    s.right = noise(n, stream + 1, amplitude);
    return s;
}

/// The parameter history of the call-pattern test: changes on grid boundaries only, covering
/// every mapped value, the pre-delay and infinity.
SpaceParams scheduled(int64_t t)
{
    const int64_t c = t / SpaceReverb::kChunk;
    auto p = spaceDefaults();
    if (c >= 200)
    {
        p.roomSize = 1.f;
        p.decayLog2 = 2.5f;
    }
    if (c >= 400)
    {
        p.predelaySeconds = 0.05f;
        p.modulation = 1.f;
    }
    if (c >= 700)
    {
        p.infinite = true;
        p.decayLog2 = 6.f;
        p.hfDamping = 0.f;
    }
    if (c >= 1000)
    {
        p.infinite = false;
        p.decayLog2 = -1.f;
        p.roomSize = -1.f;
        p.predelaySeconds = 0.01f;
        p.hfDamping = 1.f;
    }
    return p;
}

/// Idle for chunks 1200 to 1219 of the call-pattern test.
bool scheduledActive(int64_t t)
{
    const int64_t c = t / SpaceReverb::kChunk;
    return c < 1200 || c >= 1220;
}
} // namespace

TEST_CASE("SpaceReverb return is 32 samples late at any call pattern", "[space]")
{
    const size_t n = 48000;
    const auto in = stereoNoise(n, 11, 0.5f);

    // One call per sample, in one run: the reference.
    auto ref = std::make_unique<SpaceReverb>(kRate);
    const auto expected = drive(*ref, in, scheduled, scheduledActive);
    REQUIRE(allFinite(expected));
    // Nothing comes back for the first chunk: the return is the previous chunk's output.
    CHECK(positiveZeros(expected, 0, SpaceReverb::kChunk));
    CHECK(rms(expected.left, 0.0, 1.0) > 0.01);
    // Idle chunks return +0.
    CHECK(positiveZeros(expected, 1200 * SpaceReverb::kChunk, 1220 * SpaceReverb::kChunk));

    // The same history, split into host blocks. On the grid each sample gets the scheduled
    // values and activity; off it, different ones, the activity inverted too: a chunk must use
    // only what it got at its first sample.
    const auto split = [&](auto &&nextBlockSize) {
        auto reverb = std::make_unique<SpaceReverb>(kRate);
        Stereo out(n);
        size_t start = 0;
        while (start < n)
        {
            const size_t end = std::min(n, start + nextBlockSize());
            for (size_t i = start; i < end; ++i)
            {
                const auto t = int64_t(i);
                auto p = scheduled(t);
                if (t % SpaceReverb::kChunk != 0)
                {
                    p.roomSize = -p.roomSize;
                    p.infinite = !p.infinite;
                    p.decayLog2 += 1.f;
                    p.predelaySeconds = 0.5f - p.predelaySeconds;
                    p.hfDamping = 1.f - p.hfDamping;
                    p.modulation = 1.f - p.modulation;
                }
                const bool onGrid = t % SpaceReverb::kChunk == 0;
                const bool active = onGrid ? scheduledActive(t) : !scheduledActive(t);
                reverb->processSample(t, in.left[i], in.right[i], p, active, out.left[i],
                                      out.right[i]);
            }
            start = end;
        }
        return out;
    };

    for (const size_t blockSize : {1u, 7u, 31u, 32u, 33u, 4096u})
    {
        INFO("block size " << blockSize);
        CHECK(sameBits(split([blockSize] { return blockSize; }), expected));
    }
    for (uint32_t seed = 0; seed < 3; ++seed)
    {
        INFO("random splits, stream " << seed);
        uint64_t draw = 0;
        const auto randomSize = [&] {
            const auto h =
                gg::rng::hash({kNoiseSeed, 1, seed, gg::rng::Field::SignalNoise, draw++});
            return size_t(1 + h % 700);
        };
        CHECK(sameBits(split(randomSize), expected));
    }
}

TEST_CASE("SpaceReverb predelay offsets the onset", "[space]")
{
    const auto in = impulse(frames(0.5));
    auto a = std::make_unique<SpaceReverb>(kRate);
    auto b = std::make_unique<SpaceReverb>(kRate);
    const auto at0 = drive(*a, in, spaceDefaults(0.f));
    const auto at100ms = drive(*b, in, spaceDefaults(0.1f));
    const auto onset0 = onset(at0), onset100 = onset(at100ms);
    UNSCOPED_INFO("onset at Pre-delay 0: " << onset0 << ", at 0.1 s: " << onset100);
    REQUIRE(onset0 >= int64_t(SpaceReverb::kChunk));
    REQUIRE(onset100 > onset0);
    const int64_t expectedShift = std::llround(0.1 * kRate) - SpaceReverb::kChunk;
    CHECK(std::llabs((onset100 - onset0) - expectedShift) <= 1);
}

TEST_CASE("SpaceReverb idle returns zeros and resumes empty", "[space]")
{
    // 3 s active at Size 100%, so the delay lines have swept their whole buffers and the clear
    // is large (several idle chunks' budget); then idle; then 1 s active. A short idle stretch
    // leaves part of the clear for the reactivation to finish; a long one finishes it stepwise.
    const size_t activeChunks = chunkAt(3.0), resumeChunks = chunkAt(1.0);
    auto params = spaceDefaults(0.02f);
    params.roomSize = 1.f;
    params.modulation = 1.f;
    for (const size_t idleChunks : {1u, 10u, 400u})
    {
        INFO("idle chunks " << idleChunks);
        const size_t resumeAt = (activeChunks + idleChunks) * SpaceReverb::kChunk;
        const size_t n = resumeAt + resumeChunks * SpaceReverb::kChunk;
        const auto in = stereoNoise(n, 21, 0.5f);

        auto reverb = std::make_unique<SpaceReverb>(kRate);
        bool pendingAtResume = false;
        const auto out = drive(
            *reverb, in, [&](int64_t) { return params; },
            [&](int64_t t) {
                const auto c = size_t(t) / SpaceReverb::kChunk;
                if (size_t(t) == resumeAt)
                    pendingAtResume = reverb->clearPending();
                return c < activeChunks || c >= activeChunks + idleChunks;
            });
        REQUIRE(allFinite(out));
        CHECK(rms(out.left, 1.0, 3.0) > 0.01);
        // The idle chunks, and the first active one after them, return +0.
        CHECK(
            positiveZeros(out, activeChunks * SpaceReverb::kChunk, resumeAt + SpaceReverb::kChunk));
        UNSCOPED_INFO("idle for " << idleChunks << " chunks: clear pending at reactivation "
                                  << pendingAtResume);
        if (idleChunks == 1)
            CHECK(pendingAtResume); // the reactivation finished the clear
        if (idleChunks == 400)
            CHECK_FALSE(pendingAtResume); // the idle chunks finished it

        // A fresh instance driven over the same active stretch from t = 0.
        auto fresh = std::make_unique<SpaceReverb>(kRate);
        const auto resumedIn = slice(in, resumeAt, n - resumeAt);
        const auto expected = drive(*fresh, resumedIn, params);
        CHECK(rms(expected.left, 0.1, 1.0) > 0.01);
        CHECK(sameBits(slice(out, resumeAt, n - resumeAt), expected));
    }
}

TEST_CASE("SpaceReverb after clear matches a fresh instance", "[space]")
{
    // 2 s of noise at infinity, Size 100%, then reset() in the middle of the tail it holds.
    auto held = spaceDefaults(0.25f);
    held.infinite = true;
    held.decayLog2 = 6.f;
    held.roomSize = 1.f;
    held.modulation = 1.f;
    auto reverb = std::make_unique<SpaceReverb>(kRate);
    const auto tail = drive(*reverb, noiseThenSilence(1.0, 2.0, 0.5f), held);
    REQUIRE(rms(tail.left, 1.9, 2.0) > 0.01);
    reverb->reset();

    // Then the same new history for both, from t = 0: a finite stretch, then infinity again.
    const auto in = stereoNoise(frames(1.5), 31, 0.5f);
    const auto params = [&](int64_t t) {
        return t < int64_t(frames(0.75)) ? spaceDefaults(0.01f) : held;
    };
    const auto always = [](int64_t) { return true; };
    const auto after = drive(*reverb, in, params, always);
    auto fresh = std::make_unique<SpaceReverb>(kRate);
    const auto expected = drive(*fresh, in, params, always);
    REQUIRE(rms(expected.left, 0.5, 1.5) > 0.01);
    CHECK(sameBits(after, expected));
}

TEST_CASE("SpaceReverb at 44.1, 96 and 192 kHz", "[space]")
{
    int64_t lastOnset = 0;
    for (const double rate : {44100.0, 96000.0, 192000.0})
    {
        INFO("rate " << rate);
        const size_t n = size_t(std::llround(0.5 * rate));
        const auto in = impulse(n);
        auto a = std::make_unique<SpaceReverb>(rate);
        auto b = std::make_unique<SpaceReverb>(rate);
        const auto at0 = drive(*a, in, spaceDefaults(0.f));
        const auto at100ms = drive(*b, in, spaceDefaults(0.1f));
        REQUIRE(allFinite(at0));
        REQUIRE(allFinite(at100ms));

        // It rings: energy in the tail, 0.3-0.5 s.
        double tail = 0.0;
        for (size_t i = size_t(0.3 * rate); i < n; ++i)
            tail += double(at0.left[i]) * at0.left[i];
        CHECK(tail > 1e-6);

        // The onset moves with the rate: a later sample at a higher rate, and Pre-delay's
        // shift in samples is 0.1 s at this rate less the 32 the grid already adds.
        const auto onset0 = onset(at0), onset100 = onset(at100ms);
        UNSCOPED_INFO(rate << " Hz: onset at Pre-delay 0: " << onset0
                           << ", at 0.1 s: " << onset100);
        CHECK(onset0 > lastOnset);
        lastOnset = onset0;
        const int64_t expectedShift = std::llround(0.1 * rate) - SpaceReverb::kChunk;
        CHECK(std::llabs((onset100 - onset0) - expectedShift) <= 1);
    }
}

namespace
{
/// An hour at infinity with no damping and the largest room, full modulation: `input(i)` fills
/// one second of stereo input from absolute sample i. Rendered a second at a time. Returns the
/// largest |output| and whether every output sample was finite.
template <typename Input> std::pair<float, bool> hourAtInfinity(Input &&input)
{
    auto p = spaceDefaults();
    p.infinite = true;
    p.decayLog2 = gg::core::kDecayMax;
    p.hfDamping = 0.f;
    p.roomSize = 1.f;
    p.modulation = 1.f;
    auto reverb = std::make_unique<SpaceReverb>(kRate);
    const size_t second = frames(1.0);
    std::vector<float> l(second), r(second);
    float peak = 0.f;
    bool finite = true;
    for (int s = 0; s < 3600; ++s)
    {
        const size_t base = size_t(s) * second;
        input(base, l, r);
        for (size_t i = 0; i < second; ++i)
        {
            float outL, outR;
            reverb->processSample(int64_t(base + i), l[i], r[i], p, true, outL, outR);
            finite = finite && std::isfinite(outL) && std::isfinite(outR);
            peak = std::max({peak, std::fabs(outL), std::fabs(outR)});
        }
    }
    return {peak, finite};
}
} // namespace

// Spec §6.4: a lossless loop fed full-scale input must stay bounded for any length of render.
// loopClip keeps what enters each delay line below 2, so each output channel (four taps whose
// gains sum to 1.125) stays within 2.25: far inside 64.
TEST_CASE("SpaceReverb infinite stays bounded for an hour with a sine", "[space][slow]")
{
    const auto [peak, finite] =
        hourAtInfinity([](size_t base, std::vector<float> &l, std::vector<float> &r) {
            for (size_t i = 0; i < l.size(); ++i)
            {
                // The phase from the sample index modulo the sine's period at 48 kHz (110 Hz
                // has a period of 48000/110 samples; 11 periods are exactly 4800 samples).
                const auto k = double((base + i) % 4800);
                l[i] = r[i] = float(std::sin(2.0 * std::numbers::pi * 110.0 * k / kRate));
            }
        });
    WARN("an hour of a 0 dBFS 110 Hz sine at infinity: output peak " << peak);
    REQUIRE(finite);
    CHECK(peak < 64.f);
    CHECK(peak <= 2.25f);
    CHECK(peak > 1.f);
}

TEST_CASE("SpaceReverb infinite stays bounded for an hour with noise", "[space][slow]")
{
    const auto [peak, finite] =
        hourAtInfinity([](size_t base, std::vector<float> &l, std::vector<float> &r) {
            for (size_t i = 0; i < l.size(); ++i)
            {
                const uint64_t index = base + i;
                l[i] = float(gg::rng::bipolar(
                    gg::rng::hash({kNoiseSeed, 2, 1, gg::rng::Field::SignalNoise, index})));
                r[i] = float(gg::rng::bipolar(
                    gg::rng::hash({kNoiseSeed, 2, 2, gg::rng::Field::SignalNoise, index})));
            }
        });
    WARN("an hour of full-scale noise at infinity: output peak " << peak);
    REQUIRE(finite);
    CHECK(peak < 64.f);
    CHECK(peak <= 2.25f);
    CHECK(peak > 1.f);
}

namespace
{
/// The T60 of an impulse's tail through a fresh SpaceReverb at `p`, rendered for 10 s: the
/// level of both channels in 20 ms windows, in dB, and a least-squares line fitted to it from
/// the first window 5 dB below its peak to the first 35 dB below it.
double measureT60(const SpaceParams &p)
{
    auto reverb = std::make_unique<SpaceReverb>(kRate);
    const auto out = drive(*reverb, impulse(frames(10.0)), p);
    REQUIRE(allFinite(out));

    const size_t window = frames(0.02);
    std::vector<double> levelDb;
    for (size_t w = 0; (w + 1) * window <= out.left.size(); ++w)
    {
        double sum = 0.0;
        for (size_t i = w * window; i < (w + 1) * window; ++i)
            sum += double(out.left[i]) * out.left[i] + double(out.right[i]) * out.right[i];
        levelDb.push_back(10.0 * std::log10(sum / double(2 * window) + 1e-30));
    }
    const auto peakAt = size_t(std::max_element(levelDb.begin(), levelDb.end()) - levelDb.begin());
    const double peak = levelDb[peakAt];
    size_t from = peakAt;
    while (from < levelDb.size() && levelDb[from] > peak - 5.0)
        ++from;
    size_t to = from;
    while (to < levelDb.size() && levelDb[to] > peak - 35.0)
        ++to;
    REQUIRE(to < levelDb.size()); // it fell 35 dB within the render
    REQUIRE(to > from + 2);

    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const double count = double(to - from);
    for (size_t w = from; w < to; ++w)
    {
        const double x = double(w) * 0.02, y = levelDb[w];
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    const double slope = (count * sxy - sx * sy) / (count * sxx - sx * sx);
    REQUIRE(slope < 0.0);
    return -60.0 / slope;
}
} // namespace

TEST_CASE("SpaceReverb tail decays per Decay", "[space]")
{
    // Impulses at Damping 0 and Decay 0, 1 and 2 (labelled 1, 2 and 4 s). Two steps of Decay
    // scale the T60 by the label's 4, less what the loop loses whatever the Decay: the fixed LF
    // damping (0.2) and the dampers' clamped minimum (0.01) take a larger share of a longer
    // tail. Measured, 2.95 (3.21 with LF damping 0). So the window is 2.5-5: a lost Decay
    // mapping gives 1. Reverb 2's own formula is kept, so the T60 runs about 1.45 times the
    // label (ruling R-T5c: recorded for the listening check, not recalibrated); at Decay 1 the
    // window is that, within 25%.
    double t60[3];
    for (int d = 0; d < 3; ++d)
    {
        auto p = spaceDefaults();
        p.hfDamping = 0.f;
        p.decayLog2 = float(d);
        t60[d] = measureT60(p);
        WARN("T60 at Decay " << d << " (" << (1 << d) << " s), Damping 0: " << t60[d] << " s");
    }
    CHECK(t60[0] < t60[1]);
    CHECK(t60[1] < t60[2]);
    const double ratio = t60[2] / t60[0];
    WARN("T60 ratio, Decay 2 over Decay 0: " << ratio << " (label ratio 4)");
    CHECK(ratio > 2.5);
    CHECK(ratio < 5.0);
    const double expectedAt1 = 1.45 * 2.0;
    CHECK(t60[1] > 0.75 * expectedAt1);
    CHECK(t60[1] < 1.25 * expectedAt1);
}

TEST_CASE("SpaceReverb maps Size, Damping and Modulation", "[space]")
{
    // One discriminating measurement each, so that dropping any mapping fails. The fresh
    // instance starts at Space's defaults (Size 50%, Damping 30%, Modulation 25%); each pair
    // here is two values away from them.
    const auto run = [](const SpaceParams &p, double seconds) {
        auto reverb = std::make_unique<SpaceReverb>(kRate);
        return drive(*reverb, impulse(frames(seconds)), p);
    };

    // Size: Reverb 2 scales its delay and tap times by 2^roomSize, so the onset at Size 100%
    // (roomSize 1) is far later than at Size 0% (roomSize -1).
    auto small = spaceDefaults(), large = spaceDefaults();
    small.roomSize = -1.f;
    large.roomSize = 1.f;
    const auto smallOnset = onset(run(small, 0.5)), largeOnset = onset(run(large, 0.5));
    WARN("onset at Size 0%: " << smallOnset << ", at Size 100%: " << largeOnset);
    REQUIRE(smallOnset > 0);
    CHECK(largeOnset > 2 * smallOnset);

    // Damping: the tail's highs (above 4 kHz, 0.5-1.5 s) fall at Damping 100% against 0%.
    auto bright = spaceDefaults(), dark = spaceDefaults();
    bright.hfDamping = 0.f;
    dark.hfDamping = 1.f;
    const double brightHigh = rms(splitBands(run(bright, 1.5).left).high, 0.5, 1.5);
    const double darkHigh = rms(splitBands(run(dark, 1.5).left).high, 0.5, 1.5);
    WARN("tail above 4 kHz, Damping 100% against 0%: " << dB(darkHigh / brightHigh) << " dB");
    REQUIRE(brightHigh > 0.0);
    CHECK(darkHigh < 0.5 * brightHigh);

    // Modulation: the tail at 100% differs from the one at 0%, by more than rounding.
    auto still = spaceDefaults(), moving = spaceDefaults();
    still.modulation = 0.f;
    moving.modulation = 1.f;
    const auto a = run(still, 1.5), b = run(moving, 1.5);
    std::vector<float> diff(a.left.size());
    for (size_t i = 0; i < diff.size(); ++i)
        diff[i] = a.left[i] - b.left[i];
    const double tail = rms(a.left, 0.5, 1.5), change = rms(diff, 0.5, 1.5);
    WARN("tail change, Modulation 100% against 0%: " << dB(change / tail) << " dB");
    CHECK_FALSE(sameBits(a, b));
    CHECK(change > 0.1 * tail);

    // A NaN in a mapped value takes the bottom of its range, so the output stays finite.
    auto nan = spaceDefaults();
    nan.roomSize = nan.hfDamping = nan.modulation = nan.decayLog2 =
        std::numeric_limits<float>::quiet_NaN();
    auto bottom = spaceDefaults();
    bottom.roomSize = -1.f;
    bottom.hfDamping = bottom.modulation = 0.f;
    bottom.decayLog2 = -4.f;
    const auto fromNan = run(nan, 0.5);
    CHECK(allFinite(fromNan));
    CHECK(sameBits(fromNan, run(bottom, 0.5)));
}
