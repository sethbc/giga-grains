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
