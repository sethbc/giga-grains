# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# The content (spec §6.5): the CC0 samples the factory presets load, released as a zip in
# https://github.com/sethbc/giga-grains-content and pinned by content/pin.txt (tag, URL,
# SHA-256). Nothing is downloaded at configure time.
#
# - GG_CONTENT_DIR (cache PATH, empty by default) names a content folder fetched elsewhere, at
#   the pin: CI passes its cache's, filled by scripts/fetch-content.sh.
# - GG_CONTENT_ROOT is the folder the build reads: GG_CONTENT_DIR when it is set, otherwise
#   <build>/content-cache/content.
# - The gg-content target fetches the pinned zip into <build>/content-cache once (a tree already
#   at the pin costs no network), or, with GG_CONTENT_DIR set, checks that folder's .gg-pin
#   against the pin and fails naming both. The content_fixture test (tests/CMakeLists.txt) runs
#   it as the fixture `content`.

set(GG_CONTENT_DIR "" CACHE PATH
        "A content folder at content/pin.txt's pin; empty fetches one into the build tree")
set(GG_CONTENT_PIN ${CMAKE_SOURCE_DIR}/content/pin.txt)
set(GG_CONTENT_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/fetch-content.cmake)

if (GG_CONTENT_DIR)
    # CI's Windows leg passes a path with backslashes; CMake's own form has forward slashes.
    file(TO_CMAKE_PATH "${GG_CONTENT_DIR}" GG_CONTENT_ROOT)
    get_filename_component(GG_CONTENT_ROOT "${GG_CONTENT_ROOT}" ABSOLUTE)
    add_custom_target(gg-content
            COMMAND ${CMAKE_COMMAND} -DCHECK=${GG_CONTENT_ROOT} -DPIN=${GG_CONTENT_PIN}
            -P ${GG_CONTENT_SCRIPT}
            COMMENT "Checking the content in ${GG_CONTENT_ROOT}"
            VERBATIM)
else ()
    set(GG_CONTENT_ROOT ${CMAKE_BINARY_DIR}/content-cache/content)
    add_custom_target(gg-content
            COMMAND ${CMAKE_COMMAND} -DDEST=${CMAKE_BINARY_DIR}/content-cache
            -DPIN=${GG_CONTENT_PIN} -P ${GG_CONTENT_SCRIPT}
            COMMENT "Fetching the content into ${CMAKE_BINARY_DIR}/content-cache"
            VERBATIM)
endif ()
