# SDD ledger — plan: /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp3b-playhead/docs/superpowers/plans/2026-11-07-m3-playhead.md
**Before resuming (the director, 2026-11-08):** read docs/m3-progress/briefs/STANDARD-WORKFLOW.md, which is binding, and check this sub-project's gate in m3-program-ledger.md.

Spec: docs/superpowers/specs/2026-11-07-m3-playhead-design.md (859db5f, e8e0775). Plan: 0b399a9. Branch m3-sp3b-playhead, base 0b399a9 (m3-voice 18ac47d merged, up to date at start).
Voice-golden baseline: voice-baseline-wav-sha256.txt in this workspace (taken at 0b399a9).

## Pre-flight scan
| Pair / task | Shared file or interface | Finding |
|---|---|---|
| T1-T4 | ParamMap.h/.cpp: T1 adds scanSpeedOf + text helpers; T4 edits toCloudParams | consistent (T4 consumes scanSpeedOf) |
| T2-T3 | Playhead/ScanGeometry/ScanEdge/scanFold; T3's ScanPosition holds a Playhead copy, needs edge() and offsetAt() | consistent |
| T3-T4 | CloudParams::scanSpeed/scanEdge; ScanEdge enum from engine's Playhead.h used in core | consistent (core already includes engine headers) |
| T1-T2 | none (T2 adds tests/CMakeLists entry, T1 doesn't touch it) | parallel-safe |
| T4-T5 | tests/core/playhead_processor_tests.cpp (T4 creates, T5 adds a case) | sequential, fine |
| T5-T6 | T5 tests/ only; T6 docs/INTENT/CLAUDE.md | parallel-safe |
| T1 self | placement text says "Scan Speed at index 3 with ScanEdge as pair" and "right after Position and Spray"; Spray is order 3 and EditLayout's 4th arg is a *spread* pairing | conflict -> Ruling R1 |
| T2 self | tests vs interface agree | clean |
| T3 self | "smear follows" test leaves the method to the implementer | accepted (stated in task) |
| T4 self | clean | clean |
| T5 self | closed-form test assumes R = 48000 sample; fine | clean |
| T6 self | clean | clean |

Ruling R1: Edit placement: Scan Speed order 4, Scan Edge 5, Scan Track 6 in Panel::Grain, no spread argument; Texture, Smear, Quality move to 7, 8, 9 — the 4th argument means a spread, which Scan Edge isn't; "after Position and Spray" is the plan's intent — cost if wrong: one-line layout move (sub-project 5 may move them anyway).
Ruling R2: Tasks 1 and 2 run in parallel in worktrees gg-m3pb-t1 / gg-m3pb-t2 (branches m3pb-t1/m3pb-t2 off m3-sp3b-playhead); 5 and 6 likewise — the plan marks them independent — cost if wrong: a merge conflict resolved by hand.
Task 1: dispatched (sonnet, a969ede743d6a5145) in gg-m3pb-t1, BASE 0b399a9
Task 2: dispatched (opus, abfb9cb1fbfea05a2) in gg-m3pb-t2, BASE 0b399a9
Task 2: implementer DONE_WITH_CONCERNS fd9ea8b (concerns: rebase landing on x0=0 reads plain Position, per spec; sync must run every block start, per HB1 — both accepted as spec behavior). Reviewer dispatched (sonnet, ad5d032406de0bfd0).
Task 2 review: Approved (sonnet), 0 Critical/Important, 4 Minor. Minor 1 (±0 rate rebase under Stop breaks x=0 at rest) sent back to implementer; minors 2-3 (untested guards, overflow at L>9e307) deferred: theoretical; minor 4 doc note folded into the fix.
Ruling R3: throttle note applied (ctest -j 4, full suite once per task, at-rest only for DSP/render tasks, <=2 implementers at once) — top controller's instruction — cost: slower tasks.
Task 2: complete (commits 0b399a9..6907bff, review clean after minor fix; merged 58cc3d4)
Task 3: dispatched (opus, a612507528b0a7bf8) in gg-m3pb-t3 (m3pb-t3), BASE 58cc3d4
Ruling R4: the bench before/after comparison (HB12) is deferred to the final review; Task 3 reports its after-change quick ratio only — under load 73 the ratios are noise and a second base build costs everyone — cost if wrong: a hot-path regression found one step later.
Ruling R5: voice-golden baseline taken from m3-voice's build/dev/golden/*voice*.wav (rendered 08:31, after the last code commit 08:24; this branch's base differs from 18ac47d by docs only), as voice-baseline-wav-sha256.txt; the branch-start at-rest run was killed (exit 144) by the load shedding — cost if wrong: a false voice-golden alarm, checked against a fresh render.
Ruling R6 (coordinator): no name-based kills; parameter tasks run the at-rest check — Task 1 told to run it.
Task 3: implementer DONE_WITH_CONCERNS d365c1b. Deviations accepted: HB9 restart in process()'s mode-change branch (applySourceMode also runs on ring resize); double overload of SpectralSource::process kept as forwarder. Full suite: only the load-sensitive crawl48 timing case failed (load). Ruling R7: canon of x == 0.0 is 0.0 for every edge (an unmoved playhead stays today's read under edge/window changes) — fixes concern 1, which broke 'Scan 0 is today's read' — cost if wrong: none musically (Stop holding an unmoved point at Position outside the window is today's behavior). Concern 2 accepted (a note was played). Fix sent to implementer before review.
Task 1: implementer DONE 4a3509e; at-rest 108/0/0, voice WAVs match; crawl48 timing failure from load. Reviewer dispatched (sonnet, aeb3f940f2f8cee01).
Task 1 review: Approved with 1 Important (editor typed entry units) + minors (underflow, digit count, stale comments, UI text path untested, macro formatting). Fix round 1 sent: the Important + underflow + digit count + stale comments (CLAUDE.md: controller folds minors). Task 1: minor (deferred): GG_LAYER_PARAMS macro line breaks are clang-format output (cosmetic).
Task 3: R7 fix bb9d89b (canon of 0 is 0; test b widened); at-rest 108/0/0 and voice WAVs match again.
Task 3: reviewer dispatched (opus, a2e1eb235dca64f72) over 58cc3d4..bb9d89b
Ruling R8 (director via top controller, slow down for usage): one implementer at a time; Tasks 4, 5, 6 serial on sonnet (Task 4 was opus in the plan; it's core, not engine/kernel); task reviewers sonnet; opus only for the final review; re-reviews only for Critical/Important fixes; minors fixed in-round or parked. Supersedes R2's 5||6 — cost: slower, cheaper.
Task 3 review: Approved (opus), 0 Critical/Important. Minors: (1) no test pins forceNextOnset's restart (enable on, Voice->Cloud); (2) sanitization untested; (3) Spectral material kind change (sample loaded/removed) switches units without restart; (4) Engine.h:166 formula stale, spec HB9 names applySourceMode; (5) sizeof(CloudParams)==136 assert conflicts at merge with pitch-sync (take the sum).
Ruling R9: Spectral restarts the playhead when its material kind changes (sample <-> live ring) — HB9's own rationale (units don't correspond) — cost if wrong: a scan restarts at a sample load in Spectral mode, which is what a load does musically anyway.
Plan: minors 1-4 (code side) to the Task 3 implementer after Task 1's fix lands (one implementer at a time); no re-review (minors); spec HB9 wording + R9 recorded in Task 6. Minor 5: merge note for the final report.
Task 1: fix round 1/5 (4 addressed, 0 open; 4a3509e..a0a29be). Task 1: minor (deferred): ParamControl.cpp uses std::atof without <cstdlib>; panels_tests.cpp blank line before new TEST_CASE.
Task 1: complete (commits 0b399a9..a0a29be, review clean; merged 6b278e1)
Task 3: minors folded b681160 (forced-onset restart tests, sanitize test, R9 Spectral material restart, Engine.h doc); at-rest 108/0/0, voice WAVs match.
Task 3: complete (commits 58cc3d4..b681160, review clean; merged 80beacf; merged branch builds, -L playhead|cloud passes)
Task 4: dispatched (sonnet, a29118aba01ac2f02) in the branch worktree, BASE 80beacf
Task 4: implementer DONE_WITH_CONCERNS 717fa06 (crawl48 load; Track bend case uses 1.99976 st, the exact MPE-off max — accepted); at-rest 108/0/0, voice WAVs match.
Task 4 review: Approved (sonnet), 0 Critical/Important. Minors 1-4 (shared ±64 constant with engine's kMaxScanSpeed; tests: Track ignored on Cloud, Track toggled on a sounding voice, ±64 clamp at key 127) folded into Task 5's dispatch. Minors 5-7 deferred (adequate as is).
Task 4: complete (commits 80beacf..717fa06, review clean)
Task 5: dispatched (sonnet, ab33c0b5663fa82f9), BASE 717fa06, with Task 4's minors 1-4
Resumed 2026-11-09 (gate passed in the program ledger; every call confirmed but H13, amended). Spec and plan amended in a docs commit (H13: Scan Sync, sub-project 7). Build tree recreated.
Ruling R10: Task 5's implementer is a fresh one (the old agent died with the session), finishing the uncommitted work at 717fa06; origin/main is merged after Task 5 commits (the brief: never over uncommitted work), and the 7 new goldens are re-checked then — cost if wrong: one regeneration of those 14 files.
Task 5: re-dispatched (sonnet, a78cddb77449a55f9) in the branch worktree, BASE f60cffa, finishing the earlier agent's uncommitted work
Task 5: implementer DONE_WITH_CONCERNS 4c8c7e6; at-rest 108/0/0, the 8 older voice goldens match. Reviewer dispatched (sonnet, a0d7e8c81fb4ed48b).
Ruling: the crawl48 timing failure isn't this branch's — measured alone at load 10, sub-project 1's main-state build (b8dd6ac) fails it too at 52.1-53.1 ms; this branch 54.6 ms (the three parameters' share of about 2 ms); the bound is met only on faster machines — cost if wrong: a slow crawl found on the director's machine.
Task 5 review: Needs fixes (sonnet), 2 Important (INIT half vacuous; closed form skips the golden's spec), RED evidence thin, 2 minors folded in; fix round 1/5 sent to a78cddb77449a55f9, FIX_BASE 4c8c7e6
Task 5: fix round 1/5 (5 addressed, 0 open; 4c8c7e6..3a1c582)
Task 5: complete (commits f60cffa..3a1c582, review clean)
Merged origin/main (e9a9567) as cc9c974; merged tree builds, -L golden -LE slow passes (the 7 new cases unchanged). Task 6: dispatched (sonnet), BASE cc9c974
Task 6: implementer DONE d0534f9; reviewer next
Task 6 review: Approved with 1 Important (INTENT §19's golden totals) + minors; fix round 1/5 sent, FIX_BASE d0534f9
HB12 measured (R4's deferred check): X ratio main vs branch, 4 alternating --quick runs: +0.62% (32), +0.53% (64), +0.14% (256), within or about the run-to-run spread; accepted.
Task 6: fix round 1/5 (5 addressed, 0 open; d0534f9..cc904de)
Task 6: complete (commits cc9c974..cc904de, review clean)
Merged origin/main again (afb1562) as 62e1acb; §23 row renumbered #98 (main took #97). Final review next
Final review (opus): With fixes. 3 Important, 4 Minor, deferred-minor triage. Rulings below go to one fix agent; one scoped re-review after.
Ruling R11: a Scan Edge change refolds the point under the old edge before the new edge's canon, so the scan continues from where it is — H5's piecewise-linear point; HB1 is silent on it; a stepped edge mustn't teleport — cost if wrong: two new goldens regenerated (inst-scan-edges, fx-scan-ring), HB1 note, undone by one line.
Ruling R12: a state or preset load restarts every playhead (Cloud and voices) on the audio thread, in the load's own block, after setBases — H11's "a session starts at Position" extended to a preset load on a running instance; HB10's "Scan 0 holds" is a speed gesture, not a load — cost if wrong: a load mid-scan returns to Position, which is what a load does musically anyway.
Ruling R13: Stop at a Sample's upper edge holds the point where a grain still fits (start clamped to span - dur x rate, per grain), only when the playhead has moved — H3 says Stop holds, and H3 declined an edge that ends the voice; backward Stop already sustains — cost if wrong: a one-shot's end sustains instead of going silent; the director can flip it by ear.
Ruling R14: final review's Minor 4 (Wrap on a fresh ring reads unwritten frames, once per activation) goes to carry-over: bounding the fold by the written span makes the window a function of t, against HB1's "the window is never a rebase trigger", for a first-seconds-only effect — cost if wrong: the FX's first pass is partly silent until the ring fills, as any long Position is today.
Ruling R15: Minor 7 (sub-project 7 undefined at this head): no change here — the program design's rows for 7 to 11 are the top controller's (M3-RESUME "still to record") — cost if wrong: a reader follows the pointer to the m3-sp7-temposync branch.
Merged tree before fixes: at-rest 108/0/0; ctest -LE slow all pass but crawl48 (main's too); 2 skips. Final fix wave dispatched (opus), FIX_BASE 62e1acb.
Director 2026-11-09 (via coordinator): a listening pack docs/listening/m3-playhead/ before finishing — Task 7 (added), after the fix wave; crawl48 figure goes in the final report (54.6 ms).
Final fix wave: implementer DONE (paused mid-wave, committed) 6c35910, dea0be1; report final-fix-report.md. Full suite not run (stopped at 51/1867, 0 failed).
PAUSED 2026-11-09. RESUME POINT: branch m3-sp3b-playhead at dea0be1, pushed to origin (no PR yet); no task worktrees open (gg-m3pb-benchbase removed). Done: Tasks 1-6 complete; origin/main merged twice (latest afb1562, as 62e1acb; §23 row renumbered #98, main took #97); final review (opus) done; its single fix wave committed (R11 edge-change refold, R12 load restarts playheads, R13 Stop holds at a Sample's end, R14 carry-over row, minors). Next, in order: (1) the fix wave's ONE scoped re-review (sonnet, re-review-prompt.md, FIX_BASE 62e1acb, HEAD dea0be1, findings = final review's Important 1-3 + minors 5, 6, atof, R14 row); adjudicate residuals per final-review.md, no second wave; (2) Task 7, the listening pack (brief task-7-brief.md, added by the director 2026-11-09), then its review; (3) merge origin/main again; (4) owed checks: ctest --preset dev -j 4 -LE slow once, the at-rest check (108/0/0) and voice goldens, the asan build and asan tests, clang-format; (5) superpowers:finishing-a-development-branch: draft PR to main, body lists the listening pack, the confirmed calls, rulings R1-R15; watch CI (GCC 13, RTSan in CI only). Watch: fx-scan-ring's step 13 click score 15.11 against limit 15 (its pinned count could differ on another platform); crawl48 reads 54.6 ms here (main's state 52-53 ms) — report the figure; sizeof(CloudParams)==136 assert conflicts with pitch-sync at merge (take the sum).
