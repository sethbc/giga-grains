// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_TESTS_SUPPORT_ALLOCTRIPWIRE_H
#define GG_TESTS_SUPPORT_ALLOCTRIPWIRE_H

#include <cstdint>

namespace gg::test
{
/// Counts operator new allocations, to prove that code (gg::Engine::process) never allocates.
///
/// AllocTripwire.cpp replaces every global operator new (plain, array, aligned, nothrow). Each
/// call made while a Scope is alive on the calling thread increments count(); calls on other
/// threads, or outside any Scope, are not counted. Either way the operators still allocate
/// normally, so armed code keeps working.
///
/// Under ASan (GG_SANITIZE) the replacements are compiled out, so ASan keeps its own operators
/// and with them its alloc-dealloc-mismatch and new-delete-type-mismatch checks (ruling R7's
/// fallback, carry-over [144]). available() is then false and count() stays 0: a test asserts
/// on count() only when available(), and SKIPs only if the count is all it checks. The RTSan
/// build keeps the tripwire.
///
/// It doesn't see direct malloc-family calls (malloc, calloc, realloc, posix_memalign, and
/// anything a C library allocates internally). The RTSan build covers those, on Linux in CI.
///
/// Don't use Catch2 assertions inside a Scope: they can allocate. Read count() after it ends.
struct AllocTripwire
{
    /// Arms the tripwire on this thread for the Scope's lifetime. Scopes nest.
    struct Scope
    {
        Scope();
        ~Scope();
        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;
    };

    /// Whether this build counts allocations: false under GG_SANITIZE, true otherwise.
    static bool available() noexcept;

    /// Armed allocations, on any thread, since the start of the program or the last resetCount().
    /// Always 0 when !available().
    static uint64_t count() noexcept;
    static void resetCount() noexcept;
};

/// Measures the heap a piece of code holds at its peak: the bytes that the calling thread's
/// operator new allocates while an AllocPeak is alive, less what its operator delete frees of
/// them again, at the most. A decoder's buffers, say (the decoder tests' peak checks).
///
/// The replaced operators (AllocTripwire.cpp) record each such block in a fixed table, so
/// recording allocates nothing. Only one AllocPeak may be alive in the program at a time.
/// - It counts the bytes asked for, not the allocator's rounding or bookkeeping.
/// - A block freed on another thread stays counted, which can only overstate the peak.
/// - Like AllocTripwire, it doesn't see direct malloc-family calls (dr_libs' decoder state, say),
///   and under GG_SANITIZE it counts nothing: available() is then false.
/// - complete() is false if more blocks were held at once than the table has room for; the
///   bytes are then understated, so a test must REQUIRE complete().
///
/// Don't use Catch2 assertions while it is alive: they allocate. Read it after the code ran.
class AllocPeak
{
  public:
    AllocPeak();
    ~AllocPeak();
    AllocPeak(const AllocPeak &) = delete;
    AllocPeak &operator=(const AllocPeak &) = delete;

    /// Whether this build measures: false under GG_SANITIZE, true otherwise.
    static bool available() noexcept;

    /// The most bytes held at once since construction.
    uint64_t peakBytes() const noexcept;
    /// The bytes still held now.
    uint64_t liveBytes() const noexcept;
    /// Whether every block fitted in the table, so the figures are exact.
    bool complete() const noexcept;
};
} // namespace gg::test

/// Requires that `count`, an AllocTripwire::count() read after the Scope ended, is 0, in a build
/// that counts allocations (AllocTripwire::available()); elsewhere it checks nothing. A macro, so
/// Catch2 reports the caller's line. Needs <catch2/catch_test_macros.hpp>.
#define REQUIRE_NO_ALLOCATIONS(count)                                                              \
    do                                                                                             \
    {                                                                                              \
        if (::gg::test::AllocTripwire::available())                                                \
        {                                                                                          \
            REQUIRE((count) == 0);                                                                 \
        }                                                                                          \
    } while (false)

#endif // GG_TESTS_SUPPORT_ALLOCTRIPWIRE_H
