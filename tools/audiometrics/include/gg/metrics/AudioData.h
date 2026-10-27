// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_METRICS_AUDIODATA_H
#define GG_METRICS_AUDIODATA_H

#include <vector>

namespace gg::metrics
{
/// Deinterleaved audio: one vector of samples per channel. Channels are expected to have the
/// same length; metrics that use the mono mix read only the shortest channel's length.
struct AudioData
{
    double sampleRate{0.0};
    std::vector<std::vector<float>> channels;
};
} // namespace gg::metrics

#endif // GG_METRICS_AUDIODATA_H
