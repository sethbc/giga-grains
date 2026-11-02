# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg_check_pairs(<who> <files> <names>): the one check that a list of license files and its list of
# names pair up exactly, and that every file exists. <files> and <names> are lists (the values,
# quoted). foreach(... IN ZIP_LISTS) pads the shorter list with empty items, so a name without its
# file (or a file without its name) would slip through as an empty one. Used by gg_license_files
# (cmake/licenses.cmake, at configure time) and by embed-text.cmake (at build time).

function(gg_check_pairs who files names)
    list(LENGTH files file_count)
    list(LENGTH names name_count)
    if (NOT file_count EQUAL name_count OR file_count EQUAL 0)
        message(FATAL_ERROR "${who}: ${file_count} files but ${name_count} names; each file "
                "needs one name")
    endif ()
    foreach (source name IN ZIP_LISTS files names)
        if (NOT EXISTS "${source}")
            message(FATAL_ERROR "${who}: '${source}' (${name}) doesn't exist")
        endif ()
    endforeach ()
endfunction()
