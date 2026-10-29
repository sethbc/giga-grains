#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks out every submodule of a fresh linked worktree (git worktree add ...), recursively, at the
# commits its HEAD pins, taking the objects from the main checkout's submodule repositories, so it
# works offline. It changes no URL in the shared .git/config, in the main checkout's submodule
# configs or in any other worktree's. The only submodule.*.url entries it writes are the canonical
# .gitmodules URLs of nested submodules, into the configs of this worktree's own new clones of
# their parents. Each new clone's own config also gets a remote.origin.url: its .gitmodules URL,
# as a clone from upstream would record.
#
#   scripts/worktree-setup.sh [--configure <preset>[,<preset>...]]
#
# Run the worktree's own copy of this script. Each name given to --configure must be a configure
# preset (scripts/preset-dir.cmake checks it), or the script fails before it clones anything.
# git submodule status --recursive must then show every submodule checked out at its pinned
# commit (no line starting with -, + or U), or the script fails. Last, --configure runs cmake
# --preset for each preset (rtsan with GG_LLVM=/opt/homebrew/opt/llvm on macOS if GG_LLVM is unset
# and that exists); it doesn't build.
#
# How. git keeps a linked worktree's submodule repositories in .git/worktrees/<id>/modules/, but
# the config they're registered in, .git/config, is shared with the main checkout and every other
# worktree. A plain `git submodule update --init` clones each submodule from the URL registered
# there, and earlier ad hoc setups rewrote those URLs to local paths to get local objects. Instead,
# each submodule is cloned by one git command that carries two per-command overrides (git -c,
# which reach the clone git runs, and are never written anywhere):
#   - url.<main checkout's copy>.insteadOf=<its .gitmodules URL>: git fetches from the local
#     copy, but records the .gitmodules URL as the clone's origin, as a clone from upstream would;
#   - for a top-level submodule, submodule.<name>.url=<its .gitmodules URL>: the clone uses that
#     URL whatever the shared config holds, and `git submodule init` finds the URL already set,
#     so it writes nothing to the shared config. (That's also why a top-level submodule must
#     already be registered there: without its URL in the shared config it would be inactive in
#     every worktree. Run `git submodule init` once in the main checkout.)
# A nested submodule's URL is registered in its parent's config, which here is this worktree's
# own new clone, so `git submodule init` writes it there, exactly as a recursive clone would.
# --reference and alternates would avoid copying objects, but the clones would then depend on
# objects the main checkout may later prune; and they still contact the URL, so they aren't
# offline. protocol.file.allow=always lets the submodule clone use the local path, which git
# otherwise refuses for submodules. --no-recommend-shallow makes the clone a full one: a shallow
# fetch of a pinned commit that isn't a branch tip isn't guaranteed from a local copy.
#
# A submodule whose commit the main checkout lacks is fetched from its URL, which needs the
# network; the script says which.

set -euo pipefail

usage() {
    echo "usage: $0 [--configure <preset>[,<preset>...]]" >&2
    exit 2
}

configure=""
case "$#:${1:-}" in
    0:) ;;
    2:--configure) configure="$2" ;;
    *) usage ;;
esac
if [[ "$#" == 2 && -z "$configure" ]]; then
    usage
fi

cd "$(dirname "$0")/.."
root="$(pwd)"

# Every preset name is checked before anything is cloned, so a typo fails at once.
presets=()
if [[ -n "$configure" ]]; then
    IFS=, read -r -a presets <<<"$configure"
    for preset in ${presets[@]+"${presets[@]}"}; do
        resolved="$(cmake -D "PRESET=$preset" -P scripts/preset-dir.cmake 2>&1 |
            tr -d '\r' || true)"
        if [[ "$resolved" != "dir "* ]]; then
            echo "error: --configure $preset: ${resolved#error }" >&2
            exit 2
        fi
    done
fi

git_dir="$(cd "$(git rev-parse --git-dir)" && pwd -P)"
common_dir="$(cd "$(git rev-parse --git-common-dir)" && pwd -P)"
if [[ "$git_dir" == "$common_dir" ]]; then
    echo "error: $root is the main checkout, not a linked worktree;" \
        "use git submodule update --init --recursive there" >&2
    exit 2
fi

# Checks out the submodules of the repository at $1. $2 holds the main checkout's copies of them,
# as <name> subdirectories (empty if it has none); $3 is "top" for the superproject itself.
setup_submodules() {
    local super="$1" sources="$2" level="$3"
    local -a names paths overrides
    local entry key i name path sha url src display
    names=()
    paths=()
    if [[ ! -f "$super/.gitmodules" ]]; then
        return 0
    fi
    # "submodule.<name>.path\n<path>\0" per submodule. Collected first, so the git commands below
    # never read this list as their stdin.
    while IFS= read -r -d '' entry; do
        key="${entry%%$'\n'*}"
        key="${key#submodule.}"
        names+=("${key%.path}")
        paths+=("${entry#*$'\n'}")
    done < <(git -C "$super" config -f .gitmodules -z --get-regexp '^submodule\..*\.path$' || true)

    for ((i = 0; i < ${#names[@]}; i++)); do
        name="${names[i]}"
        path="${paths[i]}"
        display="${super#"$root"}/$path"
        display="${display#/}"
        # The gitlink HEAD pins: "160000 commit <sha>\t<path>".
        entry="$(git -C "$super" ls-tree HEAD -- "$path")"
        case "$entry" in
            "160000 commit "*)
                sha="${entry#160000 commit }"
                sha="${sha%%$'\t'*}"
                ;;
            *) continue ;; # in .gitmodules, but HEAD pins no commit there
        esac

        overrides=()
        if [[ "$level" == top ]]; then
            url="$(git -C "$super" config -f .gitmodules "submodule.$name.url")"
            if ! git -C "$super" config "submodule.$name.url" >/dev/null; then
                echo "error: submodule $name isn't registered in the shared git config;" \
                    "run git submodule init in the main checkout first" >&2
                exit 1
            fi
            overrides=(-c "submodule.$name.url=$url")
        else
            git -C "$super" submodule init -q -- "$path" </dev/null
            url="$(git -C "$super" config "submodule.$name.url")"
        fi

        src=""
        if [[ -n "$sources" && -d "$sources/$name" ]] &&
            git --git-dir="$sources/$name" cat-file -e "$sha^{commit}" 2>/dev/null; then
            src="$sources/$name"
            git -C "$super" -c protocol.file.allow=always -c "url.$src.insteadOf=$url" \
                ${overrides[@]+"${overrides[@]}"} \
                submodule update --init --no-recommend-shallow -q -- "$path" </dev/null
        else
            echo "note: the main checkout has no copy of $display at ${sha:0:12};" \
                "fetching it from $url"
            git -C "$super" ${overrides[@]+"${overrides[@]}"} \
                submodule update --init -q -- "$path" </dev/null
        fi

        setup_submodules "$super/$path" "${src:+$src/modules}" nested
    done
}

setup_submodules "$root" "$common_dir/modules" top

status="$(git submodule status --recursive)"
bad="$(grep -E '^[-+U]' <<<"$status" || true)"
if [[ -n "$bad" ]]; then
    echo "error: these submodules aren't at the commits HEAD pins:" >&2
    echo "$bad" >&2
    exit 1
fi
echo "$(grep -c . <<<"$status") submodules checked out at their pinned commits"

if [[ -n "$configure" ]]; then
    for preset in ${presets[@]+"${presets[@]}"}; do
        if [[ "$preset" == rtsan && -z "${GG_LLVM:-}" && "$(uname -s)" == Darwin &&
            -d /opt/homebrew/opt/llvm ]]; then
            echo "rtsan: GG_LLVM=/opt/homebrew/opt/llvm"
            GG_LLVM=/opt/homebrew/opt/llvm cmake --preset "$preset"
        else
            cmake --preset "$preset"
        fi
    done
fi
