// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_SOURCE_PREPAREDSOURCE_H
#define GG_SOURCE_PREPAREDSOURCE_H

#include <cstdint>
#include <memory>

#include "gg/source/InterleavedStereo.h"
#include "gg/source/SampleBuffer.h"

namespace gg
{
/// A sample source ready for the engine to read (spec §6.4): the planar buffer and, for a stereo
/// (or wider) buffer, the interleaved copy of its first two channels that the SIMD sample kernel
/// reads (InterleavedStereo). make() does all the work that isn't real-time safe, off the audio
/// thread, so Engine::adoptSource only has to swap pointers.
///
/// Immutable once made. The buffer's samples must not change afterwards either: the SIMD
/// interior would read the copy's old samples and the reference the new ones, mixing both within
/// a grain and silently breaking the kernels' bit identity.
class PreparedSource
{
  public:
    /// Not real-time safe. Builds the interleaved copy for a buffer of two or more channels,
    /// which allocates 2 × frames floats and doubles the source's memory. On std::bad_alloc it
    /// keeps the planar buffer alone: the engine then reads the planar channels, the same output
    /// bits, only slower. nullptr gives nullptr. Throws std::bad_alloc only if this object itself
    /// can't be allocated.
    static std::shared_ptr<const PreparedSource> make(std::shared_ptr<const SampleBuffer> buffer);

    PreparedSource(const PreparedSource &) = delete;
    PreparedSource &operator=(const PreparedSource &) = delete;

    /// The planar buffer; real-time safe.
    const SampleBuffer &buffer() const noexcept { return *buffer_; }
    /// The interleaved copy of channels 0 and 1; empty (data() null) for a mono buffer, a buffer
    /// without channels, or when make() couldn't allocate it. Real-time safe.
    const InterleavedStereo &pairs() const noexcept { return pairs_; }

    /// One level of the source's pyramid (spec §6.5): what the kernels read, the planar buffer
    /// (its frame count with it) and the interleaved pairs, at `decimation` source frames per
    /// level frame. Level 0 is the source itself, decimation 1: buffer() and pairs().
    struct Level
    {
        const SampleBuffer *buffer;
        const InterleavedStereo *pairs;
        uint32_t decimation;
    };
    /// The levels there are: 1 in wave 1 (level 0 alone; sub-project 3 fills the pyramid).
    uint32_t levelCount() const noexcept { return 1; }
    /// Level n, or level 0 when n >= levelCount(). Real-time safe.
    const Level &level(uint32_t n) const noexcept
    {
        static_cast<void>(n); // one level: every index reads level 0
        return level0_;
    }

  private:
    explicit PreparedSource(std::shared_ptr<const SampleBuffer> buffer) noexcept;

    std::shared_ptr<const SampleBuffer> buffer_; // never null
    InterleavedStereo pairs_;
    Level level0_; // buffer_ and pairs_, decimation 1
};
} // namespace gg

#endif // GG_SOURCE_PREPAREDSOURCE_H
