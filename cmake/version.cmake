# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# The version (spec §6.1). The numeric part is project()'s VERSION; GG_VERSION_PRERELEASE holds
# the pre-release suffix: dev on main, beta.N for a beta, empty for a release,
# dev.<yyyymmdd>.g<sha7> for a nightly. Nothing is derived from git at configure time.
#
#   gg_compose_version(<numeric> <prerelease> <out>)
#
# sets <out> to <numeric> when <prerelease> is empty, else to <numeric>-<prerelease>. A suffix
# must be dot-separated identifiers of [0-9A-Za-z-], none of them empty (semver's pre-release
# syntax); anything else fails the configure, naming GG_VERSION_PRERELEASE.

function(gg_compose_version numeric prerelease out)
    if ("${prerelease}" STREQUAL "")
        set(${out} "${numeric}" PARENT_SCOPE)
        return()
    endif ()
    if (NOT "${prerelease}" MATCHES "^[0-9A-Za-z.-]*$")
        message(FATAL_ERROR "GG_VERSION_PRERELEASE '${prerelease}' may hold only the characters "
                "0-9, A-Z, a-z, '.' and '-'")
    endif ()
    if ("${prerelease}" MATCHES "^\\." OR "${prerelease}" MATCHES "\\.$"
            OR "${prerelease}" MATCHES "\\.\\.")
        message(FATAL_ERROR "GG_VERSION_PRERELEASE '${prerelease}' has an empty identifier: "
                "its dot-separated parts must not be empty")
    endif ()
    set(${out} "${numeric}-${prerelease}" PARENT_SCOPE)
endfunction()
