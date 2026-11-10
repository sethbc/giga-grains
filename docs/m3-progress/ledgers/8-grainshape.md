# SDD ledger — plan: /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp8-grainshape/docs/superpowers/plans/2026-11-07-m3-grainshape.md
**Before resuming (the director, 2026-11-08):** read docs/m3-progress/briefs/STANDARD-WORKFLOW.md, which is binding, and check this sub-project's gate in m3-program-ledger.md.

Spec: docs/superpowers/specs/2026-11-07-m3-grainshape-design.md. Branch m3-sp8-grainshape (worktree /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp8-grainshape), base m3-voice 18ac47d (unmoved at start).
Lead brief: /tmp/claude-1000/-home-sethbc-src-giga-grains/b7b046d0-0ef6-4ef5-a912-4b2c26fccc5b/scratchpad/lead-brief-common.md (ctest -j 4; full suite once per task; <=2 implementers building; RTSan CI-only; PR base main, draft).
Program ledger: docs/m3-progress/program-ledger.md (append "8: ...").

## Pre-flight scan
| Pair / task | Shared | Finding |
|---|---|---|
| T1-T2-T3 | GrainKernels.h, kernel_tests.cpp | serial (T1 paramsOf; T2 sampleSimdRun; T3 live) |
| T1-T4 | GrainPool.h (edge vs GrainSpec::skew), pool_tests | different regions; T4 after T1 |
| T2-T4 | none | parallel OK |
| T3-T5 | none (T3 kernels/Avx2; T5 Rng, CloudScheduler, Engine.cpp sampleGrain, Processor.cpp) | parallel OK |
| T4-T5 | GrainSpec::skew produced by T4, copied by T5 | T5 after T4 |
| T5-T6 | Engine.cpp liveGrain, reverse_tests.cpp | T6 after T5 |
| T6-T8 | none (tests/CMakeLists.txt only T8) | parallel OK |
| T5-T8 | CloudParams::reverse/skew consumed by ParamMap | T8 after T5 |
| T5-T9 | Processor.cpp (sameCloud vs kVoiceTargets) | sequential |
| T8-T9 | grainshape_tests.cpp, rt_tests | T9 after T8 |
| T8-T10 | "A Reverse" names from macros | T10 after T8 |
| T7, T9, T10 | no shared files | mutually parallel |
| T11 | all engine/render/voice tasks | after T7..T10 |
| T12 | inst-skew, inst-reverse-hq specs (T11) | after T11; T13 docs parallel |
| T0 | self-consistent | voice grep ' (inst-voice-|fx-voice)' taken before T11 adds inst-voice-grainshape: fine |
| T4 | d<=32 "equal sigma 0's shape": a'=a exactly, r'=(a+r)-a=r when a==r (Texture symmetric) | consistent; reviewer checks |
| T14 Step 7 | "PR into m3-voice's line" vs lead brief "base main" | Ruling R1 |

Rulings:
- R1: PR base is main, draft (lead brief overrides plan T14 Step 7) — the top controller's instruction — cost: retarget the PR base.
- R2: Build asan in T0 only once the dev and release trees are built (load); RTSan via CI only (no local clang) — lead brief — cost: none.
- R3: Waves: T0; T1; T2||T4; T3||T5; T6||T8; T7||T9 then T10; T11; T12||T13; T14. Parallel tasks in worktrees /home/sethbc/src/gg8-tN off m3-sp8-grainshape. — plan's dependency graph — cost: merge conflicts in shared test CMake list.
- R4: Models: T2,T3,T5,T6,T11 opus implementers; others sonnet; reviewers sonnet, T2/T3 opus; final review opus.

## Tasks
- R5: release preset fails to build gg-tests on this box's system GCC 15 at -O3 (pre-existing -Warray-bounds in tests/core/parammap_tests.cpp, base code); the release tree builds `gg-bench` only — CI's legs are GCC 13/Clang — cost: none for CI; local release validators need gg-products (build that target only at T14).
- R6: voice-baseline.txt taken from this tree's own render at 18ac47d (m3-voice's build/dev/wav-sha256.txt of 07:47 was stale: 7 of 8 hashes differ; committed voice event logs match this render byte for byte) — cost: none.
Task 0: complete (no commit; 108 clean, voice baseline 8 lines, check-at-rest.sh, x-ratio-before.txt)
Task 1: dispatched (sonnet, agent aa22a38d1d79927c1, BASE 704dbf7)
- R7: T4 runs in parallel with T1 (not after it): disjoint regions of GrainPool.h/pool_tests.cpp — saves a wave — cost: a trivial merge conflict.
Task 4: dispatched (sonnet, agent ab8c18bc3eed3b9a2, worktree /home/sethbc/src/gg8-t4 branch gg8-t4, BASE 704dbf7)
PAUSE (coordinator, director's usage request): finish T1 and T4 in flight (review + merge), dispatch nothing new, no PR; resume at T2 (main worktree) || T5-after-T4... see 'Resume' line below.
Task 1: implementer DONE 2405bf9; reviewer dispatched (sonnet, ab86058968abb69d7), package review-704dbf7..2405bf9.diff
Task 1: complete (commits 704dbf7..2405bf9, review clean; 4 minors deferred to the final fix wave: GrainPool.h:71-74,113-114 comment "start at or after frame 0" (starts below 0 occur); LiveSource.h Cursor comment: a Cursor must not cross a storage change; kernel_tests.cpp blank line before the new sinc TEST_CASE; no Linear live negative-rate section)
Task 4: implementer DONE_WITH_CONCERNS c52aa15 (on gg8-t4). Concerns = brief errors, tests corrected to the spec's law: attackEnd is non-DEcreasing in sigma (brief said non-increasing); at d=100 tau=1 a=r=sMin (no side below the floor there); d<=32 anchor holds only for sides <=0.5 (Texture is asymmetric: a=0.6/0.7/0.6 at tau .25/.5/.75).
- R8: G9's formula is binding; its sentence "a grain of 32 samples or fewer ... Skew leaves it as it is" is true only for sides <= 0.5 (Texture 0 and 1); a textured short grain's long side is trimmed to 0.5 toward the floor, never the wrong way. Keep the code; amend the spec/INTENT wording in Task 13 — cost if wrong: a short textured grain's shape moves under Skew (audible only at <= 32-sample grains).
Task 4: reviewer dispatched (sonnet, a623b683126f91715)
Task 4: complete (commits 704dbf7..c52aa15, review clean; merged as 04faa97; worktree gg8-t4 removed). Minors deferred to the final fix wave: window_tests "moves the peak monotonically" strict >= could take 1-ulp slack; G9 wording (R8, Task 13).
Merged tree 04faa97: dev builds; ctest -L 'grainshape|window|pool|mip|interp' 49/49. The merged at-rest check was NOT run (load/usage pause): T2's implementer runs it.
PAUSED 2026-11-07 at the director's usage request (via the coordinator). No PR, nothing pushed.
Resume: next wave = T2 (opus implementer, main worktree; reviewer opus; asan tree not yet built: `cmake --preset asan -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache` then `cmake --build --preset asan --target gg-tests`) || T5 (opus, worktree /home/sethbc/src/gg8-t5 off m3-sp8-grainshape; T4's GrainSpec::skew is in). Then T3 (opus, reviewer opus) || T6, etc. per R3 (T5 no longer waits on T4). Briefs task-N-brief.md for all 14 tasks already extracted.

## Resume 2026-11-09 (the director's go; gate passed per m3-program-ledger.md; every call confirmed, G6 reopened and confirmed: T2, T3 stay)
Stale build/ deleted, reconfigured dev with ccache; origin/main (e9a9567) merged at 295d46d (no change under tests/golden). dev gg-tests and gg-render built.
- R9: Serial per the slowdown note: T2, T5, T3, T6, T8, T7, T9, T10, T11, T12, T13, T14, all in this worktree, one implementer at a time (supersedes R3's parallel waves) — slowdown-note.md — cost: time only.
- R10: Models per the slowdown note: opus implementers T2, T3, T5, T6, T11 (the plan marks them); sonnet otherwise; reviewers sonnet, T2/T3 opus (kernel bit-equality) as R4 — cost: usage.
Task 2: dispatched (opus, agent aa33671edc5abe7d2, BASE 295d46d)
- R11 (the director's close decisions, resume brief "Added 2026-11-09"): T14 Step 6's listening pack takes M2's form (docs/listening/m2-extremes/README.md): docs/listening/m3-grainshape/README.md (how to render, what to listen for, A/B pairs: Reverse 0 vs 1, Skew 0 vs ±, the confirmed calls' laws G2, G7, G9) plus specs/ with a handful of gg-render specs; NO rendered WAVs committed (supersedes the brief's "render the WAVs into docs/listening"). Keep it to a few minutes; list it in the PR body — cost: none.
- R12: T13's §23 row says the provisional calls were confirmed by the director's review of 2026-11-09 (resume brief), not "provisional" — cost: none.
- R13: No task needs a sibling sub-project: T6's 3b seam is a comment only; the sibling rule doesn't apply. Final report gives the crawl48 figure (treated as load) — cost: none.
Task 2: implementer DONE_WITH_CONCERNS 58b38a0 (report task-2-report.md: kernel 72/72 dev, GG_FORCE_SSE=1 and asan; at-rest 108/0/0 and voice 8/0; full suite 1806/1828: 21 bench_* only because gg-bench was unbuilt (58/58 once built), crawl48 53.6 ms under load). Concerns: "perf" subject unmeasured; asan on GCC 15; RTSan CI-only; two tests beyond the brief (stolen "reversed, the other half"; cut-at-edge mirrors across frame 0). REVIEW NOT STARTED (pause).
PAUSED 2026-11-09. RESUME POINT: done T0, T1, T4 (complete), T2 implemented at 58b38a0 but UNREVIEWED. Next: package review 295d46d..58b38a0 (`scripts/review-package PLAN 295d46d 58b38a0`) and dispatch the Task 2 reviewer (opus, R10) with task-2-brief.md, task-2-report.md, global-constraints.md; then T5 (opus), T3, T6, T8, T7, T9, T10, T11, T12, T13, T14 serially (R9). No task branches or worktrees open; origin/main merged at 295d46d. Rulings R11 (listening pack in M2's specs-only form, no WAVs), R12 (§23 row: calls confirmed by the director's review of 2026-11-09), R13 (no sibling dependency; report the crawl48 figure, 53.6 ms) apply to T13/T14. Deferred minors: see Task 1 and Task 4 lines. Build trees: build/dev, build/asan (built by T2); gg-bench not built in dev.
