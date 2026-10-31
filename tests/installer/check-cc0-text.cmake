# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks that the CC0 text the installers carry (installer/licenses/CC0-1.0.txt) is byte for byte
# the content release's own LICENSE, by SHA-256 (fix wave T6):
#
#   cmake -DOURS=<installer/licenses/CC0-1.0.txt> -DCONTENT=<fetched content folder> -P <this file>
#
# Its CTest is installer_cc0_text, which requires the fixture `content` (content_fixture), so the
# pinned content is fetched first, once per build tree.

cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS OURS CONTENT)
    if (NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "check-cc0-text: ${var} is not set")
    endif ()
endforeach ()
set(theirs "${CONTENT}/LICENSE")
foreach (file IN ITEMS "${OURS}" "${theirs}")
    if (NOT EXISTS "${file}")
        message(FATAL_ERROR "FAIL: ${file} doesn't exist")
    endif ()
endforeach ()
file(SHA256 "${OURS}" ours_sha256)
file(SHA256 "${theirs}" theirs_sha256)
if (NOT ours_sha256 STREQUAL theirs_sha256)
    message(FATAL_ERROR "FAIL: ${OURS} (SHA-256 ${ours_sha256}) isn't the content release's "
            "${theirs} (SHA-256 ${theirs_sha256})")
endif ()
message(STATUS "PASS: ${OURS} is the content release's LICENSE (SHA-256 ${ours_sha256})")
