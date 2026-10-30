# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Runs installer/macos/make-installer.sh unsigned and checks what it makes (spec §6.4). Run as a
# CTest:
#
#   cmake -DSCRIPT=<make-installer.sh> -DPRODUCTS=<gg-products> -DOUT=<scratch dir>
#         -DRESOURCES=<installer/macos> -DLICENSES=<gg-products/licenses> -DNUMERIC=<X.Y.Z>
#         -DFULL=<full version> [-DCONTENT=<staged content dir>] [-DMISSING=1 | -DPARTIAL_ENV=1]
#         -P <this file>
#
# Every case runs the script with the five signing variables unset, through /bin/bash (macOS's
# bash 3.2), into OUT/out, which it first deletes.
# - By default: the script exits 0 and its last line is `signed: no`; the PKG's Distribution
#   names the five components (six with CONTENT, which is passed to the script as --content: the
#   Content choice in the outline, its pkg-ref, and a component installing the staged folder's
#   Presets, Samples and the rest at /Library/Application Support/sethbc/Giga Grains), the version, both architectures, macOS 11.0, the title and the
#   two panes; each component installs its one product where spec §6.4's table says, not
#   relocatable; and the DMG is a UDZO image holding the PKG and Licenses/.
# - MISSING: run on an empty products directory, the script exits 2 naming Giga Grains.clap and
#   writes nothing.
# - PARTIAL_ENV: with MAC_SIGNING_IDENTITY=x alone, the script exits 2 naming the four variables
#   it lacks and writes nothing.

cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS SCRIPT PRODUCTS OUT RESOURCES LICENSES NUMERIC FULL)
    if (NOT DEFINED ${var})
        message(FATAL_ERROR "check-macos-pkg: ${var} is not set")
    endif ()
endforeach ()

set(signing_vars MAC_SIGNING_IDENTITY MAC_INSTALLER_IDENTITY NOTARY_KEY_FILE NOTARY_KEY_ID
        NOTARY_ISSUER_ID)
set(env_args "")
foreach (var IN LISTS signing_vars)
    list(APPEND env_args --unset=${var})
endforeach ()

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
set(out_dir "${OUT}/out")
set(products "${PRODUCTS}")
if (MISSING)
    set(products "${OUT}/empty-products")
    file(MAKE_DIRECTORY "${products}")
endif ()
if (PARTIAL_ENV)
    list(APPEND env_args MAC_SIGNING_IDENTITY=x)
endif ()

set(content_args "")
if (CONTENT)
    set(content_args --content "${CONTENT}")
endif ()

execute_process(
        COMMAND ${CMAKE_COMMAND} -E env ${env_args}
        /bin/bash "${SCRIPT}" --products "${products}" --out "${out_dir}" --version "${NUMERIC}"
        --full-version "${FULL}" --resources "${RESOURCES}" --licenses "${LICENSES}"
        ${content_args}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
set(transcript "exit status ${result}\n--- stdout\n${stdout}--- stderr\n${stderr}")

# The refusals: exit 2, a message naming what's wrong, and nothing written.
if (MISSING OR PARTIAL_ENV)
    if (MISSING)
        set(what "an empty products directory")
        set(expected_names "Giga Grains.clap")
    else ()
        set(what "MAC_SIGNING_IDENTITY alone")
        set(expected_names MAC_INSTALLER_IDENTITY NOTARY_KEY_FILE NOTARY_KEY_ID NOTARY_ISSUER_ID)
    endif ()
    set(failures "")
    if (NOT result EQUAL 2)
        string(APPEND failures "\n  it exited ${result}, not 2")
    endif ()
    foreach (name IN LISTS expected_names)
        string(FIND "${stderr}" "${name}" at)
        if (at EQUAL -1)
            string(APPEND failures "\n  its error doesn't name ${name}")
        endif ()
    endforeach ()
    if (EXISTS "${out_dir}")
        string(APPEND failures "\n  it wrote ${out_dir}")
    endif ()
    if (failures)
        message(FATAL_ERROR "FAIL: with ${what}, make-installer.sh:${failures}\n${transcript}")
    endif ()
    message(STATUS "PASS: with ${what}, make-installer.sh exits 2, says why and writes nothing")
    return()
endif ()

if (NOT result EQUAL 0)
    message(FATAL_ERROR "FAIL: make-installer.sh failed unsigned\n${transcript}")
endif ()
string(REGEX REPLACE "\n+$" "" trimmed "${stdout}")
string(REGEX MATCH "[^\n]*$" last_line "${trimmed}")
set(failures "")
if (NOT last_line STREQUAL "signed: no")
    string(APPEND failures "\n  its last line is '${last_line}', not 'signed: no'")
endif ()

set(pkg "${out_dir}/giga-grains-${FULL}-macos.pkg")
set(dmg "${out_dir}/giga-grains-${FULL}-macos.dmg")
foreach (file IN ITEMS "${pkg}" "${dmg}")
    if (NOT EXISTS "${file}")
        message(FATAL_ERROR "FAIL: make-installer.sh didn't write ${file}\n${transcript}")
    endif ()
endforeach ()
file(GLOB written RELATIVE "${out_dir}" "${out_dir}/*")
list(SORT written)
if (NOT written STREQUAL "giga-grains-${FULL}-macos.dmg;giga-grains-${FULL}-macos.pkg")
    string(APPEND failures "\n  ${out_dir} holds '${written}', not just the PKG and the DMG")
endif ()

# The product archive.
set(expanded "${OUT}/expanded")
execute_process(COMMAND pkgutil --expand "${pkg}" "${expanded}"
        RESULT_VARIABLE expand_result ERROR_VARIABLE expand_error)
if (NOT expand_result EQUAL 0)
    message(FATAL_ERROR "FAIL: pkgutil --expand ${pkg}: ${expand_error}")
endif ()
file(READ "${expanded}/Distribution" distribution)

# The components (spec §6.4): identifier, install location, and the one product each holds.
set(ids org.sethbc.giga-grains.clap.pkg org.sethbc.giga-grains.vst3.pkg
        org.sethbc.giga-grains.component.pkg org.sethbc.giga-grains.app.pkg
        org.sethbc.giga-grains-fx.app.pkg)
set(locations /Library/Audio/Plug-Ins/CLAP /Library/Audio/Plug-Ins/VST3
        /Library/Audio/Plug-Ins/Components /Applications /Applications)
set(bundles "Giga Grains.clap" "Giga Grains.vst3" "Giga Grains.component" "Giga Grains.app"
        "Giga Grains FX.app")
set(expected_components 5)
if (CONTENT)
    # The Content component (spec 6.11): its payload root holds the staged folder's own entries.
    set(expected_components 6)
    list(APPEND ids org.sethbc.giga-grains.content.pkg)
    list(APPEND locations "/Library/Application Support/sethbc/Giga Grains")
    list(APPEND bundles "")
    if (NOT distribution MATCHES "<line choice=\"org.sethbc.giga-grains.content.pkg\"/>")
        string(APPEND failures "\n  Distribution's outline has no line for the Content choice")
    endif ()
    if (NOT distribution MATCHES "<choice id=\"org.sethbc.giga-grains.content.pkg\"")
        string(APPEND failures "\n  Distribution has no Content choice")
    endif ()
    if (NOT distribution MATCHES "giga-grains-content.pkg</pkg-ref>")
        string(APPEND failures "\n  Distribution doesn't name giga-grains-content.pkg")
    endif ()
    file(GLOB content_roots RELATIVE "${CONTENT}" "${CONTENT}/*")
    list(SORT content_roots)
elseif (distribution MATCHES "<choice id=\"org.sethbc.giga-grains.content.pkg\"")
    string(APPEND failures "\n  Distribution names a Content choice without --content")
endif ()

foreach (needle IN ITEMS "version=\"${NUMERIC}\"" "hostArchitectures=\"x86_64,arm64\""
        "customize=\"always\"" "<os-version min=\"11.0\"/>" "<title>Giga Grains ${FULL}</title>"
        "<license file=\"LICENSE.txt\"/>" "<readme file=\"Readme.txt\"/>")
    string(FIND "${distribution}" "${needle}" at)
    if (at EQUAL -1)
        string(APPEND failures "\n  Distribution lacks ${needle}")
    endif ()
endforeach ()
foreach (name IN ITEMS LICENSE.txt Readme.txt)
    if (NOT EXISTS "${expanded}/Resources/${name}")
        string(APPEND failures "\n  the PKG's Resources lack ${name}")
    endif ()
endforeach ()

file(GLOB package_infos "${expanded}/*.pkg/PackageInfo")
list(LENGTH package_infos component_count)
if (NOT component_count EQUAL expected_components)
    string(APPEND failures
            "\n  the PKG holds ${component_count} components, not ${expected_components}")
endif ()
foreach (id location bundle IN ZIP_LISTS ids locations bundles)
    string(FIND "${distribution}" "<pkg-ref id=\"${id}\"" at)
    if (at EQUAL -1)
        string(APPEND failures "\n  Distribution has no pkg-ref ${id}")
    endif ()
    set(found "")
    foreach (info IN LISTS package_infos)
        file(READ "${info}" text)
        if (text MATCHES "identifier=\"${id}\"")
            set(found "${info}")
            break()
        endif ()
    endforeach ()
    if (NOT found)
        string(APPEND failures "\n  no component's PackageInfo has the identifier ${id}")
        continue()
    endif ()
    if (NOT text MATCHES "install-location=\"${location}\"")
        string(APPEND failures "\n  ${id} doesn't install to ${location}")
    endif ()
    # A relocatable bundle installs over any copy with its bundle ID elsewhere on the disk (a build
    # tree's, say) instead of into its install location.
    if (text MATCHES "<relocate>[^<]*<bundle")
        string(APPEND failures "\n  ${id}'s bundle is relocatable")
    endif ()
    # The payload holds the one product at its root.
    get_filename_component(component_dir "${found}" DIRECTORY)
    execute_process(COMMAND lsbom -s "${component_dir}/Bom" OUTPUT_VARIABLE bom
            RESULT_VARIABLE bom_result)
    string(REPLACE "\n" ";" bom_lines "${bom}")
    set(roots "")
    foreach (line IN LISTS bom_lines)
        if (line MATCHES "^\\./([^/]+)$")
            list(APPEND roots "${CMAKE_MATCH_1}")
        endif ()
    endforeach ()
    list(SORT roots)
    if (bundle STREQUAL "")
        set(want "${content_roots}")
    else ()
        set(want "${bundle}")
    endif ()
    if (NOT bom_result EQUAL 0 OR NOT roots STREQUAL "${want}")
        string(APPEND failures "\n  ${id}'s payload root holds '${roots}', not '${want}'")
    endif ()
endforeach ()

# The disk image: UDZO, holding the PKG and the license set.
# hdiutil imageinfo works on every macOS CI runs; macOS 27 calls it deprecated on stderr, which is
# shown only when it fails.
execute_process(COMMAND hdiutil imageinfo "${dmg}" OUTPUT_VARIABLE imageinfo
        RESULT_VARIABLE imageinfo_result ERROR_VARIABLE imageinfo_error)
if (NOT imageinfo_result EQUAL 0 OR NOT imageinfo MATCHES "Format: UDZO")
    string(APPEND failures "\n  hdiutil imageinfo doesn't report a UDZO image: ${imageinfo_error}")
endif ()
set(mount "${OUT}/mount")
file(MAKE_DIRECTORY "${mount}")
execute_process(COMMAND hdiutil attach -readonly -nobrowse -noautoopen -mountpoint "${mount}"
        "${dmg}" OUTPUT_QUIET RESULT_VARIABLE attach_result ERROR_VARIABLE attach_error)
if (NOT attach_result EQUAL 0)
    string(APPEND failures "\n  hdiutil attach failed: ${attach_error}")
else ()
    file(GLOB dmg_top RELATIVE "${mount}" "${mount}/*")
    file(GLOB dmg_licenses RELATIVE "${mount}/Licenses" "${mount}/Licenses/*")
    file(GLOB expected_licenses RELATIVE "${LICENSES}" "${LICENSES}/*")
    # diskutil eject: macOS 27's hdiutil calls its own detach deprecated.
    execute_process(COMMAND diskutil eject "${mount}" OUTPUT_QUIET RESULT_VARIABLE eject_result)
    if (NOT eject_result EQUAL 0)
        execute_process(COMMAND hdiutil detach -force "${mount}" OUTPUT_QUIET ERROR_QUIET)
    endif ()
    list(SORT dmg_top)
    if (NOT dmg_top STREQUAL "Licenses;giga-grains-${FULL}-macos.pkg")
        string(APPEND failures "\n  the DMG holds '${dmg_top}', not the PKG and Licenses")
    endif ()
    list(SORT dmg_licenses)
    list(SORT expected_licenses)
    if (NOT expected_licenses OR NOT dmg_licenses STREQUAL expected_licenses)
        string(APPEND failures
                "\n  the DMG's Licenses hold '${dmg_licenses}', not '${expected_licenses}'")
    endif ()
endif ()

if (failures)
    message(FATAL_ERROR "FAIL: make-installer.sh's unsigned PKG and DMG:${failures}\n${transcript}")
endif ()
message(STATUS "PASS: make-installer.sh wrote ${pkg} (${expected_components} components) and a "
        "UDZO ${dmg}, unsigned")
