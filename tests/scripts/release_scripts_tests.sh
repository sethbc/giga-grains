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
#   - test_source_tarball: scripts/release/source-tarball.sh (decision 11, §6.6) on a recursive
#     clone of a scratch superproject with one submodule: the archive holds the tracked files of
#     both under giga-grains-<version>/, no .git file or directory and no untracked file; a
#     deinitialised submodule, an export-ignore attribute and bad usage are refused.
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
# Runs scripts/release/<script> in this harness's bash (CTest's GG_BASH: bash 3.2 on macOS); sets
# $out (stdout), $err (stderr) and $status. Needs nothing but $work, so any test_* may call it.
run_release_script() { # <script> [<arg>...]
    local script="$1"
    shift
    status=0
    out="$("$BASH" "$src/scripts/release/$script" "$@" 2>"$work/stderr")" || status=$?
    err="$(cat "$work/stderr")"
}
run_check_version() { run_release_script check-version.sh "$@"; }

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

# --- source-tarball.sh ----------------------------------------------------------------------

# Prints "<n> <entries>": the number of entries in the tarball <file> that are, or are inside, a
# .git file or directory (a path component exactly .git; .gitmodules and .github don't count),
# then those entries.
git_entries() { # <file>
    local hits
    hits="$(tar -tzf "$1" | grep -E '(^|/)\.git(/|$)' || true)"
    if [[ -z "$hits" ]]; then
        echo 0
    else
        echo "$(printf '%s\n' "$hits" | wc -l | tr -d ' ') $hits"
    fi
}

test_source_tarball() {
    local script="$src/scripts/release/source-tarball.sh"
    local dir="$work/source-tarball"
    mkdir -p "$dir"

    check "source-tarball: the script is executable" "$([[ -x "$script" ]] && echo 0 || echo 1)"

    # Only these repositories: no global or system config, and git stops looking at $work. The
    # submodule's upstream is a local path, which git refuses for submodules unless allowed.
    export GIT_CEILING_DIRECTORIES="$work" GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null
    export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.invalid
    export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.invalid
    export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=protocol.file.allow GIT_CONFIG_VALUE_0=always

    local sub="$dir/sub-upstream" upstream="$dir/upstream" clone="$dir/clone"
    git init -q -b main "$sub"
    echo "sub" >"$sub/sub.txt"
    git -C "$sub" add sub.txt
    git -C "$sub" commit -q -m "sub"
    git init -q -b main "$upstream"
    echo "readme" >"$upstream/README"
    git -C "$upstream" add README
    git -C "$upstream" submodule add -q "$sub" libs/sub
    git -C "$upstream" commit -q -m "README and libs/sub"
    git clone -q --recurse-submodules "$upstream" "$clone"
    echo "untracked" >"$clone/extra.txt"
    echo "untracked" >"$clone/libs/sub/extra.txt"

    local tgz="$dir/out.tar.gz" listing
    run_release_script source-tarball.sh "$clone" 9.9.9 "$tgz"
    expect_eq "source-tarball: a recursive clone: exit 0" "$status" 0
    expect_eq "source-tarball: a success writes nothing to stderr" "$err" ""
    listing="$(tar -tzf "$tgz" 2>&1 || true)"
    expect_in "source-tarball: the archive holds the superproject's README" "$listing" \
        "giga-grains-9.9.9/README"
    expect_in "source-tarball: the archive holds the submodule's file" "$listing" \
        "giga-grains-9.9.9/libs/sub/sub.txt"
    expect_in "source-tarball: the archive keeps the tracked .gitmodules" "$listing" \
        "giga-grains-9.9.9/.gitmodules"
    expect_eq "source-tarball: no .git file or directory" "$(git_entries "$tgz")" 0
    expect_eq "source-tarball: every entry is under giga-grains-9.9.9/" \
        "$(grep -cv '^giga-grains-9\.9\.9/' <<<"$listing" || true)" 0
    check "source-tarball: no untracked file" \
        "$(grep -q 'extra\.txt' <<<"$listing" && echo 1 || echo 0)"
    expect_eq "source-tarball: the submodule's file has its committed content" \
        "$(tar -xzOf "$tgz" giga-grains-9.9.9/libs/sub/sub.txt 2>&1)" "sub"

    # A refusal writes nothing: the previous archive stays as it was, and no partial file appears.
    local before
    before="$(cksum "$tgz" 2>&1 || true)"
    git -C "$clone" submodule deinit -q -f libs/sub
    run_release_script source-tarball.sh "$clone" 9.9.9 "$tgz"
    expect_eq "source-tarball: a deinitialised submodule: exit 1" "$status" 1
    expect_in "source-tarball: the refusal names the submodule" "$err" "libs/sub"
    expect_eq "source-tarball: the refusal leaves the output alone" \
        "$(cksum "$tgz" 2>&1 || true)" "$before"
    expect_eq "source-tarball: the refusal leaves no partial file" \
        "$(find "$dir" -maxdepth 1 -name 'out.tar.gz?*' | wc -l | tr -d ' ')" 0
    git -C "$clone" submodule update -q --init

    # git archive would drop an export-ignore path and rewrite an export-subst file, so the
    # archive would no longer be the tracked files: refused, naming the repository.
    echo "sub.txt export-ignore" >"$sub/.gitattributes"
    git -C "$sub" add .gitattributes
    git -C "$sub" commit -q -m "export-ignore"
    git -C "$clone/libs/sub" fetch -q
    git -C "$clone/libs/sub" checkout -q origin/main
    run_release_script source-tarball.sh "$clone" 9.9.9 "$dir/attr.tar.gz"
    expect_eq "source-tarball: an export-ignore attribute: exit 1" "$status" 1
    expect_in "source-tarball: the export-ignore refusal names the submodule" "$err" "libs/sub"
    check "source-tarball: the export-ignore refusal writes no archive" \
        "$([[ -e "$dir/attr.tar.gz" ]] && echo 1 || echo 0)"
    git -C "$clone/libs/sub" checkout -q HEAD~1

    run_release_script source-tarball.sh
    expect_eq "source-tarball: no arguments is bad usage" "$status" 2
    expect_in "source-tarball: bad usage prints the usage" "$err" "usage:"
    run_release_script source-tarball.sh "$clone" "9.9/9" "$dir/slash.tar.gz"
    expect_eq "source-tarball: a version with a slash is bad usage" "$status" 2
    run_release_script source-tarball.sh "$dir/nope" 9.9.9 "$dir/nope.tar.gz"
    expect_eq "source-tarball: a repository that isn't there is bad usage" "$status" 2
    run_release_script source-tarball.sh "$clone/libs" 9.9.9 "$dir/libs.tar.gz"
    expect_eq "source-tarball: a directory below a work tree's top is bad usage" "$status" 2
    run_release_script source-tarball.sh "$clone" 9.9.9 "$dir/nope/out.tar.gz"
    expect_eq "source-tarball: an output in a missing directory is bad usage" "$status" 2
}

test_check_version
test_source_tarball

echo "$failures failed"
((failures == 0))
