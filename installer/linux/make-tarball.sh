#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Makes the Linux tarball (spec §6.5). The gg-installer target runs it (cmake/products.cmake).
#
#   make-tarball.sh --products <dir> --out <dir> --full-version <v> --readme <README.md>
#
#   --products      the four products, as gg-products gathers them: Giga Grains.clap,
#                   Giga Grains.vst3 (a folder bundle), and the standalones Giga Grains and
#                   Giga Grains FX (executables); and licenses/, the license set.
#   --out           where to write giga-grains-<v>-linux-x86_64.tar.gz.
#   --full-version  the full version string, <v> (the file name and its top-level folder).
#   --readme        installer/linux/README.md, which goes in as README.md.
#
# Every entry sits under giga-grains-<v>/: the four products, licenses/ and README.md. Modes are
# kept, executable bits included (tar, not zip; INTENT §19), with everything made readable by
# all and writable by its owner alone; entries are owned by root (uid and gid 0), so a copy
# extracted as root isn't owned by the build machine's user. No install script: README.md says
# where each product goes.
#
# Exit status: 0 when done; 1 when a step fails; 2 for bad usage or a missing product, before
# anything is written.

set -euo pipefail

me="make-tarball.sh"

# die <message> [exit status, default 1]
die() {
    echo "$me: $1" >&2
    exit "${2:-1}"
}

usage() {
    if [ -n "${1:-}" ]; then
        echo "$me: $1" >&2
    fi
    echo "usage: $me --products <dir> --out <dir> --full-version <v> --readme <README.md>" >&2
    exit 2
}

products=""
out=""
full_version=""
readme=""

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --products | --out | --full-version | --readme)
                [ $# -ge 2 ] || usage "$1 needs a value"
                case "$1" in
                    --products) products="$2" ;;
                    --out) out="$2" ;;
                    --full-version) full_version="$2" ;;
                    --readme) readme="$2" ;;
                esac
                shift 2
                ;;
            -h | --help) usage ;;
            *) usage "unknown argument '$1'" ;;
        esac
    done
    [ -n "$products" ] || usage "--products is required"
    [ -n "$out" ] || usage "--out is required"
    [ -n "$full_version" ] || usage "--full-version is required"
    [ -n "$readme" ] || usage "--readme is required"

    # The full version goes into the file name and its folder: only these characters.
    local full_re='^[0-9]+\.[0-9]+\.[0-9]+([-+][0-9A-Za-z.-]+)?$'
    if ! [[ "$full_version" =~ $full_re ]]; then
        usage "--full-version '$full_version' isn't X.Y.Z with an optional -suffix"
    fi
}

check_inputs() {
    local missing=""
    [ -f "$products/Giga Grains.clap" ] || missing="$missing 'Giga Grains.clap'"
    [ -d "$products/Giga Grains.vst3" ] || missing="$missing 'Giga Grains.vst3'"
    [ -f "$products/Giga Grains" ] || missing="$missing 'Giga Grains'"
    [ -f "$products/Giga Grains FX" ] || missing="$missing 'Giga Grains FX'"
    [ -d "$products/licenses" ] || missing="$missing 'licenses'"
    [ -z "$missing" ] || die "missing from '$products':$missing" 2

    local app
    for app in "Giga Grains" "Giga Grains FX"; do
        [ -x "$products/$app" ] || die "'$products/$app' isn't executable" 2
    done
    [ -f "$products/licenses/LICENSE" ] ||
        die "the license set '$products/licenses' has no LICENSE" 2
    [ -f "$readme" ] || die "the README '$readme' isn't a file" 2
}

# The work folder goes on exit; on a failure, so does the tarball, so no half-written one is left
# in --out to be mistaken for a finished one.
work=""
tarball=""
cleanup() {
    local status=$?
    if [ -n "$work" ]; then
        rm -rf "$work"
    fi
    if [ "$status" -ne 0 ] && [ -n "$tarball" ]; then
        rm -f "$tarball"
    fi
}

stage() {
    local folder="$1" name
    mkdir -p "$folder"
    for name in "Giga Grains.clap" "Giga Grains.vst3" "Giga Grains" "Giga Grains FX" licenses; do
        cp -R "$products/$name" "$folder/"
    done
    cp "$readme" "$folder/README.md"
    # X keeps an execute bit only where one was (and on folders), whatever the umask.
    chmod -R u+rwX,go+rX,go-w "$folder"
}

# GNU tar (Linux) and bsdtar (macOS, where the script is also tried) name root ownership
# differently. COPYFILE_DISABLE keeps bsdtar from adding macOS's ._ metadata files. The version
# is read whole, not piped into grep -q: under pipefail, grep's early exit could fail tar.
make_tar() {
    local file="$1" parent="$2" top="$3" version
    version="$(tar --version 2>/dev/null)" || version=""
    case "$version" in
        *"GNU tar"*) tar -czf "$file" --owner=0 --group=0 --numeric-owner -C "$parent" "$top" ;;
        *) COPYFILE_DISABLE=1 tar -czf "$file" --uid 0 --gid 0 -C "$parent" "$top" ;;
    esac
}

main() {
    parse_args "$@"
    check_inputs

    local top="giga-grains-$full_version"
    tarball="$out/$top-linux-x86_64.tar.gz"
    echo "Giga Grains $full_version: $(basename "$tarball")"

    work="$(mktemp -d "${TMPDIR:-/tmp}/gg-tarball.XXXXXX")"
    trap cleanup EXIT
    mkdir -p "$out"
    rm -f "$tarball"

    stage "$work/$top"
    make_tar "$tarball" "$work" "$top"

    echo "--- wrote:"
    ls -l "$tarball"
}

main "$@"
