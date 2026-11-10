# M3 resume notes (written 2026-11-07, when the director stopped all agents to restart the session)

Read this first, then the program ledger (`docs/m3-progress/program-ledger.md` beside this file) and each
sub-project's own SDD ledger (`<worktree>/.superpowers/sdd/<plan-name>/progress.md`).

## LATEST: the orderly pause of 2026-11-09 (supersedes the older sections below where they disagree)

The director asked to pause all work in an orderly way. Every lead was told to follow
`docs/m3-progress/briefs/pause-2026-11-09.md`: let in-flight work reach a committed point, start nothing new, write
a RESUME POINT in its own ledger and a `PAUSED` line in the program ledger, push its branch.
**Nothing restarts without the director's go.**

**What happened on 2026-11-09 (all recorded):**
- Sub-project 1 merged (PR #37, `e9a9567`); PR #38 merged (`afb1562`: `AGENTS.md` replaces `CLAUDE.md`, `scripts/check-at-rest.sh`, INTENT §23 #97); PR #39 merged (`a24c559`: the designs of 5 and 6).
- Row 403 ruled the CI runner's fault (probe run 42687277007). The retry's design and plan are approved (branch `rosetta-retry`), and its build was under way at the pause.
- The director's reviews and brainstorming: `docs/m3-progress/call-review.md`. Gates passed for 2, 3, 3b, 4, 7, 8, 9, 10, 11 (program ledger). 7's R3 was overruled (a Sync switch per modulator). 5 and 6 have approved specs on `main`.
- Leads ran in parallel from `docs/m3-progress/briefs/resume-2026-11-09.md` (read its "Added 2026-11-09" too: the listening pack per sub-project, the sibling rule, the crawl figure).

**To resume** (on the director's go): for each paused lead, a fresh lead (opus) gets the resume
brief, its sub-project's worktree and ledger, and the line "resume from your ledger's PAUSED RESUME
POINT". The per-lead state at the pause is in the table below and in the program ledger's `PAUSED`
lines.

**After the retry merges:** delete the throwaway branch `row403-probe` (local and remote); the
worktree `gg-m3v-row403` is checked out on it.

**INTENT §23 numbers:** 3b's branch and the retry's branch both took #98. Whichever merges second renumbers (and every branch renumbers its row at merge, per the lead brief).

**Later:** 5's plan when every other sub-project has landed; 6's after 5. The crawl test reads
53 to 57 ms on loaded branches (the close times it idle).

### State at the pause (filled from the leads' reports)

| Lead | Worktree | Head at pause | Resume point |
|---|---|---|---|
| 2 Expression | gg-m3-sp2-expression (+ task worktree gg-m3e-t3) | `c147a47` (pushed; main merged at `e9a9567`) | Tasks 1, 2, 4, 9, 10 merged (Task 2 finished from gg-m3e-t2's uncommitted work, `baeb304`; that worktree removed). Task 3 built, not reviewed or merged: branch `m3e-t3` at `85ea294`, worktree gg-m3e-t3, pushed. Next: scoped re-review of `c147a47..85ea294`, merge Task 3; then serially 5, 6, 7, 8, 14, 11, 12, 13, 20, 19, 15, 16, 17, 18. Listening pack in Task 17 (R11). Crawl 55.7 ms |
| 3 Pitch-sync | gg-m3-sp3-pitchsync | `b2a48b2` (pushed, first push; main merged at `e6961aa`) | Task 4 merged (`d8ddedf`, reviewed clean; the 97 golden files were at-rest noise, restored). Task 6 (seven goldens) committed, unreviewed. Next: review Task 6; Tasks 7 to 10 (Task 9 writes the listening pack); merge main; final review; draft PR. Ruling for the director: `inst-pitchsync-periods`' first step 18.75 ms, not 5 ms (5 ms scores 27.9 clicks on the grains themselves; Size's 5 ms floor unchanged, a unit test covers it). Crawl 56.6 ms, failing alone at load 2 (52.4 ms on its base earlier) |
| 3b Playhead | gg-m3-sp3b-playhead | `dea0be1` (pushed, first push; main merged at `afb1562`) | Tasks 1 to 6 reviewed clean; H13 amended (`f60cffa`); the final review's three Importants fixed (`6c35910`, `dea0be1`: R11 Edge change no jump, R12 a load restarts the playheads, R13 Stop holds at a sample's end). Next: scoped re-review of the fix wave; Task 7 (listening pack, `task-7-brief.md`); merge main; owed full suite (stopped at 51 of 1867), asan, CI; draft PR. For the director's ears: R12, R13. Watch: `fx-scan-ring` step 13 at 15.11 clicks; pitch-sync's merge conflicts on `sizeof(CloudParams) == 136` (take the sum). X +0.62% at block 32. Crawl 54.6 ms (main 52 to 53 here) |
| 4 Microtuning | gg-m3-sp4-tuning | `d2aa3d2` (pushed; main merged at `4959191`) | Review Task 8 (`8e90d5e..d2aa3d2`); then T15, T9, T10, T11, T12, T13, T14 serially. Tasks 6 and 7 merged. Rulings R11 (libMTS fetched by SHA-256 outside the tree), R13 (tasks in the sub-project worktree), R14 (listening pack in Task 14). Crawl 54.8 ms |
| 7 Tempo sync | gg-m3-sp7-temposync | `52d0085` (pushed, first push) | R3 amended in the spec (`75b9238`: five Off/On Sync switches a layer at 0x6B to 0x6F; 0x7C unused; 69 a layer, 316 in all, 70 and 320 with Scan Sync); plan amended (`8e43e50`; Task 13 gains the listening pack, Task 14 waits for 3b). Task 1 (sync clock) committed, unreviewed. Next: merge main; review Task 1 (sonnet), checking its seven interpretations (llround via `std::floor`; a non-finite host beat as no timeline); Task 2 |
| 8 Grain shapes | gg-m3-sp8-grainshape | `58b38a0` (pushed; main merged at `295d46d`) | Review Task 2 (`295d46d..58b38a0`, opus); then T5, T3, T6, T8, T7, T9 to T14 serially. R11 (listening pack form), R12 (§23 wording), R13 (no sibling dependency). Crawl 53.6 ms |
| Rosetta retry | gg-rosetta-retry | `da7a8ae` (pushed; no PR, no CI yet) | All 3 tasks built and reviewed; the final review (opus) is "with fixes", 9 Minors (`final-review.md` beside its ledger), two of them record wordings to fix before merge. Next: one fix dispatch, a scoped re-review, the draft PR and CI. For the director: prove the retry on the real runner first (a scratch case failing once, one failing twice)? Raise the step's 20-minute limit? INTENT row #98 |
| 10 Scenes | gg-m3-sp10-scenes | `bcc6cc7` (pushed, first push; main merged at `49247b2`) | Plan amended for the merged main (`f8a7a14`). Task 1 (the seven scene controls, the FX view's Scenes panel) committed, unreviewed. Next: merge main (if a sibling landed, update the pinned scene-slot table and counts on purpose, spec §7); review Task 1 (sonnet, `f8a7a14..bcc6cc7`); Task 2. Rulings: the spec's slot table is the source of truth; the gate's steps run by hand at `-j 4`; Task 6 adds its own transport accessors. Its records also took §23 #98 (renumber at merge). Crawl 50.84 ms |
| 11 Drive/crush/resonator | gg-m3-sp11-color | `014c65f` wip (pushed, first push; main merged at `8c35af2`) | Tasks 1 (sst-waveshapers `dd12f31`) and 2 (2x half-band) reviewed clean. Plan amended (`c367843`): sibling work moved to a new Task 13, gate is Task 14. Task 3 (Drive) is wip: 10 of 12 tests pass. **Needs the director before it finishes**, at Drive 100%: peaks Medium +0.52, Hard +0.76, OJD +0.70 dB against +/-0.5; RMS Rectify -15.9 dB (clipped to near-DC, which the blocker removes), Fold +11.1, Sine Fold +9.1 against +/-9 (the 8 probes miss the folds' peaks); a shape-change click 18.49 against 15 (cause not yet found). Loosen the bounds, change the probe set, or revisit C3 |
| 9 Per-voice filter | gg-m3-sp9-voicefilter | `d196ce2` (pushed; main merged at `91190e9`) | Review Task 2 (`91190e9..d196ce2`, sonnet); then Task 4 (opus) with R6's realtime annotations; Tasks 5 to 13 (briefs written). Listening pack in Task 12 (R9). Crawl read 55.4 and 55.8 ms alone |

## READ FIRST: the standard workflow and its gates (the director, 2026-11-08)
**`docs/m3-progress/briefs/STANDARD-WORKFLOW.md` is binding and overrides anything below that
disagrees.** All seven superpowers skills are standard, including on resume: brainstorming,
using-git-worktrees, writing-plans, subagent-driven-development, test-driven-development,
requesting-code-review, finishing-a-development-branch. Point every lead and implementer at it.
Nothing restarts until the director says go, and then each sub-project also has to pass its gate:
- 1 Voice: no gate; row 403, then requesting-code-review, then finishing.
- 2, 3, 3b, 4, 8, 9: go through the spec's provisional calls with the director before any more
  building.
- 7, 10, 11: full brainstorming with the director (existing spec and plan as input), then
  writing-plans, before the build starts.
- 5, 6 and anything new: full brainstorming before any spec.

**Moved 2026-11-08 (the director):** every M3 worktree is now in
`/home/sethbc/.local/share/giga-grains/worktrees/`, never `~/src`, and these notes are in the main
checkout's ignored `.superpowers/sdd/`. Uncommitted work moved intact. Each moved tree's `build/` is
stale: delete it and run `scripts/worktree-setup.sh --configure dev` on resume. Details are in the
standard workflow's "Where things live".

## How M3 is being run
- The director approved the M3 program design (PR #36, on `main`) and asked for all sub-projects
  "as parallel as possible and merging them as able to. progress as much as you can without input",
  then "can we slow down a bit, usage is going to start being a problem" (serial within each
  sub-project, sonnet for implementers and reviewers, opus only for hard engine tasks and final
  reviews; no new builds started until the director says go).
- The director moved every competitor gap (Torso S-4, Tasty Chips GR-1/GR-MEGA/GR-2; notes in the
  session scratchpad `research/competitor-gaps.md`, copy it into `docs/research/` when recording)
  into M3: sub-projects 3b and 7-11 below. Still to record: program design §8 rows for 3b and 7-11,
  INTENT §20's M3 row, a §23 row for that decision, and the research doc — do it on `m3-voice` or a
  docs PR once sub-project 1 lands.
- Until 2026-11-08 (now see the standard workflow above) each sub-project ran: spec → plan → subagent-driven build (a lead agent per sub-project, opus, using
  superpowers:subagent-driven-development) → draft PR to `main`. Briefs the leads followed (session
  scratchpad, may be gone after restart): spec-brief-common.md, plan-brief-common.md,
  lead-brief-common.md, throttle-note.md, slowdown-note.md. Their key rules: the at-rest check
  `.superpowers/sdd/2026-11-07-m3-voice/check-wav-hashes.sh <worktree>` (108 M2-era golden WAV
  hashes; restores only those cases' expected files); stage files by name; never commit regenerated
  expected files of existing cases (Linux metrics differ in the last digit); ccache flags on new
  trees; clang-format at `~/.local/share/giga-grains/format-venv/bin/clang-format`; `ctest -j 4`;
  never pkill/killall by name; CLAP index ranges per sub-project (below); new rng::Field values at the
  start of each sub-project's range.

## Sub-project 1 (Voice slice): branch `m3-voice`, draft PR #37 — nearly done
- All 13 tasks merged; the final whole-branch review's fix wave merged (b8dd6ac); scoped re-review
  clean; local: dev 1804/1805 (crawl48 timing fails only under load), at-rest 108/0/0, pushed.
- CI at b8dd6ac: RTSan and ASan pass; **mac-universal fails** in the Rosetta x86_64 shard 3:
  `tests/plugin/persist_tests.cpp:2124` (the ring-reader stress test, carry-over row 403), twice in a
  row (runs 42381691057, 42390128189). In both, the "bad" frame read the expected value
  (949/949, 83667/83667), so suspect the test's lap classification, not the reader. A debugging
  agent was started in worktree `/home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-row403` (branch m3v-row403, from b8dd6ac;
  no commits yet) and stopped. **Next (on the director's go):** redo that debugging (systematic-debugging), fix with
  test-driven-development, requesting-code-review for the fix, merge into m3-voice, push, see CI
  green, then finishing-a-development-branch (full suite first; PR #37 is its option 2 already), mark
  PR #37 ready and merge to main (the director authorized merging as able). Parked minors to add as carry-over rows with the records: the Cloud-choke wording, stale
  NoteEvents.h comments, choke-then-full-table-replacement, missing multi-block/[rt] choke tests; plus
  the row 403 occurrences.

### Row 403 findings (systematic-debugging Phase 1, 2026-11-09; no code changed, gg-m3v-row403 still clean)
- Correction to the above: the two persist failures are at different heads (run 42381691057 at 18ac47d, run 42390128189 at b8dd6ac).
- Signature: the first scan's compare says mismatch, but found == expected, a volatile re-read == expected, and a rescan of the same copy counts 0 bad frames. `classifyMismatch`'s "newer lap" label is wrong (d == 0 passes its `d % capacity == 0`), but only names the class; `bad` comes from the compare. The diagnostic has a gap: `found` is loaded after the compare, so it can't show what the compare saw.
- The copy is private: `ringToBuffer` makes, fills and returns it on the test's thread (`readWith`'s copy step runs inline). No other thread holds it.
- Same family as row 234: 8 of the last 17 finished CI runs failed in the Rosetta step's "shard N of 3", across unrelated tests: persist 2124 (2), kernel 2477 "live interior" (2; one sample, "evaluation 1 ok ... evaluation 2 ok"), kernel 1543 "simd sample kernel" (2), determinism 1173 (2). The kernel tests are single-threaded. Native arm64, Linux, Windows and the sanitizers never show it; the runner is GitHub's M1 VM, and the M5 Max Rosetta runs never reproduced it. This Linux box can't reproduce it either.
- The director chose "probe, then re-run" (2026-11-09). Running: PR #37's failed job re-run (run 42390128189, attempt 2) and probe run 42687277007 on branch `row403-probe` (fae0ae4, throwaway, never merge; delete it once row 403 is ruled on). A `^MISMATCH` line in its "row 403 probe" log group, or the test's new "the compare held" text, is the evidence. `m3v-row403` is untouched at b8dd6ac for any real fix; the gg-m3v-row403 worktree is currently checked out on `row403-probe`.
- **Probe result (run 42687277007, 2026-11-09):** the standalone program, no product code, on the macOS runner under Rosetta beside three rounds of shards: float compares misfired 572 (fresh) + 531 (reuse) times in about 757 billion, about 1 in 690 million; the 80 reported (cap 40 per mode) all held right operands, with rescan bad 0. SSE arithmetic compared by integer bits: 0 in 2 trillion. All nine shard runs passed that time, so the test's "compare held" text never fired. PR #37's re-run (attempt 2 at b8dd6ac): success. Proven: this machine misfires a plain float compare. Inferred, not proven: that this explains the `sameBits` and event-log failures of row 234. No native arm64 control was run.
- Working hypothesis at the time, since confirmed for the compare: transient wrong execution on that VM under Rosetta with three parallel shards, not a bug in the ring reader. The persist test is the likeliest victim on exposure: about 900M compares a run.

## Sub-projects building (leads stopped; each resumes from its own ledger after its provisional-call review)
| SP | Branch / worktree | Spec / plan | State at stop | Uncommitted work |
|---|---|---|---|---|
| 2 Expression (+ note routing per layer E16, envelope curves E17) | m3-sp2-expression, /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp2-expression | 2026-11-07-m3-expression-design.md / plan (20 tasks) | Tasks 1, 3?, 4, 9, 10 merged (head 272937a "Merge Task 10"); Task 2 (Pan's sound) in flight in `/home/sethbc/.local/share/giga-grains/worktrees/gg-m3e-t2` | gg-m3e-t2: 114 modified files (likely golden expected files from an interrupted at-rest check plus Task 2's code — inspect `git diff --stat`, restore tests/golden, keep code); gg-m3e-t3 clean at ccd708b (Task 3, check whether merged) |
| 3 Pitch-sync | m3-sp3-pitchsync, /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp3-pitchsync | m3-pitchsync design / plan (10 tasks) | Tasks 1, 2, 3, 5 merged (head 23d1d18); Task 4 (Processor mapping) in flight in the main worktree | 101 modified + 1 untracked in the main worktree (Task 4 code plus probably golden churn) |
| 3b Playhead | m3-sp3b-playhead, /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp3b-playhead | m3-playhead design / plan (6 tasks) | Tasks 1, 2, 3 merged; Task 4 committed (717fa06 "Scan Speed's law…"); Task 5 (goldens and tests) in flight | 7 modified + 21 untracked in the main worktree (Task 5's goldens/tests) |
| 4 Microtuning (+ Pitch Quantize T16) | m3-sp4-tuning, /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp4-tuning | m3-tuning design / plan (15 tasks) | Tasks 1, 2, 3, 4?, 5 merged (head 5713030); Task 6 (TUNE chunk) and Task 7 (gg-render tuning) in flight; Task 7 in `/home/sethbc/.local/share/giga-grains/worktrees/gg-m3t-t7` | gg-m3t-t7: 11 modified + 9 untracked (Task 7) |

## Sub-projects paused cleanly (resume points in their ledgers, after each one's provisional-call review)
- 8 Grain shapes: m3-sp8-grainshape at 04faa97, T0, T1, T4 of 15 done; resume T2 + T5 (opus).
- 9 Per-voice filter: m3-sp9-voicefilter at b992efc, Tasks 1, 3 of 13 done; resume Task 2 (sonnet), then Task 4 (opus) with its R6.

## Planned, build not started (waiting for the director's go and full brainstorming with the director)
- 7 Tempo sync: m3-sp7-temposync, plan 42ea09a (14 tasks).
- 10 Scenes: m3-sp10-scenes, plan 7d97063 (12 tasks).
- 11 Drive/crush/resonator: m3-sp11-color, plan fa0e918 (13 tasks).
- 5 Views and presets, 6 Close: not specified yet (after 2-4, 7-11); each starts with brainstorming.

## CLAP index ranges (parallel branches)
Globals: SP2 0x25-0x2F, SP3 0x30-0x37, SP4 0x38-0x3F, SP3b 0x40-0x47, SP7 0x48-0x4F, SP10 0x51-0x57
(0x50 is Budget), SP11 0x58-0x5F. Layer: SP2 0x32-0x37, SP3 0x38-0x3B, SP4 0x3C-0x3F, SP3b 0x68-0x6F,
SP8 0x70-0x73, SP9 0x74-0x77, SP11 0x78-0x7B, SP7 0x7C-0x7F. Merges combine isReservedIndexInUse,
counts, isVoiceTarget, ParamIds lists (index order) and renumber §23 rows at merge.

## Waiting on the director
- Hands-on play of sub-project 1 (LinnStrument, keyboard) — later.
- Review of the provisional calls in each sub-project spec, now a gate before more building on 2, 3, 3b, 4, 8 and 9; 7, 10 and 11 take theirs in full brainstorming (most-flagged: SP3 S3/S4, SP4 T2/T10,
  SP2 E1/E9, SP8 G2/G7/G9, SP11 C3/C8/C9, SP7 R2/R3).
- The go to resume building (and how fast, given usage).
- Worker sessions (claude-session-driver) need `csd grant-consent` typed by the director.

## Copies of the briefs and research
`docs/m3-progress/briefs/` (spec, plan, lead briefs; throttle and slowdown notes; research/ with competitor-gaps.md, note-path.md, mpe-paths.md, voicemanager.md). Point new dispatches there, not at the old scratchpad paths.

## At the stop
An orphaned at-rest check (`check-wav-hashes.sh /home/sethbc/.local/share/giga-grains/worktrees/gg-m3e-t2`) was still running; it restores that worktree's 108 baseline expected files when it finishes, so gg-m3e-t2's "114 modified" should drop to Task 2's code only. If it was killed instead, run `git checkout -- tests/golden` there (keeping the code).
