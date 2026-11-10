#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks scripts/retry-failed.py (CI's Rosetta step re-runs each failed test case once) in
# scratch files under <scratch dir>, never this repository's:
#
#   tests/scripts/retry_failed_tests.sh <scratch dir>
#
# A stub `gg-tests` logs its arguments (one CALL line, tab-separated) and exits by a scenario
# file (one exit code per line, the n-th for the n-th call, the last one after that), over
# hand-written Catch2 XML reports. Prints "ok: <name>" or "FAIL: <name>" per check; the exit
# status is the number of failures (capped at 1). Runs in bash 3.2.

set -euo pipefail

if (($# != 1)); then
    echo "usage: $0 <scratch dir>" >&2
    exit 2
fi
here="$(cd "$(dirname "$0")" && pwd)"
src="$(cd "$here/../.." && pwd)"
script="$src/scripts/retry-failed.py"
work="$1"
rm -rf "$work"
mkdir -p "$work/bin"
work="$(cd "$work" && pwd)"

failures=0
out="" status=0
check() { # <name> <condition result: 0 is true>
    if (($2 == 0)); then
        echo "ok: $1"
    else
        echo "FAIL: $1"
        failures=$((failures + 1))
    fi
}
expect_in() { # <name> <text> <expected>
    if grep -qF -- "$3" <<<"$2"; then
        check "$1" 0
    else
        check "$1" 1
        echo "    expected: $3"
        sed 's/^/    | /' <<<"$2"
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

stub="$work/bin/gg-tests"
cat >"$stub" <<'STUB'
#!/usr/bin/env bash
{
    printf 'CALL'
    for a in "$@"; do printf '\t%s' "$a"; done
    printf '\n'
} >>"$STUB_LOG"
n="$(wc -l <"$STUB_LOG" | tr -d ' ')"
code="$(sed -n "${n}p" "$STUB_EXIT")"
[[ -n "$code" ]] || code="$(tail -n 1 "$STUB_EXIT")"
exit "$code"
STUB
chmod +x "$stub"
export STUB_LOG="$work/stub.log" STUB_EXIT="$work/stub.exit"

# Starts a scenario: an empty call log and the stub's exit codes (one per call).
scenario() { # <exit code>...
    : >"$STUB_LOG"
    printf '%s\n' "$@" >"$STUB_EXIT"
}
calls() { wc -l <"$STUB_LOG" | tr -d ' '; }
tab=$'\t'

# Runs the script; sets $out (stdout) and $status.
run() { # <arg>...
    status=0
    out="$(python3 "$script" "$@" 2>"$work/stderr")" || status=$?
}

# Catch2 XML reports, written by hand.
begin_report() { printf '<Catch2TestRun name="gg-tests">\n' >"$1"; }
# <file> <xml-escaped name> pass|fail [<expression body>]
add_case() {
    local def='<Expression success="false" type="CHECK" filename="t.cpp" line="7"><Original>a == b</Original><Expanded>1 == 2</Expanded></Expression>'
    local body="${4-$def}"
    {
        printf '  <TestCase name="%s" tags="[x]">\n' "$2"
        if [[ "$3" == fail ]]; then
            printf '    <Section name="inner">%s</Section>\n' "$body"
            printf '    <OverallResult success="false"/>\n'
        else
            printf '    <OverallResult success="true"/>\n'
        fi
        printf '  </TestCase>\n'
    } >>"$1"
}
end_report() { printf '  <OverallResults successes="1" failures="0"/>\n</Catch2TestRun>\n' >>"$1"; }

# Writes <file> with the cases: name:result ...
simple_report() { # <file> <name:pass|fail>...
    local f="$1" c
    shift
    begin_report "$f"
    for c in "$@"; do add_case "$f" "${c%:*}" "${c##*:}"; done
    end_report "$f"
}

# --- catch2 ----------------------------------------------------------------------------------

check "the script exists" "$([[ -f "$script" ]] && echo 0 || echo 1)"

rep="$work/r.xml"
sum="$work/summary.md"

simple_report "$rep" "alpha:pass" "beta:pass"
scenario 0
run catch2 --report "$rep" --status 0 -- "$stub" --opt
expect_eq "clean: exit 0" "$status" 0
expect_eq "clean: the stub is not run" "$(calls)" 0

simple_report "$rep" "alpha:pass" "beta:fail"
scenario 0
rm -f "$sum"
run catch2 --report "$rep" --status 42 --summary "$sum" -- "$stub" --opt
expect_eq "one failed, passes retry: exit 0" "$status" 0
expect_eq "one failed, passes retry: one call" "$(calls)" 1
expect_in "one failed, passes retry: the name is the last argument" "$(cat "$STUB_LOG")" "CALL${tab}--opt${tab}beta"
expect_in "one failed, passes retry: warning" "$out" "::warning title=Rosetta retry::beta failed once, passed on retry"
expect_in "one failed, passes retry: summary header" "$(cat "$sum")" "| Case | First failure | Retry |"
expect_in "one failed, passes retry: summary row" "$(cat "$sum")" "| beta | t.cpp:7 1 == 2 | passed |"

scenario 1
run catch2 --report "$rep" --status 42 -- "$stub"
expect_eq "one failed, fails retry: exit 1" "$status" 1
expect_in "one failed, fails retry: error" "$out" "::error title=Rosetta retry::beta failed twice"

scenario 2
run catch2 --report "$rep" --status 42 -- "$stub"
expect_eq "a retry exiting 2 is failed: exit 1" "$status" 1
expect_in "a retry exiting 2 is failed: error" "$out" "::error title=Rosetta retry::beta failed twice"

# Each special character in a name is escaped, and the retry runs exactly that case.
names=('a, b' 'x [y]' 's*t' '~u' 'q&quot;r' 'b\s')
wants=('a\, b' 'x \[y\]' 's\*t' '\~u' 'q\"r' 'b\\s')
i=0
while ((i < ${#names[@]})); do
    simple_report "$rep" "${names[$i]}:fail"
    scenario 0
    run catch2 --report "$rep" --status 42 -- "$stub"
    expect_in "special characters: ${wants[$i]}" "$(cat "$STUB_LOG")" "CALL${tab}${wants[$i]}"
    i=$((i + 1))
done
simple_report "$rep" "renders are identical with no pool and pools of 1, 2 and 8 workers:fail"
scenario 0
run catch2 --report "$rep" --status 42 -- "$stub"
expect_in "the real name with commas is escaped" "$(cat "$STUB_LOG")" \
    "CALL${tab}renders are identical with no pool and pools of 1\\, 2 and 8 workers"

# A truncated report is never read as no failures.
simple_report "$rep" "alpha:pass" "beta:fail"
head -c 200 "$rep" >"$work/cut.xml"
scenario 0
run catch2 --report "$work/cut.xml" --status 42 -- "$stub"
expect_eq "truncated report: exit 1" "$status" 1
expect_eq "truncated report: the stub is not run" "$(calls)" 0
expect_in "truncated report: no retry error" "$out" "::error title=Rosetta retry::no retry:"
scenario 0
run catch2 --report "$work/cut.xml" --status 0 -- "$stub"
expect_eq "truncated report at status 0: exit 1" "$status" 1

# A report that parses but lacks OverallResults (cut after a case) is incomplete too.
{
    begin_report "$work/nores.xml"
    add_case "$work/nores.xml" beta fail
    printf '</Catch2TestRun>\n' >>"$work/nores.xml"
}
run catch2 --report "$work/nores.xml" --status 42 -- "$stub"
expect_eq "report without OverallResults: exit 1" "$status" 1
expect_eq "report without OverallResults: the stub is not run" "$(calls)" 0
printf '<Other><OverallResults/></Other>\n' >"$work/root.xml"
run catch2 --report "$work/root.xml" --status 0 -- "$stub"
expect_eq "report with another root: exit 1" "$status" 1

# The status and the report must agree.
simple_report "$rep" "alpha:pass"
run catch2 --report "$rep" --status 42 -- "$stub"
expect_eq "status 42 with no failed case: exit 1" "$status" 1
expect_eq "status 42 with no failed case: the stub is not run" "$(calls)" 0
simple_report "$rep" "alpha:fail"
run catch2 --report "$rep" --status 0 -- "$stub"
expect_eq "status 0 with a failed case: exit 1" "$status" 1
expect_eq "status 0 with a failed case: the stub is not run" "$(calls)" 0

# Any other status, and a missing report.
run catch2 --report "$rep" --status 139 -- "$stub"
expect_eq "status 139: exit 1" "$status" 1
expect_eq "status 139: the stub is not run" "$(calls)" 0
expect_in "status 139: no retry error" "$out" "::error title=Rosetta retry::no retry: "
run catch2 --report "$work/absent.xml" --status 42 -- "$stub"
expect_eq "missing report: exit 1" "$status" 1
expect_eq "missing report: the stub is not run" "$(calls)" 0

# 5 failures are retried; 6 are not.
many() { # <count>
    local k=1 args=()
    while ((k <= $1)); do
        args+=("case$k:fail")
        k=$((k + 1))
    done
    simple_report "$rep" "${args[@]}"
}
many 5
scenario 0
run catch2 --report "$rep" --status 42 -- "$stub"
expect_eq "5 failures: exit 0" "$status" 0
expect_eq "5 failures: five calls" "$(calls)" 5
many 6
scenario 0
run catch2 --report "$rep" --status 42 -- "$stub"
expect_eq "6 failures: exit 1" "$status" 1
expect_eq "6 failures: the stub is not run" "$(calls)" 0
expect_in "6 failures: no retry error" "$out" "::error title=Rosetta retry::no retry: "
many 6
scenario 0
run catch2 --report "$rep" --status 42 --max 6 -- "$stub"
expect_eq "--max 6 retries six" "$(calls)" 6

# Summary cells and annotations are escaped.
begin_report "$rep"
add_case "$rep" "pipes" fail \
    '<Expression success="false" filename="p.cpp" line="3"><Expanded>a | b
c</Expanded></Expression>'
end_report "$rep"
scenario 0
rm -f "$sum"
run catch2 --report "$rep" --status 42 --summary "$sum" -- "$stub"
expect_in "a | in a cell is escaped and a newline is a space" "$(cat "$sum")" "p.cpp:3 a \\| b c |"
begin_report "$rep"
add_case "$rep" "p%q&#10;r" fail
end_report "$rep"
scenario 0
run catch2 --report "$rep" --status 42 -- "$stub"
expect_in "an annotation escapes % and a newline" "$out" "::warning title=Rosetta retry::p%25q%0Ar failed once"

# A case failing without an Expression: its first Failure, Exception or FatalErrorCondition.
begin_report "$rep"
add_case "$rep" "boom" fail '<Failure filename="f.cpp" line="9">kaboom</Failure>'
add_case "$rep" "thrown" fail '<Exception filename="e.cpp" line="4">oops</Exception>'
add_case "$rep" "silent" fail ''
end_report "$rep"
scenario 0
rm -f "$sum"
run catch2 --report "$rep" --status 42 --summary "$sum" -- "$stub"
expect_in "no Expression: a Failure's location and text" "$(cat "$sum")" "| boom | f.cpp:9 kaboom | passed |"
expect_in "no Expression: an Exception's" "$(cat "$sum")" "| thrown | e.cpp:4 oops | passed |"
expect_in "nothing at all: (no message)" "$(cat "$sum")" "| silent | (no message) | passed |"

# --- list ------------------------------------------------------------------------------------

scenario 0
run list --failures "$work/nofile" --label golden -- "$stub"
expect_eq "list: a missing file is none failed: exit 0" "$status" 0
: >"$work/empty"
run list --failures "$work/empty" --label golden -- "$stub"
expect_eq "list: an empty file: exit 0" "$status" 0
expect_eq "list: no calls for none failed" "$(calls)" 0

printf 'one case|x|y\ntwo\n' >"$work/f.txt"
scenario 0
rm -f "$sum"
run list --failures "$work/f.txt" --label golden --summary "$sum" -- "$stub" --render
expect_eq "list: two lines pass: exit 0" "$status" 0
expect_in "list: a line is one argument, spaces and pipes kept" "$(cat "$STUB_LOG")" "CALL${tab}--render${tab}one case|x|y"
expect_in "list: the second line" "$(cat "$STUB_LOG")" "CALL${tab}--render${tab}two"
expect_in "list: the warning" "$out" "::warning title=Rosetta retry::two failed once, passed on retry"
expect_in "list: the summary row has the label" "$(cat "$sum")" "| two | golden | passed |"
expect_in "list: the pipe in a case is escaped" "$(cat "$sum")" "| one case\\|x\\|y | golden | passed |"

scenario 1
run list --failures "$work/f.txt" --label golden -- "$stub"
expect_eq "list: a retry failing: exit 1" "$status" 1
expect_in "list: failed twice" "$out" "::error title=Rosetta retry::two failed twice"

seq 1 6 >"$work/six.txt"
scenario 0
run list --failures "$work/six.txt" --label golden -- "$stub"
expect_eq "list: 6 lines: exit 1" "$status" 1
expect_eq "list: 6 lines: the stub is not run" "$(calls)" 0
seq 1 5 >"$work/five.txt"
run list --failures "$work/five.txt" --label golden -- "$stub"
expect_eq "list: 5 lines retried" "$(calls)" 5

# --- usage -----------------------------------------------------------------------------------

run
expect_eq "no arguments: exit 2" "$status" 2
run catch2 --report "$rep" --status 0
expect_eq "no command separator: exit 2" "$status" 2
run catch2 --report "$rep" --status 0 --
expect_eq "an empty command: exit 2" "$status" 2
run list --failures "$work/f.txt" --label x
expect_eq "list without --: exit 2" "$status" 2

echo "$failures failed"
((failures == 0))
