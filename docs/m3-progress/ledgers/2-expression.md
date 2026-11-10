# SDD ledger — plan: docs/superpowers/plans/2026-11-07-m3-expression.md
**Before resuming (the director, 2026-11-08):** read docs/m3-progress/briefs/STANDARD-WORKFLOW.md, which is binding, and check this sub-project's gate in m3-program-ledger.md.
Spec: docs/superpowers/specs/2026-11-07-m3-expression-design.md (binding). Branch m3-sp2-expression; merged m3-voice 18ac47d as 471bbc6.
Layout: every task runs in its own worktree /home/sethbc/.local/share/giga-grains/worktrees/gg-m3e-tN (branch m3e-tN off m3-sp2-expression); the branch worktree only receives merges.

## Pre-flight scan
| Tasks | Shared file/interface | Finding |
|---|---|---|
| 1→2,3,5,13 | ParamIds slots Pan, MpeLower/UpperZone, upper ranges | 2,3,5 consume Task 1's slots; must start after 1 merges |
| 3→7,8,12,14 | kVoiceLayerTargets/kPerNoteSlots, VoiceInputs | 7 and 8 fill VoiceInputs fields; 12's "92 slots" = 4×22+4 Levels, consistent |
| 4→8 | ParamEvent::seq, eventRank | 8 adds rank 2; consistent |
| 4,6,7 | NoteEvents.h ControlKind/seq | serial edits, append-only; fine |
| 5→6→7 | MpeLayout, MidiSettings, ControlKind::Expression | consistent |
| 6,7→11; 8→12; 6-8→13 | plugin/gg-render consumers | consistent |
| 9→10,16 | VoiceParams::weak, GrainStealSink voice arg | consistent |
| 3,4,5,9,10 | Processor.cpp (different functions) | parallel merges need hand resolution; small |
| 7 vs spec EB9 | pan expression term | Plan: "added after toCloudParams with a clamp"; EB9: "joins the voice's pan before toCloudParams' clamp". CONFLICT → ruling R1 |
| 15 | needs 13 (gg-render keys) and 9/10 (budget golden) | consistent |
| Each task self-consistent | | 1: ok; 2: ok; 3: ok (Pan in kVoiceLayerTargets needs 1); 4: ok; 5: ok; 6: RP-053 read first, rulings recorded; 7: see R1; 8: ok; 9: ok; 10: ok; 11: ok; 12: ok; 13: ok; 14: ok; 15: ok; 16: ok; 17: ok; 18: ok |

Ruling R1: the PAN expression's term (note pan + channel pan) joins the voice's Pan slot sum before its clamp (spec EB9), not added after toCloudParams — spec is binding — cost if wrong: differs only when layer Pan + routes already exceed ±1; trivial to move.
Ruling R2: parallel schedule (the plan's "1 to 4 serial" relaxed per the lead brief): wave A = {1, 4, 9}; then 2, 3, 5 after 1; 10 after 9; 6→7 after 5; 8 after 3+4; 14 after 3; 11 after 6+7; 12 after 8; 13 after 6-8; 16 after 9; 15 after all but 14,16; then 17, 18. At most 3 implementers at once (machine shared with SP3, SP4) — cost if wrong: merge conflicts in Processor.cpp, resolved by the controller.

## Tasks
Wave A dispatched (base 471bbc6): Task 1 sonnet agent ac86339ab52866540 in gg-m3e-t1; Task 4 sonnet a967766572258d82b in gg-m3e-t4; Task 9 opus a386a0824e98d6625 in gg-m3e-t9.
Scope addition (top controller's ruling, provisional for the director, 2026-11-07): note routing per layer + envelope curves. Spec §8 (E16-E18), plan Tasks 19, 20 and a Task 1 addendum (committed 'docs(m3): ... scope addition'). Task 1's implementer was sent the addendum mid-task.
Ruling R3: curves are per-layer parameters (Env 1 Curve 0x36, Env 2 Curve 0x37 in the layer range), not globals (0x29+) as the coordinator's note said — the envelopes they shape are per layer (Env 1 Attack etc. are layer params) — cost if wrong: renumber two IDs before any release.
Ruling R4: one curve per envelope (not per stage) with the rational law w(x)=x/(1+k(1-x)), no libm, Sustain preserved via the inverse — the layer range 0x33-0x37 holds exactly the 3 routing + 2 curve params; libm would break cross-platform logs — cost if wrong: per-stage curves need new IDs later.
Ruling R5: routing params (Note Channel 0-18 incl. Lower/Upper zone, Key Low, Key High) are patch params saved in presets, Mutate-locked, no route target; zone choices pass nothing with MPE Off; low>high passes nothing — cost if wrong: small semantic edits.
Ruling R6: all five new params added by Task 1 (one parameter-list change, fewer merge conflicts with SP3/SP4) — cost if wrong: none.
Throttle note applied (top controller): ctest -j 4, build -j 8, full suite once per task, at-rest only for DSP tasks, ≤2 implementers building per sub-project from now on (wave A's three already running were told to build -j 8). Ruling R7: let the three wave-A implementers finish rather than kill one; later waves keep 2 at once — cost if wrong: a little extra load for one wave.
Task 9: implementer DONE_WITH_CONCERNS (2a16075; concerns: row 412 left for Task 17; budget_tests.cpp absent, sinks in engine_tests.cpp); reviewer opus a5912c6ddbd2ccc70 dispatched
Task 9: complete (commits 471bbc6..2a16075, review clean; merged into m3-sp2-expression)
Task 9: minor (deferred → carried into Task 10's dispatch): voiceVictim skips a slot with slotWeight 0 before oldestSoundingOf (cost under overload)
Task 9: minor (deferred → carried into Task 10's dispatch): Processor wiring (params.weak, onSteal's voice forwarding) untested end to end; add a Processor-level case whose steal line carries "v"
Task 9: minor (deferred): room-test fall-through after D4 passes uncovered (mixed tiers)
Task 9: minor (deferred): "one voice" test checks the victim only, not drop placement
Task 9: minor (deferred): carry-over row 412 closes in Task 17
Task 4: implementer DONE (d7f28bf); reviewer sonnet a3a65bf4aae550b72 dispatched
Task 10: dispatched sonnet a74b06c4bd00f6fa5 in gg-m3e-t10 (base 8e3e1f3), with Task 9's minors 1-2 folded in
Task 4: complete (commits 471bbc6..d7f28bf, review clean; merged)
Task 4: minor (deferred): eventRank for params is a function returning 0 while the others are constants (style; Task 8 extends it)
Task 4: minor (deferred → Task 8): no test of a ParamEvent's seq deciding a tie (Task 8's per-note-at-note-on test exercises it)
Task 4: minor (deferred → Task 11): applyBetweenBlocks' numbering untested (Task 11's zero-frame test)
Rule (top controller): no pkill/killall/pattern kills; only own PIDs. Added to constraints.md; sent to live agents.
Task 1: implementer DONE_WITH_CONCERNS (c17d75d incl. addendum; concerns: a pkill -f on own watchers before the rule; ParamTable gains a modulatable field; Pan at Space order 1 shifts Diffusion/Feedback/Level; short label 'Upper Bend'); reviewer sonnet a99967f4e83a13cdf
Task 1: complete (commits 471bbc6..c17d75d, review clean; merged)
Task 1: minor (deferred): isValidTarget's doc comment doesn't name the routing/curve refusal
Task 1: minor (deferred): "Upper Bend" short label is a one-slot special case in ParamPanel.cpp
Task 1: minor (deferred): no direct test of Pan/curve % display text or of kMidiSettingSlots' members
Task 1: process note: implementer ran `pkill -f` on its own watchers before the kill rule arrived (reported to the top controller in the program ledger)
Task 3: dispatched opus aef7336210d2cd3c2 in gg-m3e-t3 (base d4ce9c7)
Slowdown (the director via the top controller): one implementer at a time; sonnet implementers and reviewers (opus only for engine/kernel design-heavy tasks and the final review); short prompts; re-reviews only for Critical/Important.
Ruling R8: Tasks 3 (opus) and 10 (sonnet) were both in flight when the note arrived; both finish (stopping either wastes its work), then everything runs serially — cost if wrong: one overlap period of two builders.
Ruling R9: Tasks 6 and 8 (plan: opus) run on sonnet — they are core, not engine/kernel; escalate on a stuck fix loop — cost if wrong: more fix rounds.
Serial order from here: 2, 5, 6, 7, 8, 14?, 11, 12, 13, 20, 19, 15, 16, 17, 18 (14 after 3; placed where convenient).
Task 10: implementer DONE_WITH_CONCERNS (180cd15; no separate RED run; weak e2e and frees-the-slot tests weakish; another pattern pkill before the rule); reviewer sonnet a0a0138a3745db361
Task 10: complete (commits 8e3e1f3..180cd15, review clean; merged with a test-file conflict resolved, both blocks kept)
Task 10: minor (parked): weak-flag e2e test asserts only released != held; tighten to a count of ordinal 1's steals after release (final review to triage)
Task 10: minor (parked): "frees the slot" test can't tell E10 from V4's cap steal beyond budgetVoiceSteals()==1
Task 10: minor (parked): layerQuotaAt(int64_t) ignores its argument
Task 10: minor (parked): nextGrainWeight reads the requested source, which may differ from the one admit uses within a group
Task 3: implementer DONE_WITH_CONCERNS (ccd708b; Key→Pan checked via new Processor::voiceValue accessor until Task 2; P2 refinement: voices without own env routes follow the layer's current env; voice key refreshed at every update; commitVoices release loop restructured → manual merge with Task 10); reviewer sonnet ac22cdeeb0e3393a0
Task 2: dispatched sonnet af09e8558a80d7c10 in gg-m3e-t2 (base 272937a)
Task 3: review Approved (no Critical/Important).
Ruling R10 (P2 amendment): a voice with no envelope routes of its own follows the layer's current envelope settings at the step (bit-identical to before); a voice with its own envelope routes uses its cached copy (computed at start, release and every update), so a base change reaches it one cell later — deterministic and block-size invariant; keeps at-rest bits — cost if wrong: a one-cell inconsistency between voices on a layer-level envelope automation. Record in Task 17's §23 row with P1-P9.
Task 3: minor (to fix with the merge round): a test pinning R10 (own-env voice's one-cell lag vs ordinary voice; own-env voice block-size invariance)
Task 3: minor (parked): voiceValue's Pitch row returns the layer's pitch (test-only accessor)
Task 3: minor (parked): follow() returns true on a pedal-hold flip (one harmless extra computeVoice)
Task 3: pending merge — conflict with Task 10 in commitVoices; resume Task 3's implementer to merge m3-sp2-expression into m3e-t3 after Task 2 finishes (one implementer at a time)

## Resume 2026-11-09 (the director's go; gate passed, every provisional call confirmed)
Merged origin/main e9a9567 into the branch (one test-file conflict: both blocks kept). Build trees of the moved worktrees deleted and reconfigured as each is used. Slowdown note holds inside the sub-project: one implementer at a time, sonnet, opus for the plan's design-heavy tasks (R9 keeps 6 and 8 on sonnet unless stuck), and the final review.
Task 2: original implementer gone (session restart); uncommitted code in gg-m3e-t2 inspected: complete per the brief, unbuilt evidence. A fresh sonnet implementer verifies, records RED/GREEN and commits it. Task 3: review approved, pending its merge with Task 10 (commitVoices) and the R10 pin test.
Task 2: resumed with a fresh sonnet implementer a7869dc19ea1e7762 in gg-m3e-t2 (base 272937a; backup patch beside the brief)
Task 2: complete (commits 272937a..baeb304, review clean; merged)
Task 2: minor (deferred): the sanitizer's clamp and finiteOr for pan have no direct test (pan 3.0, NaN through setParams)
Task 2: minor (deferred): A Pan's layer test uses Stereo 0, so the add and clamp through the Processor rest on the engine tests
Task 2: minor (deferred): no test of a tiny nonzero pan's subnormal flush
Task 3: merge round dispatched, fresh opus implementer a9f6a184749a78e65 in gg-m3e-t3 (merge m3-sp2-expression at c147a47, R10 pin test); then a scoped re-review
Director's close duties (2026-11-09, resume brief "Added 2026-11-09"): Ruling R11: Task 17's docs/listening/m3-expression/ takes M2's form (read docs/listening/m2-extremes/README.md): a README saying how to render plus specs/ with a handful of gg-render specs (no renders committed), each item naming what to listen for, A/B pairs where a setting changes the sound (Pan off/on, envelope curve -1/0/+1, E9/E10 under load, note routing split, MPE MIDI vs CLAP twin), a few minutes' listening; the §4.10 host-check list stays in the README; listed in the PR body — cost if wrong: a docs edit. No sibling dependency here (2 is the one others need). Crawl timing figures to report: Task 2 55.7 ms under load ~11.
Task 3: merge round implementer DONE (66adc32 merge of c147a47, deafaa0 Key to Pan on the grains, 85ea294 R10 pin tests; full -LE slow 1846/1849, 2 skipped, crawl timing failed under load; at rest 108/0/0; ruling M1 in task-3-report.md "Merge round"). Scoped re-review NOT started (pause).
PAUSED 2026-11-09. RESUME POINT: branch m3-sp2-expression at c147a47 (pushed to origin; no PR yet). Done and merged: Tasks 1, 2, 4, 9, 10 (+ origin/main e9a9567). Not merged: Task 3 on branch m3e-t3, worktree /home/sethbc/.local/share/giga-grains/worktrees/gg-m3e-t3, head 85ea294, clean. Next: scoped re-review (sonnet, fix-loop re-review prompt) of c147a47..85ea294 in m3e-t3 against the Task 3 brief, the merge-round section of task-3-report.md and the implementer's ruling M1 (a voice computed for P2 earlier in a group may then be budget-stolen: one wasted computeVoice, no sound change); if clean, merge m3e-t3 into the branch, mark Task 3 complete, remove the worktree (git worktree remove --force after checking it is clean and merged: it holds submodules). Then serial: 5, 6, 7, 8, 14, 11, 12, 13, 20, 19, 15, 16, 17 (with R11's listening pack), 18; merge origin/main before the final review. Crawl timing figures so far: 55.7 ms (Task 2). Rulings pending: none.
