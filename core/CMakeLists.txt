# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-core: what both plugins share above the engine, without JUCE or a plugin format. For now the
# patch: the parameters, their metadata and the saved state. Built in every preset, the sanitizer
# presets included. The engine stays framework-free: sst-plugininfra, tinyxml and CLAP's headers
# come in here, never in gg-engine.

add_library(gg-core STATIC
        src/ParamIds.cpp
        src/Patch.cpp
        )
add_library(gg::core ALIAS gg-core)

target_include_directories(gg-core PUBLIC include)
# PUBLIC: Patch.h derives from sst-plugininfra's PatchBase, whose header brings in tinyxml, CLAP's
# headers and sst-basic-blocks' ParamMetaData, which formats with fmt. gg::engine carries our
# compiler options to whatever links gg-core.
target_link_libraries(gg-core
        PUBLIC gg::engine sst-plugininfra::patchbase fmt::fmt
        PRIVATE gg::compiler-options
        )
