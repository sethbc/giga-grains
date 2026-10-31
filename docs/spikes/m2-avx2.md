# M2 spike: the AVX2 kernels

*M2 sub-project 3 (the engine), Task 12, 2026-10-30. The spec's §6.12 asked whether 8-wide
(AVX2) sample and live kernels, chosen at load and bit-identical to the SSE path, are worth
keeping. This is the measurement and the verdict.*

## Verdict

- **KEPT.** `engine-sample`'s ratio to `reference` improves on `engine-sample-sse`'s on both
  floor legs, at every block size in every run, by far more than the runs' spread. No leg
  regresses at any block size.
- Best block on each leg: Linux GCC 13, block 256, improvement 0.2199 at the smallest of
  three runs against a spread of 0.0189. Windows ClangCL, block 256, improvement 0.2820 at
  the smallest against a spread of 0.1116.

## What was measured

`gg-bench` at 630d740b151ff61b32bdd214d548a3c5e94591c3 (m2-engine) gained `engine-sample-sse`:
`engine-sample` with the SSE path forced, run in the same rounds as `engine-sample`
(dispatched) and `reference`. Each case's ratio is its ns per grain-sample over `reference`'s
in the same round, median and IQR over 5 rounds of at least 2 s (2,000 grains of 100 ms over
a 60 s stereo noise source at 48 kHz). Lower is faster.

Three full-length runs (`gh workflow run ci.yml --ref m2-engine`, all green), each with both
x86 floor legs: 41111335410, 41111344500 and 41111354554. Artifacts `gg-bench-linux-gcc13` and
`gg-bench-windows-clangcl` were downloaded and read with
`scratchpad/avx2-extract.py` (every number below is from their `gg-bench.json`).

Every one of the six leg-runs reports `kernelLanes` 8: each ran the AVX2 path for
`engine-sample`. None fell back to SSE for want of AVX2. (`engine-sample-sse` is the forced
fallback in all six.)

## Runs and models

| Run | Linux GCC 13 CPU | Windows ClangCL CPU |
|---|---|---|
| 41111335410 | AMD EPYC 9V45 | Intel Xeon Platinum 8370C @ 2.80 GHz |
| 41111344500 | AMD EPYC 7763 | AMD EPYC 7763 |
| 41111354554 | AMD EPYC 7763 | AMD EPYC 9V45 |

GitHub assigned the legs different CPU models from run to run: Linux landed on 9V45, 7763,
7763; Windows on three different models (an Intel Xeon and two AMD). The spec says nothing
about models, so the runs are not split by model (one run each would leave no spread). The
model-to-model difference is part of the spread, which makes the test stricter: an
improvement has to clear it. Because each ratio is `engine-sample` over `reference` in the
same process, the machine's speed cancels, but not its microarchitecture (CLAUDE.md,
Benchmarks).

## Every number

Ratios to `reference`, median (IQR). Improvement = `engine-sample-sse` ratio minus
`engine-sample` ratio, within one run; the percentage is of the SSE ratio.

### Linux GCC 13

| Run | Block | `engine-sample` (AVX2) | `engine-sample-sse` | Improvement | % |
|---|---|---|---|---|---|
| 41111335410 | 32 | 1.0665 (0.0345) | 1.2473 (0.0429) | +0.1808 | 14.49 |
| 41111335410 | 64 | 0.9220 (0.0133) | 1.1414 (0.0102) | +0.2194 | 19.22 |
| 41111335410 | 256 | 0.8241 (0.0247) | 1.0629 (0.0364) | +0.2388 | 22.47 |
| 41111344500 | 32 | 1.0417 (0.0110) | 1.2035 (0.0241) | +0.1618 | 13.44 |
| 41111344500 | 64 | 0.9331 (0.0041) | 1.1369 (0.0075) | +0.2038 | 17.92 |
| 41111344500 | 256 | 0.8294 (0.0140) | 1.0493 (0.0131) | +0.2199 | 20.96 |
| 41111354554 | 32 | 1.0400 (0.0092) | 1.2050 (0.0109) | +0.1650 | 13.69 |
| 41111354554 | 64 | 0.9190 (0.0023) | 1.1242 (0.0082) | +0.2052 | 18.25 |
| 41111354554 | 256 | 0.8280 (0.0031) | 1.0531 (0.0027) | +0.2251 | 21.38 |

### Windows ClangCL

| Run | Block | `engine-sample` (AVX2) | `engine-sample-sse` | Improvement | % |
|---|---|---|---|---|---|
| 41111335410 | 32 | 1.1919 (0.0173) | 1.4049 (0.0417) | +0.2130 | 15.16 |
| 41111335410 | 64 | 0.9064 (0.0096) | 1.2213 (0.0512) | +0.3150 | 25.79 |
| 41111335410 | 256 | 0.7356 (0.0063) | 1.1292 (0.0249) | +0.3936 | 34.85 |
| 41111344500 | 32 | 0.9677 (0.0051) | 1.2301 (0.0008) | +0.2624 | 21.33 |
| 41111344500 | 64 | 0.8319 (0.0020) | 1.1015 (0.0046) | +0.2696 | 24.47 |
| 41111344500 | 256 | 0.7313 (0.0074) | 1.0155 (0.0067) | +0.2843 | 27.99 |
| 41111354554 | 32 | 0.9922 (0.0159) | 1.2496 (0.0518) | +0.2574 | 20.60 |
| 41111354554 | 64 | 0.9229 (0.0245) | 1.2078 (0.0287) | +0.2849 | 23.59 |
| 41111354554 | 256 | 0.8965 (0.0116) | 1.1785 (0.0808) | +0.2820 | 23.93 |

## The spread

The spec's words are "beyond those runs' spread" and it defines the spread no further, so
this spike defines it as the **range (max - min) of the per-run improvement across the three
runs, per leg and block size**. The test is that the smallest of the three improvements
exceeds that range, so a result has to hold in every run, with margin.

| Leg | Block | Improvements (run order above) | Smallest | Spread (range) | Smallest / spread |
|---|---|---|---|---|---|
| Linux | 32 | +0.1808, +0.1618, +0.1650 | +0.1618 | 0.0190 | 8.5x |
| Linux | 64 | +0.2194, +0.2038, +0.2052 | +0.2038 | 0.0156 | 13.1x |
| Linux | 256 | +0.2388, +0.2199, +0.2251 | +0.2199 | 0.0189 | 11.6x |
| Windows | 32 | +0.2130, +0.2624, +0.2574 | +0.2130 | 0.0494 | 4.3x |
| Windows | 64 | +0.3150, +0.2696, +0.2849 | +0.2696 | 0.0454 | 5.9x |
| Windows | 256 | +0.3936, +0.2843, +0.2820 | +0.2820 | 0.1116 | 2.5x |

## The rule's arithmetic

- **Improves beyond the spread on at least one floor leg:** Linux, block 256: smallest
  improvement 0.2199 > spread 0.0189. Every other row clears its spread too (smallest column
  above), so both legs qualify at all three blocks. A stricter test passes too: adding the
  largest single-run IQR of an involved ratio on the leg to the spread (Linux 0.0429, Windows
  0.0808) still leaves every smallest improvement above it (the closest is Windows block 256,
  0.1116 + 0.0808 = 0.1924 < 0.2820).
- **Regresses beyond the spread on none:** the improvement is positive in all 18 leg-run-block
  cells (13% to 35% of the SSE ratio); no cell shows `engine-sample` slower than
  `engine-sample-sse`.
- **Verdict: KEPT.** The x86 baselines are to be remade from three further runs, with
  `engine-sample-sse` kept as the fallback's report-only case.

## Caveats

- The Windows spread is wide because the three runs landed on three different CPU models, and
  one run (the Xeon 8370C) improved most (+0.3936 at block 256), so the Windows margin at
  block 256 is the smallest (2.5x). The verdict does not rest on it: Linux alone clears the
  rule by 8x or more at every block.
- Both Linux runs on EPYC 7763 agree within 0.01 on every improvement, and the 9V45 run is
  within 0.02 of them, so the model effect on the improvement is small there.
- The IQRs of a single run (up to 0.08) are for one run's rounds; the spread above is across
  runs, which is the larger-grained figure.
- Bit-identity of the two paths is the kernel tests' business (the AVX2-versus-SSE `memcmp`
  test), not this measurement's.

## Baselines

Remade with `gg-bench --make-baseline <leg> tools/gg-bench/baselines/<leg>.json --merge
tools/gg-bench/baselines/<leg>.json a.json b.json c.json` from the three full-length runs
41111335410, 41111344500 and 41111354554 (engine code identical to `630d740`'s kernels; HEAD
`cdc73d5` changed tests only). Output:

```
linux-gcc13
  AMD EPYC 9V45 96-Core Processor (x86_64): 30 ratios from 1 run (replaced)
  AMD EPYC 7763 64-Core Processor (x86_64): 30 ratios from 2 runs (replaced)
windows-clangcl
  AMD EPYC 7763 64-Core Processor (x86_64): 30 ratios from 1 run (replaced)
  AMD EPYC 9V74 80-Core Processor (x86_64): 12 ratios from 1 run (kept)
  Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz (x86_64): 30 ratios from 1 run (new)
  AMD EPYC 9V45 96-Core Processor (x86_64): 30 ratios from 1 run (new)
```

The tool made every entry 30 ratios: the four gated cases plus all six report-only cases
(`engine-layers`, `engine-layers-full`, `engine-sample-hq`, `engine-sample-hq-up`,
`engine-overload`, `engine-sample-sse`) enter the baseline, as the baselines README says three
merged runs do. Tolerances stay at the 0.10 floor everywhere. Entries from one run (Windows) have
no spread to measure. Windows' EPYC 9V74 entry is kept from the earlier engine code (its 12
ratios are older, SSE-only numbers) because none of the three runs landed on that model; it
should be remade when one does.

### Before and after (ratio, old to new)

Linux, `engine-sample` / `engine-sample-eco` / `engine-live` / `engine-full-pool`, blocks 32, 64, 256:

| Model | Case | Block 32 | Block 64 | Block 256 |
|---|---|---|---|---|
| EPYC 9V45 | engine-sample | 1.1555 to 1.0665 (-7.7%) | 1.1015 to 0.9220 (-16.3%) | 1.0269 to 0.8241 (-19.7%) |
| | engine-sample-eco | 0.8434 to 0.8683 (+2.9%) | 0.8122 to 0.7565 (-6.9%) | 0.7551 to 0.6794 (-10.0%) |
| | engine-live | 1.2953 to 1.0810 (-16.5%) | 1.1988 to 0.9839 (-17.9%) | 1.1026 to 0.8888 (-19.4%) |
| | engine-full-pool | 1.1142 to 1.0397 (-6.7%) | 1.0785 to 0.9269 (-14.1%) | 1.0496 to 0.8259 (-21.3%) |
| EPYC 7763 | engine-sample | 1.1077 to 1.0408 (-6.0%) | 1.0324 to 0.9261 (-10.3%) | 0.9760 to 0.8287 (-15.1%) |
| | engine-sample-eco | 0.8564 to 0.8759 (+2.3%) | 0.7944 to 0.7701 (-3.1%) | 0.7318 to 0.6778 (-7.4%) |
| | engine-live | 1.2195 to 1.0762 (-11.8%) | 1.0828 to 0.9859 (-8.9%) | 0.9554 to 0.8980 (-6.0%) |
| | engine-full-pool | 1.0641 to 0.9889 (-7.1%) | 1.0121 to 0.9289 (-8.2%) | 0.9575 to 0.8321 (-13.1%) |

Windows (EPYC 7763, the one replaced entry with an old counterpart on the same model):

| Case | Block 32 | Block 64 | Block 256 |
|---|---|---|---|
| engine-sample | 1.2763 to 0.9677 (-24.2%) | 1.1726 to 0.8319 (-29.1%) | 1.0624 to 0.7313 (-31.2%) |
| engine-sample-eco | 0.9833 to 0.8420 (-14.4%) | 0.8621 to 0.7050 (-18.2%) | 0.7506 to 0.6009 (-19.9%) |
| engine-live | 1.4409 to 1.3135 (-8.8%) | 1.2322 to 1.1226 (-8.9%) | 1.0126 to 0.9428 (-6.9%) |
| engine-full-pool | 1.2437 to 0.9595 (-22.9%) | 1.1597 to 0.8309 (-28.4%) | 1.0544 to 0.7431 (-29.5%) |

Explanation. `engine-sample`, `engine-live` and `engine-full-pool` all drop at every block on
every replaced entry: the 8-wide Hermite kernels now read the sample and the ring. The drop grows
with the block size (the dispatch costs a call per grain, which a bigger block amortises) and is
largest on Windows. `engine-sample-eco` falls on Windows (the Eco tier has an 8-wide linear
kernel) and at blocks 64 and 256 on Linux. **Two ratios rose: `engine-sample-eco` at block 32 on
both Linux models (+2.9% on 9V45, +2.3% on 7763).** Both are inside the 0.10 tolerance and inside
the spread between the runs; the block-32 Eco grain is short work, so the per-grain indirect call
is a visible share of it. They are named here as regressions of a small size, not explained away:
if later runs show Linux Eco at block 32 drifting up, the dispatch's cost at small blocks is the
first suspect. `engine-sample-sse` entered at 1.2473, 1.1414 and 1.0629 on 9V45 and 1.2042, 1.1305
and 1.0512 on 7763 (Linux, blocks 32, 64, 256), and 1.2301, 1.1015, 1.0155 on Windows' 7763: the
fallback's report-only figure, now baselined beside the headline.

### macOS (`mac-universal`, Apple M1 VM, arm64): the non-x86 path

The mac leg builds no AVX2 and pays an indirect call per grain. Its three artifacts, checked
against `tools/gg-bench/baselines/mac-universal.json` as `gg-bench --baseline` does (a ratio
regresses above `ratio x (1 + tolerance)`):

| Run | engine-sample 32 / 64 / 256 | Gated cells over the limit |
|---|---|---|
| 41111335410 | 1.4570 / 1.0276 / 0.9386 (limits 1.6736 / 1.3006 / 1.1630) | engine-sample-eco 32, engine-full-pool 32 |
| 41111344500 | 1.3217 / 1.1748 / 0.9602 | engine-sample-eco 64 |
| 41111354554 | 1.2215 / 1.1413 / 1.0315 | engine-sample-eco 64 |

No `engine-sample` ratio regressed in any of the nine cells (baseline 1.2357 / 1.0535 / 1.0190).
Four other cells exceeded their limit (`engine-sample-eco` at 32 once and at 64 twice,
`engine-full-pool` at 32 once), each by a margin the M1 VM's run-to-run drift has produced before
(CLAUDE.md records a 1.1546 against 1.0377 for the same Eco cell at block 32). On the mac the
fallback path and `engine-sample-sse` are the same code: `engine-sample-sse` read 1.3172 /
0.9995 / 0.7545, 1.3162 / 1.1360 / 0.9787 and 1.1539 / 1.2836 / 1.0109, so the spread between
runs of one binary is as large as any change the dispatch could make. The mac baseline is not
remade (no engine change for that path, and the gate is report-only until 1.0.0).
