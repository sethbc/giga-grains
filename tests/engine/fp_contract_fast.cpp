// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The engine's header-inline math and the Cloud scheduler's inline schedule(), compiled with an
// includer's flags that allow contraction:
// tests/CMakeLists.txt builds this file with Clang's default -ffp-contract=on instead of the
// project's -ffp-contract=off (under GCC, whose guard is empty, it keeps the project flags).
// fp_contract_tests.cpp checks that each function here matches the same call compiled with the
// project's flags, bit for bit.
//
// flatten and -O2 inline the header functions all the way into these wrappers, so what runs is
// the code this file's flags produced, and no out-of-line copy is left behind. An out-of-line
// call could reach any translation unit's copy of an inline function, whichever the linker kept.

#include <cstdint>

#include "gg/core/Interpolate.h"
#include "gg/core/MathDet.h"
#include "gg/core/Window.h"
#include "gg/grain/CloudScheduler.h"

float ggFastLinear(const float *data, int64_t frames, double pos);
float ggFastHermite(const float *data, int64_t frames, double pos);
double ggFastExp2(double x);
double ggFastLog(double x);
float ggFastHann(double phase01);
int ggFastCloud(const gg::CloudParams &params, int64_t frames, gg::GrainDraw *out, int capacity);
float ggFastCanary(float a, float b, float c);

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

/// Draws the grains of a Cloud stream at 48 kHz over frames samples, in 64-frame blocks, into
/// out (at most capacity of them), and returns how many there were.
[[gnu::flatten]] int ggFastCloud(const gg::CloudParams &params, int64_t frames, gg::GrainDraw *out,
                                 int capacity)
{
    gg::CloudScheduler cloud;
    cloud.prepare(48000.0);
    cloud.setParams(params);
    int count = 0;
    for (int64_t start = 0; start < frames; start += 64)
    {
        cloud.schedule(start, 64, 1234, 0, 0, [&](const gg::GrainDraw &g) {
            if (count < capacity)
            {
                out[count] = g;
            }
            ++count;
        });
    }
    return count;
}

/// The positive control: plain a * b + c, unguarded, so this file's flags contract it where the
/// target has an FMA. If it isn't contracted, nothing here is, and the test above proves nothing.
float ggFastCanary(float a, float b, float c) { return a * b + c; }
