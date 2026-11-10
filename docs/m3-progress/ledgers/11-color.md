# SDD ledger — plan: /home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp11-color/docs/superpowers/plans/2026-11-07-m3-color.md

Spec: docs/superpowers/specs/2026-11-07-m3-color-design.md (C1-C15 confirmed by the director's review, 2026-11-09).
Branch m3-sp11-color. origin/main merged at 8c35af2; plan amended c367843 (Task 13 siblings last, Task 14 gate).
Gate: "11: gate passed 2026-11-09 (brainstorming; every call confirmed)" in the program ledger.
Rules: one implementer at a time; sonnet default, opus for Tasks 3, 6, 7, 8 and the final review; ctest -j 4, build -j 8; never kill by name.

## Pre-flight scan (2026-11-09)

| Pair / task | Produces vs consumes | Found |
|---|---|---|
| T1 / T3 | T1 links sst-waveshapers PRIVATE into gg-core; T3's Drive.cpp includes it | consistent |
| T2 / T3 | HalfBand2x::up(l,r,outL[2],outR[2]) / down(inL[2],inR[2],l,r) | consistent |
| T3 / T5 | T3 adds enum DriveShape to ParamIds.h; T5 adds the params and colorParams returning DriveShape | consistent (both edit ParamIds.h, serial) |
| T3 / T6 | Drive::prepare(fs, rampSamples, oversample), reset, jump, setCell, process(l,r,d) | consistent |
| T4 / T6 | Crush static bitStep/processBits/holdIncrement, member processHold/reset | consistent |
| T5 / T7 | T5 defines enums ResonatorFollow, ResonatorChord; T7's ResonatorPitch consumes them | consistent; T7 must follow T5 (it does) |
| T5 / T8 | resonatorParams(effective) -> ResonatorParams | consistent |
| T7 / T8 | Resonator, ResonatorFollower, FollowedNote, ResonatorTuningView, kStandIn* | consistent |
| T6 / T8 | both edit Processor.h/.cpp ramp sites and reset | serial; T8's "load stops every stage" needs T6's units: order holds |
| T5 / T9 / T10 | gg-render names "A Drive" .. "Resonator Channel" | consistent |
| T3,T4,T7 / T11 | gg-bench uses core/src/fx units | consistent if gg-bench already reaches core/src (it times BusFilter today); T11 checks |
| T8 / T13 | T13 replaces T8's three seam bodies and kStandInC4Hz | consistent |
| T12 / T13 | T13 amends T12's records | consistent |
| T1 | self-consistent (Step 1 takes the 116-hash branch baseline) | ok |
| T2 | tests vs code consistent | ok |
| T3 | comp's probe: "S(g·a·j/8) with drive = g" reads as double gain if the input is also pre-multiplied | Ruling R1 |
| T4 | P2 pins 17 levels | ok |
| T5 | texts: Crush 0.5 -> "8.5 bits"; Root 60 = C4 | ok |
| T6 | gating rule consistent with C10 | ok |
| T7 | "harmonic is exact ... bit for bit": a literal table vs runtime log2 could differ in a last bit | Ruling R2 |
| T8 | seams per P1 | ok |
| T9-T12, T14 | self-consistent | ok |

Ruling R1: comp's probes call the shaper with input a·j/8 and its drive argument g (sst's shapers apply the drive internally, the way process step 2 calls it), so the effective argument is g·a·j/8, never g²·a·j/8 — the spec's S(g·a·j/8) names the effective argument — cost if wrong: comp off by the drive gain, caught by the level-match test.
Ruling R2: Harmonic's eight offsets are computed off the audio thread from 12.0·std::log2(j) (construction or a static), and the test compares against the same expression — bit for bit on one machine — cost if wrong: a last-bit difference in a harmonic band's pitch, inaudible.

## Tasks
Task 1: dispatched (sonnet, agent a3b9710b9e3e73c10), BASE c367843
Task 1: implementer DONE ec9acc9; review dispatched (sonnet)
Ruling: Task 1's Minor (THIRD_PARTY_NOTICES.md lines over 100 columns) parked, no change — the file already carries unwrapped lines up to 324 columns (line 63) and the embed test is byte-for-byte on whatever is there — cost if wrong: cosmetics.
Task 1: complete (commits c367843..ec9acc9, review clean; 1 Minor parked). Branch baseline: build/dev/golden/branch-start-sha256.txt (116; 108 match at-rest baseline).
Task 2: dispatched (sonnet), BASE ec9acc9
Note: build/asan configured (ccache) 2026-11-09 for later tasks' -L color/resonator ASan runs.
Task 2: implementer DONE 82c834f; asan deferred to Task 3's run (tree configured after dispatch); review dispatched
Ruling: Task 2's four Minors parked (chunking test of a per-sample API; cascade's coefficient locals as static constexpr; cascade lacks SST_CPPUTILS_NONBLOCKING though inline in the same header; FMA note covered by the fp-contract canary) — polish, no behavior — cost if wrong: an RTSan/-Wfunction-effects warning on CI's LLVM, which the rtsan job would show.
Task 2: complete (commits ec9acc9..82c834f, review clean; 4 Minors parked)
Task 3: dispatched (opus), BASE 82c834f
PAUSE received 2026-11-09 (pause-2026-11-09.md): Task 3's implementer told to finish or commit wip within ~20 min; nothing new starts.
Task 3: implementer NEEDS_CONTEXT at pause; committed 014c65f "wip: Drive (C2-C4); left: level-match and shape-change click failures need a ruling" (10/12 -L color pass; asan and the full dev suite not run; no review yet). Report task-3-report.md. Alias margins measured Soft 9.62 dB, Hard 7.82 dB (pinned 8.6, 6.8). Implementer found HalfBand2x::up keeps the library's 0.5 gain; Drive doubles its outputs.

PAUSED 2026-11-09. RESUME POINT: Tasks 1-2 complete (ec9acc9, 82c834f). Task 3 in progress at 014c65f (wip, on m3-sp11-color itself, no task branch or task worktree). Rulings pending before Task 3 can finish (all at d = 1, read task-3-report.md): (a) peaks of Medium +0.52, Hard +0.76, OJD +0.70 dB vs the ±0.5 dB bound (half-band overshoot on a near-square); (b) RMS: Rectify -15.9 dB (clip then rectify ≈ DC, removed by the DC blocker), Fold +11.1 dB and Sine Fold +9.1 dB (the eight probes miss the folds' peaks; comp at its 4x cap) vs ±9 dB; (c) clicks: the shape stepped 0..7 at d = 0.6 scores 18.49 (which change unknown), turn-back and third-shape renders 15.49, limit 15. (a)/(b) touch C3's law (confirmed by the director) — decide whether test bounds or the probe set change, and whether that needs the director. Then resume the same implementer or a fresh opus one with that ruling, run asan -L color (build/asan configured, not built) and the full dev suite, then the task review. Next tasks: 4 (Crush, sonnet) onward; Task 13 waits for sp4 and sp2 on main. Branch baseline hashes: build/dev/golden/branch-start-sha256.txt.
