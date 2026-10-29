# Benchmarks: the published measurements

The JSON files here are `gg-bench`'s own output from the runs INTENT publishes. Each is kept as
written; CLAUDE.md's "Benchmarks" section describes the modes and the schema.

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
