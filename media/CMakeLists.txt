# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-media: sample-file I/O, without JUCE or tinyxml: decoding WAV, AIFF/AIFC, FLAC and MP3 with
# the pinned dr_libs (gg/media/Decode.h), sample-rate conversion with the pinned r8brain-free-src
# (gg/media/Convert.h) and the file fingerprint, an in-house SHA-256 (gg/media/Sha256.h). Shared
# by the plugins' loader and the tools; never on the audio thread.

add_library(gg-media STATIC
        src/Convert.cpp
        src/Decode.cpp
        src/Sha256.cpp
        )
add_library(gg::media ALIAS gg-media)

target_include_directories(gg-media PUBLIC include)
# PUBLIC: Decode.h returns gg::SampleBuffer, and gg::engine carries our compiler options to
# whatever links gg-media. dr_libs' and r8brain's headers stay private: the public headers don't
# include them. dr_libs' implementations are compiled once, in gg::dr_libs_impl; r8brain is
# header-only.
target_link_libraries(gg-media
        PUBLIC gg::engine
        PRIVATE gg::dr_libs gg::dr_libs_impl gg::r8brain gg::compiler-options
        )
