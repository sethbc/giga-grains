# SDD ledger — plan: docs/superpowers/plans/2026-11-07-m3-tuning.md
**Before resuming (the director, 2026-11-08):** read docs/m3-progress/briefs/STANDARD-WORKFLOW.md, which is binding, and check this sub-project's gate in m3-program-ledger.md.

Spec: docs/superpowers/specs/2026-11-07-m3-tuning-design.md (binding). Branch m3-sp4-tuning, worktree /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp4-tuning.
Start: merged m3-voice (18ac47d) -> c95bcda.

## Top-controller ruling (amends Task 1, A5, Task 12)
Ruling R0: MTS-ESP is vendored, not a submodule: libs/mts-esp-client/ holds Client/libMTSClient.{h,cpp} and the 0BSD LICENSE unmodified, a README (upstream URL, pin f214739b8832e7f297cb9970d0c0efbf783f1462, SHA-256 of each file), and cmake/MtsEspGuard.cmake fails configure on any SHA-256 mismatch (scratch-project tests like asio_guard_*). tuning-library stays a submodule at d6fcc08. CI downloads libMTS.so (SHA-256 checked) only in the Linux leg's step. — the upstream repo holds ODDSound's closed binaries — cost if wrong: mechanical switch to a submodule.

## Pre-flight scan
| Pair / task | Produces vs consumes | Finding |
|---|---|---|
| T1 self | submodule add MTS-ESP; tarball exclusion; release_scripts test | Conflicts with R0. Ruling R1 below. |
| T1 -> T12 | gg_mts_master from libs/MTS-ESP/Master/libMTSMaster.cpp; CI step checks libs/MTS-ESP/libMTS/... | No submodule under R0. Ruling R1, R2. |
| T1 -> T2 | gg::tuning-library INTERFACE target | ok |
| T2 -> T5 | TuningTable, MtsTuning, tuningFromScala; T5 adds tuningFromFrequencies to Tuning.h/.cpp | ok (T5 extends T2's files) |
| T2 -> T6 | kMaxTuningTextBytes (Tuning.h) | T6 depends on T2 only; can run parallel with T5 |
| T3 -> T5 | EngineConfig::keySemitones, CloudScheduler::setKeySemitones | ok |
| T4 -> T5 | ParamSlot::MtsEsp | ok |
| T4 vs T3 | disjoint files (ParamIds/EditLayout vs engine) | parallel |
| T5 -> T7, T9 | setTuning, effectiveTuning, notePitch | ok |
| T6 -> T7, T9 | SessionState::tuning/newerTuning, TuningFiles | ok |
| T7 vs T9 | tools/ vs plugin/ ; both touch tests/CMakeLists.txt | parallel; merge conflicts trivial |
| T7 -> T8 | tuning key in specs | ok |
| T9 -> T10 | TuningStatus (T9 puts it in Tuning.h), tuningStatus() | ok |
| T10 -> T11 | TuningStatus Mts fields, hooks | T11 after T10 |
| T10 -> T12 | plugin with real client; seam | ok; T11 and T12 parallel |
| T8 vs T10 | disjoint | parallel |
| T5 self | notePitch public; spec says sub-project 3 adds notePitch with today's body | whoever merges second consolidates (R5) |
| T8 self | expected-frequency test for inst-voice-just after 5 s left to T5 | ok |
| T11 self | names ui/src/Editor.cpp etc. which may live under plugin/src/editor | implementer locates real files; not a conflict |
| T12 self | skip on missing lib; GG_REQUIRE_MTS_ESP | ok |
| T13 self | m3-carry-over.md exists now (sub-project 1 created it) | append section |

Ruling R1: Task 1 vendors into libs/mts-esp-client/ also Master/libMTSMaster.{h,cpp} (0BSD, source only), because Task 12's test executable compiles it — "exactly the files the build compiles" — cost if wrong: delete two files and fetch them in CI.
Ruling R2: A5 is moot (no libMTS in the tree): source-tarball.sh is not changed and the release_scripts test `source tarball leaves out libMTS binaries` is not written; instead the guard test `mts_esp_guard_*` and a notices test. Task 12's CI step downloads https://raw.githubusercontent.com/ODDSound/MTS-ESP/f214739b8832e7f297cb9970d0c0efbf783f1462/libMTS/Linux/x86_64/libMTS.so, checks 03d23cb8d64ae246ceb416a7323cb230c5748c4b58d52a7c288d2c93ea3b7de5, installs to /usr/local/lib. — cost if wrong: small CI edit.
Ruling R3: parallel waves: {T1,T3,T4} -> T2 -> {T5,T6} -> {T7,T9} -> {T8,T10} -> {T11,T12} -> T13 -> T14; each parallel task in its own worktree off m3-sp4-tuning, merged when its review is clean — plan marks no order beyond interfaces; the interfaces above allow it — cost if wrong: merge conflicts (small).
Ruling R5: notePitch: if m3-sp3-pitchsync lands first, our merge keeps one Processor::notePitch with our tuned body and one kC4Hz (gg/core/Tuning.h's); if we land first, sub-project 3's merge does it. — spec §9 — cost if wrong: one consolidation edit.

## Tasks
Wave A dispatched (BASE c95bcda for all): T1 worktree gg-m3t-t1 (m3t-t1) sonnet a19dd6eaeb6f3eb3e; T3 gg-m3t-t3 (m3t-t3) sonnet aeaf3f21b12ba0c4b; T4 gg-m3t-t4 (m3t-t4) sonnet aacccf7cd8157804a.
Ruling R6 (top controller's scope addition, provisional for the director): Pitch Quantize, per layer (Off, Scale), layer index 0x3C, spec §12 T16, plan Task 15 (after Task 7, before Task 13). The layer's Pitch (base + mod) snaps with Pitch Spread (the engine sees one pitch per grain); degrees = sorted distinct mapped key terms; ties lower; ends clamp — cost if wrong: move the snap point / add a period-repeating degree set.
T1: review ✅/approved; minors 1-2 (double guard include, .gitattributes -text) sent to implementer
T2 dispatched opus a91272f6d72f2dc25 in gg-m3t-t2 (branched from m3t-t1 a72c7cf)
Task 1: complete (commits c95bcda..31aa6ae, review clean; minors 1-2 fixed, controller-checked) merged 4b3129e
Throttle note applied (ctest -j 4; at most 2 implementers building; at-rest only for DSP/render tasks); appended to global-constraints.md
Ruling R7: T7's "a key outside the .kbm's first-to-last range is unmapped" yields to the pin: tuning-library at d6fcc08 ignores that range, and T4 (the port matches the library, corpus-tested) binds — spec T7 superseded on that clause — cost if wrong: one extra mask in tuningFromScala, and a corpus-test exception.
Ruling R8: bad/ corpus files: we agree with tuning-library on each (it accepts 3 of 10 on purpose: trailing data), not "all refused" — the plan's text was wrong about the corpus — cost if wrong: none.
Ruling R9: spec §5.2's drop-past-15-digits rule stands even where not correctly rounded (deterministic; under 1e-12 cents) — cost if wrong: a longer exact parse.
T2 review (opus a94a0626b6e446670): spec ✅; Important 1 (corpus pairs only within folders); minors 1-4 promoted into round 1 by ruling (high-byte guard too strict; cents exponent silently differs; maps-no-key comment+test; snap helper reusable for T5) — they touch correctness or Task 5. Deferred minors: T2 minor (deferred): catch handlers could throw under OOM.
Task 2: fix round 1/5 dispatched to a91272f6d72f2dc25 (FIX_BASE 0f9f6fe)
Task 2: fix round 1/5 (5 addressed per implementer, re-review a943e32b12c385296 pending; commits 0f9f6fe..b82d94d)
T3 implementer DONE_WITH_CONCERNS 6162f09 (crawl test 57 ms = load; table in namespace gg); review a82f636fa6e987d29 dispatched.
Task 2: complete (commits a72c7cf..b82d94d, review clean after round 1) merged
Task 2: minor (deferred): kbmHasHighByte parses the first value as int64 vs atoi's int (files past INT_MAX refused elsewhere)
Task 3: complete (commits c95bcda..6162f09, review clean) merged
Task 3: minor (deferred): spread-branch test checks only rate != base; keyIndex declared among data members
T4 implementer DONE 6265ed8; review a88eeab24955b6a26. T6 dispatched sonnet a85ab1c2bbedaf4c9 in gg-m3t-t6 (BASE dfdaaf3).
Task 4: complete (commits c95bcda..6265ed8, review clean) merged
Task 4: minor (deferred): ParamIds.h:360 next-free comment vs the empty 0x25-0x37 gap; EditLayout.cpp:165 stale "62 + 2 + 27" comment
T5 dispatched opus a22e04f550b55d72f in gg-m3t-t5 (BASE b18fa0c)
Rule: never pkill/killall/kill by pattern; appended to global-constraints and sent to T5, T6
Ruling R10: encodeState skips a tuning over the limits (decode would refuse the whole state); unreachable since every load path refuses >64 KiB — cost if wrong: a state silently without its tuning in an impossible case. T6 implementer ran one pkill -f before the rule reached it (reported to top controller in final report).
Task 6: complete (commits dfdaaf3..0156012, review clean) merged ce88541
Slowdown rule (director via top controller): one implementer at a time; sonnet for implementers/reviewers (opus only for design-heavy engine/kernel and final review); short dispatches; re-reviews only for Critical/Important. Ruling R3 (parallel waves) superseded: serial from T5 on: T5 -> T7 -> T8 -> T15 -> T9 -> T10 -> T11 -> T12 -> T13 -> T14. T9 and T10 drop to sonnet (plan said opus; plugin-side, not engine) — cost if wrong: more fix rounds.
Task 5: complete (commits b18fa0c..d5d2dfb, review clean) merged
Task 5: minor (deferred): a dropped unmapped note-on still splits a piece (no output change); NoteTable's noteId<0 wildcard could match a held note on the same channel+key (existing semantics)
T7 dispatched sonnet a62eeb6b75b1d9d1f in gg-m3t-t7 (BASE 5713030)

## Resume 2026-11-09 (director's go; gate passed per m3-program-ledger.md; every call confirmed)
Task 6 verified on the branch (ce88541, SessionState::tuning present). Merged origin/main e9a9567 -> 4959191.
T7's uncommitted work kept: committed as c506457 on m3t-t7 (unverified WIP), then merged 4959191 into m3t-t7 -> 09e9ce2. T7's review BASE is 4959191. Implementer a62eeb6b75b1d9d1f did not survive the stop; a new one finishes it.
Stale build/ trees of both worktrees deleted, worktree-setup --configure dev rerun with ccache.
Ruling R11: Task 12's CI step fetches libMTS.so by SHA-256 to a directory outside the source tree ($RUNNER_TEMP), per the director's review of 2026-11-09; the plan's "libs/MTS-ESP/libMTS/..." and the spec's "pinned submodule" are superseded (R0, R2) — the closed binary must never sit in this repository — cost if wrong: small CI edit.
Ruling R12: scripts/check-at-rest.sh is not on main yet, so the at-rest check stays the old check-wav-hashes.sh in giga-grains-m3-voice's plan workspace (STANDARD-WORKFLOW: until a branch has merged main with it) — cost if wrong: none.
T7 finisher dispatched sonnet a797509eaa0906a6d in gg-m3t-t7 (review BASE 4959191)
Task 7: complete (commits 4959191..c50382d, review clean; reviewer acb524cf1d49dfc07)
Task 7: minor (deferred): render_tuning_save_state.cmake's hex FIND isn't byte-aligned (the in-process test covers it); bad-TUNE preset's warning text untested, state's 12-TET not compared; state-then-preset and spec-over-state order untested end to end; a preset's newerTuning renders 12-TET with no warning; RenderSpec.h:1481 comment wrap; render_spec_tests.cpp:614 unused empty.kbm
Crawl test: 54.8 ms against 50 in dev under load (load average 10-15); unrelated to gg-render; treated as load per the lead brief.
Ruling R13: from T8 on, implementers work in the sub-project worktree itself (serial, one at a time; no task worktrees) — saves a build per task — cost if wrong: none.
T8 dispatched sonnet a84a183e8302afb59 in the sub-project worktree (BASE 8e90d5e)
Director (via coordinator, 2026-11-09): a short listening pack per sub-project before finishing. Ruling R14: Task 14 Step 5 becomes docs/listening/m3-tuning/ in M2's form (docs/listening/m2-extremes/README.md): README with how to render, specs/ of a handful of gg-render specs, no renders committed; A/B pairs (12-TET vs 19-EDO; just vs 12-TET thirds; unmapped keys; BP; Pitch Quantize Off vs Scale); a few minutes in all; listed in the PR body — supersedes the plan's "the four cases' WAVs" — cost if wrong: none.
No task of ours needs a sibling not yet on main (T16 is this sub-project's own). Final report gives the crawl figure (54.8 ms in dev under load at T7).
T8 implementer DONE_WITH_CONCERNS d2aa3d2 (full suite 1886/1887, only the crawl; at rest 108 unchanged; fx-tuning-unmapped click count 12 pinned; 19-EDO one grain per note for the 1-cent peak check). Review NOT dispatched (pause).
PAUSED 2026-11-09. RESUME POINT: Tasks 1-7 complete and merged; Task 8 implemented and committed on the branch itself (8e90d5e..d2aa3d2, report task-8-report.md) but not reviewed: next is Task 8's task review (BASE 8e90d5e, HEAD d2aa3d2), then serial T15 -> T9 -> T10 -> T11 -> T12 -> T13 -> T14 (R14: the listening pack in M2's form in Task 14; R11: CI fetches libMTS.so by SHA-256 outside the tree). No task branches or task worktrees remain (gg-m3t-t7 merged and removed). No ruling pending. Head d2aa3d2, pushed to origin/m3-sp4-tuning. Crawl test reads 54.8 ms under load. At-rest check is still the old check-wav-hashes.sh (check-at-rest.sh not on main).
