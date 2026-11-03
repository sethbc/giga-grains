#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Installs what a macOS build of Giga Grains needs, and optionally clones, builds and tests it:
#
#   scripts/bootstrap-macos.sh [--ci] [--clone DIR]
#
# It checks the Xcode command-line tools (if they are missing it asks for `xcode-select
# --install`, which opens a dialog no script can answer, and exits 1), then installs with
# Homebrew (itself installed first if missing) whatever of cmake, ninja, git and llvm is
# missing. Homebrew's llvm gives scripts/format.sh its clang-format 23 and the rtsan preset its
# LLVM 20 or newer. Each tool is reported as found or installed; a second run changes nothing.
#
#   --ci           skip llvm (CI's macOS leg builds `mac-universal` and doesn't use it)
#   --clone DIR    then clone the repository (with its submodules) into DIR, or update the clone
#                  there, and run `cmake --preset dev`, `cmake --build --preset dev` and
#                  `ctest --preset dev` in it
#
# Exit status: 0 on success, 1 when an install or a step fails, 2 for bad usage. Works in
# macOS's bash 3.2. The whole script is one function, called last, so `curl ... | bash -s -- ...`
# is safe: bash has read it all before anything runs.

main() {
    set -euo pipefail

    local ci=0 clone=""
    while (($# > 0)); do
        case "$1" in
        --ci) ci=1 ;;
        --clone)
            if (($# < 2)) || [[ -z "$2" ]]; then
                echo "bootstrap-macos: --clone needs a directory" >&2
                return 2
            fi
            clone="$2"
            shift
            ;;
        *)
            echo "bootstrap-macos: unknown option: $1" >&2
            echo "usage: bootstrap-macos.sh [--ci] [--clone DIR]" >&2
            return 2
            ;;
        esac
        shift
    done

    local repo_url="${GG_REPO_URL:-https://github.com/sethbc/giga-grains.git}"

    echo "== Xcode command-line tools"
    if ! xcode-select -p >/dev/null 2>&1; then
        echo "The Xcode command-line tools are missing. Run:  xcode-select --install"
        echo "Finish the dialog it opens, then run this script again."
        return 1
    fi
    echo "found: $(xcode-select -p)"

    echo "== Homebrew"
    if ! command -v brew >/dev/null 2>&1; then
        if [[ -x /opt/homebrew/bin/brew ]]; then
            eval "$(/opt/homebrew/bin/brew shellenv)"
        elif [[ -x /usr/local/bin/brew ]]; then
            eval "$(/usr/local/bin/brew shellenv)"
        fi
    fi
    if command -v brew >/dev/null 2>&1; then
        echo "found: $(command -v brew)"
    else
        echo "installing Homebrew"
        NONINTERACTIVE=1 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)" </dev/null || {
            echo "bootstrap-macos: the Homebrew install failed" >&2
            return 1
        }
        if [[ -x /opt/homebrew/bin/brew ]]; then
            eval "$(/opt/homebrew/bin/brew shellenv)"
        elif [[ -x /usr/local/bin/brew ]]; then
            eval "$(/usr/local/bin/brew shellenv)"
        fi
        command -v brew >/dev/null 2>&1 || {
            echo "bootstrap-macos: brew is not on the PATH after its install" >&2
            return 1
        }
    fi

    echo "== Build tools"
    local missing=() tool
    for tool in cmake ninja git; do
        if command -v "$tool" >/dev/null 2>&1; then
            echo "found: $tool ($(command -v "$tool"))"
        else
            missing+=("$tool")
        fi
    done
    if ((ci == 0)); then
        if brew list --formula llvm >/dev/null 2>&1; then
            echo "found: llvm (Homebrew)"
        else
            missing+=(llvm)
        fi
    else
        echo "skipped: llvm (--ci)"
    fi
    if ((${#missing[@]} > 0)); then
        echo "installing: ${missing[*]}"
        HOMEBREW_NO_AUTO_UPDATE=1 brew install "${missing[@]}" || {
            echo "bootstrap-macos: brew install failed" >&2
            return 1
        }
    else
        echo "nothing to install"
    fi
    if ((ci == 0)); then
        echo "For the rtsan preset: GG_LLVM=\$(brew --prefix llvm) cmake --preset rtsan"
    fi

    if [[ -n "$clone" ]]; then
        echo "== Clone, build and test: $clone"
        if [[ -e "$clone/.git" ]]; then
            echo "found a clone: updating its submodules"
            git -C "$clone" submodule update --init --recursive
        else
            git clone --recurse-submodules "$repo_url" "$clone"
        fi
        cd "$clone"
        cmake --preset dev
        cmake --build --preset dev
        ctest --preset dev
    fi
    echo "bootstrap-macos: done"
}

main "$@"
exit $?
