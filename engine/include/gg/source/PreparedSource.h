// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_SOURCE_PREPAREDSOURCE_H
#define GG_SOURCE_PREPAREDSOURCE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "gg/source/InterleavedStereo.h"
#include "gg/source/Mipmap.h"
#include "gg/source/SampleBuffer.h"

namespace gg
{
/// A sample source ready for the engine to read (spec §6.4): the planar buffer, for a stereo
/// (or wider) buffer the interleaved copy of its first two channels that the SIMD sample kernel
/// reads (InterleavedStereo), and the mip pyramid (spec §6.3, Mipmap.h). make() does all the work
/// that isn't real-time safe, off the audio thread, so Engine::adoptSource only has to swap
/// pointers.
///
/// Immutable once made. The buffer's samples must not change afterwards either: the SIMD
/// interior would read the copy's old samples and the reference the new ones, mixing both within
/// a grain and silently breaking the kernels' bit identity, and the levels would no longer be
/// the buffer's.
class PreparedSource
{
  public:
    /// Not real-time safe. Builds the interleaved copy for a buffer of two or more channels,
    /// which allocates 2 × frames floats, then the pyramid's levels 1 … mipLevelCount(frames) − 1,
    /// one copy per level (interleaved pairs for two or more channels, the planar channel for
    /// one), each from the level above: about one more copy of the source's first two channels
    /// in all. On std::bad_alloc for the pairs it keeps the planar buffer alone: the engine then
    /// reads the planar channels, the same output bits, only slower. On std::bad_alloc for a
    /// level it stops there, keeping the levels already built (levelCount() says how many).
    /// nullptr gives nullptr. Throws std::bad_alloc only if this object itself can't be
    /// allocated.
    ///
    /// `cancel`, if not null, is read (relaxed) on entry and before each level: once it
    /// is true, make() stops and returns nullptr, as for a null buffer, so a loader superseded
    /// while its source is preparing doesn't build the rest of the pyramid (about 1.75 s of
    /// worker time for a 10-minute stereo file). The caller discards a nullptr it asked for.
    static std::shared_ptr<const PreparedSource> make(std::shared_ptr<const SampleBuffer> buffer,
                                                      const std::atomic<bool> *cancel = nullptr);

    PreparedSource(const PreparedSource &) = delete;
    PreparedSource &operator=(const PreparedSource &) = delete;

    /// The planar buffer; real-time safe.
    const SampleBuffer &buffer() const noexcept { return *buffer_; }
    /// The interleaved copy of channels 0 and 1; empty (data() null) for a mono buffer, a buffer
    /// without channels, or when make() couldn't allocate it. Real-time safe.
    const InterleavedStereo &pairs() const noexcept { return pairs_; }

    /// One level of the source's pyramid, as the kernels read it: frame f of channel c is
    /// data[c][stride × f], for f in [0, frames), at `decimation` source frames per level frame.
    ///
    /// - Level 0 is the source itself: data the buffer's channels 0 and 1 (both channel 0 for a
    ///   mono buffer, both null with frames 0 for a buffer without channels), stride 1,
    ///   interleaved pairs().data() (null for mono, or when make() couldn't allocate it),
    ///   decimation 1.
    /// - Level n ≥ 1 is one copy, ceil(frames / 2ⁿ) frames with InterleavedStereo::kGuardFrames
    ///   zero frames before and after. A stereo source's is interleaved: data {lv, lv + 1},
    ///   stride 2, interleaved lv, mono false. A mono source's is planar: data {lv, lv},
    ///   stride 1, interleaved null, mono true. Decimation 2ⁿ.
    struct Level
    {
        const float *data[2]{nullptr, nullptr};
        int64_t frames{0};
        int64_t stride{1};
        const float *interleaved{nullptr}; // the pairs, when the SIMD kernel can read them
        bool mono{true};
        uint32_t decimation{1};
    };
    /// The levels make() built, 1 … kMaxMipLevels. Real-time safe.
    uint32_t levelCount() const noexcept { return levelCount_; }
    /// Level n, or level 0 when n >= levelCount(). Real-time safe.
    const Level &level(uint32_t n) const noexcept
    {
        return n < levelCount_ ? levels_[n] : levels_[0];
    }
    /// The bytes of every float this source holds: the buffer, the pairs and the levels, guard
    /// frames included. Real-time safe.
    std::size_t bytes() const noexcept;

  private:
    explicit PreparedSource(std::shared_ptr<const SampleBuffer> buffer) noexcept;
    /// Builds level n from level n − 1; throws std::bad_alloc and changes nothing if it can't.
    void buildLevel(uint32_t n);

    std::shared_ptr<const SampleBuffer> buffer_; // never null
    InterleavedStereo pairs_;
    std::array<Level, kMaxMipLevels> levels_{};
    // Level n's floats, n ≥ 1, guard frames included (index n − 1); level 0 reads buffer_ and
    // pairs_.
    std::array<std::vector<float>, kMaxMipLevels - 1> storage_{};
    uint32_t levelCount_{1};
};
} // namespace gg

#endif // GG_SOURCE_PREPAREDSOURCE_H
