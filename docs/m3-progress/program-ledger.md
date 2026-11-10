# M3 program ledger (controller), from 2026-11-07

Director 2026-11-07: "keep building all of the sub-projects as parallel as possible and merging them as able to. progress as much as you can without input" — overrides §23 #87 (series) for the rest of M3: record as a §23 row (with SP1's records or the first SP2-4 records).
CLAP index allocation for parallel branches: SP2 global 0x25+, layer 0x32+; SP3 global 0x30+, layer 0x38+; SP4 global 0x38+, layer 0x3C+.
Worktrees: SP2 /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp2-expression (m3-sp2-expression); SP3 /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp3-pitchsync (m3-sp3-pitchsync); SP4 /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp4-tuning (m3-sp4-tuning); all from m3-voice 0885636.
Spec agents dispatched (opus): SP2 a9eaebf083e126ee1, SP3 a64bf84b1e36eab8c, SP4 a13a8977337bcb01c.
Worker sessions (csd) need the director's grant-consent (asked); until then builds run as subagents from the controller.
Merge policy: SP1 (PR #37) to main once gate + final review + CI green; SP2-4 merge m3-voice/main as they go, PR to main each when done.
SP3 spec done: f7e398c on m3-sp3-pitchsync (S1-S14, SB1-SB12; layer index 0x38; notes: INTENT §14 lists pitch-sync as a 4th mode -> amend; §23 numbers at merge; conflicts with SP2 in ParamIds lists and updateVoices; noteSemitones owned by SP4)
SP4 spec done: 469f8c8 on m3-sp4-tuning (T1-T15; global 0x38 MTS-ESP; TUNE chunk; deterministic port of tuning-library mapping; libMTS loaded at runtime, never shipped — flag T10 for the director)
SP1 Task 7 merged 45a4084, pushed.
SP2 spec done: 8f7f0c5 on m3-sp2-expression (E1-E15, EB1-EB13; globals 0x25-0x28, layer 0x32 Pan; E9 per-voice fair steal order may move SP3 budget golden at second merge; E5 closes row 407; E8 sequence numbers close row 417; E14 closes rows 416, 412)
SP3 plan done: 8a84960 (10 tasks; merged m3-voice 3f4eb3f; seam Processor::notePitch; Clock in engine; 64/layer, 294 total)
SP3 lead dispatched (opus, ac8e6fa3d4555bb75) — runs SDD for its plan, draft PR at end; leads have the Agent tool (probe confirmed)
3: Tasks 1, 2, 5 dispatched in parallel (2026-11-07)
SP4 plan done: 2dbbd7f (14 tasks; A1-A13; merged m3-voice 89d9d0e)
Ruling: MTS-ESP is vendored (Client/libMTSClient.h/.cpp + 0BSD LICENSE in libs/mts-esp-client/, SHA-256 guard at configure), not a submodule — the upstream repo holds closed libMTS binaries that must never sit in this AGPL tree — cost if wrong: mechanical switch to a submodule later.
SP4 lead dispatched (opus)
4: lead started; merged m3-voice 18ac47d; rulings R0-R5 in ledger; wave A (Tasks 1, 3, 4) dispatched in parallel
SP2 plan done: 203280e (18 tasks; P1-P9; merged m3-voice bd4e022)
SP2 lead dispatched (opus)
2: lead started; merged m3-voice 18ac47d (471bbc6); ruling R1 (pan term before the clamp, EB9), R2 (parallel waves); wave A (Tasks 1, 4, 9) dispatched in parallel
Competitor research done (scratchpad/research/competitor-gaps.md): top M3 gaps — note routing per layer (INTENT §14 promises M3; program said later: silent divergence), per-voice playhead, envelope curves, scale-quantized grain pitch; M4 row to spell out synced rates, looper transport.
Ruling: note routing per layer and envelope curves join SP2; scale-quantized grain pitch joins SP4; the per-voice playhead becomes new parallel SP3b; M4/M5 rows to be spelled out in the program design — closes the §14 divergence, covers the most common way a granular instrument is played — cost if wrong: scope the director can trim (all marked provisional).
SP3b worktree /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp3b-playhead (m3-sp3b-playhead from m3-voice 18ac47d). CLAP ranges for SP3b: layer 0x68-0x6F (the unused tail after _M2_EXTREMES), global 0x40-0x47.
2: scope addition taken (note routing per layer, envelope curves): spec §8 E16-E18, plan Tasks 19-20 + Task 1 addendum, ddeb5d1 on m3-sp2-expression; layer indices 0x33-0x37 (curves per layer, not globals: ruling R3); 318 params, kLayerParamCount 69, kNextFreeLayerIndex 0x38
3: Task 5 complete (YIN f0 estimator), merged into m3-sp3-pitchsync
3b: spec done: e8e0775 on m3-sp3b-playhead (docs/superpowers/specs/2026-11-07-m3-playhead-design.md; H0-H13, HB1-HB12; layer 0x68-0x6A, no global; 302 params on branch, kLayerParamCount 66)
3b: plan done: 0b399a9 (6 tasks; T1||T2, T3, T4, T5||T6)
3b: build started; Tasks 1 and 2 dispatched in parallel
4: Task 1 complete (vendored MTS-ESP client + SHA-256 guard, tuning-library submodule, notices)
Load 73/32 cores: stopped my stale gate run (rerun after the fix wave, quiet); throttle note sent to all leads (ctest -j 4, full suite once per task, ≤2 implementers per SP, stop watchers); appended to the lead brief. The 'gh pr checks 87 --watch' process belongs to another session (no PR 87 here) — left alone.
Director 2026-11-07: "no i think this all needs to happen in m3" — every competitor gap moves into M3: tempo-synced rates (beyond the grain grid, which stays M4), grain shapes (reverse, percussive windows), per-voice filter, snapshots/scenes, drive/bit-crush/scale-tuned resonator. Plan: five new sub-projects 7-11, specs first, builds staggered as load allows. §23 row with SP1's research records.
New sub-projects (worktrees from m3-voice 18ac47d), CLAP ranges: SP7 tempo sync — global 0x48-0x4F, layer 0x7C-0x7F; SP8 grain shapes — layer 0x70-0x73; SP9 per-voice filter — layer 0x74-0x77; SP10 scenes — global 0x50-0x57; SP11 color FX (drive, crush, resonator) — global 0x58-0x5F, layer 0x78-0x7B.
3b: Task 2 complete (Playhead.h), merged
Concurrent subagent cap (20) hit: SP7 spec NOT started — queue it when a slot frees (prompt as for SP8-11 with globals 0x48-0x4F, layer 0x7C-0x7F, worktree /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp7-temposync). SP8-SP11 spec agents running. Stale waiter notifications from SP1 implementers are noise.
SP8 spec done: d699c1e on m3-sp8-grainshape (G1-G16, GB1-GB17; Reverse 0x70, Skew 0x71; rng::Field::Reverse = 0x70; seams with 3b liveGrain order, SP3 S4, SP2 voice targets union, sameCloud static_assert sum). Plan + lead: when slots free.
Convention: new rng::Field values take the sub-project's CLAP index range start (SP7 0x48+, SP8 0x70, SP9 0x74+, SP10 0x50+, SP11 0x58+; SP2/3/3b/4 pick inside their ranges).
SP7 spec dispatched (opus).
SP9 spec done: c455aea on m3-sp9-voicefilter (F1-F14, FB1-FB17; layer 0x74-0x77; kParamCount 306 on its branch; per-voice lanes in the engine; Off never reroutes; SP2 per-note slot count 92->100 at second merge). Program design §8 + INTENT §20 need rows for 3b and 7-11 (controller, on m3-voice after the fix wave). Plans for SP8, SP9 queued for free slots.
SP10 spec done: 243a44a on m3-sp10-scenes (N1-N15, NB1-NB16; 8 scenes; globals 0x51-0x57 — 0x50 is Budget, my allocation error; checked: all other ranges clear of existing globals 0x01-0x24, 0x50, 0x60-0x67 and layer blocks; SP11's 0x58-0x5F sits in Budget's reserved-block tail, fine with isReservedIndexInUse). Plan queued.
SP11 spec done: 857d0a9 on m3-sp11-color (C1-C15; globals 0x58-0x5F resonator, layer 0x78-0x7B drive/crush; sst-waveshapers pinned dd12f31 (fuzzes excluded: std:: distributions); C13 waveshaper last bits differ across CPUs — accepted under metric tolerances like the reverb; depends on SP4 tuning table and SP2 channel rule at merge). Plan queued.
SP8 and SP9 plans dispatched (opus). SP10, SP11 plans next when slots free.
2: Task 9 complete (steal order in Voice mode, "v" on steal lines), merged
4: Tasks 2 (tuning table, port matches tuning-library on all 561 corpus pairs) and 3 (engine key table) complete
2: Task 4 complete (E8 tie order by host sequence), merged
SP8 plan done: 704dbf7 (15 tasks T0-T14; PD1-PD9). Build (lead) waits for load. SP10 plan dispatched.
SP8 lead dispatched (opus) at load 19.
4: Task 4 complete (MTS-ESP parameter 0x38; 291 params)
Rule added to throttle note and lead brief: never pkill/killall/name-based kills (my shell got SIGTERM'd); sent to all leads. Told 3b lead its Task 1 must run the at-rest check (slot shift).
SP9 plan done: 675eadd (13 tasks; P1-P10). Lead when load allows.
SP7 spec done: ab0b1a4 (R0-R15; layer 0x7C-0x7E, global 0x48-0x49, rng 0x48-0x49; Scan Sync depends on 3b; transport sources' block-size dependence -> carry-over row; SP10 should use its nextLineSample). Plan next.
3b: Task 1 done (4a3509e; at-rest check skipped — told lead to run it).
3: Task 2 complete (Pitch clock in the scheduler). NOTE for sub-projects 2 and 4 and every merge: adding parameter IDs changes the sweep bursts' rendezvous draw; on this branch it exposed row 217's Reverb Size echo past kReverbSizeExempt (bursts fx, 27.6 at 0.68 s after a burst of 4 Size moves). Sub-project 3 widens the exemption to the measured echo end and re-pins floors; a merge that adds IDs may need the same.
3: Task 3 dispatched (held ring anchor)
SP10 plan done: 7d97063 (12 tasks; NP1-NP14). Lead when load allows.
8: build started; Task 0 complete (baselines, at-rest wrapper)
SP9 lead dispatched (opus) at load 15.
9: build started; merged m3-voice b8dd6ac; Task 1 dispatched
4: Task 6 complete (TUNE chunk); Task 5 (Processor tuning) building
2: Task 1 complete (params 0x25-0x28, layer 0x32-0x37; 318 slots, kLayerParamCount 69), merged. Note: SP2's Task 1 implementer ran a 'pkill -f' on its watcher shells before the kill rule arrived — may have hit other sessions' watchers
Director: "can we slow down a bit, usage is going to start being a problem". Done: slowdown note to SP2/3/3b/4 leads (serial, sonnet, short prompts); SP8 and SP9 leads told to pause after the in-flight task and report; SP7/SP11 plans allowed to finish; no new leads (SP7, SP10, SP11 builds queued until the director says go).
SP7 plan done: 42ea09a (14 tasks; P1-P10; merged m3-voice 9f15140). Build queued (director's go).
SP11 plan done: fa0e918 (13 tasks; P1-P7; merged m3-voice 6f4ff30). Build queued (director's go).
3b: Task 1 complete (the three parameters), merged
8: Task 1 complete (scalar paths at negative rates, 2405bf9)
3: Task 1 complete (Clock param 0x38; bursts leave Reverb Size out of their draw — see note above). Tasks 1, 2, 5 merged; Task 3 in flight.
3b: Task 3 complete (engine playhead), merged 80beacf
2: Task 10 complete (E10 steal by budget), merged
2: Task 10 complete (E10 steal by budget), merged 272937a
9: Task 3 complete (VoiceFilterBank, BusFilter's law bit for bit), merged 23de95e
4: Task 5 complete (Processor tuning; at rest 108 unchanged, voice goldens unchanged); serial from here per the slowdown note
3: Task 3 complete (held ring anchor); Task 4 next, serial
9: Task 1 complete (schedule harness; at rest 108 + 8, schedules 0 changed). PAUSED per coordinator at b992efc; resume at Task 2.
SP9 PAUSED: Tasks 1, 3 of 13 merged on m3-sp9-voicefilter at b992efc; resume per its ledger RESUME POINT (merge m3-voice/main, Task 2 sonnet, then Task 4 opus with R6). inst-voice-expr == inst-voice-mpe WAV hash is by design (B7).
8: Task 4 complete (the window's Skew, merged 04faa97). PAUSED at the director's usage request: T0, T1, T4 done; resume at T2 || T5 (ledger /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp8-grainshape/.superpowers/sdd/2026-11-07-m3-grainshape/progress.md).
SP8 PAUSED: T0, T1, T4 of 15 on m3-sp8-grainshape at 04faa97; resume T2+T5 (opus) per its ledger. Its R8: G9 wording correction at T13.
3b: Task 4 complete (Processor: law, per-voice speed, Scan Track)
Director 2026-11-08: all seven superpowers workflows are standard, including on resume; gates per sub-project: docs/m3-progress/briefs/STANDARD-WORKFLOW.md (binding). Provisional-call review before more building on 2, 3, 3b, 4, 8, 9; full brainstorming for 7, 10, 11, 5, 6 and anything new. A lead starts only after its "gate passed" line here. Nothing restarted.
Director 2026-11-08: draft PR #38 (branch worktree-at-rest-and-agents-md) carries scripts/check-at-rest.sh with its baseline, the rename of CLAUDE.md to AGENTS.md and the mandatory-workflow rules (INTENT §23 #95). All M3 worktrees moved to ~/.local/share/giga-grains/worktrees/; this ledger moved to the main checkout. Nothing restarted.
2: gate passed 2026-11-09 (provisional-call review; every call confirmed; docs/m3-progress/call-review.md)
3: gate passed 2026-11-09 (provisional-call review; every call confirmed; docs/m3-progress/call-review.md)
3b: gate passed 2026-11-09 (provisional-call review; every call confirmed but H13, amended: tempo-locked scanning is sub-project 7's; docs/m3-progress/call-review.md)
4: gate passed 2026-11-09 (provisional-call review; every call confirmed; docs/m3-progress/call-review.md)
8: gate passed 2026-11-09 (provisional-call review; every call confirmed; docs/m3-progress/call-review.md)
9: gate passed 2026-11-09 (provisional-call review; every call confirmed; docs/m3-progress/call-review.md)
7: gate passed 2026-11-09 (brainstorming; R3 overruled: a Sync switch per modulator per layer, layer indices 0x6B to 0x6F; every other call confirmed; docs/m3-progress/call-review.md)
10: gate passed 2026-11-09 (brainstorming; every call confirmed; docs/m3-progress/call-review.md)
11: gate passed 2026-11-09 (brainstorming; every call confirmed; docs/m3-progress/call-review.md)
Director 2026-11-09: 7, 10 and 11 start now, in parallel with the six. 5 builds after everything else lands; 5 and 6 get specs from the brainstorming (m3-call-review.md). A listening pack per sub-project as each lands.
Director 2026-11-09: row 403 is the runner's fault (probe run 42687277007: float compares misfire under Rosetta, no product code): close it; the Rosetta step re-runs failed cases once and lists them (design pending the director's approval). Go given: finish and merge PR #37 first, then resume 2, 3, 3b, 4, 8 and 9 in parallel, one implementer each (lifts the slowdown note's serial rule for these six).
4: resumed 2026-11-09; Task 6 verified on the branch; origin/main merged 4959191; Task 7's uncommitted work committed on m3t-t7 and being finished
3: Task 4 complete (d8ddedf), resumed 2026-11-09
4: Task 7 complete (gg-render tuning, packer, --save-state)
2: Task 2 complete (Pan's sound), merged c147a47
3b: resumed 2026-11-09; H13 amended (f60cffa); Task 5 committed 4c8c7e6, in review
3b: Task 5 complete (seven goldens, end-to-end tests) 3a1c582
3b: Task 6 complete (records, §23 #97); final review next
10: started 2026-11-09; origin/main merged 49247b2; plan confirmed and amended f8a7a14 (writing-plans); Task 1 dispatched
11: resumed 2026-11-09; origin/main merged 8c35af2; plan confirmed and amended c367843 (siblings' seams last as Task 13); Task 1 dispatched
7: started 2026-11-09; origin/main merged; spec R3 amended (75b9238), plan amended (8e43e50): Sync switch per modulator at layer 0x6B-0x6F, Loop Sync 0x7D, Scan Sync 0x7E (0x7C unused); Task 1 dispatched
11: Task 1 complete (sst-waveshapers pinned dd12f31, ec9acc9)
11: Task 2 complete (HalfBand2x, bit for bit with HalfRateFilter, 82c834f)
Director 2026-11-09: approved the designs of 5 and 6; merged as PR #39 (a24c559). 5's plan comes when every other sub-project has landed; 6's after 5.
9: PAUSED 2026-11-09 at d196ce2; next Task 2's review (sonnet), then Task 4 (opus, R6)
4: PAUSED 2026-11-09 at d2aa3d2; next Task 8's review (implemented, committed d2aa3d2), then T15
8: PAUSED 2026-11-09 at 58b38a0; next Task 2's review (implemented, unreviewed), then T5
rosetta-retry: PAUSED 2026-11-09 at da7a8ae; next final-review fix wave (final-review.md), then draft PR
11: PAUSED 2026-11-09 at 014c65f; next Task 3 (Drive, wip: rulings pending on C3's level-match bounds at d = 1 and shape-change click scores; see sp11 ledger)
2: PAUSED 2026-11-09 at c147a47; next Task 3's scoped re-review (m3e-t3 at 85ea294, unmerged), then Task 5
3: PAUSED 2026-11-09 at b2a48b2; next Task 6 review (committed, unreviewed), then Tasks 7-10
7: PAUSED 2026-11-09 at 52d0085; next Task 1's review (Task 1 committed, unreviewed), then Task 2
3b: PAUSED 2026-11-09 at dea0be1; next the fix wave's scoped re-review, then Task 7 (listening pack), owed checks, draft PR
10: PAUSED 2026-11-09 at bcc6cc7; next Task 1's review (implemented, unreviewed), then Task 2
