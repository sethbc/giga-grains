# Final whole-branch review (opus), afb1562..da7a8ae, 2026-11-09

Ready to merge: with fixes. 0 Critical, 0 Important, 9 Minor. The code can't pass a real failure; only two record wordings must change before merge.

Ran: retry_failed_tests.sh all ok (Python 3.14); bash -n on the extracted step; a golden-path simulation under bash -eo pipefail (export -f, xargs -P 3, list retry through bash -c read, spaces in paths) ended failed=0; real gg-tests: an unmatched name exits 2, the escaped threads name matches one case. Catch2's fatal-signal handler completes the XML before re-raising, so the status check (only 0/42) is the real guard against crashes; it holds.

Minor (MUST before merge: 1 and 2):
1. docs/m3-carry-over.md:41 (row 403): "and the runner is the explanation for the rest" claims a cause for run 41534801566 (no diagnostic). Fix: "the director's ruling attributes the first, undiagnosed failure (run 41534801566) to the runner too." (= the ledger's Task 3 deferred minor)
2. docs/spikes/m3-rosetta-compare-probe.md:278: "A real regression fails twice and still fails the step": true only for a deterministic one (the retry runs the case alone). Fix: "A deterministic regression fails twice...".
3. scripts/retry-failed.py:31-33 escape_name: Catch2 drops escapes before building its wildcard, so an escaped leading/trailing `*` is still a wildcard, and matching is case-insensitive (confirmed on the real binary). Fails safe (the named case always runs); no test name starts or ends with `*`. Fix: a docstring note.
4. spike:256-257: the compare-vs-load distinction rests on unchecked codegen (-O3; disassembly not inspected). Fix: "a float compare-and-branch (or the load feeding it)", or say codegen wasn't checked.
5. tests/CMakeLists.txt:380-385: scripts_retry_failed is the first CTest needing python3; a minimal Linux bootstrap would show bare FAIL lines. Fix: find_package(Python3 COMPONENTS Interpreter) guard, a clear skip message, or python3 in bootstrap-linux.sh.
6. Rate rounding: docs/releasing.md:506-507 (and the spec) say 1 in 690 million; INTENT #98, row 234 and the spike say 686 (756,842,496,000 / 1,103). Use 686 in releasing.md (the spec is the approved record; leave it or note it).
7. INTENT row 75 lacks an "(amended by #98)" back-reference, as earlier rows carry.
8. Spec status line (line 5) still says "Draft for the director's approval".
9. More than 5 failures leaves no list of names (retry-failed.py:87-88; ci.yml:946): before, each failing golden had its own ::error. Fix: write the names to the summary as "not retried".

Ledger triage: R1-R7 sound. Parked T1 M2, T1 M3 (add M3's docstring note), T2 M1: fine to leave. T3 deferred: must fix (= Minor 1).

Recommendations: (a) prove the retry path on the real runner before merge: a scratch branch with a test or golden that fails once then passes (marker file), dispatched on the macOS leg; check ::warning, the summary table and a green step; then a fail-twice variant goes red. (b) The step's 20-minute limit (ci.yml:850) could be hit by retrying a slow case on the nightly; consider raising it a few minutes.

Declined to judge: the CI-history counts; whether retrying alone hides load/order bugs (spec's choice); the spec's 690M figure; the probe's method; Catch2 stdout capture; spec-parser tag-alias/space/exclude edges; summary cell length; rows 423-426's content (count 32 checked).
