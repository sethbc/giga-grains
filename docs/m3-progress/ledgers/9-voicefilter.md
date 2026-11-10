# SDD ledger — plan: /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp9-voicefilter/docs/superpowers/plans/2026-11-07-m3-voicefilter.md
**Before resuming (the director, 2026-11-08):** read docs/m3-progress/briefs/STANDARD-WORKFLOW.md, which is binding, and check this sub-project's gate in m3-program-ledger.md.

Spec: docs/superpowers/specs/2026-11-07-m3-voicefilter-design.md (read). Lead brief: scratchpad lead-brief-common.md.
Start: merged m3-voice (b8dd6ac, final review's fix wave) into m3-sp9-voicefilter -> 245cbfe.

## Pre-flight scan
| Pair / task | Produces -> consumes | Found |
|---|---|---|
| T1 -> T4,T5,T6 | renderSchedule, BeforeCall, VoiceSchedule, fnv1a | consistent |
| T1 -> all "at rest" steps | check-at-rest.sh | consistent |
| T2 -> T7,T8,T9,T10 | LayerParam::VoiceFilter..VoiceKeyTrack, 306/67 counts | consistent |
| T2 vs T7 | isVoiceTarget false for four (T2 test) -> T7 flips Cutoff, Resonance (P7) | consistent; T2's step 1 adds a mod_matrix_tests case not in its Test file list (minor, allowed) |
| T3 -> T4,T5,T6,T11 | VoiceFilterBank API, kNoLane, VoiceFilter.h | consistent |
| T4 -> T5,T6,T7,T11 | observers, setVoiceFilterType, VoiceParams fields, GrainRows::lane | consistent |
| T5 split rule vs spec FB3/4.2 | plan: engaged_ or (anyBusy && requested != target); spec: engaged or request != in force | equivalent given FB15 (no busy lane -> request applies at once) |
| T5 -> T6 | partition reference helper (EngineReference.h) | consistent |
| T7 -> T8,T9,T10 | Processor cells/type | T8's rt test and T10's goldens need T7: order T7 before T8/T9/T10 |
| T11 vs T4/T5 | observers, engaged path | consistent |
| T12 | records after all | consistent |
| T13 vs lead brief | plan: PR base m3-voice while #37 open; brief: base main | Ruling R1 |
| each task self-consistency | T1..T13 tests vs files | consistent; T2's files incl. tests/core/mod_matrix_tests.cpp implicitly |

## Rulings
- Ruling R1: PR base is `main` (lead brief), not `m3-voice` — the top controller's brief is binding over the plan — cost if wrong: the PR diff includes m3-voice's commits until #37 merges; retargeting is one command.
- Ruling R2: ctest runs at `-j 4` (top controller's load note) where the plan says the preset's 8 — load — cost: slower tests.
- Ruling R3: check-at-rest.sh runs for tasks that change DSP/rendering or parameter tables (T1, T2, T4, T5, T6, T7, T10, T13); skipped for T3 (bank unreachable from any render), T8 (tests + docs, unless it changes code), T9 (test only), T11 (bench only), T12 (docs) — load note — cost if wrong: a hidden at-rest move caught later at T13's check.
- Ruling R4: the asan tree is configured only when first needed (Task 6), not in Task 1 — load — cost: none (T6 configures it).
- Ruling R5: parallel waves: T2 || T3 (T3 in worktree /home/sethbc/src/gg-sp9-t3); T8 || T9; T10 || T11; at most two implementers building at once — the plan's dependencies allow it — cost: merge conflicts in tests/CMakeLists.txt (trivial).

## Tasks
- Task 1: dispatched (sonnet, main worktree), BASE 245cbfe
- Task 3: dispatched (opus, worktree /home/sethbc/src/gg-sp9-t3 branch m3-sp9-t3), BASE 245cbfe
- PAUSE ordered by coordinator (director: slow down for usage): finish T1 and T3 in flight (review + merge), dispatch nothing new, no PR. Resume at Task 2 (main worktree) after T1/T3 merge.
- Task 3: implementer DONE ef3be3b (m3-sp9-t3); reviewer dispatched (sonnet) on review-245cbfe..ef3be3b.diff
- Task 3: review APPROVED (spec ✅, 0 Critical/Important). Minors parked:
  - Ruling R6: add SST_CPPUTILS_NONBLOCKING to setCell, drain, raiseLastEnd, endAllAt, zero, advance in Task 4's dispatch (Task 4 edits the bank's callers anyway) — the RTSan CI job's function-effects may need it — cost if wrong: one CI round.
  - Ruling R7: stateMagnitude's std::max ignoring NaN (a NaN lane reads 0 and frees) is accepted, documented in Task 6 (non-finite cases) — freeing a poisoned lane is the right outcome — cost if wrong: none audible.
  - Ruling R8: tests for endAllAt/zero, channels 2-3 bit-equality, reset()'s redundant busy_ = 0: left to Tasks 4-6, which exercise them through the engine — cost: none.
  Merge into m3-sp9-voicefilter pending Task 1's commit (same tree).
- Task 3: complete (commits 245cbfe..ef3be3b, review clean, 3 minors parked R6-R8); merged as 23de95e; voicefilter label 8/8 after merge; worktree gg-sp9-t3 removed
- Task 1: review approved with 2 Important (schedules not shown non-silent; steal check tautological) -> fix round 1 sent to the implementer (FIX_BASE d5cb0b8; HEAD then 23de95e).
- Task 1: minor (deferred): check-at-rest.sh hard-codes SP1's worktree path (acceptable, out of git); chord/five-argument share a form (names per brief).
- Note for the coordinator: SP1's goldens inst-voice-expr and inst-voice-mpe have identical WAV SHA-256 (a205ea99...); may be legitimate, worth SP1's check.
- Task 1: fix round 1/5 (3 addressed, 0 open — non-silence asserts, steal check reads the render, baseline line-count guard; commits 23de95e..b992efc)
- Task 1: complete (commits 245cbfe..b992efc, review clean after 1 round); at rest: 108 + 8 goldens, 0 changed; schedules 0 changed
- Task 1: minor (deferred): steal check hard-codes 16*1500 (the schedule's timing); parse() tied to the sink's event-line format.

## RESUME POINT (paused 2026-11-07 by the coordinator: director asked to slow down for usage)
- Branch m3-sp9-voicefilter at b992efc: m3-voice b8dd6ac merged; Tasks 1 and 3 complete and merged; no task worktree open; nothing pushed, no PR.
- Next: Task 2 (the four parameters; sonnet; main worktree; brief task-2-brief.md already extracted). Then Task 4 (opus) — carry Ruling R6 (NONBLOCKING annotations on the bank's remaining methods) into its dispatch.
- Before resuming: merge m3-voice (or origin/main if SP1 has landed) again.

## RESUMED 2026-11-09 (the director's go; docs/m3-progress/briefs/resume-2026-11-09.md; gate line present; every call confirmed)
- Merged origin/main (e9a9567) -> 91190e9, no conflicts. build/ deleted and dev reconfigured with ccache.
- scripts/check-at-rest.sh is not on main yet; the workspace check-at-rest.sh falls back to SP1's check-wav-hashes.sh.
- Task 2: dispatched (sonnet, main worktree), BASE 91190e9
- NEW DUTY (the director, 2026-11-09, resume brief "Added 2026-11-09"): before finishing, add docs/listening/m3-voicefilter/ in M2's form (README with render steps + specs/ of a handful of gg-render specs, A/B pairs Off vs On and the confirmed laws: Key Track, Resonance, type per voice; a few minutes of listening); list it in the PR body. Ruling R9: fold it into Task 12 (records) — docs only, after Task 10's goldens exist — cost if wrong: one more docs commit. Also: report the crawl timing figure (50 ms test) in the final report. No sibling dependency in SP9's plan.
- Task 2: implementer DONE d196ce2 (report task-2-report.md: RED/GREEN present; at rest 108 + 8, 0 changed; sweep: fx bursts seed 3101 -> 3121 (row 217 Reverb Size echo at 30.2), floors 0.44 and 0.37 with reasons; crawl test 55.4-55.8 ms alone on the loaded box). NOT YET REVIEWED (pause arrived before the review dispatch).

## PAUSED 2026-11-09. RESUME POINT
- Head d196ce2 on m3-sp9-voicefilter (origin/main e9a9567 merged at 91190e9). Pushed to origin as a backup; no PR.
- Done: Tasks 1, 3 (complete, reviewed). Task 2 committed and reported, review pending.
- Next: generate the review package 91190e9..d196ce2 and dispatch Task 2's task reviewer (sonnet); then Task 4 (opus) carrying Ruling R6 (SST_CPPUTILS_NONBLOCKING on the bank's setCell, drain, raiseLastEnd, endAllAt, zero, advance).
- No task branches or task worktrees open. Rulings R1-R9 stand; R9 folds the listening pack (docs/listening/m3-voicefilter/) into Task 12.
- Briefs for Tasks 4-13 are already extracted in this workspace. Final report must quote the crawl timing figure (55 ms here, under load).
