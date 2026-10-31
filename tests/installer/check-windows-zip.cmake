# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Runs installer/windows/make-zip.cmake and checks the zip it makes (spec §6.5). Run as a CTest:
#
#   cmake -DSCRIPT=<make-zip.cmake> -DPRODUCTS=<gg-products> -DREADME=<installer/windows/readme.txt>
#         -DOUT=<scratch dir> -DFULL=<full version> -DCHECK_ARCHIVE=<check-archive.cmake>
#         [-DCONTENT=<staged content dir>] [-DMISSING=1] -P <this file>
#
# Every case runs the script with this CMake into OUT/out, which it first deletes.
# - By default: the script succeeds and OUT/out holds giga-grains-<FULL>-windows-x64.zip alone, a
#   zip whose every entry sits under giga-grains-<FULL>/, holding the CLAP, the VST3 bundle's
#   binary, the two standalones, the license set (with the ASIO SDK's license, and without the
#   AudioUnit SDK's, which is the macOS AU's) and readme.txt (check-archive.cmake checks these);
#   with CONTENT (passed as -DCONTENT), also content/ with its Presets, Samples and PROVENANCE.md;
#   extracted, its readme.txt is the one given and its licenses/ the products' license set, file
#   for file.
# - MISSING: run on an empty products directory, the script fails naming every product and the
#   license set, and writes nothing.

cmake_minimum_required(VERSION 3.22)

# An empty value is refused too (as check-archive.cmake does): OUT is deleted below.
foreach (var IN ITEMS SCRIPT PRODUCTS README OUT FULL CHECK_ARCHIVE)
    if (NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "check-windows-zip: ${var} is not set")
    endif ()
endforeach ()

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
set(out_dir "${OUT}/out")
set(products "${PRODUCTS}")
if (MISSING)
    set(products "${OUT}/empty-products")
    file(MAKE_DIRECTORY "${products}")
endif ()

set(content_args "")
if (CONTENT)
    set(content_args "-DCONTENT=${CONTENT}")
endif ()

execute_process(
        COMMAND ${CMAKE_COMMAND} "-DPRODUCTS=${products}" "-DOUT=${out_dir}"
        "-DFULL_VERSION=${FULL}" "-DREADME=${README}" ${content_args} -P "${SCRIPT}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
set(transcript "exit status ${result}\n--- stdout\n${stdout}--- stderr\n${stderr}")

# The refusal: a failure, a message naming every product and the license set, and nothing
# written.
if (MISSING)
    set(failures "")
    if (result EQUAL 0)
        string(APPEND failures "\n  it succeeded")
    endif ()
    foreach (name IN ITEMS "Giga Grains.clap" "Giga Grains.vst3" "Giga Grains.exe"
            "Giga Grains FX.exe" "licenses")
        string(FIND "${stderr}" "'${name}'" at)
        if (at EQUAL -1)
            string(APPEND failures "\n  its error doesn't name '${name}'")
        endif ()
    endforeach ()
    if (EXISTS "${out_dir}")
        string(APPEND failures "\n  it wrote ${out_dir}")
    endif ()
    if (failures)
        message(FATAL_ERROR "FAIL: with an empty products directory, make-zip.cmake:"
                "${failures}\n${transcript}")
    endif ()
    message(STATUS
            "PASS: with an empty products directory, make-zip.cmake fails, names every missing "
            "product and writes nothing")
    return()
endif ()

if (NOT result EQUAL 0)
    message(FATAL_ERROR "FAIL: make-zip.cmake failed\n${transcript}")
endif ()
set(top "giga-grains-${FULL}")
set(name "${top}-windows-x64.zip")
set(zip "${out_dir}/${name}")
file(GLOB written RELATIVE "${out_dir}" "${out_dir}/*")
if (NOT written STREQUAL "${name}")
    message(FATAL_ERROR "FAIL: ${out_dir} holds '${written}', not just ${name}\n${transcript}")
endif ()

# The entries. A quoted -D argument stays one argument, so each list reaches check-archive whole.
set(expect "${top}/Giga Grains.clap"
        "${top}/Giga Grains.vst3/Contents/x86_64-win/Giga Grains.vst3"
        "${top}/Giga Grains.exe" "${top}/Giga Grains FX.exe"
        "${top}/licenses/THIRD_PARTY_NOTICES.md" "${top}/licenses/ASIO-SDK-LICENSE.txt"
        "${top}/readme.txt")
set(forbid "${top}/licenses/AudioUnitSDK-LICENSE.txt")
if (CONTENT)
    list(APPEND expect "${top}/content/Presets" "${top}/content/Samples"
            "${top}/content/PROVENANCE.md" "${top}/content/LICENSE-CC0-1.0.txt"
            "${top}/content/content-version.txt")
endif ()
execute_process(
        COMMAND ${CMAKE_COMMAND} "-DARCHIVE=${zip}" -DKIND=zip "-DEXPECT=${expect}"
        "-DFORBID=${forbid}" "-DPREFIX=${top}" -P "${CHECK_ARCHIVE}"
        RESULT_VARIABLE check_result
        OUTPUT_VARIABLE check_out
        ERROR_VARIABLE check_err)
if (NOT check_result EQUAL 0)
    message(FATAL_ERROR "FAIL: check-archive.cmake:\n${check_out}${check_err}")
endif ()
string(STRIP "${check_out}${check_err}" check_said)
string(REGEX REPLACE "^-- " "" check_said "${check_said}")
message(STATUS "check-archive.cmake: ${check_said}")

# The contents of readme.txt and licenses/.
set(extracted "${OUT}/extracted")
file(MAKE_DIRECTORY "${extracted}")
execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${zip}"
        WORKING_DIRECTORY "${extracted}"
        RESULT_VARIABLE extract_result ERROR_VARIABLE extract_error)
if (NOT extract_result EQUAL 0)
    message(FATAL_ERROR "FAIL: cmake -E tar xf ${zip}: ${extract_error}")
endif ()
set(failures "")
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files "${README}"
        "${extracted}/${top}/readme.txt" RESULT_VARIABLE same)
if (NOT same EQUAL 0)
    string(APPEND failures "\n  its readme.txt isn't ${README}")
endif ()
file(GLOB expected_licenses RELATIVE "${PRODUCTS}/licenses" "${PRODUCTS}/licenses/*")
file(GLOB zip_licenses RELATIVE "${extracted}/${top}/licenses" "${extracted}/${top}/licenses/*")
list(SORT expected_licenses)
list(SORT zip_licenses)
if (NOT expected_licenses OR NOT zip_licenses STREQUAL expected_licenses)
    string(APPEND failures "\n  its licenses/ holds '${zip_licenses}', not '${expected_licenses}'")
else ()
    foreach (license IN LISTS expected_licenses)
        execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files
                "${PRODUCTS}/licenses/${license}" "${extracted}/${top}/licenses/${license}"
                RESULT_VARIABLE same)
        if (NOT same EQUAL 0)
            string(APPEND failures "\n  its licenses/${license} differs from the products'")
        endif ()
    endforeach ()
endif ()
if (failures)
    message(FATAL_ERROR "FAIL: ${zip}:${failures}\n${transcript}")
endif ()
message(STATUS "PASS: make-zip.cmake wrote ${zip}")
