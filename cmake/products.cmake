# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-products: gathers every built plugin format into one directory, ${CMAKE_BINARY_DIR}/gg-products
# (build/<preset>/gg-products/), for validation and packaging:
#   Giga Grains.clap, Giga Grains.vst3, Giga Grains.component (macOS),
#   the standalones Giga Grains / Giga Grains FX (.app on macOS, .exe on Windows),
#   licenses/, the notices and license texts every installer carries (below), and content/, the
#   factory content the installers' Content component carries (below).
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
# (build/<preset>/gg-products/licenses/) under their published names. GG_LICENSE_FILES and
# gg_license_names, their source paths and those names, come from cmake/licenses.cmake, the one
# list the editor's About box embeds too (gg_add_notices, below), so the two can't drift.
set(GG_LICENSE_DIR ${GG_PRODUCT_DIR}/licenses)
gg_license_files(GG_LICENSE_FILES gg_license_names)

add_custom_command(TARGET gg-products POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "gg-products: licenses"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${GG_LICENSE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GG_LICENSE_DIR}"
        VERBATIM
        )
# gg_license_files has checked that the lists pair up and that every text exists.
foreach (source name IN ZIP_LISTS GG_LICENSE_FILES gg_license_names)
    add_custom_command(TARGET gg-products POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${source}" "${GG_LICENSE_DIR}/${name}"
            VERBATIM
            )
endforeach ()

# The editor's About box shows the same texts (spec §6.10). gg-ui is declared in ui/, before
# clients/clap-first has found AudioUnitSDK, so its notices are attached here, once the list is
# complete. This file is included only when the editor is built, as gg-ui is.
gg_add_notices(gg-ui)
# The content (M2 sub-project 5's spec §6.11): gg-products stages GG_CONTENT_STAGE_DIR
# (build/<preset>/gg-products/content/) from gg-presets' packed presets and the fetched release
# (GG_CONTENT_ROOT, cmake/content.cmake), so every installer's Content component carries it:
#   Presets/<Product>/<name>.ggpreset, Samples/, PROVENANCE.md, LICENSE-CC0-1.0.txt (the release's
#   LICENSE), README.md and content-version.txt (`giga-grains-content <tag> <zip sha256>`, the pin's).
# gg-products depends on gg-content, which fetches the pinned release or checks GG_CONTENT_DIR
# against the pin, and fails if it isn't there, so a tree without the pinned content builds no
# partial component; the copies below fail too on a missing file.
set(GG_CONTENT_STAGE_DIR ${GG_PRODUCT_DIR}/content)
file(WRITE ${CMAKE_BINARY_DIR}/content/content-version.txt
        "giga-grains-content ${GG_CONTENT_TAG} ${GG_CONTENT_ZIP_SHA256}\n")
add_dependencies(gg-products gg-content gg-presets)
add_custom_command(TARGET gg-products POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "gg-products: content"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${GG_CONTENT_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GG_CONTENT_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${GG_PRESETS_DIR}"
        "${GG_CONTENT_STAGE_DIR}/Presets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${GG_CONTENT_ROOT}/Samples"
        "${GG_CONTENT_STAGE_DIR}/Samples"
        COMMAND ${CMAKE_COMMAND} -E copy "${GG_CONTENT_ROOT}/PROVENANCE.md"
        "${GG_CONTENT_ROOT}/README.md" "${GG_CONTENT_STAGE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy "${GG_CONTENT_ROOT}/LICENSE"
        "${GG_CONTENT_STAGE_DIR}/LICENSE-CC0-1.0.txt"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/content/content-version.txt"
        "${GG_CONTENT_STAGE_DIR}/"
        VERBATIM
        )

# gg-installer (spec §6.3): builds gg-products, then runs the platform's installer script on them,
# writing into GG_INSTALLER_DIR (build/<preset>/gg-installer/). The script reads the signing
# environment itself (installer/macos/make-installer.sh says which variables), so the same target
# makes a local unsigned installer and CI's signed one. Each command is USES_TERMINAL: Ninja then
# runs it in the console pool and streams its output as it runs, instead of holding it until the
# command exits, so a long notarization shows its progress and a timed-out CI step shows how far
# the script got.
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
            --content ${GG_CONTENT_STAGE_DIR}
            VERBATIM USES_TERMINAL
            )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # giga-grains-<v>-linux-x86_64.tar.gz (spec §6.5).
    add_custom_command(TARGET gg-installer POST_BUILD
            COMMAND ${CMAKE_SOURCE_DIR}/installer/linux/make-tarball.sh
            --products ${GG_PRODUCT_DIR} --out ${GG_INSTALLER_DIR}
            --full-version ${GG_VERSION_STRING}
            --readme ${CMAKE_SOURCE_DIR}/installer/linux/README.md
            --content ${GG_CONTENT_STAGE_DIR}
            VERBATIM USES_TERMINAL
            )
elseif (WIN32)
    # giga-grains-<v>-windows-x64.zip always, by a CMake script (no shell on the Windows side), and
    # giga-grains-<v>-windows-x64.exe when Inno Setup 6's compiler is found (spec §6.5). The
    # release job requires the .exe (GG_ISCC, a cache variable, says whether it can be built).
    set(gg_windows_installer_dir ${CMAKE_SOURCE_DIR}/installer/windows)
    add_custom_command(TARGET gg-installer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DPRODUCTS=${GG_PRODUCT_DIR} -DOUT=${GG_INSTALLER_DIR}
            -DFULL_VERSION=${GG_VERSION_STRING} -DREADME=${gg_windows_installer_dir}/readme.txt
            -DCONTENT=${GG_CONTENT_STAGE_DIR}
            -P ${gg_windows_installer_dir}/make-zip.cmake
            VERBATIM USES_TERMINAL
            )
    find_program(GG_ISCC ISCC PATHS "C:/Program Files (x86)/Inno Setup 6"
            DOC "Inno Setup 6's command-line compiler, ISCC.exe, which builds the Windows installer")
    if (GG_ISCC)
        message(STATUS "gg-installer: the zip, and the installer with Inno Setup's ${GG_ISCC}")
        # ISCC takes Windows paths; its own log names its version (giga-grains.iss prints it).
        foreach (var IN ITEMS GG_PRODUCT_DIR GG_LICENSE_DIR GG_INSTALLER_DIR GG_CONTENT_STAGE_DIR)
            file(TO_NATIVE_PATH "${${var}}" gg_native_${var})
        endforeach ()
        file(TO_NATIVE_PATH "${gg_windows_installer_dir}/giga-grains.iss" gg_native_iss)
        add_custom_command(TARGET gg-installer POST_BUILD
                COMMAND ${GG_ISCC} /DStagedAssets=${gg_native_GG_PRODUCT_DIR}
                /DLicenses=${gg_native_GG_LICENSE_DIR} /DVersion=${GG_VERSION_NUMERIC}
                /DFullVersion=${GG_VERSION_STRING} /DOutDir=${gg_native_GG_INSTALLER_DIR}
                /DContent=${gg_native_GG_CONTENT_STAGE_DIR} ${gg_native_iss}
                VERBATIM USES_TERMINAL
                )
    else ()
        message(STATUS "gg-installer: Inno Setup's ISCC.exe not found; gg-installer builds the "
                "zip alone")
    endif ()
endif ()
