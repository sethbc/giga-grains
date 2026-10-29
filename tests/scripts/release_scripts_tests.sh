#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks the release pipeline's scripts (spec 2026-10-29-release-pipeline-design.md), in scratch
# files under <scratch dir>, never this repository's:
#
#   tests/scripts/release_scripts_tests.sh <scratch dir>
#
# One function per script under test:
#   - test_check_version: scripts/release/check-version.sh (§6.1, §6.6) against scratch
#     CMakeLists.txt files: a tag matching the version with a suffix and without, a mismatched
#     tag, the suffix dev refused, and bad usage or an unparsable file.
#
# Prints "ok: <name>" or "FAIL: <name>" per check; the exit status is the number of failures
# (capped at 1).

set -euo pipefail

if (($# != 1)); then
    echo "usage: $0 <scratch dir>" >&2
    exit 2
fi
here="$(cd "$(dirname "$0")" && pwd)"
src="$(cd "$here/../.." && pwd)"
work="$1"
rm -rf "$work"
mkdir -p "$work"
work="$(cd "$work" && pwd)"

failures=0
out="" err="" status=0
check() { # <name> <condition result: 0 is true>
    if (($2 == 0)); then
        echo "ok: $1"
    else
        echo "FAIL: $1"
        failures=$((failures + 1))
    fi
}
# Fails <name> unless <text> contains the fixed string <expected>; shows the text if not.
expect_in() { # <name> <text> <expected>
    if grep -qF -- "$3" <<<"$2"; then
        check "$1" 0
    else
        check "$1" 1
        echo "    expected: $3"
        sed 's/^/    | /' <<<"$2"
    fi
}
# Fails <name> unless <actual> equals <expected>; shows both if not.
expect_eq() { # <name> <actual> <expected>
    if [[ "$2" == "$3" ]]; then
        check "$1" 0
    else
        check "$1" 1
        echo "    expected: $3"
        echo "    actual:   $2"
    fi
}

# --- check-version.sh -----------------------------------------------------------------------

# Writes a scratch CMakeLists.txt shaped like the repository's, with the suffix <s>.
write_version_cmake() { # <file> <s>
    cat >"$1" <<CMAKE
cmake_minimum_required(VERSION 3.22)
project(GigaGrains VERSION 0.2.0 LANGUAGES C CXX)
set(GG_VERSION_PRERELEASE "$2" CACHE STRING "...")
CMAKE
}
# Runs check-version.sh in this harness's bash (CTest's GG_BASH: bash 3.2 on macOS); sets $out
# (stdout), $err (stderr) and $status.
run_check_version() {
    status=0
    out="$("$BASH" "$src/scripts/release/check-version.sh" "$@" 2>"$work/check-version/stderr")" ||
        status=$?
    err="$(cat "$work/check-version/stderr")"
}

test_check_version() {
    local script="$src/scripts/release/check-version.sh"
    local dir="$work/check-version"
    mkdir -p "$dir"

    check "check-version: the script is executable" "$([[ -x "$script" ]] && echo 0 || echo 1)"

    local beta="$dir/beta.txt" release="$dir/release.txt" dev="$dir/dev.txt"
    write_version_cmake "$beta" "beta.1"
    write_version_cmake "$release" ""
    write_version_cmake "$dev" "dev"

    run_check_version v0.2.0-beta.1 "$beta"
    expect_eq "check-version: v0.2.0-beta.1 matches 0.2.0 + beta.1" "$status" 0
    expect_in "check-version: prints numeric=" "$out" "numeric=0.2.0"
    expect_in "check-version: prints prerelease=" "$out" "prerelease=beta.1"
    expect_in "check-version: prints version=" "$out" "version=0.2.0-beta.1"
    expect_eq "check-version: a match writes nothing to stderr" "$err" ""

    run_check_version v0.2.0 "$beta"
    expect_eq "check-version: v0.2.0 against 0.2.0-beta.1 is a mismatch" "$status" 1
    expect_in "check-version: the mismatch's reason names the version" "$err" "0.2.0-beta.1"

    run_check_version v0.2.1-beta.1 "$beta"
    expect_eq "check-version: v0.2.1-beta.1 against 0.2.0-beta.1 is a mismatch" "$status" 1

    run_check_version v0.2.0 "$release"
    expect_eq "check-version: v0.2.0 matches 0.2.0 + an empty suffix" "$status" 0
    expect_in "check-version: an empty suffix prints prerelease=" "$out" "prerelease="
    expect_in "check-version: an empty suffix prints version=0.2.0" "$out" "version=0.2.0"
    expect_eq "check-version: an empty suffix's output is exactly three lines" "$out" \
        "$(printf 'numeric=0.2.0\nprerelease=\nversion=0.2.0')"

    run_check_version v0.2.0-beta.1 "$release"
    expect_eq "check-version: v0.2.0-beta.1 against 0.2.0 is a mismatch" "$status" 1

    run_check_version v0.2.0-dev "$dev"
    expect_eq "check-version: the suffix dev is refused" "$status" 1
    expect_in "check-version: the refusal's reason mentions dev" "$err" "dev"
    expect_eq "check-version: the refusal prints nothing on stdout" "$out" ""

    run_check_version
    expect_eq "check-version: no arguments is bad usage" "$status" 2
    expect_in "check-version: bad usage prints the usage" "$err" "usage:"

    local noproject="$dir/noproject.txt"
    printf '%s\n' 'cmake_minimum_required(VERSION 3.22)' \
        'set(GG_VERSION_PRERELEASE "" CACHE STRING "...")' >"$noproject"
    run_check_version v0.2.0 "$noproject"
    expect_eq "check-version: a file without a project( line is unparsable" "$status" 2
    expect_eq "check-version: the unparsable file's reason is one line" \
        "$(printf '%s\n' "$err" | wc -l | tr -d ' ')" 1
}

test_check_version

echo "$failures failed"
((failures == 0))
