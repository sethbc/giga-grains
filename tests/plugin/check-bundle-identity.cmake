# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks a built macOS bundle's identity in its Info.plist, with plutil. The identities are frozen
# at the first public release (INTENT §18). Run as a CTest:
#
#   cmake -DPLUTIL=<plutil> -DBUNDLE=<bundle dir> -DCHECK=bundle-id -DEXPECTED=<id> -P <this file>
#   cmake -DPLUTIL=<plutil> -DBUNDLE=<bundle dir> -DCHECK=au-components
#         -DEXPECTED=<type>/<subtype>/<manufacturer>,... -P <this file>
#   cmake -DPLUTIL=<plutil> -DBUNDLE=<bundle dir> -DCHECK=bundle-version -DEXPECTED=<version>
#         -P <this file>
#
# bundle-id: CFBundleIdentifier must be EXPECTED.
# bundle-version: CFBundleShortVersionString and CFBundleVersion must both be EXPECTED, the
# version's numeric part (spec §6.1: Apple's bundle versions take no pre-release suffix).
# au-components: AudioComponents must hold exactly the EXPECTED type/subtype/manufacturer triples,
# comma-separated, in that order.

cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS PLUTIL BUNDLE CHECK EXPECTED)
    if (NOT DEFINED ${var})
        message(FATAL_ERROR "check-bundle-identity: ${var} is not set")
    endif ()
endforeach ()

set(plist "${BUNDLE}/Contents/Info.plist")
if (NOT EXISTS "${plist}")
    message(FATAL_ERROR "FAIL: ${plist} doesn't exist")
endif ()

# plutil -extract <key> <format> -o - <file> prints one key's value.
function(extract key format out)
    execute_process(COMMAND "${PLUTIL}" -extract "${key}" "${format}" -o - "${plist}"
            OUTPUT_VARIABLE value ERROR_VARIABLE error RESULT_VARIABLE status
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "FAIL: ${plist} has no ${key}: ${error}")
    endif ()
    set(${out} "${value}" PARENT_SCOPE)
endfunction()

if (CHECK STREQUAL "bundle-id")
    extract(CFBundleIdentifier raw id)
    if (NOT id STREQUAL EXPECTED)
        message(FATAL_ERROR "FAIL: ${BUNDLE}: CFBundleIdentifier is '${id}', expected '${EXPECTED}'")
    endif ()
    message(STATUS "${BUNDLE}: CFBundleIdentifier ${id}")
elseif (CHECK STREQUAL "bundle-version")
    foreach (key IN ITEMS CFBundleShortVersionString CFBundleVersion)
        extract(${key} raw version)
        if (NOT version STREQUAL EXPECTED)
            message(FATAL_ERROR "FAIL: ${BUNDLE}: ${key} is '${version}', expected '${EXPECTED}'")
        endif ()
        message(STATUS "${BUNDLE}: ${key} ${version}")
    endforeach ()
elseif (CHECK STREQUAL "au-components")
    string(REPLACE "," ";" EXPECTED "${EXPECTED}")
    extract(AudioComponents json components)
    string(JSON count LENGTH "${components}")
    set(found "")
    if (count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach (i RANGE ${last})
            string(JSON type GET "${components}" ${i} type)
            string(JSON subtype GET "${components}" ${i} subtype)
            string(JSON manufacturer GET "${components}" ${i} manufacturer)
            list(APPEND found "${type}/${subtype}/${manufacturer}")
        endforeach ()
    endif ()
    if (NOT found STREQUAL EXPECTED)
        message(FATAL_ERROR "FAIL: ${BUNDLE}: AudioComponents are '${found}', expected '${EXPECTED}'")
    endif ()
    message(STATUS "${BUNDLE}: AudioComponents ${found}")
else ()
    message(FATAL_ERROR "check-bundle-identity: unknown CHECK '${CHECK}'")
endif ()
