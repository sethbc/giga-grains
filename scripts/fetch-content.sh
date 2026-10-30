#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Fetches the content release that content/pin.txt pins (the CC0 samples the factory presets
# load; spec §6.5) into <dest>/content:
#
#   scripts/fetch-content.sh .content-cache
#
# Then configure with the -DGG_CONTENT_DIR=... option the script prints on its last line (with
# CMake-style forward slashes), and the build's gg-content target checks that folder against the
# pin instead of fetching its own. CI uses it so the content can be cached, as fetch-sdks.sh's
# SDKs are. A local build needs none of this: gg-content fetches into the build tree once.
#
# The work is scripts/fetch-content.cmake's: the download, its SHA-256 check, the extraction and
# <dest>/content/.gg-pin. A <dest> already at the pin costs no network.

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <dest>" >&2
    exit 2
fi
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$1"
dest="$(cd "$1" && pwd)"
# CMake wants Windows paths there, with forward slashes: cygpath -m gives them.
if command -v cygpath >/dev/null 2>&1; then
    dest="$(cygpath -m "$dest")"
    repo_root="$(cygpath -m "$repo_root")"
fi

cmake -DDEST="$dest" -DPIN="$repo_root/content/pin.txt" -P "$repo_root/scripts/fetch-content.cmake"
echo "-DGG_CONTENT_DIR=$dest/content"
