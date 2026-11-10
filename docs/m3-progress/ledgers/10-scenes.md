# SDD ledger — plan: /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp10-scenes/docs/superpowers/plans/2026-11-07-m3-scenes.md

Spec: docs/superpowers/specs/2026-11-07-m3-scenes-design.md (approved 2026-11-09; N1-N15 confirmed by the director's brainstorming).
Branch m3-sp10-scenes; origin/main merged 49247b2; plan amended f8a7a14 (writing-plans, after the merge).
Models: opus for Tasks 4, 5, 6, 7 and the final review; sonnet otherwise and for every task reviewer. Never Fable.
Load: ctest -j 4; build -j 8; full suite once per task (the implementer's); at-rest check only for DSP/render tasks.

## Pre-flight scan

| Pair / task | Produces vs consumes | Found |
|---|---|---|
| T1 -> T2 | isSceneSlot, isSceneControl (ParamIds.h) -> Scenes.cpp tables | consistent |
| T1 self | NP1 builds isSceneSlot from AUTOMATABLE minus Budget and the seven; test pins = !(control or Budget or isMidiSetting or Ring Length) | consistent only if MIDI settings and Ring Length are not AUTOMATABLE; see Ruling 1 |
| T2 -> T3 | SceneBank, capture, dropRingParams, clearSceneMomentaries -> PTCH and presets | consistent |
| T2 -> T4/T5/T6 | morphTarget, recallInto, sceneIndexOf, switchOn, divisionBeats | consistent |
| T3 -> T7 | fromStateXml(..., SceneBank*, names*) with momentaries cleared (NP8) -> plugin loads | consistent |
| T3 -> T9 | toStateXml/fromStateXml -> gg-render --save-state / state: | consistent |
| T4 -> T5 -> T6 | writeSceneBase, requestRecall, applyRecall, commitScenes, finishScenes in Processor.cpp; serial edits of one file | consistent; T4's requestRecall applies at once, T6 adds the wait (stated in both) |
| T4 SceneStatus | waiting/waitingDivision filled only from T6, glideProgress from T5 | consistent (T4 leaves defaults) |
| T6 -> T8 | ControlKind::ProgramChange -> 0xC0 path | consistent |
| T6 -> T9 | ProgramChange -> "programChange" control | consistent |
| T6 test vs T10 | scenes_quantize_block_size_invariant builds fx-scene-quantize's events in the test, before T10's JSON exists | consistent (built in test) |
| T4 -> T7/T8 | takeSceneWrites, takePublishedScenes, setScenes(bank, epoch) | consistent |
| T7 -> T8 | sceneOutPending_ path uses values_ updated by T7's flush; both in GigaGrainsPlugin.cpp | consistent, serial |
| T8 test rename | "remote controls pages are thirteen" exists on main (params_tests.cpp:1263) | consistent |
| T9 -> T10 | spec keys -> five golden specs | consistent |
| T10 -> T11 | goldens and slow-list -> AGENTS.md counts 73 / 121 / 297 / 14 pages | consistent (amended for AGENTS.md) |
| T11 self | §23 row number: #97 taken on main | amended: next free row at write time (#98 now) |
| T12 self | check.sh runs ctest at the preset's 8 jobs; throttle says -j 4 | Ruling 2 |
| T12 vs Added note | listening pack form (README + specs/, no renders) | amended in plan |
| NP14 vs sibling rule | Task 6 could depend on SP7's SyncClock | Ruling 3 |

Ruling 1: isSceneSlot's source of truth is spec §4.2's table as the T1 test pins it; if a flag disagrees (an automatable MIDI setting or Ring Length), the implementation excludes it explicitly and the test stays as written — the spec is binding — costs a few lines if wrong.
Ruling 2: at Task 12 the gate runs check.sh's steps by hand per preset at ctest -j 4 (configure if needed, build gg-all for asan / default for dev, ctest --preset P -j 4, format.sh --check) instead of check.sh at 8 jobs — the throttle note is binding on this shared box — costs one manual summary instead of check.sh's.
Ruling 3: Task 6 adds TransportSource's own accessors (NP14) and does not wait for sub-project 7 — spec §4.4 and §7 give this branch the accessors ("whichever lands second adopts"), so it is not a "needs a sibling" task under the 2026-11-09 note — if wrong, SP7's merge adopts or replaces three small accessors.

## Tasks
Task 1: dispatched (sonnet, agent a9d911760f43599a0), BASE f8a7a14
Task 1: implementer done, committed bcc6cc7 (BASE f8a7a14); NOT reviewed (pause arrived before the review). Report task-1-report.md: Ruling 1 found no disagreement (MIDI settings and Ring Length aren't automatable); at rest 108/0/0; voice WAVs identical; crawl 50.84 ms (load).

PAUSED 2026-11-09. RESUME POINT: head bcc6cc7 on m3-sp10-scenes (pushed to origin, no PR). Done: origin/main merged (49247b2), plan confirmed and amended with writing-plans (f8a7a14), pre-flight scan and Rulings 1-3 above, Task 1 implemented (bcc6cc7). Next: Task 1's task review (sonnet; review package BASE f8a7a14 HEAD bcc6cc7, brief task-1-brief.md, report task-1-report.md, constraints.md), then the fix loop if needed, then Task 2. No task branches or task worktrees exist. No ruling pending. A fresh lead: merge origin/main first (siblings may have landed: re-pin isSceneSlot's table and the counts on purpose per spec §7), delete nothing; build/dev is configured and built in this worktree; constraints.md carries the load rules and TDD line for every dispatch.
