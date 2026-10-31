// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_SOURCE_LIVESOURCE_H
#define GG_SOURCE_LIVESOURCE_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

#include <sst/cpputils/rtsan_support.h>

#include "gg/source/RingStorage.h"

namespace gg
{
/// The live ring source (spec §6.3), internal to the Engine: a stereo ring that the FX writes its
/// input into before any grain of the same block reads it.
///
/// **Ring time.** Frames are numbered from 0 at construction or reset(). R(t), the write head, is
/// the number of frames written through output sample t. At t the ring's window is
/// [R(t) − N, R(t) − 1]: the newest frame is R(t) − 1 and the oldest readable one is R(t) − N.
/// Not frozen, the head advances one frame per output sample. Indices below 0 (before the first
/// frame) read as silence, so the ring never needs clearing.
///
/// **Storage** (ruling M1-o). Frame i is kept in slot i mod C, with C = N + B physical slots,
/// where B is the largest block write() takes. A slot holds the frame's two channels side by
/// side, L then R (§16 lever 2), so a read's taps of both channels are one run of memory. A block's
/// input is written whole before its samples render, so when sample t of a block renders, the
/// frames R(t) .. R(t) + B − 2 may already be in the ring. With C = N + B they overwrite frames
/// R(t) − N − B .. R(t) − N − 2, all below the oldest frame t may read, R(t) − N. So every frame in
/// t's window is the one written for it, and what a sample reads depends neither on the host's
/// block size nor on B.
///
/// **Freeze.**
/// - On: the write gain ramps from 1 to 0 over L = freezeRampSamples() samples while the head
///   keeps advancing (the L-th frame is written with gain 0), then the head stops.
/// - Off: the head resumes at once and the gain ramps back to 1 over L samples.
/// - A toggle during a ramp continues from the current gain.
///
/// The gain is ramp / L for an integer ramp in [0, L], so it is exact, and it depends only on
/// which samples were frozen, never on where blocks fall.
///
/// **Input.** Non-finite samples are written as 0, and finite ones are clamped to
/// ±kMaxInputMagnitude. A mono input fills both channels; a missing input, or a missing channel
/// of a stereo one, is silence.
///
/// **The window** (M2 sub-project 2's spec §6.1). What a sample may read is [E − W, E − 1]:
/// - with no splice selected, E = R(t) and W = N, or min(L, N) while Loop is on with length L
///   (setLoop), so a loop is the newest L frames;
/// - while splice k is selected (selectSplice), the span from the k-th mark still in storage at
///   R(t), oldest first, to the next (the last mark's to the head): E is the later mark, or R(t)
///   for the last splice, and E − W the earlier. A k past the marks in storage is the last
///   splice; with none in storage the window is the first case's.
/// Which marks are in storage is decided per sample, from R(t) alone, so the window never depends
/// on where blocks fall. The window's oldest frame is never below R(t) − N, so every frame in it
/// is the one written for it (see Storage). At the defaults (Loop off, no splice) it is M1's
/// [R(t) − N, R(t) − 1], bit for bit. The settings and the marks apply from the next write(),
/// which captures them for its block.
///
/// **Overdub.** write() may take a gain o per frame: the frame written at R is then
/// sanitize(sanitize(in) + o × frame(R − P)) before the freeze gain, with the period P = min(L, N)
/// while Loop is on, else N. Frame R − P is still in storage when R is written (the slots hold
/// N + B frames), and a frame before 0 is silence. Where o is 0, or no gains are given, the frame
/// is the input alone, by M1's path, so Overdub 0 keeps every bit (the sign of zero included).
///
/// **Splice marks:** at most kMaxSplices ring-time positions, ascending, in a fixed array. A
/// mark m has left storage once R − N > m, and from that sample on it no longer counts (D6: the
/// splices' numbering shifts as marks age out); write() then drops it.
///
/// **Storage** lives in a RingStorage: the one prepare() allocates, which the source owns, or one
/// the caller owns, swapped in by adoptStorage() (a resize, ring time going on) or
/// restoreStorage() (a restore, ring time restarting).
///
/// **Reads** never touch a frame outside the window; see fade() and read().
class LiveSource
{
  public:
    /// M: the head-proximity margin and fade length, in frames (spec §6.3; INTENT §14's
    /// 256-sample head-crossing crossfade).
    static constexpr int64_t kHeadMargin = 256;
    /// The freeze write-gain ramp, in seconds; freezeRampSamples() rounds it to samples.
    static constexpr double kFreezeRampSeconds = 0.02;
    /// Finite input beyond ± this (2^20, about +120 dBFS) is clamped. It is far above any real
    /// signal, and low enough that no sum of grains can overflow a float.
    static constexpr float kMaxInputMagnitude = 1048576.0f;

    /// The most frames adoptStorage() copies itself (spec §6.5): the newest 64 KB per channel.
    static constexpr int64_t kAdoptCopyFrames = 8192;
    /// The most splice marks a ring keeps (spec §6.1).
    static constexpr uint32_t kMaxSplices = 32;

    /// L = round(kFreezeRampSeconds × sampleRate) samples, at least 1.
    static int64_t freezeRampSamples(double sampleRate) noexcept;

    /// A ring with no storage yet: the rate (for the freeze ramp) and the largest block write()
    /// takes, which a storage must leave room for (capacity() − frames() >= maxBlockSize; 0 counts
    /// as 1). Not enabled() until adoptStorage() or restoreStorage() gives it storage. Lets go of
    /// any storage it had: one it owned is freed, and one the caller gave stays the caller's,
    /// without being handed back. Resets (see reset()). Not real-time safe: it may free.
    void prepare(double sampleRate, uint32_t maxBlockSize) noexcept;

    /// prepare(sampleRate, maxBlockSize), then a RingStorage the source owns, whose window is
    /// N = ceil(seconds × sampleRate) frames (no ring when seconds is 0), stored in
    /// N + maxBlockSize slots (see the class comment). For the callers that size the ring once (the
    /// Engine's constructor, the tests, gg-bench and gg-render's sample mode). Not real-time safe.
    /// sampleRate must be finite and > 0. Throws std::invalid_argument, changing nothing, when
    /// seconds is not finite and >= 0, or when the storage would exceed 2^40 frames.
    void prepare(double sampleRate, double seconds, uint32_t maxBlockSize);

    /// The ring exchange's audio-thread half (spec §6.5), for a resize: swaps in `next`, whose
    /// slots already hold the window's frames below `copiedTo` at their slots (frame f at f mod
    /// next->capacity()), copied by the main thread from the frames it found stable. Copies
    /// [copiedTo, head()) itself, the frames written since, as far as both windows reach, and
    /// refreshes next's guard slots. Ring time continues: the head, the freeze state and the write
    /// gain stay. `previous` gets the storage replaced, for the main thread to free: nullptr when
    /// the source owned it (it keeps that one until the next prepare() or its destruction, so
    /// nothing is freed here) or had none. Marks older than the new window are dropped (as
    /// write() drops them). The loop's W follows the new N.
    ///
    /// Returns false, changing nothing (previous is set to nullptr), when next is null or already
    /// this ring's, when its capacity leaves less than a block's slack past its frames, when
    /// copiedTo is past the head, or when the copy would be more than kAdoptCopyFrames frames:
    /// the main thread then copies again from the current head. Real-time safe, with the
    /// threading rule of the setters: on the audio thread between write() calls.
    bool adoptStorage(RingStorage *next, int64_t copiedTo,
                      RingStorage *&previous) noexcept SST_CPPUTILS_NONBLOCKING;

    /// A restore (spec §6.6): `next` holds frames [0, frames) at their slots. Ring time restarts
    /// with the head at `frames`, and `marks` replace the current ones: those ascending, in the
    /// new window and below `frames`. Not frozen, the write gain is 1. Frozen, the ring is held
    /// from the first sample, as the saved session rendered it: the freeze on, the write gain 0
    /// and the head stopped, so no ramp moves it (keep the freeze setting on after it). `previous`
    /// gets the storage replaced, as adoptStorage() hands it back. Refreshes next's guard slots;
    /// copies no frame. Until the next write(), R(t) is the restored head at every output sample
    /// t, and the window is the restored ring's (the last write's block is forgotten with the old
    /// ring time).
    ///
    /// Returns false, changing nothing (previous is set to nullptr), when next is null or already
    /// this ring's, when its capacity leaves less than a block's slack, when frames is below 0 or
    /// past next->capacity(), or when there are more than kMaxSplices marks. Real-time safe, with
    /// the threading rule of the setters.
    bool restoreStorage(RingStorage *next, int64_t frames, std::span<const int64_t> marks,
                        bool frozen, RingStorage *&previous) noexcept SST_CPPUTILS_NONBLOCKING;

    /// N, the ring length in frames; 0 when there is no ring.
    int64_t frames() const noexcept { return frames_; }
    /// The storage in effect (prepare()'s own, or the one last adopted or restored), or nullptr
    /// without a ring: for a main thread's copy of the window (spec §6.4), whose slots are laid out
    /// as RingStorage documents.
    const RingStorage *storage() const noexcept { return storage_; }
    bool enabled() const noexcept { return frames_ > 0; }

    /// Restarts ring time at 0: the head at 0, not frozen, write gain 1. The old frames stay in
    /// memory, but no read can reach them again.
    void reset() noexcept;

    /// The freeze setting for the next write() and after.
    void setFrozen(bool frozen) noexcept { frozen_ = frozen; }

    // The window and the marks (spec §6.1). Each setter is real-time safe and takes effect from
    // the next write(); call them on the audio thread between write() calls.

    /// Loop on: the window is the newest min(lengthFrames, N) frames, and overdub's period the
    /// same; off, both are N. A length below 1 counts as 1.
    void setLoop(bool on, int64_t lengthFrames) noexcept
    {
        loopOn_ = on;
        loopLength_ = lengthFrames;
    }

    /// Selects splice k (D6): 0 none (the whole window, the loop's if Loop is on), k >= 1 the
    /// span from the k-th mark in storage, oldest first, to the next or, for the last mark, to
    /// the head. The selection is an index, kept as given: as marks age out the numbering shifts
    /// under it, a k past the marks in storage selects the last splice, and with no mark in
    /// storage the window is the whole one.
    void selectSplice(uint32_t k) noexcept { spliceRequest_ = k; }

    /// Adds a mark at ring time `ringTime`, keeping them ascending. False, changing nothing, when
    /// kMaxSplices are kept, a mark is there already, or ringTime is outside [head − N, head] or
    /// below 0.
    bool addSplice(int64_t ringTime) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Removes every mark; with none, a selection is the whole window from the next write().
    void clearSplices() noexcept { markCount_ = 0; }

    /// How many marks there are: those still in storage as of the last write() (and those added
    /// since), for a splice count shown to the user.
    uint32_t spliceCount() const noexcept { return markCount_; }
    /// The marks spliceCount() counts, ascending.
    std::span<const int64_t> splices() const noexcept { return {marks_.data(), markCount_}; }

    /// Writes the input for output samples [blockStart, blockStart + numFrames), where
    /// in[c] + offset is channel c's first sample, and records R(t) for those samples (headAt()
    /// and headRunAfter()), the window for the block (anchorAt(), windowFrames()) and the marks
    /// still in storage. Blocks must be contiguous from reset(), and numFrames at most the
    /// maxBlockSize given to prepare(): a longer block would overwrite frames its own earlier
    /// samples read. Does nothing but record without storage.
    ///
    /// overdubGain + offset, when not null, is the first frame's overdub gain, one per frame
    /// (see Overdub in the class comment); nullptr is 0 throughout, M1's write bit for bit.
    void write(const float *const *in, uint32_t numInChannels, uint32_t offset, int64_t blockStart,
               uint32_t numFrames,
               const float *overdubGain = nullptr) noexcept SST_CPPUTILS_NONBLOCKING;

    /// R after the last write(): the frames written since reset().
    int64_t head() const noexcept { return head_; }

    /// R(t), for an output sample t of the last write() block. The head advances on a prefix of
    /// the block (all of it unless a freeze stops it), so R(t) = R(blockStart − 1) +
    /// min(t − blockStart + 1, frames written in the block).
    int64_t headAt(int64_t t) const noexcept
    {
        return blockHead_ + std::min(t - blockStart_ + 1, blockAdvance_);
    }

    /// The window's end E at output sample t of the last write() block: headAt(t), or while a
    /// splice is in effect its later mark (at most the head), or headAt(t) for the last splice.
    /// Position and Spray read seconds behind it (Engine's admission clamp).
    int64_t anchorAt(int64_t t) const noexcept { return windowEnd(headAt(t)); }

    /// The window's oldest frame E − W at output sample t of the last write() block: never below
    /// R(t) − N, and R(t) − N itself with Loop off and no splice.
    int64_t windowOldestAt(int64_t t) const noexcept { return windowOldest(headAt(t)); }

    /// headRunAfter(t), or 0 while a splice is in effect at t: a splice's window doesn't move
    /// with the head (the last splice's end does, which admission then treats as still,
    /// conservatively).
    double anchorRunAfter(int64_t t) const noexcept
    {
        int64_t start = 0;
        int64_t end = 0;
        return spliceAt(headAt(t), start, end) ? 0.0 : headRunAfter(t);
    }

    /// The first output sample after t of the last write() block from which the window's ends
    /// may follow other lines in t, or INT64_MAX: the sample where a mark leaves storage while a
    /// splice is selected, shifting the splices' numbering. Before it, from t on, anchorAt() and
    /// windowOldestAt() each stay constant or move with the head, so the SIMD live kernel's runs
    /// (kernels::detail::liveRun) end there.
    int64_t windowSteadyUntil(int64_t t) const noexcept SST_CPPUTILS_NONBLOCKING;

    /// W at output sample t of the last write() block: the window's length, anchorAt(t) less its
    /// oldest frame. N or the loop's length with no splice selected, a splice's span while one is.
    /// It varies within a block only for the last splice while the head advances, whose start
    /// stays at its mark as its end follows the head.
    int64_t windowFramesAt(int64_t t) const noexcept
    {
        const int64_t head = headAt(t);
        return windowEnd(head) - windowOldest(head);
    }

    /// W in effect at the end of the last write() block (a splice's span, the loop or N): the
    /// length of the window as of head(), which a capture reads.
    int64_t windowFrames() const noexcept { return windowEnd(head_) - windowOldest(head_); }

    /// The sample of the last write() block from which on the head stands still for the rest of
    /// the block: blockStart + the frames written in the block, which is the block's end unless
    /// a freeze stopped the head. Before it R(t) = R(blockStart − 1) + (t − blockStart + 1),
    /// rising by one per sample; from it on R(t) is the head(), so on each side R is linear in t.
    int64_t blockKink() const noexcept { return blockStart_ + blockAdvance_; }

    /// The ring's storage, for kernels that gather taps themselves: frame f >= 0 is in slot
    /// s = f mod capacity, whose channel c is slots[2s + c]. Slots 0 .. capacity + 14 are
    /// readable, with capacity .. capacity + 14 mirroring slots 0 .. 14, so taps starting at any
    /// slot s < capacity are contiguous for 16 frames. capacity is C = N + maxBlockSize and frames
    /// is N (both 0, with null slots, when there is no ring). Only a frame inside a sample's
    /// window [R(t) − N, R(t) − 1] is the one written for it (see Storage).
    struct RingView
    {
        const float *slots;
        int64_t capacity;
        int64_t frames;
    };
    RingView view() const noexcept { return {slots_, capacity_, frames_}; }

    /// One level of the ring's pyramid (spec §6.5): the ring view the kernels read, at
    /// `decimation` ring frames per level frame. Level 0 is the ring itself, decimation 1.
    struct Level
    {
        RingView view;
        uint32_t decimation;
    };
    /// The levels there are: 1 in wave 1 (level 0 alone; sub-project 2 builds the ring's levels).
    uint32_t levelCount() const noexcept { return 1; }
    /// Level n, or level 0 when n >= levelCount(). Real-time safe.
    Level level(uint32_t n) const noexcept
    {
        static_cast<void>(n); // one level: every index reads level 0
        return {view(), 1};
    }

    /// How many more frames the head will advance after output sample t of the last write()
    /// block, if the freeze setting doesn't change: +infinity when not frozen; when frozen, the
    /// frames left in the ramp, 0 once the head has stopped.
    double headRunAfter(int64_t t) const noexcept
    {
        if (!frozen_)
        {
            return std::numeric_limits<double>::infinity();
        }
        return static_cast<double>(blockRamp_ - std::min(t - blockStart_ + 1, blockRamp_));
    }

    /// The head-proximity fade for a read at pos (ring time) while the head is at R (spec §6.3),
    /// over the window [E − W, E − 1] of the last write() block (E and E − W from R; see The
    /// window in the class comment):
    ///
    ///     g = clamp((E − 1 − (pos + 2)) / M, 0, 1) × clamp(((pos − 1) − (E − W)) / M, 0, 1)
    ///
    /// With no splice and no loop, E = R and W = N: M1's fade, bit for bit. It is 1 while every
    /// tap is at least M frames inside both ends of the window, and falls linearly to 0 over M
    /// frames as a tap nears either end: the newest frame, or the oldest one, which the next frame
    /// written moves out of the window. (The slot it overwrites holds frame R − N − B, already
    /// outside the window; see Storage.) Frozen, those two ends are the seam where the ring's
    /// newest and oldest frames meet, so this is the head-crossing crossfade; a loop's or a
    /// splice's two ends are crossfaded the same way, and a grain admitted under a wider window
    /// that now reads outside a narrower one is taken out by it.
    double fade(double pos, int64_t head) const noexcept { return fadeFor<1, 2>(pos, head); }

    /// fade() for a read whose taps reach kBefore frames before floor(pos) and kAfter after it:
    ///
    ///     g = clamp((E − 1 − (pos + kAfter)) / M, 0, 1)
    ///       × clamp(((pos − kBefore) − (E − W)) / M, 0, 1)
    ///
    /// fade() is fadeFor<1, 2>, the Hermite's reach; the HQ tier's sinc uses fadeFor<7, 8>
    /// (Sinc::fade in GrainKernels.h), so it too fades out before a tap leaves the window.
    template <int64_t kBefore, int64_t kAfter>
    double fadeFor(double pos, int64_t head) const noexcept
    {
        const auto end = static_cast<double>(windowEnd(head));
        const double toHead = (end - 1.0 - (pos + static_cast<double>(kAfter))) / kMargin;
        const double fromTail =
            ((pos - static_cast<double>(kBefore)) - static_cast<double>(windowOldest(head))) /
            kMargin;
        return unit(toHead) * unit(fromTail);
    }

    /// Where the last read() or readLinear() was, so that reads moving forward find their slot
    /// without a division. A fresh Cursor works for any read.
    struct Cursor
    {
        int64_t index{0}; ///< the ring-time index of the last read's first tap
        int64_t slot{0};  ///< that index's slot
        bool valid{false};
    };

    /// Reads both channels at pos (ring time) with interp::catmullRom, the 4-point Hermite of
    /// interp::hermite4, over taps floor(pos) − 1 .. floor(pos) + 2. Returns false and reads
    /// nothing unless every tap is in the window [E − W, E − 1] at head, where head is R(t) of a
    /// sample t of the last write() block (see fade()); every frame there is the one written for
    /// it (see Storage in the class comment). Taps before frame 0 read as 0.
    bool read(double pos, int64_t head, Cursor &cursor, float &left,
              float &right) const noexcept SST_CPPUTILS_NONBLOCKING;

    /// The Eco tier's read: both channels at pos with interp::lerp, the line of interp::linear,
    /// over taps floor(pos) and floor(pos) + 1. Returns false and reads nothing unless both taps
    /// are in the window [E − W, E − 1] at head, as read() requires of its four. Taps before frame
    /// 0 read as 0. The check is for this read's own reach; the renderer's fade() stays the
    /// one for the Hermite's reach, floor(pos) − 1 .. floor(pos) + 2, which is a frame wider at
    /// each end, so for a linear read it is conservative: wherever fade() is above 0, both taps
    /// are in the window.
    bool readLinear(double pos, int64_t head, Cursor &cursor, float &left,
                    float &right) const noexcept SST_CPPUTILS_NONBLOCKING;

    /// The raw taps of a kTaps-point read at pos (kTaps 2, 4 or 16): taps floor(pos) − kBefore ..
    /// floor(pos) − kBefore + kTaps − 1, kBefore = kTaps / 2 − 1 (0, 1 or 7), written in order to
    /// left[0 .. kTaps) and right[0 .. kTaps). Returns false and writes nothing unless every tap
    /// is in the window [head − N, head − 1], as read() requires of its four; taps before frame 0
    /// read as 0. read() and readLinear() are gatherTaps<4> and <2> with their curves applied.
    template <int kTaps>
    bool gatherTaps(double pos, int64_t head, Cursor &cursor, float *left,
                    float *right) const noexcept SST_CPPUTILS_NONBLOCKING;

  private:
    static constexpr double kMargin = static_cast<double>(kHeadMargin);
    // A read's kTaps taps read slots s .. s + kTaps − 1, at most 16 (the sinc's). Slots
    // C .. C + 14 mirror slots 0 .. 14, so the taps are contiguous wherever s falls.
    static constexpr int64_t kGuardFrames = 15;

    // The slot of ring-time frame first >= 0, found from the cursor when reads move forward, and
    // recorded in it.
    int64_t slotOf(int64_t first, Cursor &cursor) const noexcept SST_CPPUTILS_NONBLOCKING;

    // read() (kTaps 4: catmullRom over taps floor(pos) − 1 .. floor(pos) + 2) and readLinear()
    // (kTaps 2: lerp over floor(pos) and floor(pos) + 1), which differ only in their reach and
    // their curve. Defined in LiveSource.cpp, beside them.
    template <int kTaps>
    bool readTaps(double pos, int64_t head, Cursor &cursor, float &left,
                  float &right) const noexcept SST_CPPUTILS_NONBLOCKING;

    // clamp(x, 0, 1), with NaN read as 0.
    static double unit(double x) noexcept { return x > 0.0 ? (x < 1.0 ? x : 1.0) : 0.0; }

    // The splice in effect at head R(t) for the last write() block: false when none is (no
    // selection, or no mark of the block in storage at R(t)), else its first mark and its later
    // one (INT64_MAX for the last splice, which runs to the head). A mark m is in storage while
    // m >= R(t) − N, so its first is too; the marks that left are a prefix (they are ascending),
    // all but the few that leave during the block already dropped at write().
    bool spliceAt(int64_t head, int64_t &start, int64_t &end) const noexcept
    {
        if (blockRequest_ == 0)
        {
            return false;
        }
        const uint32_t first = firstInStorage(head);
        const uint32_t kept = blockMarkCount_ - first;
        if (kept == 0)
        {
            return false;
        }
        const uint32_t k = std::min(blockRequest_, kept);
        start = blockMarks_[first + k - 1];
        end = k < kept ? blockMarks_[first + k] : std::numeric_limits<int64_t>::max();
        return true;
    }
    // The first of the block's marks still in storage at head R(t).
    uint32_t firstInStorage(int64_t head) const noexcept
    {
        uint32_t first = 0;
        while (first < blockMarkCount_ && blockMarks_[first] < head - frames_)
        {
            ++first;
        }
        return first;
    }

    // The last write() block's window at head R(t): its end E, and its oldest frame E − W, never
    // below R(t) − N.
    int64_t windowEnd(int64_t head) const noexcept
    {
        int64_t start = 0;
        int64_t end = 0;
        return spliceAt(head, start, end) ? std::min(head, end) : head;
    }
    int64_t windowOldest(int64_t head) const noexcept
    {
        int64_t start = 0;
        int64_t end = 0;
        return spliceAt(head, start, end) ? start : head - window_;
    }

    // Points the source at `storage` (nullptr: none), for prepare(), adoptStorage() and
    // restoreStorage(); the head and the write slot are the caller's.
    void attach(RingStorage *storage) noexcept;
    // Hands the current storage back through `previous`: nullptr when the source owns it (then
    // kept in retired_ until prepare() or destruction, so nothing is freed on the audio thread)
    // or has none.
    void release(RingStorage *&previous) noexcept;
    // W without a splice, from the loop setting and N.
    int64_t loopWindow() const noexcept;
    // Drops the marks before `oldest`: they have left storage.
    void dropMarksBefore(int64_t oldest) noexcept SST_CPPUTILS_NONBLOCKING;
    // Captures the selection and the marks for the block about to be written (or a restore's).
    void captureWindow() noexcept;

    RingStorage *storage_{nullptr};        // the slots' storage, owned_ or the caller's
    std::unique_ptr<RingStorage> owned_;   // the storage prepare(rate, seconds, block) allocated
    std::unique_ptr<RingStorage> retired_; // owned_ once replaced, freed by the next prepare()
    float *slots_{nullptr};                // C + kGuardFrames slots of 2 floats: s's c at 2s + c
    int64_t frames_{0};                    // N, the storage's window
    int64_t capacity_{0};                  // C = N + maxBlockSize slots (ruling M1-o)
    int64_t maxBlock_{1};                  // B, the largest block write() takes
    int64_t rampLength_{1};                // L

    // The window settings (setLoop, selectSplice) and the marks.
    bool loopOn_{false};
    int64_t loopLength_{0};
    uint32_t spliceRequest_{0};
    std::array<int64_t, kMaxSplices> marks_{};
    uint32_t markCount_{0};

    bool frozen_{false};
    int64_t ramp_{1};      // the write gain is ramp_ / rampLength_, ramp_ in [0, rampLength_]
    int64_t head_{0};      // R
    int64_t writeSlot_{0}; // head_ mod C

    // The last write() block.
    int64_t blockStart_{0};
    int64_t blockHead_{0};     // R before the block
    int64_t blockAdvance_{0};  // frames written in the block, on its first blockAdvance_ samples
    int64_t blockRamp_{0};     // ramp_ before the block
    int64_t window_{0};        // W with no splice (the loop's or N), and overdub's period P
    uint32_t blockRequest_{0}; // the splice selected for the block, 0 for none
    std::array<int64_t, kMaxSplices> blockMarks_{}; // the marks as the block began
    uint32_t blockMarkCount_{0};
};
} // namespace gg

#endif // GG_SOURCE_LIVESOURCE_H
