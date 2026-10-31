# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-snap: the editor's views rendered to PNGs (M2 sub-project 6's spec §6.13), headless, through
# the plugins' CLAP factory and JUCE's createComponentSnapshot. It needs the editor, so it is built
# only with GG_BUILD_EDITOR. CI's mac-universal leg uploads its output as the gg-snapshots artifact.

add_executable(gg-snap main.cpp)
# The same libraries and private include directories the editor's tests use
# (tests/CMakeLists.txt): the plugin's own headers (Factory.h, editor/EditorPlugin.h) are private.
target_include_directories(gg-snap PRIVATE ${CMAKE_SOURCE_DIR}/plugin/src)
target_link_libraries(gg-snap PRIVATE gg::plugin-impl gg-plugin-core gg::ui sst-jucegui
        gg::juce-headers gg::compiler-options)
