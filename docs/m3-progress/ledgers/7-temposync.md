# SDD ledger — plan: /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp7-temposync/docs/superpowers/plans/2026-11-07-m3-temposync.md

Sub-project 7 (Tempo sync), lead session started 2026-11-09. Gate: program ledger line "7: gate passed 2026-11-09 (brainstorming; R3 overruled ...)".
Spec: docs/superpowers/specs/2026-11-07-m3-temposync-design.md (amended 75b9238: R3 per-modulator Sync switches 0x6B-0x6F).
Plan amended 8e43e50. Branch m3-sp7-temposync; origin/main merged (afb1562) before amendments.
Constraints for every dispatch: .superpowers/sdd/2026-11-07-m3-temposync/constraints.md

## Pre-flight scan (2026-11-09)

| Pair / task | Produces vs consumes | Found |
|---|---|---|
| T1 -> T3 | SyncClock observe/resolveAt/beatAt/epoch/anchor*, Transport beatUnit/barStart | agree |
| T1 -> T8 | nextLineSample, locked, epoch | agree |
| T1 -> T14 | anchorSample, anchorBeat, beatsPerSample, epoch | agree |
| T2 -> T5 | noteValueText, loopBeatsText, *FromText, *Ref | agree |
| T2 -> T6 / T7 / T14 | modDivisionBeats; delayDivisionBeats, loopSyncBeats; passBeats | agree (ModSyncLevel/modSyncs removed from T2 by the amendment; no consumer left) |
| T4 -> T5, T6, T7, T8 | SyncedMod, modSyncOf(values, layer, m), loopSyncOf, delaySyncOf, ringQuantizeOf | agree after amendment; SyncDivisions.cpp (T5) includes ParamMap.h on the main thread: fine |
| T4 <-> T14 | ParamIds.h lists, isReservedIndexInUse, EditLayout, Mutate | T14 extends T4's TEMPOSYNC list and the 0x7D test to 0x7D-0x7E: agree |
| T3, T6, T7, T8 | Processor.cpp stepSourcesAt / process() / applyCellsBelow | sequential edits of one file; serial execution: no conflict |
| T7 <-> T14 | tempoFeaturesEngaged | T14 adds Scan Sync: agree |
| T5 <-> T14 | plugin text helpers, rescan | T14 adds passText: agree |
| T9 -> T10, T11 | transport keys, RenderOptions::transportEveryCall | agree |
| T12 <-> T14 | records | T14 amends T12's: agree |
| T1 self | tests vs arithmetic | 6/8 offset 1.5 -> 36,000; beat -4.25 -> 6,000; 1 BPM bar 11,520,000: all checked, consistent |
| T2 self | tests vs tables | 1/8T at log2 6 = 1/3 beat, consistent |
| T3 self | the 10 s transport sequence's beats | 6.0 at 5 s, 9.0 at 7 s: consistent |
| T4 self | counts 69/316, 26 slots | consistent |
| T5 self | consistent |
| T6 self | Euclidean mod k=-9, len 6 -> 3: consistent |
| T7 self | 1.875 at 1 BPM, 96,000/128,000, 115,200: consistent |
| T8 self | Beat line 144,000 with R=960; relock to 240,000; 6 BPM bar at 40 s: consistent |
| T9-T11 self | consistent |
| T13 self | Step 1a listening pack before Step 5's PR: consistent with the brief |
| T14 self | waits for 3b on main: consistent with the brief's sibling rule |

Rubric conflicts: none found.

Ruling: ctest runs with `-j 4` (throttle note) though the presets default to 8; the implementer runs the full suite once per task just before committing, others run focused labels — the throttle note binds — costs only time.
Ruling: models per plan (opus Tasks 1, 3, 6, 8, 14; sonnet the rest and every task reviewer; opus final review) — slowdown note allows opus for tasks the plan marks design-heavy — cost if wrong: usage.
Ruling: Task 4 carries the ledger note (program ledger line 54) that new parameter IDs move the sweep bursts' rendezvous draw; a sweep exemption or floor may need re-pinning with its reason — costs a fix round if missed.
Ruling: the crawl timing test (50 ms) failing under load is load unless it fails alone on a quiet machine; record the figure — brief's rule.
Ruling: Task 14 (Scan Sync) is last; if 3b is not on main when 1-13 are done, open the PR without it (RB14) and report — brief's sibling rule.

## Progress
- Task 1 dispatched (opus, implementer a02ffe14145d13735), BASE 8e43e50
- PAUSE requested 2026-11-09 (pause-2026-11-09.md): Task 1 implementer allowed to finish and commit; no review dispatched
- Task 1 implemented: 52d0085 (BASE 8e43e50), DONE_WITH_CONCERNS (7 interpretations, task-1-report.md "Concerns"), UNREVIEWED. Full suite 1817/1818 (only the crawl test, 50.3 ms under load).

PAUSED 2026-11-09. RESUME POINT: branch m3-sp7-temposync at 52d0085, pushed to origin, clean; no task branches or task worktrees exist. Done: origin/main merged (afb1562); spec R3 amended (75b9238); plan amended (8e43e50); worktree set up (build/dev configured with ccache); Task 1 committed (52d0085) but NOT reviewed. Next: generate the review package `review-package PLAN 8e43e50 52d0085` and dispatch Task 1's task reviewer (sonnet) with task-1-brief.md, task-1-report.md and constraints.md; have it weigh the report's 7 interpretations (llround via floor, meter applied without a timeline, NaN host beat = no timeline, seeding rule, internal == !locked, stopped seeding extrapolates). Fix-loop rounds 1-3 resume implementer a02ffe14145d13735 if it still exists, else a fresh opus implementer. Then Task 2 (sonnet). Rulings pending: none beyond the pre-flight table. Before resuming: merge origin/main (siblings may have landed: resolve ParamIds lists in index order). Task 14 (Scan Sync) waits for 3b on main. Note: sibling leads share the session scratchpad; prefix scratch files sp7-.
