# SDD ledger — plan: /home/sethbc/.local/share/giga-grains/worktrees/giga-grains-m3-voice/docs/superpowers/plans/2026-11-07-m3-voice.md
**Before resuming (the director, 2026-11-08):** read docs/m3-progress/briefs/STANDARD-WORKFLOW.md, which is binding, and check this sub-project's gate in m3-program-ledger.md.

Spec: docs/superpowers/specs/2026-11-07-m3-voice-design.md (provisional calls V1-V6; director asleep, overnight autonomy).
Branch m3-voice (from m3-program 2026-11-07). Baseline golden WAV hashes: build/dev/golden/wav-sha256.txt at branch start (update-goldens run 2026-11-07).

## Pre-flight scan

| Pair / task | Shared file or interface | Finding |
|---|---|---|
| 1 & 2 | Mutate (1: Mutate.cpp key; 2: Mutate.h locks) | disjoint edits; 1 first makes 2's appends not move presses |
| 2 & 4 | ModMatrix.h (2: isValidTarget; 4: ModSource, ModRoute) | disjoint hunks; merge order 2 then 4 |
| 2 & 3/5/6 | kParamCount, slot shifts | 3 and 5 don't index slots; 6 consumes 2 |
| 4 & 6 | isPerVoiceSource, voiceOnly, applyRoutes skip | 4 produces, 6 consumes (applyVoiceRoutes is 6's) |
| 5 & 6/7/8 | NoteEvent/ControlEvent/NoteTable | 5 produces; 6, 7, 8 consume |
| 3 & 6 | Engine voice API, AmpEnvelope | 3 produces; 6 builds AmpEnvelope from Adsr (6 adds Adsr::envelope) |
| 3 & 8/9 | event line "v" | 3 prints; 9's goldens rely on it |
| Task 3 self | AmpEnvelope header in engine; Adsr in core builds it | consistent (core depends on engine) |
| Task 4 self | factory routes 44-48 vs "slots 1 to 12 unchanged" test | consistent |
| Task 5 self | held-set equality test vs Processor switch | consistent |
| Task 6 self | steal test "click score below 15" needs metrics lib in core tests | check: tests link gg-metrics? implementer to confirm; else assert no output step via max |diff| bound |
| Task 9 self | "event lines equal after dropping i" between MIDI-MPE and expression renders | holds only if both paths give identical per-voice values; ruling below |

Ruling: Tasks 1-5 run in parallel, each in its own git worktree off m3-voice, merged into m3-voice one at a time (1, 2, 4, 5, 3) with the full dev suite after each merge — the director asked for maximum parallelism overnight; separate worktrees remove the skill's shared-workspace conflict — if wrong: merge-conflict resolution time.
Ruling: Task 9's MPE/expression equivalence compares per-voice pitch, pressure-driven density and timbre position, i.e. event lines minus "i"; if MIDI 14-bit bend quantization makes rates differ in the last bits, the test compares rates within 1e-6 relative instead — cost if wrong: a weaker equivalence check.

## Tasks
Ruling: builder-check answers B1-B8 adopted as written into spec §3a (commit 9523e25) — they close determinism and double-counting holes — cost if wrong: Task 6 rework.
Dispatched (parallel worktrees, base b1c3c40 for 1/2/4, 9523e25 for 3/5):
- Task 1 sonnet in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-t1 (agent a115ccdaf9143f6de)
- Task 2 sonnet in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-t2 (agent a855b585ccace6336)
- Task 4 sonnet in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-t4 (agent a0873cc3a3889ded4)
- Task 3 opus in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-t3 (agent a019b84d64bb031f6)
- Task 5 sonnet in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-t5 (agent a2f5d80a242ab921f)
Task 1: complete (commits b1c3c40..ef2a688, review clean; 1 minor parked: no reason comment on the re-keyed jitter-law test) — merged into m3-voice
Task 4: implementer DONE_WITH_CONCERNS (b4392ab): crawl48 timing failed under load (rerun idle at the gate); INIT pinned bytes updated; Rings.cpp skips voiceOnly routes; Mod view shows slots 44-48 — in review
PR #35 (row 405) merged as 4b0d607; CI dispatched on main (run 42369118941) to see the bootstrap job
Task 4: review round 0 -> Needs fixes (Important: SourceStrip labelOf has no labels for the 4 new sources; minors: Patch voiceOnly "1"-only comment, redundant flagged==6) — fix round 1 dispatched (resume a0873cc3a3889ded4), FIX_BASE b4392ab
m3-voice: merged origin/main (row 405) as d2b7ce8; pushed; draft PR #37 (CI on each push). Merge each task on a clean review, then push.
Task 4: fix round 1/5 (3 addressed, 0 open — SourceStrip labels, Patch comment, brittle check; commits b4392ab..b94d132)
Task 4: complete (commits b1c3c40..b94d132, review clean after 1 round) — merged 377ff3b; full dev suite 1721/1722, crawl48 timing fails under load (rerun idle at gate); editor_labels_dejavu pass; pushed
Task 3: implementer DONE (c9c0cba): concerns — steal lines lack "v"; engine never self-ends a voice (Processor must finishVoice, B8); Voice->Cloud switch forces Cloud onset; rtsan tree not built — in review
Task 5: implementer DONE (15ef16a): concerns — 2 plugin rt tests' expectations changed (128-note cap drops oldest); bend (v-0.5)*2 not exact (B7 for Task 6); NoteEvents.h new; beginGroup at end of commitPending (Task 6 reads lists before); unknown-ID Off ignored; AllOff releases pedal-held — in review
Task 5: complete (commits 9523e25..15ef16a, review clean) — merged; minors deferred: (1) no test for PedalHeld/Held replacement tiers when the table is full; (2) beginGroup() at end of commitPending — Task 6 must read lists before it; (3) NoteEvents.h enumerator alignment; (4) bend (v-0.5)*2 inexact (B7, Task 6)
Task 3: complete (commits 9523e25..c9c0cba, review clean) — merged; minors deferred: (1) no [engine][rt][voice] tripwire case (carry into Task 6's rt test) and rtsan tree unbuilt (CI RTSan + gate); (2) steal lines carry no "v" (record; Task 9 goldens that steal); (3) startVoice while voice mode off leaves stale spawning voices (Task 6 must clearVoices on mode switch or ignore — carried into Task 6 dispatch); (4) two badly wrapped comments Engine.h:326,393; (5) test helpers copied in voice_tests.cpp; (6) amp-envelope test mirrors the formula
Task 2: implementer DONE_WITH_CONCERNS (2df3f1b): kParamCount is 290 (plan's 287 was wrong: 283+4+3); pins retaken; burst sweep floor 0.405->0.39; INIT resets MPE settings; --pack-preset doesn't refuse MIDI settings; crawl48 under load — in review
Task 2: review -> Approved with changes (Important: INIT resets MIDI settings; minors: pack-preset should refuse MIDI settings; stale comment; sweep floor accepted) — fix round 1 dispatched with a merge of m3-voice into m3v-t2 (FIX_BASE 2df3f1b)
Task 8: dispatched sonnet in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-t8 (base m3-voice after Task 3 merge + plan fix; agent a172fd0651cecf7f7)
Task 8: implementer DONE (2ba9836): auto IDs 1000000+index; timedNotes/timedControls moved to gg::render in Render.h; one error message updated — in review (base aaee0b5)
Task 8: complete (commits aaee0b5..2ba9836, review clean; minor deferred: muddled frame comment in render_spec_tests timedC block) — merged, pushed
Task 2: fix round 1 commit 33738db wrongly committed 108 regenerated golden expected files — resumed implementer to revert them (new commit); re-review after
Task 2: fix round 1/5 (3 addressed, 0 open — INIT keeps MIDI settings, pack-preset refuses them, stale comment; plus golden-file revert; commits 2df3f1b..86f6943)
Task 2: complete (commits b1c3c40..86f6943, review clean after 1 round) — merged, pushed. Deferred: crawl48 50.5-52.6 ms in every full run — re-run idle at the gate, may be a real slowdown from larger presets
Task 6: dispatched opus in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-t6 (base 574627d; agent ae57feb4e045ff05d); carried: Task 3 minors (startVoice-while-off trap, finishVoice duty, Voice->Cloud forced onset, [rt] 16-voice case), Task 5 minors (beginGroup order, B7 exact bend)
Task 10: dispatched sonnet in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-t10 (base 574627d; agent a92c81a1fc65167e3)
Row 405: CI dispatch on main (run 42369118941) success; bootstrap (linux, fresh container) = success — row 405 confirmed closed
Task 10: implementer DONE (40f7236): engine-voices ratios 0.8637/0.7953/0.7300 vs engine-sample 0.8704/0.7933/0.7290 (quick, busy machine); CLAUDE.md 66 configurations — in review
crawl48: at load 2.5, passes 3/3 in both the pre-M3 tree and m3-voice (574627d) — the failures were load; resolved
Task 10: complete (commits 574627d..40f7236, review clean; minors deferred: Bench.h list wording, main.cpp ragged comment) — merged, pushed
Task 6: implementer DONE_WITH_CONCERNS (2e2bf8a): "processor does not allocate" test now keeps Mode Cloud; switching to Voice starts no voices for already-held notes (design call, director); tail is Task 7s; steal lines lack "v"; note-end queue 128 drops counted; rtsan unbuilt — in review
Ruling: Tasks 7 and 9 start from m3v-t6 (Task 6 under review) in parallel — overnight throughput; if Task 6's fix changes interfaces they merge it — cost if wrong: a merge and small rework.
Task 7: dispatched opus in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-t7 (base 2e2bf8a; agent a7e2ebc9b20b0d43e)
Task 9: dispatched sonnet in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-t9 (base 2e2bf8a; agent a120d85efb5a457e0)
Task 6: review -> Needs fixes (Important: steal ignores same-group releases, V4 legato at the cap). Minors sent in round 1: CloudParams cache never hits; material not in cache key; unused unsafe allocator methods; strike grain gain 0 at an already-stepped boundary. Fix round 1 dispatched (resume ae57feb4e045ff05d), FIX_BASE 2e2bf8a
Ruling: Note Pitch includes the tuning expression (departs from spec §4.4's (key+bend)/127) — it makes the MPE and CLAP-expression paths agree — cost if wrong: a source-value change later. Spec edited.
Ruling: switching a layer Cloud->Voice (or Layer On) with notes held starts no voices until the next note-on — simplest deterministic rule; flagged for the director — cost if wrong: small allocator change.
Deferred: confirm voiceOnly routes with a non-per-voice source or non-voice target can't be authored (or document they never apply) — Task 11 records / SP2 Mod view toggle.
Task 9: implementer DONE (57c8111): 8 cases, 16/16 each, MPE==expr bit-identical; concern fx-voice broadband streaks, peak -1.02 dBFS — controller suspects saturation; asked implementer to test at lower level and fix the golden (pre-review)
Task 9: fx-voice streaks were saturation (3 voices summed, input 0.5, peak -1.02 dBFS); golden now input 0.1, peak -6.89, click 2.24 (0a712cc). Deferred for the director/SP2: voices sum without headroom, a 3-note chord saturates at moderate input — gain staging (per-voice headroom or Voice-layer normalization) — in review
Task 9: review clean (minor deferred: test comment claims bit-for-bit audio but checks only channels; include order). HOLD merge until Task 6 fix merges; then merge t9 and re-render the 8 new cases (steal-order fix may move inst-voice-steal) — regenerate new cases only, with reason
Task 6: fix round 1/5 (5 addressed, 0 open — steal order, cache key, material, allocator methods, unstepped start; commits 2e2bf8a..9201263)
Task 6: complete (commits 574627d..9201263, review clean after 1 round) — merged ec28a55
Task 9: merged 9fe3c2e (+ fix 'close the steal-release test the merge cut'); the 8 new goldens pass unchanged after Task 6's fix; m3-voice: 1782/1783 (crawl48 load), at-rest 108/0/0, format clean; pushed
Task 7: implementer DONE (28c8b88): validators clean (clap-validator 71/0, pluginval 10, VST3 94/94); concerns — pedal-down and note-off at one sample apply note first; AU pressure/bend still channel-wide (wrapper); INTENT tail 122.5->132.5 s for Task 11; >128 notes: replaced note never gets NOTE_END and clap-wrapper VST3 grows its list (core fix needed); NOTE_END stamped at block's last frame — in review
Task 11: dispatched sonnet in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-t11 (base m3-voice after Task 9 merge; agent a57b4caa434361f6c)
Task 7: review Approved (plugin side) with 1 Important in core: evicted notes get no NOTE_END (VST3 wrapper leak/alloc). Ruling: Task 7's implementer fixes it (owns NOTE_END path; Task 6 merged) after merging m3-voice — cost if wrong: none. Minors in round: byte-1 masking consistency, R15 lenient-ID doc, stale kMaxControlEvents comment. Ruling: same-sample pedal/note-off tie stays notes-first; carry-over row. Deferred: NOTE_END stamped at block's last frame (carry-over). Fix round 1 dispatched (resume a7e2ebc9b20b0d43e), FIX_BASE 28c8b88
Task 11: implementer DONE (6740db5): m3-carry-over rows 406-422; INTENT §14/15/17/18/20/23 #95; CLAUDE.md — in review (base 46b7d05d065e1bd622b977a26fe624a42eae0fe4)
Task 11: review clean; HOLD merge until Task 7 merges (records describe Task 7s tail and NOTE_END). Minor deferred: "four parameters" vs 290 (state four parameters, seven slots)
Director 2026-11-07: approved the M3 program design and P1-P8 (PR #36 readied, merging); approved V1-V4 and B10 ("3) approved"); gain staging waits for SP2 ("4) sounds fine"); hands-on play later ("5 will have to wait"). Task 11 records must reflect these approvals (send to the records before merge)
PR #36 merged to main 97615a5. Task 11 resumed to record the approvals and fix "four parameters, seven slots"
Director 2026-11-07: "yes voice lyaers should get automatic headroom" — Ruling (form): 1/sqrt(n) per voice grain, n = layer's spawning voices in the process() call, latched at onset — matches the existing per-grain power normalization; one note unchanged; Cloud untouched — cost if wrong: retune and regenerate 8 goldens. Spec V7, plan Task 13 (0885636).
Task 13: dispatched sonnet in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-t13 (base 0885636)
Task 11: approvals recorded (57d37de, f101d66; merged m3-voice and origin/main). Needs a final pass after Task 13: row 411 gain staging -> resolved by V7 (headroom in SP1, the director's word)
Task 7: fix round 1/5 (3 addressed, 0 open — eviction NOTE_END, byte masking, docs; commits 9fe0ba9..276024b)
Task 7: complete (commits 2e2bf8a..276024b, review clean after 1 round) — merged, pushed
Task 13: implementer DONE (11ba5b5): 7 voice goldens regenerated (inst-voice-release unchanged, n=1); click injection 16/16; check script wiped voice goldens once -> controller fixed the script to restore only the 108 baseline cases — in review
Task 13: complete (commits 0885636..11ba5b5, review clean; minors: redundant guard Engine.cpp:583) — merged, pushed
Task 11: complete (review clean after 2 instruction rounds) — merged, pushed. All tasks 1-11, 13 complete; Task 12 gate running
Final review: with fixes — Important: route drop blocked by Voice-only factory routes (regression); MPE member-channel reuse rewrites releasing notes. Minors: NOTE_CHOKE as off; duplicate started_ in a full table; redundant guard; doc lines; row 234 Rosetta occurrence. Evidence: head CI incl RTSan, at-rest at head, crawl48 idle.
Ruling: fix the MPE channel-reuse leak now (not a carry-over row) — it's the LinnStrument headline case — cost if wrong: none (MPE Off unchanged).
Ruling: NOTE_CHOKE silences voices (finish at boundary, NOTE_END) — hosts expect silence — cost if wrong: small.
Fix wave dispatched (opus) in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-fix (base 18ac47d)
Final fix wave: e5c03b2, fdbfcee, 5d973ad (6 items; ASan 424/424; at-rest 108/0/0; voice goldens unchanged). Ruling: a route drop matching a Voice-only route clears its voiceOnly flag rather than adding a second route — avoids double counting on Voice layers — cost if wrong: small UI behavior change. Choke by pair ends every note on the pair (accepted: a choke is a hard stop).
Final fix wave re-review: all addressed, no new Critical/Important. Merged m3v-fix into m3-voice (b8dd6ac); first test run killed by an external SIGTERM (an agent's name-based kill) — rerunning in background, then push.
Task 12: parked — Cloud-choke wording in INTENT §14 / spec §4.5 (a choke ignores the pedal, ends every note on a pair) — Ruling: real, docs-only, carry-over row
Task 12: parked — NoteEvents.h channel/key/noteId comments stale for Choke — Ruling: real, comment-only, carry-over row
Task 12: parked — choke then full-table replacement in one group loses the choke — Ruling: real, needs a full 128-note table, carry-over row
Task 12: parked — no multi-block-size or [rt] choke test — Ruling: holds by construction; add with SP2's tests; carry-over row
Ruling (out of scope): addRouteForDrop first-match with a Voice-only route below a normal duplicate — only authorable by gg-render/state now; check when SP2's Voice toggle lands.
m3-voice b8dd6ac verified: dev 1804/1805 (crawl48 load), at-rest 108/0/0; pushed. CI at 18ac47d failed only mac-universal's Rosetta shard 3: tests/plugin/persist_tests.cpp:2124 (row 403's ring-reader stress: "a newer lap's frame: an overwritten frame accepted", read 162, frame 949, swaps 3) — first reproduction of watch row 403, in M2 code SP1 didn't touch; record the occurrence in row 403 (with the records commit) and watch b8dd6ac's run.
CI at b8dd6ac: rtsan + asan pass; mac-universal Rosetta shard 3 fails persist_tests.cpp:2124 again (row 403). Both failures report found == expected (949/949, 83667/83667) — suspect the test lap-check classification, not the reader. Debug agent dispatched in /home/sethbc/.local/share/giga-grains/worktrees/gg-m3v-row403.
