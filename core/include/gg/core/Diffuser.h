// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_DIFFUSER_H
#define GG_CORE_DIFFUSER_H

#include <array>
#include <cstdint>
#include <vector>

#include <sst/cpputils/rtsan_support.h>

namespace gg::core
{
/// The per-layer diffuser (Space spec §6.2): four Schroeder allpasses in series per channel,
/// with different lengths on left and right, so it blurs grains into each other and widens them,
/// crossfaded with its input by an amount. A layer owns one; in M1 the Processor runs one on
/// layer A's grain sum.
///
/// **Output.** For an amount δ, each channel's sample w becomes
/// `δ > 0 ? w + δ·(diffuse(w) − w) : w`, so at δ = 0 the frame passes untouched, bit for bit
/// (the sign of zero included).
///
/// **The allpasses run every call,** whatever the amount, so their state is continuous: raising
/// the amount from 0 resumes the diffusion of the signal already heard, not of silence.
///
/// **Each allpass** of length D and coefficient g = kDiffuserCoeff, with v its delay line:
/// `v[n] = x[n] + g·v[n−D]`, `y[n] = v[n−D] − g·v[n]`. It passes every frequency at unit gain,
/// so diffusion changes the energy only by what is still in the delay lines.
///
/// **Determinism.** Plain float arithmetic per sample, no libm, no RNG, no block quantum: the
/// output is the same however the calls are split into blocks.
class Diffuser
{
  public:
    static constexpr float kDiffuserCoeff = 0.625f;
    static constexpr int kStages = 4; ///< Allpasses per channel.

    /// The lengths in milliseconds, left then right (spec §6.2, voiced in the plan).
    static constexpr std::array<double, 2 * kStages> kLengthsMs{4.1, 5.9, 8.3, 13.7,
                                                                4.7, 6.7, 7.9, 12.3};

    /// Sizes the delay lines for sampleRate (finite and > 0) and clears them. Allocates: call it
    /// off the audio thread. Each length is llround(ms × sampleRate / 1000) raised to the next
    /// prime (itself, if prime), then to the next prime again while it equals an earlier one, in
    /// kLengthsMs order, so all eight are distinct primes.
    void prepare(double sampleRate);

    /// Clears the delay lines.
    void reset() noexcept;

    /// Diffuses one frame in place by amount (0–1). prepare() must have been called.
    void process(float &l, float &r, float amount) noexcept SST_CPPUTILS_NONBLOCKING
    {
        float dl = l, dr = r;
        for (int s = 0; s < kStages; ++s)
        {
            dl = stages_[s].run(dl);
            dr = stages_[kStages + s].run(dr);
        }
        if (amount > 0.0f)
        {
            l = l + amount * (dl - l);
            r = r + amount * (dr - r);
        }
    }

    /// The eight lengths in samples, left four then right four, in kLengthsMs order. For tests.
    std::array<uint32_t, 2 * kStages> lengths() const noexcept;

  private:
    struct Allpass
    {
        std::vector<float> line; ///< v, as a ring of D samples.
        uint32_t pos{0};         ///< Where v[n−D] is read and v[n] is written.

        float run(float x) noexcept SST_CPPUTILS_NONBLOCKING
        {
            const float delayed = line[pos];
            const float v = x + kDiffuserCoeff * delayed;
            line[pos] = v;
            if (++pos == line.size())
                pos = 0;
            return delayed - kDiffuserCoeff * v;
        }
    };

    std::array<Allpass, 2 * kStages> stages_{};
};
} // namespace gg::core

#endif // GG_CORE_DIFFUSER_H
