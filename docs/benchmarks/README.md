# Benchmarks: the published measurements

The JSON files here are `gg-bench`'s own output from the runs INTENT publishes. Each is kept as
written; AGENTS.md's "Benchmarks" section describes the modes and the schema.

## 2026-10-29: M1's headline (sub-project 6, Close)

One session on the director's Apple M5 Max (arm64, bare metal), the `release` preset, AppleClang
21.0.0.21000334, at commit `be2291e` (main's `09f3155` plus one docs file; the engine and tools
sources are main's at M1's close, and the Perform close PR #10 merged after it changed only
comments in the engine). Nothing else was built, tested or benchmarked on the machine during
the session; the desktop (WindowServer, iTerm2, Photos' library daemon) was open. The one-minute
load average was 2.58 before the run and 3.20 after, above the design's bar of 1.5 and below
sub-project 2's judging runs (about 4.7). Every in-run IQR is at or under 0.07 ns, under 3% of
its median.

| File | Command | What it is |
|---|---|---|
| `2026-10-29-m1-headline.json` | `gg-bench --commit be2291e --json …` | The full default run: 2,000 grains of 100 ms over 60 s of stereo noise at 48 kHz, 5 interleaved rounds of at least 2 s per configuration, every case at blocks 32, 64 and 256. Schema 2, with the ratios against the frozen `reference` kernel. |
| `2026-10-29-m1-space.json` | `gg-bench --space --commit be2291e --json …` | Space's per-sample stage, the diffuser then the reverb, on stereo noise at 48 kHz, Diffusion and Amount at 100%, 5 rounds of at least 2 s. |
| `2026-10-29-m1-canvas.json` | `gg-bench --canvas --json …` | The canvas splat renderer at 3,200 × 1,200 (pixel scale 2), 240 frames after a 20-frame warm-up, at 4,096, 8,192 and 16,384 grains. `--canvas` takes no `--commit`; it ran in the same session as the other two. |

**The figures** (medians; % of one M5 Max performance core for 2,000 grains at 48 kHz, from the
median ns per stereo grain-sample × 2,000 × 48,000 ÷ 10⁷):

| Case | Block 32 | Block 64 | Block 256 |
|---|---|---|---|
| `engine-sample` (**X**, INTENT §16) | 2.44 ns, **23.4%** | 2.21 ns, **21.2%** | 2.03 ns, **19.5%** |
| `engine-live` (the FX's figure) | 3.32 ns, 31.9% | 2.69 ns, 25.8% | 2.18 ns, 21.0% |
| `engine-sample-eco` | 1.73 ns, 16.6% | 1.52 ns, 14.6% | 1.33 ns, 12.8% |
| `reference` (frozen, for scale) | 2.12 ns, 20.4% | 2.07 ns, 19.8% | 2.01 ns, 19.3% |
| ratio `engine-sample` / `reference` | 1.154 | 1.072 | 1.024 |

- **X is 19.5%**, the block-256 figure (a host's typical block), with 23.4% and 21.2% at blocks
  32 and 64. There is no ceiling on X (INTENT §16, §23 #39).
- **Space:** 29.44 ns per stereo sample (min 29.38, IQR 0.06), **0.141%** of one core.
  Sub-project 4's fix wave measured 28.49 ns, 0.137%, at `ac81dfb`.
- **Canvas:** 2.50 / **2.87** / 3.66 ms per frame at 4,096 / 8,192 / 16,384 grains (p95 2.57 /
  2.92 / 3.71). The 8,192 figure is the idle run decision D3 of sub-project 5 asks for, against
  the 3 ms budget: inside it (M0's idle figure was 2.77 ms, sub-project 5's loaded one 2.80 ms).
- **Carry-over row 185** (sub-project 2's ratio check, re-run idle): sub-project 2 recorded the
  M5 Max's `engine-sample` and `reference` percentages at `a911b93` (24.0 / 21.5 / 19.7 against
  20.6 / 20.2 / 19.4, `docs/spikes/m1-cpu-levers.md`), ratios 1.165 / 1.064 / 1.015, and ruling
  R3's loaded check at `a0611c1` moved them by +1.3% / +1.1% / −0.9%, so about 1.180 / 1.076 /
  1.006. This run's 1.154 / 1.072 / 1.024 are −2.2% / −0.4% / +1.8% from those, inside R3's 3%,
  with Texture, the fresh seed and the Perform UI's engine changes since.

**Reproduce** (on an otherwise idle machine; about ten minutes):

```sh
cmake --preset release
cmake --build --preset release --target gg-bench
b=build/release/tools/gg-bench/gg-bench
uptime                                   # the load average, for the record
$b --commit "$(git rev-parse --short HEAD)" --json headline.json
$b --space --commit "$(git rev-parse --short HEAD)" --json space.json
$b --canvas --json canvas.json
```

## 2026-11-01: M2 sub-project 4 (Extremes)

One session on the director's Apple M5 Max (arm64), the `release` preset, AppleClang
21.0.0.21000401, 4-wide kernels, at commit `cc68e3d` (the engine and tools sources are
`66b58f5`'s; only docs and a test comment changed since). An idle window the program manager
granted: nothing else was built or benchmarked. The one-minute load average was 1.60 before the
published run and 4.04 after, with no process above 12.5% CPU at the start.

| File | Command | What it is |
|---|---|---|
| `2026-11-01-m2-extremes.json` | `gg-bench --cases reference,engine-sample,engine-sample-smear,engine-buses,engine-spectral,hermite-simd,hermite-simd-grainfilter --commit cc68e3d --json …` | The published run: the three sub-project 4 engine cases beside X, `reference`, `hermite-simd` and the §22 kernel, at blocks 32, 64 and 256. |
| `2026-11-01-m2-extremes-chain.json` | `gg-bench --chain --commit cc68e3d --json …` | The wet chain's new stages (two bus filters, the delay, the limiter), same window, just before. |
| `2026-11-01-m2-extremes-ab-main.json` | `gg-bench --cases reference,engine-sample --json …` with main's binary at `4fed5bb` | The A/B's main run (the second of two). |
| `2026-11-01-m2-extremes-ab-head.json` | `gg-bench --cases reference,engine-sample --json …` with this head's binary | The A/B's head run (the second of two). |

**The figures** (medians; ns per stereo grain-sample at blocks 32 / 64 / 256, then % of one core
and the ratio to `reference`):

| Case | ns | % of one core | Ratio |
|---|---|---|---|
| `engine-sample` (X) | 2.472 / 2.245 / 2.065 | 23.7 / 21.6 / 19.8 | 1.172 / 1.101 / 1.032 |
| `engine-sample-smear` | 2.500 / 2.265 / 2.100 | 24.0 / 21.7 / 20.2 | 1.176 / 1.099 / 1.035 |
| `engine-buses` | 2.505 / 2.255 / 2.073 | 24.1 / 21.7 / 19.9 | 1.171 / 1.091 / 1.031 |
| `engine-spectral` | 3.403 / 2.721 / 2.212 | 32.7 / 26.1 / 21.2 | 1.604 / 1.328 / 1.108 |
| `reference` | 2.114 / 2.039 / 1.992 | | |
| `hermite-simd` | 2.133 / 2.064 / 1.996 | | |
| `hermite-simd-grainfilter` | 8.919 / 9.043 / 9.400 | | |

- **`--chain`:** 11.781 ns per stereo sample (min 11.773, IQR 0.009), **0.057%** of one core.
- **X shows no regressed ratio.** Against the M1 headline's 1.154 / 1.072 / 1.024 it is +1.6% /
  +2.7% / +0.8%, inside ruling R3's 3%.
- **The A/B** separates the extremes from the drift of three sub-projects. Main (`4fed5bb`) and
  this head were timed alternately in the same window (`reference,engine-sample`: main, head,
  main, head). Main's second run gave 1.1770 / 1.0896 / 1.0338; the head's two gave 1.1746 /
  1.0939 / 1.0327 and 1.1770 / 1.0934 / 1.0322. The extremes move X by at most ±0.4% at any
  block size. (Main's first run, 1.1787 / 1.1797 / 1.0449, and an earlier full run's X at block
  32, 1.2019, were the machine settling after a busy browser; neither is published.)
- **§22:** `hermite-simd-grainfilter` is 4.18× / 4.38× / 4.71× `hermite-simd` and 3.61× / 4.03× /
  4.55× X; the filter's own added cost (less `hermite-simd`) is 6.79 / 6.98 / 7.40 ns, 2.75× /
  3.11× / 3.59× X per grain-sample, far above a quarter of X. Buses with channel masks are v1's
  answer; per-grain filters wait for M5's budget weights.

**Reproduce** (on an otherwise idle machine):

```sh
cmake --preset release
cmake --build --preset release --target gg-bench
b=build/release/tools/gg-bench/gg-bench
uptime                                   # the load average, for the record
$b --cases reference,engine-sample,engine-sample-smear,engine-buses,engine-spectral,hermite-simd,hermite-simd-grainfilter \
   --commit "$(git rev-parse --short HEAD)" --json extremes.json
$b --chain --commit "$(git rev-parse --short HEAD)" --json chain.json
# the A/B: build main (4fed5bb) the same way, then alternate main's binary and this one:
$b --cases reference,engine-sample --json ab-head.json
```

## 2026-11-03: M2's close (sub-project 7, the idle run)

One session on the director's Apple M5 Max (arm64), the `release` preset, AppleClang
21.0.0.21000401, 4-wide kernels, at commit `ca60f54` (main's engine and tools sources at M2's
close plus the close's own changes). An idle window the director granted. The one-minute load
average was 1.63 before the run and 1.73 to 2.24 after (the `run.log` lines at the end of each
of the four commands), under the 3.20 of M1's run. Every in-run IQR of `engine-sample` is at or
under 0.013 ns, under 0.6% of its median.

| File | Command | What it is |
|---|---|---|
| `2026-11-03-m2-headline.json` | `gg-bench --commit ca60f54 --json …` | The full default run: 63 configurations (every case at blocks 32, 64 and 256) over 2,000 grains of 100 ms from 60 s of stereo noise at 48 kHz, 5 interleaved rounds of at least 2 s. Schema 2, with the ratios against the frozen `reference` kernel. |
| `2026-11-03-m2-canvas.json` | `gg-bench --canvas --json …` | The canvas at 3,200 x 1,200 (pixel scale 2), four lanes, the banded resolve, 240 frames after a 20-frame warm-up, at 4,096, 8,192 and 16,384 grains. |
| `2026-11-03-m2-space.json` | `gg-bench --space --commit ca60f54 --json …` | Space's per-sample stage, as M1's. |
| `2026-11-03-m2-chain.json` | `gg-bench --chain --commit ca60f54 --json …` | The wet chain's bus filters, delay and limiter, and the `spectral` stage (one Spectral Smear's STFT). |

**The figures** (medians; ns per stereo grain-sample at blocks 32 / 64 / 256, % of one core at
48 kHz, and the ratio to `reference` in the same round), beside M1's:

| Case | ns | % of one core | Ratio |
|---|---|---|---|
| `engine-sample` (**X**, INTENT §16) | 2.431 / 2.188 / 2.017 | **23.3 / 21.0 / 19.4** | 1.1704 / 1.0897 / 1.0302 |
| M1's X (2026-10-29) | 2.44 / 2.21 / 2.03 | 23.4 / 21.2 / 19.5 | 1.154 / 1.072 / 1.024 |
| `engine-live` | 3.399 / 2.717 / 2.177 | 32.6 / 26.1 / 20.9 | 1.6367 / 1.3520 / 1.1091 |
| `engine-sample-eco` | 1.760 / 1.556 / 1.338 | 16.9 / 14.9 / 12.9 | 0.8474 / 0.7706 / 0.6829 |
| `reference` (frozen) | 2.077 / 2.010 / 1.963 | 19.9 / 19.3 / 18.8 | |
| `engine-live-long` (300 s ring) | 3.524 / 2.851 / 2.323 | 33.8 / 27.4 / 22.3 | 1.6960 / 1.4143 / 1.1866 |
| `engine-layers` (4 x 500 grains) | 3.066 / 2.818 / 2.387 | 29.4 / 27.1 / 22.9 | 1.4774 / 1.4035 / 1.2192 |
| `engine-layers-full` (4 x 2,000) | 3.632 / 3.124 / 2.367 | 139.5 / 120.0 / 90.9 (8,000 grains) | 1.7503 / 1.5540 / 1.2088 |
| `engine-sample-hq` | 16.688 / 16.237 / 15.887 | 160.3 / 155.9 / 152.6 | 8.0275 / 8.0752 / 8.1144 |
| `engine-sample-hq-up` | 16.928 / 16.420 / 15.977 | 162.6 / 157.7 / 153.4 | 8.1396 / 8.1443 / 8.1479 |
| `engine-overload` | 2.398 / 2.190 / 2.029 | 23.3 / 21.2 / 19.7 | 1.1545 / 1.0867 / 1.0357 |
| `engine-sample-sse` | 2.451 / 2.192 / 2.021 | 23.5 / 21.1 / 19.4 | 1.1800 / 1.0901 / 1.0311 |
| `engine-sample-smear` | 2.432 / 2.191 / 2.025 | 23.3 / 21.0 / 19.4 | 1.1701 / 1.0889 / 1.0328 |
| `engine-buses` | 2.435 / 2.201 / 2.017 | 23.4 / 21.1 / 19.4 | 1.1699 / 1.0945 / 1.0293 |
| `engine-spectral` | 3.399 / 2.715 / 2.183 | 32.6 / 26.1 / 21.0 | 1.6356 / 1.3492 / 1.1148 |

- **X is 19.4%** at block 256 (23.3% at block 32 and 21.0% at block 64), against M1's 19.5%
  (23.4 / 21.2 / 19.5): 0.4% to 1.0% lower in ns. The ratios, 1.1704 / 1.0897 / 1.0302 against
  M1's 1.154 / 1.072 / 1.024, are +1.4% / +1.6% / +0.6%, inside ruling R3's 3%, and agree with
  sub-project 4's idle A/B head runs (1.1770 / 1.0934 / 1.0322) to within 0.6%. (This machine's `reference`
  is 2.077 / 2.010 / 1.963 ns here against M1's 2.12 / 2.07 / 2.01, so the machine ran a little
  faster in absolute terms and the ratio is the comparison.)
- **HQ against Normal** (row 307): `engine-sample-hq` over `engine-sample` is **6.87 / 7.42 /
  7.88** at blocks 32 / 64 / 256 (16.688 / 2.431, 16.237 / 2.188, 15.887 / 2.017), against
  sub-project 3's 6.8 / 7.4 / 7.8 and the weight table's 6.79 (the `--quick` figure at block
  256, on a loaded machine). The weights are unchanged: they wait for M5's calibration (INTENT
  §14), and the row stays open for it and for the x86 legs.
- **`engine-layers-full`** (the Task 19 brief's A/B at `630d740` and at the head) was not
  re-measured locally: the program manager dropped it, since the Linux baseline remake from three
  full CI runs settles whether it moved. Here it is 3.632 / 3.124 / 2.367 ns, IQR up to 0.24 ns
  (6.5% of the median at block 32), the noisiest case in the run, as the layers' threaded-less
  `SerialRunner` sum is.
- **Space:** 28.718 ns per stereo sample (min 28.686, IQR 0.048), **0.138%** of one core, against
  M1's 29.44 ns, 0.141%, and sub-project 4's fix wave 28.49 ns, 0.137%.
- **`--chain`:** the bus filters, delay and limiter 11.706 ns per stereo sample (min 11.656, IQR
  0.049), **0.056%** of one core (sub-project 4: 11.781 ns, 0.057%). Its new `spectral` stage,
  one Spectral Smear's STFT over stereo noise with no grain reading it, is **26.705 ns** (min
  26.561, IQR 0.035), **0.128%** of one core. That is **row 372's figure**: the STFT keeps
  running while a Spectral layer is switched off, so an idle Spectral layer costs 0.128% of a
  core, under the 0.25% that would have gone to the director and the 1% of the program
  manager's ruling of 2026-11-03. The row closes as measured; the cost is not removed.
- **Canvas:** 2.474 / **2.848** / 3.592 ms per frame at 4,096 / 8,192 / 16,384 grains (p95 2.508
  / 2.880 / 3.639), with four lanes and the banded resolve (`"lanes": 4`, `"resolve":
  "banded"`). At 8,192 grains it is inside the 3 ms budget by 0.15 ms, against M1's one-tint
  2.87 ms and sub-project 6's four-lane run of 2.959 ms (loaded; p95 4.009). The splat is 0.737
  ms and the resolve 2.054 ms.

**CI's report-only gate, read at the close** (the three full-length runs at `ca60f54`, dispatches
41579049597, 41579058682 and 41579067573; verdicts against the baselines as they stood before the
remake, from each run's `gg-bench.txt` verdict lines): Linux's EPYC 7763 (runs 41579049597 and
41579067573) reported 0 of 42 ratios regressed (`engine-layers-full` included, so the flag it drew
earlier on the 9V45 was that single-run entry's, not the code's); the mac leg's M1 VM reported 0 of
42 in all three runs; Windows' EPYC 7763 (run 41579049597) reported 7 of 42, six of them at block
256 (`engine-sample`, `engine-sample-eco`, `engine-full-pool`, `engine-layers`,
`engine-layers-full` and `engine-overload`) and one (`engine-layers-full`) at block 32. The old
entry is a single run at `630d740`, made before the Ninja and clang-cl move (INTENT §23 #76,
merged 2026-11-02), and the Windows leg drifts up to ~20% between runs (§23 #47); the same commit's
Linux and mac runs show no such rise. The two 9V74s (Windows' and Linux's) had no entry and so no
verdict. The gate stays report-only until 1.0.0. The remake from these runs is commit `cf37b83`,
whose message compares each replaced entry with the one before it.

**Reproduce** (on an otherwise idle machine; about twenty minutes):

```sh
cmake --preset release
cmake --build --preset release --target gg-bench
b=build/release/tools/gg-bench/gg-bench
uptime                                   # the load average, for the record
c="$(git rev-parse --short HEAD)"
$b --commit "$c" --json headline.json
$b --canvas --json canvas.json
$b --space --commit "$c" --json space.json
$b --chain --commit "$c" --json chain.json
```
