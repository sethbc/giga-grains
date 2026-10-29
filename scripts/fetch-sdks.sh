#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Fetches the SDKs that clap-wrapper downloads at configure time (CPM), into <dest>, at exactly
# the tags its cmake/base_sdks.cmake pins at our clap-wrapper submodule:
#
#   scripts/fetch-sdks.sh .sdk-cache
#
# Then configure with the -D...SDK_ROOT options the script prints on its last lines (one per
# line, ready for the command line, with CMake-style forward slashes), and clap-wrapper uses
# these checkouts instead of downloading its own. CI uses it so the checkouts can be cached
# (carry-over [96]): CPM's own cache doesn't help here, because clap-wrapper gives every package
# a SOURCE_DIR, which makes CPM skip CPM_SOURCE_CACHE. A local build needs none of this: the
# presets leave the options unset and clap-wrapper downloads as it always has.
#
# The pins are read from base_sdks.cmake, not copied here, so a clap-wrapper bump changes what
# this fetches. A checkout already in <dest> at the same pin is kept (the pin is recorded in
# <dest>/<name>/.gg-pin), so a restored cache costs no network. The CLAP SDK (we build the
# submodule's) and the AAX SDK (never built) are skipped.

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <dest>" >&2
    exit 2
fi
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
base_sdks="$repo_root/libs/clap/clap-wrapper/cmake/base_sdks.cmake"
mkdir -p "$1"
dest="$(cd "$1" && pwd)"

if [[ ! -f "$base_sdks" ]]; then
    echo "$0: no $base_sdks: run git submodule update --init" >&2
    exit 1
fi

# One line per CPMAddPackage: NAME|GITHUB_REPOSITORY|GIT_TAG|GIT_SUBMODULES (comment lines are
# skipped, and so is any argument that isn't one of these four).
packages="$(awk '
    /^[[:space:]]*#/ { next }
    /CPMAddPackage\(/ { inside = 1; name = repo = tag = subs = ""; next }
    inside && /^[[:space:]]*\)/ {
        print name "|" repo "|" tag "|" subs
        inside = 0
        next
    }
    inside {
        line = $0
        gsub(/^[[:space:]]+/, "", line)
        key = line
        sub(/[[:space:]].*$/, "", key)
        value = line
        sub(/^[^[:space:]]+[[:space:]]+/, "", value)
        gsub(/"/, "", value)
        sub(/[[:space:]]+$/, "", value)
        if (key == "NAME") name = value
        else if (key == "GITHUB_REPOSITORY") repo = value
        else if (key == "GIT_TAG") tag = value
        else if (key == "GIT_SUBMODULES") subs = value
    }
' "$base_sdks")"

# The option clap-wrapper reads for each package, and the file that shows a checkout is right.
root_variable() {
    case "$1" in
        vst3sdk) echo VST3_SDK_ROOT ;;
        AudioUnitSDK) echo AUDIOUNIT_SDK_ROOT ;;
        rtaudio) echo RTAUDIO_SDK_ROOT ;;
        rtmidi) echo RTMIDI_SDK_ROOT ;;
        wil) echo WIL_SDK_ROOT ;;
        *) return 1 ;;
    esac
}
signal_file() {
    case "$1" in
        vst3sdk) echo public.sdk/source/main/pluginfactory.cpp ;;
        AudioUnitSDK) echo src/AudioUnitSDK ;;
        rtaudio) echo RtAudio.h ;;
        rtmidi) echo RtMidi.h ;;
        wil) echo include/wil/common.h ;;
    esac
}

options=()
fetched=0
while IFS='|' read -r name repo tag subs; do
    variable="$(root_variable "$name")" || continue
    if [[ -z "$repo" || -z "$tag" ]]; then
        echo "$0: no repository or tag for $name in $base_sdks" >&2
        exit 1
    fi
    pin="$repo $tag $subs"
    dir="$dest/$name"
    if [[ -f "$dir/.gg-pin" && "$(cat "$dir/.gg-pin")" == "$pin" ]]; then
        echo "$name: $tag already in $dir"
    else
        echo "$name: cloning $repo at $tag"
        rm -rf "$dir"
        git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$tag" "https://github.com/$repo" "$dir"
        if [[ -n "$subs" ]]; then
            # GIT_SUBMODULES lists the submodules to check out, separated by spaces.
            # shellcheck disable=SC2086
            git -C "$dir" submodule update --quiet --init --depth 1 $subs
        fi
        printf '%s\n' "$pin" >"$dir/.gg-pin"
        fetched=$((fetched + 1))
    fi
    if [[ ! -e "$dir/$(signal_file "$name")" ]]; then
        echo "$0: $dir has no $(signal_file "$name"); the checkout is wrong" >&2
        exit 1
    fi
    # CMake wants forward slashes; cygpath -m gives a Windows drive path with them.
    if command -v cygpath >/dev/null 2>&1; then
        cmake_dir="$(cygpath -m "$dir")"
    else
        cmake_dir="$dir"
    fi
    options+=("-D$variable=$cmake_dir")
done <<<"$packages"

if [[ ${#options[@]} -ne 5 ]]; then
    echo "$0: expected 5 SDKs in $base_sdks, found ${#options[@]}" >&2
    exit 1
fi
echo "fetched $fetched of 5; the rest were already there"
printf '%s\n' "${options[@]}"
