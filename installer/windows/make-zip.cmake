# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Makes the Windows zip (spec §6.5). The gg-installer target runs it (cmake/products.cmake). A
# CMake script, so the Windows side needs no shell:
#
#   cmake -DPRODUCTS=<dir> -DOUT=<dir> -DFULL_VERSION=<v> -DREADME=<readme.txt>
#         [-DCONTENT=<dir>] -P make-zip.cmake
#
#   PRODUCTS      the four products, as gg-products gathers them: Giga Grains.clap,
#                 Giga Grains.vst3 (a folder bundle), Giga Grains.exe and Giga Grains FX.exe; and
#                 licenses/, the license set.
#   OUT           where to write giga-grains-<v>-windows-x64.zip.
#   FULL_VERSION  the full version string, <v> (the file name and its top-level folder).
#   README        installer/windows/readme.txt, which goes in as readme.txt.
#   CONTENT       the staged factory content (gg-products' content/: Presets/, Samples/, ...),
#                 which goes in as content/. Without it the zip has no content/.
#
# Every entry sits under giga-grains-<v>/: the four products, licenses/, readme.txt and, with
# CONTENT, content/, the same tree as the Linux tarball. No install script: readme.txt says where
# each product goes, and the Inno Setup installer (giga-grains.iss) puts them there.
#
# It fails naming every missing input before it writes anything, and leaves no partial zip when
# a later step fails. (file(COPY) stops the script itself on an error, before any zip exists,
# leaving the staging folder in OUT; the next run deletes it first.)

cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS PRODUCTS OUT FULL_VERSION README)
    if (NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "make-zip: ${var} is not set\n"
                "usage: cmake -DPRODUCTS=<dir> -DOUT=<dir> -DFULL_VERSION=<v> "
                "-DREADME=<readme.txt> -P make-zip.cmake")
    endif ()
endforeach ()

# The full version goes into the file name and its folder: only these characters.
if (NOT FULL_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+([-+][0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR
            "make-zip: FULL_VERSION '${FULL_VERSION}' isn't X.Y.Z with an optional -suffix")
endif ()

# Relative paths are taken from the working directory.
foreach (var IN ITEMS PRODUCTS OUT README)
    get_filename_component(${var} "${${var}}" ABSOLUTE)
endforeach ()
if (DEFINED CONTENT AND NOT "${CONTENT}" STREQUAL "")
    get_filename_component(CONTENT "${CONTENT}" ABSOLUTE)
else ()
    set(CONTENT "")
endif ()

# The inputs, each checked before anything is written; every missing one is named, one per line
# (a line starting with spaces is printed as it is, never re-wrapped).
set(missing "")
foreach (file IN ITEMS "Giga Grains.clap" "Giga Grains.exe" "Giga Grains FX.exe")
    if (NOT EXISTS "${PRODUCTS}/${file}" OR IS_DIRECTORY "${PRODUCTS}/${file}")
        string(APPEND missing "\n  '${file}' (a file)")
    endif ()
endforeach ()
foreach (folder IN ITEMS "Giga Grains.vst3" "licenses")
    if (NOT IS_DIRECTORY "${PRODUCTS}/${folder}")
        string(APPEND missing "\n  '${folder}' (a folder)")
    endif ()
endforeach ()
# The VST3 bundle's binary, where a host looks for it: a bundle folder without it is no plugin.
set(vst3_binary "Giga Grains.vst3/Contents/x86_64-win/Giga Grains.vst3")
if (IS_DIRECTORY "${PRODUCTS}/Giga Grains.vst3"
        AND (NOT EXISTS "${PRODUCTS}/${vst3_binary}" OR IS_DIRECTORY "${PRODUCTS}/${vst3_binary}"))
    string(APPEND missing "\n  '${vst3_binary}' (the VST3 bundle's binary, a file)")
endif ()
if (missing)
    message(FATAL_ERROR "make-zip: missing from '${PRODUCTS}':${missing}")
endif ()
if (NOT EXISTS "${PRODUCTS}/licenses/LICENSE")
    message(FATAL_ERROR "make-zip: the license set '${PRODUCTS}/licenses' has no LICENSE")
endif ()
if (NOT EXISTS "${README}" OR IS_DIRECTORY "${README}")
    message(FATAL_ERROR "make-zip: the readme '${README}' isn't a file")
endif ()
if (CONTENT AND (NOT IS_DIRECTORY "${CONTENT}/Presets" OR NOT IS_DIRECTORY "${CONTENT}/Samples"))
    message(FATAL_ERROR "make-zip: the content '${CONTENT}' has no Presets/ and Samples/")
endif ()

set(top "giga-grains-${FULL_VERSION}")
set(zip "${OUT}/${top}-windows-x64.zip")
set(staging "${OUT}/${top}-windows-x64.staging")
message(STATUS "Giga Grains ${FULL_VERSION}: ${top}-windows-x64.zip")

# fail(<message>): removes the staging folder and any partial zip, then stops.
macro(fail text)
    file(REMOVE_RECURSE "${staging}")
    file(REMOVE "${zip}")
    message(FATAL_ERROR "make-zip: ${text}")
endmacro()

file(MAKE_DIRECTORY "${OUT}")
file(REMOVE "${zip}")
file(REMOVE_RECURSE "${staging}")
file(MAKE_DIRECTORY "${staging}/${top}")

# file(COPY) copies a folder whole, under its own name.
file(COPY "${PRODUCTS}/Giga Grains.clap" "${PRODUCTS}/Giga Grains.vst3"
        "${PRODUCTS}/Giga Grains.exe" "${PRODUCTS}/Giga Grains FX.exe" "${PRODUCTS}/licenses"
        DESTINATION "${staging}/${top}")
if (CONTENT)
    # The folder's entries go under content/: the trailing slash copies what is inside the folder,
    # not the folder itself, so its own name is dropped.
    file(COPY "${CONTENT}/" DESTINATION "${staging}/${top}/content")
endif ()
file(COPY_FILE "${README}" "${staging}/${top}/readme.txt" RESULT copy_result)
if (NOT copy_result STREQUAL "0")
    fail("copying '${README}' failed: ${copy_result}")
endif ()

# From the staging parent, so every entry's path starts at giga-grains-<v>/.
execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar cf "${zip}" --format=zip "${top}"
        WORKING_DIRECTORY "${staging}"
        RESULT_VARIABLE tar_result
        OUTPUT_VARIABLE tar_out
        ERROR_VARIABLE tar_err)
if (NOT tar_result EQUAL 0 OR NOT EXISTS "${zip}")
    fail("cmake -E tar failed (${tar_result}):\n${tar_out}${tar_err}")
endif ()
file(REMOVE_RECURSE "${staging}")

file(SIZE "${zip}" size)
message(STATUS "wrote ${zip} (${size} bytes)")
