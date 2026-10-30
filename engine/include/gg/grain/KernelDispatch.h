// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_KERNELDISPATCH_H
#define GG_GRAIN_KERNELDISPATCH_H

#include <cstdint>

#include <sst/cpputils/rtsan_support.h>

#include "gg/grain/GrainKernels.h"

/// GG_KERNELS_AVX2 is 1 where the build has the 8-wide AVX2 kernels (GrainKernelsAvx2.cpp, the
/// AVX2 spike: M2 engine spec §6.12): x86-64 with GCC or Clang (ClangCL included), 0 elsewhere,
/// where every table holds the 4-wide kernels. A universal macOS build has them in its x86_64
/// slice only.
/// ClangCL below 19 is left out (final review I2): its <immintrin.h> declares the AVX2 intrinsics
/// without /arch:AVX2 only from Clang 19 on (VS 2022 ships 17 and 18 before 17.14), so such a build
/// gets the 4-wide table everywhere instead of failing to compile.
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__clang__) || defined(__GNUC__)) &&       \
    !(defined(_MSC_VER) && defined(__clang__) && __clang_major__ < 19)
#define GG_KERNELS_AVX2 1
#else
#define GG_KERNELS_AVX2 0
#endif

/// The kernel tables (M2 engine spec §6.12): one function per (reader, sample or live) pair, with
/// renderSampleSimd's and renderLiveSimd's contracts. The SSE table holds those 4-wide templates;
/// the AVX2 table their 8-wide forms, whose output is the 4-wide kernels' bit for bit (each
/// 8-wide operation is the 4-wide one on two halves, lane-wise, with no horizontal operation and
/// no FMA), so which table renders changes speed alone, never a bit (INTENT §14; §23's ruling).
namespace gg::kernels
{
using SampleKernel = void (*)(const GrainRows &rows, uint32_t i, const SampleSource &src,
                              const Output &out, int64_t from, int64_t to,
                              const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING;
using LiveKernel = void (*)(const GrainRows &rows, uint32_t i, const LiveSource &live,
                            uint32_t decimation, const Output &out, int64_t from, int64_t to,
                            const Tables &tables) noexcept SST_CPPUTILS_NONBLOCKING;

struct Dispatch
{
    SampleKernel sampleHermite;
    SampleKernel sampleLinear;
    SampleKernel sampleSinc;
    LiveKernel liveHermite;
    LiveKernel liveLinear;
    LiveKernel liveSinc;
    /// The interior's width: 4 for the SSE (or NEON) table, 8 for the AVX2 one.
    uint32_t lanes;
};

/// The 4-wide table: renderSampleSimd<Reader> and renderLiveSimd<Reader>.
const Dispatch &sseKernels() noexcept;

/// Whether this CPU and OS run AVX2 code: cpuid's OSXSAVE, AVX and AVX2 bits, and xgetbv's XMM
/// and YMM state (the OS saves the YMM registers). False where GG_KERNELS_AVX2 is 0.
bool cpuRunsAvx2() noexcept;

/// The 8-wide table, or null where GG_KERNELS_AVX2 is 0 or !cpuRunsAvx2().
const Dispatch *avx2Kernels() noexcept;

/// dispatch()'s choice from what it reads: the SSE table when forceSse is a non-empty string (the
/// value of GG_FORCE_SSE) or the CPU doesn't run AVX2 (cpuRunsAvx2's answer), otherwise the AVX2
/// table where the build has it. Pure, so the tests can check the environment's path.
const Dispatch &resolveKernels(const char *forceSse, bool runsAvx2) noexcept;

/// The process's table, resolved on the first call: resolveKernels(std::getenv("GG_FORCE_SSE"),
/// cpuRunsAvx2()). The first call is Engine's constructor's, so the environment is read there, off
/// the audio thread, and never again. Each Engine keeps the table it was constructed with
/// (EngineConfig::kernels).
const Dispatch &dispatch() noexcept;

/// Switches dispatch() to the SSE table for the rest of the process, for the tests. An Engine
/// keeps the table it was constructed with, so call this before constructing the engines it is
/// for, and so before any of them renders. (gg-bench's engine-sample-sse doesn't use it: it sets
/// EngineConfig::kernels on its own engines.)
void forceSse() noexcept;

#if GG_KERNELS_AVX2
namespace detail
{
/// The 8-wide table, defined in GrainKernelsAvx2.cpp. Only call it where cpuRunsAvx2().
const Dispatch &avx2Table() noexcept;
} // namespace detail
#endif
} // namespace gg::kernels

#endif // GG_GRAIN_KERNELDISPATCH_H
