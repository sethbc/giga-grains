# M1 spike: the four CPU levers

*M1 sub-project 2, Tasks 10 to 13, 2026-10-27 (dates and times are local, CDT). INTENT §16
gave sub-project 2 a fixed list of four optimization levers: try each, keep it if it wins, then
stop. This is the record of the four trials, the two ports that followed, and what the CI
numbers turned out to be able to show. The kernels they start from are Task 6's and Task 7's
4-wide SIMD kernels, which match the scalar reference bit for bit.*

## Verdict

- **One lever kept: the stereo-interleaved source, both halves** (the sample source and the live
  ring). On the M5 Max it makes `engine-sample` 7–9% faster and `engine-live` 9–13% faster.
- **The sample half is kept by a director's exception.** On the Windows leg the sample ratio
  regressed 1–2%, and Eco up to 7% at block 32, which §16's keep rule ("no leg regresses beyond
  its spread") would reject. Told of the M5's 7–9% sample win against Windows' 1–2% sample
  regression, the director decided to keep it, at 2× memory for a stereo source (INTENT §23
  #46).
- **Three levers rejected:**
  - per-grain software prefetch, first as built and again as a live-only port;
  - contiguous loads with shuffles, first as built and again as an arm64-only port;
  - cache-aware grain scheduling.
- **Optimization stops here** (§16). Sub-project 6 measures X and publishes it.
- **No lever changed a bit.** Every lever branch passed the `[kernel]` memcmp tests, the goldens
  and the block-size invariance tests unchanged, and its golden WAVs hashed the same as Task 5's.
- **The CI floor legs can't resolve a few percent between runs.** Unchanged code moved by up to
  ~20% between CI runs on Windows, when the two runs landed on different CPU models. See "CI
  variance".

## Method

**What is compared.** Each lever's in-process ratio against its base's: an engine case's ns per
grain-sample over the frozen `reference` kernel's, timed in the same round and taken as the
median over the rounds. The ratio cancels the machine's speed. The cases are `engine-sample` (X,
the headline), `engine-sample-eco` (X at the Eco tier) and `engine-live` (the same stream over
the live ring, the FX's figure), each at blocks 32, 64 and 256. A table cell is the lever's ratio
over the base's: below 1 is faster.

**The base.** The four levers were built in parallel, each on `1bd948a` (Task 7's head), and each
was judged against it. After the interleaved source was merged, the two ports were built on
`8eb0613` (the interleaved kernel plus a doc-only commit) and judged against that.

**On the M5 Max** (the director's machine; see `docs/spikes/m0-kernel-benchmark.md` for its
specification):

- the `release` preset, AppleClang 21.0.0.21000334, native arm64;
- `gg-bench --cases reference,engine-sample,engine-sample-eco,engine-live --json`, at full
  length: 5 rounds of at least 2 s;
- the base's and the levers' binaries run interleaved, 3 cycles, with the order rotated each
  cycle, 22:10–22:45 CDT. The ports' judging ran 4 cycles of base and port, 23:06–23:20 CDT,
  without `engine-sample-eco`;
- no builds or tests ran, but the director's desktop apps did (Firefox among them; load average
  about 4.7);
- a cell is the median over the cycles of each run's ratio, over the same for the base. It is a
  **WIN** or a **LOSS** when the change is beyond the spread. The spread is the largest of: the
  base's in-run IQR, the lever's in-run IQR (each the median over the cycles), and the base's
  range from cycle to cycle. `~` means within the spread.

**On CI:**

- one full-length CI run per lever branch, each leg's `gg-bench.json` artifact against the same
  leg's in the base's run;
- a cell is the lever's median ratio over the base's. It is a **WIN** or a **LOSS** when the
  change is beyond the larger of the two runs' IQRs;
- the bases: the pre-lever run 40622875194 (`m1-cloud-engine` at `1bd948a`) for the four levers,
  and the interleaved lever's run 40624459703 (`a911b93`) for the ports.

**The keep rule** (§16): kept when the ratio improves beyond the spread on the M5 Max or on any CI
leg, and no leg regresses beyond its spread. Each source path is judged on its own case, so a
lever may be kept for the sample path or the live path alone.

The lever tables were generated from the downloaded artifacts and the local JSON by two short
Python scripts that apply exactly the comparisons above. The scripts and the data aren't
committed; the tables are their output. The one exception is the arm64 shuffle port's first
try, whose table comes from Task 12's own `--quick` runs (see "The two ports").

## Lever 1: per-grain software prefetch (Task 10)

**Mechanism.** `GG_PREFETCH(p)` is `__builtin_prefetch(p, 0, 3)`. In `Engine::renderPool`'s
spawn-order loops, before grain i renders, the engine prefetches the first tap of grain i + 8 at
its first sample in the block (both channels; on the ring, its slot). The distance D = 8 was
chosen from 4, 8 and 16 in rough M5 runs. One branch per grain per call; nothing changes inside
the kernels.

**Commit:** `dd41947`, branch `m1ce-lever-prefetch`. CI run 40624735018.

**M5 Max** (3 cycles):

| Case | Block | lever / base |
|---|---:|---:|
| `engine-sample` | 32 | 0.967 **WIN** |
| `engine-sample` | 64 | 0.984 ~ |
| `engine-sample` | 256 | 0.992 ~ |
| `engine-sample-eco` | 32 | 0.967 **WIN** |
| `engine-sample-eco` | 64 | 0.983 ~ |
| `engine-sample-eco` | 256 | 0.980 ~ |
| `engine-live` | 32 | 1.017 ~ |
| `engine-live` | 64 | 0.994 ~ |
| `engine-live` | 256 | 1.006 ~ |

**CI:**

| Case | Block | M1 VM | Windows | Linux |
|---|---:|---:|---:|---:|
| `engine-sample` | 32 | 1.138 ~ | 0.967 **WIN** | 1.004 ~ |
| `engine-sample` | 64 | 1.252 ~ | 0.991 **WIN** | 0.987 ~ |
| `engine-sample` | 256 | 1.081 ~ | 0.998 ~ | 1.027 **LOSS** |
| `engine-sample-eco` | 32 | 0.891 ~ | 0.987 **WIN** | 1.085 **LOSS** |
| `engine-sample-eco` | 64 | 1.000 ~ | 1.039 **LOSS** | 1.114 **LOSS** |
| `engine-sample-eco` | 256 | 0.994 ~ | 1.006 ~ | 1.137 **LOSS** |
| `engine-live` | 32 | 0.867 **WIN** | 0.957 **WIN** | 0.991 ~ |
| `engine-live` | 64 | 1.018 ~ | 0.990 **WIN** | 0.949 **WIN** |
| `engine-live` | 256 | 1.083 ~ | 1.001 ~ | 1.011 ~ |

**Verdict, per path.**

- **Sample half: rejected.** On the M5 it wins only at block 32, by about 3%. Windows Eco at
  block 64 regresses beyond its spread, and so do Linux's Eco and block-256 cells, by up to 14%.
  The Linux run landed on a different CPU model from the base's (see "CI variance"), so its
  losses are confounded. The Windows run didn't, and its Eco loss alone fails the keep rule.
- **Live half: kept by the rule, pending a port.** Windows wins at blocks 32 and 64, Linux at 64,
  and no leg loses, though the M5 shows nothing. The M1 VM's live WIN at block 32 (0.867) counts for
  nothing either way: that leg's drift between runs for unchanged code is larger (see the port's
  verdict). The ring's layout was about to change under the interleaved lever, so the live half was
  ported onto it and judged again (see "The two ports").

## Lever 2: a stereo-interleaved source (Task 11) — kept

**Mechanism.**

- **Sample source.** `Engine::setSource` builds, for a stereo source, an interleaved copy: L, R
  per frame, plus 3 zero guard frames at each end (`InterleavedStereo`). The SIMD sample kernel
  reads each lane's taps as one 8-float run (`Hermite::read4Pair`: two `loadu_ps` and two 4×4
  transposes; Linear: one load and one transpose), so a tap needs one address, not two. A mono
  source and a one-output render read the planar `SampleBuffer` as before.
- **Live ring.** `LiveSource` stores L, R per slot (slot s's channel c at `2s + c`), with the
  guard slots mirrored as before. The ring's size doesn't change.
- **Memory.** A stereo sample source costs 2× its memory: the bench's 60 s at 48 kHz is 23.04 MB
  planar plus a 23.04 MB copy, and each `Engine` holds its own copy. A mono source costs nothing
  extra.

**Commits:** `a911b93`, plus the doc-only `8eb0613` (`setSource` snapshots the source and may
throw), branch `m1ce-lever-interleave`. CI run 40624459703. Merged into `m1-cloud-engine`.

**M5 Max** (3 cycles):

| Case | Block | lever / base |
|---|---:|---:|
| `engine-sample` | 32 | 0.905 **WIN** |
| `engine-sample` | 64 | 0.913 **WIN** |
| `engine-sample` | 256 | 0.931 **WIN** |
| `engine-sample-eco` | 32 | 0.917 **WIN** |
| `engine-sample-eco` | 64 | 0.950 **WIN** |
| `engine-sample-eco` | 256 | 0.960 **WIN** |
| `engine-live` | 32 | 0.869 **WIN** |
| `engine-live` | 64 | 0.877 **WIN** |
| `engine-live` | 256 | 0.914 **WIN** |

**CI:**

| Case | Block | M1 VM | Windows | Linux |
|---|---:|---:|---:|---:|
| `engine-sample` | 32 | 1.095 ~ | 1.012 **LOSS** | 0.985 ~ |
| `engine-sample` | 64 | 1.098 ~ | 1.016 **LOSS** | 0.980 ~ |
| `engine-sample` | 256 | 1.059 ~ | 1.022 **LOSS** | 0.977 **WIN** |
| `engine-sample-eco` | 32 | 0.920 ~ | 1.072 **LOSS** | 0.924 **WIN** |
| `engine-sample-eco` | 64 | 0.921 ~ | 1.019 **LOSS** | 0.961 **WIN** |
| `engine-sample-eco` | 256 | 1.028 ~ | 1.002 ~ | 0.960 ~ |
| `engine-live` | 32 | 0.891 ~ | 0.916 **WIN** | 0.992 ~ |
| `engine-live` | 64 | 0.957 ~ | 0.906 **WIN** | 0.984 **WIN** |
| `engine-live` | 256 | 0.957 ~ | 0.861 **WIN** | 0.975 **WIN** |

**Verdict: kept, both halves.**

- **Live half: kept by the rule.** It wins on the M5 by 9–13%, and on Windows by 8–14%; no leg
  loses.
- **Sample half: kept by the director's exception.** It wins on the M5 by 7–9% (Eco 4–8%), but
  Windows regresses 1.2–2.2% at Normal and 7.2% at Eco block 32, beyond Windows' small in-run IQR.
  Both Windows runs ran on the same CPU model (EPYC 7763), so the loss can't be blamed on the runner
  mix. The keep rule would reject the sample half. The director, told that it wins 7–9% on the M5
  and regresses 1–2% on Windows, decided to keep it, at 2× memory for a stereo source (INTENT §23
  #46). The Eco loss at block 32 wasn't part of what the director was shown.
- The Linux run landed on a different CPU model from the base's (EPYC 9V45 against 7763), so its
  wins are confounded. The M1 VM's IQRs are too wide to call anything.

## Lever 3: cache-aware grain scheduling (Task 13) — rejected

**Mechanism.** `gg::TileScheduler`: per tile of 32 samples, visit the pool's grains in order of
their current source position (an index kept sorted by insertion sort between tiles), render
each into its own scratch row filled with −0, then add the rows into the output in spawn order.
−0 is the additive identity, so each sample sums the same terms in the same order and the bits
don't change. The scratch is preallocated in the `Engine` constructor: 2.3 MB at the default
capacity of 4,096.

**Commit:** `eb4819e`, branch `m1ce-lever-sched`. CI run 40624846402. Not merged.

**M5 Max** (3 cycles):

| Case | Block | lever / base |
|---|---:|---:|
| `engine-sample` | 32 | 1.177 **LOSS** |
| `engine-sample` | 64 | 1.279 **LOSS** |
| `engine-sample` | 256 | 1.398 **LOSS** |
| `engine-sample-eco` | 32 | 1.230 **LOSS** |
| `engine-sample-eco` | 64 | 1.398 **LOSS** |
| `engine-sample-eco` | 256 | 1.608 **LOSS** |
| `engine-live` | 32 | 1.196 **LOSS** |
| `engine-live` | 64 | 1.373 **LOSS** |
| `engine-live` | 256 | 1.579 **LOSS** |

**CI:**

| Case | Block | M1 VM | Windows | Linux |
|---|---:|---:|---:|---:|
| `engine-sample` | 32 | 1.299 ~ | 1.203 **LOSS** | 1.191 **LOSS** |
| `engine-sample` | 64 | 1.525 **LOSS** | 1.364 **LOSS** | 1.319 **LOSS** |
| `engine-sample` | 256 | 1.767 **LOSS** | 1.536 **LOSS** | 1.479 **LOSS** |
| `engine-sample-eco` | 32 | 1.139 ~ | 1.290 **LOSS** | 1.236 **LOSS** |
| `engine-sample-eco` | 64 | 1.430 **LOSS** | 1.467 **LOSS** | 1.412 **LOSS** |
| `engine-sample-eco` | 256 | 1.697 **LOSS** | 1.678 **LOSS** | 1.568 **LOSS** |
| `engine-live` | 32 | 1.112 ~ | 1.142 **LOSS** | 1.153 **LOSS** |
| `engine-live` | 64 | 1.421 **LOSS** | 1.377 **LOSS** | 1.308 **LOSS** |
| `engine-live` | 256 | 1.479 **LOSS** | 1.616 **LOSS** | 1.503 **LOSS** |

**Verdict: rejected.** It is slower on every leg, case and block size, by 11% to 77%, and loses
beyond the spread everywhere but the M1 VM at block 32. The loss grows with the block size. Each
grain-sample is written to a scratch row and then read back and added: a second pass over the
output, plus the sort, the tile bookkeeping and a kernel call per grain per tile instead of per
block. Whatever locality the ordering buys doesn't pay for that on any machine here. The plan
expected this. The task review was skipped, by ruling, because no leg won.

## Lever 4: contiguous loads with shuffles (Task 12) — rejected

**Mechanism.** In place of the per-lane gather, a 4-lane chunk loads a 12-float window from its
first tap (three 4-float loads) and selects each lane's taps by byte shuffles: NEON `vqtbl3q_u8`
through simde on arm64, and on x86 SSSE3 `pshufb` on each of the three vectors, OR-combined. It is
used per grain per call when the chunk's taps fit the window: Hermite below rate 8/3, Linear
below 10/3, which covers the bench's +7 semitones. Otherwise it gathers. Shuffles move bits, so
the taps and the output are the same.

**Commit:** `bb1f373`, branch `m1ce-lever-shuffle`. CI run 40625096040. Not merged.

**M5 Max** (3 cycles):

| Case | Block | lever / base |
|---|---:|---:|
| `engine-sample` | 32 | 0.935 **WIN** |
| `engine-sample` | 64 | 0.946 **WIN** |
| `engine-sample` | 256 | 0.969 ~ |
| `engine-sample-eco` | 32 | 1.003 ~ |
| `engine-sample-eco` | 64 | 0.988 ~ |
| `engine-sample-eco` | 256 | 1.016 ~ |
| `engine-live` | 32 | 0.913 **WIN** |
| `engine-live` | 64 | 0.906 **WIN** |
| `engine-live` | 256 | 0.927 **WIN** |

**CI:**

| Case | Block | M1 VM | Windows | Linux |
|---|---:|---:|---:|---:|
| `engine-sample` | 32 | 1.032 ~ | 1.146 **LOSS** | 1.272 **LOSS** |
| `engine-sample` | 64 | 1.072 ~ | 1.151 **LOSS** | 1.281 **LOSS** |
| `engine-sample` | 256 | 1.059 ~ | 1.173 **LOSS** | 1.302 **LOSS** |
| `engine-sample-eco` | 32 | 0.921 ~ | 1.228 **LOSS** | 1.183 **LOSS** |
| `engine-sample-eco` | 64 | 0.828 ~ | 1.216 **LOSS** | 1.201 **LOSS** |
| `engine-sample-eco` | 256 | 0.912 ~ | 1.231 **LOSS** | 1.199 **LOSS** |
| `engine-live` | 32 | 0.892 ~ | 1.052 **LOSS** | 1.393 **LOSS** |
| `engine-live` | 64 | 1.064 ~ | 1.061 **LOSS** | 1.453 **LOSS** |
| `engine-live` | 256 | 0.898 ~ | 1.075 **LOSS** | 1.486 **LOSS** |

**Verdict: rejected as built.** Every x86 cell loses, by 5% to 49%; both runs landed on the base's
CPU model (EPYC 7763), so this is a clean signal. The x86 route costs three `pshufb`, plus their
controls and two ORs, per vector of taps, where the gather makes four scalar loads; it was not
profiled. On the M5 it wins 5–9% on the planar layout it was built on. That layout was about to
change under the interleaved lever, so an arm64-only variant was ported onto the interleaved kernel
and judged again (see "The two ports").

## The two ports

After the interleaved source was merged (`a911b93`, `8eb0613`), the two levers that had a case
left were ported onto it, each on its own branch from `8eb0613`, and judged against the
interleaved kernel.

### Prefetch, live only (`b675ad9`, branch `m1ce-lever-prefetch2`) — rejected

**What changed from `dd41947`.** Only the live half: one prefetch per grain of the slot's L, R
pair, `ring.slots + 2 × slot`, since both channels now share the line, plus a prologue that
prefetches the first 8 grains before the loop. CI run 40629306377.

**M5 Max** (4 cycles, against `8eb0613`):

| Case | Block | port / base |
|---|---:|---:|
| `engine-sample` | 32 | 1.010 ~ |
| `engine-sample` | 64 | 1.010 ~ |
| `engine-sample` | 256 | 1.007 ~ |
| `engine-live` | 32 | 1.011 ~ |
| `engine-live` | 64 | 1.013 ~ |
| `engine-live` | 256 | 1.001 ~ |

**CI** (against run 40624459703):

| Case | Block | M1 VM | Windows | Linux |
|---|---:|---:|---:|---:|
| `engine-sample` | 32 | 0.821 ~ | 1.064 **LOSS** | 0.973 ~ |
| `engine-sample` | 64 | 0.944 ~ | 1.125 **LOSS** | 0.996 ~ |
| `engine-sample` | 256 | 0.990 ~ | 1.198 **LOSS** | 1.007 ~ |
| `engine-sample-eco` | 32 | 0.884 ~ | 0.848 **WIN** | 0.966 ~ |
| `engine-sample-eco` | 64 | 1.114 ~ | 0.887 **WIN** | 1.013 ~ |
| `engine-sample-eco` | 256 | 0.927 ~ | 0.960 **WIN** | 1.063 **LOSS** |
| `engine-live` | 32 | 0.667 **WIN** | 0.996 ~ | 0.942 **WIN** |
| `engine-live` | 64 | 0.982 ~ | 1.039 **LOSS** | 0.992 ~ |
| `engine-live` | 256 | 0.898 ~ | 1.144 **LOSS** | 0.948 **WIN** |

**Verdict: rejected.** The M5 shows no win, every cell within the spread and slightly worse.
On CI, Linux live wins by 5–6% at blocks 32 and 256 and Windows live "loses" by up to 14%. But
the port doesn't touch the sample path at all, so its sample cells measure the drift between the
two runs:

- **Linux** ran both on an EPYC 9V45, and its unchanged sample path still moved by up to 6%
  (`engine-sample-eco` at block 256, 1.063, itself a "LOSS"). The 5–6% live win is therefore not
  beyond the between-run spread on the same model.
- **Windows** landed on a different CPU model (see "CI variance"): its unchanged sample cells
  moved by 6–20% and its Eco cells by 4–15% the other way, so its CI signal shows nothing either
  way.
- **The M1 VM**'s live WIN at block 32 (0.667) is inside that leg's drift between runs: in this
  run its unchanged sample path moved to 0.821 at block 32, and the baselines' verification run
  (40649977361) showed a false 1.15 "regression" (`engine-sample-eco` at block 32), so a lone WIN
  cell there shows nothing.

No demonstrable win, so the live half of the prefetch lever is rejected too. Not merged.

### Shuffles, arm64 only (`824b413`, local branch `m1ce-lever-shuffle2`) — rejected

**What changed from `bb1f373`.** The x86 route and its SSSE3 requirement are gone: on x86 the
build is the base's gather code, instruction for instruction. On arm64, the window was first
tried on the interleaved layout itself, by deinterleaving three `vld2q_f32` loads into a 12-frame
L window and a 12-frame R window, then `vqtbl3q_u8` per tap. That was 9–20% slower than the
interleaved kernel on every case and block size, well beyond the spread. This table is Task 12's
own measurement, not the judging scripts': `gg-bench --quick` on the M5, base and try
alternated, 3 runs each with their 15 per-round ratios pooled, while other builds ran:

| Case | Block | base median (IQR) | window median (IQR) | window / base |
|---|---:|---:|---:|---:|
| `engine-sample` | 32 | 1.202 (0.122) | 1.326 (0.016) | 1.103 |
| `engine-sample` | 64 | 1.111 (0.047) | 1.214 (0.045) | 1.093 |
| `engine-sample` | 256 | 1.026 (0.087) | 1.155 (0.036) | 1.126 |
| `engine-sample-eco` | 32 | 0.830 (0.102) | 0.917 (0.018) | 1.105 |
| `engine-sample-eco` | 64 | 0.744 (0.068) | 0.826 (0.017) | 1.109 |
| `engine-sample-eco` | 256 | 0.669 (0.048) | 0.743 (0.032) | 1.110 |
| `engine-live` | 32 | 1.288 (0.052) | 1.487 (0.027) | 1.154 |
| `engine-live` | 64 | 1.134 (0.200) | 1.319 (0.032) | 1.163 |
| `engine-live` | 256 | 1.007 (0.144) | 1.210 (0.045) | 1.201 |

So the committed port keeps the window only on the planar sample paths (one output, a mono
source, or a stereo source without its interleaved copy). On the engine's cases it is neutral,
0.999 to 1.024, as expected: they run the interleaved code.

**Verdict: rejected on every platform.** The interleaved `read4Pair` already makes two contiguous
loads per lane, which removed the one-at-a-time tap loads this lever attacked. What's left for it
is the planar paths, where a micro-benchmark found one clear win, −6%, on planar stereo into two
outputs, a path the engine never runs, because `setSource` always builds the interleaved copy for
a stereo source. About 200 lines of arm64-only code for no gain on a hot path. Not merged, and
not pushed; no CI run.

## CI variance

**Unchanged code moved by up to ~20% between CI runs on Windows.** The live-only prefetch port
doesn't touch the sample path, so its sample cells compare the same code in two runs:

| Leg | Base run's CPU (40624459703) | Port run's CPU (40629306377) | `engine-sample` 32 / 64 / 256 | `engine-sample-eco` 32 / 64 / 256 |
|---|---|---|---:|---:|
| Windows | AMD EPYC 7763 | AMD EPYC 9V45 | 1.064 / 1.125 / 1.198 | 0.848 / 0.887 / 0.960 |
| Linux | AMD EPYC 9V45 | AMD EPYC 9V45 | 0.973 / 0.996 / 1.007 | 0.966 / 1.013 / 1.063 |

Every one of those six Windows cells is a "WIN" or a "LOSS" by the in-run IQR test, on code that
didn't change. On Linux, where both runs landed on the same CPU model, the same cells moved by up
to 6% (Eco at block 256, also a "LOSS"). The x86 in-run IQRs are small (mostly under 0.05 in
ratio), so they say nothing about the next run.

**The runner's CPU model is part of it.** GitHub's hosted x86 images aren't one machine. The
`machine` field in each run's JSON shows three CPU models across these runs:

| Run | Commit | Windows | Linux |
|---|---|---|---|
| 40622875194 (pre-lever base) | `1bd948a` | EPYC 7763 | EPYC 7763 |
| 40624735018 (prefetch) | `dd41947` | EPYC 7763 | Xeon Platinum 8370C |
| 40624459703 (interleave) | `a911b93` | EPYC 7763 | EPYC 9V45 |
| 40624846402 (scheduling) | `eb4819e` | EPYC 7763 | EPYC 7763 |
| 40625096040 (shuffles) | `bb1f373` | EPYC 7763 | EPYC 7763 |
| 40629306377 (prefetch port) | `b675ad9` | EPYC 9V45 | EPYC 9V45 |

The engine and the reference kernel have different instruction mixes, so their ratio depends on
the microarchitecture, and the ratio can't cancel that. The M1 VM is always `Apple M1 (Virtual)`,
but its in-run IQRs reach 0.39 on the judged cases (0.49 counting `engine-full-pool`), so it
calls almost nothing.

**What it means:**

- A single CI run can't judge a few-percent change. The verdicts above lean on the M5 and on CI
  pairs that ran on the same CPU model: the interleaved lever's Windows cells, and the shuffles'
  and the scheduling's x86 cells.
- **Baselines are per CPU model** (INTENT §23 #47). A leg's baseline file holds one entry per
  `machine`, and `gg-bench --make-baseline` groups its input runs by `machine`. Each entry's
  tolerance comes from the spread between separate CI runs, not the in-run IQR:
  `max(0.10, 2 × (max − min) / median)`. A run gets a verdict only against its own machine's
  entry. On a model with no entry, `gg-bench` prints `no baseline for <machine>; no verdict` and
  exits 0, and a later set of runs on that model adds its entry.
- This supports §19's report-only gate through M1. Sub-project 6 makes the gate blocking, with
  the published X, and adds entries for the CPU models the first baselines haven't seen.

## Before and after on the M5 Max (indicative)

Percent of one M5 Max core for 2,000 grains at 48 kHz, from the judging runs above: the median
over 3 cycles, at blocks 32 / 64 / 256. **Indicative only:** these ran with desktop apps open,
and X is measured in sub-project 6, on an idle machine, and published as measured.

| Case | Before levers (`1bd948a`) | After (`a911b93`, the engine's kernel since) |
|---|---|---|
| `engine-sample` (X) | 26.3 / 23.4 / 21.3 | 24.0 / 21.5 / 19.7 |
| `engine-sample-eco` | 18.0 / 15.4 / 13.5 | 16.7 / 14.7 / 12.9 |
| `engine-live` | 30.4 / 25.9 / 22.6 | 26.6 / 23.0 / 20.3 |
| `reference` (frozen, for scale) | 20.4 / 19.8 / 19.6 | 20.6 / 20.2 / 19.4 |

The ports' judging runs at `8eb0613` (4 cycles) agree: `engine-sample` 24.0 / 21.8 / 20.1 and
`engine-live` 26.6 / 23.2 / 20.3.

## Reproduce

```sh
cmake --preset release
cmake --build --preset release --target gg-bench
# one cycle: run each binary in turn (base, then each lever), rotating the order per cycle
build/release/tools/gg-bench/gg-bench --cases reference,engine-sample,engine-sample-eco,engine-live \
  --json build/bench-<name>-<cycle>.json

# the CI numbers: each build leg uploads gg-bench-<preset> (gg-bench.json and gg-bench.txt)
gh run download <run-id> --repo sethbc/giga-grains --pattern 'gg-bench-*' --dir build/ci-bench/<name>

# the levers' bit identity
ctest --preset dev -L 'kernel|golden|determinism'
```

CI artifacts expire, so this document is the durable record of the numbers.
