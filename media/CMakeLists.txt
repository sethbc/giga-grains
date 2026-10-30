# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-media: sample-file I/O, without JUCE or tinyxml: decoding WAV, AIFF/AIFC, FLAC and MP3 with
# the pinned dr_libs (gg/media/Decode.h), sample-rate conversion with the pinned r8brain-free-src
# (gg/media/Convert.h), the file fingerprint, an in-house SHA-256 (gg/media/Sha256.h), loading a
# sample with its manifest entry (gg/media/Sample.h), the search for a missing one
# (gg/media/Search.h), the per-machine settings file (gg/media/SettingsFile.h) and the float32
# WAV bytes of embedded and filed captures (gg/media/WavBytes.h). Shared by the plugins' loader
# and the tools; never on the audio thread.

add_library(gg-media STATIC
        src/Convert.cpp
        src/Decode.cpp
        src/Sample.cpp
        src/Search.cpp
        src/SettingsFile.cpp
        src/Sha256.cpp
        src/WavBytes.cpp
        )
add_library(gg::media ALIAS gg-media)

target_include_directories(gg-media PUBLIC include PRIVATE src)
# PUBLIC: Decode.h returns gg::SampleBuffer, and gg::engine carries our compiler options to
# whatever links gg-media. gg::core gives the manifest's MediaEntry and the settings' codec
# (gg/core/SessionState.h, gg/core/MachineSettings.h), which the public headers use; gg-core never
# links gg-media, and its tinyxml stays private to it (ruling M1-l). dr_libs', r8brain's and
# utf8proc's headers stay private: the public headers don't include them. dr_libs'
# implementations are compiled once, in gg::dr_libs_impl; r8brain is header-only; utf8proc, the
# search's Unicode normalization, is a static C library. The tests reach src/TestHooks.h.
target_link_libraries(gg-media
        PUBLIC gg::core gg::engine
        PRIVATE gg::dr_libs gg::dr_libs_impl gg::r8brain gg::utf8proc gg::compiler-options
        )
