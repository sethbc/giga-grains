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
#     tag, the suffix dev refused, --print (no tag; dev accepted), and bad usage, an unparsable
#     file or a suffix with an empty identifier.
#   - test_source_tarball: scripts/release/source-tarball.sh (decision 11, §6.6) on a recursive
#     clone of a scratch superproject with one submodule: the archive holds the tracked files of
#     both under giga-grains-<version>/, no .git file or directory and no untracked file; a
#     deinitialised submodule, an export-ignore attribute and bad usage are refused.
#   - test_mac_keychain (macOS only): scripts/release/mac-keychain.sh (§6.2) imports a scratch
#     self-signed code-signing identity into a scratch keychain, lists it, never prints either
#     password, and deletes the keychain, leaving the user's keychain search list as it found it;
#     a missing or wrong password, a missing .p12 and bad usage are refused, creating nothing. An
#     EXIT trap deletes the scratch keychain and takes it out of the search list even when a
#     check fails. The user's login keychain is never passed to the script.
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

    run_check_version --print "$dev"
    expect_eq "check-version: --print accepts the suffix dev" "$status" 0
    expect_eq "check-version: --print prints the three lines for 0.2.0-dev" "$out" \
        "$(printf 'numeric=0.2.0\nprerelease=dev\nversion=0.2.0-dev')"
    expect_eq "check-version: --print writes nothing to stderr" "$err" ""

    run_check_version --print "$release"
    expect_eq "check-version: --print with an empty suffix prints version=0.2.0" "$out" \
        "$(printf 'numeric=0.2.0\nprerelease=\nversion=0.2.0')"

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
    expect_in "check-version: the unparsable file's reason names the project( line" "$err" \
        "expected one 'project(GigaGrains VERSION X.Y.Z' line"
    run_check_version --print "$noproject"
    expect_eq "check-version: --print on a file without a project( line is unparsable" \
        "$status" 2

    local emptyid="$dir/emptyid.txt"
    write_version_cmake "$emptyid" "beta..1"
    run_check_version v0.2.0-beta..1 "$emptyid"
    expect_eq "check-version: a suffix with an empty identifier (beta..1) is refused: exit 2" \
        "$status" 2
    expect_in "check-version: the refusal says the identifier is empty" "$err" \
        "has an empty identifier"
    expect_eq "check-version: the refusal prints nothing on stdout" "$out" ""
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

# --- mac-keychain.sh ------------------------------------------------------------------------

# Prints the user's keychain search list, one path per line, unquoted.
user_keychains() {
    security list-keychains -d user | sed -e 's/^[[:space:]]*"//' -e 's/"[[:space:]]*$//'
}
# Takes <keychain> out of the user's search list, if it's there, and deletes it, if it exists,
# without the script under test: the EXIT trap's cleanup, so a failing check never leaves a stray
# keychain in the search list. Only ever called with the scratch keychain's path.
remove_scratch_keychain() { # <keychain>
    local kc="$1" line
    local rest=()
    while IFS= read -r line; do
        if [[ -n "$line" && "$line" != "$kc" ]]; then
            rest+=("$line")
        fi
    done < <(user_keychains)
    if user_keychains | grep -qxF -- "$kc"; then
        security list-keychains -d user -s ${rest[@]+"${rest[@]}"}
    fi
    if [[ -e "$kc" ]]; then
        security delete-keychain "$kc" || true
    fi
}
# 0 when <keychain> is in the user's search list, else 1.
in_search_list() { # <keychain>
    user_keychains | grep -qxF -- "$1" && echo 0 || echo 1
}

test_mac_keychain() {
    if [[ "$(uname)" != Darwin ]]; then
        echo "ok: mac-keychain: skipped (not macOS)"
        return 0
    fi
    local script="$src/scripts/release/mac-keychain.sh"
    local dir="$work/mac-keychain"
    mkdir -p "$dir"
    dir="$(cd "$dir" && pwd -P)"
    local kc="$dir/t.keychain-db" p12="$dir/t.p12"
    mac_keychain_scratch="$kc"
    trap 'remove_scratch_keychain "$mac_keychain_scratch"' EXIT

    check "mac-keychain: the script is executable" "$([[ -x "$script" ]] && echo 0 || echo 1)"

    local before
    before="$(user_keychains)"

    # A self-signed identity with the code-signing extended key usage, so find-identity's
    # codesigning policy matches it. The .p12 uses SHA-1/3DES, which every macOS's security import
    # reads, rather than OpenSSL 3's PBES2 default, which an older one rejects; LibreSSL and
    # OpenSSL 3 and 4 all write it.
    openssl req -x509 -newkey rsa:2048 -keyout "$dir/k.pem" -out "$dir/c.pem" -days 2 -nodes \
        -subj '/CN=gg test' -addext 'extendedKeyUsage=codeSigning' >/dev/null 2>&1
    openssl pkcs12 -export -out "$p12" -inkey "$dir/k.pem" -in "$dir/c.pem" \
        -passout pass:secret -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg sha1

    # Refusals create nothing.
    run_release_script mac-keychain.sh
    expect_eq "mac-keychain: no arguments is bad usage" "$status" 2
    expect_in "mac-keychain: bad usage prints the usage" "$err" "usage:"
    run_release_script mac-keychain.sh frob "$kc"
    expect_eq "mac-keychain: an unknown command is bad usage" "$status" 2
    run_release_script mac-keychain.sh import "$p12"
    expect_eq "mac-keychain: import without a keychain is bad usage" "$status" 2
    run_release_script mac-keychain.sh delete
    expect_eq "mac-keychain: delete without a keychain is bad usage" "$status" 2

    unset P12_PASSWORD
    run_release_script mac-keychain.sh import "$p12" "$kc"
    expect_eq "mac-keychain: import without P12_PASSWORD: exit 2" "$status" 2
    expect_in "mac-keychain: the refusal names P12_PASSWORD" "$err" "P12_PASSWORD"
    export P12_PASSWORD=""
    run_release_script mac-keychain.sh import "$p12" "$kc"
    expect_eq "mac-keychain: import with an empty P12_PASSWORD: exit 2" "$status" 2
    check "mac-keychain: the refusals create no keychain" "$([[ -e "$kc" ]] && echo 1 || echo 0)"

    # These refusals run with P12_PASSWORD=secret: none may print it.
    export P12_PASSWORD=secret
    local refused=""
    run_release_script mac-keychain.sh import "$dir/missing.p12" "$kc"
    expect_eq "mac-keychain: a missing .p12: exit 2" "$status" 2
    refused="$refused$out$err"
    run_release_script mac-keychain.sh import "$p12" "$dir/t.keychain"
    expect_eq "mac-keychain: a keychain not named *.keychain-db: exit 2" "$status" 2
    refused="$refused$out$err"
    run_release_script mac-keychain.sh import "$p12" "$dir/nope/t.keychain-db"
    expect_eq "mac-keychain: a keychain in a missing directory: exit 2" "$status" 2
    refused="$refused$out$err"
    check "mac-keychain: the path refusals never print the .p12 password" \
        "$(grep -qF secret <<<"$refused" && echo 1 || echo 0)"
    check "mac-keychain: the bad paths create no keychain" "$([[ -e "$kc" ]] && echo 1 || echo 0)"

    # A wrong password fails the import, and the half-made keychain is deleted again.
    export P12_PASSWORD=wrongpw
    run_release_script mac-keychain.sh import "$p12" "$kc"
    check "mac-keychain: a wrong password fails the import" "$( ((status != 0)) && echo 0 || echo 1)"
    check "mac-keychain: a failed import leaves no keychain" "$([[ -e "$kc" ]] && echo 1 || echo 0)"
    expect_eq "mac-keychain: a failed import leaves the search list alone" "$(user_keychains)" \
        "$before"
    check "mac-keychain: a failed import never prints the password" \
        "$(grep -qF wrongpw <<<"$out$err" && echo 1 || echo 0)"

    export P12_PASSWORD=secret
    run_release_script mac-keychain.sh import "$p12" "$kc"
    expect_eq "mac-keychain: import: exit 0" "$status" 0
    expect_in "mac-keychain: import lists the identity" "$out" "gg test"
    check "mac-keychain: import never prints the .p12 password" \
        "$(grep -qF secret <<<"$out$err" && echo 1 || echo 0)"
    # The keychain's own password is 48 hex digits (openssl rand -hex 24); the identity's SHA-1
    # hash, which find-identity prints, is 40.
    check "mac-keychain: import never prints the keychain's password" \
        "$(grep -qE '[0-9A-Fa-f]{48}' <<<"$out$err" && echo 1 || echo 0)"
    check "mac-keychain: import creates the keychain" "$([[ -f "$kc" ]] && echo 0 || echo 1)"
    expect_eq "mac-keychain: import puts the keychain first in the search list, keeping the rest" \
        "$(user_keychains)" "$(printf '%s\n%s' "$kc" "$before")"
    check "mac-keychain: the identity is in the keychain" \
        "$(security find-identity -p codesigning "$kc" | grep -qF '"gg test"' && echo 0 || echo 1)"

    run_release_script mac-keychain.sh import "$p12" "$kc"
    expect_eq "mac-keychain: import into an existing keychain: exit 2" "$status" 2
    check "mac-keychain: that refusal never prints the .p12 password" \
        "$(grep -qF secret <<<"$out$err" && echo 1 || echo 0)"
    check "mac-keychain: the refusal leaves the keychain" "$([[ -f "$kc" ]] && echo 0 || echo 1)"

    # A keychain in the user's own Keychains folder is refused (here a scratch HOME's, so the
    # real login keychain is never named, even if the guard were broken), whether the folder is
    # there or not.
    HOME="$dir/home" run_release_script mac-keychain.sh delete \
        "$dir/home/Library/Keychains/login.keychain-db"
    expect_eq "mac-keychain: delete refuses a keychain in a missing ~/Library/Keychains" \
        "$status" 2
    mkdir -p "$dir/home/Library/Keychains"
    HOME="$dir/home" run_release_script mac-keychain.sh delete \
        "$dir/home/Library/Keychains/login.keychain-db"
    expect_eq "mac-keychain: delete refuses a keychain in ~/Library/Keychains" "$status" 2

    run_release_script mac-keychain.sh delete "$kc"
    expect_eq "mac-keychain: delete: exit 0" "$status" 0
    check "mac-keychain: delete removes the file" "$([[ -e "$kc" ]] && echo 1 || echo 0)"
    expect_eq "mac-keychain: delete takes it out of the search list" "$(in_search_list "$kc")" 1
    expect_eq "mac-keychain: the search list is as it was" "$(user_keychains)" "$before"

    run_release_script mac-keychain.sh delete "$kc"
    expect_eq "mac-keychain: deleting a keychain that's gone: exit 0" "$status" 0
    expect_eq "mac-keychain: ... quietly" "$out$err" ""
    run_release_script mac-keychain.sh delete "$dir/gone/t.keychain-db"
    expect_eq "mac-keychain: deleting a keychain whose directory is gone: exit 0" "$status" 0

    unset P12_PASSWORD
    remove_scratch_keychain "$kc"
    trap - EXIT
}

test_check_version
test_source_tarball
test_mac_keychain

echo "$failures failed"
((failures == 0))
