# The Rosetta step retries failed cases once: short design

| | |
|---|---|
| **Status** | Draft for the director's approval, 2026-11-09 |
| **Size** | A small change: one CI step, one tested script, the records |

## Why

CI's macOS leg runs the x86_64 slice under Rosetta on GitHub's M1 VM. That step failed 8 of the last
17 finished runs, across unrelated tests, while native arm64, Linux, Windows and the sanitizers
passed. Probe run 42687277007 ran a program with no product code on that runner: plain float
compares misfired 1,103 times in about 757 billion, with correct operands in all 80 reported. The
director's ruling (2026-11-09): the runner's fault; row 403 closes; the step re-runs failed cases
once and lists them.

What the probe does not prove: that the misfire explains row 234's `sameBits` and event-log
failures. That stays an inference, and the retry doesn't depend on it.

## Approaches (the director picked the first)

1. **Re-run only the failed cases once, automatically, and list them.** Ends the red runs; a repeat
   failure still fails.
2. Close the row and re-run the job by hand (`docs/releasing.md`). No gate changes; about half of
   runs keep needing a person.
3. Run a native arm64 control of the probe first and hold the ruling. Native tests have never shown
   this failure, so it wouldn't change the answer.

## Behavior

- **Scope:** the "Test the x86_64 slice under Rosetta" step alone: its three shards, its `[serial]`
  run and its golden checks. Native arm64's `ctest`, Linux, Windows and the sanitizers are untouched
  and never retry.
- **A shard or the serial run** writes Catch2's XML report beside its console output. Each case the
  report marks failed is re-run once, on its own, in that step.
- **A golden case** that fails is re-rendered and compared once.
- **The rule:** a case that passes its retry is reported and doesn't fail the step. A case that fails
  its retry fails the step.
- **No retry at all, so the step fails, when:**
  - more than 5 cases failed in one shard, or more than 5 golden cases failed (a real regression,
    not a 1-in-690-million event);
  - a process crashed or its report is missing or incomplete (only a clean "these cases failed"
    qualifies).
- **The record:** every retried case is listed in the step summary with its first failure's message
  and retry result, plus a `::warning` annotation, so a pattern stays visible.

## Non-goals

No retry anywhere else. No change to any test. No change to `ci-ok`, the required checks or the
shard layout (INTENT §23 #75). The ring-reader test's diagnostic and `classifyMismatch`'s wrong
label aren't touched.

## What's left to the builder

The retry logic lives in a script under `scripts/`, tested under the `scripts` label with a stub
`gg-tests`, not inline in `ci.yml`, so that it can be tested at all (red first). How a case name is
escaped for Catch2's test spec is the builder's.

## Records, in the same change

- INTENT §23: a new row amending #75 (Part II), with the probe's numbers.
- `AGENTS.md`'s "x86_64 slice under Rosetta" paragraph; `docs/releasing.md`'s Rosetta entry.
- Carry-over: row 403 closed, row 234 updated with the probe and the inference.
- `docs/spikes/m3-rosetta-compare-probe.md`: the probe's source, its method and results, so that the
  throwaway `row403-probe` branch can then be deleted.

## Order

Built on a branch from `main` after PR #37 merges, as its own pull request. CI proves it on the step
it changes.
