// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The RTSan build's negative control (built only with GG_USE_RTSAN): it allocates inside a
// SST_CPPUTILS_NONBLOCKING function, which RealtimeSanitizer must report before it stops the
// program. The rtsan_negative_control CTest passes only when that report appears, so the RTSan
// job can't pass vacuously with the sanitizer silently missing from the build. Nothing this
// program prints itself may contain the sanitizer's name.

#include <cstdio>

#include <sst/cpputils/rtsan_support.h>

namespace
{
int allocateInRealtimeContext() noexcept SST_CPPUTILS_NONBLOCKING
{
    // volatile, so the optimizer can't elide the new/delete pair.
    int *volatile p = new int(42);
    const int value = *p;
    delete p;
    return value;
}
} // namespace

int main()
{
    const int value = allocateInRealtimeContext();
    std::printf(
        "negative control: allocated %d in a nonblocking function, and nothing stopped it\n",
        value);
    return 0;
}
