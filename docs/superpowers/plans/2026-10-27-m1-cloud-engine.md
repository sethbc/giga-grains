# M1 Cloud Engine Implementation Plan

> **Erratum (2026-10-28; INTENT §23 #46, #47):** where the branch differs from this plan, the
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

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the slice's scalar renderer with a structure-of-arrays pool and 4-wide SIMD
Eco/Normal kernels over both sources, make `gg-bench` gate-grade with real-engine cases and
in-process ratios, then try the four §16 levers and keep the winners.

**Architecture:**
- `GrainPool` becomes SoA with 2 × capacity slots, an end-sample min-heap for admission and one
  stable compaction per block.
- A new public header, `engine/include/gg/grain/GrainKernels.h` (`gg::kernels`), holds the scalar
  reference renderers and the SIMD renderers, templated on a reader (`Hermite`, `Linear`) and on
  the output shape. `Engine::renderPool` dispatches per grain, never per sample.
- `gg-bench` splits into a library (`Bench.h/.cpp`: cases, interleaved rounds, statistics, JSON,
  baselines) and a thin `main.cpp`.

**Tech Stack:** C++20, sst-basic-blocks `SIMD_MM` (simde on arm64; SSE up to 4.2 on x86,
`-march=nehalem`), Catch2 v3, nlohmann_json (already in `libs/`), GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-27-m1-cloud-engine-design.md` (rulings 1–5, the scalar
reference, the pool, the bench), with `docs/superpowers/specs/2026-10-27-cpu-headline-design.md`
(the levers and the stop rule). Read both, plus `INTENT.md` §14, §16 and §19 and
`docs/m1-carry-over.md`, before starting any task.

## Global Constraints

Every task's requirements include these. `CLAUDE.md` "Non-negotiables" still holds in full.

- **Output bits change exactly once,** in Task 4. Every other task leaves every golden's metrics
  and WAV bits unchanged, except Task 5, which only adds a golden. **No task changes a
  `*.events.jsonl` file.**
- **The SIMD kernels match the scalar reference bit for bit** (`std::memcmp`), never "within a
  tolerance". They use no horizontal adds and no FMA intrinsics, and evaluate the reference's
  expressions in its order.
- `-ffp-contract=off` everywhere; never fast-math. x86 is `-march=nehalem`: no AVX, no FMA.
- SIMD only through `SIMD_MM(...)` / `SIMD_M128` from `<sst/basic-blocks/simd/setup.h>`, 4-wide
  float, as in `tools/gg-bench/Kernels.h`.
- Positions and the elapsed time e stay double. float only for in-block offsets, windows, gains
  and samples.
- Grains are summed in spawn order. Output is bit-identical at block sizes 1, 7, 32, 64, 511 and
  4096 and under random splits.
- `Engine::process` never allocates, locks or does I/O; it handles 0- and 1-frame calls and
  `numFrames > maxBlockSize`. New audio-path functions are `noexcept SST_CPPUTILS_NONBLOCKING`.
- **No per-sample branches in the SIMD interiors.** Run counts (where a grain sounds, where its
  reads are in bounds, the live interior) are computed once per grain per render call.
- Both plugins stay Normal. No new CLAP parameter, no state-format change.
- The Cloud density ceiling is the sample rate (ruling 5): a positive density clamps to
  `[0.01, sampleRate]`.
- The gate is report-only through M1 (ruling 3): no CI step fails on a bench ratio.
- `gg-bench`'s `reference` case is `gg::bench::renderHermiteSimd` from
  `tools/gg-bench/Kernels.h`, unchanged. Changing that kernel invalidates every baseline.
- **Pushing the branch is outward-facing.** Ask the director before the first push (Task 9).
- Each task ends with `scripts/check.sh` (default presets `dev,asan,rtsan`) printing `PASS`; paste
  its summary in the task report. Format with `scripts/format.sh`.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

The five conditions most likely to bite a user that no task's main tests would hit. Each has a
test in the owning task.

1. **A grain whose reads cross a source end** (position near 0 or the last frame, at any rate):
   the SIMD sample kernel must fall back to the bounds-checked reads for exactly those chunks and
   still match the reference bit for bit. Task 6, `simd sample kernel matches the reference at
   the source ends`.
2. **A freeze toggled while live grains sound, with the ring wrapped:** the head stops mid-block,
   so the live interior's two segments, the edges and the fade must all agree with the
   reference. Task 7, `simd live kernel matches the reference through freeze and wrap`.
3. **A saturated pool whose 2 × capacity storage fills mid-block:** the emergency compaction must
   keep admission (T3-b), event logs and output identical at every block size. Task 3,
   `saturated pool is block-size invariant`.
4. **Extreme rates and very long grains:** pitch ±48 (rates 1/16 and 16) and a 3,600 s grain
   (window phase from e × invDuration over 2^31 samples) must neither misread nor diverge from
   the reference. Task 6, `simd sample kernel matches the reference at extreme rates and lengths`.
5. **Output shapes:** one output channel, a mono source, a null `out[1]`, and 0- and 1-frame
   calls through the SIMD path. Task 6, `simd sample kernel matches the reference for every
   output shape`.

---

### Task 1: Record the rulings in INTENT, and lift the density ceiling

**Files:**
- Modify: `INTENT.md` (§14 clocks bullet, §16, §19, §20 M1 row, §23 new rows #43–#45)
- Modify: `engine/src/CloudScheduler.cpp:12`, `:91`; `engine/include/gg/grain/CloudScheduler.h`
  (`CloudParams::densityHz` doc, the `kMinIntervalSamples` comment's "maximum density (P = 12)")
- Test: `tests/engine/cloud_tests.cpp` (`out-of-range values are clamped`, around `:445`)

**Interfaces:** Produces: `CloudScheduler` clamps a positive density to `[0.01, sampleRate]`.

- [ ] **Step 1: Change the failing expectations first.** In `out-of-range values are clamped`,
  density `1e9` at 48 kHz is now one grain per sample:

```cpp
const auto grains = collect(cloud, 0, 120, 120);
REQUIRE(grains.size() == 120); // the sample rate, distribution 0: every sample
REQUIRE(onsetTime(grains[1]) == 1.0);
```

  Add a section `the density reaches 20 kHz at 48 kHz` with `cloudAt(20000.0)`, 4,800 frames:
  `grains.size() == 2000` and `onsetTime(grains[1]) == 2.4`.
- [ ] **Step 2:** `ctest --preset dev -L cloud` fails on those lines.
- [ ] **Step 3:** Delete `kMaxDensityHz`; `maxHz = std::max(kMinDensityHz, sampleRate_)`. Update
  the docs to "clamped to [0.01, sampleRate]" and the floor comment's example to 20 kHz (P = 2.4).
- [ ] **Step 4:** `ctest --preset dev -L 'cloud|golden|determinism'` passes, no golden changed.
- [ ] **Step 5: INTENT.md**, each Part II edit under the new rows:
  - §14 clocks: "free-running Hz (0.01 Hz up to the sample rate; the plugins' Density stops at
    400 Hz)".
  - §19 CPU gate bullet: append "Through M1 it reports and never fails; sub-project 6 makes it
    blocking, with the published X."
  - §16: under the headline, "`gg-bench` also times the same case over the live ring; that is
    published beside X as the FX's figure."
  - §23 **#43** "Sub-project 2's kernel bits and the Eco tier" (rulings 1 and 2, spec §2–§3);
    **#44** "The CPU gate is report-only through M1; X is the Sample-mode case, the live figure
    beside it" (rulings 3 and 4); **#45** "The Cloud density ceiling is the sample rate"
    (ruling 5; amends M1-a in #36). Each: "Decided 2026-10-27 by the director, during M1
    sub-project 2", pointing at the spec.
- [ ] **Step 6:** `scripts/check.sh`; commit `engine, docs: density ceiling at the sample rate;
  record sub-project 2's rulings (INTENT §23 #43–#45)`.

### Task 2: The fp-contract guard (carry-over [70])

**Files:**
- Create: `engine/include/gg/core/FpContract.h`
- Modify: `engine/include/gg/core/MathDet.h`, `Interpolate.h`, `Window.h`
- Test: `tests/engine/fp_contract_tests.cpp`, `tests/engine/fp_contract_fast.cpp` (compiled with
  `-ffp-contract=fast`), `tests/CMakeLists.txt`

**Interfaces:** Produces: macro `GG_FP_CONTRACT_OFF`, placed as the first statement of every
inline function body in those three headers and, from Task 4 on, in `GrainKernels.h`. On Clang
(ClangCL included) it is `_Pragma("clang fp contract(off)")`; on GCC it is empty, and the header
says GCC relies on the project-wide `-ffp-contract=off`.

- [ ] **Step 1: Write the test.** `fp_contract_fast.cpp` defines
  `float ggFastHermite(const float *, int64_t, double)`, `double ggFastExp2(double)`,
  `double ggFastLog(double)` and `float ggFastHann(double)`, each forwarding to the header
  function. `tests/CMakeLists.txt` sets `COMPILE_OPTIONS -ffp-contract=fast` on that file when
  the compiler is Clang (ClangCL: `/clang:-ffp-contract=fast`); under GCC, whose guard is empty,
  the file builds with the project flags and the test still runs. Test
  `inline math is not contracted by an includer's flags`
  `[math]`: over 10,000 inputs from `gg::rng`, each `ggFast*` equals the in-TU call
  bit for bit.
- [ ] **Step 2:** On arm64 Clang the test fails (FMAs appear in the fast TU).
- [ ] **Step 3:** Add `FpContract.h` and the macro to every inline function in the three headers.
- [ ] **Step 4:** `ctest --preset dev -L math` passes; `scripts/check.sh` PASS.
- [ ] **Step 5:** Commit `engine: guard inline math against FP contraction (carry-over [70])`.

### Task 3: The SoA pool, heap admission and once-per-block compaction (carry-over [103])

**Files:**
- Modify: `engine/include/gg/grain/GrainPool.h`, `engine/src/GrainPool.cpp`,
  `engine/src/Engine.cpp` (`admit`, `renderPool`, `renderGrain`, `renderLiveGrain`)
- Test: `tests/engine/pool_tests.cpp`, `tests/engine/determinism_tests.cpp`,
  `tests/engine/rt_tests.cpp`

**Interfaces:** Produces (later tasks extend `GrainRows` with more fields):

```cpp
struct GrainRows // pointers to the pool's arrays; entries [0, size()) in spawn order
{
    const uint64_t *index;
    const int64_t *onsetSample;
    const double *onsetFrac;
    const double *startPos;
    const double *rate;
    const double *durationSamples;
    const float *gain;
    const float *pan;
    const int64_t *endSample; // grainEndSample of each
};

class GrainPool
{
  public:
    explicit GrainPool(uint32_t capacity);  // allocates 2 × capacity slots and the heap
    uint32_t capacity() const noexcept;     // the admission limit
    uint32_t size() const noexcept;         // stored grains, finished or not; <= 2 × capacity
    /// Pops every stored end <= onset, then: fewer than capacity grains sound at onset.
    /// Onsets passed here must not decrease between clear()s.
    bool canAdmit(int64_t onset) noexcept SST_CPPUTILS_NONBLOCKING;
    bool storageFull() const noexcept;      // size() == 2 × capacity
    bool spawn(const GrainSpec &g) noexcept SST_CPPUTILS_NONBLOCKING; // false when storageFull
    void retireFinished(int64_t nowSample) noexcept SST_CPPUTILS_NONBLOCKING; // stable
    void clear() noexcept SST_CPPUTILS_NONBLOCKING;
    const GrainRows &rows() const noexcept;
    GrainSpec spec(uint32_t i) const noexcept; // i < size()
};
```

`at()` and `earliestEnd()` go. `Engine::admit` becomes: `if (!pool_.canAdmit(g.onsetSample))`
drop; else if `storageFull()`, render the pool to the onset, set `renderedTo`, `retireFinished`
at the onset; then `spawn`. The renderers read `rows()` fields; output bits don't change.

- [ ] **Step 1: Write the tests.**
  - `pool admits by sounding grains, not stored ones` `[pool]`: capacity 4; spawn 4 grains
    ending at 100; `canAdmit(99)` is false; `canAdmit(100)` is true; spawn 4 more without any
    `retireFinished`; `size() == 8`, `storageFull()`; `spawn` of a 9th returns false;
    `retireFinished(100)` leaves the last 4 in spawn order.
  - Rewrite `pool full drops deterministically` and `pool keeps spawn order through retirement`
    on `canAdmit`/`spec(i).index`.
  - `pool admission matches a brute-force count` `[pool]`: capacity 64, 20,000 grains from
    `gg::rng` (durations 1–500, onsets non-decreasing, steps 0–3); a reference counts the
    admitted grains with end > onset by a linear scan; the admitted index sequences are equal.
  - `saturated pool is block-size invariant` `[determinism]`: capacity 64, density 20 kHz, 50 ms
    grains (about 2,400 wanted), 1 s; the audio and the sink's GrainSpec sequence are identical
    at block sizes 1, 7, 64, 511, 4096 and `randomSplit()`, and `droppedGrains()` is equal and
    > 0.
  - `rt_tests.cpp` `engine process does not allocate with a full pool or odd block sizes`: add a
    2 × capacity-filling configuration (capacity 16, density 20 kHz, 1 s grains).
- [ ] **Step 2:** The new tests fail to compile (no `canAdmit`).
- [ ] **Step 3:** Implement. The heap is a preallocated `std::vector<int64_t>` used with
  `std::push_heap`/`std::pop_heap` and `std::greater<>`. `retireFinished(now)` pops ends ≤ now,
  then compacts every array stably.
- [ ] **Step 4:** `ctest --preset dev -L 'pool|engine|live|determinism|golden|rt'` passes, and
  `git status tests/golden` is clean.
- [ ] **Step 5:** `scripts/check.sh`; commit `engine: SoA grain pool with heap admission,
  compacted once per block (carry-over [103])`, the body noting that Task 9's
  `engine-full-pool` is [103]'s bench case.

### Task 4: The scalar reference renderers, and the one golden regeneration

**Files:**
- Create: `engine/include/gg/grain/GrainKernels.h`
- Modify: `engine/include/gg/grain/GrainPool.h` (`GrainRows` gains `invDuration`, `gainL`,
  `gainR`), `engine/src/GrainPool.cpp` (`spawn` latches them), `engine/src/Engine.cpp`
  (`renderPool` calls the kernels; `renderGrain`/`renderLiveGrain` go),
  `engine/include/gg/engine/Engine.h`, `engine/CMakeLists.txt` (link `sst-basic-blocks simde`
  PUBLIC)
- Modify: `tests/golden/expected/*.metrics.json` (regenerated)
- Test: `tests/engine/kernel_tests.cpp` (new)

**Interfaces:** Produces, in `namespace gg::kernels`:

```cpp
struct SampleSource { const float *data[2]; int64_t frames; bool mono; };
struct Output { float *dst[2]; int64_t blockStart; }; // dst[0] non-null; dst[1] null = one output
struct Hermite; // static float read(const float *, int64_t frames, double pos) = interp::hermite4
                // static bool readLive(const LiveSource &, double pos, int64_t head,
                //                      LiveSource::Cursor &, float &l, float &r)
template <class Reader>
void renderSampleRef(const GrainRows &, uint32_t i, const SampleSource &, const Output &,
                     int64_t from, int64_t to, const HannTable &) noexcept SST_CPPUTILS_NONBLOCKING;
template <class Reader>
void renderLiveRef(const GrainRows &, uint32_t i, const LiveSource &, const Output &,
                   int64_t from, int64_t to, const HannTable &) noexcept SST_CPPUTILS_NONBLOCKING;
```

`invDuration = 1.0 / durationSamples` (double); `gainL = gain * panGains(pan).left`,
`gainR = gain * panGains(pan).right` (float). The arithmetic is the spec's §3 table, exactly:
`e = double(t − onsetSample) − onsetFrac`, `w = hann.at(e * invDuration)`, skip unless `w > 0`,
`pos = startPos + rate * e`, then `dst[c][j] += read_c(pos) * (w * gain_c)`; one output uses
source channel 0 and `gain`. Live: `f = float(live.fade(pos, R(t)))`, skip unless `f > 0` and
the read succeeds, then `dst[c][j] += read_c * ((w * f) * gain_c)`.

- [ ] **Step 1: Write the tests** in `kernel_tests.cpp` `[kernel]`:
  - `sample reference follows the spec's arithmetic`: one grain (onsetFrac 0.37, rate 1.5,
    duration 480, pan 0.4, gain 0.8) over a 2,000-frame noise source; an inline loop written
    from the spec's table produces the same bits, both outputs.
  - `reference reads nothing where the window is zero`: a source of NaN except frames
    [100, 600); a grain reading only [100, 600) inside its window, with its onset sample and
    its last two samples at NaN positions; every output sample is finite (closes T4 (126) for
    both kinds; the live variant writes the same pattern into a ring).
  - `live reference follows the spec's arithmetic`: as above, through a 1 s ring with a freeze
    on at frame 3,000.
- [ ] **Step 2:** They fail to compile.
- [ ] **Step 3:** Implement `GrainKernels.h` (with `GG_FP_CONTRACT_OFF`) and route `renderPool`
  through it: per grain, `renderSampleRef<Hermite>` (or live), with the one-output and mono
  cases as the `Output`/`SampleSource` shapes.
- [ ] **Step 4:** `ctest --preset dev -L 'kernel|engine|live|determinism'` passes. The golden
  tests fail on metrics only: `ctest --preset dev -L golden` shows no `events` mismatch.
- [ ] **Step 5: Regenerate.** `scripts/update-goldens.sh`; `git diff --stat
  tests/golden/expected` lists only `*.metrics.json`. Open every spectrogram in
  `build/dev/golden/`, and compare each case's RMS, peak and spectral figures before and after
  (they move in the last digits). Keep `build/dev/golden/` from before this step as
  `build/dev/golden-pre-t4/` for Task 14's A/B.
- [ ] **Step 6:** `scripts/check.sh`; commit `engine: scalar reference renderers; regenerate the
  audio-metric goldens (ruling 1)`, the body naming the two changes (phase by multiplication,
  one latched channel gain), that event logs are unchanged, and the largest metric delta.

### Task 5: The Eco tier

**Files:**
- Create: `engine/include/gg/grain/Tier.h`
- Modify: `CloudScheduler.h` (`CloudParams::quality`, `GrainDraw::tier`), `GrainPool.h`
  (`GrainSpec::tier`, `GrainRows::tier`), `Engine.cpp` (`sampleGrain`, `liveGrain`, per-grain
  dispatch), `LiveSource.h/.cpp` (`readLinear`), `GrainKernels.h` (`Linear`),
  `tools/gg-render/RenderSpec.{h,cpp}` (`StreamSpec::quality`), `tools/gg-render/Render.cpp`
- Create: `tests/golden/cases/dense-noise-eco.json`, its expected files
- Test: `tests/engine/kernel_tests.cpp`, `tests/engine/cloud_tests.cpp`,
  `tests/tools/render_spec_tests.cpp`, `tests/engine/determinism_tests.cpp`

**Interfaces:**
- `enum class Tier : uint8_t { Eco, Normal };` in `gg/grain/Tier.h`.
- `CloudParams::quality{Tier::Normal}`, copied unsanitized (an enum) into every `GrainDraw::tier`
  and `GrainSpec::tier` (the last member of each).
- `struct Linear` in `gg::kernels`: `read` = `interp::linear`; `readLive` =
  `LiveSource::readLinear(pos, head, Cursor &, float &, float &)`, taps floor(pos) and
  floor(pos) + 1, false unless both are in [head − N, head − 1].
- `renderPool` dispatches each grain on `rows().tier[i]`.
- gg-render: optional `stream.quality`, `"eco"` or `"normal"` (default); anything else is
  `stream.quality must be "eco" or "normal"`. The event line does not print the tier.

- [ ] **Step 1: Write the tests.**
  - `cloud_tests.cpp`: `quality is latched per grain`: Eco for grains 0–4, Normal after;
    `grains[3].tier == Tier::Eco`, `grains[7].tier == Tier::Normal`.
  - `kernel_tests.cpp`: `eco reference reads linearly` (sample and live; as Task 4's arithmetic
    test with `interp::linear`).
  - `render_spec_tests.cpp`: `stream.quality parses and rejects`: `"eco"` → `Tier::Eco`; missing
    → Normal; `"hq"` and `3` throw with the message above.
  - `determinism_tests.cpp`: `eco render is identical across block sizes`.
  - The golden `dense-noise-eco`: `dense-noise.json` plus `"quality": "eco"`. Its event log must
    equal `dense-noise.events.jsonl` byte for byte: test `eco golden has the normal golden's
    events` `[golden]` compares the two expected files.
- [ ] **Step 2:** They fail.
- [ ] **Step 3:** Implement. Processor and plugins are untouched (they keep the default).
- [ ] **Step 4:** `scripts/update-goldens.sh`; only `dense-noise-eco.*` is new, nothing else in
  `git status tests/golden` changed. Look at its spectrogram against `dense-noise`'s (Eco has
  more high-frequency imaging).
- [ ] **Step 5:** `scripts/check.sh`; commit `engine, gg-render: the Eco tier, latched per grain
  (ruling 2)`.

### Task 6: The SIMD sample kernels

**Files:**
- Modify: `engine/include/gg/grain/GrainKernels.h`, `engine/src/Engine.cpp`
- Test: `tests/engine/kernel_tests.cpp`

**Interfaces:** Produces `template <class Reader> void renderSampleSimd(...)`, same signature
as `renderSampleRef`. `Engine::renderPool` calls it for Sample mode.

The algorithm the signature leaves open:
- **Run counts, per grain per call:** `[a, b)` = `[max(from, onsetSample), min(to, endSample))`.
  Within it, the in-bounds run is where every tap of `pos(t)` is in `[0, frames)` (the reader's
  reach: Hermite −1 … +2, Linear 0 … +1). pos is monotone in t (rate > 0), so compute its first
  and last t from the bound positions, then step each end inward while the exact reference
  `pos(t)` falls outside, so rounding can't admit an out-of-bounds lane. Outside the in-bounds
  run, call `renderSampleRef` on those samples.
- **Inside it, 4 samples per step:** per lane, scalar double e, pos and the window's
  `x = (e * invDuration) * kSize`, index and float fraction exactly as `HannTable::at`; gather
  the table and the taps; then 4-wide float: window lerp, `w > 0` mask, the reader, `amp_c = w *
  gain_c`, `y = read * amp`, masked add into `dst`. Remainders (< 4 samples) go to the reference.
  A masked lane adds +0, which leaves `dst` unchanged because `dst` is never −0 (it starts at +0,
  and x + y is −0 only when both are), so the mask equals the reference's skip.
- `HannTable` gains `const float *points() const noexcept` (its kSize + 1 entries) for the gather.

- [ ] **Step 1: Write the tests** `[kernel]`, each comparing `renderSampleSimd<R>` and
  `renderSampleRef<R>` with `std::memcmp`, for `R` in {Hermite, Linear}, over a pool of 500
  random grains (`gg::rng`; rates 2^[−2, 2], onsetFrac in [0, 1), durations 16–9,600, pans
  ±1) and `from`/`to` drawn per call, on a 2 s stereo noise source:
  - `simd sample kernel matches the reference` (block 4096, plus 200 random `[from, to)`).
  - `simd sample kernel matches the reference at the source ends` (Review Focus 1): positions
    within 3 frames of 0 and of `frames − 1`, both directions of approach, rates 0.25–4.
  - `simd sample kernel matches the reference at extreme rates and lengths` (Review Focus 4):
    rates 1/16 and 16; one grain of 3,600 s at 768 kHz (durationSamples 2,764,800,000) rendered
    at t near its middle and its end.
  - `simd sample kernel matches the reference for every output shape` (Review Focus 5): one
    output; mono source; two outputs from a mono source; `to − from` of 0, 1, 2, 3 and 5.
  - `simd sample kernel matches the reference with non-finite taps`: NaN and inf frames in the
    source; the outputs compare equal with NaN == NaN.
- [ ] **Step 2:** They fail to compile.
- [ ] **Step 3:** Implement; switch `renderPool`'s Sample path to `renderSampleSimd`.
- [ ] **Step 4:** `ctest --preset dev -L 'kernel|engine|determinism|golden|rt'` passes with
  `git status tests/golden` clean (bit-identical to Task 4).
- [ ] **Step 5:** `scripts/check.sh`, then on the `release` preset `ctest -L 'kernel|golden'`
  (optimizer-dependent bit drift shows here) and `build/release/tests/gg-tests` under
  `arch -x86_64` if a universal build is at hand (CI's Rosetta step covers it otherwise).
- [ ] **Step 6:** Commit `engine: 4-wide SIMD sample kernels, bit-identical to the reference`.

### Task 7: The SIMD live kernel

**Files:**
- Modify: `engine/include/gg/grain/GrainKernels.h`, `engine/include/gg/source/LiveSource.h`
  (`RingView`, `view()`, `blockKink()`), `engine/src/Engine.cpp`
- Test: `tests/engine/kernel_tests.cpp`

**Interfaces:** Produces:
- `struct LiveSource::RingView { const float *channel[2]; int64_t capacity; int64_t frames; };`
  `RingView view() const noexcept;` (slots `0 … capacity + 2` readable, the guard mirrored).
- `int64_t LiveSource::blockKink() const noexcept` = `blockStart_ + blockAdvance_`: from this
  sample on, the head stands still for the rest of the block.
- `template <class Reader> void renderLiveSimd(...)`, the signature of `renderLiveRef`.
- `struct LiveInterior { int64_t begin[2], end[2]; };`
  `LiveInterior liveInterior(const GrainRows &, uint32_t i, const LiveSource &, int64_t from,
  int64_t to) noexcept;`

The algorithm:
- **Interior.** Split `[a, b)` at `blockKink()`. On each part R(t) and pos(t) are linear, so the
  conditions `R − 1 − (pos + 2) ≥ M + 1` and `(pos − 1) − (R − N) ≥ M + 1` (one frame beyond the
  fade's margin, far more than any rounding) each hold on a half-line. Solve each in double, then
  step the ends inward until the exact reference `fade(pos(t), R(t)) == 1.0` and `read` would
  succeed at the end samples. There f is exactly 1, so the reference's amp is `w * gain_c`.
- **The interior renders 4-wide** like the sample kernel, reading slots from `view()`: the first
  tap's slot comes from one `%` per chunk, then the lanes' slots by adding the frame offsets and
  subtracting `capacity` once if past it. Everything outside the interiors calls `renderLiveRef`.

- [ ] **Step 1: Write the tests** `[kernel]`, each `std::memcmp` against `renderLiveRef<R>`,
  `R` in {Hermite, Linear}, 300 random grains, the ring written block by block as the engine
  does:
  - `live interior is exactly where the fade is one`: for every t in each interior, the
    reference fade is 1.0 and `read` succeeds; for 1,000 random grains.
  - `simd live kernel matches the reference` (not frozen; rates 0.25–4).
  - `simd live kernel matches the reference through freeze and wrap` (Review Focus 2): a 0.5 s
    ring wrapped 20 times; freeze on and off at 30 random samples, some mid-block; blocks of 1,
    7, 64 and 511.
- [ ] **Step 2:** They fail to compile.
- [ ] **Step 3:** Implement; switch `renderPool`'s Live path to `renderLiveSimd`.
- [ ] **Step 4:** `ctest --preset dev -L 'kernel|live|determinism|golden|rt|core|plugin'` passes,
  `git status tests/golden` clean.
- [ ] **Step 5:** `scripts/check.sh`; commit `engine: 4-wide SIMD live kernel over the fade's
  interior (closes carry-over T4 (126))`.

### Task 8: `gg-bench` gate-grade: rounds, statistics, JSON v2, schema test

**Files:**
- Create: `tools/gg-bench/Bench.h`, `tools/gg-bench/Bench.cpp`
- Modify: `tools/gg-bench/main.cpp` (argument parsing, printing), `tools/gg-bench/CMakeLists.txt`
  (`gg-bench-lib`, linked by `gg-bench` and `gg-tests`; `nlohmann_json`)
- Test: `tests/tools/bench_tests.cpp` (new), `tests/CMakeLists.txt` (`bench_cli`)

**Interfaces:** Produces, in `gg::bench`:

```cpp
struct Options
{
    double runSeconds{2.0};          // --seconds; --quick = 0.2
    int rounds{5};                   // --rounds
    uint32_t grains{2000};           // --grains
    std::vector<std::string> cases;  // --cases a,b; empty = all
    std::string commit{"unknown"};   // --commit
};
struct CaseResult
{
    std::string name;
    std::string kind;                // "kernel" | "engine"
    uint32_t blockSize;
    double grains;                   // nominal (kernel) or mean active (engine)
    std::vector<double> nsPerGrainSample; // one per round, in round order
    double median, min, iqr;         // of nsPerGrainSample
    double cpuPctOneCoreAt48k;       // from the median
};
struct Report { Options options; std::string machine, compiler; std::vector<CaseResult> results; };
Report run(const Options &);
std::string toJson(const Report &);   // schemaVersion 2
std::vector<uint32_t> roundOrder(uint32_t configurations, int round); // the run order of a round
double quantile(std::vector<double> v, double q);                    // linear interpolation
```

- **Rounds:** each configuration (case × block size 32/64/256) is built once and warmed up once;
  then for round r in `[0, rounds)` every configuration runs once for `runSeconds`, in a fixed
  order rotated by r, so no configuration always follows the same one.
- `iqr` is `q(0.75) − q(0.25)` with linear interpolation between order statistics.
- JSON v2 top level: `schemaVersion`, `machine`, `compiler`, `commit`, `runSeconds`, `rounds`,
  `results` (each `CaseResult`'s fields, `nsPerGrainSample` as `runs`), and `ratios` (Task 9;
  an empty array here). The text table prints case, block, median, min, IQR, % core.

- [ ] **Step 1: Write the tests** `[bench]`:
  - `bench json matches schema 2`: `run({.runSeconds = 0.002, .rounds = 3, .grains = 50,
    .cases = {"hermite-simd"}})`, parse `toJson` with nlohmann; every key above is present with
    its JSON type; 3 results; each has 3 runs; `min <= median`; `iqr >= 0`.
  - `bench statistics`: the quantile helper on {1, 2, 3, 4} gives median 2.5, IQR 1.5.
  - `bench rounds rotate the order`: `roundOrder(4, r)` (exposed for the test) is a rotation by
    r of {0, 1, 2, 3}.
  - `bench_cli` (`add_test`): `gg-bench --seconds 0.002 --rounds 2 --grains 50 --cases
    hermite-simd --json <build>/bench_cli.json`, exit 0.
- [ ] **Step 2:** They fail to build.
- [ ] **Step 3:** Move the workload and kernel timing out of `main.cpp` into `Bench.cpp`;
  `--canvas` stays in `main.cpp` unchanged.
- [ ] **Step 4:** `ctest --preset dev -L bench` and `bench_cli` pass. On `release`,
  `gg-bench --quick` prints all five kernels.
- [ ] **Step 5:** `scripts/check.sh`; commit `gg-bench: interleaved rounds, every run with
  median, min and IQR, JSON schema 2 (carry-over B-I2, [133], [132])`.

### Task 9: Real-engine cases, ratios, the baseline verdict, CI reporting

**Files:**
- Modify: `tools/gg-bench/Bench.{h,cpp}`, `tools/gg-bench/main.cpp`,
  `.github/workflows/ci.yml` (bench step, ~`:128`)
- Create: `tools/gg-bench/baselines/README.md` (format; baselines land in Task 14)
- Test: `tests/tools/bench_tests.cpp`

**Interfaces:** Produces:
- Engine cases, each one `gg::Engine` (48 kHz, `maxBlockSize` 256, `grainCapacity` 4096, seed
  the bench seed) over the bench's 60 s stereo noise, `CloudParams{.densityHz = grains / 0.1,
  .distribution = 0, .grainDurationSec = 0.1, .position = 0.5, .spray = 0.45,
  .pitchSemitones = 7.0, .stereo = 1.0, .gain = 0.5f}`. Spray 0.45 keeps every read inside
  [0.05, 0.95] of the source, so no grain piles up at a clamped end:
  - `engine-sample` (X), `engine-sample-eco` (`quality = Tier::Eco`);
  - `engine-live`: `liveSeconds` 16, Live mode, `position` 4.0, `spray` 8.0, the noise fed as
    input each block;
  - `engine-full-pool`: `grainCapacity = grains + 48`, `grainDurationSec` 1.0 at the same
    density (10× oversubscribed).
  - Warm-up, before round 0: 2 × the grain duration of audio; `engine-live` warms up for 16 s, so
    the ring is full. The timed block is one
    `Engine::process` call; `grains` is the mean of `activeGrains()` over the timed blocks;
    `cpuPctOneCoreAt48k = nsPerFrame × 48000 / 1e7`.
- `reference`: `renderHermiteSimd` on the M0 workload, at the same block sizes.
- `struct Ratio { std::string name; uint32_t blockSize; std::vector<double> runs; double median,
  iqr; };` — `runs[r] = case.ns[r] / reference.ns[r]`, same block size, same round. `Report`
  gains `std::vector<Ratio> ratios`, in the JSON.
- Baseline file (JSON): `{"schemaVersion": 1, "leg": "<preset>", "commit": "<sha>",
  "ratios": [{"case", "blockSize", "ratio", "tolerance"}]}`.
  `struct Verdict { std::string name; uint32_t blockSize; double ratio, limit; bool regressed; };`
  `std::vector<Verdict> compare(const Report &, const nlohmann::json &baseline);` with
  `limit = ratio × (1 + tolerance)`.
  `nlohmann::json makeBaseline(const std::string &leg, const std::vector<nlohmann::json> &runs);`
  `ratio` = median over the runs; `tolerance = max(0.05, 2 × (max − min) / median)`.
- CLI: `--baseline file` prints the verdicts and exits 3 if any regressed (0 otherwise; 1 error,
  2 usage). `--make-baseline <leg> <out.json> <in.json>...` writes a baseline and exits.

- [ ] **Step 1: Write the tests** `[bench]`:
  - `engine cases report their grain counts`: `--grains 50`, `engine-sample`: mean grains in
    [49, 51]; `engine-full-pool`: in [97, 98] (capacity 98); `engine-live`'s > 45.
  - `ratios pair runs from the same round`: a hand-built `Report` with case runs {2, 4, 6} and
    reference {1, 2, 2} gives runs {2, 2, 3}, median 2.
  - `baseline verdicts`: ratio 2.0 vs baseline 1.8 with tolerance 0.05 regresses (limit 1.89);
    1.85 passes; a case missing from the baseline is reported, not regressed.
  - `make baseline`: three runs with ratios 1.0, 1.1, 1.2 give ratio 1.1, tolerance
    max(0.05, 0.4 / 1.1).
- [ ] **Step 2:** They fail.
- [ ] **Step 3:** Implement. CI: pass `--commit "${{ github.sha }}"`; when
  `tools/gg-bench/baselines/${{ matrix.preset }}.json` exists, add `--baseline` with it; the step
  gets `continue-on-error: true`; its summary prints the ratio and verdict tables. Rename the step
  "Benchmark (full length; ratios reported, not gated through M1)". The full run grows to about 5
  minutes: raise its `timeout-minutes` to 30.
- [ ] **Step 4:** `ctest --preset dev -L bench` passes. On `release`, record one full
  `gg-bench --json` on the M5 Max as `build/release/bench-t9.json`.
- [ ] **Step 5:** `scripts/check.sh`; commit `gg-bench: real-engine cases, in-process ratios and
  a report-only baseline verdict (ruling 3)`.
- [ ] **Step 6: Ask the director to approve pushing the branch.** Then push and let CI run; keep
  each leg's `gg-bench-<preset>` artifact (`gh run download`) as the pre-lever numbers.

### Tasks 10–13: The four levers (spec §6; INTENT §16)

Each lever task has the same shape. The lever's own mechanism is below each heading.

**Files (each):** the kernel or source files named in its mechanism; `docs/spikes/m1-cpu-levers.md`
(created in Task 10: one section per lever, with tables of ratios before and after, median and
IQR, per leg and block size).

- [ ] **Step 1:** Implement the lever. All Task 6/7 `[kernel]` tests and the goldens must pass
  unchanged: no lever changes a bit.
- [ ] **Step 2:** `scripts/check.sh` PASS.
- [ ] **Step 3:** On `release`, on the idle M5 Max: `gg-bench --cases
  reference,engine-sample,engine-live,engine-sample-eco --json`, three times; compare each
  engine ratio's median against the previous kept state's, with its IQR.
- [ ] **Step 4:** Commit, push, and collect each CI leg's artifact.
- [ ] **Step 5: Judge** (INTENT §16): kept if any leg's ratio for `engine-sample` or
  `engine-live` improves by more than that leg's ratio IQR, and no leg's ratio worsens by more
  than its IQR. Judge each source path on its own case; a lever may be kept for one path only.
- [ ] **Step 6:** Record the tables and the verdict in `docs/spikes/m1-cpu-levers.md`. If
  rejected, `git revert` the lever commit (keep the doc). Commit the doc.

**Task 10, per-grain software prefetch.** `GG_PREFETCH(p)` in `GrainKernels.h` is
`__builtin_prefetch(p, 0, 3)` (every toolchain here is Clang, ClangCL or GCC). Before
rendering grain i, prefetch the first taps of grain i + D at its first position in `[from, to)`
(both channels; on the ring, its slot). Try D = 4, 8 and 16 on the M5 Max; keep the best D for
the CI run.

**Task 11, a stereo-interleaved source.** `Engine::setSource` builds, for a stereo source, an
interleaved copy (L, R per frame, plus 3 guard frames of zeros at each end, so a Hermite read
needs one address); the SIMD sample kernel reads pairs from it. The ring stores L, R per slot
(its guard mirrored as now). `SampleBuffer` is unchanged; the copy costs 2× source memory, which
the doc records. Judge the two paths separately.

**Task 12, contiguous loads with shuffles.** A 4-lane chunk's taps span
`floor(pos₃) − floor(pos₀) + before + after + 1` frames, at most `ceil(3 × rate) + before + after
+ 1`. Decide per grain per call: when that bound is ≤ 12 (Hermite: rate ≤ 8/3, which covers the
bench's +7 semitones; Linear: rate ≤ 10/3), load three 4-float vectors from the chunk's first tap
and select each lane's taps by byte shuffles (NEON `vqtbl3q_u8` via simde; on x86, SSSE3
`pshufb` on each vector, OR-combined); otherwise the gather. Same tap values, so the same bits.

**Task 13, cache-aware grain scheduling.** Per tile of 32 frames: visit the pool's grains in
order of their current source position (an index array, preallocated at 2 × capacity, kept
sorted by insertion sort between tiles), render each into its own scratch row (preallocated
2 × capacity × 32 × 2 floats), then add the rows into `dst` in spawn order. Each sample's sum
is the same values in the same order, so the same bits. Then optimization stops (§23 #39).

### Task 14: Baselines, docs and the director's check

**Files:**
- Create: `tools/gg-bench/baselines/{mac-universal,windows-clangcl,linux-gcc12}.json`
- Modify: `CLAUDE.md` (Benchmarks), `docs/m1-carry-over.md`, `docs/spikes/m1-cpu-levers.md`,
  `engine/include/gg/grain/GrainPool.h` (class comment: SoA is here),
  `tools/gg-bench/main.cpp` (header comment)
- Modify (memory, outside the repo): the M1 progress note

- [ ] **Step 1: Baselines.** With the final engine, three CI runs on the branch (re-run the
  workflow twice); `gh run download` each leg's artifact; `gg-bench --make-baseline <preset>
  tools/gg-bench/baselines/<preset>.json a.json b.json c.json` per leg; commit; one more CI run
  shows every verdict `pass` in the step summary.
- [ ] **Step 2: CLAUDE.md** Tests: add the `kernel` label (the scalar references and SIMD kernels). Benchmarks: the new options (`--rounds`, `--seconds`, `--grains`,
  `--cases`, `--commit`, `--baseline`, `--make-baseline`), the cases and what X is, the ratio,
  the report-only gate through M1, how to refresh a baseline, the ~5-minute full run.
- [ ] **Step 3: Carry-over.** Mark **Closed** with their commits: [70], [103], B-I2, [133],
  [132], T4 (126). Add what this sub-project leaves open, each with its owner (sub-project 6 for
  the blocking gate and X).
- [ ] **Step 4: The director's check.** Render A/B files for every pre-existing golden case:
  `build/dev/golden-pre-t4/<case>.wav` against `build/dev/golden/<case>.wav`, plus
  `dense-noise` against `dense-noise-eco`. Checklist for the director: each A/B pair sounds the
  same (ruling 1 changed only the last bits); Eco is audibly brighter or grainier on
  `dense-noise-eco` but not broken; the FX in Ableton Live and Bitwig on a drum loop with
  Density at 400 Hz and Size 2 s still plays without clicks and at lower CPU in the host meter
  than `main`.
- [ ] **Step 5:** `scripts/check.sh` PASS; final whole-branch review (opus), run alongside the
  director's check; then superpowers:finishing-a-development-branch.
