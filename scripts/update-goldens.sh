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
# from there, not from the current directory. It builds gg-render and gg-presets there first
# (gg-presets packs the factory presets, content/presets/, and needs the fetched content, which
# gg-content fetches into the tree once, so the first run needs the network) and writes the test
# media the cases read into <build-dir>/golden-media (gg-render --write-media; they're generated,
# never committed), as the golden_media CTest does, and the packed presets into its presets/, as
# the presets_built CTest does. Then it renders every case of its two case folders (the same
# globs the golden CTests use), with --media-dir pointing there:
#   - tests/golden/cases/, into tests/golden/expected/<case>.metrics.json and
#     <case>.events.jsonl, which are committed, and <build-dir>/golden/<case>.wav and <case>.png,
#     which are not;
#   - the preset goldens, tests/golden/presets/cases/ (M2 sub-project 5's spec §10), with
#     --content-dir the fetched content (GG_CONTENT_DIR, or <build-dir>/content-cache/content),
#     into tests/golden/presets/expected/ and <build-dir>/golden/presets/.
# Then it renders each case again and compares it with the new files, exactly as the golden
# CTest entries do. That fails if a render isn't reproducible, has a clickScore above the limit
# (15; Tolerances::maxClickScore in tools/audiometrics), or has non-finite or denormal samples.
#
# Last, it prints each case's WAV SHA-256, the preset goldens' as presets/<case>, and compares
# them with the previous run's in this build tree (<build-dir>/golden/wav-sha256.txt, rewritten
# each time): it lists the cases whose WAV changed, and those new since that run or gone. This is
# INTENT §19's check for a change meant to
# leave the output alone: run the script before the change and after it, and say in the commit
# that every hash matches. The metrics' tolerances (0.5 dB) and the event log, which holds only the
# grains, would let a small gain error after the grains through; the hashes don't.
#
# Before committing new goldens, look at every spectrogram PNG, and say in the commit message why
# they changed.

set -euo pipefail
# A case folder with no case, or a product with no packed preset, globs to nothing.
shopt -s nullglob

cd "$(dirname "$0")/.."

build_dir="${1:-build/dev}"
if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    echo "error: $build_dir is not a configured build tree; run cmake --preset dev first" >&2
    exit 2
fi

cmake --build "$build_dir" --target gg-render gg-presets

gg_render="$build_dir/tools/gg-render/gg-render"
if [[ -x "$gg_render.exe" ]]; then
    gg_render="$gg_render.exe"
fi

renders_dir="$build_dir/golden"
media_dir="$build_dir/golden-media"
# The content folder the build reads (cmake/content.cmake's GG_CONTENT_ROOT).
content_dir="$(sed -n 's/^GG_CONTENT_DIR:PATH=//p' "$build_dir/CMakeCache.txt")"
if [[ -z "$content_dir" ]]; then
    content_dir="$build_dir/content-cache/content"
fi

"$gg_render" --write-media "$media_dir"
rm -rf "$media_dir/presets"
for product in "Giga Grains" "Giga Grains FX"; do
    mkdir -p "$media_dir/presets/$product"
    packed=("$build_dir/content/Presets/$product/"*.ggpreset)
    if ((${#packed[@]} > 0)); then
        cp "${packed[@]}" "$media_dir/presets/$product/"
    fi
done

# The case folders: "<cases> <expected> <renders> <hash-name prefix> <content dir>" (the first
# group's cases render without --content-dir, as they always have).
groups=(
    "tests/golden/cases|tests/golden/expected|$renders_dir||"
    "tests/golden/presets/cases|tests/golden/presets/expected|$renders_dir/presets|presets/|$content_dir"
)

render_group() { # cases expected renders prefix content
    local cases_dir="$1" expected_dir="$2" out_dir="$3" content="$5" spec name
    local extra=()
    if [[ -n "$content" ]]; then
        extra=(--content-dir "$content")
    fi
    mkdir -p "$expected_dir" "$out_dir"
    for spec in "$cases_dir"/*.json; do
        name="$(basename "$spec" .json)"
        "$gg_render" "$spec" \
            --media-dir "$media_dir" \
            ${extra[@]+"${extra[@]}"} \
            -o "$out_dir/$name.wav" \
            --spectrogram "$out_dir/$name.png" \
            --metrics "$expected_dir/$name.metrics.json" \
            --events "$expected_dir/$name.events.jsonl"
    done
}

check_group() { # cases expected renders prefix content
    local cases_dir="$1" expected_dir="$2" content="$5" spec name
    local extra=()
    if [[ -n "$content" ]]; then
        extra=(--content-dir "$content")
    fi
    for spec in "$cases_dir"/*.json; do
        name="$(basename "$spec" .json)"
        "$gg_render" "$spec" \
            --media-dir "$media_dir" \
            ${extra[@]+"${extra[@]}"} \
            --compare "$expected_dir/$name.metrics.json" \
            --compare-events "$expected_dir/$name.events.jsonl"
    done
}

for group in "${groups[@]}"; do
    IFS='|' read -r g_cases g_expected g_renders g_prefix g_content <<<"$group"
    render_group "$g_cases" "$g_expected" "$g_renders" "$g_prefix" "$g_content"
done

echo
echo "Checking the new goldens:"
for group in "${groups[@]}"; do
    IFS='|' read -r g_cases g_expected g_renders g_prefix g_content <<<"$group"
    check_group "$g_cases" "$g_expected" "$g_renders" "$g_prefix" "$g_content"
done

# Each case's WAV SHA-256, "<hash> <case>" per line (a preset golden's as presets/<case>), sorted
# by case, against the previous run's.
hashes="$renders_dir/wav-sha256.txt"
current=""
for group in "${groups[@]}"; do
    IFS='|' read -r g_cases g_expected g_renders g_prefix g_content <<<"$group"
    for spec in "$g_cases"/*.json; do
        name="$(basename "$spec" .json)"
        # cmake -E sha256sum prints "<hash>  <file>"; CMake is here already, unlike sha256sum.
        hash="$(cmake -E sha256sum "$g_renders/$name.wav" | tr -d '\r')"
        current+="${hash%% *} $g_prefix$name"$'\n'
    done
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
        [[ -n "$name" ]] || continue # an empty previous-hashes file reads one blank line
        old="$(awk -v n="$name" '$2 == n { print $1 }' <<<"$previous")"
        if [[ -z "$old" ]]; then
            added+=("$name")
        elif [[ "$old" != "$hash" ]]; then
            changed+=("$name")
        fi
    done <<<"$current"
    removed=()
    while read -r hash name; do
        [[ -n "$name" ]] || continue
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
echo "Updated tests/golden/expected and tests/golden/presets/expected; renders and spectrograms" \
    "are in $renders_dir (the preset goldens' in $renders_dir/presets)."
