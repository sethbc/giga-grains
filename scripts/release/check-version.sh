#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# The release tag check (spec §6.1, §6.6): a tagged run fails here, before anything is signed, if
# the tag disagrees with the version in CMake or the suffix is dev.
#
#   scripts/release/check-version.sh <tag> [<CMakeLists.txt>]
#   scripts/release/check-version.sh --print [<CMakeLists.txt>]
#
# Reads the numeric version from `project(GigaGrains VERSION X.Y.Z` and the pre-release suffix
# from `set(GG_VERSION_PRERELEASE "<s>"` in <CMakeLists.txt> (default: the repository's), and
# composes the full version as cmake/version.cmake does: X.Y.Z when <s> is empty, else X.Y.Z-<s>.
# On success prints, one per line (so a workflow can append them to $GITHUB_OUTPUT),
#
#   numeric=X.Y.Z
#   prerelease=<s>
#   version=<full>
#
# --print checks no tag and accepts dev: it prints the same three lines for the version CMake
# would compose, which the workflow's plan job uses for a nightly's numeric part and a dry run's
# version.
#
# Exit status: 0 when <tag> is v<full> (always, with --print); 1, with a one-line reason on stderr,
# when it isn't or when <s> is dev; 2, with a one-line reason on stderr, for bad usage or an
# unparsable file.

set -euo pipefail

me="check-version.sh"
if (($# < 1 || $# > 2)); then
    echo "usage: $0 <tag> [<CMakeLists.txt>]" >&2
    echo "       $0 --print [<CMakeLists.txt>]" >&2
    exit 2
fi
tag="$1"
file="${2:-$(cd "$(dirname "$0")/../.." && pwd)/CMakeLists.txt}"
if [[ ! -f "$file" ]]; then
    echo "$me: $file: no such file" >&2
    exit 2
fi

# Each sed prints one line per match, prefixed with x so an empty suffix is told from no line.
n='[0-9][0-9]*'
project_re="^[[:space:]]*project(GigaGrains VERSION \\($n\\.$n\\.$n\\)[[:space:])].*\$"
numeric="$(sed -n "s/$project_re/x\\1/p" "$file")"
prerelease="$(sed -n 's/^[[:space:]]*set(GG_VERSION_PRERELEASE "\([^"]*\)".*$/x\1/p' "$file")"
nl='
'
if [[ -z "$numeric" || "$numeric" == *"$nl"* ]]; then
    echo "$me: $file: expected one 'project(GigaGrains VERSION X.Y.Z' line" >&2
    exit 2
fi
if [[ -z "$prerelease" || "$prerelease" == *"$nl"* ]]; then
    echo "$me: $file: expected one 'set(GG_VERSION_PRERELEASE \"<suffix>\"' line" >&2
    exit 2
fi
numeric="${numeric#x}"
prerelease="${prerelease#x}"

# The composition, and the suffix syntax, of cmake/version.cmake's gg_compose_version.
if [[ -z "$prerelease" ]]; then
    version="$numeric"
else
    if [[ ! "$prerelease" =~ ^[0-9A-Za-z.-]*$ ]]; then
        echo "$me: $file: GG_VERSION_PRERELEASE '$prerelease' may hold only the characters" \
            "0-9, A-Z, a-z, '.' and '-'" >&2
        exit 2
    fi
    if [[ "$prerelease" == .* || "$prerelease" == *. || "$prerelease" == *..* ]]; then
        echo "$me: $file: GG_VERSION_PRERELEASE '$prerelease' has an empty identifier" >&2
        exit 2
    fi
    version="$numeric-$prerelease"
fi

if [[ "$tag" == --print ]]; then
    echo "numeric=$numeric"
    echo "prerelease=$prerelease"
    echo "version=$version"
    exit 0
fi
if [[ "$prerelease" == "dev" ]]; then
    echo "$me: the pre-release suffix is dev ($version), which is never released;" \
        "set GG_VERSION_PRERELEASE first" >&2
    exit 1
fi
if [[ "$tag" != v* || "${tag#v}" != "$version" ]]; then
    echo "$me: the tag $tag does not match the CMake version $version (expected v$version)" >&2
    exit 1
fi

echo "numeric=$numeric"
echo "prerelease=$prerelease"
echo "version=$version"
