# M2 sub-project 3: Engine scale and quality — design

| | |
|---|---|
| **Status** | **Approved by the programme manager 2026-10-30** against the programme design and INTENT. **The director's decisions D1–D5, 2026-10-30,** are recorded in §8 and applied in place: D1 option 1, D2 option 1, D3 option 1, D4 option 1, D5 option 1. Not yet built: the plan is `docs/superpowers/plans/2026-10-30-m2-engine.md` |
| **Milestone** | M2 Giga (INTENT §20, §23 #64), sub-project 3 of 7; wave 2, merged first (sub-projects 2 and 4 rebase on this one) |
| **Spec authority** | `INTENT.md`, then `docs/superpowers/specs/2026-10-29-m2-programme-design.md` (its §6.3, §7 and §12 are this sub-project's charter: realized here, not reopened). This document records how this sub-project implements them |
| **Builds on** | M2 sub-project 1 as merged (`ab9f4e2`, PR #15; INTENT §23 #65): four layers each with its own `gg::Engine` and pool of 4,096, the Source kind, the mod matrix, render tasks, the patch at version 2, the mip-level accessor (`levelCount()`, `level(n)`) on `PreparedSource` and `LiveSource`, and the reserved index block `0x..50`–`0x..5F` in every scope |
| **Feeds** | Sub-project 2 (the live ring's and resample buffer's incremental pyramids behind the same accessor; the memory cap counts the file pyramid); sub-project 4 (the chain after the grain sum is untouched here); sub-project 6 (the Quality and Budget parameters in the Edit view and the memory readout); sub-project 7 (the idle run's HQ and overload figures, the baselines' new entries) |
| **Starts from** | `main` at `ab9f4e2`, and `docs/m2-carry-over.md` with **11 open rows** (51, 85, 87, 88, 144, 148, 184, 217, 218, 221, 232) |

## 1. Why this sub-project

INTENT §5 promises thousands of concurrent grains at bounded CPU, and §8.4 and §14 say how the
bound is kept: a deterministic budget that degrades in a fixed order and never adapts to measured
load. M1 built the pool and two tiers; sub-project 1 gave each layer its own pool. Nothing yet
bounds what four layers at HQ may cost, nothing anti-aliases an upward transposition, and the
third tier, the windowed sinc, is still M0's scalar prototype in `gg-bench`.

This sub-project delivers the programme design's inventory items I14 to I19 (§6.3 there):

- **I14** the budget manager: a fixed weight table per tier and channel count, a per-instance
  budget, per-layer quotas, and the overload ladder in §14's order (lower the tier, drop new
  grains, steal with fades), closing carry-over row 51;
- **I15** the pool configurable to 16,384 grains;
- **I16** mipmaps for upward transposition: the file pyramid, built offline, and the kernels'
  level selection per grain from its rate, behind the accessor sub-project 1 left (the live and
  resample buffers' incremental pyramids are sub-project 2's, after this merges);
- **I17** the HQ tier, a SIMD windowed sinc bit-identical to its scalar reference, and a
  per-layer Quality parameter (Eco, Normal, HQ; default Normal) in both plugins;
- **I18** the sweep harness as a test family that runs in CI;
- **I19** the timeboxed AVX2 spike (§7.10), kept only if measured.

## 2. Goals

1. **Bounded, deterministic cost.** An instance never costs more than its budget's worth of
   weighted grain-samples per output sample (plus the bounded overshoot of §6.5), whatever the
   layers ask for, and the ladder's decisions depend on the schedule alone: identical at block
   sizes 1, 32, 64, 511 and 4096, with no thread pool and with pools of 1, 2 and 8 workers.
2. **It never clicks and never drops out.** Overload at 16,384 one-second grains into one engine
   renders continuous, finite audio with no click above the goldens' limit, at every block size,
   the same bytes.
3. **Bit-identity with M1 at Normal with no overload.** With every layer at Normal and no layer
   over its quota, every one of the 36 goldens keeps its WAV SHA-256 through every task, in both
   products.
4. **HQ is real.** A 16-tap (D3) windowed sinc, SIMD, matching its scalar reference bit
   for bit, with the file pyramid so an upward transposition at HQ is anti-aliased; the
   `engine-sample-hq` figure published beside X.
5. **No CPU regression on X:** `engine-sample` shows no `regressed` ratio against the committed
   baselines on any CI model, and its M5 Max figure is within the run-to-run spread of
   `docs/benchmarks/2026-10-29-m1-headline.json`.
6. **The machines sweep.** The sweep harness runs on every CI leg and under the sanitizers:
   parameter sweeps, extremes and automation bursts over both products, with no NaN, crash,
   runaway gain or click above the limit.
7. **AVX2 answered:** kept with the §23 ruling and rebuilt x86 baselines, or dropped with its
   measurements recorded.

## 3. Non-goals and anti-goals

**Non-goals (later sub-projects, or later milestones):**
- The live ring's and resample buffer's pyramids (sub-project 2): at HQ a Live layer's grain above
  rate 1 reads level 0 until 2 lands, through the same accessor, and aliases as Normal does.
- The memory cap, which counts the file pyramid (2). This spec records the pyramid's cost (§6.3).
- Per-voice quotas (M3's Voice scheduler): the quota model here is per layer, with one stream per
  layer; M3 divides a layer's quota among its voices.
- Calibrating the weight table against measured kernels on several machines (M5's "budget weights
  calibrated", INTENT §20): M2's table is set from `gg-bench` on the M5 Max and rounded (§6.4).
- Per-grain filters behind a budget weight (M5).
- A UI for Quality, Budget or the memory readout (6). Both parameters are reachable through the
  host's generic editor, automation, the remote pages and `gg-render` until then.
- Synthetic grains' mip levels from their frequency (M5).

**Anti-goals** (a failure even if everything works):
- Any admission, tier or steal decision that reads a clock, a load measurement, a thread ID or a
  host block boundary.
- A budget or quota that lives in the per-machine settings file: a session must render
  identically on every machine (INTENT §8.8), so the budget is in the state or fixed, never per
  machine.
- A render-mode-dependent tier (offline bounce at HQ, say) without an explicit parameter.
- An AVX2 path kept on a feeling: it stays only if the floor legs' ratios improve beyond their
  spread (§7.10).
- A golden that moves without its commit naming the case and the reason.

## 4. Constraints

- CLAUDE.md's non-negotiables: no allocation, lock, I/O or unbounded loop on the audio thread;
  RTSan-clean; 0- and 1-sample blocks; the counter RNG alone; layers summed in order; 64-bit
  positions; the ladder's order (tier, drop, steal); licensing.
- **Ownership** (programme design §6.3): `engine/include/gg/grain/` and its sources (the pool,
  kernels, scheduler admission, tiers), `Interpolate.h`, the mipmap code (`PreparedSource` and a
  new `Mipmap.h`), `tools/gg-bench`'s cases, the sweep harness under `tests/`. Shared files
  (`Processor`, `ParamIds.h`, `tests/CMakeLists.txt`, INTENT) by addition only. `LiveSource` is
  sub-project 2's: this sub-project adds to it only what the HQ kernel needs to read the ring
  (§6.2) and widens its guard; its levels, length and operations are 2's. The brief says to stop
  at a boundary rather than copy another's code, and this spec names each crossing.
- **Parameters** in the reserved block `0x..50`–`0x..5F`: index `0x50` in each layer scope and in
  scope 0 (§6.8). No patch version bump: absent parameters take their defaults (programme design
  §7.5). No new chunk. No new scope.
- Merged first in wave 2: sub-projects 2 and 4 rebase on the new pool, kernels and `Tier`.
- The `reference` kernel in `tools/gg-bench/Kernels.h` is frozen: nothing here touches it, so
  every baseline stays valid.
- The grain-event line gains fields by insertion only (precedents: `"tex"`, `"layer"`), in one
  commit that rewrites every expected log and moves no WAV.
- Every new dependency would need a pin and an AGPLv3-compatible license; this spec adds none.

## 5. Approaches

**The budget's scope.** Three shapes were weighed for where the bound lives.
- **A per-layer quota enforced in each `Engine`, computed by the `Processor` from the instance
  budget (chosen).** Each layer admits within its quota without knowing the others, so the four
  render tasks stay independent and the output is the same with any thread pool. The quota
  changes only at grid boundaries, from the on-set and the Budget's effective value, which are
  block-size invariant.
- *One shared ledger across layers, admissions in onset order:* exact instance-wide accounting,
  but a layer's admission would then depend on the other layers' grains at that onset, which
  the parallel tasks admit concurrently; it needs scheduling pulled out of the tasks and serialized
  per piece, a restructure of what sub-project 1 built, and pieces end at call ends.
- *A per-layer budget only (no instance budget):* four layers could cost four budgets; the
  programme design says per instance.

**The HQ kernel's coefficients.** Two.
- **One continuous table of the windowed sinc, read with linear interpolation, stretched by the
  residual rate (chosen).** The same lookup serves rate ≤ 1 (no stretch) and the residual rate in
  [1, 2) above a mip level (stretched, so the cutoff follows the rate); one scalar reference
  defines the bits.
- *A polyphase table indexed by the fractional position:* faster for rate ≤ 1, but the stretched
  case needs a second table and a second arithmetic, and the reference would have to mirror both.
  The builder may use a polyphase layout inside the SIMD kernel only if it reproduces the
  reference's bits (§15).

**Mipmaps at which tiers.** The director's D5. HQ only is recommended: it keeps every M1 hash,
and it makes the tiers audibly different "characters" (INTENT §6), Normal keeping M1's sound.

**Stealing.** Two accounting designs for a grain whose end moves (row 51).
- **A heap with handles (chosen):** each stored grain has one heap entry whose key is its end and
  whose payload is its weight; a steal decreases that key. The heap never exceeds the pool's
  storage, and the ledger stays exact in O(log capacity).
- *Lazy residues (push a correcting pair and keep the old entry):* simpler to write, but each
  steal leaves two entries until the original end, and at the exit's density (16,384 steals a
  second against one-second grains) the heap grows without a bound the audio thread can hold.

**The sweep harness.** A Catch2 family in `gg-tests` (chosen), so it runs on every leg and under
the sanitizers with no new CI step, split into many cases so the parallel test preset spreads it;
not a separate executable with a script, which CI would have to learn and the sanitizers would
miss.

**AVX2.** A second translation unit of the sample and live kernels compiled with `-mavx2` (and no
FMA), chosen once at load by CPU features through a function pointer (chosen), not GCC's function
multiversioning attributes, which ClangCL doesn't offer and which hide the dispatch point the
tests need to force.

## 6. Design

### 6.1 Tiers and the Quality parameter (I17)

- `Tier` gains `HQ` (= 2): Eco 0, Normal 1, HQ 2, matching the parameter's values. `Tier` is
  latched per grain at its onset, as M1's ruling 2 says; a change reaches only the grains that
  start after it. The pool's `tier` row, the renderer's dispatch, `GrainDraw::tier` and
  `CloudParams::quality` all take the third value.
- **Quality** is a per-layer stepped parameter, index `0x50` in scopes 1 to 4 (`A Quality` to
  `D Quality`), values 0 Eco, 1 Normal, 2 HQ, default 1 in both products. `ParamMap` maps it to
  the layer's `CloudParams::quality`. It is a mod-matrix target like Freeze (stepped: the
  effective value's nearest step); no exception is made.
- **The ladder may lower a grain's tier below its layer's Quality only under overload** (§6.5).
  Nothing else changes a grain's tier.
- **The mod matrix's target rule** (the programme manager's ruling PM-1, 2026-10-30, found while
  reviewing sub-project 4's spec): `isValidTarget` admitted a layer parameter only below
  `LFO 1 Rate`, so every wave-2 layer parameter, Quality included, would have been silently
  refused as a route target. Since this sub-project merges first in wave 2, the fix is its:
  the layer rule refuses the modulators' own controls by name (the LFO, Env, Random and Step
  parameters, `isModulatorControl`) and admits every other layer parameter, with a test that a
  route to Quality is accepted and a route to `LFO 1 Rate` refused. Sub-projects 2 and 4 rely on
  it after rebasing.
- `gg-render`'s sample mode takes `"quality": "hq"` beside `"eco"` and `"normal"`.

### 6.2 The HQ kernel: the windowed sinc (I17)

**The reader.** A third reader policy beside `Hermite` and `Linear` in `GrainKernels.h`:
`Sinc` with `kTaps` = 16 (D3), `kBefore` = kTaps/2 − 1,
`kAfter` = kTaps/2. For a read at position `pos` with fractional part `frac` and stretch
`s` ≤ 1:

```
x_k  = (k − frac) × s                      for k = −kBefore … kAfter      (double)
c_k  = T(x_k)                              the table, linearly interpolated (float)
y    = (Σ_k c_k × tap_k) / (Σ_k c_k)        both sums in k order, float
```

- **The table `T`** (`SincTable`, like `HannTable`): the windowed sinc
  `sinc(x) × w(x / (kTaps/2))` for |x| ≤ kTaps/2, tabulated at 512 points per unit (8,193
  points for 16 taps, 32 KiB of floats), computed once at construction in double with `std::sin`
  and the window, as `HannTable` computes with `std::cos`, so the audio thread only reads it. `w`
  is a Kaiser window; its β is the builder's, chosen so that at s = 1 the passband is within
  0.1 dB to 0.4 of the source's Nyquist and the first image is at least 70 dB down at 16 taps
  (60 dB at 12). The normalization by Σ c_k keeps DC exact and makes the stretched kernel's gain
  independent of `s`.
- **The stretch** `s = min(1, 1 / r)`, with `r` the grain's residual rate at its mip level
  (§6.3): `s` = 1 for a downward or unity transposition (the kernel's cutoff is the source's
  Nyquist) and `1/r` ∈ (0.5, 1] for the residual upward rate, so the cutoff follows the rate and
  nothing above the output's Nyquist is read in. `s` is a per-grain constant, latched with the
  grain, and `1/r` is one IEEE division.
- **The scalar reference** `renderSampleRef<Sinc>` and `renderLiveRef<Sinc>` define the bits as
  M1's ruling 1 says: the SIMD kernels `renderSampleSimd<Sinc>` and `renderLiveSimd<Sinc>` must
  match them by `std::memcmp`, through the same tests the Hermite and Linear kernels pass
  (`kernel_tests.cpp`'s template cases gain `Sinc`), at the source ends, at extreme rates and
  lengths, for every output shape, with non-finite taps, through freeze and wrap, and at every
  mip level and stretch.
- **The sample kernel** gathers 16 taps per lane as the Hermite gathers 4: from the interleaved
  pairs for a stereo source (two 4-float loads become eight), from the planar channel otherwise.
  How the gather is laid out is the builder's, within bit identity.
- **The live kernel.** The ring's slots mirror `kGuardFrames` = 3 slots past its capacity so 4
  taps are contiguous; the sinc needs 15. This sub-project widens `LiveSource::kGuardFrames` to
  `kTaps − 1` and adds `LiveSource::readSinc(pos, head, cursor, s, l, r)`, which reads nothing
  unless all kTaps taps are inside the window, as `read` does for its four. Nothing else in
  `LiveSource` changes; it stays sub-project 2's. The HQ live grain's head-proximity fade uses
  the sinc's reach: `g = clamp((R − 1 − (p + kAfter)) / M, 0, 1) × clamp(((p − kBefore) − (R −
  N)) / M, 0, 1)`, so `g` is 0 before any tap leaves the window, as the Hermite's fade is for its
  reach. It is computed in the kernel (`kernels::Sinc::fade`, the same expression with the
  sinc's reach); `LiveSource::fade` and the Hermite and Linear kernels' calls to it are
  untouched. The admission clamp keeps every Hermite tap M = 256
  frames inside the window over the grain's life, so every sinc tap is at least M − 6 inside and
  the HQ fade is above 0 wherever the Hermite's is, by at most 6/256 less.
- **Cost.** M0 measured the scalar 16-tap sinc at 8.6 ns per stereo grain-sample against the
  SIMD Hermite's 2.1 (INTENT §16); the SIMD sinc is expected between 3 and 5 ns. Its weight in
  the table (§6.4) is set from `engine-sample-hq`'s measurement at the build's end.

### 6.3 Mipmaps: the file pyramid and the level per grain (I16)

- **The pyramid** lives in `PreparedSource`, built in `make()` off the audio thread: level 0 is
  the source as today (the planar buffer and, for stereo, the interleaved pairs); level `n` ≥ 1
  holds the source lowpassed and decimated by 2ⁿ, **one copy per level**: interleaved pairs for a
  stereo source, the planar channel for mono, so the pyramid costs about one more copy of the
  source's audio (Σ 1/2ⁿ), a stereo source going from 2× to 3× its audio and a mono one from 1×
  to 2×. A 10-minute stereo 48 kHz file (230 MB of audio) is then about 690 MB in memory instead
  of today's 460 (the memory cap, sub-project 2's, counts it: `PreparedSource` reports its
  bytes). The level accessor is
  sub-project 1's: `levelCount()` and `level(n)`, `level(n)` for `n` ≥ `levelCount()` returning
  level 0. A `Level` gains the channel stride its data is read with (1 for planar, 2 for pairs),
  which the scalar reference reads through too (`interp` readers take a stride), so the
  reference and the SIMD interior read the same floats of every level.
- **Level count:** `1 + floor(log2(max rate))` for the engine's rate range, 5 levels
  (decimations 1, 2, 4, 8, 16) for ±48 semitones, and fewer for a short source: a level is built
  only while it would hold at least `kMinLevelFrames` = 64 frames; level `n` has
  `ceil(frames / 2ⁿ)` frames. `kMaxLevel` = 4: a rate above 32 (a 96 kHz file at +48 semitones
  into a 48 kHz host) reads level 4 stretched by `s` < 0.5, bounded but with a shorter effective
  kernel, an accepted edge.
- **The halfband:** each level is the one above it filtered by a linear-phase half-band FIR and
  decimated by 2 (every other output computed; the FIR's odd taps are zero but the centre). The
  design targets a passband to 0.4 of the level's new rate within 0.1 dB and 80 dB of stopband
  from 0.6 of it; a Kaiser-windowed half-band of about 31 taps meets it. Its taps are a named
  constant; the exact count and window are the builder's (§15). Plain float arithmetic in a
  fixed order, so one platform gives one set of bits. Building a level costs frames × (taps / 2)
  multiplies, about a second of main-thread time for a 10-minute stereo file at every level
  together; a synchronous session restore (INTENT §18, D8) pays it, recorded in §14.
- **The level per grain.** At spawn the engine latches `level = clamp(floor(log2 rate), 0,
  kMaxLevel)` from the grain's rate in level-0 frames per output sample (`frexp`, exact), into a
  new pool row. At render the kernel asks the grain's source for `level(level)` and reads the
  level it gets, with its actual decimation `d` = 2ᴸ: the read position is `pos₀ × 2⁻ᴸ` where
  `pos₀ = startPos + rate × e` is the level-0 position computed exactly as today, and the
  residual rate is `rate × 2⁻ᴸ`, so a source that has fewer levels (a short file, the live ring
  until sub-project 2, a replacement source) is read correctly at whichever level it offers,
  positions and rates scaled by an exact power of two. **The sounding grains keep their level
  index** through a source swap, as they keep their slot.
- **Only HQ grains read above level 0** (D5, option 1): Eco and Normal grains latch level 0
  whatever their rate, so every M1 golden's bits hold. At HQ a grain at rate 1.9 reads level 0
  stretched (`s` = 1/1.9); at rate 2.0 it reads level 1 at residual rate 1 with no stretch; at
  rate 3 it reads level 1 at 1.5, stretched.
- **Determinism:** the level depends on the rate alone; the read on the level's bits and the
  exact scaling; the pyramid on the source's bits and a fixed filter. INTENT §19's "interpolators
  (mipmaps from M2)" unit tests cover the halfband's response, the level count by length, the
  level choice by rate, the exact position scaling and the accessor's fallback.

### 6.4 The budget manager: weights, budget and quotas (I14)

**The weight table.** Each sounding grain costs a fixed integer weight per output sample, by tier
and the channel count of what it reads, a named constant `kGrainWeights[tier][channels]`:

| Tier | Mono source | Stereo source |
|---|---|---|
| Eco | 2 | 3 |
| Normal | 3 | 4 |
| HQ | 9 | 12 |

Normal stereo, X's case, is the unit of 4, so a budget "of N grains" means 4N weight units. The
table's values are provisional: Eco's come from M1's headline run (1.33 against 2.03 ns at block
256, 0.65 of Normal), HQ's are set from `engine-sample-hq`'s measurement at the build's end and
the mono column from a one-off measurement, each rounded to an integer; M5 calibrates them. The
weights never change at run time.

**The budget** is a global parameter, **Budget**, index `0x000050`, in both products (D1,
option 1): an integer count of Normal stereo grains, 256 to 16,384, stepped, default 4,096,
saved in the state like any parameter, automatable and a mod target like any other (nothing is
special-cased). Its effective value `clamp(base + mod + Σ routes)` is read on the grid. 4,096
Normal grains are about 40% of one M5 Max core at 48 kHz (X scaled) and 100–130% of a CI floor
core: a ceiling for the reference machine, which a session on a weaker machine lowers and
carries with it. In M2's plugin ranges one layer reaches at most 800 grains (Density 400 Hz ×
Size 2 s; Size Spread doesn't raise it), so four Normal layers (3,200 grains, 12,800 units) fit
the default and the ladder engages only with HQ layers (four at 12 units each, 38,400 units) or
a lowered Budget; M3's voices will reach it at Normal.

**The quotas** (D2, option 1). At every grid boundary where the Budget's effective value or the
set of rendering layers changes, the `Processor` gives each rendering layer's engine an equal
share: `quota_L = floor(4 × Budget / n)` weight units, `n` the layers rendering (a layer's
on-ramp target or value not 0, the same rule that dispatches its task). A layer that isn't
rendering has no quota and no grains. The quota applies from the engine's next call, which the
`Processor` cuts at that boundary as it cuts pieces for every other change, so it is block-size
invariant and the same with any pool: the quotas are computed serially before the tasks run,
from state known at the boundary, never from what the other tasks admit. `Engine::setQuota(units)`
is real-time safe, with `setCloudParams`'s threading rule; an engine with no quota set has
`kUnlimitedQuota`, so M1's engine tests and `gg-render`'s sample mode behave as before.

**The ledger.** Each engine's pool keeps the weighted load `W(t)` = Σ weights of the grains
whose end is after `t`, exactly, in the admission heap: each entry's key is a grain's end and its
payload its weight, and a running sum is kept as entries are pushed and popped. `canAdmit`
becomes `admissionRoom(onset)`: it first forgets every end ≤ onset, subtracting their weights, and
returns the room, `quota − W(onset)`, and whether the pool has a grain's worth of storage left.
Row 51's second half: admission counts only the grains whose end is after the onset, never
`pool_.size()`, as M1's count already did; now it is weighted.

### 6.5 The overload ladder: tier, drop, steal (I14; row 51)

For a draw at onset `o` with its layer's tier `q` (Quality, or the layer's mod-moved value) and
channel count `c`, with `w(t) = kGrainWeights[t][c]`:

1. **Admit at the layer's tier** if `W(o) + w(q) ≤ quota` and the pool has room.
2. **Lower the tier** (INTENT §14, rung 1): otherwise, the highest tier `t < q` with `W(o) + w(t)
   ≤ quota`, Normal before Eco; the grain latches `t`. An Eco layer has nothing lower.
3. **Steal, or drop** (rungs 2 and 3, in their order by the threshold): if no tier fits, or the
   pool's capacity is reached, look at **the oldest sounding grain** (the first stored grain in
   spawn order whose end is after `o` and that hasn't been stolen already: a stolen grain is
   already yielding, and a second steal would restart its fade with a jump). If it has sounded
   for at least **half its window** (D4,
   option 1: `o − onsetSample − onsetFrac ≥ 0.5 × durationSamples`) and its weight makes room for
   some tier `t ≤ q` (`W(o) − w(old) + w(t) ≤ quota`), **steal it**: its end moves to `o + F`
   with `F = min(kStealFade, oldEnd − o)` and `kStealFade` = 256 samples, the new grain is
   admitted at the highest such `t`, and one steal event is reported. Otherwise the new grain is
   **dropped** and counted, and nothing is stolen. At most one grain is stolen per admission, so
   the ladder's work per draw is O(log capacity) plus a bounded scan for the oldest sounding
   grain (the engine keeps a cursor that only advances within a block; finished grains are
   skipped, retired grains are gone).

So under mild overload the new grains come in cheaper; under moderate overload, while the
oldest grains are still young, the new ones are dropped; under sustained overload the oldest
grains yield with fades, one per new grain, so the stream keeps refreshing at its onset rate
rather than freezing on whatever got in first. Each rung is reachable and the order is §14's.

**The fade.** A stolen grain's window is multiplied by `clamp((end − t) / F, 0, 1)` from the
steal on, with `end` its new end: 1 at the steal (the grain is at least half-way through, so its
window is already falling) and 0 at `end`, a linear fade over F samples, shorter only when the
grain would have ended sooner. The pool stores the fade's start and 1/F per grain (`INT64_MAX`
and 0 for an unstolen grain, whose factor is exactly 1 and is skipped). The scalar references
define the multiply; the SIMD kernels match it bit for bit, the fade region being one more run of
the grain. An unstolen grain renders M1's bits.

**Accounting during a fade.** The stolen grain's weight stays in the ledger until its new end, so
for up to F samples both it and the new grain sound: the instance's real cost may exceed the
budget by one grain's weight per steal in flight, at most `kStealFade × onset rate` grains, 8% of
the default budget at the exit's 16,384 onsets a second. The excess is bounded and deterministic;
it is recorded in INTENT §14.

**Why it never clicks or drops out.** A tier change is per grain; a drop plays nothing; a steal
fades. The budget never silences a layer: once the oldest unstolen grain is half-way through, a
new grain of no greater weight than it can always be admitted by stealing it, so under sustained
overload the stream keeps playing, refreshed at its onset rate. The exit's case (16,384 one-second grains into one
engine of capacity 16,384 at a quota far below) renders continuous audio: every 100 ms window
above −60 dBFS over a full-scale sine source, the click score below the goldens' limit, the same
bytes at block sizes 1, 32, 64, 511 and 4096, and the same admitted, lowered, dropped and stolen
counts at each.

**Counters.** `Engine::droppedGrains()` stays; `loweredGrains()` and `stolenGrains()` join it,
and the `Processor` sums them per layer as it sums dropped events (ruling LS-T11a).

**Row 51's first half**, the mutable end: the pool's `endSample` row is written by the steal, the
heap entry's key is decreased through its handle (the pool keeps each row's heap position and
each entry's row, both moved by `moveRows`), and `earliestEnd_` is lowered to the new end, so
`retireFinished` and the sources' `lastEnd` (which only ever bounds from above) stay correct.
The pool's tests gain a brute-force check of the weighted room and of stealing against a
scanned model.

### 6.6 The pool to 16,384 (I15)

- `EngineConfig::grainCapacity` is validated at most `kMaxGrainCapacity` = 16,384 (today below
  2³¹). The pool's storage, 2 × capacity rows, is about 150 bytes a row with the new rows (level,
  fade start, fade scale, the heap handle), 4.9 MB at 16,384.
- **The plugins keep 4,096 per layer** (`ProcessorConfig::grainCapacity`, new, default 4,096;
  16,384 grains per instance across the four pools), since M2's plugin ranges reach 800 grains a
  layer and the default budget is 4,096 grains; M3's voices raise it where they need to. The exit's
  16,384 case runs in one engine at the ceiling, in the tests and in `gg-bench`.
- Admission at the pool's capacity is rung 3's (steal or drop), no longer M1's bare drop, so a
  full pool degrades the same way a spent budget does.

### 6.7 The `Processor` (shared; additions only)

- Reads **Budget** and each layer's **Quality** (§6.8), maps Quality to the layer's
  `CloudParams::quality`, and sets the quotas as §6.4 says, at the boundaries it already cuts.
- Passes `ProcessorConfig::grainCapacity` to each layer's engine; the fixed grain-event buffer per
  layer is sized from it as today.
- **The grain-event line** gains `"tier"` (0, 1, 2) after `"tex"` and `"lvl"` (the latched mip
  level) after it, and a steal is a line of its own, `{"i":k,"layer":L,"steal":o}` with `o` the
  steal's onset sample, emitted through a second sink callback (`GrainEventSink` gains a kind) in
  the layer's event order. One commit rewrites the 36 expected `.events.jsonl` by insertion and
  moves no WAV. The cross-platform gate then sees the ladder's every decision.
- The thread-pool invariance test gains an overload case (four layers over a small Budget, each
  at a different Quality) and requires the same bytes, grains, steals and counters through
  `SerialRunner` and the stub pools.

### 6.8 Parameters: the reserved block (programme design §7.1)

| Parameter | Scope, index | Kind | Range, default | Where |
|---|---|---|---|---|
| Quality | 1–4, `0x50` (`A Quality` … `D Quality`) | stepped | 0 Eco, 1 Normal, 2 HQ; **1** | both products; `GG_LAYER_PARAMS_M2_ENGINE` appended to `GG_LAYER_PARAMS` |
| Budget | 0, `0x000050` | stepped integer | 256 … 16,384 grains; **4,096** | both products; appended to scope 0's table after `LevelD` |

Five parameters, 223 in all (`tests/support/ParamTable.h` grows by five rows, the pinned reserved
block test now expects `0x50` used in every scope and `0x51`–`0x5F` free). The patch stays at
version 2: a loaded state without them takes the defaults, so an M1 or sub-project-1 session plays
as it did. The remote-controls pages are sub-project 6's; neither parameter joins a page here. The
AU order follows from the IDs. Nothing is frozen before M5.

### 6.9 `gg-render`

- Sample mode: `"quality": "hq"`.
- FX and instrument modes: `"A Quality"` … `"D Quality"` (0, 1, 2) and `"Budget"` (a count) as
  base values and automation points; `"Quality"` bare means layer A's, as the M1 names do.
- The event log's new fields and the steal line (§6.7).
- **Goldens** (seven; the cases in `tests/golden/cases/`, each 3 to 8 s):
  - `sparse-sine-hq`: M0's `sparse-sine` at HQ (sample mode): the sinc on the simplest case,
    every injected click caught.
  - `sweep-stretch-hq`: M0's `sweep-stretch` at HQ, an octave down: the sinc's smoothness in a
    slow stretch, against `sweep-stretch`'s Hermite by metrics and spectrogram.
  - `fx-hq-pitch-ladder`: layer A at HQ over a 50 Hz–20 kHz stereo sweep, Pitch stepped 0, +12,
    +24, +36 and +47 at 1 s intervals (levels 0, 1, 2, 3 and 3 with stretch): the anti-aliased
    transposition, the level in every event line.
  - `fx-quality-switch`: Quality stepped Eco, Normal, HQ, Normal at 1 s intervals over a 330 Hz
    sine at Pitch +7: the per-grain latch, no click at a switch.
  - `fx-overload`: four layers over a 330 Hz sine (a noise bed would hide clicks), Density 400 Hz,
    Size 500 ms, A and B at HQ, C Normal, D Eco, Budget automated 4,096 → 1,024 at 2 s → 256 at
    4 s → 4,096 at 6 s: the tiers lowering, then drops, then steals, the counts in the log; layer
    D switched off at 5 s and on at 5.5 s, so the quotas move with the rendering set.
  - `inst-overload`: the instrument, two notes, one layer at HQ with one-second grains at Density
    400 Hz, Budget 256: sustained stealing, the stolen tails audible as a refreshing texture.
  - `fx-hq-live-level0`: the FX's layer A, Live, at HQ and Pitch +12 over a 50 Hz–20 kHz sweep:
    the ring has one level, so the grain reads level 0 stretched at rate 2 (`"lvl":0` in every
    line); the case sub-project 2 regenerates when the ring's levels land, named for it.
  - Every case catches all sixteen injected clicks or is added to the click-injection table with
    its reason in the same commit (`fx-overload`'s dense sine cloud may hide some; the count is
    pinned there). Every one of the 36 existing cases keeps its WAV SHA-256.

### 6.10 `gg-bench` (programme design §6.3, §7.6)

Report-only beside X, in the default case list so CI's legs time them and the close's
`--make-baseline --merge` adds their entries:

- **`engine-sample-hq`:** `engine-sample` at HQ (2,000 sinc grains over the stereo noise in RAM,
  reading level 0 at `s` = 1): the HQ figure for INTENT §16, and the source of HQ's weight.
- **`engine-sample-hq-up`:** the same at Pitch +19 (rate 2.997: level 1 at residual 1.5, the
  stretched read through the pyramid), the mipmap path's cost.
- **`engine-overload`:** one engine at capacity 16,384 and a quota of 2,000 Normal grains,
  one-second grains asked for at 8,000 a second (four times the quota's worth), so every rung
  runs steadily: ns per *rendered* grain-sample, which shows the ladder's bookkeeping cost over
  `engine-sample`'s rendering.
- `engine-sample` is untouched in definition, and with the quota unlimited its admission path adds
  one integer add per grain: its ratio must not regress (§10). The `[bench]` case
  "engine-sample-hq runs at the HQ tier" checks `admittedTiers`; a new `TierCounts::hq` counts it,
  and `engine-overload`'s counts show every rung.

### 6.11 The sweep harness (I18)

A Catch2 family, tag `[sweep]`, in `tests/sweep/`, over `gg::core::Processor` in both products
and over `gg::Engine`, each case a few seconds of audio at 48 kHz with the block size drawn per
call (0, 1 and up to 4,096 among them):

- **Parameter sweeps:** every parameter swept over its whole CLAP range and a quarter beyond each
  end (the mapping clamps) in 64 steps over 2 s, while a cloud plays (layer A Live over a sine
  in the FX; a held note over a sine in the instrument), one case per parameter group (layer A's
  48 in four groups, the globals in two, layers B to D's as one group each with the layer on).
- **Extremes:** every parameter at its minimum, at its maximum, and 32 seeded random corners
  (each parameter at one end), each held for 1 s, with notes.
- **Automation bursts:** 1,024 seeded random parameter events in one call, repeated 16 times,
  with notes, controls and transport changes among them; then the same at block size 1.
- **Overload sweeps:** Budget swept 16,384 → 256 → 16,384 under four HQ layers at full Density
  and Size; Quality swept on each layer under a small Budget.
- **The checks,** on every render: no NaN or Inf (and every ring, every peak strip and every
  snapshot finite), the peak at most 2 + the input's peak (the chain's bound, INTENT §14), no
  runaway (the last second's RMS at most 6 dB above the loudest earlier second once the input
  has stopped changing), and the goldens' click score (`metricsVersion` 2, the limit 15) over the
  output, except across a Reverb Size move (row 217, accepted S8) and across the input's own
  edges, which the harness fades as `gg-render` does. A failed check names the parameter, the
  step and the sample.
- The family runs on every CI leg and under ASan and RTSan by being in `gg-tests`; it is sized to
  about two minutes in `dev` across its cases (the parallel preset spreads them) and `[slow]`
  marks none, so CI never skips it. The seeds are fixed; a failure reproduces with the case's name.
- The engine's own `random param sweep stays finite and bounded` stays as it is.

### 6.12 The AVX2 spike (I19; programme design §7.10)

- **Timebox:** one plan task, after the HQ kernel and the bench cases.
- **What it builds:** `GrainKernelsAvx2.cpp`, the sample and live kernels for all three readers
  instantiated 8 lanes wide (`__m256`, `<immintrin.h>`), compiled with `-mavx2 -mno-fma` (GCC,
  Clang) or `/arch:AVX2` (ClangCL; `-ffp-contract=off` already forbids contraction), x86-64 only.
  Each 8-wide operation is the 4-wide one on two halves, lane-wise, with no horizontal operation,
  so the bits are the SSE path's by construction; the kernel tests run both paths against the
  reference, and a test runs the two paths against each other on the same grains.
- **Dispatch:** one function table, set once before any engine renders (at static initialization
  or the first `Engine` construction, on the main thread) from `cpuid` (OSXSAVE, AVX2 and the YMM
  state), falling back to the SSE path; `GG_FORCE_SSE` (an environment variable read at that same
  moment, never on the audio thread) forces the fallback for tests and measurement. Rosetta's
  x86_64 slice on CI's macOS leg has no AVX2 and so tests the fallback.
- **The measurement:** `gg-bench` gains `engine-sample-sse`, `engine-sample` with the SSE path
  forced, beside `engine-sample` (dispatched), so one full-length CI run on each x86 floor leg
  (Windows ClangCL, Linux GCC 13; `gh workflow run ci.yml --ref m2-engine`, three runs each)
  gives both ratios against `reference` in the same rounds. **Kept** only if `engine-sample`'s
  ratio improves on `engine-sample-sse`'s beyond those runs' spread on at least one floor leg
  and regresses beyond its spread on none; then the x86 baselines are remade from three further
  runs and `engine-sample-sse` stays as the fallback's report-only case. **Dropped** otherwise:
  the TU, the dispatch and the case are removed, the measurements go in
  `docs/spikes/m2-avx2.md`, and §22 records the result.
- **The ruling** (INTENT §23, this sub-project's row): a kernel path chosen at load by CPU
  features, whose output is bit-identical to the fallback's, isn't CPU-adaptive behaviour under
  §14; what varies is speed alone, as it already does between a NEON and an SSE build.

## 7. Determinism and real-time safety

- Every ladder decision is a function of the onset, the grains' latched ends and weights, the
  quota in effect at the last grid boundary, and the weight table: no clock, load, thread or call
  boundary enters. Quotas change only at grid boundaries, serially, before the tasks run.
- The tier and level are latched per grain, the stretch and fade constants with them; a grain
  renders the same bits in any call that covers it.
- Positions stay 64-bit; the level scaling is an exact power of two in double.
- The sinc table and the pyramid are built off the audio thread; the audio thread reads them.
- The handle heap, the cursor and the counters allocate nothing; the `[rt]` tests put the tripwire
  on overload at every rung and on HQ grains, and the RTSan build runs them.
- The AVX2 dispatch is decided once, off the audio thread; both paths render the same bits.
- No `std::` distribution, no fast-math, no contraction (the AVX2 TU included).

## 8. Decisions for the director

Each a forced choice; the recommended option is first, with one line on what each costs. Nothing
engineering-only is here (that is §9).

**The director's answers, 2026-10-30, relayed by the programme manager:** every recommended
option: D1 option 1, D2 option 1, D3 option 1, D4 option 1, D5 option 1. Each is marked below
and was already applied in §6.

**D1. The budget: a parameter, and its default** (§6.4). **Decided: option 1.**
1. **A global "Budget" parameter, 256 to 16,384 Normal grains, default 4,096,** saved with the
   session. *Cost:* one more global control to explain (sub-project 6 shows it); at the default,
   four HQ layers at full Density and Size degrade (by design), while anything at Normal in M2's
   ranges fits.
2. *The same parameter, default 16,384:* the ladder is unreachable in M2's plugin ranges at any
   tier until the user lowers it. *Cost:* the ladder goes unheard in normal use until M3, and a
   weak machine glitches before it degrades.
3. *A fixed constant of 4,096, no parameter.* *Cost:* no session can raise or lower its ceiling
   for its machine; the only bound is the pool.

**D2. How layers share the budget** (§6.4). **Decided: option 1.**
1. **Equal shares among the layers that are rendering,** recomputed when one switches on or off.
   *Cost:* switching a second layer on halves the first's ceiling, audible only under overload; a
   sparse layer's unused share is wasted.
2. *A fixed quarter each, always.* *Cost:* one layer alone reaches only a quarter of the budget.
3. *Each layer gets the whole budget.* *Cost:* four layers may cost four budgets; the bound is per
   layer, not per instance, which the programme design asked for.

**D3. The HQ kernel's length** (§6.2). **Decided: option 1.**
1. **16 taps.** *Cost:* about a third more per HQ grain than 12 taps (weight 12 against 9); at
   16 taps the first image sits at least 70 dB down and the passband holds to 0.4 of Nyquist.
2. *12 taps.* *Cost:* images about 10 dB higher and a softer top octave, for a quarter less CPU.

**D4. When the ladder steals rather than drops** (§6.5): the oldest sounding grain is stolen, with
a 256-sample fade, only once it has sounded for this share of its window; before that, new grains
are dropped. **Decided: option 1.**
1. **Half.** *Cost:* under sustained overload a grain's second half is cut by a fade; the stream
   refreshes at its onset rate once the oldest grain is past its peak.
2. *Ninety percent:* only tails are ever cut. *Cost:* under overload most new grains are dropped
   and the texture sits on whatever got in first; long grains starve the stream for seconds.
3. *A quarter:* steal eagerly. *Cost:* grains are cut off during their swell, so overload sounds
   choppy rather than thinned.

**D5. Which tiers read the mip pyramid** (§6.3). **Decided: option 1.**
1. **HQ only:** Eco and Normal keep reading the full-rate source, as in M1. *Cost:* Normal still
   aliases above rate 1 (an octave up on a bright file folds its top octave), and HQ is the only
   anti-aliased tier; every M1 golden keeps its hash.
2. *Every tier:* Normal and Eco also pick a level from their rate. *Cost:* about fifteen of the 36
   goldens (every case with a grain above rate 1) move in one commit, Normal costs a little more
   at rate > 1, and M1's pitched-up sound changes; the tiers then differ by kernel alone.

## 9. My calls

Engineering choices, each with its reason.

- **The quota is enforced in the `Engine`, computed in the `Processor`** (§5): the tasks stay
  independent, so no pool or thread can change a bit, and M1's engine tests run unchanged with
  the quota unlimited.
- **Weights are integers, the budget in weight units** (§6.4): integer sums are exact and
  identical everywhere; a float ledger would drift with summation order.
- **One steal per admission, the oldest sounding grain** (§6.5): bounded work per draw, a 1:1
  refresh rate under overload, and nothing is stolen when the new grain is dropped anyway.
- **The steal fade is 256 samples** (`kStealFade` = `LiveSource::kHeadMargin`): the same length as
  the head-crossing fade, so the engine has one fade length; it's shorter than Size's floor only
  for grains under 5 ms, where `F` shrinks to what's left.
- **The stolen grain's weight stays in the ledger until its new end:** honest accounting (both
  grains do sound during the fade); the overshoot is bounded and recorded.
- **A handle heap, not residues** (§5): the heap stays within the pool's storage under any steal
  rate.
- **The level is latched at spawn, the position scaled at render** (§6.3): a source with fewer
  levels, a swap and the ring's one level all read correctly, with no per-grain re-latching when a
  source changes.
- **One copy per pyramid level** (planar for mono, pairs for stereo): half the memory of two
  copies; the reference reads a stride, which costs it nothing it didn't already pay.
- **A continuous, stretched sinc table** rather than per-rate coefficients (§5): one lookup, one
  reference, the cutoff following the residual rate.
- **The sinc live read widens the ring's guard to 15 frames** rather than reading with wrap
  checks per tap: a 12-frame change to the ring's storage, no output change, no per-tap branch.
- **`kMaxGrainCapacity` = 16,384 and the plugins at 4,096 per layer** (§6.6): INTENT's figure as
  the engine's ceiling, tested there; the plugins' pools match what M2's ranges and default budget
  can use, and M3 raises them with the reason.
- **The event line gains `"tier"` and `"lvl"`, and steals get a line:** the ladder's decisions
  must be in the cross-platform gate, as the grains are.
- **The pyramid is built for every file at load, HQ or not:** Quality changes on the audio
  thread, which can't build it; the cost is memory and load time, both recorded.
- **The AVX2 spike measures both paths in one process** (`engine-sample-sse`): CI's ratios drift
  by more than their in-run spread between runs (§23 #47), so a cross-run A/B would prove nothing.
- **The sweep harness lives in `gg-tests`:** it runs where the sanitizers run, with no new CI
  wiring.
- **Rejected, recorded:** a per-machine budget in the settings file (it breaks INTENT §8.8); a
  fill threshold that lowers tiers before the budget is spent (the brief says below Quality only
  under overload); pre-emptive stealing of several grains per admission (unbounded scan or
  wasted steals).

## 10. Verification

- **Unit tests** (`engine`, `pool`, `kernel`, `interp`, `source`, `core`, `layers`, `threads`,
  `plugin`, `rt`, `determinism`, `golden`, `metrics`, `bench`, `sweep`):
  - `Tier::HQ` everywhere a tier is read; the Quality parameter's mapping, text and state
    round trip; Budget's; the five rows in `ParamTable.h`; the reserved block's pin.
  - The sinc table against a double-precision model; the reference's arithmetic against a
    spelled-out loop (as `kernel_tests.cpp` does for Hermite); the SIMD sample and live kernels
    against the reference by `memcmp` in every existing template case plus every mip level and
    stretch; the HQ live fade's reach; the widened guard.
  - The halfband's response; the level count by length; the level per rate; exact position
    scaling; the accessor's fallback with fewer levels; the pyramid's byte count.
  - The weighted ledger against a brute-force model; stealing (the end moves, the heap stays a
    heap, `earliestEnd_` and `retireFinished` agree); the cursor; each rung reached, counted and
    reported; the thresholds at exactly half; the fade's bits against the reference; the quota's
    unlimited default.
  - **The exit's case:** 16,384 one-second grains into one engine of capacity 16,384 at a quota
    of 1,024 grains, over a full-scale sine, at block sizes 1, 32, 64, 511 and 4096 and random
    splits: identical bytes, identical counters, every 100 ms window above −60 dBFS, the click
    score below 15, no NaN. The `Processor` version with four layers, each at a different
    Quality, over a small Budget, through `SerialRunner` and stub pools of 1, 2 and 8.
  - The `[rt]` tripwire on every rung, on HQ grains at every level, on the quota change and the
    Quality change; the RTSan build runs them.
  - The `[plugin]` tests: the five parameters through the CLAP factory; a state without them loads
    with the defaults; the AU state test still passes (no identity change).
  - The `[sweep]` family (§6.11), on every leg and under the sanitizers.
- **Goldens:** the seven of §6.9, each caught by the click injection or pinned with its reason;
  `scripts/update-goldens.sh` run before and after every DSP change, the hash list in the commit,
  **every one of the 36 existing cases' WAV SHA-256 unchanged** at every task, except that the
  event-log insertion commit rewrites the `.events.jsonl` files alone.
- **Benchmarks:** `engine-sample` with no `regressed` ratio against the committed baselines on
  every CI model (a full-length run via `gh workflow run ci.yml --ref m2-engine`), and its M5 Max
  figures within 3% of the headline run's (the ruling R3 bar); `engine-sample-hq`,
  `engine-sample-hq-up` and `engine-overload` measured on the M5 Max from `release` with the
  load average recorded, and reported here and in INTENT §16 as this sub-project's figures (the
  close republishes idle).
- **The AVX2 verdict** with its three runs per leg and the spread (§6.12).
- **The gate:** `scripts/check.sh` over `dev,asan,rtsan` passes (summary pasted in the PR); CI
  green on the head commit via the draft PR; the validators on `release` unchanged (no bundle
  change); `ctest -L au` and the identity tests (no clap-wrapper change, but they run).
- **The record:** the pack rendered (§11); the spec's status Built with amendments marked; INTENT's
  rows in; the carry-over at 10 open rows (§12); a final review by a fresh reviewer subagent and
  its fix wave.

## 11. The listening pack: `docs/listening/m2-engine/`

Rendered for the record, heard at the close (decision 1). Specs in `specs/`, a README in the
form of `docs/listening/m2-layers/README.md` (how to render, the measured peaks and RMS, what to
listen for, a host checklist), and no committed audio.

| File | What it is | Listen for |
|---|---|---|
| `01-three-tiers.wav` | The FX over a bright stereo loop (`loop-48k.wav`'s bursts under a 2 kHz–16 kHz sweep), Pitch +7, Size 80 ms, Density 48 Hz, Quality stepped Eco, Normal, HQ, 3 s each | The tiers as characters: Eco's dull top and grit, Normal's M1 sound, HQ's clean top with the aliasing gone |
| `02-hq-transposition-ladder.wav` | HQ and Normal side by side (A at HQ, B at Normal, the pad alternating every 2 s) over the sweep, Pitch stepped 0, +12, +24, +36 | At each step the Normal half folds the sweep's top back down; the HQ half doesn't |
| `03-slow-stretch.wav` | The instrument, one note, 200 ms grains at rate 1/16 (Pitch −48) over `tone-44k1.wav`, Normal for 6 s then HQ for 6 s | The Hermite's slow-stretch image tones against the sinc's |
| `04-overload-ladder.wav` | Four HQ layers over a 330 Hz sine at Density 400 Hz and Size 500 ms; Budget 4,096 for 3 s, 1,024 for 3 s, 256 for 3 s, 4,096 for 3 s | The texture first softens (tiers lower), then thins (drops), then churns (steals), then returns; no click anywhere |
| `05-steal-tails.wav` | One HQ layer, one-second grains at Density 200 Hz, Budget 256, the instrument, two notes | The stolen tails as a continuous, refreshing texture rather than a stuck chord |
| `06-quotas.wav` | Budget 512, layer A at Density 400 Hz; layer B on at 3 s and off at 6 s | A thins when B takes its half, and recovers |
| `07-live-hq.wav` | The FX, layer A Live at HQ over the sweep, Pitch +12 | The ring has one level until sub-project 2: HQ reads it stretched at rate 2; the top folds less than Normal's but isn't the file's anti-aliasing of `02` |

Two questions for the sitting: whether HQ earns its weight on real material, and whether D4's
threshold sounds right under sustained overload.

## 12. The carry-over

**11 open rows at this sub-project's start** (`docs/m2-carry-over.md`: 51, 85, 87, 88, 144, 148,
184, 217, 218, 221, 232).

**Closed here:** **51** (the overload ladder's hazards: the mutable end that keeps `earliestEnd_`
correct, and admission counting only the grains whose end is after the onset) by §6.5 and §6.4,
with the commit named in the row.

**Touched files with open rows:** `engine/include/gg/grain/GrainPool.h` and `engine/src/Engine.cpp`
(51, closed above). `core/include/gg/core/Processor.h` (184: M3's Voice scheduler, not a
when-touched row). No other open row names a file this sub-project edits; 217 (`Reverb2Patched.h`,
the FX view) and the upstream rows wait where they are.

**Expected at the end: 10 open rows,** no new rows. Every minor the final review raises is fixed in
the fix wave unless blocked outside our control or genuinely large (the policy).

## 13. INTENT changes (Part II, with one §23 row in the same change)

- **§14:** the grain pool (the ceiling, the plugins' 4,096 per layer); the kernels' three tiers
  with HQ built (the sinc, its taps, the stretch); the deterministic CPU budget as built (the
  weight table, the Budget parameter, the quotas, the ladder's three rungs with D4's threshold,
  the fade, the bounded overshoot); mipmaps as built for files (the pyramid, one copy per level,
  the level per grain, HQ only per D5, the ring's level until sub-project 2); the event line's
  fields and the steal line.
- **§16:** `engine-sample-hq`, `engine-sample-hq-up` and `engine-overload` beside X, report-only,
  with this sub-project's M5 Max figures; the AVX2 outcome under SIMD.
- **§18:** the five parameters in the table, 223 in all; the reserved block's use.
- **§19:** the seven goldens; the sweep harness as built; the event line's `"tier"` and `"lvl"`;
  the overload ladder's block-size invariance test; the mipmap unit tests.
- **§21:** the memory risk row names the pyramid's cost.
- **§22:** the AVX2 row resolved either way, with the measurement.
- **§14 (the mod matrix's targets):** the layer rule as amended by PM-1 (§6.1).
- **§23 #66:** this sub-project's director decisions (D1–D5, with their dates), the programme
  manager's ruling PM-1 (the mod matrix's target rule), the AVX2 ruling,
  its rulings made during the build, and the §9 calls that amend Part II (the plugins' pool
  size, the overshoot, one copy per level).

Part I is unchanged.

## 14. Risks

| Risk | Mitigation |
|---|---|
| The SIMD sinc costs more than 5 ns per grain-sample, so HQ is a niche tier | The weight table carries whatever it measures; HQ is opt-in per layer and the budget bounds it; the figure is published honestly |
| The pyramid's memory and load time on long files | One copy per level (§6.3), the byte count reported for sub-project 2's cap, the load cost recorded; levels stop at 64 frames |
| The handle heap and the steal complicate `GrainPool` | A brute-force model test for the ledger and the steals; the exit's case at five block sizes; the `[rt]` tripwire |
| The ladder changes an M1 golden's bits | The quota is unlimited by default in the engine and far above every golden's load in the `Processor`; the hash list before and after every task |
| `engine-sample` regresses from the weighted admission | One integer add per draw; measured on the M5 Max and the CI models before the PR is marked ready; fixed here, not carried (§7.6) |
| The sweep harness takes too long under ASan | Many small cases run 8 at a time; each case's audio is seconds; the family's wall time is measured in the plan and trimmed before CI sees it |
| The AVX2 path isn't bit-identical on some compiler (a contraction, a different `sqrt`) | `-mno-fma` and `-ffp-contract=off`; the two-path equality test on every x86 leg; dropped if it can't be made identical within the timebox |
| Sub-project 2's `LiveSource` work conflicts with the guard and `readSinc` | Both are additions at named places; 2 rebases on this merge, first in the wave |
| The fade's bounded overshoot surprises a reviewer | Recorded in §6.5, §14 and INTENT; measured in `engine-overload` |

## 15. Left to the builder

- The Kaiser β and the half-band's tap count and window, within §6.2's and §6.3's targets, each
  pinned by a response test.
- The SIMD gather layout for 16 taps per lane, and whether a polyphase layout serves rate ≤ 1,
  provided the reference's bits are reproduced.
- The handle heap's exact structure (positions kept per row, rows per entry, both moved by
  `moveRows`), and the oldest-sounding cursor's reset rule.
- The pyramid's build order and buffer reuse, so a level is built from the one above it without a
  third copy alive.
- `GrainEventSink`'s shape with two event kinds, and how `gg-render` and the `Processor`'s
  per-layer buffers carry steals in order.
- The parameter groups and seeds of the sweep family, and its time budget per case.
- The AVX2 TU's build wiring per toolchain and the `cpuid` probe.
- The listening specs' exact values, voiced so each file makes its point audible.
- The order of the plan's waves: the tier and the Quality parameter first (everything else
  latches through them), then the pool and ladder beside the sinc and pyramid in parallel
  worktrees, then the `Processor` and `gg-render`, the goldens and the bench cases, the sweep
  family, and the AVX2 task last.
