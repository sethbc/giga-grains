// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_TESTS_SUPPORT_ALLOCTRIPWIRE_H
#define GG_TESTS_SUPPORT_ALLOCTRIPWIRE_H

#include <cstdint>

namespace gg::test
{
/// Counts heap allocations, to prove that code (gg::Engine::process) never allocates.
///
/// AllocTripwire.cpp replaces every global operator new (plain, array, aligned, nothrow). Each
/// call made while a Scope is alive on the calling thread increments count(); calls on other
/// threads, or outside any Scope, are not counted. Either way the operators still allocate
/// normally, so armed code keeps working.
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

    /// Armed allocations, on any thread, since the start of the program or the last resetCount().
    static uint64_t count() noexcept;
    static void resetCount() noexcept;
};
} // namespace gg::test

#endif // GG_TESTS_SUPPORT_ALLOCTRIPWIRE_H
