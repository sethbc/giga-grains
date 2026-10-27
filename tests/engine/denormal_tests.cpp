// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <catch2/catch_test_macros.hpp>

#include "gg/core/DenormalGuard.h"

TEST_CASE("denormals flush inside guard", "[denormal]")
{
    // volatile keeps the multiply at run time, under whatever FP mode is current.
    volatile float t = 1e-38f;
    {
        gg::ScopedDenormalsOff guard;
        volatile float inside = t * 1e-3f;
        REQUIRE(inside == 0.0f);
    }
    volatile float outside = t * 1e-3f;
    REQUIRE(outside != 0.0f);
}

TEST_CASE("nested guards restore the outer guard's mode", "[denormal]")
{
    // Engine::process sets its own guard, and the plugin may set one around it (M1): when the
    // inner guard ends, flushing must stay on until the outer one ends too.
    volatile float t = 1e-38f;
    {
        gg::ScopedDenormalsOff outer;
        {
            gg::ScopedDenormalsOff inner;
            volatile float inside = t * 1e-3f;
            REQUIRE(inside == 0.0f);
        }
        volatile float afterInner = t * 1e-3f;
        REQUIRE(afterInner == 0.0f);
    }
    volatile float outside = t * 1e-3f;
    REQUIRE(outside != 0.0f);
}
