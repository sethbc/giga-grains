# SDD ledger — plan: docs/superpowers/plans/2026-11-09-rosetta-retry.md

Setup: worktree gg-rosetta-retry, branch rosetta-retry, worktree-setup --configure dev exit 0; submodules at pins.
Spec: docs/superpowers/specs/2026-11-09-rosetta-retry-design.md (binding).
Base before Task 1: a99ba80.

## Pre-flight scan

| Pair / task | Produces vs consumes | Found |
|---|---|---|
| T1 -> T2 | retry-failed.py `catch2`/`list` CLI vs the step's two calls | Agree. `list`'s line `<spec>\|<expected>\|<out>` lands in a Markdown table cell: R2 |
| T2 -> T3 | ci.yml comment names INTENT's new row vs T3 adding it | Number must match: R6 |
| T1 -> T3 | script/test names vs AGENTS.md | T3 names only the Rosetta paragraph; the `scripts` label list goes stale: R5 |
| T1 self | tests vs code | Agree. First-failure assumes `<Expression>`; `FAIL()` and exceptions have none: R3. Test script runs on macOS bash 3.2: R4 |
| T2 self | "a failing status runs the script" vs Review Focus 3 (status 0 with a failed case) | The script's status-0 branch is tested but the step never calls it at 0. Not a conflict: R7 |
| T3 self | five files vs steps | Agree |

Ruling R1: split argv at the first `--` by hand, before argparse — argparse's REMAINDER/`--` handling changed across 3.12-3.13 and the macOS runner's Python differs from this box's — costs a few lines if wrong.
Ruling R2: table cells escape `|` as `\|` and newlines as spaces; annotation text escapes `%`, CR, LF as GitHub's workflow commands require — golden lines hold `|`, Catch2's Expanded can hold both — cost if wrong: cosmetic.
Ruling R3: a failed case with no failing Expression falls back to its first Failure, Exception or FatalErrorCondition, else "(no message)" — plan says only Expression, spec says "first failure's message" — cost if wrong: one extra branch.
Ruling R4: tests/scripts/retry_failed_tests.sh stays bash 3.2-safe — macOS legs run the scripts label — cost: none.
Ruling R5: AGENTS.md's `scripts` label line gets one clause naming retry-failed.py beyond the plan's paragraph — the list is otherwise stale — cost: one clause.
Ruling R6: Task 2's comment names row 98; Task 3 re-checks the next free number at commit and fixes the comment if it moved.
Ruling R7: the step calls the script on non-zero status only, as the plan says — status 0 with a failed case stays covered in the script's tests, unreachable from the step — cost if wrong: a Catch2 bug reporting 0 with a failure goes unseen, as today.

Task 1: implementer ae57367 (sonnet), commit 3b9a8c9. Review (sonnet): spec ✅, Approved, 0 Critical/Important, 3 Minor.
Task 1 Minor 1 (bash 3.2 `\"` in `${4-...}` default, retry_failed_tests.sh:93): fixing in the same round.
Parked: Task 1 Minor 2 — a case name listed twice among failed cases is retried twice and counts twice toward 5 — Catch2 lists a TestCase once — harmless if wrong.
Parked: Task 1 Minor 3 — the suite never runs real Catch2, so a Catch2 upgrade changing its escape rule goes uncaught — Task 1 step 5 checked it by hand against the pinned v3.16.1 — cost if wrong: a retry runs no test and counts as failed (fails safe).
Task 1: complete (commits a99ba80..e89de4c, review clean; 2 Minors parked)
Task 2: dispatched implementer a591dbc (sonnet), BASE e89de4c
Task 2: commit 94ba115. Review (sonnet): spec ✅, Approved, 0 Critical/Important, 3 Minor.
Parked: Task 2 Minor 1 — the golden retry names its case by the whole `spec|expected|out` line — the plan mandates that line format — cost: a noisier summary row.
Task 2 Minor 2 (row 98 not yet in INTENT): Task 3 adds it (R6). Minor 3 (cap path not simulated): Task 1's tests cover it.
Task 2: complete (commits e89de4c..94ba115, review clean; 1 Minor parked)
Task 3: dispatched implementer a863d34 (sonnet), BASE 94ba115
Task 3: commit 85e573b. Review (sonnet): Needs fixes — 2 Important (row 403 overclaims: "never at fault", "each time"), 3 Minor (row 234 stale re-run text, row 403 missing period, spike loose rate line), all sent in fix round 1 with the run dates.
Task 3: fix round 1/5 (5 addressed, 0 open; commits 85e573b..da7a8ae)
Task 3: minor (deferred): row 403's "and the runner is the explanation for the rest" still asserts a cause for run 41534801566, which had no diagnostic.
Task 3: complete (commits 94ba115..da7a8ae, review clean)
Final review (opus): with fixes, 0 Critical/Important, 9 Minor; full text in final-review.md beside this ledger. No fix wave started (pause).

PAUSED 2026-11-09. RESUME POINT: Tasks 1-3 complete and reviewed (branch rosetta-retry at da7a8ae, pushed to origin; worktree /home/sethbc/.local/share/giga-grains/worktrees/gg-rosetta-retry; no task branches or worktrees). Final whole-branch review done (final-review.md). Next: ONE final-review fix dispatch (sonnet) with final-review.md's Minors 1 and 2 (must, record overclaims) plus the cheap ones (3 docstring, 4 spike wording, 6 releasing.md 686, 7 INTENT #75 back-reference, 8 spec status, 9 list names when over 5; 5 python3 guard at the fixer's judgment), then one scoped re-review (sonnet), then finishing-a-development-branch: draft PR to main, CI green (one poll at a time). Consider the reviewer's recommendation to prove the retry on the real runner with a scratch fail-once branch, and raising the step's 20-minute limit; both are the director's or the lead's call, not yet ruled. Don't delete row403-probe or merge: the top controller does both. Rulings R1-R7 and the parked minors are above.
