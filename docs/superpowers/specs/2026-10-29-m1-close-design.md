# M1 sub-project 6: Close

| | |
|---|---|
| **Status** | Short design, written 2026-10-29 for the director's approval |
| **Milestone** | M1 (Cloud core), sub-project 6 of 6 |
| **Depends on** | Sub-projects 1–5 merged (PRs #2, #4, #6, #7, #8, #9) and the Perform close PR (#10) |
| **Authority** | INTENT.md. Part II changes below each add a dated §23 row |

## 1. Intent

Close M1: publish the §16 headline as measured, record the director's decisions that end the
milestone, settle every open carry-over row by closing it or stating what it waits on, and mark
M1 done in INTENT §20. Nothing in the audio path changes.

**Director decisions (2026-10-29, in brainstorming):**
1. **The CPU gate stays report-only through M1 and turns blocking at 1.0** (the first public
   release), not in this sub-project. INTENT §19, §20 and §23 #44 say "sub-project 6 makes it
   blocking"; they are amended. Carry-over rows 144 and 148 move to "At 1.0".
2. **Listening gate #1 is passed:** the director listened to the Sources, Space and Perform packs
   (`docs/listening/m1-sources`, `m1-space`, `m1-perform`) and accepted them. No further pack.
   The macro depths stay at the spec's starting values (my reading of "good there"; the Perform
   pack's tuning step is closed as "accepted as shipped").

## 2. Goals, non-goals, anti-goals

- **Goals:** X published as measured on the reference core; the live and Space figures beside
  it; the canvas's idle figure (decision D3 of sub-project 5); INTENT consistent with what was
  decided; the carry-over at its lowest honest count; M1 closed in §20.
- **Non-goals:** the blocking gate (1.0); installers and the release pipeline (their own spec);
  any DSP, engine or UI change; new baselines for CPU models CI hasn't shown.
- **Anti-goals:** a headline measured on a loaded machine or on CI; a carry-over row closed
  without the thing it names being done; a §23 row that rewrites a decision instead of
  recording it.

## 3. Constraints

- X is "2,000 Normal grains per instance at 48 kHz in X% of one Apple M5 Max performance core",
  measured on the real engine path (`gg-bench`'s `engine-sample`), published with no ceiling
  and naming the chip (INTENT §16, §23 #39, #44).
- The measurement runs on this machine with nothing else running: no agent builds, no other
  benchmarks, the load average under 1.5 for the minute before, the `release` preset, the full
  default run (5 rounds × 2 s per configuration). The same session runs `gg-bench --space` and
  `gg-bench --canvas` (the idle 8,192-grain figure D3 asks for) and the ratio check row 185 asks
  for.
- Only AGPLv3-compatible dependencies; nothing new is added.
- Formatting and the pre-commit gate as always; the goldens' 22 WAV hashes unchanged (no code
  changes, so trivially).

## 4. Approaches

- **One idle run on the reference core, published as measured (chosen).** The only way that
  gives the headline INTENT promises: the chip named, the engine path, no load. Cost: the machine
  must be idle for about ten minutes.
- *CI's gg-bench figures:* rejected. They name GitHub's EPYC and Xeon models and an M1 VM, and
  drift ~20% between runs (§23 #47).
- *A quick run now, a full run later:* rejected. A `--quick` figure would be published and then
  moved; INTENT publishes once.

## 5. The how, as decided

### 5.1 The idle run

1. Confirm idleness (`uptime` load average under 1.5; no `cmake`, `ninja`, `clang` or `gg-`
   processes), `cmake --preset release`, build `gg-bench`.
2. `gg-bench --commit <sha> --json docs/benchmarks/2026-10-29-m1-headline.json` (the full
   default run), then `--space --json docs/benchmarks/2026-10-29-m1-space.json` and
   `--canvas --json docs/benchmarks/2026-10-29-m1-canvas.json`. The JSON files are committed;
   `docs/benchmarks/README.md` says what they are and how to reproduce them.
3. **X** = `engine-sample`'s median ns per grain-sample at block 256 × 2,000 grains × 48,000
   samples/s ÷ 10⁷, in percent of one core; the three block sizes are all published, X being
   the 256 figure (a host's typical block). The live figure is `engine-live`'s the same way;
   Space's is `--space`'s "% of one core"; the canvas figure is `--canvas`'s 8,192-grain
   median against the 3 ms budget.
4. Row 185's check: the `engine-sample` ratios against `reference` from this run, beside
   sub-project 2's `a0611c1` figures, within R3's 3%.

### 5.2 INTENT (Part II)

- **§16:** the headline line gets X, with the block-32/64 figures, the live figure, Space's
  (already 0.137%, re-measured), the canvas's idle figure, the commit and the date; "X is
  measured at the end of M1" becomes past tense.
- **§18:** verify the AUv2 parameter order against clap-wrapper's AUv2 source (`ParameterList`
  / `GetParameterList` in `libs/clap/clap-wrapper/src/detail/auv2/`) and auval's listing (the
  Perform pack recorded ascending CLAP-ID order); amend the "slot order is the AUv2 order"
  sentence to what clap-wrapper does, keeping "frozen at first release". (Sub-project 5's
  decision D2.)
- **§19, §20:** "sub-project 6 makes it blocking" → "it turns blocking at 1.0"; §20's M1 row
  reads as closed: listening gate #1 passed 2026-10-29 on the three packs, X published, CPU
  baseline recorded.
- **§23 #60:** the two decisions of §1, the AU-order amendment, X and the figures, the
  carry-over's end state, "Decided 2026-10-29 by the director, during M1 sub-project 6".

### 5.3 The carry-over

- 144, 148: "Sub-project 6" → "At 1.0 (the director, 2026-10-29: the gate turns blocking at the
  first public release)".
- 185: **Closed** by the idle run (cite the JSON).
- 233 (the AU order): **Closed** by the §18 amendment.
- Every other open row stays, with its reason as written (upstream bumps: 85, 87, 88, 218, 231;
  M2/M3 features: 51, 118, 119, 184, 217; the installers: 221).
- Count stated in the commit: 15 before, 14 after (144 and 148 stay open at 1.0).

### 5.4 CLAUDE.md, memory

- CLAUDE.md: "Sub-project 6 makes the gate blocking" (two places) → "blocking at 1.0"; the
  benchmarks section gains the published X beside the Space figure it already carries.
- The session memory marks M1 closed and points at the installers' spec as the next
  pre-release item.

## 6. Left to the builder

Exact wording of the INTENT lines; whether the canvas JSON belongs in `docs/benchmarks/` or
beside the Perform pack (either, said in the README); the `docs/benchmarks/README.md` text.

## 7. Verification

- The three JSON files parse and carry the stated commit.
- `scripts/check.sh` passes (docs and JSON only; nothing else changes).
- The carry-over count is stated and matches `grep`.
- The PR's CI is green; merged on green (the director's standing authorization).
