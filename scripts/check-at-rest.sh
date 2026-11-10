#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# The at-rest check: proves a new feature, at rest, moves no existing golden's audio.
#
#   scripts/check-at-rest.sh <worktree>
#
# It runs scripts/update-goldens.sh in <worktree>/build/dev, restores the committed expected files
# of the baseline's cases (this machine's metrics may differ in the last digits; a newer case keeps
# its render), and compares each case's WAV SHA-256 with this platform's baseline,
# <worktree>/tests/golden/at-rest/<os>-<arch>-dev.sha256 (linux-x86_64-dev, say). A WAV's exact
# bytes differ between platforms, so each platform has its own file. To add one, run
# scripts/update-goldens.sh build/dev in a dev build at a commit whose audio is known to be right
# and commit build/dev/golden/wav-sha256.txt as that file.
#
# Because it restores those files, it refuses to run over an uncommitted edit of one. Newer cases'
# expected files are rewritten and stay so, so stage files by name.
#
# The last line is "baseline cases: N, changed: C, gone: G". Exit status: 0 when nothing changed
# or went, 1 when something did or the renders failed, 2 for bad usage, no baseline for this
# platform, no build tree, or an uncommitted edit of a baseline case's expected file.

set -euo pipefail

if (($# != 1)); then
    echo "usage: $0 <worktree>" >&2
    exit 2
fi
cd "$1"
base="tests/golden/at-rest/$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)-dev.sha256"
if [[ ! -f "$base" ]]; then
    echo "check-at-rest: this platform has no baseline yet ($base). To add one, run" \
        "scripts/update-goldens.sh build/dev at a commit whose audio is known to be right," \
        "then commit build/dev/golden/wav-sha256.txt as that file." >&2
    exit 2
fi
if [[ ! -d build/dev ]]; then
    echo "check-at-rest: no build/dev here; run scripts/worktree-setup.sh --configure dev" >&2
    exit 2
fi

restore=()
while read -r _ name; do
    [[ -n "$name" ]] || continue
    case "$name" in
        presets/*)
            dir=tests/golden/presets/expected
            case_name=${name#presets/}
            ;;
        *)
            dir=tests/golden/expected
            case_name=$name
            ;;
    esac
    for f in "$dir/$case_name.metrics.json" "$dir/$case_name.events.jsonl"; do
        if git ls-files --error-unmatch "$f" >/dev/null 2>&1; then
            restore+=("$f")
        fi
    done
done <"$base"
if ((${#restore[@]} > 0)) && ! git diff --quiet HEAD -- "${restore[@]}"; then
    echo "check-at-rest: these baseline expected files have uncommitted edits, which it would" \
        "discard; commit them, or run git checkout -- tests/golden:" >&2
    git diff --name-only HEAD -- "${restore[@]}" >&2
    exit 2
fi
restore_expected() {
    if ((${#restore[@]} > 0)); then
        git checkout -- "${restore[@]}"
    fi
}

if ! scripts/update-goldens.sh build/dev >build/dev/update-goldens.log 2>&1; then
    tail -20 build/dev/update-goldens.log
    restore_expected
    exit 1
fi
restore_expected

cases=0
changed=0
gone=0
while read -r sha name; do
    [[ -n "$name" ]] || continue
    cases=$((cases + 1))
    now="$(awk -v n="$name" '$2 == n { print $1 }' build/dev/golden/wav-sha256.txt)"
    if [[ -z "$now" ]]; then
        echo "GONE $name"
        gone=$((gone + 1))
    elif [[ "$now" != "$sha" ]]; then
        echo "CHANGED $name"
        changed=$((changed + 1))
    fi
done <"$base"
echo "baseline cases: $cases, changed: $changed, gone: $gone"
((changed == 0 && gone == 0))
