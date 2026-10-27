// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_DENORMALGUARD_H
#define GG_CORE_DENORMALGUARD_H

#include <cstdint>

// Each slice of a universal macOS build is compiled separately, so exactly one branch applies.
#if defined(__aarch64__) || defined(_M_ARM64)
#if defined(__GNUC__) || defined(__clang__)
#define GG_DENORMAL_GUARD_AARCH64 1
#endif
#elif defined(__x86_64__) || defined(_M_X64) || defined(__SSE2__)
#include <xmmintrin.h>
#define GG_DENORMAL_GUARD_X86 1
#endif

namespace gg
{
/// While alive, flushes denormal floats and doubles to zero on the calling thread, so decaying
/// grain tails don't fall onto the slow denormal path. The destructor restores the previous
/// mode.
///
/// - x86-64: sets MXCSR FTZ and DAZ (0x8040).
/// - AArch64: sets FPCR.FZ (bit 24).
/// - Anything else: does nothing.
class ScopedDenormalsOff
{
  public:
    ScopedDenormalsOff() noexcept
    {
#if defined(GG_DENORMAL_GUARD_AARCH64)
        uint64_t fpcr;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr)::"memory");
        saved_ = fpcr;
        fpcr |= uint64_t{1} << 24;
        __asm__ __volatile__("msr fpcr, %0" ::"r"(fpcr) : "memory");
#elif defined(GG_DENORMAL_GUARD_X86)
        const unsigned int csr = _mm_getcsr();
        saved_ = csr;
        _mm_setcsr(csr | 0x8040u);
#endif
    }

    ~ScopedDenormalsOff() noexcept
    {
#if defined(GG_DENORMAL_GUARD_AARCH64)
        __asm__ __volatile__("msr fpcr, %0" ::"r"(saved_) : "memory");
#elif defined(GG_DENORMAL_GUARD_X86)
        _mm_setcsr(static_cast<unsigned int>(saved_));
#endif
    }

    ScopedDenormalsOff(const ScopedDenormalsOff &) = delete;
    ScopedDenormalsOff &operator=(const ScopedDenormalsOff &) = delete;

  private:
    [[maybe_unused]] uint64_t saved_{0};
};
} // namespace gg

#undef GG_DENORMAL_GUARD_AARCH64
#undef GG_DENORMAL_GUARD_X86

#endif // GG_CORE_DENORMALGUARD_H
