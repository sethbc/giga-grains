# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-engine: the framework-free grain engine shared by both plugins, the tools and the tests.

add_library(gg-engine STATIC
        src/CloudScheduler.cpp
        src/Engine.cpp
        src/GrainKernelsAvx2.cpp
        src/GrainPool.cpp
        src/KernelDispatch.cpp
        src/LiveSource.cpp
        src/Mipmap.cpp
        src/PreparedSource.cpp
        src/SampleBuffer.cpp
        src/SpectralSource.cpp
        src/SincTable.cpp
        src/Version.cpp
        src/Window.cpp
        )
add_library(gg::engine ALIAS gg-engine)
# GrainKernelsAvx2.cpp, the AVX2 spike's 8-wide kernels (M2 engine spec §6.12), is compiled with the
# project's flags like every other source: its functions carry __attribute__((target("avx2")))
# (AVX2 without FMA) themselves, and the file is empty except on x86-64 (GG_KERNELS_AVX2), so a
# universal macOS build has them in its x86_64 slice alone. No per-file -mavx2: that would compile
# every inline function and template the file instantiates from the shared headers (the
# references, the run finders, std::) with AVX2 too, and the linker keeps one copy of each, which
# may then be the AVX2 one for every caller, on a CPU without AVX2.

target_include_directories(gg-engine PUBLIC include)
# PUBLIC so plugin and UI targets can use them directly: GG_VERSION_STRING is the full version
# ("0.2.0-dev"), GG_VERSION_NUMERIC its numeric part and GG_VERSION_PRERELEASE its suffix, which
# is empty for a release (the top-level CMakeLists.txt, spec §6.1).
target_compile_definitions(gg-engine PUBLIC
        GG_VERSION_STRING="${GG_VERSION_STRING}"
        GG_VERSION_NUMERIC="${GG_VERSION_NUMERIC}"
        GG_VERSION_PRERELEASE="${GG_VERSION_PRERELEASE}")
# sst-cpputils is PUBLIC: Engine.h uses its SST_CPPUTILS_NONBLOCKING annotation. sst-basic-blocks
# and simde are PUBLIC: GrainKernels.h's SIMD kernels include <sst/basic-blocks/simd/setup.h>.
target_link_libraries(gg-engine PUBLIC gg::compiler-options sst-cpputils sst-basic-blocks simde)
# pffft is PRIVATE: only SpectralSource.cpp includes it (the header forward-declares its setup).
target_link_libraries(gg-engine PRIVATE gg::pffft)
