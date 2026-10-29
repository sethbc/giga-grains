#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks scripts/check.sh and scripts/worktree-setup.sh (carry-over [130], [131], [132]) in
# scratch repositories, never this one:
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
#     passes and "fails one" are labelled core and rt) with a test preset like ours: all of it
#     (fail), -L core -E fails (pass) and -L nosuchlabel (none).
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
# Stub cmake: -P scripts run in the real one; a configure writes a cache; a build only logs.
set -euo pipefail
case "$1" in
    -D | -P) exec "$REAL_CMAKE" "$@" ;;
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
