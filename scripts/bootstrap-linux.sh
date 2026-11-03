#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Installs what a Linux build of Giga Grains needs, and optionally clones, builds and tests it:
#
#   scripts/bootstrap-linux.sh [--with-format] [--clone DIR]
#
# For Ubuntu 22.04 and 24.04, and Debian-family systems with the same packages. Run it as root
# or with sudo available. It installs with apt whatever is missing of:
#   - gcc-13 and g++-13 (from the ubuntu-toolchain-r/test PPA on Ubuntu 22.04, whose archive has
#     none; from the archive on 24.04);
#   - cmake, ninja-build, git, curl, unzip, file, binutils, pkg-config, ca-certificates;
#   - the twelve JUCE development packages that CI's pinned prepare-for-juce action installs
#     (libfontconfig1-dev libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev
#     libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libfreetype6-dev
#     libglu1-mesa-dev libjack-jackd2-dev);
#   - libxi-dev (JUCE 9's juce_gui_basics includes XInput2.h) and xvfb (headless GUI tests).
# A second run finds everything present and changes nothing.
#
#   --with-format  also install clang-format 23 the way the Format workflow gets it: a Python
#                  virtual environment in ~/.local/share/giga-grains/format-venv with the
#                  hash-pinned wheel (x86_64 only), then print the CLANG_FORMAT export
#   --clone DIR    then clone the repository (with its submodules) into DIR, or update the clone
#                  there, and run `cmake --preset dev`, `cmake --build --preset dev` and
#                  `ctest --preset dev` in it, with gcc-13 as the compiler. GG_REPO_URL names
#                  another repository, and GG_REPO_REF a commit or branch to check out after the
#                  clone (CI sets it to the commit under test)
#
# Exit status: 0 on success, 1 when an install or a step fails, 2 for bad usage. Works in bash
# 3.2. The whole script is one function, called last, so `curl ... | bash -s -- ...` is safe: the trailing
# `main "$@"` and `exit $?` are the guard.

main() {
    set -euo pipefail

    local with_format=0 clone=""
    while (($# > 0)); do
        case "$1" in
        --with-format) with_format=1 ;;
        --clone)
            if (($# < 2)) || [[ -z "$2" ]]; then
                echo "bootstrap-linux: --clone needs a directory" >&2
                return 2
            fi
            clone="$2"
            shift
            ;;
        *)
            echo "bootstrap-linux: unknown option: $1" >&2
            echo "usage: bootstrap-linux.sh [--with-format] [--clone DIR]" >&2
            return 2
            ;;
        esac
        shift
    done

    local repo_url="${GG_REPO_URL:-https://github.com/sethbc/giga-grains.git}"
    local os_release="${GG_OS_RELEASE:-/etc/os-release}"

    local id="" id_like="" version_id=""
    if [[ -r "$os_release" ]]; then
        # shellcheck disable=SC1090,SC1091
        id="$(. "$os_release" && echo "${ID:-}")"
        # shellcheck disable=SC1090
        id_like="$(. "$os_release" && echo "${ID_LIKE:-}")"
        # shellcheck disable=SC1090
        version_id="$(. "$os_release" && echo "${VERSION_ID:-}")"
    fi
    case " $id $id_like " in
    *" ubuntu "* | *" debian "*) ;;
    *)
        echo "bootstrap-linux: this script supports Ubuntu and Debian-family systems (found '${id:-unknown}')" >&2
        return 1
        ;;
    esac

    local sudo_cmd=""
    if [[ "$(id -u)" != 0 ]]; then
        if ! command -v sudo >/dev/null 2>&1; then
            echo "bootstrap-linux: run as root, or install sudo" >&2
            return 1
        fi
        sudo_cmd="sudo"
    fi

    # installed <package>: 0 when dpkg lists it as installed.
    installed() {
        dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'install ok installed'
    }
    # missing_of <package>...: sets $missing to those not installed.
    local missing=()
    missing_of() {
        missing=()
        local p
        for p in "$@"; do
            if installed "$p"; then
                echo "found: $p"
            else
                missing+=("$p")
            fi
        done
    }

    local apt_updated=0
    apt_update() {
        if ((apt_updated == 0)); then
            $sudo_cmd apt-get update || return 1
            apt_updated=1
        fi
    }
    apt_install() {
        apt_update || return 1
        $sudo_cmd env DEBIAN_FRONTEND=noninteractive apt-get install -y "$@"
    }

    echo "== Compiler: gcc-13"
    missing_of gcc-13 g++-13
    if ((${#missing[@]} > 0)); then
        if [[ "$id" == ubuntu && "$version_id" == 22.04 ]]; then
            echo "Ubuntu 22.04's archive has no gcc-13: adding ppa:ubuntu-toolchain-r/test"
            local prereq=()
            installed software-properties-common || prereq+=(software-properties-common)
            installed ca-certificates || prereq+=(ca-certificates)
            if ((${#prereq[@]} > 0)); then
                echo "installing: ${prereq[*]}"
                apt_install "${prereq[@]}" || {
                    echo "bootstrap-linux: apt-get install failed" >&2
                    return 1
                }
            fi
            $sudo_cmd env DEBIAN_FRONTEND=noninteractive add-apt-repository -y ppa:ubuntu-toolchain-r/test || {
                echo "bootstrap-linux: adding the PPA failed" >&2
                return 1
            }
            apt_updated=0
        fi
        echo "installing: gcc-13 g++-13"
        apt_install gcc-13 g++-13 || {
            echo "bootstrap-linux: apt-get install failed" >&2
            return 1
        }
    fi

    echo "== Build tools and libraries"
    missing_of cmake ninja-build git curl unzip file binutils pkg-config ca-certificates \
        libfontconfig1-dev libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev \
        libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libfreetype6-dev \
        libglu1-mesa-dev libjack-jackd2-dev libxi-dev xvfb
    if ((${#missing[@]} > 0)); then
        echo "installing: ${missing[*]}"
        apt_install "${missing[@]}" || {
            echo "bootstrap-linux: apt-get install failed" >&2
            return 1
        }
    else
        echo "nothing to install"
    fi

    if ((with_format == 1)); then
        echo "== clang-format 23"
        local venv="${HOME:-/root}/.local/share/giga-grains/format-venv"
        if [[ -x "$venv/bin/clang-format" ]]; then
            echo "found: $venv/bin/clang-format"
        else
            # The pin and hash are the Format workflow's (.github/workflows/format.yml): the
            # manylinux x86_64 wheel's.
            installed python3-venv || apt_install python3-venv python3-pip || return 1
            python3 -m venv "$venv"
            echo "clang-format==23.1.3 --hash=sha256:58bf1804216501669832a39f2415e14284337438a062ff564729792adec664ac" >"$venv/requirements.txt"
            "$venv/bin/pip" install --quiet --only-binary :all: --require-hashes -r "$venv/requirements.txt" || {
                echo "bootstrap-linux: the clang-format install failed (the pinned wheel is x86_64 only)" >&2
                return 1
            }
            echo "installed: $venv/bin/clang-format"
        fi
        echo "export CLANG_FORMAT=$venv/bin/clang-format"
    fi

    if [[ -n "$clone" ]]; then
        echo "== Clone, build and test: $clone"
        if [[ -e "$clone/.git" ]]; then
            echo "found a clone: updating its submodules"
            git -C "$clone" submodule update --init --recursive
        else
            git clone --recurse-submodules "$repo_url" "$clone"
            if [[ -n "${GG_REPO_REF:-}" ]]; then
                git -C "$clone" checkout "$GG_REPO_REF"
                git -C "$clone" submodule update --init --recursive
            fi
        fi
        cd "$clone"
        export CC=gcc-13 CXX=g++-13
        cmake --preset dev
        cmake --build --preset dev
        ctest --preset dev
    fi
    echo "bootstrap-linux: done"
}

main "$@"
exit $?
