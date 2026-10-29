#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Regenerates the golden renders' expected files from the current engine.
#
#   scripts/update-goldens.sh [build-dir]
#
# build-dir is a configured single-config (Ninja) build tree, build/dev by default (create it with
# `cmake --preset dev`). The script runs from the repository root, so a relative build-dir is taken
# from there, not from the current directory. It builds gg-render there first and writes the test
# media the cases read into <build-dir>/golden-media (gg-render --write-media; they're generated,
# never committed), as the golden_media CTest does. Then it renders every case in
# tests/golden/cases/ (the same glob the golden CTests use), with --media-dir pointing there, into
#   - tests/golden/expected/<case>.metrics.json and <case>.events.jsonl, which are committed;
#   - <build-dir>/golden/<case>.wav and <case>.png, which are not.
# Then it renders each case again and compares it with the new files, exactly as the golden
# CTest entries do. That fails if a render isn't reproducible, has a clickScore above 30, or has
# non-finite or denormal samples.
#
# Last, it prints each case's WAV SHA-256 and compares them with the previous run's in this build
# tree (<build-dir>/golden/wav-sha256.txt, rewritten each time): it lists the cases whose WAV
# changed, and those new since that run or gone. This is INTENT §19's check for a change meant to
# leave the output alone: run the script before the change and after it, and say in the commit
# that every hash matches. The metrics' tolerances (0.5 dB) and the event log, which holds only the
# grains, would let a small gain error after the grains through; the hashes don't.
#
# Before committing new goldens, look at every spectrogram PNG, and say in the commit message why
# they changed.

set -euo pipefail

cd "$(dirname "$0")/.."

build_dir="${1:-build/dev}"
if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    echo "error: $build_dir is not a configured build tree; run cmake --preset dev first" >&2
    exit 2
fi

cmake --build "$build_dir" --target gg-render

gg_render="$build_dir/tools/gg-render/gg-render"
if [[ -x "$gg_render.exe" ]]; then
    gg_render="$gg_render.exe"
fi

cases_dir=tests/golden/cases
expected_dir=tests/golden/expected
renders_dir="$build_dir/golden"
media_dir="$build_dir/golden-media"
mkdir -p "$expected_dir" "$renders_dir"

"$gg_render" --write-media "$media_dir"

for spec in "$cases_dir"/*.json; do
    name="$(basename "$spec" .json)"
    "$gg_render" "$spec" \
        --media-dir "$media_dir" \
        -o "$renders_dir/$name.wav" \
        --spectrogram "$renders_dir/$name.png" \
        --metrics "$expected_dir/$name.metrics.json" \
        --events "$expected_dir/$name.events.jsonl"
done

echo
echo "Checking the new goldens:"
for spec in "$cases_dir"/*.json; do
    name="$(basename "$spec" .json)"
    "$gg_render" "$spec" \
        --media-dir "$media_dir" \
        --compare "$expected_dir/$name.metrics.json" \
        --compare-events "$expected_dir/$name.events.jsonl"
done

# Each case's WAV SHA-256, "<hash> <case>" per line, sorted by case, against the previous run's.
hashes="$renders_dir/wav-sha256.txt"
current=""
for spec in "$cases_dir"/*.json; do
    name="$(basename "$spec" .json)"
    # cmake -E sha256sum prints "<hash>  <file>"; CMake is here already, unlike sha256sum.
    hash="$(cmake -E sha256sum "$renders_dir/$name.wav" | tr -d '\r')"
    current+="${hash%% *} $name"$'\n'
done
current="$(sort -k 2 <<<"${current%$'\n'}")"

echo
echo "WAV SHA-256 per case:"
sed 's/^/  /' <<<"$current"
if [[ -f "$hashes" ]]; then
    previous="$(tr -d '\r' <"$hashes" | sort -k 2)"
    changed=()
    added=()
    while read -r hash name; do
        old="$(awk -v n="$name" '$2 == n { print $1 }' <<<"$previous")"
        if [[ -z "$old" ]]; then
            added+=("$name")
        elif [[ "$old" != "$hash" ]]; then
            changed+=("$name")
        fi
    done <<<"$current"
    removed=()
    while read -r hash name; do
        if ! awk -v n="$name" '$2 == n { found = 1 } END { exit !found }' <<<"$current"; then
            removed+=("$name")
        fi
    done <<<"$previous"
    total="$(grep -c . <<<"$current")"
    echo "Against the previous run's hashes ($hashes):"
    if ((${#changed[@]} == 0)); then
        echo "  every case's WAV is unchanged ($((total - ${#added[@]})) compared)"
    else
        echo "  ${#changed[@]} of $((total - ${#added[@]})) compared changed: ${changed[*]}"
    fi
    if ((${#added[@]} > 0)); then
        echo "  new since then: ${added[*]}"
    fi
    if ((${#removed[@]} > 0)); then
        echo "  gone since then: ${removed[*]}"
    fi
else
    echo "No previous run's hashes in $renders_dir; the next run compares with these."
fi
printf '%s\n' "$current" >"$hashes"

echo
echo "Updated $expected_dir; renders and spectrograms are in $renders_dir."
