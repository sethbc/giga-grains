# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-audiometrics: the metrics golden renders are compared by (INTENT §19), plus audio-file I/O
# and spectrogram images. Used by gg-render and the tests, never by the plugins.

add_library(gg-audiometrics STATIC
        src/Metrics.cpp
        src/Spectrogram.cpp
        src/WavIO.cpp
        )
add_library(gg::audiometrics ALIAS gg-audiometrics)

target_include_directories(gg-audiometrics PUBLIC include)
target_link_libraries(gg-audiometrics
        PUBLIC gg::compiler-options
        PRIVATE gg::pffft gg::dr_libs gg::stb nlohmann_json::nlohmann_json fmt::fmt
        )
