# M0 spike: grain kernel benchmark

*M0 Task 7, 2026-10-26; CI numbers added in Task 10b. Measures what one stereo grain-sample
costs with prototype grain kernels, to replace the first-principles estimates in INTENT §16 and
to check its headline target: **at least 2,000 Normal-quality grains per instance at 48 kHz,
using less than 25% of one Apple M1 performance core.** The first numbers come from one
machine, the director's Apple M5 Max, which INTENT §16 names as the ceiling, not the target.
The floor numbers, from CI's Apple M1 VM and AMD EPYC x86 VMs, are in "CI numbers (Task 10b)".*

## Verdict

- **The headline is judged on `hermite-simd`,** the 4-wide SIMD Hermite kernel (ruling T7-a).
  The scalar kernels are correctness references and carry extra bounds checks (see "Caveats").
- **On the M5 Max, `hermite-simd` with 2,000 grains uses 21.6%, 20.7% and 19.9% of one core**
  at block sizes 32, 64 and 256. That is under 25% at every block size.
- **The margin is thin even on ceiling hardware.**
  - At 25% of one core, 2,000 grains at 48 kHz allow **2.604 ns per stereo grain-sample**.
    `hermite-simd` uses 86% of that at block 32, 83% at block 64 and 80% at block 256.
  - That is room for about **2,320, 2,420 and 2,510 grains** respectively.
  - A core that runs this workload more than **1.16× slower** than the M5 Max misses the
    target at block 32. More than 1.26× slower misses it at every block size.
- **On the floor hardware in CI, the headline is missed by about 2×.** `hermite-simd` needs
  48% to 53% of a core on the M1 VM (1.9× to 2.1× the budget) and 55% to 69% on the x86 VMs
  (2.2× to 2.8×). The 25% budget fits about 940 to 1,040 grains on the M1 VM and 720 to 910
  on x86. See "CI numbers (Task 10b)".
- **Director ruling (2026-10-26): keep the headline; milestone M1 closes the gap.** The headline
  stays as the provisional target, closing the gap is an explicit goal of the M1 renderer, M1's
  CI CPU gate starts as a regression gate at the measured baseline, and the headline is judged
  again at the end of M1. See "Director ruling".

## What was measured

`gg-bench` (`tools/gg-bench/`) times the prototype kernels in `tools/gg-bench/Kernels.h`.

**Workload:**

- a 60 s stereo white-noise source at 48 kHz, 23 MB of float samples, from `gg::rng`;
- 2,000 grains of 100 ms with a Hann window;
- random positions, gains uniform in [0.5, 1] and rates uniform in [0.5, 2];
- a grain respawns at a new random position, rate and gain when its window ends;
- every read stays inside the source, which is the usual in-bounds path;
- block sizes 32, 64 and 256.

**Kernels.** Every kernel adds each grain's windowed, gain-scaled source read into a stereo
output block.

| Kernel | Interpolation | Form |
|---|---|---|
| `linear-scalar` | linear, through `gg::interp::linear` | scalar reference |
| `hermite-scalar` | 4-point Catmull-Rom, through `gg::interp::hermite4` | scalar reference |
| `sinc-scalar` | 16-tap Blackman-windowed sinc, 512-phase table | scalar; no SIMD version yet |
| `linear-simd` | linear | 4-wide, grain-major |
| `hermite-simd` | 4-point Catmull-Rom | 4-wide, grain-major |

The SIMD kernels are **grain-major**:

- each 4-wide step renders 4 consecutive output samples of one grain;
- each lane's source taps are gathered one at a time, because neither SSE2 nor NEON has a
  gather;
- the interpolation, the window and the accumulation then run 4-wide.

Their cost includes that per-lane gather. They match the scalar references to 1e-5 of the
peak (`ctest --preset dev -L bench`, 3/3 pass).

**Timing:**

- Each configuration resets to the same starting grains and warms up for a quarter of a run.
- It then makes 5 runs of at least 2 s each and reports the **median**.
- `ns/grain-sample` is the wall time divided by blocks × block size × 2,000 grains.
- `% core @48k` = ns × 2,000 × 48,000 / 10⁹ × 100: the share of one core needed to render
  2,000 grains in real time.
- Denormals are off while timing.
- The thread asks for `QOS_CLASS_USER_INTERACTIVE`, so that it runs on a performance core.

**Build:**

- the `release` preset: AppleClang 21.0.0.21000334, `-O3 -ffp-contract=off`, macOS minimum
  11.0;
- native arm64; AppleClang's default arm64 CPU for this target is `apple-m1`, so the binary
  uses no instructions an M1 lacks.

**Machine:**

- Apple M5 Max, 18 cores (6 "Super" and 12 "Performance", per `hw.perflevel*`);
- 128 GB of RAM;
- macOS 27.0.1.

## Ambient conditions

This was one full run on the director's desktop, with apps open. No builds or tests ran during
it: the release and dev builds had finished, and `ps` showed no compiler, ninja, cmake or ctest
process.

**Just before the run** (2026-10-26 21:21:35 CDT):

```
load averages: 3.34 2.96 2.75
 %CPU COMM
 39.0 WindowServer
  7.2 iTerm2
  6.5 coreaudiod
  5.4 trustd
  5.2 claude
```

**Just after the run** (21:24:25 CDT):

```
load averages: 3.04 2.96 2.78
 %CPU COMM
 99.3 mediaanalysisd
 14.2 WindowServer
 13.2 Firefox GPU Helper
 10.8 claude
  8.7 iTerm2
```

**Background load:**

- `mediaanalysisd`, macOS's background photo and video analysis daemon, has accumulated
  781 minutes of CPU time over 5 days, so it was probably busy during the run as well.
- With 18 cores it need not share the benchmark's core, but it does share the memory system.
- Firefox held an "audio-playing" power assertion, which explains coreaudiod's 6.5%.

**Power:**

- The machine started on battery, at 93%.
- The run went from about 21:21:41 to 21:24:19, when the JSON was written.
- The machine switched to AC at 21:23:11, about 90 s in, while `sinc-scalar` was running at
  block 256. `linear-simd` and `hermite-simd` therefore ran on AC.
- The power mode is 0 ("Automatic", not Low Power) on both battery and AC.
- `pmset -g therm` recorded no thermal or performance warning.

## Results

Recorded run, `gg-bench --json build/bench-m5max.json`:

- machine `"Apple M5 Max (arm64)"`;
- compiler `"AppleClang 21.0.0.21000334"`;
- 2,000 grains;
- the median of 5 runs of at least 2 s each.

The raw JSON stays in `build/` and is not committed.

| Kernel | Block | ns / stereo grain-sample | % of one core at 48 kHz |
|---|---:|---:|---:|
| `linear-scalar` | 32 | 2.1564 | 20.702 |
| `linear-scalar` | 64 | 2.0930 | 20.093 |
| `linear-scalar` | 256 | 2.0152 | 19.346 |
| `hermite-scalar` | 32 | 4.5168 | 43.362 |
| `hermite-scalar` | 64 | 4.4873 | 43.078 |
| `hermite-scalar` | 256 | 4.3703 | 41.955 |
| `sinc-scalar` | 32 | 8.7482 | 83.982 |
| `sinc-scalar` | 64 | 8.6831 | 83.358 |
| `sinc-scalar` | 256 | 8.6082 | 82.639 |
| `linear-simd` | 32 | 1.4856 | 14.262 |
| `linear-simd` | 64 | 1.3875 | 13.320 |
| `linear-simd` | 256 | 1.3543 | 13.001 |
| **`hermite-simd`** | **32** | **2.2483** | **21.584** |
| **`hermite-simd`** | **64** | **2.1521** | **20.660** |
| **`hermite-simd`** | **256** | **2.0747** | **19.917** |

**Reproducibility check.** A `gg-bench --quick` run made straight afterwards uses runs of
0.2 s, so its numbers are not the recorded ones.

- For `hermite-simd` it agreed within 2.1%: 21.2%, 20.2% and 19.9%.
- Across all 15 configurations it agreed within 5.7%.

## Against the INTENT §16 estimates

| Interpolation | §16 first-principles estimate | Measured on the M5 Max | Reading |
|---|---|---|---|
| Linear | 1 to 1.5 ns | 1.35 to 1.49 ns (`linear-simd`) | inside the range |
| Hermite | about 2 ns | 2.07 to 2.25 ns (`hermite-simd`) | 4% to 12% above |
| Sinc, 16 taps | 7 to 13 ns | 8.61 to 8.75 ns (`sinc-scalar`) | inside the range, scalar only |

- **§16 warned that gathers and cache misses could add 2× to 4×.** That did not show up on
  this machine.
  - The SIMD numbers include the per-lane gather.
  - Each block reads from 2,000 separate places in a 23 MB source.
  - A core with smaller caches, such as the M1 or a mid-range x86, may show more of that
    penalty. On CI, `hermite-simd` runs 2.4× to 3.2× slower than here (see "CI numbers
    (Task 10b)"). Those runs can't separate core speed, virtualization and memory stalls, and
    nothing was profiled.
- **Small blocks cost more,** because the per-block work below is spread over fewer samples.
  From block 256 to block 32:
  - `hermite-simd` costs 8.4% more and `linear-simd` 9.7% more;
  - the scalar kernels cost 1.6% to 7.0% more;
  - on CI the SIMD slope is about the same on Windows but nearly twice as steep on Linux
    (see "Gap analysis").

## Caveats

1. **The scalar/SIMD ratio overstates the SIMD gain.**
   - The scalar linear and Hermite kernels read through `gg::interp`, which bounds-checks
     every tap.
   - The SIMD kernels check once per grain per block (`readsInside`), then load unchecked.
   - Part of the measured ratio comes from the checks, not from SIMD width. The ratios are
     1.45× to 1.51× for linear and 2.01× to 2.11× for Hermite.
2. **`sinc-scalar` checks once per read, not per tap.**
   - Each read loads 16 taps per channel after a single range check. The bounds checks
     therefore cost it less than they cost the other scalar kernels.
   - There is no SIMD sinc kernel yet.
   - Its cutoff is fixed at Nyquist, with no anti-aliasing for rates above 1. A production
     reader would lower the cutoff at no extra cost per sample, but this one's quality is not
     production-ready.
3. **The timed block is more than the kernel.** Each block also includes:
   - clearing the output;
   - the per-block grain update, which advances all 2,000 grains and respawns those whose
     window ended;
   - an output checksum, which stops the compiler from dropping the work. It costs about one
     add per output sample, against 2,000 grain-samples;
   - one `steady_clock::now()` call, to check the run's deadline. That is tens of nanoseconds
     against the 95 µs (`linear-simd`) or more that a block of 32 takes here, so it is
     negligible.

   The numbers are therefore kernel plus per-block overhead, like an engine's. That overhead
   is why small blocks cost more.
4. **x86 instruction sets differ by platform.**
   - The kernels use only SSE2-level intrinsics, but the compiler may use more.
   - The macOS universal build's x86_64 slice uses AppleClang's default x86_64 CPU, `penryn`.
     That has SSE4.1, so the compiler may emit `insertps` when assembling vectors from
     gathered taps.
   - Linux and Windows builds use `-march=nehalem` (`cmake/compiler-options.cmake`).
   - x86 numbers from macOS and from Linux or Windows therefore come from slightly different
     code.
   - **Any Rosetta run is translated and must be flagged as such,** never reported as x86
     hardware numbers. `gg-bench` does not detect Rosetta itself. Under Rosetta the brand
     string is still the Apple CPU, so a translated run reports `machine` as
     `"Apple M5 Max (x86_64)"`.
   - No Rosetta run is recorded here. Every number above is native arm64.
5. **The kernels run in a fixed order,** so thermal drift could bias the later ones.
   - The order is `linear-scalar`, `hermite-scalar`, `sinc-scalar`, `linear-simd`, then
     `hermite-simd`, each at blocks 32, 64 and 256.
   - `hermite-simd` runs last, starting about 2.1 minutes in.
   - Thermal drift would make it look slower, not faster.
   - No thermal warning was recorded, and the `--quick` run (about 16 s, cool by comparison)
     agreed within 2.1%.
   - The power source is a different confound and can push the other way. The machine
     switched from battery to AC about 90 s in, during `sinc-scalar` (see "Ambient
     conditions"), so the SIMD kernels ran on AC and the scalar linear and Hermite kernels on
     battery. That also confounds the scalar/SIMD ratios in caveat 1.
6. **The core type is not reported.**
   - The M5 Max has two kinds of performance core ("Super" and "Performance").
   - The user-interactive QoS asks for a performance core, but the scheduler picks which one.
   - `gg-bench` can't pin the thread or report where it ran.
   - The full and quick runs agree, which suggests the same kind of core both times.
7. **This was one run on a desktop in use** (see "Ambient conditions"). Treat the numbers as
   good to a few percent, not to the four decimals printed.

## CI numbers (Task 10b)

**Recorded run:**

- CI run 40439674760, commit cc59031, 2026-10-26 between 21:43 and 21:49 CDT;
- the same workload and settings as above, at full length (`gg-bench --json`, ruling T10-c):
  2,000 grains, the median of 5 runs of at least 2 s each;
- the JSON and text output are the run's `gg-bench-<preset>` artifacts and are not committed.

Every leg is a shared GitHub-hosted virtual machine:

| Leg | Runner image | `machine` (from the JSON) | `compiler` | Build |
|---|---|---|---|---|
| M1 VM | `macos-latest` (arm64) | `Apple M1 (Virtual) (arm64)` | `AppleClang 21.0.0.21000101` | `mac-universal`; only the arm64 slice is run |
| Windows | `windows-2022` | `AMD EPYC 7763 64-Core Processor (x86_64)` | `Clang 19.1.5` (ClangCL) | `windows-clangcl`, `-march=nehalem` |
| Linux | `ubuntu-22.04` | `AMD EPYC 7763 64-Core Processor (x86_64)` | `GNU 12.3.0` | `linux-gcc12`, `-march=nehalem` |

All three are Release builds with `-ffp-contract=off`. The universal build's x86_64 slice is
not run in CI, so the x86 numbers come from the Windows and Linux legs. No Rosetta run is
involved.

**Results** (ns per stereo grain-sample, and % of one core at 48 kHz):

| Kernel | Block | M1 VM ns | M1 VM % | Windows ns | Windows % | Linux ns | Linux % |
|---|---:|---:|---:|---:|---:|---:|---:|
| `linear-scalar` | 32 | 8.9924 | 86.327 | 7.7595 | 74.491 | 8.1956 | 78.678 |
| `linear-scalar` | 64 | 8.4675 | 81.288 | 7.5520 | 72.499 | 7.7600 | 74.496 |
| `linear-scalar` | 256 | 6.6326 | 63.673 | 8.0948 | 77.711 | 7.2714 | 69.806 |
| `hermite-scalar` | 32 | 15.5804 | 149.571 | 15.5374 | 149.159 | 14.4613 | 138.828 |
| `hermite-scalar` | 64 | 9.5708 | 91.879 | 15.2805 | 146.693 | 13.9262 | 133.691 |
| `hermite-scalar` | 256 | 11.8176 | 113.449 | 15.1965 | 145.886 | 13.7762 | 132.252 |
| `sinc-scalar` | 32 | 29.8814 | 286.861 | 27.7725 | 266.616 | 26.3908 | 253.351 |
| `sinc-scalar` | 64 | 20.5479 | 197.260 | 27.6099 | 265.055 | 25.9979 | 249.579 |
| `sinc-scalar` | 256 | 20.4202 | 196.034 | 27.3778 | 262.827 | 25.5076 | 244.873 |
| `linear-simd` | 32 | 3.8669 | 37.122 | 4.4662 | 42.876 | 5.2168 | 50.082 |
| `linear-simd` | 64 | 4.2895 | 41.179 | 4.2786 | 41.074 | 4.7844 | 45.930 |
| `linear-simd` | 256 | 3.1373 | 30.118 | 4.1411 | 39.755 | 4.4537 | 42.756 |
| **`hermite-simd`** | **32** | **5.5150** | **52.944** | **6.2214** | **59.725** | **7.2135** | **69.249** |
| **`hermite-simd`** | **64** | **5.4062** | **51.900** | **5.9278** | **56.907** | **6.6150** | **63.504** |
| **`hermite-simd`** | **256** | **4.9994** | **47.994** | **5.7151** | **54.865** | **6.2640** | **60.134** |

**Noise.** The M1 VM is noisy, and the x86 VMs much less so.

- The earlier CI run 40437303454 (2026-10-26, about 21:20 CDT) used `gg-bench --quick`, with
  runs of 0.2 s. On the M1 VM it put `hermite-simd` at **95.0% / 83.4% / 66.0%** at blocks
  32 / 64 / 256, against **52.9% / 51.9% / 48.0%** at full length.
- On Linux the two runs agreed within 2.1% for `hermite-simd` (quick: 67.8% / 63.0% / 60.0%)
  and within 2.6% across all 15 configurations. That run has no Windows numbers, because its
  Windows build failed.
- Even at full length, the M1 VM's scalar numbers are not monotonic in block size. For
  example, `hermite-scalar` is 149.6% / 91.9% / 113.4%.
- Treat the M1 VM numbers as indicative of the floor, not as a measurement of real M1
  silicon. Its noise is why CI runs at full length (ruling T10-c).

### Gap analysis

The budget is 25% of one core for 2,000 grains, which is 2.604 ns per stereo grain-sample.
Figures are for `hermite-simd` at blocks 32 / 64 / 256.

| Machine | % of one core | Multiple of the 25% budget | Grains that fit in 25% | ns relative to the M5 Max |
|---|---|---|---|---|
| M5 Max (ceiling, local) | 21.6 / 20.7 / 19.9 | 0.86 / 0.83 / 0.80 | 2,317 / 2,420 / 2,510 | 1× |
| M1 VM | 52.9 / 51.9 / 48.0 | **2.12 / 2.08 / 1.92** | 944 / 963 / 1,042 | 2.45 / 2.51 / 2.41× |
| Windows (EPYC 7763) | 59.7 / 56.9 / 54.9 | **2.39 / 2.28 / 2.19** | 837 / 879 / 911 | 2.77 / 2.75 / 2.75× |
| Linux (EPYC 7763) | 69.2 / 63.5 / 60.1 | **2.77 / 2.54 / 2.41** | 722 / 787 / 831 | 3.21 / 3.07 / 3.02× |

- **About 2× over budget on the floor hardware.** The M1 VM needs 1.9× to 2.1× the budget and
  the x86 VMs 2.2× to 2.8×. Even `linear-simd` misses on every CI leg: 1.2× to 1.65× on the
  M1 VM, and 1.6× to 2.0× on x86.
- **Block size matters more on CI than on the M5 Max.** Block 32 costs this much more than
  block 256 per grain-sample:

  | Machine | `hermite-simd` | `linear-simd` |
  |---|---:|---:|
  | M5 Max | +8.4% | +9.7% |
  | M1 VM | +10.3% (quick run: +44%) | +23.3% (non-monotonic: block 64 is the slowest) |
  | Windows | +8.9% | +7.9% |
  | Linux | +15.2% (quick run: +13.1%) | +17.1% (quick run: +14.6%) |

  - The M5 Max curve is nearly flat, and Windows matches it.
  - Linux has nearly twice the slope (1.8×), and it reproduces across both runs.
  - On the M1 VM the slope is buried in noise.
  - A steeper slope means the per-grain, per-block costs weigh more on those cores. Those
    costs include the grain update, each grain's setup, and the first, likely cache-missing,
    taps of each grain in each block (caveat 3).
  - This was not profiled. Two of the M1 levers below, per-grain prefetch and cache-aware
    grain scheduling, target exactly those costs.
- **Compiler, same CPU model.** Windows and Linux report the same EPYC 7763.
  - Linux's gcc 12 build runs `hermite-simd` 10% to 16% slower than Windows' Clang 19 build.
  - It runs `hermite-scalar` 7% to 9% faster.
  - These are different VMs, and the codegen was not compared.

## Director ruling (2026-10-26)

Asked how INTENT §16 should respond to the CI miss, the director chose **"Keep it, M1 closes
it"**:

- Keep "at least 2,000 Normal grains at 48 kHz under 25% of one Apple M1 performance core" as
  the **provisional** headline.
- **Closing the gap is an explicit goal of the milestone-M1 renderer.** The candidate levers
  are:
  - per-grain software prefetch;
  - a stereo-interleaved source layout;
  - cache-aware grain scheduling.
- **M1's CI CPU gate starts as a regression gate** at the measured baseline. The headline
  becomes the gate once M1 meets it.
- **The headline is judged again at the end of M1.**

This is recorded in INTENT §16, §19, §20, §21 and §23 (#23).

## Reproduce

```sh
cmake --preset release
cmake --build --preset release --target gg-bench
build/release/tools/gg-bench/gg-bench --json build/bench-m5max.json   # about 2.6 min
build/release/tools/gg-bench/gg-bench --quick --json build/bench-quick.json   # about 16 s

# the CI numbers: each build leg uploads gg-bench-<preset> (gg-bench.json and gg-bench.txt)
gh run download 40439674760 --repo sethbc/giga-grains --pattern 'gg-bench-*' --dir build/ci-bench

# correctness of the kernels
cmake --preset dev && cmake --build --preset dev --target gg-tests
ctest --preset dev -L bench
```
