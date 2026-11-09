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
# its render), and compares each case's WAV SHA-256 with
# <worktree>/tests/golden/at-rest/linux-x86_64-dev.sha256. WAVs match only on one platform and
# build type, so it refuses any platform but Linux x86_64.
#
# It rewrites expected files while it runs, so stage files by name.
#
# The last line is "baseline cases: N, changed: C, gone: G". Exit status: 0 when nothing changed
# or went, 1 when something did or the renders failed, 2 for bad usage, an unsupported platform, or
# a missing baseline or build tree.

set -euo pipefail

if (($# != 1)); then
    echo "usage: $0 <worktree>" >&2
    exit 2
fi
if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
    echo "check-at-rest: only the linux-x86_64-dev baseline exists; this is" \
        "$(uname -s) $(uname -m)" >&2
    exit 2
fi
cd "$1"
base=tests/golden/at-rest/linux-x86_64-dev.sha256
if [[ ! -f "$base" ]]; then
    echo "check-at-rest: no baseline at $base" >&2
    exit 2
fi
if [[ ! -d build/dev ]]; then
    echo "check-at-rest: no build/dev here; run scripts/worktree-setup.sh --configure dev" >&2
    exit 2
fi

if ! scripts/update-goldens.sh build/dev >build/dev/update-goldens.log 2>&1; then
    tail -20 build/dev/update-goldens.log
    exit 1
fi

restore=()
while read -r _ name; do
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
if ((${#restore[@]} > 0)); then
    git checkout -- "${restore[@]}"
fi

changed=0
gone=0
while read -r sha name; do
    now="$(awk -v n="$name" '$2 == n { print $1 }' build/dev/golden/wav-sha256.txt)"
    if [[ -z "$now" ]]; then
        echo "GONE $name"
        gone=$((gone + 1))
    elif [[ "$now" != "$sha" ]]; then
        echo "CHANGED $name"
        changed=$((changed + 1))
    fi
done <"$base"
echo "baseline cases: $(wc -l <"$base" | tr -d ' '), changed: $changed, gone: $gone"
((changed == 0 && gone == 0))
