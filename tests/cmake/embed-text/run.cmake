# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# embed_text_*: runs cmake/embed-text.cmake over fixture files and compares its output with what
# the escaping rules (the script's header) say it must be. A mismatch reports the first differing
# byte offset and a few bytes around it, not two whole blobs.
#   cmake -DGG_CASE=<escapes|long-line|empty|unpaired> -DGG_REPO=<repo> -DGG_OUT=<dir>
#         -P run.cmake

file(REMOVE_RECURSE "${GG_OUT}")
file(MAKE_DIRECTORY "${GG_OUT}")

# The offset of the first byte at which two strings differ (-1 when equal), with a window of
# each around it.
function(gg_first_difference out a b)
    string(LENGTH "${a}" la)
    string(LENGTH "${b}" lb)
    if (la LESS lb)
        set(n ${la})
    else ()
        set(n ${lb})
    endif ()
    set(at -1)
    # Walk in blocks of 64, then bytes.
    set(i 0)
    while (i LESS n)
        math(EXPR rest "${n} - ${i}")
        if (rest GREATER 64)
            set(rest 64)
        endif ()
        string(SUBSTRING "${a}" ${i} ${rest} sa)
        string(SUBSTRING "${b}" ${i} ${rest} sb)
        if (NOT sa STREQUAL sb)
            foreach (j RANGE 0 63)
                string(SUBSTRING "${a}" ${i} 1 ca)
                string(SUBSTRING "${b}" ${i} 1 cb)
                if (NOT ca STREQUAL cb)
                    set(at ${i})
                    break()
                endif ()
                math(EXPR i "${i} + 1")
            endforeach ()
            break()
        endif ()
        math(EXPR i "${i} + ${rest}")
    endwhile ()
    if (at EQUAL -1 AND NOT la EQUAL lb)
        set(at ${n})
    endif ()
    set(msg "")
    if (NOT at EQUAL -1)
        math(EXPR from "${at} - 20")
        if (from LESS 0)
            set(from 0)
        endif ()
        string(SUBSTRING "${a}" ${from} 60 wa)
        string(SUBSTRING "${b}" ${from} 60 wb)
        set(msg "first difference at byte ${at} (lengths ${la} and ${lb}):\n  actual:   [${wa}]\n  expected: [${wb}]")
    endif ()
    set(${out} "${at}" PARENT_SCOPE)
    set(${out}_message "${msg}" PARENT_SCOPE)
endfunction()

set(src "${GG_OUT}/input.txt")
set(gen "${GG_OUT}/Out.cpp")

function(gg_embed)
    execute_process(COMMAND ${CMAKE_COMMAND} "-DOUT=${gen}" "-DFILES=${ARGV0}" "-DNAMES=${ARGV1}"
            -P "${GG_REPO}/cmake/embed-text.cmake"
            RESULT_VARIABLE status ERROR_VARIABLE err OUTPUT_VARIABLE out)
    set(gg_status ${status} PARENT_SCOPE)
    set(gg_err "${err}" PARENT_SCOPE)
endfunction()

if (GG_CASE STREQUAL "unpaired")
    # Two files, one name: the script refuses, naming the counts.
    file(WRITE "${src}" "x")
    gg_embed("${src};${src}" "only")
    if (gg_status EQUAL 0 OR NOT gg_err MATCHES "2 files but 1 names")
        message(FATAL_ERROR "an unpaired list should fail with the counts; status ${gg_status}: ${gg_err}")
    endif ()
    return()
endif ()

# The fixture, and the body the generated array must hold (between the "constexpr char kText0[] ="
# line and the closing ";").
if (GG_CASE STREQUAL "escapes")
    # A quote, a backslash, a tab, a newline, a control character (0x01) followed by a hex digit
    # and a non-hex letter, then UTF-8 (e acute, c3 a9) followed by a hex digit.
    string(ASCII 1 c01)
    string(ASCII 9 tab)
    file(WRITE "${src}" "say \"hi\" \\ ${tab}end\n${c01}1${c01}z\n")
    file(APPEND "${src}" "x")
    file(READ "${src}" before HEX)
    # file(WRITE) of é: append the two bytes through a hex-safe route.
    file(WRITE "${GG_OUT}/utf8.txt" "")
    execute_process(COMMAND ${CMAKE_COMMAND} -E echo_append "é" OUTPUT_FILE "${GG_OUT}/utf8.txt")
    file(READ "${GG_OUT}/utf8.txt" utf8 HEX)
    if (NOT utf8 STREQUAL "c3a9")
        message(FATAL_ERROR "the fixture's UTF-8 bytes are ${utf8}, not c3a9")
    endif ()
    file(READ "${GG_OUT}/utf8.txt" utf8text)
    file(APPEND "${src}" "${utf8text}" "7\n")
    set(expected
        "    \"say \\\"hi\\\" \\\\ \\x09\" \"end\\x0a\"\n"
        "    \"\\x01\" \"1\\x01z\\x0a\"\n"
        "    \"x\\xc3\\xa9\" \"7\\x0a\"\n")
    string(REPLACE ";" "" expected "${expected}")
elseif (GG_CASE STREQUAL "long-line")
    # One 40,000-byte line: three pieces of 16,000, 16,000 and 8,000 bytes (no newline until the
    # last byte), each its own literal.
    string(REPEAT "a" 16000 piece_a)
    string(REPEAT "b" 16000 piece_b)
    string(REPEAT "c" 7999 piece_c)
    file(WRITE "${src}" "${piece_a}${piece_b}${piece_c}\n")
    set(expected "    \"${piece_a}\"\n    \"${piece_b}\"\n    \"${piece_c}\\x0a\"\n")
elseif (GG_CASE STREQUAL "empty")
    file(WRITE "${src}" "")
    set(expected "    \"\"\n")
else ()
    message(FATAL_ERROR "unknown case ${GG_CASE}")
endif ()

gg_embed("${src}" "fixture.txt")
if (NOT gg_status EQUAL 0)
    message(FATAL_ERROR "embed-text failed (${gg_status}): ${gg_err}")
endif ()
file(READ "${gen}" generated)
string(FIND "${generated}" "constexpr char kText0[] =\n" start)
string(FIND "${generated}" ";\n\nconstexpr std::array" stop)
if (start EQUAL -1 OR stop EQUAL -1)
    message(FATAL_ERROR "the generated source has no kText0 array:\n${generated}")
endif ()
string(LENGTH "constexpr char kText0[] =\n" head)
math(EXPR from "${start} + ${head}")
math(EXPR len "${stop} - ${from}")
string(SUBSTRING "${generated}" ${from} ${len} actual)
# The array ends with ";" in place of its last newline: put the newline back.
string(APPEND actual "\n")
gg_first_difference(at "${actual}" "${expected}")
if (NOT at EQUAL -1)
    message(FATAL_ERROR "${GG_CASE}: ${at_message}")
endif ()
