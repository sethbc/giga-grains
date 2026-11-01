// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_SPECTRAL_SPECTRALSOURCE_H
#define GG_SPECTRAL_SPECTRALSOURCE_H

#include <cstdint>
#include <memory>
#include <vector>

#include <sst/cpputils/rtsan_support.h>

#include "gg/source/LiveSource.h"

struct PFFFT_Setup; // pffft's opaque setup (pffft.h); pffft stays private to gg-engine

namespace gg
{
/// What one frame of the Spectral Smear reads: the layer's sample when sample[0] != nullptr,
/// otherwise its live ring.
struct SpectralMaterial
{
    /// The sample's channels (sample[1] is ignored when sampleMono, and a null sample[1] counts
    /// as mono), each sampleFrames long, read frame by frame at the running rate.
    const float *sample[2]{nullptr, nullptr};
    int64_t sampleFrames{0};
    bool sampleMono{true};
    /// The sample's rate, for information only: the loader and gg-render convert a sample to the
    /// running rate on load, so the smear never resamples it.
    double sampleRate{0.0};
    /// The live ring, already written for the block process() is given; read when there is no
    /// sample. Null, or a ring without frames, is no material: the frame is silent.
    const LiveSource *ring{nullptr};
};

/// The Spectral Smear source (extremes spec §6.2, I21): a paulstretch-style stream that a
/// Spectral layer's grains read as they read the live ring.
///
/// **The STFT.** N = fftSizeFor(sampleRate), hop H = N/4. Frame k is computed when the output
/// clock reaches kH, from one window of N frames of the material:
/// - a sample: frames from floor(position × (frames − 1)) − N/2 on, position clamped to [0, 1];
///   frames outside [0, frames) read 0;
/// - the ring: frames from floor(R − 256 − N − position × sampleRate) on, where R =
///   ring->headAt(kH) is the head at the hop itself, clamped so the window lies within
///   [R − ringFrames, R − 256); frames below 0 or below R − ringFrames read 0.
///
/// Each channel's window is multiplied by the analysis Hann and transformed forward with pffft's
/// real FFT. Bin b (1 … N/2 − 1) of channel c keeps its magnitude and takes the phase θ =
/// uniform01(hash(seed, layer, c, Field::SpectralPhase, k × (N/2 + 1) + b)) turns, cos θ and
/// sin θ from sineDet; DC and Nyquist are kept as they are. The inverse is scaled by 1/N, the
/// synthesis Hann and 1/kOverlapGain, and overlap-added for spectral time [kH, kH + N). The
/// ring's sample at output time t is the accumulation at spectral time t − N, so every sample
/// written is complete. Mono material: channel 1 copies channel 0.
///
/// So the stream is a function of the sample clock, the seed, the layer, the material and the
/// position at each hop, never of where blocks fall.
///
/// **The output ring** is a LiveSource of kRingSeconds, written on every process() call: the smear
/// while active; while inactive, first the drain, then silence (no STFT runs). It is never frozen.
/// Switching on from silence, the stream starts at the next hop boundary, N or more frames after
/// the switch, fading in over the first frames' overlap-add. Switching off, no new frame is
/// computed, but the frames already overlap-added keep coming out until they are spent, at
/// output time kH + 2N for the last frame k: the stream fades out over the synthesis windows'
/// tails, so the grains still reading it don't click (the extremes spec §3's anti-goal). From
/// there on the ring takes silence. Switching back on during the drain adds the new frames onto
/// what is still pending. The drain depends only on the frames computed, so on the switch frames
/// and never on where blocks fall.
///
/// Everything, pffft's setup and work buffer included, is allocated at construction.
class SpectralSource
{
  public:
    /// The output ring: Spray's 2 s, Size's 2 s, the 256-frame margins and a block (spec §6.2).
    static constexpr double kRingSeconds = 5.0;
    /// The STFT's window, in seconds; fftSizeFor() rounds it to a power of two.
    static constexpr double kWindowSeconds = 0.085;
    /// What the synthesis divides out: 0.75 = √(mean(w_a²) × Σ_k w_s(n − kH)²) = √(3/8 × 3/2),
    /// not the 1.5 that Σ_k w_a·w_s sums to. The phases are redrawn every frame, so the
    /// overlapping frames are incoherent and add in power, not in amplitude: dividing by 1.5
    /// would leave a steady material 6 dB down. The intent (spec §6.2) is that a steady material
    /// keeps its RMS within 1 dB, and 0.75 does (a test).
    static constexpr double kOverlapGain = 0.75;

    /// N: the power of two nearest kWindowSeconds × sampleRate (4096 at 44.1 and 48 kHz), at
    /// least 128 so that the hop N/4 is a multiple of 32.
    static uint32_t fftSizeFor(double sampleRate) noexcept;

    /// Allocates everything (not real-time safe). Throws std::invalid_argument unless sampleRate
    /// is finite and > 0, and std::bad_alloc if pffft can't make its setup or buffers.
    /// maxBlockSize 0 counts as 1.
    SpectralSource(double sampleRate, uint32_t maxBlockSize);
    ~SpectralSource();
    SpectralSource(SpectralSource &&) noexcept;
    SpectralSource &operator=(SpectralSource &&) noexcept;
    SpectralSource(const SpectralSource &) = delete;
    SpectralSource &operator=(const SpectralSource &) = delete;

    uint32_t fftSize() const noexcept { return fftSize_; }
    uint32_t hop() const noexcept { return fftSize_ / 4; }

    /// Ring time 0, frame 0, the accumulation cleared at once (no drain). Keeps the active setting.
    void reset() noexcept;

    /// Whether the layer's kind is Spectral, from the next process() call. Turning it off starts
    /// the drain (see the class comment); turning it on computes frames again from the next hop
    /// boundary, adding onto any drain still pending.
    void setActive(bool active) noexcept;
    bool active() const noexcept { return active_; }

    /// Writes numFrames samples at output clock blockStart into the ring: the smear while active,
    /// computing every frame whose hop boundary falls in the block; while inactive, what is still
    /// pending of the frames already computed, then silence.
    /// position is in the material's units: a fraction of the sample, or seconds behind the
    /// ring's head. Blocks must be contiguous from reset(), and numFrames at most maxBlockSize,
    /// as LiveSource::write requires (a longer block is written in pieces, so headAt() then
    /// covers its last piece alone). Real-time safe.
    void process(const SpectralMaterial &m, double position, uint64_t seed, uint32_t layer,
                 int64_t blockStart, uint32_t numFrames) noexcept SST_CPPUTILS_NONBLOCKING;

    const LiveSource &ring() const noexcept { return ring_; }

  private:
    struct SetupDeleter
    {
        void operator()(PFFFT_Setup *setup) const noexcept;
    };
    struct AlignedDeleter
    {
        void operator()(float *p) const noexcept;
    };
    using AlignedFloats = std::unique_ptr<float[], AlignedDeleter>;

    // Computes frame k (hop boundary kH) and adds it into the accumulation.
    void computeFrame(const SpectralMaterial &m, double position, uint64_t seed, uint32_t layer,
                      int64_t k) noexcept SST_CPPUTILS_NONBLOCKING;
    // Fill frame_ with channel c of the window starting at material frame start, times the
    // analysis Hann.
    void readSample(const SpectralMaterial &m, uint32_t c,
                    int64_t start) noexcept SST_CPPUTILS_NONBLOCKING;
    void readRing(const LiveSource &ring, uint32_t c, int64_t start,
                  int64_t oldest) noexcept SST_CPPUTILS_NONBLOCKING;
    // Transforms frame_ forward, draws its phases, transforms it back and adds it into
    // accumulator channel c (and channel 1 too when mono) from spectral time kH on.
    void smearInto(uint32_t c, bool mono, uint64_t seed, uint32_t layer,
                   int64_t k) noexcept SST_CPPUTILS_NONBLOCKING;

    double sampleRate_;
    uint32_t maxBlockSize_;
    uint32_t fftSize_;
    std::unique_ptr<PFFFT_Setup, SetupDeleter> setup_;
    AlignedFloats frame_;          // N: the windowed material, then the resynthesis (in place)
    AlignedFloats spectrum_;       // N: pffft's ordered spectrum
    AlignedFloats work_;           // N: pffft's work buffer, so nothing goes on the stack
    std::vector<float> analysis_;  // N: the analysis Hann
    std::vector<float> synthesis_; // N: the synthesis Hann × 1/(N × kOverlapGain)
    // Two channels of 2N slots: spectral time s is in slot s mod 2N. A slot is cleared when it is
    // read, and holds frames for [t − N, t + N) at output time t.
    std::vector<float> accumulation_[2];
    std::vector<float> out_[2]; // maxBlockSize: one block's output, before it goes into the ring
    LiveSource ring_;
    bool active_{false};
    // The output time from which the accumulation is all 0: kH + 2N for the last frame k
    // computed, 0 before any. Inactive from it on, process() writes silence without reading it.
    int64_t pendingUntil_{0};
};
} // namespace gg

#endif // GG_SPECTRAL_SPECTRALSOURCE_H
