// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Replaces every global operator new and delete for the whole test executable. The operators
// allocate with malloc (posix_memalign or _aligned_malloc when over-aligned) and free with
// the matching call, as the standard library's do, and count an armed operator new.

#include "support/AllocTripwire.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace
{
std::atomic<uint64_t> allocations{0};
// How many Scopes are alive on this thread. constinit: reading it from operator new must not
// run an initializer.
constinit thread_local int armedDepth = 0;

void *tryAllocate(std::size_t size, std::size_t alignment, bool aligned) noexcept
{
    size = std::max<std::size_t>(size, 1); // new must return a unique pointer even for 0 bytes
    if (!aligned)
    {
        return std::malloc(size);
    }
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    void *p = nullptr;
    return posix_memalign(&p, std::max(alignment, sizeof(void *)), size) == 0 ? p : nullptr;
#endif
}

void *allocate(std::size_t size, std::size_t alignment, bool aligned)
{
    if (armedDepth > 0)
    {
        allocations.fetch_add(1, std::memory_order_relaxed);
    }
    for (;;)
    {
        if (void *p = tryAllocate(size, alignment, aligned))
        {
            return p;
        }
        const std::new_handler handler = std::get_new_handler();
        if (handler == nullptr)
        {
            throw std::bad_alloc();
        }
        handler();
    }
}

void *allocateNoThrow(std::size_t size, std::size_t alignment, bool aligned) noexcept
{
    try
    {
        return allocate(size, alignment, aligned);
    }
    catch (...)
    {
        return nullptr;
    }
}

void release(void *p) noexcept { std::free(p); }

void releaseAligned(void *p) noexcept
{
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

std::size_t value(std::align_val_t alignment) noexcept
{
    return static_cast<std::size_t>(alignment);
}
} // namespace

namespace gg::test
{
AllocTripwire::Scope::Scope() { ++armedDepth; }
AllocTripwire::Scope::~Scope() { --armedDepth; }

uint64_t AllocTripwire::count() noexcept { return allocations.load(std::memory_order_relaxed); }
void AllocTripwire::resetCount() noexcept { allocations.store(0, std::memory_order_relaxed); }
} // namespace gg::test

void *operator new(std::size_t size) { return allocate(size, 0, false); }
void *operator new[](std::size_t size) { return allocate(size, 0, false); }
void *operator new(std::size_t size, std::align_val_t a) { return allocate(size, value(a), true); }
void *operator new[](std::size_t size, std::align_val_t a)
{
    return allocate(size, value(a), true);
}
void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    return allocateNoThrow(size, 0, false);
}
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
    return allocateNoThrow(size, 0, false);
}
void *operator new(std::size_t size, std::align_val_t a, const std::nothrow_t &) noexcept
{
    return allocateNoThrow(size, value(a), true);
}
void *operator new[](std::size_t size, std::align_val_t a, const std::nothrow_t &) noexcept
{
    return allocateNoThrow(size, value(a), true);
}

void operator delete(void *p) noexcept { release(p); }
void operator delete[](void *p) noexcept { release(p); }
void operator delete(void *p, std::size_t) noexcept { release(p); }
void operator delete[](void *p, std::size_t) noexcept { release(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { release(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { release(p); }
void operator delete(void *p, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete[](void *p, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete(void *p, std::align_val_t, const std::nothrow_t &) noexcept
{
    releaseAligned(p);
}
void operator delete[](void *p, std::align_val_t, const std::nothrow_t &) noexcept
{
    releaseAligned(p);
}
