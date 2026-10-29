// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Replaces every global operator new and delete for the whole test executable. The operators
// allocate with malloc (posix_memalign or _aligned_malloc when over-aligned) and free with
// the matching call, as the standard library's do, count an armed operator new, and record the
// blocks an AllocPeak measures.
//
// Under GG_SANITIZE none of them is replaced: ASan's own operators stay, with its
// alloc-dealloc-mismatch and new-delete-type-mismatch checks, and nothing is counted.

#include "support/AllocTripwire.h"

#if !defined(GG_SANITIZE)
#error "GG_SANITIZE must be defined, 0 or 1 (tests/CMakeLists.txt)"
#endif

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

#if !GG_SANITIZE
/// The live AllocPeak's state: the blocks allocated on its thread and not yet freed, in a fixed
/// table so recording never allocates. Only that thread touches it, through `measuring`.
struct PeakState
{
    struct Block
    {
        void *p;
        std::size_t size;
    };
    static constexpr std::size_t kCapacity = 4096;
    Block blocks[kCapacity];
    std::size_t count = 0;
    uint64_t live = 0;
    uint64_t peak = 0;
    bool complete = true;

    void add(void *p, std::size_t size) noexcept
    {
        if (count == kCapacity)
        {
            complete = false;
            return;
        }
        blocks[count++] = {p, size};
        live += size;
        peak = std::max(peak, live);
    }
    void remove(void *p) noexcept
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (blocks[i].p == p)
            {
                live -= blocks[i].size;
                blocks[i] = blocks[--count];
                return;
            }
        }
    }
};
constinit PeakState peakState{};
std::atomic<bool> peakAlive{false};
/// The thread whose allocations are measured has this set while its AllocPeak lives.
constinit thread_local bool measuring = false;

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
            if (measuring)
            {
                peakState.add(p, size);
            }
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

void forget(void *p) noexcept
{
    if (measuring && p != nullptr)
    {
        peakState.remove(p);
    }
}

void release(void *p) noexcept
{
    forget(p);
    std::free(p);
}

void releaseAligned(void *p) noexcept
{
    forget(p);
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
#endif // !GG_SANITIZE
} // namespace

namespace gg::test
{
AllocTripwire::Scope::Scope() { ++armedDepth; }
AllocTripwire::Scope::~Scope() { --armedDepth; }

bool AllocTripwire::available() noexcept { return !GG_SANITIZE; }
uint64_t AllocTripwire::count() noexcept { return allocations.load(std::memory_order_relaxed); }
void AllocTripwire::resetCount() noexcept { allocations.store(0, std::memory_order_relaxed); }

#if GG_SANITIZE
AllocPeak::AllocPeak() = default;
AllocPeak::~AllocPeak() = default;
bool AllocPeak::available() noexcept { return false; }
uint64_t AllocPeak::peakBytes() const noexcept { return 0; }
uint64_t AllocPeak::liveBytes() const noexcept { return 0; }
bool AllocPeak::complete() const noexcept { return true; }
#else
AllocPeak::AllocPeak()
{
    if (peakAlive.exchange(true))
    {
        std::abort(); // two at once: the table is the program's only one
    }
    peakState.count = 0;
    peakState.live = 0;
    peakState.peak = 0;
    peakState.complete = true;
    measuring = true;
}
AllocPeak::~AllocPeak()
{
    measuring = false;
    peakAlive.store(false);
}
bool AllocPeak::available() noexcept { return true; }
uint64_t AllocPeak::peakBytes() const noexcept { return peakState.peak; }
uint64_t AllocPeak::liveBytes() const noexcept { return peakState.live; }
bool AllocPeak::complete() const noexcept { return peakState.complete; }
#endif
} // namespace gg::test

#if !GG_SANITIZE
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
#endif // !GG_SANITIZE
