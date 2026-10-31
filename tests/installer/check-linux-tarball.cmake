# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Runs installer/linux/make-tarball.sh and checks the tarball it makes (spec §6.5). Run as a
# CTest:
#
#   cmake -DSCRIPT=<make-tarball.sh> -DPRODUCTS=<gg-products> -DREADME=<installer/linux/README.md>
#         -DOUT=<scratch dir> -DFULL=<full version> -DCHECK_ARCHIVE=<check-archive.cmake>
#         [-DCONTENT=<staged content dir>] [-DMISSING=1] -P <this file>
#
# Every case runs the script through /bin/bash into OUT/out, which it first deletes.
# - By default: the script exits 0 and OUT/out holds giga-grains-<FULL>-linux-x86_64.tar.gz alone,
#   a gzip-compressed tar whose every entry sits under giga-grains-<FULL>/, holding the CLAP, the
#   VST3 bundle's .so, the two standalones (executable), the license set (no ASIO SDK license:
#   that is the Windows standalone's) and README.md (check-archive.cmake checks these); with
#   CONTENT (passed as --content), also content/ with its Presets, Samples and PROVENANCE.md;
#   extracted, its README.md is the one given and its licenses/ the products' license set, file
#   for file.
# - MISSING: run on an empty products directory, the script exits 2 naming Giga Grains.clap and
#   writes nothing.

cmake_minimum_required(VERSION 3.22)

# An empty value is refused too (as check-archive.cmake does): OUT is deleted below.
foreach (var IN ITEMS SCRIPT PRODUCTS README OUT FULL CHECK_ARCHIVE)
    if (NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "check-linux-tarball: ${var} is not set")
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
    set(content_args --content "${CONTENT}")
endif ()

execute_process(
        COMMAND /bin/bash "${SCRIPT}" --products "${products}" --out "${out_dir}"
        --full-version "${FULL}" --readme "${README}" ${content_args}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
set(transcript "exit status ${result}\n--- stdout\n${stdout}--- stderr\n${stderr}")

# The refusal: exit 2, a message naming the first product, and nothing written.
if (MISSING)
    set(failures "")
    if (NOT result EQUAL 2)
        string(APPEND failures "\n  it exited ${result}, not 2")
    endif ()
    string(FIND "${stderr}" "Giga Grains.clap" at)
    if (at EQUAL -1)
        string(APPEND failures "\n  its error doesn't name Giga Grains.clap")
    endif ()
    if (EXISTS "${out_dir}")
        string(APPEND failures "\n  it wrote ${out_dir}")
    endif ()
    if (failures)
        message(FATAL_ERROR "FAIL: with an empty products directory, make-tarball.sh:"
                "${failures}\n${transcript}")
    endif ()
    message(STATUS
            "PASS: with an empty products directory, make-tarball.sh exits 2, says why and writes "
            "nothing")
    return()
endif ()

if (NOT result EQUAL 0)
    message(FATAL_ERROR "FAIL: make-tarball.sh failed\n${transcript}")
endif ()
set(top "giga-grains-${FULL}")
set(name "${top}-linux-x86_64.tar.gz")
set(tarball "${out_dir}/${name}")
file(GLOB written RELATIVE "${out_dir}" "${out_dir}/*")
if (NOT written STREQUAL "${name}")
    message(FATAL_ERROR "FAIL: ${out_dir} holds '${written}', not just ${name}\n${transcript}")
endif ()

# The entries. A quoted -D argument stays one argument, so each list reaches check-archive whole.
set(expect "${top}/Giga Grains.clap" "${top}/Giga Grains.vst3/Contents/x86_64-linux/Giga Grains.so"
        "${top}/Giga Grains" "${top}/Giga Grains FX" "${top}/licenses/THIRD_PARTY_NOTICES.md"
        "${top}/licenses/GPL-3.0.txt" "${top}/README.md")
set(forbid "${top}/licenses/ASIO-SDK-LICENSE.txt")
if (CONTENT)
    list(APPEND expect "${top}/content/Presets" "${top}/content/Samples"
            "${top}/content/PROVENANCE.md" "${top}/content/LICENSE-CC0-1.0.txt"
            "${top}/content/content-version.txt")
else ()
    list(APPEND forbid "${top}/content")
endif ()
set(executable "${top}/Giga Grains" "${top}/Giga Grains FX")
execute_process(
        COMMAND ${CMAKE_COMMAND} "-DARCHIVE=${tarball}" -DKIND=tar "-DEXPECT=${expect}"
        "-DFORBID=${forbid}" "-DPREFIX=${top}" "-DEXECUTABLE=${executable}"
        -P "${CHECK_ARCHIVE}"
        RESULT_VARIABLE check_result
        OUTPUT_VARIABLE check_out
        ERROR_VARIABLE check_err)
if (NOT check_result EQUAL 0)
    message(FATAL_ERROR "FAIL: check-archive.cmake:\n${check_out}${check_err}")
endif ()
string(STRIP "${check_out}${check_err}" check_said)
string(REGEX REPLACE "^-- " "" check_said "${check_said}")
message(STATUS "check-archive.cmake: ${check_said}")

# The contents of README.md and licenses/.
set(extracted "${OUT}/extracted")
file(MAKE_DIRECTORY "${extracted}")
execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${tarball}"
        WORKING_DIRECTORY "${extracted}"
        RESULT_VARIABLE extract_result ERROR_VARIABLE extract_error)
if (NOT extract_result EQUAL 0)
    message(FATAL_ERROR "FAIL: cmake -E tar xf ${tarball}: ${extract_error}")
endif ()
set(failures "")
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files "${README}"
        "${extracted}/${top}/README.md" RESULT_VARIABLE same)
if (NOT same EQUAL 0)
    string(APPEND failures "\n  its README.md isn't ${README}")
endif ()
file(GLOB expected_licenses RELATIVE "${PRODUCTS}/licenses" "${PRODUCTS}/licenses/*")
file(GLOB tar_licenses RELATIVE "${extracted}/${top}/licenses" "${extracted}/${top}/licenses/*")
list(SORT expected_licenses)
list(SORT tar_licenses)
if (NOT expected_licenses OR NOT tar_licenses STREQUAL expected_licenses)
    string(APPEND failures "\n  its licenses/ holds '${tar_licenses}', not '${expected_licenses}'")
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
    message(FATAL_ERROR "FAIL: ${tarball}:${failures}\n${transcript}")
endif ()
message(STATUS "PASS: make-tarball.sh wrote ${tarball}")
