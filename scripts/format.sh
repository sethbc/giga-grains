#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Formats the first-party C/C++/Objective-C++ sources with clang-format 23.
#
#   scripts/format.sh          rewrite the files in place
#   scripts/format.sh --check  change nothing; fail if any file is not formatted
#
# The binary is $CLANG_FORMAT if set, else Homebrew LLVM's, else clang-format-23, else
# clang-format. Its major version must be 23 so local and CI formatting agree.

set -euo pipefail

required_major=23
source_dirs=(engine core plugin ui tools tests clients)

case "${1:-}" in
    "")
        mode=(-i)
        action=formatted
        ;;
    --check)
        mode=(--dry-run --Werror)
        action=checked
        ;;
    *)
        echo "usage: $0 [--check]" >&2
        exit 2
        ;;
esac

cd "$(dirname "$0")/.."

if [[ -n "${CLANG_FORMAT:-}" ]]; then
    clang_format="$CLANG_FORMAT"
elif [[ -x /opt/homebrew/opt/llvm/bin/clang-format ]]; then
    clang_format=/opt/homebrew/opt/llvm/bin/clang-format
elif command -v clang-format-23 >/dev/null 2>&1; then
    clang_format=clang-format-23
elif command -v clang-format >/dev/null 2>&1; then
    clang_format=clang-format
else
    echo "error: clang-format not found; install clang-format $required_major or set CLANG_FORMAT" >&2
    exit 1
fi

version_line="$("$clang_format" --version 2>/dev/null || true)"
major="$(sed -nE 's/.*clang-format version ([0-9]+).*/\1/p' <<<"$version_line")"
if [[ "$major" != "$required_major" ]]; then
    echo "error: $clang_format reports '${version_line:-no version}';" \
        "clang-format $required_major is required (set CLANG_FORMAT to its path)" >&2
    exit 1
fi

dirs=()
for d in "${source_dirs[@]}"; do
    if [[ -d "$d" ]]; then
        dirs+=("$d")
    fi
done

files=()
if ((${#dirs[@]} > 0)); then
    while IFS= read -r -d '' f; do
        files+=("$f")
    done < <(find "${dirs[@]}" -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.c' \
        -o -name '*.cpp' -o -name '*.m' -o -name '*.mm' \) -print0 | sort -z)
fi

if ((${#files[@]} == 0)); then
    echo "No sources to format."
    exit 0
fi

"$clang_format" "${mode[@]}" "${files[@]}"
echo "clang-format $major: ${#files[@]} files $action"
