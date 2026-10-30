# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks the content folder that gg-products stages (cmake/products.cmake; spec M2-5 §6.11), which
# every installer's Content component carries. Run as a CTest:
#
#   cmake -DDIR=<gg-products/content> -DPIN=<content/pin.txt> -DPRODUCTS_MIN=<n> -DMIDI_MIN=<n>
#         -P <this file>
#
# - Presets/Giga Grains/ and Presets/Giga Grains FX/ each hold at least PRODUCTS_MIN .ggpreset
#   files (0 in M2-5's first group, which has no factory presets yet; 24 once they exist), and
#   Presets/Giga Grains FX/ at least MIDI_MIN of them tagged `midi` (0, then 8). A preset's tags
#   are readable in the file's text (`tags="texture midi"`).
# - Samples/ holds at least one file, and every file's SHA-256 appears in PROVENANCE.md.
# - PROVENANCE.md, LICENSE-CC0-1.0.txt, README.md and content-version.txt are there, and
#   content-version.txt is `giga-grains-content <tag> <sha256>` of PIN's tag and zip SHA-256.

cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS DIR PIN PRODUCTS_MIN MIDI_MIN)
    if (NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "check-content: ${var} is not set")
    endif ()
endforeach ()
if (NOT IS_ABSOLUTE "${DIR}")
    message(FATAL_ERROR "check-content: DIR '${DIR}' isn't an absolute path")
endif ()
if (NOT IS_DIRECTORY "${DIR}")
    message(FATAL_ERROR "FAIL: ${DIR} doesn't exist")
endif ()

set(failures "")

foreach (name IN ITEMS PROVENANCE.md LICENSE-CC0-1.0.txt README.md content-version.txt)
    if (NOT EXISTS "${DIR}/${name}")
        string(APPEND failures "\n  ${name} is missing")
    endif ()
endforeach ()
foreach (name IN ITEMS Presets Samples)
    if (NOT IS_DIRECTORY "${DIR}/${name}")
        string(APPEND failures "\n  ${name}/ is missing")
    endif ()
endforeach ()
if (failures)
    message(FATAL_ERROR "FAIL: ${DIR} isn't the staged content:${failures}")
endif ()

# The version file names the pin's tag and zip SHA-256.
file(STRINGS "${PIN}" pin_lines)
set(tag "")
set(zip_sha "")
foreach (line IN LISTS pin_lines)
    if (line MATCHES "^tag +([^ ]+)")
        set(tag "${CMAKE_MATCH_1}")
    elseif (line MATCHES "^sha256 +([0-9a-f]+)")
        set(zip_sha "${CMAKE_MATCH_1}")
    endif ()
endforeach ()
if (NOT tag OR NOT zip_sha)
    message(FATAL_ERROR "check-content: ${PIN} has no tag or sha256 line")
endif ()
file(READ "${DIR}/content-version.txt" version_text)
string(STRIP "${version_text}" version_text)
if (NOT version_text STREQUAL "giga-grains-content ${tag} ${zip_sha}")
    string(APPEND failures "\n  content-version.txt is '${version_text}', not "
            "'giga-grains-content ${tag} ${zip_sha}'")
endif ()

# The preset folders.
foreach (product IN ITEMS "Giga Grains" "Giga Grains FX")
    set(folder "${DIR}/Presets/${product}")
    if (NOT IS_DIRECTORY "${folder}")
        string(APPEND failures "\n  Presets/${product}/ is missing")
        continue()
    endif ()
    file(GLOB presets "${folder}/*")
    set(count 0)
    set(midi 0)
    foreach (preset IN LISTS presets)
        if (NOT preset MATCHES "\\.ggpreset$")
            string(APPEND failures "\n  ${preset} isn't a .ggpreset file")
            continue()
        endif ()
        math(EXPR count "${count} + 1")
        file(STRINGS "${preset}" tag_lines REGEX "tags=\"([^\"]* )?midi( [^\"]*)?\"" LIMIT_COUNT 1)
        if (tag_lines)
            math(EXPR midi "${midi} + 1")
        endif ()
    endforeach ()
    if (count LESS PRODUCTS_MIN)
        string(APPEND failures "\n  Presets/${product}/ holds ${count} presets, fewer than "
                "${PRODUCTS_MIN}")
    endif ()
    if (product STREQUAL "Giga Grains FX" AND midi LESS MIDI_MIN)
        string(APPEND failures "\n  Presets/${product}/ holds ${midi} midi presets, fewer than "
                "${MIDI_MIN}")
    endif ()
endforeach ()

# Every sample's SHA-256 is in PROVENANCE.md.
file(READ "${DIR}/PROVENANCE.md" provenance)
file(GLOB_RECURSE samples "${DIR}/Samples/*")
list(LENGTH samples sample_count)
if (sample_count EQUAL 0)
    string(APPEND failures "\n  Samples/ holds no file")
endif ()
foreach (sample IN LISTS samples)
    file(SHA256 "${sample}" sha)
    string(FIND "${provenance}" "${sha}" at)
    if (at EQUAL -1)
        file(RELATIVE_PATH rel "${DIR}" "${sample}")
        string(APPEND failures "\n  ${rel}'s SHA-256 ${sha} isn't in PROVENANCE.md")
    endif ()
endforeach ()

if (failures)
    message(FATAL_ERROR "FAIL: ${DIR} isn't the staged content:${failures}")
endif ()
message(STATUS "PASS: ${DIR} holds the content at ${tag}: ${sample_count} samples, all in "
        "PROVENANCE.md")
