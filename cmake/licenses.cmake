# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# The license set (spec §6.3 and §6.10, carry-over row 221), in one list that two readers share,
# so the About box can't drift from the installers:
# - cmake/products.cmake copies it into gg-products/licenses/, which every installer carries;
# - gg_add_notices embeds the same files into the editor, whose About box shows them.
#
# Included from the top-level CMakeLists.txt before any directory that uses it. The functions
# read AudioUnitSDK's location, which is known only once clients/clap-first has been processed,
# so call them from cmake/products.cmake (included after it), not from ui/.

set(GG_EMBED_TEXT_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/embed-text.cmake)

# gg_license_files(<out_files> <out_names>): the license texts' source paths and, paired with
# them, the names gg-products publishes them under, in the installer's order:
#   THIRD_PARTY_NOTICES.md, LICENSE (the AGPL), GPL-3.0.txt (sst-basic-blocks' LICENSE.md is the
#   GPLv3 text), CMakeRC-LICENSE.txt and flowbite-icons-LICENSE.txt (MIT texts no checkout holds,
#   kept in installer/licenses/), AudioUnitSDK-LICENSE.txt (macOS, the Apache 2.0 text of the SDK
#   checkout clap-wrapper builds the AU from) and ASIO-SDK-LICENSE.txt (Windows, the ASIO SDK's);
#   CC0-1.0.txt (every platform; the CC0 1.0 legal code of the factory content, kept in
#   installer/licenses/ since no checkout holds a standalone copy).
# Every file is checked at configure time, and the two lists must pair up exactly.
function(gg_license_files out_files out_names)
    set(files
            ${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
            ${CMAKE_SOURCE_DIR}/LICENSE
            ${CMAKE_SOURCE_DIR}/libs/sst/sst-basic-blocks/LICENSE.md
            ${CMAKE_SOURCE_DIR}/installer/licenses/CMakeRC-LICENSE.txt
            ${CMAKE_SOURCE_DIR}/installer/licenses/flowbite-icons-LICENSE.txt
            ${CMAKE_SOURCE_DIR}/installer/licenses/CC0-1.0.txt)
    set(names THIRD_PARTY_NOTICES.md LICENSE GPL-3.0.txt CMakeRC-LICENSE.txt
            flowbite-icons-LICENSE.txt CC0-1.0.txt)
    if (APPLE)
        # CI passes AUDIOUNIT_SDK_ROOT (scripts/fetch-sdks.sh); a local tree has CPM's checkout,
        # recorded in the cache when clients/clap-first was processed, before this is called.
        if (NOT "${AUDIOUNIT_SDK_ROOT}" STREQUAL "")
            set(au_license "${AUDIOUNIT_SDK_ROOT}/LICENSE.txt")
        else ()
            set(au_license "${CPM_PACKAGE_AudioUnitSDK_SOURCE_DIR}/LICENSE.txt")
        endif ()
        if (NOT EXISTS "${au_license}")
            message(FATAL_ERROR "gg_license_files: no AudioUnitSDK LICENSE.txt at '${au_license}'; "
                    "neither AUDIOUNIT_SDK_ROOT ('${AUDIOUNIT_SDK_ROOT}') nor "
                    "CPM_PACKAGE_AudioUnitSDK_SOURCE_DIR ('${CPM_PACKAGE_AudioUnitSDK_SOURCE_DIR}') "
                    "names an SDK checkout that has one")
        endif ()
        list(APPEND files "${au_license}")
        list(APPEND names AudioUnitSDK-LICENSE.txt)
    elseif (WIN32)
        list(APPEND files ${CMAKE_SOURCE_DIR}/libs/sst/sst-plugininfra/libs/asiosdk/LICENSE.txt)
        list(APPEND names ASIO-SDK-LICENSE.txt)
    endif ()

    # ZIP_LISTS pads the shorter list with empty items, so a name without its file (or a file
    # without its name) would slip through as an empty one: the two lists must pair up exactly.
    list(LENGTH files file_count)
    list(LENGTH names name_count)
    if (NOT file_count EQUAL name_count)
        message(FATAL_ERROR "gg_license_files: ${file_count} license texts but ${name_count} "
                "names; each license text needs one name")
    endif ()
    foreach (source name IN ZIP_LISTS files names)
        if (NOT EXISTS "${source}")
            message(FATAL_ERROR "gg_license_files: the license text '${source}' (${name}) "
                    "doesn't exist")
        endif ()
    endforeach ()

    set(${out_files} "${files}" PARENT_SCOPE)
    set(${out_names} "${names}" PARENT_SCOPE)
endfunction()

# gg_add_notices(<target>): compiles the license set into <target> as gg::ui::noticeTexts()
# (gg/ui/Notices.h). embed-text.cmake writes ${CMAKE_BINARY_DIR}/generated/Notices.cpp at build
# time, not configure time, so editing a license text rebuilds it without a re-configure: the
# command depends on every text and on the script. The list itself travels on the command line,
# so a change to it (a new SDK checkout, say) changes the command, which Ninja reruns too.
#
# The command is declared here but the target may live in another directory, whose generator
# wouldn't see the rule: CMake attaches a custom command only to targets of its own directory.
# So a custom target in this directory owns the rule, and <target> depends on it, the pattern
# CMake documents for a generated source used across directories.
function(gg_add_notices target)
    gg_license_files(files names)
    set(out ${CMAKE_BINARY_DIR}/generated/Notices.cpp)
    add_custom_command(OUTPUT ${out}
            COMMAND ${CMAKE_COMMAND} "-DOUT=${out}" "-DFILES=${files}" "-DNAMES=${names}"
            -P ${GG_EMBED_TEXT_SCRIPT}
            DEPENDS ${files} ${GG_EMBED_TEXT_SCRIPT}
            COMMENT "Embedding the license set into ${target} (Notices.cpp)"
            VERBATIM
            )
    add_custom_target(${target}-notices DEPENDS ${out})
    add_dependencies(${target} ${target}-notices)
    # CMP0118 (NEW from 3.20) makes GENERATED visible in every directory; say so explicitly for
    # the target's own directory too, so its generate step never looks for the file on disk.
    set_source_files_properties(${out} TARGET_DIRECTORY ${target} PROPERTIES GENERATED TRUE)
    target_sources(${target} PRIVATE ${out})
endfunction()
