#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# The release's complete corresponding source (spec decision 11, §6.6): a tarball of the tree with
# every submodule at its pin. GitHub's automatic archives omit the submodules; this one doesn't.
#
#   scripts/release/source-tarball.sh <repo> <full-version> <out.tar.gz>
#
# Writes <out.tar.gz> holding, under giga-grains-<full-version>/, the tracked files of <repo> (the
# top of a git work tree) at its HEAD and of every submodule, recursively, at its checked-out
# commit: `git archive` of each, extracted into one staging directory, then `tar -czf`. Tracked
# files such as .gitmodules and .gitignore stay; no .git file or directory and no untracked file
# goes in.
#
# Refused, with a reason on stderr, before anything is written:
#   - a submodule some .gitmodules names that isn't checked out (an empty directory, as after
#     `git submodule deinit`), each one named: exit 1;
#   - a file carrying the export-ignore or export-subst attribute, which `git archive` would drop
#     or rewrite, so the archive would no longer be the tracked files: exit 1;
#   - bad usage: exit 2.
# The staged tree must hold exactly as many files as the repositories track (a case-insensitive
# file system could merge two of them), or nothing is written: exit 1. A failure leaves no partial
# output, and an existing <out.tar.gz> is replaced only on success.

set -euo pipefail

me="source-tarball.sh"
usage() {
    echo "usage: $0 <repo> <full-version> <out.tar.gz>" >&2
    exit 2
}
fail() { # <status> <reason>...
    local status="$1"
    shift
    echo "$me: $*" >&2
    exit "$status"
}
(($# == 3)) || usage
repo="$1"
version="$2"
out="$3"

# The version names the top directory, so it may not hold a slash or start with a dot.
if [[ ! "$version" =~ ^[0-9A-Za-z][0-9A-Za-z.+-]*$ ]]; then
    fail 2 "the version '$version' may hold only 0-9, A-Z, a-z, '.', '+' and '-'," \
        "and must start with a letter or digit"
fi
[[ -d "$repo" ]] || fail 2 "$repo: no such directory"
repo="$(cd "$repo" && pwd -P)"
top="$(git -C "$repo" rev-parse --show-toplevel 2>/dev/null)" ||
    fail 2 "$repo: not in a git work tree"
[[ "$(cd "$top" && pwd -P)" == "$repo" ]] ||
    fail 2 "$repo: not the top of its git work tree ($top)"
out_dir="$(dirname "$out")"
[[ -d "$out_dir" ]] || fail 2 "$out: the directory $out_dir does not exist"
[[ ! -d "$out" ]] || fail 2 "$out: is a directory"
out="$(cd "$out_dir" && pwd -P)/$(basename "$out")"

# --- Refusals ------------------------------------------------------------------------------

# Every submodule each .gitmodules names, the superproject's and every checked-out submodule's,
# must be checked out: `git submodule status` prefixes one that isn't with -, and a conflicted one
# with U. Appends each one that isn't, relative to the top, to $missing (one per line), and the
# relative path of each that is to $repos.
missing=""
repos="."
check_submodules() { # <dir> <prefix>
    local dir="$1" prefix="$2" entry path state
    [[ -f "$dir/.gitmodules" ]] || return 0
    while IFS= read -r -d '' entry; do
        path="${entry#*$'\n'}"
        state="$(git -C "$dir" submodule status -- "$path" 2>/dev/null || true)"
        case "$state" in
            " "* | "+"*)
                repos="$repos"$'\n'"$prefix$path"
                check_submodules "$dir/$path" "$prefix$path/"
                ;;
            *) missing="$missing$prefix$path"$'\n' ;;
        esac
    done < <(git -C "$dir" config -z --file .gitmodules --get-regexp '^submodule\..*\.path$' ||
        true)
}
check_submodules "$repo" ""
if [[ -n "$missing" ]]; then
    {
        echo "$me: these submodules are not checked out" \
            "(run git submodule update --init --recursive):"
        printf '%s' "$missing" | sed 's/^/    /'
    } >&2
    exit 1
fi

# The files of each repository that git archive would drop (export-ignore) or rewrite
# (export-subst), as `git check-attr` sees the checked-out tree (its .gitattributes files, and
# $GIT_DIR/info/attributes and core.attributesFile, which git archive reads too); one line each,
# "<repo>: <file>: <attribute>: <value>".
exported=""
while IFS= read -r rel; do
    hits="$(git -C "$repo/$rel" ls-tree -r -z --name-only HEAD |
        git -C "$repo/$rel" check-attr --stdin -z export-ignore export-subst |
        tr '\0' '\n' | paste - - - |
        awk -F '\t' -v r="$rel" \
            '$3 != "unspecified" && $3 != "unset" { print r ": " $1 ": " $2 ": " $3 }')"
    [[ -z "$hits" ]] || exported="$exported$hits"$'\n'
done <<<"$repos"
if [[ -n "$exported" ]]; then
    {
        echo "$me: git archive would drop or rewrite these tracked files" \
            "(export-ignore or export-subst):"
        printf '%s' "$exported" | sed 's/^/    /'
    } >&2
    exit 1
fi

# --- The archive ---------------------------------------------------------------------------

root="giga-grains-$version"
staging="$(mktemp -d "${TMPDIR:-/tmp}/gg-source-tarball.XXXXXX")"
partial="$out.tmp.$$"
trap 'rm -rf "$staging"; rm -f "$partial"' EXIT
mkdir "$staging/tree"

# Each submodule's archive is written to a file before it's extracted: foreach runs its command in
# sh, which has no pipefail, so a pipe would hide a failed git archive.
git -C "$repo" archive --format=tar --prefix="$root/" -o "$staging/repo.tar" HEAD
tar -xf "$staging/repo.tar" -C "$staging/tree"
GG_STAGING="$staging" GG_ROOT="$root" git -C "$repo" submodule foreach --quiet --recursive '
    git archive --format=tar --prefix="$GG_ROOT/$displaypath/" -o "$GG_STAGING/repo.tar" HEAD &&
        tar -xf "$GG_STAGING/repo.tar" -C "$GG_STAGING/tree"'
rm -f "$staging/repo.tar"

tracked=0
while IFS= read -r rel; do
    n="$(git -C "$repo/$rel" ls-tree -r HEAD | grep -cv '^160000 ' || true)"
    tracked=$((tracked + n))
done <<<"$repos"
staged="$(find "$staging/tree" ! -type d | wc -l | tr -d ' ')"
((staged == tracked)) ||
    fail 1 "the staged tree holds $staged files but the repositories track $tracked" \
        "(a case-insensitive file system?); nothing written"

# COPYFILE_DISABLE keeps macOS's tar from adding ._ entries for extended attributes.
COPYFILE_DISABLE=1 tar -C "$staging/tree" -czf "$partial" "$root"
mv -f "$partial" "$out"
