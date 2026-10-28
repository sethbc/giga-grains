// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The engine's header-inline math, compiled with an includer's flags that allow contraction:
// tests/CMakeLists.txt builds this file with Clang's default -ffp-contract=on instead of the
// project's -ffp-contract=off (under GCC, whose guard is empty, it keeps the project flags).
// fp_contract_tests.cpp checks that each function here matches the same call compiled with the
// project's flags, bit for bit.
//
// flatten inlines the header functions into these wrappers, so what runs is the code this file's
// flags produced. An out-of-line call could reach any translation unit's copy of the inline
// function, whichever the linker kept.

#include <cstdint>

#include "gg/core/Interpolate.h"
#include "gg/core/MathDet.h"
#include "gg/core/Window.h"

float ggFastLinear(const float *data, int64_t frames, double pos);
float ggFastHermite(const float *data, int64_t frames, double pos);
double ggFastExp2(double x);
double ggFastLog(double x);
float ggFastHann(double phase01);

[[gnu::flatten]] float ggFastLinear(const float *data, int64_t frames, double pos)
{
    return gg::interp::linear(data, frames, pos);
}

[[gnu::flatten]] float ggFastHermite(const float *data, int64_t frames, double pos)
{
    return gg::interp::hermite4(data, frames, pos);
}

[[gnu::flatten]] double ggFastExp2(double x) { return gg::exp2Det(x); }

[[gnu::flatten]] double ggFastLog(double x) { return gg::logDet(x); }

[[gnu::flatten]] float ggFastHann(double phase01)
{
    static const gg::HannTable table; // the constructor is out of line, in the engine
    return table.at(phase01);
}
