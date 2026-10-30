# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-engine: the framework-free grain engine shared by both plugins, the tools and the tests.

add_library(gg-engine STATIC
        src/CloudScheduler.cpp
        src/Engine.cpp
        src/GrainPool.cpp
        src/LiveSource.cpp
        src/PreparedSource.cpp
        src/RingStorage.cpp
        src/SampleBuffer.cpp
        src/Version.cpp
        src/Window.cpp
        )
add_library(gg::engine ALIAS gg-engine)

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
