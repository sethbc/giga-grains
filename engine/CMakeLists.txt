# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-engine: the framework-free grain engine shared by both plugins, the tools and the tests.

add_library(gg-engine STATIC
        src/Engine.cpp
        src/GrainPool.cpp
        src/PeriodicStream.cpp
        src/SampleBuffer.cpp
        src/Version.cpp
        src/Window.cpp
        )
add_library(gg::engine ALIAS gg-engine)

target_include_directories(gg-engine PUBLIC include)
# PUBLIC so plugin and UI targets can use GG_VERSION_STRING directly.
target_compile_definitions(gg-engine PUBLIC GG_VERSION_STRING="${PROJECT_VERSION}")
# sst-cpputils is PUBLIC: Engine.h uses its SST_CPPUTILS_NONBLOCKING annotation.
target_link_libraries(gg-engine PUBLIC gg::compiler-options sst-cpputils)
