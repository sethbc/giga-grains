// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/source/LiveSource.h"

#include <cmath>
#include <cstddef>
#include <stdexcept>

#include "gg/core/Interpolate.h"

namespace gg
{
namespace
{
// Keeps every ring-time and slot computation far from int64 overflow; 2^40 frames is 265 days
// at 48 kHz, and far more memory than any ring would get.
constexpr double kMaxRingFrames = 0x1.0p40;

// Non-finite input is stored as 0 (Review Focus 2); finite input is clamped.
float sanitize(float x) noexcept
{
    if (!std::isfinite(x))
    {
        return 0.0f;
    }
    return std::clamp(x, -LiveSource::kMaxInputMagnitude, LiveSource::kMaxInputMagnitude);
}
} // namespace

int64_t LiveSource::freezeRampSamples(double sampleRate) noexcept
{
    // llround is exact; the min keeps its argument in range for any finite rate.
    const double samples = std::min(kFreezeRampSeconds * sampleRate, kMaxRingFrames);
    return std::max<int64_t>(1, std::llround(samples));
}

void LiveSource::prepare(double sampleRate, double seconds, uint32_t maxBlockSize)
{
    if (!(std::isfinite(seconds) && seconds >= 0.0))
    {
        throw std::invalid_argument("LiveSource: seconds must be finite and >= 0");
    }
    const double frames = std::ceil(seconds * sampleRate);
    const auto block = static_cast<double>(std::max<uint32_t>(1, maxBlockSize));
    if (!(frames + block <= kMaxRingFrames))
    {
        throw std::invalid_argument("LiveSource: the ring would exceed 2^40 frames");
    }
    frames_ = static_cast<int64_t>(frames);
    rampLength_ = freezeRampSamples(sampleRate);
    if (frames_ > 0)
    {
        // Ruling M1-o: a whole block's worth of slots past the window, so the frames a block
        // writes after sample t never land in t's window (see Storage in LiveSource.h).
        capacity_ = frames_ + static_cast<int64_t>(block);
        const auto stride = static_cast<std::size_t>(capacity_ + kGuardFrames);
        storage_.assign(2 * stride, 0.0f);
        channels_[0] = storage_.data();
        channels_[1] = storage_.data() + stride;
    }
    else
    {
        capacity_ = 0;
        storage_.clear();
        channels_[0] = channels_[1] = nullptr;
    }
    reset();
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
}

void LiveSource::write(const float *const *in, uint32_t numInChannels, uint32_t offset,
                       int64_t blockStart, uint32_t numFrames) noexcept
{
    blockStart_ = blockStart;
    blockHead_ = head_;
    blockRamp_ = ramp_;
    blockAdvance_ = 0;
    if (frames_ == 0)
    {
        return; // no ring
    }

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

    float *const left = channels_[0];
    float *const right = channels_[1];
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
        if (ramp_ != rampLength_)
        {
            const float gain = static_cast<float>(ramp_) / static_cast<float>(rampLength_);
            l *= gain;
            r *= gain;
        }
        left[writeSlot_] = l;
        right[writeSlot_] = r;
        if (writeSlot_ < kGuardFrames)
        {
            left[capacity_ + writeSlot_] = l;
            right[capacity_ + writeSlot_] = r;
        }
        if (++writeSlot_ == capacity_)
        {
            writeSlot_ = 0;
        }
        ++written;
    }
    head_ += written;
    blockAdvance_ = written;
}

bool LiveSource::read(double pos, int64_t head, Cursor &cursor, float &left,
                      float &right) const noexcept
{
    // Ring time stays far below 2^62; the check also rejects NaN and ±inf before the integer
    // conversion.
    if (!(pos > -0x1.0p62 && pos < 0x1.0p62))
    {
        return false;
    }
    const double base = std::floor(pos);
    const auto i = static_cast<int64_t>(base);
    const int64_t first = i - 1;
    // Exact integer bounds on the window, so no rounding in fade() can let a stale or unwritten
    // tap through. Inside it every slot holds its own frame, even with the rest of the block
    // already written (ruling M1-o; see Storage in LiveSource.h).
    if (first < head - frames_ || i + 2 > head - 1)
    {
        return false;
    }
    const auto t = static_cast<float>(pos - base);
    if (first < 0)
    {
        // Taps before frame 0 are silence. Here head − N <= first < 0, so head < N. The block has
        // written at most maxBlockSize − 1 frames past head − 1, so no frame yet written is at or
        // past C = N + maxBlockSize: none has wrapped, and frame i is in slot i.
        float l[4];
        float r[4];
        for (int64_t k = 0; k < 4; ++k)
        {
            const int64_t index = first + k;
            l[k] = index >= 0 ? channels_[0][index] : 0.0f;
            r[k] = index >= 0 ? channels_[1][index] : 0.0f;
        }
        left = interp::catmullRom(l[0], l[1], l[2], l[3], t);
        right = interp::catmullRom(r[0], r[1], r[2], r[3], t);
        cursor.valid = false;
        return true;
    }
    const int64_t slot = slotOf(first, cursor);
    const float *l = channels_[0] + slot;
    const float *r = channels_[1] + slot;
    left = interp::catmullRom(l[0], l[1], l[2], l[3], t);
    right = interp::catmullRom(r[0], r[1], r[2], r[3], t);
    return true;
}

bool LiveSource::readLinear(double pos, int64_t head, Cursor &cursor, float &left,
                            float &right) const noexcept
{
    // As read(), for taps i and i + 1.
    if (!(pos > -0x1.0p62 && pos < 0x1.0p62))
    {
        return false;
    }
    const double base = std::floor(pos);
    const auto i = static_cast<int64_t>(base);
    if (i < head - frames_ || i + 1 > head - 1)
    {
        return false;
    }
    const auto t = static_cast<float>(pos - base);
    if (i < 0)
    {
        // Taps before frame 0 are silence. As in read(), head < N here, so no frame has wrapped
        // and frame 0, the only tap that can be written, is in slot 0.
        float l[2];
        float r[2];
        for (int64_t k = 0; k < 2; ++k)
        {
            const int64_t index = i + k;
            l[k] = index >= 0 ? channels_[0][index] : 0.0f;
            r[k] = index >= 0 ? channels_[1][index] : 0.0f;
        }
        left = interp::lerp(l[0], l[1], t);
        right = interp::lerp(r[0], r[1], t);
        cursor.valid = false;
        return true;
    }
    const int64_t slot = slotOf(i, cursor);
    const float *l = channels_[0] + slot;
    const float *r = channels_[1] + slot;
    left = interp::lerp(l[0], l[1], t);
    right = interp::lerp(r[0], r[1], t);
    return true;
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
