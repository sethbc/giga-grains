#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# The pre-commit gate in one command: configures, builds and tests each preset, then checks the
# formatting, and prints only a short summary.
#
#   scripts/check.sh [--presets dev,asan,rtsan] [-L label]... [--no-format] [--allow-install]
#
# For each preset, in the order given (dev,asan,rtsan by default):
#   1. configure, only when the build tree has no CMakeCache.txt. rtsan takes its compilers from
#      $GG_LLVM; when that's unset on macOS and /opt/homebrew/opt/llvm exists, it uses that, and
#      the summary says so.
#   2. build, with the target CI builds: gg-all for asan and rtsan (the sanitizer job), the
#      default target (everything) for the others.
#   3. test: ctest --preset <preset>, with every -L passed through.
# A failed step skips the rest of that preset, and the next preset still runs, so one run reports
# everything. Last comes scripts/format.sh --check, unless --no-format.
#
# Each step's full output goes to build/<preset>/check/<step>.log (configure, build, test) and the
# format check's to build/check/format.log. The terminal gets only the summary: a line per preset
# as it finishes, with the failing tests or the first build errors under it, then the format
# result and PASS or FAIL. The exit status is 0 only if every step passed, 1 if any failed, and 2
# for bad usage.
#
# A tree configured with GG_COPY_PLUGIN_AFTER_BUILD=ON installs the plugins into your plugin
# folders whenever a bundle is rebuilt, so check.sh won't build it unless given --allow-install.

set -euo pipefail

usage() {
    echo "usage: $0 [--presets dev,asan,rtsan] [-L label]... [--no-format] [--allow-install]" >&2
    exit 2
}

presets=dev,asan,rtsan
labels=()
format=true
allow_install=false
while (($# > 0)); do
    case "$1" in
        --presets)
            [[ $# -ge 2 ]] || usage
            presets="$2"
            shift 2
            ;;
        -L)
            [[ $# -ge 2 ]] || usage
            labels+=(-L "$2")
            shift 2
            ;;
        --no-format)
            format=false
            shift
            ;;
        --allow-install)
            allow_install=true
            shift
            ;;
        *)
            usage
            ;;
    esac
done

preset_list=()
IFS=, read -r -a preset_list <<<"$presets"
((${#preset_list[@]} > 0)) || usage
for preset in "${preset_list[@]}"; do
    [[ "$preset" =~ ^[A-Za-z0-9_.-]+$ ]] || usage
done

cd "$(dirname "$0")/.."
root="$(pwd)"

# The preset's build tree (its binaryDir in CMakePresets.json).
binary_dir() {
    case "$1" in
        windows-clangcl) echo build/windows ;;
        *) echo "build/$1" ;;
    esac
}

# Up to 10 error lines from a configure or build log, indented and cut to 200 columns: CMake
# errors with the message lines under them, compiler and linker errors, and Ninja's FAILED lines.
# The log's last 10 lines if nothing matches.
first_errors() {
    local errors
    errors="$(tr -d '\r' <"$1" | awk '
        n >= 10 { exit }
        cmake && /^[ \t]*$/ { cmake = 0; next }
        cmake { print; n++; next }
        /CMake Error/ { print; n++; cmake = 1; next }
        /error( [A-Z]+[0-9]+)?:|^FAILED:|Undefined symbols|undefined reference/ { print; n++ }')"
    if [[ -z "$errors" ]]; then
        errors="$(tr -d '\r' <"$1" | tail -n 10)"
    fi
    printf '%s\n' "$errors" | cut -c 1-200 | sed 's/^/    /'
}

# ctest's result line: "100% tests passed out of 138" (CTest 4), or
# "97% tests passed, 4 tests failed out of 138".
result_line() {
    tr -d '\r' <"$1" | grep -E '^[0-9]+% tests passed' | tail -n 1 || true
}

# The names of the tests ctest lists as failed: "  12 - name (Failed)", maybe followed by labels.
failed_tests() {
    tr -d '\r' <"$1" | awk '
        /^The following tests FAILED:/ { listing = 1; next }
        listing && /^[ \t]*[0-9]+ - / { print; next }
        listing { listing = 0 }' |
        sed -E 's/^[[:space:]]*[0-9]+ - (.*) \([^()]*\)([[:space:]].*)?$/\1/'
}

failed=0
for preset in "${preset_list[@]}"; do
    start=$SECONDS
    dir="$(binary_dir "$preset")"
    logs="$root/$dir/check"
    mkdir -p "$logs"
    rm -f "$logs/configure.log" "$logs/build.log" "$logs/test.log"
    ok=true
    configure=cached
    llvm_note=""
    build="not run"
    tests="not run"
    details=""

    if [[ ! -f "$dir/CMakeCache.txt" ]]; then
        configure_cmd=(cmake --preset "$preset")
        if [[ "$preset" == rtsan && -z "${GG_LLVM:-}" && "$(uname -s)" == Darwin &&
            -d /opt/homebrew/opt/llvm ]]; then
            configure_cmd=(env GG_LLVM=/opt/homebrew/opt/llvm "${configure_cmd[@]}")
            llvm_note=" (GG_LLVM=/opt/homebrew/opt/llvm)"
        fi
        if "${configure_cmd[@]}" >"$logs/configure.log" 2>&1; then
            configure=ok
        else
            ok=false
            configure=FAILED
            details="$(first_errors "$logs/configure.log")"$'\n'"    log: $logs/configure.log"
            # A failed configure still writes a cache. The tree had none, so this run wrote it;
            # without it, the next run configures again instead of building a broken tree.
            rm -f "$dir/CMakeCache.txt"
        fi
    fi

    if [[ "$ok" == true ]]; then
        copy="$(sed -n 's/^GG_COPY_PLUGIN_AFTER_BUILD:[A-Z]*=//p' "$dir/CMakeCache.txt" |
            tr -d '\r' | tr '[:lower:]' '[:upper:]')"
        case "$copy" in
            ON | YES | TRUE | Y | [1-9]*)
                if [[ "$allow_install" != true ]]; then
                    ok=false
                    build=REFUSED
                    details="    GG_COPY_PLUGIN_AFTER_BUILD is ON in $dir/CMakeCache.txt, so a"
                    details+=" build installs the plugins; pass --allow-install to build it anyway"
                fi
                ;;
        esac
    fi

    if [[ "$ok" == true ]]; then
        build_cmd=(cmake --build --preset "$preset" --parallel)
        case "$preset" in
            asan | rtsan) build_cmd+=(--target gg-all) ;;
        esac
        if "${build_cmd[@]}" >"$logs/build.log" 2>&1; then
            build=ok
        else
            ok=false
            build=FAILED
            details="$(first_errors "$logs/build.log")"$'\n'"    log: $logs/build.log"
        fi
    fi

    if [[ "$ok" == true ]]; then
        status=0
        ctest --preset "$preset" ${labels[@]+"${labels[@]}"} >"$logs/test.log" 2>&1 || status=$?
        result="$(result_line "$logs/test.log")"
        failures=0
        if [[ -n "$result" ]]; then
            total="$(sed -E 's/.* out of ([0-9]+).*/\1/' <<<"$result")"
            if [[ "$result" =~ ([0-9]+)\ tests?\ failed ]]; then
                failures="${BASH_REMATCH[1]}"
            fi
            skipped="$(tr -d '\r' <"$logs/test.log" |
                grep -cE '^[[:space:]]*[0-9]+ - .* \(Skipped\)' || true)"
            tests="$((total - failures - skipped))/$total passed, $skipped skipped"
        fi
        if ((failures > 0)); then
            tests+=", $failures FAILED"
            names=()
            while IFS= read -r name; do
                names+=("$name")
            done < <(failed_tests "$logs/test.log")
            for name in ${names[@]+"${names[@]}"}; do
                details+="    FAILED $name  $logs/test.log"$'\n'
            done
            # At most 10 names, so a mass failure can't flood the summary.
            if ((${#names[@]} > 10)); then
                details="$(head -n 10 <<<"$details")"$'\n'"    ... and $((${#names[@]} - 10)) more"
            fi
            details="${details%$'\n'}"
        fi
        if ((status != 0)); then
            ok=false
            if [[ -z "$result" ]]; then
                tests=FAILED
            elif ((failures == 0)); then
                tests+=", but ctest exited with status $status"
            fi
            if [[ -z "$details" ]]; then
                # No test failed, so ctest itself did: no test matched the labels, say.
                details="$(tr -d '\r' <"$logs/test.log" | grep -m 3 -iE 'error|no tests' |
                    cut -c 1-200 | sed 's/^/    /' || true)"
                details+=$'\n'"    log: $logs/test.log"
            fi
        fi
    fi

    echo "$preset: configure $configure$llvm_note, build $build, tests $tests," \
        "$((SECONDS - start))s"
    if [[ -n "$details" ]]; then
        printf '%s\n' "$details"
    fi
    if [[ "$ok" != true ]]; then
        failed=1
    fi
done

if [[ "$format" == true ]]; then
    format_log="$root/build/check/format.log"
    mkdir -p "$root/build/check"
    if scripts/format.sh --check >"$format_log" 2>&1; then
        echo "format: ok ($(tail -n 1 "$format_log" | tr -d '\r'))"
    else
        failed=1
        files="$(tr -d '\r' <"$format_log" |
            sed -n -E 's/^(.*):[0-9]+:[0-9]+: (error|warning): code should be clang-formatted.*/\1/p' |
            sort -u)"
        if [[ -n "$files" ]]; then
            count="$(wc -l <<<"$files" | tr -d ' ')"
            listed="$(head -n 3 <<<"$files" | paste -s -d ' ' -)"
            if ((count > 3)); then
                listed+=" and $((count - 3)) more"
            fi
            echo "format: FAILED in $listed (scripts/format.sh fixes it)"
            echo "    log: $format_log"
        else
            echo "format: FAILED: $(head -n 1 "$format_log" | tr -d '\r' | cut -c 1-160)"
            echo "    log: $format_log"
        fi
    fi
fi

if ((failed == 0)); then
    echo PASS
else
    echo FAIL
fi
exit "$failed"
