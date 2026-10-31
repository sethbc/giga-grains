// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/spectral/SpectralSource.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <new>
#include <stdexcept>

#include <pffft/pffft.h>

#include "gg/core/MathDet.h"
#include "gg/core/Rng.h"

namespace gg
{
namespace
{
// The smallest and largest N: the hop N/4 stays a multiple of 32, and pffft's int size and the
// buffers stay small.
constexpr uint32_t kMinFftSize = 128;
constexpr uint32_t kMaxFftSize = 1u << 24;

// The ring's newest frames a window never reads: LiveSource's head-proximity margin.
constexpr int64_t kNewestMargin = LiveSource::kHeadMargin;

// A turn in [0, 1) as the float sineDet takes, wrapped again after the rounding to float.
float turnOf(double u) noexcept
{
    auto f = static_cast<float>(u);
    return f >= 1.0f ? f - 1.0f : f;
}
} // namespace

void SpectralSource::SetupDeleter::operator()(PFFFT_Setup *setup) const noexcept
{
    pffft_destroy_setup(setup);
}

void SpectralSource::AlignedDeleter::operator()(float *p) const noexcept { pffft_aligned_free(p); }

uint32_t SpectralSource::fftSizeFor(double sampleRate) noexcept
{
    const double target = kWindowSeconds * sampleRate;
    if (!(target > static_cast<double>(kMinFftSize))) // NaN too
    {
        return kMinFftSize;
    }
    uint32_t above = kMinFftSize; // the smallest power of two >= target
    while (static_cast<double>(above) < target && above < kMaxFftSize)
    {
        above <<= 1;
    }
    const uint32_t below = above >> 1;
    const double up = static_cast<double>(above) - target;
    const double down = target - static_cast<double>(below);
    return (above > kMinFftSize && down < up) ? below : above;
}

SpectralSource::SpectralSource(double sampleRate, uint32_t maxBlockSize)
    : sampleRate_(sampleRate), maxBlockSize_(std::max<uint32_t>(1, maxBlockSize)),
      fftSize_(fftSizeFor(sampleRate))
{
    if (!(std::isfinite(sampleRate) && sampleRate > 0.0))
    {
        throw std::invalid_argument("SpectralSource: sampleRate must be finite and > 0");
    }
    const std::size_t n = fftSize_;
    setup_.reset(pffft_new_setup(static_cast<int>(fftSize_), PFFFT_REAL));
    if (!setup_)
    {
        throw std::bad_alloc();
    }
    auto aligned = [n] {
        auto *p = static_cast<float *>(pffft_aligned_malloc(n * sizeof(float)));
        if (p == nullptr)
        {
            throw std::bad_alloc();
        }
        std::fill(p, p + n, 0.0f);
        return AlignedFloats(p);
    };
    frame_ = aligned();
    spectrum_ = aligned();
    work_ = aligned();

    // The periodic Hann, w[i] = sin²(πi/N), from sineDet: sin(πi/N) = sin(2π·i/(2N)), whose
    // phase i/(2N) is exact in float.
    analysis_.resize(n);
    synthesis_.resize(n);
    const double synthesisScale = 1.0 / (static_cast<double>(n) * kOverlapGain);
    for (std::size_t i = 0; i < n; ++i)
    {
        const float s = sineDet(static_cast<float>(i) / static_cast<float>(2 * n));
        const float w = s * s;
        analysis_[i] = w;
        synthesis_[i] = static_cast<float>(static_cast<double>(w) * synthesisScale);
    }
    for (auto &a : accumulation_)
    {
        a.assign(2 * n, 0.0f);
    }
    for (auto &o : out_)
    {
        o.assign(maxBlockSize_, 0.0f);
    }
    ring_.prepare(sampleRate, kRingSeconds, maxBlockSize_);
}

SpectralSource::~SpectralSource() = default;
SpectralSource::SpectralSource(SpectralSource &&) noexcept = default;
SpectralSource &SpectralSource::operator=(SpectralSource &&) noexcept = default;

void SpectralSource::reset() noexcept
{
    ring_.reset();
    for (auto &a : accumulation_)
    {
        std::fill(a.begin(), a.end(), 0.0f);
    }
    pendingUntil_ = 0;
}

void SpectralSource::setActive(bool active) noexcept
{
    // Nothing is cleared: switched off, the frames already overlap-added drain out, and switched
    // back on during the drain, the new frames add onto what is still pending, so neither switch
    // cuts the stream. Once a drain is spent, every slot of the accumulation is 0 again.
    active_ = active;
}

void SpectralSource::process(const SpectralMaterial &m, double position, uint64_t seed,
                             uint32_t layer, int64_t blockStart, uint32_t numFrames) noexcept
{
    const auto n = static_cast<int64_t>(fftSize_);
    const int64_t hopSize = n / 4;
    const auto mask = static_cast<uint64_t>(2 * n - 1);
    float *const accL = accumulation_[0].data();
    float *const accR = accumulation_[1].data();
    float *const outL = out_[0].data();
    float *const outR = out_[1].data();
    const float *const outs[2] = {outL, outR};

    // One piece at a time, at most maxBlockSize_ each (once, with no frames, for a 0-sample
    // block, so the ring still records it).
    do
    {
        const uint32_t piece = std::min(numFrames, maxBlockSize_);
        if (!active_ && blockStart >= pendingUntil_)
        {
            ring_.write(nullptr, 0, 0, blockStart, piece); // silence, and no STFT
        }
        else
        {
            // Active, or draining: the accumulation's samples go out, and only while active are
            // new frames added. Past pendingUntil_ every slot read is +0, as silence is, so where a
            // piece ends never changes the ring.
            for (uint32_t j = 0; j < piece; ++j)
            {
                const int64_t t = blockStart + j;
                if (active_ && (t & (hopSize - 1)) == 0)
                {
                    computeFrame(m, position, seed, layer, t / hopSize);
                    pendingUntil_ = t + 2 * n; // frame t / H comes out over [t + N, t + 2N)
                }
                // The output at t is the accumulation at spectral time t − N, complete since
                // frame floor((t − N)/H) and every frame before it have been added.
                const auto slot = static_cast<std::size_t>(static_cast<uint64_t>(t - n) & mask);
                outL[j] = accL[slot];
                outR[j] = accR[slot];
                accL[slot] = 0.0f; // slot s + 2N is first written by a frame after t
                accR[slot] = 0.0f;
            }
            ring_.write(outs, 2, 0, blockStart, piece);
        }
        blockStart += piece;
        numFrames -= piece;
    } while (numFrames > 0);
}

void SpectralSource::computeFrame(const SpectralMaterial &m, double position, uint64_t seed,
                                  uint32_t layer, int64_t k) noexcept
{
    const auto n = static_cast<int64_t>(fftSize_);
    const int64_t hopTime = k * (n / 4);
    if (m.sample[0] != nullptr && m.sampleFrames > 0)
    {
        // A fraction of the sample, its window centred there; NaN reads as 0.
        const double p = position > 0.0 ? (position < 1.0 ? position : 1.0) : 0.0;
        const double centre = std::floor(p * static_cast<double>(m.sampleFrames - 1));
        const int64_t start = static_cast<int64_t>(centre) - n / 2;
        const bool mono = m.sampleMono || m.sample[1] == nullptr;
        for (uint32_t c = 0; c < (mono ? 1u : 2u); ++c)
        {
            readSample(m, c, start);
            smearInto(c, mono, seed, layer, k);
        }
        return;
    }
    if (m.ring == nullptr || !m.ring->enabled())
    {
        return; // no material: the frame is silent
    }
    // Seconds behind the ring's head at the hop itself, R − 256 − N/2 being the newest centre.
    const LiveSource &ring = *m.ring;
    const int64_t head = ring.headAt(hopTime);
    const int64_t ringFrames = ring.frames();
    double behind = position * sampleRate_;
    behind = behind == behind ? behind : 0.0; // NaN reads as 0
    const auto oldestStart = static_cast<double>(head - ringFrames);
    const auto newestStart = static_cast<double>(head - kNewestMargin - n);
    double start = newestStart - behind;
    start = std::min(std::max(start, oldestStart), newestStart); // the newest bound wins
    const auto first = static_cast<int64_t>(std::floor(start));
    const int64_t oldest = std::max<int64_t>(0, head - ringFrames);
    for (uint32_t c = 0; c < 2; ++c)
    {
        readRing(ring, c, first, oldest);
        smearInto(c, false, seed, layer, k);
    }
}

void SpectralSource::readSample(const SpectralMaterial &m, uint32_t c, int64_t start) noexcept
{
    const auto n = static_cast<int64_t>(fftSize_);
    const float *const src = m.sample[c];
    float *const frame = frame_.get();
    // Window index i reads sample frame start + i, inside [0, frames) for i in [lo, hi).
    const int64_t lo = std::clamp<int64_t>(-start, 0, n);
    const int64_t hi = std::clamp<int64_t>(m.sampleFrames - start, lo, n);
    std::fill(frame, frame + lo, 0.0f);
    for (int64_t i = lo; i < hi; ++i)
    {
        frame[i] = src[start + i] * analysis_[static_cast<std::size_t>(i)];
    }
    std::fill(frame + hi, frame + n, 0.0f);
}

void SpectralSource::readRing(const LiveSource &ring, uint32_t c, int64_t start,
                              int64_t oldest) noexcept
{
    const auto n = static_cast<int64_t>(fftSize_);
    const LiveSource::RingView view = ring.view();
    float *const frame = frame_.get();
    // Window index i reads ring frame start + i; frames below oldest (before frame 0, or out of
    // the ring's window) read 0. The window ends 256 frames before the head, so none is newer.
    const int64_t lo = std::clamp<int64_t>(oldest - start, 0, n);
    std::fill(frame, frame + lo, 0.0f);
    if (lo == n)
    {
        return;
    }
    int64_t slot = (start + lo) % view.capacity;
    for (int64_t i = lo; i < n; ++i)
    {
        frame[i] = view.slots[2 * slot + c] * analysis_[static_cast<std::size_t>(i)];
        slot = slot + 1 == view.capacity ? 0 : slot + 1;
    }
}

void SpectralSource::smearInto(uint32_t c, bool mono, uint64_t seed, uint32_t layer,
                               int64_t k) noexcept
{
    const auto n = static_cast<std::size_t>(fftSize_);
    float *const frame = frame_.get();
    float *const spectrum = spectrum_.get();
    pffft_transform_ordered(setup_.get(), frame, spectrum, work_.get(), PFFFT_FORWARD);

    // Ordered layout: spectrum[0] is DC and spectrum[1] Nyquist, both real and kept; bin b in
    // 1 … N/2 − 1 is (spectrum[2b], spectrum[2b + 1]).
    const uint64_t firstIndex = static_cast<uint64_t>(k) * (n / 2 + 1);
    for (std::size_t b = 1; b < n / 2; ++b)
    {
        const float re = spectrum[2 * b];
        const float im = spectrum[2 * b + 1];
        const float magnitude = std::sqrt(re * re + im * im);
        const rng::Key key{seed, layer, c, rng::Field::SpectralPhase, firstIndex + b};
        const double theta = rng::uniform01(rng::hash(key)); // turns
        const double quarterOn = theta + 0.25 < 1.0 ? theta + 0.25 : theta + 0.25 - 1.0;
        spectrum[2 * b] = magnitude * sineDet(turnOf(quarterOn)); // cos θ
        spectrum[2 * b + 1] = magnitude * sineDet(turnOf(theta)); // sin θ
    }

    pffft_transform_ordered(setup_.get(), spectrum, frame, work_.get(), PFFFT_BACKWARD);

    // Overlap-add for spectral time [kH, kH + N), scaled by 1/N, the synthesis Hann and
    // 1/kOverlapGain (synthesis_ holds all three).
    const auto mask = static_cast<uint64_t>(2 * n - 1);
    const uint64_t first = static_cast<uint64_t>(k) * (n / 4);
    float *const acc = accumulation_[c].data();
    float *const copy = mono ? accumulation_[1].data() : nullptr;
    for (std::size_t i = 0; i < n; ++i)
    {
        const float y = frame[i] * synthesis_[i];
        const auto slot = static_cast<std::size_t>((first + i) & mask);
        acc[slot] += y;
        if (copy != nullptr)
        {
            copy[slot] += y;
        }
    }
}
} // namespace gg
