// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/SamplePool.h"

#include <algorithm>
#include <bit>

namespace gg::media
{
namespace
{
bool sameKey(const PoolKey &a, const PoolKey &b) noexcept
{
    return a.fileSize == b.fileSize &&
           std::bit_cast<uint64_t>(a.rate) == std::bit_cast<uint64_t>(b.rate) &&
           a.sha256Hex == b.sha256Hex;
}
} // namespace

SamplePool &SamplePool::process()
{
    static SamplePool pool;
    return pool;
}

std::shared_ptr<const PreparedSource> SamplePool::find(const PoolKey &key)
{
    const std::lock_guard lock(mutex_);
    std::erase_if(entries_, [](const Entry &e) { return e.source.expired(); });
    for (const Entry &e : entries_)
    {
        if (sameKey(e.key, key))
        {
            return e.source.lock();
        }
    }
    return nullptr;
}

std::shared_ptr<const PreparedSource>
SamplePool::insert(const PoolKey &key, const std::shared_ptr<const PreparedSource> &source)
{
    const std::lock_guard lock(mutex_);
    for (Entry &e : entries_)
    {
        if (sameKey(e.key, key))
        {
            if (auto live = e.source.lock())
            {
                return live;
            }
            e.source = source;
            return source;
        }
    }
    entries_.push_back({key, source});
    return source;
}

std::size_t SamplePool::alive() const
{
    const std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(std::count_if(
        entries_.begin(), entries_.end(), [](const Entry &e) { return !e.source.expired(); }));
}

void SamplePool::clear()
{
    const std::lock_guard lock(mutex_);
    entries_.clear();
}
} // namespace gg::media
