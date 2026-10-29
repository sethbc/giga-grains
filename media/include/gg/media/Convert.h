// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_CONVERT_H
#define GG_MEDIA_CONVERT_H

#include <memory>

#include "gg/source/SampleBuffer.h"

/// Sample-rate conversion on load (spec §6.4), with the pinned r8brain-free-src. Not real-time
/// safe: it allocates the whole output and r8brain's filters, so it runs on a loader thread or,
/// when a session is restored or the plugin activated, the main thread.
namespace gg::media
{
/// `in` at `targetRate`.
/// - When in->sampleRate() == targetRate, returns `in` itself, the same pointer.
/// - Otherwise a new buffer at `targetRate`, with in's channel count and exactly
///   llround(double(in->frames()) × targetRate / in->sampleRate()) frames per channel, but at
///   least 1 when `in` has any (0 when it has none). Each
///   channel is converted on its own with r8b::CDSPResampler24 (linear phase), its latency
///   compensated, so frame 0 of the output is aligned with frame 0 of the input. The audio
///   before frame 0 and after the last frame is taken as silence.
/// - Deterministic: the same input and rate give bit-identical output.
/// - Bounded memory: besides the output, r8brain's buffers are sized for about 65,536 output
///   frames per call (or one input frame's worth, when the rate rises more than 65,536-fold),
///   however far the rate rises.
/// `in` must not be null, and `targetRate` must be finite and positive. Throws std::bad_alloc on
/// allocation failure, and std::system_error if r8brain's filter-cache std::mutex fails to lock
/// (which the standard allows and no supported platform does in practice); nothing else.
std::shared_ptr<SampleBuffer> convertRate(std::shared_ptr<SampleBuffer> in, double targetRate);
} // namespace gg::media

#endif // GG_MEDIA_CONVERT_H
