// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_SPACEREVERB_H
#define GG_CORE_SPACEREVERB_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <sst/cpputils/rtsan_support.h>

#include "gg/core/ParamMap.h"

namespace gg::core
{
/// Space's reverb (spec §6.3–§6.4): the patched Reverb 2 (core/src/space/Reverb2Patched.h), run
/// on the absolute 32-sample control grid, with Space's parameters mapped onto it and the idle
/// rule. The Processor feeds it one sample at a time.
///
/// **The grid.** Reverb 2 processes whole chunks of kChunk samples. Chunk k holds absolute
/// samples [32k, 32k + 32), counted from 0 at construction or reset(), whatever the host's
/// blocks. A chunk runs when its last sample arrives, and its output is returned during the
/// next chunk: the return for sample t is the reverb's output for t − 32, a constant 32 samples
/// late at every block size. Nothing is returned for chunk 0.
///
/// **Parameters** are latched at each chunk's first sample: the values passed with the others
/// are ignored. At that chunk's run, Reverb 2 gets (spec §6.3, the plan's global constraints):
/// - fixed: rev2_diffusion 1.0, rev2_buildup 1.0, rev2_lf_damping 0.2, rev2_mix 1.0, width 0 dB
///   (full stereo), rev2_predelay at its minimum (unused: the smoothed pre-delay replaces it);
/// - rev2_room_size = roomSize, rev2_hf_damping = hfDamping, rev2_modulation = modulation, each
///   clamped to its range ([−1, 1], [0, 1], [0, 1]);
/// - rev2_decay_time = decayLog2, clamped to Reverb 2's range [−4, 6] (finite Decay is within
///   it; at ∞ the value only sets Reverb 2's ring-out estimate);
/// - setInfinite(infinite);
/// - setPredelaySamples(max(0, predelaySeconds × sampleRate − 32)): the grid's 32 samples count
///   toward Pre-delay, so the return of an impulse is predelaySeconds late, at least 32 samples.
///
/// Reverb 2's mix and width are held at their targets from the start, so its output is wet only
/// and full stereo from the first chunk (upstream fades the mix in from 0 over a few dozen
/// chunks, which would leak the dry input into the return).
///
/// **The idle rule** (spec §6.3). The `active` flag at a chunk's first sample decides the chunk;
/// the Processor passes "the Amount ramp is not at exactly 0 here".
/// - An idle chunk doesn't run the reverb, and every sample of it returns +0.
/// - The first idle chunk after an active one begins clearing the reverb; each idle chunk zeroes
///   at most kClearBudgetFloats floats of it (ruling R-T5a), so an idle chunk's cost is bounded.
/// - The first active chunk after idle ones finishes whatever is left of the clear, then runs.
///   So raising Amount starts from an empty room: from there the output is exactly a fresh
///   instance's for the same input and parameters. Its own samples return +0 (nothing ran
///   before it).
///
/// It all depends on the chunk sequence alone, never on time, CPU or the signal.
///
/// **Memory.** Reverb 2 holds about 14.5 MB of fixed arrays, allocated by the constructor. A new
/// sample rate needs a new SpaceReverb (ruling R-T5b).
class SpaceReverb
{
  public:
    /// The chunk: the control grid, Processor::kControlGrid, and Reverb 2's block.
    static constexpr uint32_t kChunk = 32;

    /// The most floats an idle chunk zeroes of a clear (64 KiB).
    static constexpr size_t kClearBudgetFloats = 16384;

    /// Allocates the reverb (off the audio thread) for sampleRate (finite and > 0), empty and
    /// idle.
    explicit SpaceReverb(double sampleRate);
    ~SpaceReverb();
    SpaceReverb(const SpaceReverb &) = delete;
    SpaceReverb &operator=(const SpaceReverb &) = delete;

    /// Back to the constructed state: clears the whole reverb at once (a cost hosts expect of a
    /// reset), then time restarts at 0.
    void reset() noexcept;

    /// One sample at absolute time t, which starts at 0 after construction or reset() and
    /// increases by 1 per call. `params` are the values in effect at t; a chunk uses the values
    /// at its first sample. `active` is whether this sample belongs to an active chunk (the idle
    /// rule), read at the chunk's first sample. Writes the return for t: the reverb's output for
    /// t − 32, or +0 while idle.
    void processSample(int64_t t, float inL, float inR, const SpaceParams &params, bool active,
                       float &outL, float &outR) noexcept SST_CPPUTILS_NONBLOCKING
    {
        const auto i = static_cast<size_t>(t & (kChunk - 1));
        if (i == 0)
        {
            params_ = params;
            chunkActive_ = active;
        }
        // Slot i holds the last chunk's output for this position until this sample's input
        // replaces it: the chunk runs in place on these buffers.
        outL = chunkActive_ ? left_[i] : 0.0f;
        outR = chunkActive_ ? right_[i] : 0.0f;
        left_[i] = inL;
        right_[i] = inR;
        if (i == kChunk - 1)
            endChunk();
    }

    /// Whether a clear begun by idle chunks is still unfinished: the next active chunk would
    /// finish it. For tests.
    bool clearPending() const noexcept { return clearing_; }

  private:
    struct Reverb;

    /// Runs the chunk just completed (or, idle, steps the clear) and leaves its output in
    /// left_ and right_.
    void endChunk() noexcept SST_CPPUTILS_NONBLOCKING;

    std::unique_ptr<Reverb> reverb_;
    double sampleRate_;
    // The chunk being filled, and before each slot is filled, the last chunk's output.
    alignas(16) std::array<float, kChunk> left_{};
    alignas(16) std::array<float, kChunk> right_{};
    SpaceParams params_{}; // latched at the chunk's first sample
    bool chunkActive_{false};
    bool ran_{false};      // the reverb has run since it was last empty
    bool clearing_{false}; // a stepwise clear is under way
};
} // namespace gg::core

#endif // GG_CORE_SPACEREVERB_H
