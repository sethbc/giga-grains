# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-plugin-impl: both CLAP plugins (Giga Grains and Giga Grains FX) and their factory, as a
# static library. clients/clap-first links it into every format (CLAP, VST3, AUv2,
# standalone); the tests link it directly.

add_library(gg-plugin-impl STATIC
        src/Factory.cpp
        src/GigaGrainsPlugin.cpp
        src/HelpersConfig.cpp
        )
add_library(gg::plugin-impl ALIAS gg-plugin-impl)

target_include_directories(gg-plugin-impl PUBLIC include PRIVATE src)

# Only header-only interfaces are PUBLIC. The engine and our compiler options stay PRIVATE so
# they do not reach clap-wrapper's own sources, which make_clapfirst_plugins compiles into the
# same format targets that link this library.
target_link_libraries(gg-plugin-impl
        PUBLIC clap clap-helpers clap-wrapper-extensions
        PRIVATE gg::engine gg::compiler-options
        )
