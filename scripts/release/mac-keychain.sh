#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# The signing keychain (spec §6.2, §6.6): CI's macOS leg imports the Developer ID certificates
# into a throwaway keychain before it configures with a real signing identity, and deletes the
# keychain at the end, in an always() step.
#
#   P12_PASSWORD=<password> scripts/release/mac-keychain.sh import <p12 file> <keychain path>
#   scripts/release/mac-keychain.sh probe <keychain path> <installer identity>
#   scripts/release/mac-keychain.sh delete <keychain path>
#
# import creates <keychain path> (which must end in .keychain-db and not exist yet) with a random
# password (openssl rand -hex 24, held only in this shell), sets it never to lock, unlocks it,
# imports <p12 file> with the password in P12_PASSWORD so codesign, pkgbuild, productbuild,
# productsign and security may use its keys (the keys' access list, security import -T: a tool
# not on it raises an access prompt, which a headless runner never answers, so the tool waits
# forever; make-installer.sh signs with pkgbuild and productbuild), puts it first in the user's
# keychain search list (keeping the rest), lets the Apple tools use the keys without a prompt
# (security set-key-partition-list, which the access list still needs), and prints the
# identities `security find-identity -p codesigning` finds in it: every matching one, then the
# valid ones (without -v, so an identity that isn't valid, say for a missing intermediate
# certificate, is still listed, and the log shows why codesign won't use it). A failure after
# the keychain is created deletes it again.
#
# probe checks that package signing works without a prompt: it signs a one-file test package
# with pkgbuild --sign <installer identity> --keychain <keychain path>, in the background, and
# fails (exit 1) if pkgbuild fails, printing its output, or if it hasn't finished in 90 s, the
# sign of a keychain access prompt. It never prints the identity: pkgbuild's output has it
# replaced. Its scratch files are in a mktemp -d directory, removed on exit.
#
# delete takes <keychain path> out of the user's search list and deletes it; a keychain that's
# already gone is no error, and prints nothing.
#
# Neither password is ever printed. They reach `security` as arguments, as that tool requires, so
# `ps` can see them for the moment it runs. A keychain in ~/Library/Keychains or
# /Library/Keychains (the login and System keychains) is refused, never touched.
#
# Exit status: 0 on success; non-zero (the failing command's status) when a security or openssl
# command fails, or probe's pkgbuild fails or blocks; 2, with a one-line reason on stderr, for
# bad usage, an unset or empty P12_PASSWORD, a missing <p12 file>, a refused keychain path, or
# (probe) a keychain that doesn't exist or an empty identity, before anything is created or
# changed.

set -euo pipefail
set +x # Never traced, even under bash -x or an inherited SHELLOPTS: no password is printed.

me="mac-keychain.sh"
usage() {
    echo "usage: P12_PASSWORD=<password> $0 import <p12 file> <keychain path>" >&2
    echo "       $0 probe <keychain path> <installer identity>" >&2
    echo "       $0 delete <keychain path>" >&2
    exit 2
}
refuse() { # <reason>...
    echo "$me: $*" >&2
    exit 2
}

# Prints the user's keychain search list, one path per line, unquoted.
search_list() {
    security list-keychains -d user | sed -e 's/^[[:space:]]*"//' -e 's/"[[:space:]]*$//'
}

# Sets $keychain to <path> made absolute, its directory resolved (security list-keychains records
# the path it's given, and create-keychain puts a bare name in ~/Library/Keychains), and refuses
# a name that isn't *.keychain-db or the system's keychain folders. A missing directory is
# refused unless <missing ok> is 1 (delete: then the keychain is gone, and $keychain is <path>
# made absolute as it stands).
resolve_keychain() { # <path> <missing ok>
    local path="$1" dir base home
    base="$(basename "$path")"
    [[ "$base" == ?*.keychain-db ]] || refuse "$path: the keychain's name must end in .keychain-db"
    if ! dir="$(cd "$(dirname "$path")" 2>/dev/null && pwd -P)"; then
        ((${2})) || refuse "$path: no such directory"
        dir="$(dirname "$path")"
        [[ "$dir" == /* ]] || dir="$(pwd -P)/$dir"
    fi
    home="$(cd "$HOME" 2>/dev/null && pwd -P)" || home="$HOME"
    case "$dir" in
        "$home/Library/Keychains" | "$HOME/Library/Keychains" | /Library/Keychains)
            refuse "$path: refusing a keychain in $dir" ;;
    esac
    keychain="$dir/$base"
}

# Sets the user's search list to what it is without $keychain, if $keychain is in it.
drop_from_search_list() {
    local line found=0
    local rest=()
    while IFS= read -r line; do
        if [[ "$line" == "$keychain" ]]; then
            found=1
        elif [[ -n "$line" ]]; then
            rest+=("$line")
        fi
    done < <(search_list)
    if ((found)); then
        security list-keychains -d user -s ${rest[@]+"${rest[@]}"}
    fi
}

delete_keychain() {
    drop_from_search_list
    if [[ -e "$keychain" ]]; then
        security delete-keychain "$keychain"
    fi
}

(($# >= 1)) || usage
command="$1"
shift
case "$command" in
    import)
        (($# == 2)) || usage
        p12="$1"
        [[ -n "${P12_PASSWORD:-}" ]] || refuse "P12_PASSWORD is unset or empty"
        [[ -f "$p12" ]] || refuse "$p12: no such file"
        resolve_keychain "$2" 0
        [[ ! -e "$keychain" ]] || refuse "$keychain: already exists"

        keychain_password="$(openssl rand -hex 24)"
        [[ "${#keychain_password}" == 48 ]] || {
            echo "$me: openssl rand -hex 24 failed" >&2
            exit 1
        }
        # The search list before ours goes first, captured before anything changes.
        existing=()
        while IFS= read -r line; do
            if [[ -n "$line" ]]; then
                existing+=("$line")
            fi
        done < <(search_list)

        created=0
        # A failure from here on deletes what was made, so a local run leaves no stray keychain.
        trap 'status=$?; if ((status != 0 && created)); then delete_keychain || true; fi' EXIT
        security create-keychain -p "$keychain_password" "$keychain"
        created=1
        security set-keychain-settings "$keychain" # No timeout and no lock on sleep.
        security unlock-keychain -p "$keychain_password" "$keychain"
        security import "$p12" -k "$keychain" -P "$P12_PASSWORD" -f pkcs12 \
            -T /usr/bin/codesign -T /usr/bin/pkgbuild -T /usr/bin/productbuild \
            -T /usr/bin/productsign -T /usr/bin/security
        security list-keychains -d user -s "$keychain" ${existing[@]+"${existing[@]}"}
        # It prints every key's attributes; only its status matters here.
        security set-key-partition-list -S apple-tool:,apple:,codesign: -s \
            -k "$keychain_password" "$keychain" >/dev/null
        keychain_password=""
        security find-identity -p codesigning "$keychain"
        ;;
    probe)
        (($# == 2)) || usage
        resolve_keychain "$1" 0
        [[ -f "$keychain" ]] || refuse "$keychain: no such keychain"
        identity="$2"
        [[ -n "$identity" ]] || refuse "the installer identity is empty"
        limit=90

        probe_dir="$(mktemp -d "${TMPDIR:-/tmp}/gg-keychain-probe.XXXXXX")"
        pid=""
        trap 'if [[ -n "$pid" ]]; then kill "$pid" 2>/dev/null || true; fi
            rm -rf "$probe_dir"' EXIT
        mkdir "$probe_dir/root"
        echo "probe" >"$probe_dir/root/probe.txt"
        # In the background, with a watchdog: a keychain access prompt blocks pkgbuild forever,
        # and macOS has no timeout command.
        pkgbuild --quiet --root "$probe_dir/root" --identifier org.sethbc.giga-grains.probe \
            --version 1 --keychain "$keychain" --sign "$identity" "$probe_dir/probe.pkg" \
            </dev/null >"$probe_dir/pkgbuild.log" 2>&1 &
        pid=$!
        waited=0
        while kill -0 "$pid" 2>/dev/null && ((waited < limit)); do
            sleep 1
            waited=$((waited + 1))
        done
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
            pid=""
            echo "$me: pkgbuild blocked for $limit s: the keychain is asking for access;" \
                "the key's access list must authorise /usr/bin/pkgbuild (security import -T)" >&2
            exit 1
        fi
        status=0
        wait "$pid" || status=$?
        pid=""
        if ((status != 0)); then
            log="$(cat "$probe_dir/pkgbuild.log")"
            echo "$me: pkgbuild failed (exit $status) to sign a test package with the installer" \
                "identity:" >&2
            printf '%s\n' "${log//"$identity"/<the installer identity>}" | sed 's/^/    /' >&2
            exit 1
        fi
        echo "probe: pkgbuild signed a test package"
        ;;
    delete)
        (($# == 1)) || usage
        resolve_keychain "$1" 1
        delete_keychain
        ;;
    *)
        usage
        ;;
esac
