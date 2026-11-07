// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/VoiceAllocator.h"

namespace gg::core
{

int VoiceAllocator::start(uint8_t noteIndex, uint64_t noteOrdinal, int64_t at, uint32_t &ordinalOut,
                          int &stolenSlotOut) noexcept
{
    // The lowest free slot; else the oldest weak voice, else the oldest (V4).
    int slot = -1;
    for (uint32_t i = 0; i < kVoices; ++i)
    {
        if (!voices_[i].spawning)
        {
            slot = static_cast<int>(i);
            break;
        }
    }
    stolenSlotOut = -1;
    if (slot < 0)
    {
        for (const bool weakOnly : {true, false})
        {
            for (uint32_t i = 0; i < kVoices; ++i)
            {
                const Voice &v = voices_[i];
                if ((!weakOnly || v.weak) &&
                    (slot < 0 || v.ordinal < voices_[static_cast<uint32_t>(slot)].ordinal))
                {
                    slot = static_cast<int>(i);
                }
            }
            if (slot >= 0)
            {
                break;
            }
        }
        stolenSlotOut = slot;
    }
    Voice &v = voices_[static_cast<uint32_t>(slot)];
    v = Voice{};
    v.spawning = true;
    v.note = noteIndex;
    v.noteOrdinal = noteOrdinal;
    v.startedAt = at;
    // The engine's kNoVoice (0xFFFFFFFF) is never an ordinal: the count skips it.
    if (nextOrdinal_ == gg::kNoVoice)
    {
        nextOrdinal_ = 0;
    }
    v.ordinal = nextOrdinal_++;
    ordinalOut = v.ordinal;
    return slot;
}

void VoiceAllocator::finish(int slot) noexcept
{
    if (slot >= 0 && static_cast<uint32_t>(slot) < kVoices)
    {
        voices_[static_cast<uint32_t>(slot)].spawning = false;
    }
}

void VoiceAllocator::clear() noexcept
{
    for (Voice &v : voices_)
    {
        v.spawning = false;
    }
}

void VoiceAllocator::reset() noexcept
{
    voices_.fill(Voice{});
    nextOrdinal_ = 0;
}

bool VoiceAllocator::anySpawning() const noexcept
{
    for (const Voice &v : voices_)
    {
        if (v.spawning)
        {
            return true;
        }
    }
    return false;
}

int VoiceAllocator::oldestWeak() const noexcept
{
    int slot = -1;
    for (uint32_t i = 0; i < kVoices; ++i)
    {
        const Voice &v = voices_[i];
        if (v.spawning && v.weak &&
            (slot < 0 || v.ordinal < voices_[static_cast<uint32_t>(slot)].ordinal))
        {
            slot = static_cast<int>(i);
        }
    }
    return slot;
}

} // namespace gg::core
