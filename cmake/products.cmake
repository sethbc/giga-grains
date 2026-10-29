# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-products: gathers every built plugin format into one directory, ${CMAKE_BINARY_DIR}/gg-products
# (build/<preset>/gg-products/), for validation and packaging:
#   Giga Grains.clap, Giga Grains.vst3, Giga Grains.component (macOS),
#   the standalones Giga Grains / Giga Grains FX (.app on macOS, .exe on Windows),
#   and licenses/, the notices and license texts every installer carries (below).
# Adapted from Shortcircuit XT's cmake/basic-installer.cmake.

set(GG_PRODUCT_DIR ${CMAKE_BINARY_DIR}/gg-products)

add_custom_target(gg-products)

# gg_add_product(<format target postfix> <file suffix>)
function(gg_add_product format suffix)
    set(target gg_clapfirst_${format})
    if (NOT TARGET ${target})
        return()
    endif ()

    get_target_property(output_name ${target} LIBRARY_OUTPUT_NAME)
    if (NOT output_name)
        get_target_property(output_name ${target} OUTPUT_NAME)
    endif ()

    add_dependencies(gg-products ${target})

    # On macOS every format is a bundle; elsewhere only the VST3 is (a folder, as
    # WINDOWS_FOLDER_VST3 asks). A bundle's binary sits three levels below its root
    # (<name><suffix>/Contents/<arch or MacOS>/<binary>).
    set(is_bundle FALSE)
    if (APPLE OR "${suffix}" STREQUAL ".vst3")
        set(is_bundle TRUE)
    endif ()

    message(STATUS "gg-products: ${target} -> '${output_name}${suffix}'")
    set(dest "${GG_PRODUCT_DIR}/${output_name}${suffix}")
    if (is_bundle)
        add_custom_command(TARGET gg-products POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E echo "gg-products: ${output_name}${suffix}"
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${dest}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<TARGET_FILE_DIR:${target}>/../../../${output_name}${suffix}" "${dest}"
                VERBATIM
                )
    else ()
        add_custom_command(TARGET gg-products POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E echo "gg-products: $<TARGET_FILE_NAME:${target}>"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${GG_PRODUCT_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${target}>" "${GG_PRODUCT_DIR}/"
                VERBATIM
                )
    endif ()
endfunction()

gg_add_product(clap ".clap")
gg_add_product(vst3 ".vst3")
gg_add_product(auv2 ".component")
if (APPLE)
    gg_add_product(standalone ".app")
    gg_add_product(standalone_fx ".app")
elseif (WIN32)
    gg_add_product(standalone ".exe")
    gg_add_product(standalone_fx ".exe")
else ()
    gg_add_product(standalone "")
    gg_add_product(standalone_fx "")
endif ()

# The license set (spec §6.3, carry-over row 221): every installer carries THIRD_PARTY_NOTICES.md
# and the license texts it points to, which gg-products writes into GG_LICENSE_DIR
# (build/<preset>/gg-products/licenses/) under these published names:
#   THIRD_PARTY_NOTICES.md, LICENSE (the AGPL), GPL-3.0.txt (sst-basic-blocks' LICENSE.md is the
#   GPLv3 text), CMakeRC-LICENSE.txt and flowbite-icons-LICENSE.txt (MIT texts no checkout holds,
#   kept in installer/licenses/), AudioUnitSDK-LICENSE.txt (macOS, the Apache 2.0 text of the SDK
#   checkout clap-wrapper builds the AU from) and ASIO-SDK-LICENSE.txt (Windows, the ASIO SDK's).
# GG_LICENSE_FILES is the list of their source paths, each checked here at configure time.
set(GG_LICENSE_DIR ${GG_PRODUCT_DIR}/licenses)
set(GG_LICENSE_FILES
        ${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
        ${CMAKE_SOURCE_DIR}/LICENSE
        ${CMAKE_SOURCE_DIR}/libs/sst/sst-basic-blocks/LICENSE.md
        ${CMAKE_SOURCE_DIR}/installer/licenses/CMakeRC-LICENSE.txt
        ${CMAKE_SOURCE_DIR}/installer/licenses/flowbite-icons-LICENSE.txt)
set(gg_license_names THIRD_PARTY_NOTICES.md LICENSE GPL-3.0.txt CMakeRC-LICENSE.txt
        flowbite-icons-LICENSE.txt)
if (APPLE)
    # CI passes AUDIOUNIT_SDK_ROOT (scripts/fetch-sdks.sh); a local tree has CPM's checkout,
    # recorded in the cache when clients/clap-first was processed, before this file.
    if (NOT "${AUDIOUNIT_SDK_ROOT}" STREQUAL "")
        set(gg_au_license "${AUDIOUNIT_SDK_ROOT}/LICENSE.txt")
    else ()
        set(gg_au_license "${CPM_PACKAGE_AudioUnitSDK_SOURCE_DIR}/LICENSE.txt")
    endif ()
    if (NOT EXISTS "${gg_au_license}")
        message(FATAL_ERROR "gg-products: no AudioUnitSDK LICENSE.txt at '${gg_au_license}'; "
                "neither AUDIOUNIT_SDK_ROOT ('${AUDIOUNIT_SDK_ROOT}') nor "
                "CPM_PACKAGE_AudioUnitSDK_SOURCE_DIR ('${CPM_PACKAGE_AudioUnitSDK_SOURCE_DIR}') "
                "names an SDK checkout that has one")
    endif ()
    list(APPEND GG_LICENSE_FILES "${gg_au_license}")
    list(APPEND gg_license_names AudioUnitSDK-LICENSE.txt)
elseif (WIN32)
    list(APPEND GG_LICENSE_FILES
            ${CMAKE_SOURCE_DIR}/libs/sst/sst-plugininfra/libs/asiosdk/LICENSE.txt)
    list(APPEND gg_license_names ASIO-SDK-LICENSE.txt)
endif ()

add_custom_command(TARGET gg-products POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "gg-products: licenses"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${GG_LICENSE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GG_LICENSE_DIR}"
        VERBATIM
        )
foreach (source name IN ZIP_LISTS GG_LICENSE_FILES gg_license_names)
    if (NOT EXISTS "${source}")
        message(FATAL_ERROR "gg-products: the license text '${source}' (${name}) doesn't exist")
    endif ()
    add_custom_command(TARGET gg-products POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${source}" "${GG_LICENSE_DIR}/${name}"
            VERBATIM
            )
endforeach ()

# gg-installer (spec §6.3): builds gg-products, then runs the platform's installer script on them,
# writing into GG_INSTALLER_DIR (build/<preset>/gg-installer/). The script reads the signing
# environment itself (installer/macos/make-installer.sh says which variables), so the same target
# makes a local unsigned installer and CI's signed one.
set(GG_INSTALLER_DIR ${CMAKE_BINARY_DIR}/gg-installer)
add_custom_target(gg-installer)
add_dependencies(gg-installer gg-products)
if (APPLE)
    # giga-grains-<v>-macos.pkg and giga-grains-<v>-macos.dmg.
    add_custom_command(TARGET gg-installer POST_BUILD
            COMMAND ${CMAKE_SOURCE_DIR}/installer/macos/make-installer.sh
            --products ${GG_PRODUCT_DIR} --out ${GG_INSTALLER_DIR}
            --version ${GG_VERSION_NUMERIC} --full-version ${GG_VERSION_STRING}
            --resources ${CMAKE_SOURCE_DIR}/installer/macos --licenses ${GG_LICENSE_DIR}
            VERBATIM
            )
    # elseif (UNIX): the Linux tarball (Task 7). elseif (WIN32): the Windows installer and zip
    # (Task 6).
endif ()
