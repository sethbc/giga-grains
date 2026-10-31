// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/source/LiveSource.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>

#include "gg/core/Interpolate.h"

namespace gg
{
namespace
{
// RingStorage's bound on a ring, as a double for the checks below.
constexpr auto kMaxRingFramesD = static_cast<double>(kMaxRingFrames);

// Non-finite input is stored as 0 (Review Focus 2); finite input is clamped.
float sanitize(float x) noexcept
{
    if (!std::isfinite(x))
    {
        return 0.0f;
    }
    return std::clamp(x, -LiveSource::kMaxInputMagnitude, LiveSource::kMaxInputMagnitude);
}

// Sets a storage's guard slots, C .. C + 14, to slots 0 .. 14, and each level's past its
// capacity likewise, as write() keeps them: a main thread's copy fills the slots alone.
void mirrorGuards(RingStorage &storage) noexcept
{
    for (uint32_t n = 0; n < storage.levelCount(); ++n)
    {
        float *const p = storage.levelSlots(n);
        const int64_t c = storage.levelCapacity(n);
        for (int64_t g = 0; g < RingStorage::kGuardFrames && g < c; ++g)
        {
            p[2 * (c + g)] = p[2 * g];
            p[2 * (c + g) + 1] = p[2 * g + 1];
        }
    }
}
} // namespace

int64_t LiveSource::freezeRampSamples(double sampleRate) noexcept
{
    // llround is exact; the min keeps its argument in range for any finite rate.
    const double samples = std::min(kFreezeRampSeconds * sampleRate, kMaxRingFramesD);
    return std::max<int64_t>(1, std::llround(samples));
}

void LiveSource::prepare(double sampleRate, uint32_t maxBlockSize) noexcept
{
    rampLength_ = freezeRampSamples(sampleRate);
    maxBlock_ = static_cast<int64_t>(std::max<uint32_t>(1, maxBlockSize));
    attach(nullptr);
    owned_.reset();
    retired_.reset();
    reset();
}

void LiveSource::prepare(double sampleRate, double seconds, uint32_t maxBlockSize)
{
    if (!(std::isfinite(seconds) && seconds >= 0.0))
    {
        throw std::invalid_argument("LiveSource: seconds must be finite and >= 0");
    }
    const double frames = std::ceil(seconds * sampleRate);
    const auto block = static_cast<double>(std::max<uint32_t>(1, maxBlockSize));
    if (!(frames + block <= kMaxRingFramesD))
    {
        throw std::invalid_argument("LiveSource: the ring would exceed 2^40 frames");
    }
    // Ruling M1-o: a whole block's worth of slots past the window, so the frames a block writes
    // after sample t never land in t's window (see Storage in LiveSource.h). Allocated before
    // anything changes, so a failure leaves the ring as it was.
    std::unique_ptr<RingStorage> storage;
    if (frames > 0.0)
    {
        storage = std::make_unique<RingStorage>(static_cast<int64_t>(frames), maxBlockSize);
    }
    prepare(sampleRate, maxBlockSize);
    owned_ = std::move(storage);
    attach(owned_.get());
    reset();
}

void LiveSource::attach(RingStorage *storage) noexcept
{
    static_assert(RingStorage::kGuardFrames == kGuardFrames);
    static_assert(RingStorage::kRingLevelLagLimit == kHeadMargin);
    storage_ = storage;
    slots_ = storage != nullptr ? storage->slots() : nullptr;
    frames_ = storage != nullptr ? storage->frames() : 0;
    capacity_ = storage != nullptr ? storage->capacity() : 0;
    // At most the array's size, which levelCount() never passes: bounded so GCC 13 can see it.
    levelCount_ = storage != nullptr ? std::min(storage->levelCount(), RingStorage::kMaxLevels) : 1;
    levels_ = {};
    levels_[0] = {slots_, capacity_, frames_};
    for (uint32_t n = 1; n < levelCount_; ++n)
    {
        levels_[n] = {storage->levelSlots(n), storage->levelCapacity(n),
                      mipLevelFrames(frames_, n)};
    }
}

void LiveSource::release(RingStorage *&previous) noexcept
{
    const bool owned = storage_ != nullptr && storage_ == owned_.get();
    previous = owned ? nullptr : storage_;
    if (owned)
    {
        // retired_ is empty here: owned_ is set only by prepare(), which empties retired_, and
        // leaves at most once after it. So the move frees nothing.
        retired_ = std::move(owned_);
    }
}

int64_t LiveSource::loopWindow() const noexcept
{
    return loopOn_ && frames_ > 0 ? std::clamp(loopLength_, int64_t{1}, frames_) : frames_;
}

void LiveSource::reset() noexcept
{
    frozen_ = false;
    ramp_ = rampLength_;
    head_ = 0;
    writeSlot_ = 0;
    blockStart_ = 0;
    blockHead_ = 0;
    blockAdvance_ = 0;
    blockRamp_ = ramp_;
    markCount_ = 0; // ring time restarts, so the old marks name nothing
    window_ = loopWindow();
    blockRequest_ = 0;
    blockMarkCount_ = 0;
}

bool LiveSource::addSplice(int64_t ringTime) noexcept
{
    if (frames_ == 0 || markCount_ == kMaxSplices || ringTime > head_ ||
        ringTime < std::max<int64_t>(0, head_ - frames_))
    {
        return false;
    }
    uint32_t at = 0;
    while (at < markCount_ && marks_[at] < ringTime)
    {
        ++at;
    }
    if (at < markCount_ && marks_[at] == ringTime)
    {
        return false; // a mark is there already
    }
    for (uint32_t i = markCount_; i > at; --i)
    {
        marks_[i] = marks_[i - 1];
    }
    marks_[at] = ringTime;
    ++markCount_;
    return true;
}

void LiveSource::dropMarksBefore(int64_t oldest) noexcept
{
    uint32_t dropped = 0;
    while (dropped < markCount_ && marks_[dropped] < oldest)
    {
        ++dropped;
    }
    if (dropped == 0)
    {
        return;
    }
    // The selection stays the caller's index (D6): the numbering shifts under it.
    for (uint32_t i = dropped; i < markCount_; ++i)
    {
        marks_[i - dropped] = marks_[i];
    }
    markCount_ -= dropped;
}

void LiveSource::captureWindow() noexcept
{
    blockRequest_ = spliceRequest_;
    blockMarkCount_ = markCount_;
    std::copy_n(marks_.begin(), markCount_, blockMarks_.begin());
}

int64_t LiveSource::windowSteadyUntil(int64_t t) const noexcept
{
    constexpr int64_t kNever = std::numeric_limits<int64_t>::max();
    const int64_t kink = blockKink();
    if (blockRequest_ == 0 || t >= kink)
    {
        return kNever; // no splice selected, or the head still: nothing leaves storage
    }
    const int64_t head = headAt(t);
    const uint32_t first = firstInStorage(head);
    if (first == blockMarkCount_)
    {
        return kNever; // no mark in storage, and none comes back
    }
    // The oldest mark m leaves once R > m + N; before the kink R rises by one per sample, to
    // head() at kink − 1.
    const int64_t leaves = blockMarks_[first] + frames_ + 1;
    const int64_t at = t + (leaves - head);
    return at < kink ? at : kNever;
}

bool LiveSource::adoptStorage(RingStorage *next, int64_t copiedTo, RingStorage *&previous) noexcept
{
    previous = nullptr;
    if (next == nullptr || next == storage_ || next->capacity() - next->frames() < maxBlock_ ||
        copiedTo > head_ || head_ - copiedTo > kAdoptCopyFrames)
    {
        return false;
    }
    // The frames written since copiedTo, as far as both windows reach: older ones have left the
    // old storage or are outside the new window. At most kAdoptCopyFrames.
    float *const dst = next->slots();
    const int64_t nextCapacity = next->capacity();
    const int64_t from = std::max({copiedTo, head_ - next->frames(), head_ - frames_, int64_t{0}});
    if (from < head_)
    {
        int64_t s = from % capacity_;
        int64_t d = from % nextCapacity;
        for (int64_t f = from; f < head_; ++f)
        {
            dst[2 * d] = slots_[2 * s];
            dst[2 * d + 1] = slots_[2 * s + 1];
            if (++s == capacity_)
            {
                s = 0;
            }
            if (++d == nextCapacity)
            {
                d = 0;
            }
        }
        // Their level frames, copied from the old storage, where write() made them complete
        // (spec §6.9): the frames fillLevels would make, at the cost of a copy.
        copyLevels(*storage_, *next, from, head_);
    }
    mirrorGuards(*next);
    release(previous);
    attach(next);
    writeSlot_ = head_ % capacity_;
    window_ = loopWindow();
    // The block's captured marks are filtered by the new N as they are read (spliceAt).
    dropMarksBefore(head_ - frames_);
    return true;
}

bool LiveSource::restoreStorage(RingStorage *next, int64_t frames, std::span<const int64_t> marks,
                                bool frozen, RingStorage *&previous) noexcept
{
    previous = nullptr;
    if (next == nullptr || next == storage_ || next->capacity() - next->frames() < maxBlock_ ||
        frames < 0 || frames > next->capacity() || marks.size() > kMaxSplices)
    {
        return false;
    }
    mirrorGuards(*next);
    release(previous);
    attach(next);
    // Frozen, the head stands still from the first sample: the gain already 0, nothing to ramp.
    frozen_ = frozen;
    ramp_ = frozen ? 0 : rampLength_;
    head_ = frames;
    writeSlot_ = head_ % capacity_;
    // Until the next write(), R(t) is the head at every sample: with no frame written, headAt(t)
    // is blockHead_ for every t >= blockStart_ − 1, so a block start of 0 covers any t >= 0, as
    // reset() leaves it (the old ring time's block start would read below the head before it).
    blockStart_ = 0;
    blockHead_ = head_;
    blockAdvance_ = 0;
    blockRamp_ = ramp_;
    markCount_ = 0;
    const int64_t oldest = std::max<int64_t>(0, head_ - frames_);
    for (const int64_t m : marks) // at most kMaxSplices
    {
        if (m >= oldest && m < head_ && (markCount_ == 0 || m > marks_[markCount_ - 1]))
        {
            marks_[markCount_++] = m;
        }
    }
    window_ = loopWindow();
    captureWindow(); // until the next write(), the window is the restored ring's
    return true;
}

void LiveSource::write(const float *const *in, uint32_t numInChannels, uint32_t offset,
                       int64_t blockStart, uint32_t numFrames, const float *overdubGain) noexcept
{
    blockStart_ = blockStart;
    blockHead_ = head_;
    blockRamp_ = ramp_;
    blockAdvance_ = 0;
    window_ = loopWindow(); // this block's W, and overdub's period P
    if (frames_ == 0)
    {
        blockRequest_ = 0;
        blockMarkCount_ = 0;
        return; // no ring
    }
    // The selection and the marks for this block. Which marks are in storage is decided per
    // sample (spliceAt), so where a mark leaves never depends on where blocks fall.
    captureWindow();

    // A mono input fills both channels; a missing input or channel is silence.
    const float *src[2] = {nullptr, nullptr};
    if (in != nullptr && numInChannels > 0)
    {
        src[0] = in[0];
        src[1] = numInChannels == 1 ? in[0] : in[1];
    }
    for (const float *&s : src)
    {
        s = s != nullptr ? s + offset : nullptr;
    }

    const float *const gains = overdubGain != nullptr ? overdubGain + offset : nullptr;

    float *const slots = slots_;
    // Overdub reads frame R − P from its slot, (R − P) mod C: P <= N < C, so it wraps once.
    int64_t backSlot = writeSlot_ - window_;
    if (backSlot < 0)
    {
        backSlot += capacity_;
    }
    int64_t written = 0;
    for (uint32_t j = 0; j < numFrames; ++j)
    {
        // The gain steps once per frame written; frozen, the head stops once it reaches 0.
        if (frozen_)
        {
            if (ramp_ == 0)
            {
                break; // stopped: no later sample of this block advances either
            }
            --ramp_;
        }
        else if (ramp_ < rampLength_)
        {
            ++ramp_;
        }
        float l = src[0] != nullptr ? sanitize(src[0][j]) : 0.0f;
        float r = src[1] != nullptr ? sanitize(src[1][j]) : 0.0f;
        // A gain of 0, or a frame R − P before frame 0 (silence), writes the input alone, by M1's
        // path: the sum would turn a −0 input into +0. The sum is stored as any input is.
        if (gains != nullptr && gains[j] != 0.0f && head_ + written >= window_)
        {
            const float o = gains[j];
            l = sanitize(l + o * slots[2 * backSlot]);
            r = sanitize(r + o * slots[2 * backSlot + 1]);
        }
        if (ramp_ != rampLength_)
        {
            const float gain = static_cast<float>(ramp_) / static_cast<float>(rampLength_);
            l *= gain;
            r *= gain;
        }
        slots[2 * writeSlot_] = l;
        slots[2 * writeSlot_ + 1] = r;
        if (writeSlot_ < kGuardFrames)
        {
            slots[2 * (capacity_ + writeSlot_)] = l;
            slots[2 * (capacity_ + writeSlot_) + 1] = r;
        }
        if (++writeSlot_ == capacity_)
        {
            writeSlot_ = 0;
        }
        // The level frames this frame completes (spec §6.9).
        storage_->levelsAfter(head_ + written);
        if (++backSlot == capacity_)
        {
            backSlot = 0;
        }
        ++written;
    }
    head_ += written;
    blockAdvance_ = written;
    // The marks that left storage by the block's end go (spec §6.1). The block's own captured
    // marks stay: they leave sample by sample.
    dropMarksBefore(head_ - frames_);
}

template <int kTaps>
bool LiveSource::gatherTaps(double pos, int64_t head, Cursor &cursor, float *left,
                            float *right) const noexcept
{
    static_assert(kTaps == 2 || kTaps == 4 || kTaps == 16);
    // The sinc's taps start seven frames before floor(pos), the Hermite's one, the line's at
    // floor(pos).
    constexpr int64_t kBefore = kTaps / 2 - 1;
    // Ring time stays far below 2^62; the check also rejects NaN and ±inf before the integer
    // conversion.
    if (!(pos > -0x1.0p62 && pos < 0x1.0p62))
    {
        return false;
    }
    const double base = std::floor(pos);
    const auto i = static_cast<int64_t>(base);
    const int64_t first = i - kBefore;
    // Exact integer bounds on the window, so no rounding in fade() can let a stale or unwritten
    // tap through. Inside it every slot holds its own frame, even with the rest of the block
    // already written (ruling M1-o; see Storage in LiveSource.h): its oldest frame is never below
    // head − N.
    if (first < windowOldest(head) || first + (kTaps - 1) > windowEnd(head) - 1)
    {
        return false;
    }
    if (first < 0)
    {
        // Taps before frame 0 are silence. Here head − N <= the window's oldest <= first < 0, so
        // head < N. The block has written at most maxBlockSize − 1 frames past head − 1, so no
        // frame yet written is at or past C = N + maxBlockSize: none has wrapped, and frame i is
        // in slot i.
        for (int64_t k = 0; k < kTaps; ++k)
        {
            const int64_t index = first + k;
            left[k] = index >= 0 ? slots_[2 * index] : 0.0f;
            right[k] = index >= 0 ? slots_[2 * index + 1] : 0.0f;
        }
        cursor.valid = false;
    }
    else
    {
        // Slot s's channel c is at 2s + c; the guard slots keep the taps contiguous.
        const float *p = slots_ + 2 * slotOf(first, cursor);
        for (int64_t k = 0; k < kTaps; ++k)
        {
            left[k] = p[2 * k];
            right[k] = p[2 * k + 1];
        }
    }
    return true;
}

template bool LiveSource::gatherTaps<2>(double, int64_t, Cursor &, float *, float *) const noexcept;
template bool LiveSource::gatherTaps<4>(double, int64_t, Cursor &, float *, float *) const noexcept;
template bool LiveSource::gatherTaps<16>(double, int64_t, Cursor &, float *,
                                         float *) const noexcept;

template <int kTaps>
bool LiveSource::readTaps(double pos, int64_t head, Cursor &cursor, float &left,
                          float &right) const noexcept
{
    static_assert(kTaps == 2 || kTaps == 4);
    float l[kTaps];
    float r[kTaps];
    if (!gatherTaps<kTaps>(pos, head, cursor, l, r))
    {
        return false;
    }
    // gatherTaps accepted pos, so it is finite and far inside int64's range.
    const double base = std::floor(pos);
    const auto t = static_cast<float>(pos - base);
    if constexpr (kTaps == 4)
    {
        left = interp::catmullRom(l[0], l[1], l[2], l[3], t);
        right = interp::catmullRom(r[0], r[1], r[2], r[3], t);
    }
    else
    {
        left = interp::lerp(l[0], l[1], t);
        right = interp::lerp(r[0], r[1], t);
    }
    return true;
}

bool LiveSource::read(double pos, int64_t head, Cursor &cursor, float &left,
                      float &right) const noexcept
{
    return readTaps<4>(pos, head, cursor, left, right);
}

bool LiveSource::readLinear(double pos, int64_t head, Cursor &cursor, float &left,
                            float &right) const noexcept
{
    return readTaps<2>(pos, head, cursor, left, right);
}

template <int kTaps>
bool LiveSource::gatherLevelTaps(uint32_t n, double pos, int64_t head, Cursor &cursor, float *left,
                                 float *right) const noexcept
{
    static_assert(kTaps == 2 || kTaps == 4 || kTaps == 16);
    if (n == 0)
    {
        return gatherTaps<kTaps>(pos, head, cursor, left, right);
    }
    if (n >= levelCount_)
    {
        return false;
    }
    constexpr int64_t kBefore = kTaps / 2 - 1;
    // Level time stays far below 2^62 (ring time does); the check also rejects NaN and ±inf.
    if (!(pos > -0x1.0p62 && pos < 0x1.0p62))
    {
        return false;
    }
    const auto i = static_cast<int64_t>(std::floor(pos));
    const int64_t first = i - kBefore;
    const int64_t last = first + (kTaps - 1);
    // Exact integer bounds in ring frames: the first tap inside the window, the last complete at
    // head (its frame plus the level's lag at most the window's end, which is at most the head).
    // Then every tap is the one written for it: its ring frame is at or after the window's oldest,
    // never below head − N, and a level's slot is overwritten only once the head passes its ring
    // frame + C + the level's lag (RingStorage), as level 0's.
    const int64_t d = int64_t{1} << n;
    if (first * d < windowOldest(head) || last * d + RingStorage::levelLag(n) > windowEnd(head) - 1)
    {
        return false;
    }
    const RingView &level = levelView(n);
    if (first < 0)
    {
        // Level frames before 0 are silence. Here head < N (as in gatherTaps), so no level frame
        // written yet is at or past its capacity, ceil(C / 2ⁿ): frame i is in slot i.
        for (int64_t k = 0; k < kTaps; ++k)
        {
            const int64_t index = first + k;
            left[k] = index >= 0 ? level.slots[2 * index] : 0.0f;
            right[k] = index >= 0 ? level.slots[2 * index + 1] : 0.0f;
        }
        cursor.valid = false;
    }
    else
    {
        const float *p = level.slots + 2 * levelSlotOf(first, level.capacity, cursor);
        for (int64_t k = 0; k < kTaps; ++k)
        {
            left[k] = p[2 * k];
            right[k] = p[2 * k + 1];
        }
    }
    return true;
}

template bool LiveSource::gatherLevelTaps<2>(uint32_t, double, int64_t, Cursor &, float *,
                                             float *) const noexcept;
template bool LiveSource::gatherLevelTaps<4>(uint32_t, double, int64_t, Cursor &, float *,
                                             float *) const noexcept;
template bool LiveSource::gatherLevelTaps<16>(uint32_t, double, int64_t, Cursor &, float *,
                                              float *) const noexcept;

template <int kTaps>
bool LiveSource::readLevelTaps(uint32_t n, double pos, int64_t head, Cursor &cursor, float &left,
                               float &right) const noexcept
{
    static_assert(kTaps == 2 || kTaps == 4);
    float l[kTaps];
    float r[kTaps];
    if (!gatherLevelTaps<kTaps>(n, pos, head, cursor, l, r))
    {
        return false;
    }
    // gatherLevelTaps accepted pos, so it is finite and far inside int64's range.
    const double base = std::floor(pos);
    const auto t = static_cast<float>(pos - base);
    if constexpr (kTaps == 4)
    {
        left = interp::catmullRom(l[0], l[1], l[2], l[3], t);
        right = interp::catmullRom(r[0], r[1], r[2], r[3], t);
    }
    else
    {
        left = interp::lerp(l[0], l[1], t);
        right = interp::lerp(r[0], r[1], t);
    }
    return true;
}

bool LiveSource::readLevel(uint32_t n, double pos, int64_t head, Cursor &cursor, float &left,
                           float &right) const noexcept
{
    return readLevelTaps<4>(n, pos, head, cursor, left, right);
}

bool LiveSource::readLinearLevel(uint32_t n, double pos, int64_t head, Cursor &cursor, float &left,
                                 float &right) const noexcept
{
    return readLevelTaps<2>(n, pos, head, cursor, left, right);
}

int64_t LiveSource::levelSlotOf(int64_t first, int64_t capacity, Cursor &cursor) noexcept
{
    if (cursor.valid && first >= cursor.index && first - cursor.index < capacity)
    {
        cursor.slot += first - cursor.index;
        if (cursor.slot >= capacity)
        {
            cursor.slot -= capacity;
        }
    }
    else
    {
        cursor.slot = first % capacity;
    }
    cursor.index = first;
    cursor.valid = true;
    return cursor.slot;
}

int64_t LiveSource::slotOf(int64_t first, Cursor &cursor) const noexcept
{
    if (cursor.valid && first >= cursor.index && first - cursor.index < capacity_)
    {
        cursor.slot += first - cursor.index;
        if (cursor.slot >= capacity_)
        {
            cursor.slot -= capacity_;
        }
    }
    else
    {
        cursor.slot = first % capacity_;
    }
    cursor.index = first;
    cursor.valid = true;
    return cursor.slot;
}
} // namespace gg
