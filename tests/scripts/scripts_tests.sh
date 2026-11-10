#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks scripts/check.sh and scripts/worktree-setup.sh (carry-over [130], [131], [132]), and
# scripts/update-goldens.sh's WAV hashes (INTENT §19), in scratch repositories, never this one:
#
#   tests/scripts/scripts_tests.sh <scratch dir> <cmake>
#
# check.sh runs in a fake source tree with its own CMakePresets.json, a stub format.sh, and stub
# cmake and ctest on PATH: the stub cmake passes -P scripts to <cmake>, writes a CMakeCache.txt
# for a configure and only logs a build; the stub ctest prints a recorded ctest output. So the
# tests cover
#   - the install guard: which GG_COPY_PLUGIN_AFTER_BUILD values are refused, as CMake's own
#     if () counts them (NOTFOUND matched case-sensitively);
#   - --no-format deleting a stale build/check/format.log;
#   - the build tree read from the presets (binaryDir, inherited or not), and a name that isn't a
#     configure preset getting a summary line, with the next preset still run;
#   - the summary of real ctest output, CTest 3.31.6's and 4.4.5's (ctest-output/), recorded from
#     a project of four tests (one passes, "fails one" and fails_two fail, skipped is skipped;
#     passes and "fails one" are labeled core and rt) with a test preset like ours: all of it
#     (fail), -L core -E fails (pass) and -L nosuchlabel (none).
# update-goldens.sh runs in the same fake tree with a stub gg-render: its per-case WAV SHA-256s,
# and the cases changed, new or gone since the previous run; and the preset goldens' folder
# (tests/golden/presets/cases/), rendered with --content-dir from the packed presets copied into
# the media folder, their hashes listed as presets/<case> beside the others.
# check-at-rest.sh runs against a scratch git repository with a stub update-goldens.sh and a stub
# uname: nothing changed, a changed and a gone case, the expected files it restores and the newer
# case's it leaves, a failed render, the baseline chosen by platform, and the refusals (usage, no
# baseline for the platform, no build tree, an uncommitted edit of a baseline case).
# fetch-content.cmake fetches a scratch zip with the content release's layout by a file:// URL:
# it extracts it and writes .gg-pin, a second run is "already at" the pin, a pin with the wrong
# hash fails and leaves no zip, a stale .gg-pin is fetched over, and -DCHECK passes a matching
# folder and fails any other.
# worktree-setup.sh runs in real scratch git repositories: a linked worktree whose submodule pin
# the main checkout's copy has (the local path), and one whose pin it lacks and whose upstream
# doesn't advertise as a ref (the fetch path); and a name that isn't a preset, refused before
# anything is cloned.
#
# Prints "ok: <name>" or "FAIL: <name>" per check; the exit status is the number of failures
# (capped at 1).

set -euo pipefail

if (($# != 2)); then
    echo "usage: $0 <scratch dir> <cmake>" >&2
    exit 2
fi
here="$(cd "$(dirname "$0")" && pwd)"
src="$(cd "$here/../.." && pwd)"
work="$1"
real_cmake="$2"
rm -rf "$work"
mkdir -p "$work/bin"
work="$(cd "$work" && pwd)"

failures=0
check() { # <name> <condition result: 0 is true>
    if (($2 == 0)); then
        echo "ok: $1"
    else
        echo "FAIL: $1"
        failures=$((failures + 1))
    fi
}
# Fails <name> unless <text> contains the fixed string <expected>; shows the text if not.
expect_in() { # <name> <text> <expected>
    if grep -qF -- "$3" <<<"$2"; then
        check "$1" 0
    else
        check "$1" 1
        echo "    expected: $3"
        sed 's/^/    | /' <<<"$2"
    fi
}
expect_not_in() { # <name> <text> <unexpected>
    if grep -qF -- "$3" <<<"$2"; then
        check "$1" 1
        echo "    unexpected: $3"
        sed 's/^/    | /' <<<"$2"
    else
        check "$1" 0
    fi
}

# --- check.sh, in a fake source tree --------------------------------------------------------

cat >"$work/bin/cmake" <<'EOF'
#!/usr/bin/env bash
# Stub cmake: -P scripts and -E commands run in the real one; a configure writes a cache; a build
# only logs.
set -euo pipefail
case "$1" in
    -D | -P | -E) exec "$REAL_CMAKE" "$@" ;;
    --preset)
        dir="$("$REAL_CMAKE" -D "PRESET=$2" -P scripts/preset-dir.cmake)"
        dir="${dir#dir }"
        mkdir -p "$dir"
        printf 'GG_COPY_PLUGIN_AFTER_BUILD:BOOL=%s\n' "${FAKE_COPY:-OFF}" >"$dir/CMakeCache.txt"
        echo "configure $*" >>"$FAKE_LOG"
        ;;
    --build) echo "build $*" >>"$FAKE_LOG" ;;
    *)
        echo "stub cmake: unexpected arguments $*" >&2
        exit 99
        ;;
esac
EOF
cat >"$work/bin/ctest" <<'EOF'
#!/usr/bin/env bash
# Stub ctest: prints the recorded output $FAKE_CTEST_OUTPUT and exits $FAKE_CTEST_STATUS.
echo "ctest $*" >>"$FAKE_LOG"
cat "$FAKE_CTEST_OUTPUT"
exit "${FAKE_CTEST_STATUS:-0}"
EOF
chmod +x "$work/bin/cmake" "$work/bin/ctest"

repo="$work/repo"
mkdir -p "$repo/scripts"
cp "$src/scripts/check.sh" "$src/scripts/preset-dir.cmake" "$src/scripts/worktree-setup.sh" \
    "$repo/scripts/"
printf '#!/usr/bin/env bash\necho "format stub: 3 files checked"\n' >"$repo/scripts/format.sh"
chmod +x "$repo/scripts/format.sh"
# "one" inherits its binaryDir; "elsewhere" sets its own, outside build/.
cat >"$repo/CMakePresets.json" <<'EOF'
{
  "version": 3,
  "configurePresets": [
    {"name": "base", "hidden": true, "binaryDir": "${sourceDir}/build/${presetName}"},
    {"name": "one", "inherits": "base"},
    {"name": "elsewhere", "inherits": ["base"], "binaryDir": "${sourceDir}/out/tree"}
  ]
}
EOF

ctest_output="$here/ctest-output"
log="$work/stub.log"
status=0
# Runs check.sh in the fake tree; sets $out and $status.
run_check() {
    : >"$log"
    status=0
    out="$(cd "$repo" && PATH="$work/bin:$PATH" REAL_CMAKE="$real_cmake" FAKE_LOG="$log" \
        FAKE_CTEST_OUTPUT="${FAKE_CTEST_OUTPUT:-$ctest_output/ctest-4.4.5-pass.txt}" \
        FAKE_CTEST_STATUS="${FAKE_CTEST_STATUS:-0}" scripts/check.sh "$@" 2>&1)" || status=$?
}
set_copy() { # <value>: the preset "one"'s cache sets GG_COPY_PLUGIN_AFTER_BUILD to it
    mkdir -p "$repo/build/one"
    printf 'GG_COPY_PLUGIN_AFTER_BUILD:BOOL=%s\n' "$1" >"$repo/build/one/CMakeCache.txt"
}

# [131] The install guard. CMake counts these true (checked with if () in CMake 4.4):
for value in notfound x-notfound NotFound X-NotFound ON 1 yes abc 0.0; do
    set_copy "$value"
    run_check --presets one --no-format
    expect_in "guard refuses '$value'" "$out" "one: configure cached, build REFUSED"
    check "guard refuses '$value': exit 1" "$((status == 1 ? 0 : 1))"
    check "guard refuses '$value': nothing built" "$(grep -q '^build' "$log" && echo 1 || echo 0)"
done
# ...and these false: the empty string, 0, OFF, NO, FALSE, N and IGNORE in any case, NOTFOUND
# and *-NOTFOUND as written.
for value in "" 0 OFF off No n N FALSE Ignore NOTFOUND X-NOTFOUND -NOTFOUND; do
    set_copy "$value"
    run_check --presets one --no-format
    expect_in "guard accepts '$value'" "$out" "one: configure cached, build ok, tests 1/1 passed"
    check "guard accepts '$value': exit 0" "$status"
done
set_copy notfound
run_check --presets one --no-format --allow-install
expect_in "--allow-install builds a tree that installs" "$out" "build ok"
set_copy OFF

# [130] --no-format deletes a stale format log; a format check writes a fresh one.
mkdir -p "$repo/build/check"
echo "stale" >"$repo/build/check/format.log"
run_check --presets one --no-format
check "--no-format deletes build/check/format.log" "$([[ -e "$repo/build/check/format.log" ]] &&
    echo 1 || echo 0)"
expect_not_in "--no-format prints no format line" "$out" "format:"
run_check --presets one
expect_in "the format check runs without --no-format" "$out" "format: ok (format stub: 3 files"
check "the format check writes its log" "$(grep -qF 'format stub' \
    "$repo/build/check/format.log" && echo 0 || echo 1)"

# [132] The build tree is the preset's binaryDir: configured, built and logged there.
run_check --presets elsewhere --no-format
expect_in "a binaryDir outside build/ configures and builds" "$out" \
    "elsewhere: configure ok, build ok, tests 1/1 passed"
check "its cache is in its binaryDir" "$([[ -f "$repo/out/tree/CMakeCache.txt" ]] && echo 0 ||
    echo 1)"
check "its logs are in its binaryDir" "$([[ -f "$repo/out/tree/check/test.log" ]] && echo 0 ||
    echo 1)"
check "nothing is made in build/elsewhere" "$([[ -e "$repo/build/elsewhere" ]] && echo 1 ||
    echo 0)"
run_check --presets elsewhere --no-format
expect_in "the second run finds its cache" "$out" "elsewhere: configure cached, build ok"

# [132] A name that isn't a configure preset gets a summary line; the next preset still runs.
run_check --presets nope,base,one --no-format
expect_in "an unknown preset gets a summary line" "$out" \
    "nope: FAILED, no configure preset named nope in CMakePresets.json or CMakeUserPresets.json"
expect_in "a hidden preset gets a summary line" "$out" \
    "base: FAILED, base is a hidden preset, which can't be configured"
expect_in "the next preset still runs" "$out" "one: configure cached, build ok, tests 1/1 passed"
expect_in "the run fails" "$(tail -n 1 <<<"$out")" "FAIL"
check "an unknown preset: exit 1" "$((status == 1 ? 0 : 1))"

# [130] Real ctest output, CTest 3.x's and 4's: passed, failed (with names and a skip) and none.
for version in 3.31.6 4.4.5; do
    FAKE_CTEST_OUTPUT="$ctest_output/ctest-$version-pass.txt" FAKE_CTEST_STATUS=0 \
        run_check --presets one --no-format
    expect_in "ctest $version: a pass" "$out" \
        "one: configure cached, build ok, tests 1/1 passed, 0 skipped,"
    check "ctest $version: a pass: exit 0" "$status"

    FAKE_CTEST_OUTPUT="$ctest_output/ctest-$version-fail.txt" FAKE_CTEST_STATUS=8 \
        run_check --presets one --no-format
    expect_in "ctest $version: failures counted" "$out" \
        "tests 1/4 passed, 1 skipped, 2 FAILED,"
    expect_in "ctest $version: a failing name with a space and labels" "$out" "    FAILED fails one"
    expect_in "ctest $version: a failing name" "$out" "    FAILED fails_two"
    check "ctest $version: failures: exit 1" "$((status == 1 ? 0 : 1))"

    FAKE_CTEST_OUTPUT="$ctest_output/ctest-$version-none.txt" FAKE_CTEST_STATUS=8 \
        run_check --presets one --no-format
    expect_in "ctest $version: no tests fails" "$out" "build ok, tests FAILED,"
    expect_in "ctest $version: no tests says so" "$out" "    No tests were found!!!"
    check "ctest $version: no tests: exit 1" "$((status == 1 ? 0 : 1))"
done

# --- update-goldens.sh's WAV hashes, in the same fake tree ------------------------------------

# A stub gg-render in build/dev: --write-media makes the folder, a render writes "audio of <case>"
# as its WAV (" changed" added for a case named in $FAKE_CHANGED; " content <dir>" for one given
# --content-dir) and empty metrics and events, and a comparison passes.
mkdir -p "$repo/build/dev/tools/gg-render" "$repo/tests/golden/cases"
: >"$repo/build/dev/CMakeCache.txt"
cat >"$repo/build/dev/tools/gg-render/gg-render" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
spec="" wav="" metrics="" events="" content=""
while (($# > 0)); do
    case "$1" in
        --write-media)
            mkdir -p "$2"
            exit 0
            ;;
        -o) wav="$2" && shift ;;
        --metrics) metrics="$2" && shift ;;
        --events) events="$2" && shift ;;
        --content-dir) content="$2" && shift ;;
        --spectrogram | --media-dir | --compare | --compare-events) shift ;;
        *) spec="$1" ;;
    esac
    shift
done
name="$(basename "$spec" .json)"
if [[ -n "$wav" ]]; then
    suffix=""
    if [[ -n "$content" ]]; then suffix=" content $content"; fi
    case " ${FAKE_CHANGED:-} " in
        *" $name "*) echo "audio of $name changed$suffix" >"$wav" ;;
        *) echo "audio of $name$suffix" >"$wav" ;;
    esac
fi
if [[ -n "$metrics" ]]; then : >"$metrics"; fi
if [[ -n "$events" ]]; then : >"$events"; fi
EOF
chmod +x "$repo/build/dev/tools/gg-render/gg-render"
cp "$src/scripts/update-goldens.sh" "$repo/scripts/"
echo '{}' >"$repo/tests/golden/cases/a.json"
echo '{}' >"$repo/tests/golden/cases/b.json"
run_update() { # sets $out and $status
    status=0
    out="$(cd "$repo" && PATH="$work/bin:$PATH" REAL_CMAKE="$real_cmake" FAKE_LOG="$log" \
        scripts/update-goldens.sh 2>&1)" || status=$?
}
# The main cases are rendered with --content-dir <media folder>, as ctest does (row 349).
echo "audio of a content build/dev/golden-media" >"$work/expected-a.wav"
hash_a="$("$real_cmake" -E sha256sum "$work/expected-a.wav")"
hash_a="${hash_a%% *}"

run_update
check "update-goldens, first run: exit 0" "$status"
expect_in "update-goldens prints each case's WAV SHA-256" "$out" "  $hash_a a"
expect_in "update-goldens, first run: nothing to compare" "$out" \
    "No previous run's hashes in build/dev/golden; the next run compares with these."
run_update
expect_in "update-goldens, unchanged WAVs" "$out" "  every case's WAV is unchanged (2 compared)"
FAKE_CHANGED=b run_update
expect_in "update-goldens names a changed WAV" "$out" "  1 of 2 compared changed: b"
expect_not_in "update-goldens names only the changed WAV" "$out" "changed: a"
rm "$repo/tests/golden/cases/a.json"
echo '{}' >"$repo/tests/golden/cases/c.json"
FAKE_CHANGED=b run_update
expect_in "update-goldens: the rest unchanged" "$out" "  every case's WAV is unchanged (1 compared)"
expect_in "update-goldens names a new case" "$out" "  new since then: c"
expect_in "update-goldens names a gone case" "$out" "  gone since then: a"
# The preset goldens: a case in tests/golden/presets/cases/, a packed preset in
# build/dev/content/Presets/ (as gg-presets writes them).
mkdir -p "$repo/tests/golden/presets/cases" "$repo/build/dev/content/Presets/Giga Grains FX"
echo '{}' >"$repo/tests/golden/presets/cases/fx-01-p.json"
echo 'packed' >"$repo/build/dev/content/Presets/Giga Grains FX/01 P.ggpreset"
echo "audio of fx-01-p content build/dev/content-cache/content" >"$work/expected-p.wav"
hash_p="$("$real_cmake" -E sha256sum "$work/expected-p.wav")"
hash_p="${hash_p%% *}"
run_update
check "update-goldens, preset goldens: exit 0" "$status"
expect_in "update-goldens renders a preset golden with --content-dir" "$out" \
    "  $hash_p presets/fx-01-p"
expect_in "update-goldens names a new preset golden" "$out" "  new since then: presets/fx-01-p"
check "update-goldens copies the packed presets into the media folder" \
    "$([[ -f "$repo/build/dev/golden-media/presets/Giga Grains FX/01 P.ggpreset" ]] && echo 0 ||
        echo 1)"
check "update-goldens writes a preset golden's expected files beside its cases" \
    "$([[ -f "$repo/tests/golden/presets/expected/fx-01-p.metrics.json" ]] && echo 0 || echo 1)"

# --- check-at-rest.sh, against a scratch git repository --------------------------------------

# The stub update-goldens.sh rewrites every expected file, as the real one does on a machine whose
# metrics differ in the last digits, and writes the hashes $FAKE_HASHES names.
at="$work/at-rest"
mkdir -p "$at/scripts" "$at/tests/golden/at-rest" "$at/tests/golden/expected" \
    "$at/tests/golden/presets/expected" "$at/build/dev/golden" "$work/at-bin"
cat >"$at/scripts/update-goldens.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
echo "$1" >build/dev/update-goldens-arg
for f in tests/golden/expected/*.json* tests/golden/presets/expected/*.json*; do
    echo rewritten >"$f"
done
if [[ -n "${FAKE_GOLDENS_FAIL:-}" ]]; then
    echo "a render broke"
    exit 3
fi
cp "$FAKE_HASHES" build/dev/golden/wav-sha256.txt
EOF
chmod +x "$at/scripts/update-goldens.sh"
cat >"$work/at-bin/uname" <<'EOF'
#!/usr/bin/env bash
case "$1" in
    -s) echo "${FAKE_UNAME_S:-Linux}" ;;
    -m) echo "${FAKE_UNAME_M:-x86_64}" ;;
esac
EOF
chmod +x "$work/at-bin/uname"
for f in tests/golden/expected/a tests/golden/expected/b tests/golden/presets/expected/p; do
    echo committed >"$at/$f.metrics.json"
    echo committed >"$at/$f.events.jsonl"
done
echo committed >"$at/tests/golden/expected/new-voice.metrics.json"
printf '%s\n' "aaa a" "bbb b" "ppp presets/p" >"$at/tests/golden/at-rest/linux-x86_64-dev.sha256"
echo 'build/' >"$at/.gitignore"
git -C "$at" init -q
git -C "$at" add .
git -C "$at" -c user.name=t -c user.email=t@example.com commit -q -m base
run_at_rest() { # <args...>; sets $out and $status
    status=0
    out="$(PATH="$work/at-bin:$PATH" "$src/scripts/check-at-rest.sh" "$@" 2>&1)" || status=$?
}
at_hashes() { # <lines...>: the hashes the next run's stub reports
    printf '%s\n' "$@" >"$work/at-hashes"
    export FAKE_HASHES="$work/at-hashes"
}
committed() { # <file>: 0 when <file> still holds its committed text
    [[ "$(cat "$at/$1")" == committed ]] && echo 0 || echo 1
}

at_hashes "aaa a" "bbb b" "ppp presets/p" "vvv new-voice"
run_at_rest "$at"
check "check-at-rest, nothing changed: exit 0" "$status"
check "check-at-rest ends with the count line" \
    "$([[ "$(tail -1 <<<"$out")" == "baseline cases: 3, changed: 0, gone: 0" ]] && echo 0 || echo 1)"
check "check-at-rest renders into build/dev" \
    "$([[ "$(cat "$at/build/dev/update-goldens-arg")" == build/dev ]] && echo 0 || echo 1)"
check "check-at-rest restores a baseline case's metrics" \
    "$(committed tests/golden/expected/a.metrics.json)"
check "check-at-rest restores a baseline case's events" \
    "$(committed tests/golden/expected/b.events.jsonl)"
check "check-at-rest restores a preset case's expected files" \
    "$(committed tests/golden/presets/expected/p.metrics.json)"
check "check-at-rest leaves a newer case's render" \
    "$([[ "$(cat "$at/tests/golden/expected/new-voice.metrics.json")" == rewritten ]] && echo 0 ||
        echo 1)"
git -C "$at" checkout -q -- tests/golden

at_hashes "aaa a" "XXX b" "ppp presets/p"
run_at_rest "$at"
check "check-at-rest, a changed WAV: exit 1" "$(((status == 1) ? 0 : 1))"
expect_in "check-at-rest names the changed case" "$out" "CHANGED b"
expect_not_in "check-at-rest names only the changed case" "$out" "CHANGED a"
check "check-at-rest counts the change" \
    "$([[ "$(tail -1 <<<"$out")" == "baseline cases: 3, changed: 1, gone: 0" ]] && echo 0 || echo 1)"

at_hashes "aaa a" "bbb b"
run_at_rest "$at"
check "check-at-rest, a gone case: exit 1" "$(((status == 1) ? 0 : 1))"
expect_in "check-at-rest names the gone case" "$out" "GONE presets/p"
check "check-at-rest counts the gone case" \
    "$([[ "$(tail -1 <<<"$out")" == "baseline cases: 3, changed: 0, gone: 1" ]] && echo 0 || echo 1)"

FAKE_GOLDENS_FAIL=1 run_at_rest "$at"
check "check-at-rest, update-goldens fails: exit 1" "$(((status == 1) ? 0 : 1))"
expect_in "check-at-rest shows the failed run's log" "$out" "a render broke"
expect_not_in "check-at-rest compares nothing after a failed run" "$out" "baseline cases:"
check "check-at-rest restores the expected files after a failed run" \
    "$(committed tests/golden/expected/a.metrics.json)"

at_hashes "aaa a" "bbb b" "ppp presets/p"
rm -f "$at/build/dev/update-goldens-arg"
echo "hand edited" >"$at/tests/golden/expected/a.metrics.json"
run_at_rest "$at"
check "check-at-rest refuses an uncommitted edit of a baseline case: exit 2" \
    "$(((status == 2) ? 0 : 1))"
expect_in "check-at-rest names the edited file" "$out" "tests/golden/expected/a.metrics.json"
check "check-at-rest keeps the uncommitted edit" \
    "$([[ "$(cat "$at/tests/golden/expected/a.metrics.json")" == "hand edited" ]] && echo 0 ||
        echo 1)"
check "check-at-rest renders nothing over an uncommitted edit" \
    "$([[ ! -e "$at/build/dev/update-goldens-arg" ]] && echo 0 || echo 1)"
git -C "$at" add tests/golden/expected/a.metrics.json
run_at_rest "$at"
check "check-at-rest refuses a staged edit of a baseline case: exit 2" \
    "$(((status == 2) ? 0 : 1))"
git -C "$at" reset -q
git -C "$at" checkout -q -- tests/golden

echo "hand edited" >"$at/tests/golden/expected/new-voice.metrics.json"
run_at_rest "$at"
check "check-at-rest allows an uncommitted edit of a newer case" "$status"
git -C "$at" checkout -q -- tests/golden

echo >>"$at/tests/golden/at-rest/linux-x86_64-dev.sha256"
run_at_rest "$at"
check "check-at-rest ignores a blank baseline line" "$status"
check "check-at-rest doesn't count a blank baseline line" \
    "$([[ "$(tail -1 <<<"$out")" == "baseline cases: 3, changed: 0, gone: 0" ]] && echo 0 || echo 1)"
git -C "$at" checkout -q -- tests/golden

cd "$work"
run_at_rest at-rest
cd "$src"
check "check-at-rest takes a relative worktree" "$status"
git -C "$at" checkout -q -- tests/golden

run_at_rest
check "check-at-rest, no worktree: exit 2" "$(((status == 2) ? 0 : 1))"
expect_in "check-at-rest, no worktree: usage" "$out" "usage:"

rm -f "$at/build/dev/update-goldens-arg"
FAKE_UNAME_S=Darwin FAKE_UNAME_M=arm64 run_at_rest "$at"
check "check-at-rest, a platform with no baseline: exit 2" "$(((status == 2) ? 0 : 1))"
expect_in "check-at-rest names the missing platform's baseline" "$out" \
    "tests/golden/at-rest/darwin-arm64-dev.sha256"
expect_in "check-at-rest says how to make a baseline" "$out" "build/dev/golden/wav-sha256.txt"
FAKE_UNAME_M=aarch64 run_at_rest "$at"
check "check-at-rest, Linux on another CPU has its own baseline: exit 2" \
    "$(((status == 2) ? 0 : 1))"
expect_in "check-at-rest keys the baseline by CPU too" "$out" "linux-aarch64-dev"
check "check-at-rest renders nothing without a baseline" \
    "$([[ ! -e "$at/build/dev/update-goldens-arg" ]] && echo 0 || echo 1)"

# A second platform's baseline, whose hashes are not Linux's: the run on that platform uses it.
echo "mmm a" >"$at/tests/golden/at-rest/darwin-arm64-dev.sha256"
at_hashes "mmm a" "bbb b" "ppp presets/p"
FAKE_UNAME_S=Darwin FAKE_UNAME_M=arm64 run_at_rest "$at"
check "check-at-rest uses this platform's baseline: exit 0" "$status"
check "check-at-rest counts this platform's baseline" \
    "$([[ "$(tail -1 <<<"$out")" == "baseline cases: 1, changed: 0, gone: 0" ]] && echo 0 || echo 1)"
git -C "$at" checkout -q -- tests/golden
run_at_rest "$at"
check "check-at-rest on Linux ignores another platform's baseline: exit 1" \
    "$(((status == 1) ? 0 : 1))"
expect_in "check-at-rest on Linux compares Linux's hashes" "$out" "CHANGED a"
rm "$at/tests/golden/at-rest/darwin-arm64-dev.sha256"
git -C "$at" checkout -q -- tests/golden

mv "$at/build" "$work/at-build"
run_at_rest "$at"
check "check-at-rest, no build tree: exit 2" "$(((status == 2) ? 0 : 1))"
expect_in "check-at-rest, no build tree: says so" "$out" "build/dev"
mv "$work/at-build" "$at/build"

mv "$at/tests/golden/at-rest/linux-x86_64-dev.sha256" "$work/at-baseline"
run_at_rest "$at"
check "check-at-rest, no baseline: exit 2" "$(((status == 2) ? 0 : 1))"
mv "$work/at-baseline" "$at/tests/golden/at-rest/linux-x86_64-dev.sha256"

# --- fetch-content.cmake, against a local zip -----------------------------------------------

# A scratch zip with the content release's layout, and a pin naming it by a file:// URL.
content_src="$work/content-src"
mkdir -p "$content_src/Samples/Generated"
for f in LICENSE README.md PROVENANCE.md Samples/Generated/a.flac; do
    echo "scratch $f" >"$content_src/$f"
done
(cd "$content_src" &&
    "$real_cmake" -E tar cf giga-grains-content-v1.zip --format=zip LICENSE README.md \
        PROVENANCE.md Samples)
content_hash="$("$real_cmake" -E sha256sum "$content_src/giga-grains-content-v1.zip")"
content_hash="${content_hash%% *}"
write_pin() { # <file> <sha256>
    printf 'tag v1\nurl file://%s/giga-grains-content-v1.zip\nsha256 %s\n' "$content_src" "$2" \
        >"$1"
}
write_pin "$work/pin-good.txt" "$content_hash"
write_pin "$work/pin-bad.txt" "$(printf '0%.0s' {1..64})"
fetch_content() { # <dest> <pin>; sets $out and $status
    status=0
    out="$("$real_cmake" -DDEST="$1" -DPIN="$2" -P "$src/scripts/fetch-content.cmake" 2>&1)" ||
        status=$?
}
dest="$work/content-cache"
fetch_content "$dest" "$work/pin-good.txt"
check "fetch-content: exit 0" "$status"
check "fetch-content extracts the zip" \
    "$([[ "$(cat "$dest/content/Samples/Generated/a.flac" 2>/dev/null)" == \
        "scratch Samples/Generated/a.flac" ]] && echo 0 || echo 1)"
check "fetch-content writes .gg-pin" \
    "$([[ "$(cat "$dest/content/.gg-pin" 2>/dev/null)" == "v1 $content_hash" ]] && echo 0 ||
        echo 1)"
check "fetch-content keeps no zip" \
    "$([[ ! -e "$dest/giga-grains-content-v1.zip" ]] && echo 0 || echo 1)"
fetch_content "$dest" "$work/pin-good.txt"
check "fetch-content, again: exit 0" "$status"
expect_in "fetch-content, again: already there" "$out" "content: v1 already at $dest/content"
dest_bad="$work/content-cache-bad"
fetch_content "$dest_bad" "$work/pin-bad.txt"
check "fetch-content, a wrong hash: fails" "$((status != 0 ? 0 : 1))"
expect_in "fetch-content, a wrong hash: names it" "$out" "$content_hash"
check "fetch-content, a wrong hash: leaves no zip" \
    "$([[ ! -e "$dest_bad/giga-grains-content-v1.zip" ]] && echo 0 || echo 1)"
check "fetch-content, a wrong hash: no content" \
    "$([[ ! -e "$dest_bad/content" ]] && echo 0 || echo 1)"
printf 'tag v1\nsha256 %s\n' "$content_hash" >"$work/pin-no-url.txt"
fetch_content "$work/content-cache-no-url" "$work/pin-no-url.txt"
check "fetch-content, a pin without a url: fails" "$((status != 0 ? 0 : 1))"
# CMake wraps a FATAL_ERROR message at a space, so under a long path the words split across
# lines: join the lines before matching.
expect_in "fetch-content, a pin without a url: says so" "$(tr -s '\n ' ' ' <<<"$out")" "no url line"

# A stale .gg-pin (an older release's) is fetched over, and the stamp is then the pin's.
printf 'v0 %s\n' "$(printf 'f%.0s' {1..64})" >"$dest/content/.gg-pin"
fetch_content "$dest" "$work/pin-good.txt"
check "fetch-content, a stale pin: exit 0" "$status"
expect_in "fetch-content, a stale pin: fetches again" "$out" "content: fetching v1"
check "fetch-content, a stale pin: the stamp is the pin's" \
    "$([[ "$(cat "$dest/content/.gg-pin" 2>/dev/null)" == "v1 $content_hash" ]] && echo 0 ||
        echo 1)"

# Check mode (-DCHECK): a folder whose .gg-pin matches passes, offline; any other fails naming
# both.
check_content() { # <content dir> <pin>; sets $out and $status
    status=0
    out="$("$real_cmake" -DCHECK="$1" -DPIN="$2" -P "$src/scripts/fetch-content.cmake" 2>&1)" ||
        status=$?
}
check_content "$dest/content" "$work/pin-good.txt"
check "fetch-content -DCHECK, a match: exit 0" "$status"
expect_in "fetch-content -DCHECK, a match: says so" "$out" "content: v1 checked at $dest/content"
check_content "$dest/content" "$work/pin-bad.txt"
check "fetch-content -DCHECK, another pin: fails" "$((status != 0 ? 0 : 1))"
expect_in "fetch-content -DCHECK, another pin: names what it holds" "$out" "$content_hash"
mkdir -p "$work/content-unstamped"
check_content "$work/content-unstamped" "$work/pin-good.txt"
check "fetch-content -DCHECK, no stamp: fails" "$((status != 0 ? 0 : 1))"
expect_in "fetch-content -DCHECK, no stamp: says so" "$out" "(no .gg-pin)"

# --- worktree-setup.sh, in scratch git repositories -----------------------------------------

# Only these repositories: no global or system config, and git stops looking at $work.
export GIT_CEILING_DIRECTORIES="$work" GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.invalid
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.invalid
# The upstream is a local path, which git refuses for submodules unless allowed. The script
# allows it for its local copies itself; this reaches its fetch from the upstream too.
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=protocol.file.allow GIT_CONFIG_VALUE_0=always

# [130] Preset names are checked before anything is cloned (here, before git is even asked
# whether this is a worktree: the fake tree isn't a repository at all).
status=0
out="$(cd "$repo" && scripts/worktree-setup.sh --configure one,nope 2>&1)" || status=$?
expect_in "worktree-setup refuses an unknown preset" "$out" \
    "error: --configure nope: no configure preset named nope"
check "worktree-setup: an unknown preset: exit 2" "$((status == 2 ? 0 : 1))"
status=0
out="$(cd "$repo" && scripts/worktree-setup.sh --configure one 2>&1)" || status=$?
expect_not_in "worktree-setup accepts a known preset" "$out" "--configure one"
check "worktree-setup then needs a repository" "$((status != 0 ? 0 : 1))"

git_commit() { # <dir> <file> <message>: a commit changing <file>; prints its id
    echo "$3" >>"$1/$2"
    git -C "$1" add "$2"
    git -C "$1" commit -q -m "$3"
    git -C "$1" rev-parse HEAD
}
upstream="$work/git/upstream"
main="$work/git/main"
git init -q -b main "$upstream"
c1="$(git_commit "$upstream" a.txt c1)"
git init -q -b main "$main"
mkdir -p "$main/scripts"
cp "$src/scripts/worktree-setup.sh" "$src/scripts/preset-dir.cmake" "$main/scripts/"
cp "$repo/CMakePresets.json" "$main/"
git -C "$main" add scripts CMakePresets.json
git -C "$main" submodule add -q "$upstream" sub
git -C "$main" commit -q -m "sub at c1"
# The upstream moves on after the main checkout cloned it: its copy has c1 only. The pin c2 is
# no ref's tip (main is at c3), so the upstream doesn't advertise it.
c2="$(git_commit "$upstream" a.txt c2)"
git_commit "$upstream" a.txt c3 >/dev/null
git -C "$main" branch -q at-c1
git -C "$main" checkout -q -b at-c2
git -C "$main" update-index --cacheinfo "160000,$c2,sub"
git -C "$main" commit -q -m "sub at c2"
git -C "$main" checkout -q main
shared_url_before="$(git -C "$main" config submodule.sub.url)"

# The local path: the main checkout's copy has the pin.
git -C "$main" worktree add -q "$work/git/wt-c1" at-c1
status=0
out="$("$work/git/wt-c1/scripts/worktree-setup.sh" 2>&1)" || status=$?
check "worktree-setup, a pin the main checkout has: exit 0" "$status"
expect_in "worktree-setup, a pin the main checkout has: checked" "$out" \
    "1 submodules checked out at their pinned commits"
expect_not_in "worktree-setup, a pin the main checkout has: nothing fetched" "$out" "note:"
check "worktree-setup, a pin the main checkout has: sub at c1" \
    "$([[ "$(git -C "$work/git/wt-c1/sub" rev-parse HEAD)" == "$c1" ]] && echo 0 || echo 1)"

# The fetch path: the main checkout lacks the pin, which the upstream doesn't advertise.
git -C "$main" worktree add -q "$work/git/wt-c2" at-c2
status=0
out="$("$work/git/wt-c2/scripts/worktree-setup.sh" 2>&1)" || status=$?
check "worktree-setup, an unadvertised pin: exit 0" "$status"
expect_in "worktree-setup, an unadvertised pin: says it fetches" "$out" \
    "note: the main checkout has no copy of sub at ${c2:0:12}; fetching it from $upstream"
expect_in "worktree-setup, an unadvertised pin: checked" "$out" \
    "1 submodules checked out at their pinned commits"
check "worktree-setup, an unadvertised pin: sub at c2" \
    "$([[ "$(git -C "$work/git/wt-c2/sub" rev-parse HEAD)" == "$c2" ]] && echo 0 || echo 1)"
check "worktree-setup leaves the shared config's URL" \
    "$([[ "$(git -C "$main" config submodule.sub.url)" == "$shared_url_before" ]] && echo 0 ||
        echo 1)"
check "worktree-setup leaves the main checkout's copy without c2" \
    "$(git --git-dir="$main/.git/modules/sub" cat-file -e "$c2^{commit}" 2>/dev/null && echo 1 ||
        echo 0)"

echo "$failures failed"
((failures == 0))
