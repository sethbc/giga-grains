// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/source/Mipmap.h"

#include <cstddef>

#include "gg/core/FpContract.h"

namespace gg
{
void decimateHalfband(const float *in, int64_t inFrames, int64_t inStride, float *out,
                      int64_t outFrames, int64_t outStride) noexcept
{
    GG_FP_CONTRACT_OFF
    constexpr int64_t kCentre = (kHalfbandTaps - 1) / 2;
    for (int64_t m = 0; m < outFrames; ++m)
    {
        const int64_t first = 2 * m - kCentre; // the input frame tap 0 reads
        float sum = 0.0f;
        // Every tap at an even offset from the centre but the centre itself is exactly zero, as
        // is every tap outside [0, inFrames); skipping them leaves the sum's bits as written in
        // Mipmap.h for finite input. Each product of finite floats with a zero is ±0, and adding
        // ±0 changes no sum that starts at +0 under round-to-nearest (it never becomes −0).
        for (int64_t k = 0; k < kHalfbandTaps; ++k)
        {
            const int64_t i = first + k;
            if (((k & 1) != 0 && k != kCentre) || i < 0 || i >= inFrames)
            {
                continue;
            }
            const float product = kHalfband[static_cast<std::size_t>(k)] * in[inStride * i];
            sum = sum + product;
        }
        out[outStride * m] = sum;
    }
}
} // namespace gg
