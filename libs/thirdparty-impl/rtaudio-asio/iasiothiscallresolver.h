// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// RtAudio's ASIO backend, built against the Steinberg ASIO SDK 2.3.4 that sst-plugininfra
// bundles, used under its GPLv3 option (libs/CMakeLists.txt; INTENT §15, §23 #56). Windows
// x64 only. RtAudio's own include/ directory, whose Steinberg ASIO files have no license grant,
// is never on the include path, so this header stands in for the one RtAudio.cpp's ASIO
// section includes after asio.h. It is our own code, and RtAudio.cpp is the only file that
// includes it.
//
// - RtAudio's iasiothiscallresolver.h (Ross Bencina's) adapts the IASIO calls for 32-bit
//   compilers without the thiscall convention. Under a compiler that defines _MSC_VER
//   (MSVC, ClangCL), or on x64, where there is one calling convention, it compiles to nothing.
//   We build only x64 with ClangCL, so this header needs none of it.
// - The SDK's ASIOExit() (common/asio.cpp) unloads the driver through the global
//   `asioDrivers`, the AsioDrivers a host makes with loadAsioDriver(). RtAudio never calls
//   that: it keeps its own AsioDrivers, file-static in RtAudio.cpp, and leaves `asioDrivers`
//   null, so the SDK's ASIOExit() would dereference null. RtAudio's bundled copy of asio.cpp
//   adds a null check; the SDK's has none, and we compile the SDK's unmodified. So
//   `asioDrivers` points, from static initialization on, at an AsioDrivers that never loads
//   a driver: ASIOExit()'s removeCurrentDriver() on it does nothing (its curIndex stays -1),
//   and RtAudio unloads the driver itself through its own AsioDrivers, right after each
//   ASIOExit(), as it does with its patched copy.

#ifndef GG_RTAUDIO_ASIO_IASIOTHISCALLRESOLVER_H
#define GG_RTAUDIO_ASIO_IASIOTHISCALLRESOLVER_H

#if !defined(_MSC_VER) || !defined(_WIN64)
#error "RtAudio's ASIO is built only for Windows x64 with MSVC or ClangCL (no thiscall resolver)"
#endif

#if !defined(__ASIO_H)
#error "iasiothiscallresolver.h must be included after asio.h, as RtAudio.cpp does"
#endif

#include "asiodrivers.h"

extern AsioDrivers *asioDrivers;

// An AsioDrivers with no driver loaded, for the SDK's ASIOExit() (see above). Constructed
// during static initialization on the main thread, as RtAudio's own AsioDrivers is.
static AsioDrivers ggIdleAsioDrivers;
[[maybe_unused]] static const bool ggIdleAsioDriversInstalled = [] {
    if (!asioDrivers)
        asioDrivers = &ggIdleAsioDrivers;
    return true;
}();

#endif
