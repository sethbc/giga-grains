// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_KEYTUNING_H
#define GG_GRAIN_KEYTUNING_H

#include <array>
#include <cstddef>

namespace gg
{
/// The Cloud stream's key term for each MIDI key, in semitones from key 60 (M3 Tuning spec
/// §4.1): entry k is k - 60.0 exactly, so reading it is the same double as `note - 60` was.
/// A scheduler reads this table unless it is given another (CloudScheduler::setKeySemitones).
inline constexpr std::array<double, 128> kTwelveTetKeySemitones = [] {
    std::array<double, 128> t{};
    for (std::size_t k = 0; k < t.size(); ++k)
    {
        t[k] = static_cast<double>(k) - 60.0;
    }
    return t;
}();
} // namespace gg

#endif // GG_GRAIN_KEYTUNING_H
