#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks scripts/bootstrap-macos.sh and scripts/bootstrap-linux.sh (M2 close spec §6.3) against
# stub brew, apt-get, dpkg-query, sudo, xcode-select, git, cmake and ctest on a PATH that holds
# nothing else but a few symlinked coreutils, never installing anything:
#
#   tests/scripts/bootstrap_tests.sh <scratch dir>
#
# For each script: a bare machine gets exactly the spec's package list, a second run installs
# nothing, an unknown option or a missing --clone directory exits 2, and --clone runs the clone
# and the three preset commands in order, in the clone. macOS: --ci leaves llvm out, and missing
# Xcode command-line tools exit 1 with the xcode-select instruction. Linux: the PPA is added on
# 22.04 and not on 24.04, and a non-Debian system exits 1. Last, the README's one-liners name a
# script that exists and an option that script takes, and the Windows script and its developer
# shell are present (they run only on Windows).
#
# Prints "ok: <name>" or "FAIL: <name>" per check; the exit status is 1 if any failed.

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
if [[ -x /bin/bash ]]; then bash_bin=/bin/bash; else bash_bin="$(command -v bash)"; fi

failures=0
check() { # <name> <0 is true>
    if (($2 == 0)); then
        echo "ok: $1"
    else
        echo "FAIL: $1"
        failures=$((failures + 1))
    fi
}
expect_eq() { # <name> <actual> <expected>
    if [[ "$2" == "$3" ]]; then
        check "$1" 0
    else
        check "$1" 1
        echo "    expected: $3"
        echo "    actual:   $2"
    fi
}

# new_machine <name>: a scratch machine with stubs; sets $stubs, $log, $state, $tpl.
new_machine() {
    local dir="$work/$1"
    stubs="$dir/bin" tpl="$dir/tpl" state="$dir/state" log="$dir/log.txt"
    mkdir -p "$stubs" "$tpl" "$state/dpkg"
    : >"$log"
    local c
    for c in id grep dirname mkdir cp env; do ln -s "$(command -v "$c")" "$stubs/$c"; done
    # A logging stub: "<name> <args>" plus the directory it ran in.
    for c in cmake ctest ninja git; do
        cat >"$tpl/$c" <<STUB
#!/bin/bash
echo "$c \$* @\$(pwd)" >>"$log"
if [[ "$c" == git && "\$1" == clone ]]; then mkdir -p "\${@: -1}"; fi
exit 0
STUB
        chmod +x "$tpl/$c"
    done
    cat >"$stubs/xcode-select" <<STUB
#!/bin/bash
[[ -e "$state/no-xcode" ]] && exit 2
echo /Library/Developer/CommandLineTools
STUB
    cat >"$stubs/brew" <<STUB
#!/bin/bash
case "\$1" in
install)
    echo "brew \$*" >>"$log"
    shift
    for p in "\$@"; do
        : >"$state/brew-\$p"
        if [[ -e "$tpl/\$p" ]]; then cp "$tpl/\$p" "$stubs/\$p"; fi
        # cmake brings ctest.
        if [[ "\$p" == cmake ]]; then cp "$tpl/ctest" "$stubs/ctest"; fi
    done
    ;;
list) [[ -e "$state/brew-\${@: -1}" ]] ;;
esac
STUB
    cat >"$stubs/dpkg-query" <<STUB
#!/bin/bash
[[ -e "$state/dpkg/\${@: -1}" ]] && echo -n "install ok installed"
exit 0
STUB
    cat >"$stubs/apt-get" <<STUB
#!/bin/bash
echo "apt-get \$*" >>"$log"
if [[ "\$1" == install ]]; then
    shift
    for p in "\$@"; do [[ "\$p" == -* ]] || : >"$state/dpkg/\$p"; done
fi
STUB
    cat >"$stubs/add-apt-repository" <<STUB
#!/bin/bash
echo "add-apt-repository \$*" >>"$log"
STUB
    cat >"$stubs/sudo" <<STUB
#!/bin/bash
echo "sudo \$*" >>"$log"
exec "\$@"
STUB
    # Only the stubs written above: the symlinked system tools are never chmod-ed (chmod follows
    # a link to the real, possibly root-owned, file).
    for c in xcode-select brew dpkg-query apt-get add-apt-repository sudo; do chmod +x "$stubs/$c"; done
}
# run <script> [args...]: runs it on the stub PATH; sets $out and $rc.
run() {
    local script="$1"
    shift
    set +e
    out="$(cd "$work" && PATH="$stubs" GG_REPO_URL="file:///fake/repo.git" "$bash_bin" "$src/scripts/$script" "$@" 2>&1)"
    rc=$?
    set -e
}
installed_set() { (cd "$state/dpkg" && printf '%s\n' * | sort | tr '\n' ' '); }

# ---- macOS
new_machine mac-bare
run bootstrap-macos.sh
expect_eq "macos: a bare machine installs cmake ninja git llvm" "$(grep '^brew' "$log")" "brew install cmake ninja git llvm"
check "macos: bare run exits 0" $((rc))
run bootstrap-macos.sh
expect_eq "macos: a second run installs nothing" "$(grep -c '^brew' "$log")" "1"
check "macos: second run says nothing to install" "$(grep -q 'nothing to install' <<<"$out" && echo 0 || echo 1)"

new_machine mac-ci
run bootstrap-macos.sh --ci
expect_eq "macos: --ci leaves llvm out" "$(grep '^brew' "$log")" "brew install cmake ninja git"

new_machine mac-usage
run bootstrap-macos.sh --bogus
expect_eq "macos: an unknown option exits 2" "$rc" "2"
run bootstrap-macos.sh --clone
expect_eq "macos: --clone without a directory exits 2" "$rc" "2"
expect_eq "macos: bad usage installs nothing" "$(grep -c '^brew' "$log")" "0"

new_machine mac-noxcode
: >"$state/no-xcode"
run bootstrap-macos.sh
expect_eq "macos: missing command-line tools exit 1" "$rc" "1"
check "macos: it says to run xcode-select --install" "$(grep -q 'xcode-select --install' <<<"$out" && echo 0 || echo 1)"
expect_eq "macos: and installs nothing" "$(grep -c '^brew' "$log")" "0"

new_machine mac-clone
run bootstrap-macos.sh --clone "$work/mac-clone/gg"
expect_eq "macos: --clone exits 0" "$rc" "0"
expect_eq "macos: --clone clones then runs the three dev presets, in the clone" \
    "$(grep -v '^brew' "$log")" \
    "git clone --recurse-submodules file:///fake/repo.git $work/mac-clone/gg @$work
cmake --preset dev @$work/mac-clone/gg
cmake --build --preset dev @$work/mac-clone/gg
ctest --preset dev @$work/mac-clone/gg"
: >"$log"
run bootstrap-macos.sh --clone "$work/mac-clone/gg"
: >"$work/mac-clone/gg/.git" # a clone from here on
: >"$log"
run bootstrap-macos.sh --clone "$work/mac-clone/gg"
expect_eq "macos: an existing clone is updated, not cloned again" \
    "$(grep -c 'git clone' "$log")/$(grep -c 'submodule update --init --recursive' "$log")" "0/1"

# ---- Linux
printf 'ID=ubuntu\nID_LIKE=debian\nVERSION_ID="22.04"\n' >"$work/os-22.04"
printf 'ID=ubuntu\nID_LIKE=debian\nVERSION_ID="24.04"\n' >"$work/os-24.04"
printf 'ID=fedora\nVERSION_ID="40"\n' >"$work/os-fedora"
juce="libfontconfig1-dev libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libfreetype6-dev libglu1-mesa-dev libjack-jackd2-dev"
common="g++-13 gcc-13 cmake ninja-build git curl unzip file binutils pkg-config ca-certificates $juce libxi-dev xvfb"
# shellcheck disable=SC2086
sorted() { printf '%s\n' $1 | sort | tr '\n' ' '; }

new_machine linux-2204
for c in cmake ctest git; do cp "$tpl/$c" "$stubs/$c"; done
export GG_OS_RELEASE="$work/os-22.04"
run bootstrap-linux.sh
expect_eq "linux 22.04: a bare machine gets exactly the package list" "$(installed_set)" "$(sorted "$common software-properties-common")"
expect_eq "linux 22.04: the PPA is added" "$(grep -c '^add-apt-repository -y ppa:ubuntu-toolchain-r/test' "$log")" "1"
check "linux 22.04: apt runs through sudo" "$(grep -q '^sudo env DEBIAN_FRONTEND=noninteractive apt-get' "$log" && echo 0 || echo 1)"
check "linux 22.04: gcc-13 comes after the PPA" "$(awk '/add-apt-repository/{p=1} p&&/install.*gcc-13/{f=1} END{exit !f}' "$log" && echo 0 || echo 1)"
expect_eq "linux 22.04: bare run exits 0" "$rc" "0"
: >"$log"
run bootstrap-linux.sh
expect_eq "linux 22.04: a second run calls apt-get and the PPA not at all" "$(grep -c 'apt' "$log")" "0"

new_machine linux-2404
for c in cmake ctest git; do cp "$tpl/$c" "$stubs/$c"; done
export GG_OS_RELEASE="$work/os-24.04"
run bootstrap-linux.sh
expect_eq "linux 24.04: the archive's gcc-13, no PPA" "$(grep -c 'add-apt-repository' "$log")" "0"
expect_eq "linux 24.04: the same package list" "$(installed_set)" "$(sorted "$common")"

new_machine linux-usage
export GG_OS_RELEASE="$work/os-24.04"
run bootstrap-linux.sh --bogus
expect_eq "linux: an unknown option exits 2" "$rc" "2"
run bootstrap-linux.sh --clone
expect_eq "linux: --clone without a directory exits 2" "$rc" "2"
expect_eq "linux: bad usage installs nothing" "$(grep -c 'apt' "$log")" "0"
export GG_OS_RELEASE="$work/os-fedora"
run bootstrap-linux.sh
expect_eq "linux: a non-Debian system exits 1" "$rc" "1"

new_machine linux-clone
for c in cmake ctest git; do cp "$tpl/$c" "$stubs/$c"; done
export GG_OS_RELEASE="$work/os-24.04"
run bootstrap-linux.sh --clone "$work/linux-clone/gg"
expect_eq "linux: --clone exits 0" "$rc" "0"
expect_eq "linux: --clone clones then runs the three dev presets, in the clone" \
    "$(grep -E '^(git|cmake|ctest)' "$log")" \
    "git clone --recurse-submodules file:///fake/repo.git $work/linux-clone/gg @$work
cmake --preset dev @$work/linux-clone/gg
cmake --build --preset dev @$work/linux-clone/gg
ctest --preset dev @$work/linux-clone/gg"

new_machine linux-ref
for c in cmake ctest git; do cp "$tpl/$c" "$stubs/$c"; done
GG_REPO_REF=abc123 run bootstrap-linux.sh --clone "$work/linux-ref/gg"
expect_eq "linux: GG_REPO_REF is checked out, with the submodules, after the clone" \
    "$(grep -E '^git' "$log" | sed 's/ @.*//')" \
    "git clone --recurse-submodules file:///fake/repo.git $work/linux-ref/gg
git -C $work/linux-ref/gg checkout abc123
git -C $work/linux-ref/gg submodule update --init --recursive"
unset GG_OS_RELEASE

# ---- Syntax and shellcheck
for s in bootstrap-macos.sh bootstrap-linux.sh; do
    "$bash_bin" -n "$src/scripts/$s"
    check "$s parses" $?
done
if command -v shellcheck >/dev/null 2>&1; then
    shellcheck "$src/scripts/bootstrap-macos.sh" "$src/scripts/bootstrap-linux.sh" "$0" >/dev/null 2>&1
    check "shellcheck is clean" $?
else
    echo "skipped: shellcheck is not installed"
fi

# ---- The README's one-liners
check "the Windows script and its developer shell exist" \
    "$([[ -f $src/scripts/bootstrap-windows.ps1 && -f $src/scripts/dev-shell-windows.ps1 ]] && echo 0 || echo 1)"
for os in macos:--clone linux:--clone windows:-Clone; do
    name="${os%%:*}" opt="${os#*:}"
    ext='sh'
    if [[ "$name" == windows ]]; then ext='ps1'; fi
    script="scripts/bootstrap-$name.$ext"
    line="$(grep -F "raw.githubusercontent.com/sethbc/giga-grains/main/$script" "$src/README.md" | head -n 1 || true)"
    check "README has a one-liner for $name" "$([[ -n "$line" ]] && echo 0 || echo 1)"
    check "README's $name one-liner passes $opt" "$(grep -qF -- " $opt " <<<"$line" && echo 0 || echo 1)"
    check "$script exists and takes $opt" "$([[ -f $src/$script ]] && grep -qF -- "$opt" "$src/$script" && echo 0 || echo 1)"
done

((failures == 0)) || exit 1
