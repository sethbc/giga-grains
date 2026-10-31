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

# The pin's tag and zip SHA-256, for content-version.txt (below), and a configure that re-runs when
# the pin changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GG_CONTENT_PIN})
file(STRINGS ${GG_CONTENT_PIN} gg_pin_lines)
set(GG_CONTENT_TAG "")
set(GG_CONTENT_ZIP_SHA256 "")
foreach (line IN LISTS gg_pin_lines)
    if (line MATCHES "^tag +([^ ]+)")
        set(GG_CONTENT_TAG "${CMAKE_MATCH_1}")
    elseif (line MATCHES "^sha256 +([0-9a-f]+)")
        set(GG_CONTENT_ZIP_SHA256 "${CMAKE_MATCH_1}")
    endif ()
endforeach ()
if (NOT GG_CONTENT_TAG OR NOT GG_CONTENT_ZIP_SHA256)
    message(FATAL_ERROR "${GG_CONTENT_PIN} has no tag or sha256 line")
endif ()

# gg-presets (M2 sub-project 5's spec §6.11): packs every preset source,
# content/presets/<Product>/<name>.json, into
# ${CMAKE_BINARY_DIR}/content/Presets/<Product>/<name>.ggpreset with gg-render --pack-preset, which
# gg-products stages into the installers' Content component. <Product> is "Giga Grains" or
# "Giga Grains FX"; a product folder with no source (the repository has none until the factory
# presets are written) packs nothing, and both output folders exist all the same. Only these
# folders are packed: tests/fixtures/presets/ holds test sources, not factory content. The sources
# are globbed at configure (CONFIGURE_DEPENDS re-runs it when one comes or goes), and an output
# whose source is gone is deleted then, so it can't reach the installers.
set(GG_PRESETS_DIR ${CMAKE_BINARY_DIR}/content/Presets)
set(gg_preset_outputs "")
foreach (product IN ITEMS "Giga Grains" "Giga Grains FX")
    file(MAKE_DIRECTORY "${GG_PRESETS_DIR}/${product}")
    file(GLOB gg_preset_sources CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/content/presets/${product}/*.json")
    list(SORT gg_preset_sources)
    set(gg_product_outputs "")
    foreach (source IN LISTS gg_preset_sources)
        get_filename_component(stem "${source}" NAME_WLE)
        set(output "${GG_PRESETS_DIR}/${product}/${stem}.ggpreset")
        add_custom_command(OUTPUT "${output}"
                COMMAND gg-render --pack-preset "${source}" -o "${output}"
                --content-dir "${GG_CONTENT_ROOT}"
                DEPENDS gg-render "${source}"
                COMMENT "Packing the preset ${product}/${stem}"
                VERBATIM)
        list(APPEND gg_product_outputs "${output}")
    endforeach ()
    file(GLOB gg_stale "${GG_PRESETS_DIR}/${product}/*")
    foreach (file IN LISTS gg_stale)
        if (NOT file IN_LIST gg_product_outputs)
            file(REMOVE_RECURSE "${file}")
        endif ()
    endforeach ()
    list(APPEND gg_preset_outputs ${gg_product_outputs})
endforeach ()
add_custom_target(gg-presets DEPENDS ${gg_preset_outputs})
# A preset may name content samples (gg-render checks them against the content folder).
add_dependencies(gg-presets gg-content)
