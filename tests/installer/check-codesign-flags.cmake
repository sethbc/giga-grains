# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks that a bundle signed with a real identity can be notarized (spec §6.2): its signature
# carries the hardened runtime and, for the two standalone apps, the audio-input entitlement
# (installer/macos/standalone.entitlements). Run as a CTest:
#
#   cmake -DCODESIGN=<codesign> -DBUNDLE=<bundle dir> -DEXPECT_ENTITLEMENT=<key or empty> \
#         -P <this file>
#
# The signature's flags= line must name `runtime`. With EXPECT_ENTITLEMENT set, the signature's
# entitlements must hold that key set to true (`<key>KEY</key>` then `<true/>`); with it empty
# they aren't read.

cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS CODESIGN BUNDLE)
    if (NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "check-codesign-flags: ${var} is not set")
    endif ()
endforeach ()
if (NOT IS_DIRECTORY "${BUNDLE}")
    message(FATAL_ERROR "FAIL: ${BUNDLE} doesn't exist")
endif ()

# codesign -d writes its report to stderr.
execute_process(COMMAND "${CODESIGN}" -d --verbose=2 "${BUNDLE}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "FAIL: codesign -d ${BUNDLE} exited ${result}:\n${out}${err}")
endif ()
string(REGEX MATCH "(^|\n)CodeDirectory [^\n]*flags=[^\n]*" flags_line "${out}${err}")
if (flags_line STREQUAL "")
    message(FATAL_ERROR "FAIL: codesign -d ${BUNDLE} printed no flags= line:\n${out}${err}")
endif ()
string(STRIP "${flags_line}" flags_line)
if (NOT flags_line MATCHES "flags=0x[0-9a-f]+\\([^)]*runtime[^)]*\\)")
    message(FATAL_ERROR "FAIL: ${BUNDLE} isn't signed with the hardened runtime: ${flags_line}")
endif ()
message(STATUS "${flags_line}")

if (NOT "${EXPECT_ENTITLEMENT}" STREQUAL "")
    # `--entitlements :-` is deprecated ("will not work in a future release"); `- --xml` is its
    # replacement, and prints the same plist.
    execute_process(COMMAND "${CODESIGN}" -d --entitlements - --xml "${BUNDLE}"
            RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR
                "FAIL: codesign -d --entitlements ${BUNDLE} exited ${result}:\n${out}${err}")
    endif ()
    # The key as a literal in a regex (its dots, say), then the value true, whitespace allowed.
    string(REGEX REPLACE "([][.^$*+?()|{}\\])" "\\\\\\1" key_re "${EXPECT_ENTITLEMENT}")
    if (NOT out MATCHES "<key>${key_re}</key>[ \t\r\n]*<true/>")
        message(FATAL_ERROR "FAIL: ${BUNDLE} lacks the entitlement ${EXPECT_ENTITLEMENT} set to "
                "true:\n${out}${err}")
    endif ()
    message(STATUS "entitlement ${EXPECT_ENTITLEMENT} present and true")
endif ()
message(STATUS "PASS: ${BUNDLE}")
