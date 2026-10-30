// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_SOURCE_MIPMAP_H
#define GG_SOURCE_MIPMAP_H

#include <array>
#include <cstdint>

/// The file pyramid's arithmetic (spec §6.3): how many levels a source of a given length gets,
/// how long each is, and the half-band filter that makes each level from the one above.
/// PreparedSource builds the levels with it, off the audio thread.
namespace gg
{
/// Levels 0 … 4, decimations 1, 2, 4, 8 and 16: enough for ±48 semitones.
inline constexpr uint32_t kMaxMipLevels = 5;
/// A level is built only while it would hold at least this many frames.
inline constexpr int64_t kMinLevelFrames = 64;
/// The half-band's length; the sum is centred on tap (kHalfbandTaps − 1) / 2 = 27.
inline constexpr int64_t kHalfbandTaps = 55;

/// The half-band lowpass, linear phase: a Kaiser-windowed half-band, 55 taps, β = 8.0
/// (ruling ES-T3a). With M = 27 and n = k − M for tap k = 0 … 54:
///
///     h[k] = float( sinc_half(n) × I0(β √(1 − (n / M)²)) / I0(β) ),
///     sinc_half(0) = 0.5, sinc_half(n) = sin(π n / 2) / (π n), so 0 for even n ≠ 0,
///
/// I0 the zeroth-order modified Bessel function of the first kind; not renormalized, so the
/// centre tap is exactly 0.5 (the DC gain is 1.0000035). Measured on these float taps (4,001
/// points per band): within 0.00085 dB of unity to 0.4 of the new rate (0.2 of the input rate)
/// and at least 80.2 dB down from 0.6 of it (0.3 of the input rate) to the input's Nyquist, the
/// pins of spec §6.3 (the ruling's 80.4 dB came from a coarser grid). A 31-tap Kaiser half-band
/// reached only 51 dB there, at its best β (4.5); 55 is the fewest taps that meets both pins.
/// The taps are literal so no platform's libm can move them; each is printed with 9 significant
/// digits, which round-trips a float.
inline constexpr std::array<float, kHalfbandTaps> kHalfband{
    -2.7573069e-05f, 0.0f, 0.000147917468f,  0.0f, -0.000431437424f, 0.0f, 0.000990431872f, 0.0f,
    -0.00197609304f, 0.0f, 0.0035842529f,    0.0f, -0.006066781f,    0.0f, 0.00975956675f,  0.0f,
    -0.0151532218f,  0.0f, 0.0230767131f,    0.0f, -0.0352200679f,   0.0f, 0.0559388883f,   0.0f,
    -0.101300932f,   0.0f, 0.316680104f,     0.5f, 0.316680104f,     0.0f, -0.101300932f,   0.0f,
    0.0559388883f,   0.0f, -0.0352200679f,   0.0f, 0.0230767131f,    0.0f, -0.0151532218f,  0.0f,
    0.00975956675f,  0.0f, -0.006066781f,    0.0f, 0.0035842529f,    0.0f, -0.00197609304f, 0.0f,
    0.000990431872f, 0.0f, -0.000431437424f, 0.0f, 0.000147917468f,  0.0f, -2.7573069e-05f};

/// Level n's frame count, ceil(frames / 2ⁿ), for frames ≥ 0 and n < 63.
inline constexpr int64_t mipLevelFrames(int64_t frames, uint32_t n) noexcept
{
    return (frames + (int64_t{1} << n) - 1) >> n;
}

/// The levels a source of `frames` frames gets: level 0, then each n in 1 … 4 whose
/// ceil(frames / 2ⁿ) is at least kMinLevelFrames.
inline constexpr uint32_t mipLevelCount(int64_t frames) noexcept
{
    uint32_t count = 1;
    while (count < kMaxMipLevels && mipLevelFrames(frames, count) >= kMinLevelFrames)
    {
        ++count;
    }
    return count;
}

/// One channel lowpassed by kHalfband and decimated by 2: for m in [0, outFrames),
///
///     out[outStride × m] = Σ_{k=0}^{54} kHalfband[k] × in[inStride × (2m + k − 27)],
///
/// accumulated in float from 0 in k order, taps outside [0, inFrames) zero. Allocates nothing;
/// PreparedSource::make calls it off the audio thread. `in` may be null when inFrames is 0.
void decimateHalfband(const float *in, int64_t inFrames, int64_t inStride, float *out,
                      int64_t outFrames, int64_t outStride) noexcept;
} // namespace gg

#endif // GG_SOURCE_MIPMAP_H
