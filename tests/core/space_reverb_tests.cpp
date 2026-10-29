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
#include <limits>
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
