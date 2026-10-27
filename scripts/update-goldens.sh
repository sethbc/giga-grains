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
# from there, not from the current directory. It builds gg-render there first, then renders every
# case in tests/golden/cases/ (the same glob the golden CTests use) into
#   - tests/golden/expected/<case>.metrics.json and <case>.events.jsonl, which are committed;
#   - <build-dir>/golden/<case>.wav and <case>.png, which are not.
# Finally it renders each case again and compares it with the new files, exactly as the golden
# CTest entries do. That fails if a render isn't reproducible, has a clickScore above 30, or has
# non-finite or denormal samples.
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
mkdir -p "$expected_dir" "$renders_dir"

for spec in "$cases_dir"/*.json; do
    name="$(basename "$spec" .json)"
    "$gg_render" "$spec" \
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
        --compare "$expected_dir/$name.metrics.json" \
        --compare-events "$expected_dir/$name.events.jsonl"
done

echo
echo "Updated $expected_dir; renders and spectrograms are in $renders_dir."
