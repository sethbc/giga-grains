# M1 sub-project 2: the Cloud engine — design

> **Erratum (2026-10-28; INTENT §23 #46, #47):** where the branch differs from this spec, the
> branch holds; the body is left as written.
> - **Baselines are per CPU model,** schema 2: one entry per `machine`, each tolerance
>   `max(0.10, 2 × (max − min) / median)`, not schema 1 with one set of ratios per leg and a
>   0.05 floor. A run on a model with no entry gets no verdict (#47).
> - **No `continue-on-error`** on CI's bench step: exit 3 becomes a warning and the step exits 0;
>   anything else still fails the job.
> - **`roundOrder` reverses the rotated order on odd rounds,** so each configuration's
>   predecessor alternates; it isn't a plain rotation.
> - **The levers were built in parallel branches,** each judged against one base (`1bd948a`).
>   The rejected ones were never merged, so nothing was `git revert`ed. From the interleave
>   lever's merge on, the stack was sequential: the prefetch live half and the arm64 shuffles
>   were ported onto it and judged against it, and both were rejected (#46).
> - **`engine-full-pool`'s capacity is `grains + 48`,** which is 2,048 at the default 2,000
>   grains.
> - **"Each lever is one commit"** doesn't hold for the interleave lever: it is two (`a911b93`
>   and the doc fix `8eb0613`).

| | |
|---|---|
| **Milestone** | M1 Cloud core (INTENT §20), sub-project 2 of 6 |
| **Status** | Rulings 1–5 decided by the director, 2026-10-27, by forced choice |
| **Spec authority** | `INTENT.md`; this document records how this sub-project implements it |
| **Builds on** | `docs/superpowers/specs/2026-10-27-cpu-headline-design.md` (the four levers and the stop rule), the FX-slice spec §1 (scope) |
| **Starts from** | `main` at `93c0d90` (sub-project 1 merged) and `docs/m1-carry-over.md` |

## 1. Scope

Sub-project 2 replaces the slice's scalar renderer with the engine INTENT §14 describes, and
builds the measuring stick for it:

- **a structure-of-arrays grain pool,** compacted once per block (carry-over [103]);
- **grain-major 4-wide SIMD kernels** for the Eco (linear) and Normal (4-point Hermite) tiers,
  over both source kinds: the sample buffer and the live ring;
- **a gate-grade `gg-bench`** (carry-over B-I2, [133], [132]) with real-engine cases and an
  in-process ratio against a frozen reference kernel;
- **the four §16 levers,** each tried, kept if it wins, then stop (§23 #39);
- carry-over items owned here: [70] the fp-contract guard, T4 (126) the live renderer's wasted
  reads.

Not in scope: HQ, mipmaps, the budget manager, the overload ladder, a Quality parameter (all M2);
publishing X (sub-project 6).

## 2. Rulings

1. **Kernel bits.** The renderer gets new per-sample math, defined once by a scalar reference
   (§3). The SIMD kernels match that reference bit for bit, on every platform's own build. The
   audio-metric goldens are regenerated once, in one commit that says why; the grain-event logs
   do not change.
2. **Eco is engine-only until M2.** The tier is latched per grain, at its onset, from
   `CloudParams::quality`, and stored in the pool. `gg-render`'s sample mode takes an optional
   `stream.quality`; a golden case covers Eco. Both plugins stay Normal. A Quality parameter
   arrives in M2 with HQ and the budget manager.
3. **The CPU gate is report-only through M1.** Sub-project 2 builds it: in-process ratios against
   a frozen reference kernel, per-leg baselines, and a verdict. CI reports the verdict without
   failing. Sub-project 6 turns it blocking, with the published X. INTENT §19 says so.
4. **The headline's source.** X is the Sample-mode case: a stereo RAM buffer. `gg-bench` also
   times the same case over the live ring, published beside X as the FX's figure.
5. **The Cloud density ceiling is the sample rate.** The §16 case needs 2,000 overlapping grains
   of 100 ms, which is 20 kHz, but the scheduler capped density at min(4 kHz, sample rate)
   (ruling M1-a). The 4 kHz cap goes: a positive density clamps to [0.01, sampleRate]. The
   plugin's Density parameter still stops at 400 Hz, so nothing a user can reach changes. This
   amends M1-a and §14's "about 4 kHz" clock range.

## 3. The scalar reference (ruling 1)

For grain g at output sample t, with every quantity latched at the onset:

| Quantity | Definition | Type |
|---|---|---|
| n | t − onsetSample | int64 |
| e | double(n) − onsetFrac | double |
| invDuration | 1.0 / durationSamples, latched once | double |
| w | HannTable::at(e × invDuration) | float |
| pos | startPos + rate × e | double |
| gainL, gainR | gain × panGains(pan).left, gain × panGains(pan).right, latched once | float |

- **Sample source:** for each output channel c, `dst[c][t] += read_c(pos) × (w × gain_c)`, where
  read is `interp::hermite4` (Normal) or `interp::linear` (Eco). One output carries source
  channel 0 with `gain` alone.
- **Live ring:** `f = float(LiveSource::fade(pos, R(t)))`, then `dst[c][t] += read_c(pos) ×
  ((w × f) × gain_c)`, where read is the ring's Hermite (Normal) or linear (Eco) reader.
- **Only samples with w > 0 contribute.** Elsewhere nothing is read and nothing is added. On the
  live ring a sample also contributes only when f > 0 and the read is inside the window, as now.
  This closes carry-over T4 (126).
- What changes against the slice: the window phase `e × invDuration` replaces `e / duration`; the
  channel gain is one latched product, so the per-sample product is `read × (w × gain_c)` in place
  of `((read × w) × gain) × pan_c`. Both change output bits, which ruling 1 allows once.
- Neither the pool's admission rule, `grainEndSample`, nor anything the scheduler emits changes,
  so the event logs stay byte-identical.

**The SIMD kernels** compute, per lane, the same double e, pos and window index as the reference,
and the same float operations in the same order; they never use horizontal operations or FMA.
Bit-identical output across block sizes therefore still holds. On the live ring, the SIMD path
covers only each grain's interior, where every tap is more than one frame inside the fade's
margins, so the reference's f is exactly 1 there; the edges run the reference itself (plan, Task 7).

## 4. The pool

- **Storage is SoA,** one array per field (§14), with 2 × capacity slots.
- **Admission is unchanged** (ruling T3-b): a grain is admitted when fewer than capacity grains
  are still sounding at its onset. A min-heap of the stored grains' end samples answers that in
  O(log capacity) per admission, with no compaction.
- **Compaction is once per block,** after rendering, stable. A mid-block compaction happens only
  when all 2 × capacity slots are full, which needs more than capacity finished grains, so it is
  amortized O(1) per admission (carry-over [103]).

## 5. `gg-bench`

- **Rounds, interleaved.** Each round runs every (case, block size) configuration once, round
  robin; each configuration keeps its own state between rounds. Every run is emitted, with the
  median, min and IQR, the run length, the round count and `--commit`.
- **Cases:** the M0 prototype kernels; `reference` (a frozen copy of M0's `hermite-simd`); and
  real-engine cases through `Engine::process` at 48 kHz, each with 2,000 grains of 100 ms
  (density 20 kHz, periodic), position 0.5 with spray 0.45 (every read inside the source), pitch +7 semitones, stereo 1:
  `engine-sample` (X), `engine-live` (the FX's figure), `engine-sample-eco`, and
  `engine-full-pool` (capacity 2,048 against 2,000 × 1 s of demand: admission under saturation).
- **Ratios.** For each engine case and block size, the median over rounds of
  `time(case) / time(reference)` in the same round. Machine speed cancels.
- **Gate.** `--baseline file` compares each ratio with the leg's baseline and reports
  `pass`/`regressed`; the threshold is the baseline's ratio times (1 + its recorded spread). CI
  runs it report-only. Baselines come from three CI runs of the finished sub-project.
- **A schema test** checks the JSON ([132]).

## 6. The levers

The four levers of §16, in this order: prefetch, interleaved source, contiguous loads with
shuffles, cache-aware scheduling. Each is one commit, judged by §16's rule on the M5 Max and the
CI legs, and either kept or reverted with its numbers recorded in
`docs/spikes/m1-cpu-levers.md`. None may change output bits: all four change only how memory is
fetched. Then optimization stops.
