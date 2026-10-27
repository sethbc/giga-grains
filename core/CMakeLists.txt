# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-core: what both plugins share above the engine, without JUCE or a plugin format: the
# parameters (gg/core/ParamIds.h), the saved state (gg/core/PatchState.h), the parameter mapping
# (gg/core/ParamMap.h) and the Processor that runs the engine and the wet chain
# (gg/core/Processor.h). Built in every preset, the sanitizer presets included. The engine stays
# framework-free: sst-plugininfra, tinyxml and CLAP's headers come in here, never in gg-engine.

add_library(gg-core STATIC
        src/ParamIds.cpp
        src/ParamMap.cpp
        src/Patch.cpp
        src/PatchState.cpp
        src/Processor.cpp
        )
add_library(gg::core ALIAS gg-core)

target_include_directories(gg-core PUBLIC include PRIVATE src)
# PUBLIC: ParamIds.h uses CLAP's clap_id, and paramMeta returns sst-basic-blocks' ParamMetaData,
# whose header formats with fmt. gg::engine carries our compiler options to whatever links
# gg-core.
#
# PRIVATE: sst-plugininfra's patchbase, and with it tinyxml, whose consumers compile with char8_t
# off (-fno-char8_t, MSVC /Zc:char8_t-). Only gg-core's sources include them, through the
# internal src/Patch.h; the public headers don't, so the flag never reaches what links gg-core
# (ruling M1-l). Linking statically, consumers still link tinyxml.
target_link_libraries(gg-core
        PUBLIC gg::engine sst-basic-blocks fmt::fmt clap
        PRIVATE sst-plugininfra::patchbase gg::compiler-options
        )
