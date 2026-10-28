// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_WINDOW_H
#define GG_CORE_WINDOW_H

#include <array>
#include <cstdint>

#include "gg/core/FpContract.h"

namespace gg
{
/// The Hann window w(p) = 0.5 - 0.5·cos(2πp), p in [0, 1], tabulated at kSize + 1 points and
/// read with linear interpolation. Two copies offset by half a period sum to one. The
/// constructor computes the table (with std::cos); at() is real-time safe.
class HannTable
{
  public:
    static constexpr uint32_t kSize = 4096;

    HannTable();

    /// The window at phase01, or 0 if phase01 is outside [0, 1] or NaN.
    float at(double phase01) const noexcept
    {
        GG_FP_CONTRACT_OFF
        if (!(phase01 >= 0.0 && phase01 <= 1.0))
        {
            return 0.0f;
        }
        const double x = phase01 * kSize; // exact: kSize is a power of two
        // phase01 == 1 is the end of the last segment.
        const uint32_t i = x < kSize ? static_cast<uint32_t>(x) : kSize - 1;
        const auto t = static_cast<float>(x - i);
        return table_[i] + (table_[i + 1] - table_[i]) * t;
    }

  private:
    std::array<float, kSize + 1> table_{};
};
} // namespace gg

#endif // GG_CORE_WINDOW_H
