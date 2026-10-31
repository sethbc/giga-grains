// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The kernel tables' resolution (M2 engine spec §6.12). This file is compiled with the project's
// flags alone; the 8-wide kernels are GrainKernelsAvx2.cpp's.

#include "gg/grain/KernelDispatch.h"

#include <atomic>
#include <cstdlib>

namespace gg::kernels
{
namespace
{
constexpr Dispatch kSse{
    &renderSampleSimd<Hermite>,
    &renderSampleSimd<Linear>,
    &renderSampleSimd<Sinc>,
    &renderLiveSimd<Hermite>,
    &renderLiveSimd<Linear>,
    &renderLiveSimd<Sinc>,
    4,
};

#if GG_KERNELS_AVX2
struct CpuidRegs
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

// cpuid at (leaf, subleaf), in GCC's inline assembly, which ClangCL takes too.
CpuidRegs cpuid(uint32_t leaf, uint32_t subleaf) noexcept
{
    CpuidRegs r{0, 0, 0, 0};
    __asm__ volatile("cpuid"
                     : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx)
                     : "a"(leaf), "c"(subleaf));
    return r;
}

// XCR0, the OS-enabled state components: valid only once cpuid has reported OSXSAVE.
uint64_t xcr0() noexcept
{
    uint32_t lo = 0;
    uint32_t hi = 0;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
#endif

// dispatch()'s table, resolved once, on the first call. An atomic, so forceSse() may switch it
// while another thread reads it.
std::atomic<const Dispatch *> &current() noexcept
{
    static std::atomic<const Dispatch *> table{
        &resolveKernels(std::getenv("GG_FORCE_SSE"), cpuRunsAvx2())};
    return table;
}
} // namespace

const Dispatch &sseKernels() noexcept { return kSse; }

bool cpuRunsAvx2() noexcept
{
#if GG_KERNELS_AVX2
    if (cpuid(0, 0).eax < 7)
    {
        return false;
    }
    const uint32_t ecx1 = cpuid(1, 0).ecx;
    constexpr uint32_t kOsxsave = 1u << 27;
    constexpr uint32_t kAvx = 1u << 28;
    if ((ecx1 & (kOsxsave | kAvx)) != (kOsxsave | kAvx))
    {
        return false;
    }
    constexpr uint64_t kXmmYmm = 0x6; // XCR0 bits 1 (SSE state) and 2 (AVX state)
    if ((xcr0() & kXmmYmm) != kXmmYmm)
    {
        return false;
    }
    constexpr uint32_t kAvx2 = 1u << 5; // leaf 7, subleaf 0, EBX
    return (cpuid(7, 0).ebx & kAvx2) != 0;
#else
    return false;
#endif
}

const Dispatch *avx2Kernels() noexcept
{
#if GG_KERNELS_AVX2
    return cpuRunsAvx2() ? &detail::avx2Table() : nullptr;
#else
    return nullptr;
#endif
}

const Dispatch &resolveKernels(const char *forceSse, bool runsAvx2) noexcept
{
    if (forceSse != nullptr && *forceSse != '\0')
    {
        return kSse;
    }
#if GG_KERNELS_AVX2
    if (runsAvx2)
    {
        return detail::avx2Table();
    }
#else
    (void)runsAvx2;
#endif
    return kSse;
}

const Dispatch &dispatch() noexcept { return *current().load(std::memory_order_acquire); }

void forceSse() noexcept { current().store(&kSse, std::memory_order_release); }
} // namespace gg::kernels
