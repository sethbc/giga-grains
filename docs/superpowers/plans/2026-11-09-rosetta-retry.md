# The Rosetta step retries failed cases once: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Every implementer invokes superpowers:test-driven-development and reports RED and GREEN.

**Goal:** CI's "Test the x86_64 slice under Rosetta" step re-runs each failed case once, lists what it retried, and fails only on a repeat, a crash or more than five failures.

**Architecture:** One tested script, `scripts/retry-failed.py` (stdlib only), decides and retries. `ci.yml`'s step gives it what each run produced: a Catch2 XML report and exit status, or a list of failed golden cases.

**Tech Stack:** Python 3 (stdlib), bash, Catch2 v3.16.1's XML reporter, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-09-rosetta-retry-design.md`

## Global Constraints

- Only the Rosetta step changes. Native arm64 `ctest`, Linux, Windows and the sanitizers never retry; `ci-ok`, the required checks and the shard layout are unchanged.
- A case is retried at most once, on its own. A case failing its retry fails the step.
- At most 5 failures are retried per run of a shard, of the serial run, or of the goldens. More than 5: no retries, the step fails.
- Only a clean "these cases failed" qualifies: Catch2 exit status 42 with a complete report, or `gg-render` exit status 1. Anything else (a signal, another status, a missing or unparsable report) fails with no retry.
- Test case names and tags stay ASCII. Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Stage files by name.

## Review Focus

1. A case name holding `,` `[` `]` `*` `~` `"` or `\`: the retry must run exactly that case (a real one: "renders are identical with no pool and pools of 1, 2 and 8 workers").
2. A report cut off mid-file by a crash: no retry, fail. Never read as "no failures".
3. Exit status and report disagreeing (42 with no failed case, 0 with one): fail, no retry.
4. A retry that matches no test (Catch2's "No tests ran", non-zero): counts as failed, never passed.
5. Exactly 5 failures are retried and 6 are not.

---

### Task 1: `scripts/retry-failed.py` and its tests

**Files:**
- Create: `scripts/retry-failed.py`
- Create: `tests/scripts/retry_failed_tests.sh`
- Modify: `tests/CMakeLists.txt` (a new `scripts_retry_failed`, label `scripts`, beside `scripts_bootstrap`, scratch dir `${CMAKE_CURRENT_BINARY_DIR}/retry-failed-tests`)

**Interfaces:**
- Produces:
  - `retry-failed.py catch2 --report XML --status N [--max 5] [--summary FILE] -- CMD...`: when N is 0 and the report has no failed case, exit 0 and run nothing. When N is 42 and the report is complete, re-run `CMD... <escaped name>` for each failed case.
  - `retry-failed.py list --failures FILE --label TEXT [--max 5] [--summary FILE] -- CMD...`: one failure per line; re-run `CMD... <line>`, the line one final argument. A missing or empty file means none failed, exit 0.
  - Exit status: 0 when nothing failed or every retry passed; 1 otherwise; 2 for bad usage.
  - On stdout, `::warning title=Rosetta retry::<case> failed once, passed on retry`, `::error title=Rosetta retry::<case> failed twice`, `::error title=Rosetta retry::no retry: <reason>`.
  - With `--summary`, appended Markdown: a table of `Case | First failure | Retry` (for `catch2`, First failure is the first `<Expression success="false">`'s `filename:line` and `<Expanded>`; for `list`, the label).
  - `escape_name(name: str) -> str`: backslash before each of `\ , [ ] * ~ "`.

- [ ] **Step 1: Write the failing tests** in `tests/scripts/retry_failed_tests.sh`, modelled on `release_scripts_tests.sh` (usage `<scratch dir>`, `ok:` and `FAIL:` lines, exit status capped at 1), with a stub `gg-tests` script that logs its arguments and exits by a scenario file, and hand-written XML reports. Checks:
  - clean: status 0, no failed case: exit 0, stub not run;
  - one failed, passes retry: exit 0, stub called once with the escaped name, `::warning` printed, summary row written;
  - one failed, fails retry: exit 1, `::error ... failed twice`;
  - the Review Focus' lines, one check each: each special character in a name; truncated XML; status 42 with no failed case and status 0 with one; retry exiting 2 is failed; 5 failures retried, 6 not (stub not run);
  - status 139, and a missing report: exit 1, stub not run;
  - `list`: missing file exit 0; two lines, each passed as one argument; a retry failing exit 1; 6 lines not retried;
  - no arguments: exit 2.
- [ ] **Step 2: Run** `bash tests/scripts/retry_failed_tests.sh /tmp/rr`. Expected: FAIL (script absent).
- [ ] **Step 3: Implement `scripts/retry-failed.py`**, parsing with `xml.etree.ElementTree` (a `ParseError` is the truncated case). A case failed when its `TestCase` has `OverallResult success="false"`.
- [ ] **Step 4: Run** the tests. Expected: every line `ok:`, exit 0.
- [ ] **Step 5: Against the real binary.** Build `gg-tests` in this tree (`scripts/worktree-setup.sh --configure dev`, with the ccache launchers exported; `cmake --build --preset dev --target gg-tests -j 8`). Confirm `gg-tests "$(python3 -c 'import importlib.util as u,sys; s=u.spec_from_file_location("r","scripts/retry-failed.py"); m=u.module_from_spec(s); s.loader.exec_module(m); print(m.escape_name("renders are identical with no pool and pools of 1, 2 and 8 workers"))')" --list-tests` prints `1 matching test case`. This requires the script to guard its entry with `if __name__ == "__main__"`.
- [ ] **Step 6: Commit** (`scripts/retry-failed.py`, `tests/scripts/retry_failed_tests.sh`, `tests/CMakeLists.txt`). Run `ctest --preset dev -L scripts -j 4` first; all pass.

### Task 2: Wire the Rosetta step

**Files:**
- Modify: `.github/workflows/ci.yml` (the step "Test the x86_64 slice under Rosetta (macOS)" and its comment)

**Interfaces:** Consumes Task 1's two commands.

- [ ] **Step 1: The shards.** Each runs `arch -x86_64 "$tests" "~[serial]$slow" --order decl --shard-count 3 --shard-index "$i" --reporter console::out=- --reporter "XML::out=$logs/shard-$i.xml"`, console redirected to `$logs/shard-$i.txt` as today. After its `wait`, with its log group printed as today, a failing status runs `python3 scripts/retry-failed.py catch2 --report "$logs/shard-$i.xml" --status "$status" --summary "$GITHUB_STEP_SUMMARY" -- arch -x86_64 "$tests"`; only that command's failure sets `failed=1` and prints today's `::error`.
- [ ] **Step 2: The serial run**, the same with its own report and status. Its `--allow-running-no-tests` stays on the first run.
- [ ] **Step 3: The goldens.** `golden` records a case that `gg-render` failed with status 1 as a line `<spec>|<expected dir>|<output dir>` appended to `$out/failed-goldens.txt`, and returns 0. Any other non-zero status prints today's `::error title=Rosetta golden` and returns 1. After both `xargs` runs: `python3 scripts/retry-failed.py list --failures "$out/failed-goldens.txt" --label golden --summary "$GITHUB_STEP_SUMMARY" -- bash -c 'IFS="|" read -r s e o <<< "$1"; golden_once "$s" "$e" "$o"' _`, where `golden_once` is the render-and-compare and `golden` wraps it; `golden_once` is `export -f`'d. The retry's failure sets `failed=1`.
- [ ] **Step 4: Update the step's comment** to say it retries, why (INTENT's new §23 row), and that nothing else does.
- [ ] **Step 5: Verify locally:** extract the step's `run` block with `python3 -c` over `yaml.safe_load` into a scratch file and run `bash -n` on it (the workflow's `${{ }}` substituted). Expected: no syntax error.
- [ ] **Step 6: Commit** `.github/workflows/ci.yml`.

### Task 3: The records

**Files:**
- Create: `docs/spikes/m3-rosetta-compare-probe.md`
- Modify: `INTENT.md` (§23), `CLAUDE.md` (the "x86_64 slice under Rosetta" paragraph), `docs/releasing.md` ("The Rosetta kernel test fails on the macOS leg"), `docs/m3-carry-over.md` (rows 234, 403)

- [ ] **Step 1: The spike.** The probe's full source (`git show row403-probe:scripts/row403-probe.cpp`) in a fenced block, how it ran, and the results: run 42687277007; `fresh` 572 and `reuse` 531 float-compare misfires in 377,588,640,000 and 379,253,856,000 compares; the 80 reported all held right operands with rescan bad 0; `sse` 0 in 2,008,571,904,000; all nine shard runs passed that time. The limits: no native arm64 control; `sse` compared by integer bits, so only the float compare is implicated; row 234's `sameBits` and event-log failures are inferred, not proven.
- [ ] **Step 2: INTENT §23**, the next free row number: the director's ruling of 2026-11-09, amending #75: the runner's float compares misfire, row 403 closed, the step's retry and its limits. Dated.
- [ ] **Step 3: `CLAUDE.md` and `docs/releasing.md`:** the step retries a failed case once and lists it; a run red after that is a real failure, so don't re-run it hoping.
- [ ] **Step 4: Carry-over.** Row 403 closed (the runner's fault, the probe, the ruling). Row 234: the probe's finding, what stays inferred, and that the retry now absorbs it. Add the two occurrences of row 403 at runs 42381691057 and 42390128189, and sub-project 1's parked minors: the Cloud-choke wording, stale `NoteEvents.h` comments, choke-then-full-table-replacement, missing multi-block and `[rt]` choke tests.
- [ ] **Step 5: Commit** the five files.

The controller, after the pull request is merged: `git push origin --delete row403-probe`, and delete the local branch.
