# M3 Grain Shapes (Reverse and Skew) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two per-layer controls latched at each grain's onset: **Reverse**, the probability that a grain reads its forward twin's span backward, and **Skew**, the window's peak moved early or late at the same mean and energy. Both are route and per-voice targets; at rest nothing moves.

**Architecture:** A reversed grain is a grain with a negative rate whose start is its forward span's far end (G2, G3), so the kernels keep one arithmetic. Every place that reads the rate's magnitude (the sinc's stretch, the mip level, the run finders, the ring tap locators, the edge at frame 0) learns the sign first (Tasks 1 to 3), bit for bit against the scalar references. Then the window gains Skew (Task 4), the scheduler gains the draw and the sources the mirror and the ring's reversed clamp (Tasks 5, 6), and the Processor, plugin, `gg-render` and goldens follow.

**Tech Stack:** C++23, sst-basic-blocks' `ParamMetaData`, Catch2, CMake presets, `gg-render` goldens, `gg-bench`.

**Spec:** `docs/superpowers/specs/2026-11-07-m3-grainshape-design.md` (G0 to G16, GB1 to GB17; parent `docs/superpowers/specs/2026-11-07-m3-program-design.md`). File:line references are at `d699c1e`.

## Global Constraints

- Audio thread: no allocation, locks, I/O, syscalls or unbounded loops; RTSan-clean; 0- and 1-sample blocks; `params.flush` trivial.
- Determinism: randomness only from `gg::rng` (seed, layer, stream, field, index); no `std::` distributions; output bit-identical at any host block size; layers summed A to D.
- At rest nothing moves: with the new feature at rest every existing golden keeps its WAV SHA-256. The at-rest check: `/home/sethbc/src/giga-grains-m3-voice/.superpowers/sdd/2026-11-07-m3-voice/check-wav-hashes.sh <worktree>` compares the 108 M2-era cases (last line `baseline cases: 108, changed: 0, gone: 0`); also confirm the 8 voice goldens (tests/golden/cases/*voice*) are unchanged unless the task changes voices on purpose.
- Stage files by name; never `git add -A` or `git commit -a` (the at-rest check rewrites expected files while it runs). Never commit regenerated expected files of existing cases (this Linux machine's metrics differ in the last digit).
- CLAP IDs append-only; your sub-project's index ranges as the spec says.
- `-Wall -Wextra -Werror`; GCC 13's diagnostics (CLAUDE.md). Test names ASCII only; tests parallel-safe.
- Format: `CLANG_FORMAT=$HOME/.local/share/giga-grains/format-venv/bin/clang-format scripts/format.sh`.
- New build trees: `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Models: implementers opus for design-heavy tasks, sonnet otherwise; reviewers sonnet at least; never Fable.
- Branch hygiene: this branch merges `m3-voice` (sub-project 1) as it moves; parallel sub-projects 2, 3, 4 edit `core/include/gg/core/ParamIds.h` lists and `Processor::updateVoices` — keep such edits small and self-contained to ease merges.

This sub-project's own:
- **Indices** (G13): layer indices **0x70 Reverse** and **0x71 Skew** in a new list `GG_LAYER_PARAMS_M3_GRAINSHAPE(X)` after `GG_LAYER_PARAMS_M2_EXTREMES(X)` (and after 3b's `GG_LAYER_PARAMS_M3_PLAYHEAD(X)` if it has landed) in both `GG_LAYER_PARAMS` and `GG_LAYER_PARAMS_SINCE_M1`; 0x72 and 0x73 stay unused; **no global parameter**; `rng::Field::Reverse = 0x70`. `kNextFreeLayerIndex` and `kNextFreeGlobalIndex` unchanged.
- **The at-rest path** (§4.10): with Reverse 0 and Skew 0, `takeNext` draws nothing new, `reversed` is false, `sampleGrain` and `liveGrain` take today's code path, `envelopeShape` returns today's shape, the kernels see positive rates, and the event log writes no `"skew"`. Every existing event log stays byte for byte.
- **The at-rest wrapper** (Task 0): every "at-rest check" below means `.superpowers/sdd/2026-11-07-m3-grainshape/check-at-rest.sh <worktree>`, which runs the 108-case check above and then the 8 voice cases; expected last two lines `baseline cases: 108, changed: 0, gone: 0` and `voice cases: 8, changed: 0`.
- Reversed kernels match their scalar references bit for bit (`std::memcmp`), in the 4-wide and 8-wide tables; a case that can't is rendered through the reference and recorded as a ruling with its `gg-bench` cost (GB5).
- 64-bit positions; the mirror and the bounds in double; `Envelope.h` arithmetic under `GG_FP_CONTRACT_OFF`.
- The frozen `reference` kernel and its copies in `tools/gg-bench/Kernels.h` are untouched.
- No patch-version bump: a state or preset without the two parameters reads 0 (G12).
- The display names "Reverse" and "Skew"; no product's names, labels or layout copied.

## Plan-level decisions (Claude's, made without the director as asked)

- **PD1. A Task 0 records the baselines** this branch needs and the existing script lacks: the 8 voice cases' WAV hashes, and the X ratio before any change (GB11).
- **PD2. `mipLevelFor` reads `|rate|` itself** (`std::fabs`; a positive rate's bits unchanged), beyond GB3's callers passing the positive rate, so no future caller can get level 0 from a sign.
- **PD3. `LiveSource::levelSlotOf` gains GB6's backward branch too**, for the same reason as `slotOf` (a level read moving backward falls back to a division).
- **PD4. `sampleSimdRun` is split into `detail::sampleRunEstimate` (the solved, widened, clamped bounds) and today's stepping**, so a test can pin that a negative rate solves its bounds instead of scanning. Forward bits and runs are unchanged.
- **PD5. The reversed kernel cases live in `tests/engine/kernel_tests.cpp`** beside the forward ones (its generators and render helpers are file-local): a `reversed(specs)` transform for sample pools and a `Direction` argument to `liveGrains`. Reversed live admission tests live in `tests/engine/live_tests.cpp` beside their forward twins for the same reason.
- **PD6. `GrainDraw` carries the skew** (spec §8's first option).
- **PD7. Mutate's default locks take both through `buildStructuralLocks`** (`core/src/Mutate.cpp:24`), per layer, beside Level and Layer On.
- **PD8. The canvas's `pitchY` (G15) rides with the parameters task** (Task 8), which already touches `ui/`.
- **PD9.** The §23 row is **#97** on this branch, renumbered at merge.

## Review Focus

1. **A window change under sounding reversed ring grains** (Loop Length automated, Freeze released, a splice selected mid-grain): the grains finish in their old span, nothing clicks over the limit, nothing non-finite. (Task 6's `a window change under sounding reversed grains doesnt click`.)
2. **Reverse at the pitch limit over a short sample** (Pitch +48, Position 1, a 0.1 s sample, 200 ms grains): every reversed grain starts at the last frame and reaches frame 0 inside its window, ending there with the edge fade; click score under 15. (Task 5's `reversed grains at plus 48 semitones over a short sample end with the edge fade`.)
3. **An old preset or state without the two parameters** loaded over an instance at Reverse 100% and Skew −100%: both read 0 after the load. (Task 8's `a preset without Reverse and Skew loads them as 0`.)
4. **The overload ladder under a Reverse 100% HQ layer**: a lowered tier keeps the negative rate and drops the level to 0; a stolen reversed grain fades in output time; the drop and steal counts equal the forward twin's. (Task 5's `the ladder treats a reversed grain as its forward twin`.)
5. **A 44.1 kHz sample in a 48 kHz instance, reversed**: the mirror spans exactly the forward read in the source's frames (`startPos + rate × D` uses the converted rate). (Task 5's `the mirror spans the forward read at a converted rate`.)

## Seams with the parallel sub-projects (plan notes)

- **3b (playhead), GB13:** in `Engine::liveGrain` the order is **forward start** (3b's `scanFold(scanPoint, …) + positionDraw` where 3b supplies it), **then the mirror** (`+ rate × dur`), **then the clamp for the grain's direction** (Task 6's reversed bounds). On a Sample the playhead's point arrives folded in `positionDraw`, so `sampleGrain`'s mirror (Task 5) needs nothing from 3b. Whichever lands second writes the merged `liveGrain` and adds `a reversed scanning ring grain stays inside the reversed bounds` (Task 6's never-reads-the-future check with a nonzero Scan Speed). 3b's H8 wording becomes "in its own direction".
- **3 (pitch-sync), G16:** at Clock Pitch, S4 keys every per-grain draw but the onset's by the last refresh grain's index; `Field::Reverse`'s draw in `takeNext` takes the same index variable as the other per-grain draws, so the merge needs no special case. A held ring anchor is the forward start; a reversed held grain starts at `anchor + rate × dur` under Task 6's bounds. Whichever lands second adds `at clock pitch every pulse between two refreshes reads the same way` (Reverse 50%).
- **2 (expression):** both extend `kVoiceTargets` (`core/src/Processor.cpp:218`) and `isVoiceTarget` (`core/include/gg/core/ModMatrix.h:195`); the merge takes the **union**, the array's size the sum, and `kVoiceKeySlots` follows from it. Its per-note modulation slots (E7) gain Reverse and Skew, whichever lands second. Keep this branch's edit to those two places a two-line addition each.
- **All:** `kLayerParamCount`, `kParamCount`, `sizeof(CloudParams)` (`core/src/Processor.cpp:179`) and every count merge by addition; the ParamIds lists never touch (0x70 is above every other list).

---

### Task 0: Worktree build and baselines

**Files:**
- Create (untracked, `.superpowers/` is ignored): `.superpowers/sdd/2026-11-07-m3-grainshape/voice-baseline.txt`, `.superpowers/sdd/2026-11-07-m3-grainshape/check-at-rest.sh`, `.superpowers/sdd/2026-11-07-m3-grainshape/x-ratio-before.txt`

- [ ] **Step 1:** If `m3-voice` has moved past `18ac47d`, `git merge m3-voice` first. Run `scripts/worktree-setup.sh`, then for `dev`, `release` and `asan`: `cmake --preset <p> -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache` and `cmake --build --preset <p>`.
- [ ] **Step 2:** Run the 108-case check script. Expected: `baseline cases: 108, changed: 0, gone: 0`. Then `grep -E ' (inst-voice-|fx-voice)' build/dev/golden/wav-sha256.txt > .superpowers/sdd/2026-11-07-m3-grainshape/voice-baseline.txt` (8 lines).
- [ ] **Step 3:** Write `check-at-rest.sh <worktree>`: run the 108-case script; then for each name in `voice-baseline.txt` compare its hash with `build/dev/golden/wav-sha256.txt` (print `CHANGED <name>`), require `git diff --quiet -- tests/golden/expected/<name>.events.jsonl`, then `git checkout --` that case's `.metrics.json` and `.events.jsonl`; print `voice cases: 8, changed: N` and exit non-zero unless N is 0. Run it. Expected: both last lines clean.
- [ ] **Step 4:** `build/release/tools/gg-bench/gg-bench --cases reference,engine-sample,engine-live --quick > .superpowers/sdd/2026-11-07-m3-grainshape/x-ratio-before.txt`. No commit.

### Task 1: The scalar paths at negative rates

**Files:**
- Modify: `engine/include/gg/grain/GrainKernels.h:626-631` (`paramsOf`), `engine/include/gg/source/Mipmap.h:66-85` (`mipLevelFor`, PD2), `engine/include/gg/grain/GrainPool.h:100-160` (`grainEdgeSample`, GB4, and its comment), `engine/src/LiveSource.cpp:689-706` and `levelSlotOf` (GB6, PD3), `engine/include/gg/source/LiveSource.h` (the `Cursor` comment: "moving forward or backward")
- Test: `tests/engine/kernel_tests.cpp`, `tests/engine/mipmap_tests.cpp`, `tests/engine/pool_tests.cpp`, `tests/engine/live_tests.cpp`

**Interfaces:**
- Produces: `paramsOf` gives `stretchOf(std::abs(rows.rate[i] * invDecimation(decimation)))`; `mipLevelFor(rate, n) == mipLevelFor(std::fabs(rate), n)` for every rate; `grainEdgeSample(g, frames)` for `g.rate < 0` returns the first output sample whose read `startPos + rate × e` is below 0, within the window, else `kNoEdge`; `slotOf` and `levelSlotOf` return `first % capacity` for any order of reads.

- [ ] **Step 1: Write the failing tests** (each tagged `[grainshape]` beside its file's own tag):
  - `kernel_tests.cpp`, `sinc reference follows the spec's arithmetic at negative rates` (`[kernel]`): the existing spec loop at rates −0.5, −1, −1.5, −4 and −8 (levels 1 to 3 at decimations 2, 4, 8), with `specStretch(std::abs(rate))`; `sameBits` against `renderSampleRef<Sinc>`. Also `REQUIRE(gg::kernels::stretchOf(std::abs(-3.0)) == gg::kernels::stretchOf(3.0))` and, through `detail::paramsOf` on a one-row pool at rate −3, `params.stretch == stretchOf(3.0)`.
  - The Hermite and Linear spec-arithmetic cases (`sample reference follows the spec's arithmetic`, `eco reference reads linearly`, `live reference follows the spec's arithmetic`) gain a `SECTION("negative rate")` at rate −1.5 with the start mirrored into the source: `sameBits` against the spec loop.
  - `mipmap_tests.cpp`, `the mip level reads the rate's magnitude` (`[engine][mip]`): for rates in {1, 1.5, 2, 3.99, 4, 8, 1e300, +inf} and level counts 1 to 6, `mipLevelFor(-r, n) == mipLevelFor(r, n)`; NaN and −0 give 0.
  - `pool_tests.cpp`, `grainEdgeSample finds frame 0 for a negative rate` (`[pool]`): 2,000 grains (rates −0.25 to −16, starts −10 to `frames + 10`, durations 16 to 9,600, onsetFrac in [0, 1)) against a brute-force scan of `t` from onset to `grainEndSample(g)` for the first `startPos + rate × e < 0`; equal, `kNoEdge` where none. For positive rates the existing case still passes unchanged; `rate == -0.0` and NaN give `kNoEdge`.
  - `live_tests.cpp`, `a backward read finds its slot without changing it` (`[live]`): on a wrapped ring, `gatherTaps<4>` and `gatherLevelTaps<4>(1, …)` at positions stepping backward by 0.7 to 3.3 frames across the storage's wrap, one shared `Cursor`: every read's taps equal a fresh-`Cursor` read's, and after each read `cursor.slot == cursor.index % capacity` (the level's capacity for the level read).
- [ ] **Step 2:** Run `ctest --preset dev -L grainshape`. Expected: the sinc, mip and edge cases FAIL; the slot case passes (GB6 changes cost, not value) and stays as the pin.
- [ ] **Step 3: Implement.** `grainEdgeSample`: GB4's estimate `n = floor(onsetFrac + (0 − startPos) / rate) + 1` and test `startPos + rate × e < 0` for `rate < 0`; today's lines verbatim for `rate > 0`. `slotOf`/`levelSlotOf`: GB6's branch (`first < cursor.index && cursor.index − first < capacity`: `slot −= difference`, `+= capacity` once if below 0).
- [ ] **Step 4:** Run `ctest --preset dev -L 'kernel|mip|pool|live|interp'`. Expected: PASS. Run the at-rest check. Expected: both lines clean.
- [ ] **Step 5:** Commit `fix(engine): the sinc's stretch, the mip level and the edge at frame 0 read the rate's sign (M3 grain shapes G6, GB4, GB6)`.

### Task 2: The sample SIMD interiors at negative rates (opus)

**Files:**
- Modify: `engine/include/gg/grain/GrainKernels.h:740-842` (`sampleSimdRun` → `sampleRunEstimate` + stepping, PD4; GB5's negative branch; the comment's "non-decreasing when rate > 0" becomes "monotone in t, non-increasing for a negative rate")
- Test: `tests/engine/kernel_tests.cpp`

**Interfaces:**
- Produces: `template <class Reader, DecimationLike Dec> Run detail::sampleRunEstimate(const GrainRows &rows, uint32_t i, int64_t frames, Dec decimation, int64_t first, int64_t last) noexcept` — today's `sampleSimdRun` up to and including the two `std::clamp`s; `sampleSimdRun` calls it, then steps. For `rateL < 0`: `nBegin = ceil(max(onsetFrac, onsetFrac + (hi − startL) / rateL)) − 2`, `nEnd = ceil(min(onsetFrac + duration, onsetFrac + (lo − startL) / rateL)) + 2`; `rateL == 0` or NaN keeps `[first, last)`.
- Test helper (file-local): `std::vector<gg::GrainSpec> reversed(std::vector<gg::GrainSpec> specs)`: every second grain's rate negated, its start moved to `startPos + rate × durationSamples` (so the pool mixes both directions and spans the same reads).

- [ ] **Step 1: Write the failing tests:**
  - `the sample run estimate solves its bounds for either sign` (`[kernel][grainshape]`, templated Hermite, Linear, Sinc): over `reversed(randomGrains(21, 500, kNoiseFrames))` at each `decimationsOf<TestType>()`, for every grain with a non-empty `sampleSimdRun` over `[0, 4096)` and 200 random calls, `estimate.begin >= run.begin − 4 && estimate.begin <= run.begin` and `estimate.end <= run.end + 4 && estimate.end >= run.end`. (Before the change a negative rate's estimate is the whole call: FAIL.)
  - Every existing sample memcmp case gains a `SECTION("reversed")` running its body over `reversed(...)` of its own pool, including `requireExactRuns` and `requireBothStretches`: `simd sample kernel matches the reference`, `… on sources of 1 to 4 frames`, `… at the source ends`, `… at extreme rates and lengths` (its rate list {1/16, 16} also as {−1/16, −16}), `… for every output shape`, `… with non-finite taps`, `a stolen grain fades through the reference and the simd kernels alike`, `a grain cut at its source's edge fades through …` (with negative rates cut at frame 0 by Task 1's `grainEdgeSample`).
  - `the avx2 kernels match the sse kernels bit for bit`'s "sample sources" section runs `requireAvx2SampleBits<R>` over a reversed pool as well (give the helper a `bool reversedPool` argument).
- [ ] **Step 2:** Run `ctest --preset dev -L kernel`. Expected: the estimate case FAILS; the reversed memcmp sections pass already (the stepping is exact), which is the bit-equality pin Step 3 must keep.
- [ ] **Step 3: Implement** PD4's split and GB5's `sampleSimdRun` branch. No change to any per-lane arithmetic.
- [ ] **Step 4:** Run `ctest --preset dev -L kernel`, and with `GG_FORCE_SSE=1` once more. Expected: PASS (the AVX2 case may SKIP on a CPU without AVX2; CI's Linux and Windows legs require it). Run `ctest --preset asan -L kernel`. Run the at-rest check. Expected: clean.
- [ ] **Step 5:** Commit `perf(kernels): the sample interiors solve their bounds at negative rates; reversed pools bit for bit in both tables (GB5)`.

### Task 3: The live SIMD interiors at negative rates (opus)

**Files:**
- Modify: `engine/include/gg/grain/GrainKernels.h:1224-1345` (`liveRun`), `:1350-1378` (`RingTaps::locate` and its comment: "monotone in lane order"), `:1387-1394` (`liveInterior`'s guard), `:1618-1720` (`liveLevelRun`), `:1730-1740` (`liveLevelInterior`'s guard), `engine/src/GrainKernelsAvx2.cpp:328-343` (`RingTaps8::locate`)
- Test: `tests/engine/kernel_tests.cpp`

**Interfaces:**
- Produces: for `rate < 0`, `liveRun` and `liveLevelRun` keep `nFirst = onsetFrac` and set `nLast = min(nLast, onsetFrac + (kB − startL) / rate)` (the head and tail bounds already go through `bound(c, k)`); the guards become `rate != 0.0 && std::abs(rate) < 0x1.0p62`; both `locate`s add `slot += slot < 0 ? capacity : 0` after today's subtraction.
- Test helper: `enum class Direction { Forward, Reversed };` and `liveGrains(seed, onsets, heads, ringFrames, Direction dir = Direction::Forward)`: Reversed negates every grain's rate and places kinds 1 to 3 against the reversed bounds (kind 1: newest frame 4 to 255 behind the head at onset; kind 2: oldest read within 255 frames of the window's oldest at its end; kind 3: rate exactly −1); kinds 0 and 4 to 7 mirror their start to `start + |rate| × dur`.

- [ ] **Step 1: Write the failing tests:**
  - `ring taps locate decreasing lanes across the wrap` (`[kernel][grainshape]`): `detail::RingTaps<Hermite>` and `<Sinc>` with capacity 1,000; lane floors `{f, f − 1, f − 2, f − 3}` and `{f, f − 2, f − 3, f − 5}` for `f − kBefore` in {0, 1, 2, 3, 500, 999}: each lane's `base == (floor − kBefore) mod 1000 + kBefore`, with the mathematical (non-negative) modulo. Increasing lanes still give today's values.
  - Every existing live memcmp case gains a `SECTION("reversed")` over `liveGrains(..., Direction::Reversed)` of its own seed, requiring `sameBits` for both outputs and `r.stats.simdSamples >= forwardSimdSamples / 2` (the forward section's count in the same test case): `simd live kernel matches the reference`, `… through freeze and wrap`, `… under a narrowed window`, `… at every ring level`, `… at every ring level under a narrowed window`, `… over a spectral ring`, `… through a spectral ring's wrap`, and the live parts of the steal and edge-fade cases.
  - `live interior is exactly where the fade is one` gains `SECTION("reversed")`: the run equals the brute-force set of samples where `Reader::fade == 1.0` and every tap is in the window, at levels 0 to 3.
  - `the avx2 kernels match the sse kernels bit for bit`'s "the live ring" section runs `requireAvx2Live<R>` over a reversed pool too (a `Direction` argument).
- [ ] **Step 2:** Run `ctest --preset dev -L kernel`. Expected: the locate case and every reversed `simdSamples` requirement FAIL (the interior is empty for a negative rate today); every `sameBits` passes.
- [ ] **Step 3: Implement** GB5's live items exactly. If a configuration can't match its reference, route it to the reference, record the ruling (GS-T3a…) in the commit message with `gg-bench` figures, and keep its `sameBits` requirement.
- [ ] **Step 4:** Run `ctest --preset dev -L 'kernel|live|spectral'`, again with `GG_FORCE_SSE=1`, and `ctest --preset asan -L kernel`. Expected: PASS. Run the at-rest check. Expected: clean.
- [ ] **Step 5:** Commit `perf(kernels): the live and level interiors and both ring tap locators at negative rates, bit for bit (GB5)`.

### Task 4: The window's Skew

**Files:**
- Modify: `engine/include/gg/grain/Envelope.h` (the header comment gains the Skew law), `engine/include/gg/grain/GrainPool.h:33-69` (`GrainSpec::skew`), `engine/src/GrainPool.cpp:239` (the latch), `engine/include/gg/grain/GrainPool.h:184` (the rows' comment)
- Test: `tests/engine/window_tests.cpp`, `tests/engine/pool_tests.cpp`

**Interfaces:**
- Produces:
  - `struct EnvelopeSides { double attack; double release; };`
  - `constexpr EnvelopeSides detail::textureSides(double texture, double durationSamples) noexcept` — today's (a, r) computation, moved verbatim;
  - `envelopeShape(texture, durationSamples)` returns `{a, 1 − r, 0.5 / a, 0.5 / r}` of `textureSides` (bits unchanged);
  - `constexpr EnvelopeShape envelopeShape(double texture, double skew, double durationSamples) noexcept`: σ = `skew` if in [−1, 1], else 0 (NaN 0); σ == 0 returns the two-argument result; else with `sMin = min(0.5, max(0.01, 16 / d))` (a NaN d gives 0.01): σ < 0, `a' = (1 − |σ|) × a + |σ| × min(a, sMin)`, `r' = (a + r) − a'`; σ > 0, `r' = (1 − σ) × r + σ × min(r, sMin)`, `a' = (a + r) − r'` (G9).
  - `GrainSpec::skew` (`double`, default 0.0), declared right after `texture`; `GrainPool::spawn` latches `envelopeShape(g.texture, g.skew, g.durationSamples)`.

- [ ] **Step 1: Write the failing tests** in `window_tests.cpp`, tagged `[engine][window][grainshape]`, over the grid τ ∈ {0, 0.25, 0.5, 0.75, 1} × d ∈ {16, 17, 100, 320, 4800, 96000}:
  - `skew 0 is the texture envelope bit for bit`: every member of `envelopeShape(τ, 0.0, d)` and of `envelopeShape(τ, NaN, d)`, `(τ, 1.5, d)`, `(τ, -2.0, d)` equals `envelopeShape(τ, d)`'s (`std::bit_cast<uint64_t>`).
  - `skew lands on its anchors`: at σ = −1, `attackEnd == min(a, sMin)`; at σ = +1, `1 − releaseStart` equals `min(r, sMin)` within 1 ulp of 1 (it is stored as `1 − r'`); for d ≤ 32 both equal σ = 0's shape.
  - `skew keeps attack plus release`: `|(a' + r') − (a + r)| <= 1 ulp of (a + r)` at σ ∈ {−1, −0.5, −0.01, 0.01, 0.5, 1}.
  - `skew moves the peak monotonically and never the wrong way`: `attackEnd` non-increasing in σ over 41 steps from −1 to 1; for each side already below `sMin` (d = 100, τ = 1), σ never lengthens it.
  - `skew keeps the window's mean and energy`: the numeric integral and energy of `hann(warpPhase(φ, shape))` over 10⁵ midpoints within 1e-6 relative of σ = 0's, at σ ∈ {−1, −0.5, 0.5, 1}, every grid point (G10).
  - `pool_tests.cpp`, `the pool latches the skewed shape` (`[pool][grainshape]`): a grain spawned with `skew = -1` has `rows.attackEnd[i] == envelopeShape(tex, -1, d).attackEnd` (and the other three rows); with `skew = 0` the rows equal today's two-argument shape.
- [ ] **Step 2:** Run `ctest --preset dev -L grainshape`. Expected: FAIL (compile errors first).
- [ ] **Step 3: Implement.** Keep the arithmetic under `GG_FP_CONTRACT_OFF`. Add `.skew = …` nowhere yet (Task 5 copies it from the draw); every existing designated initializer of `GrainSpec` still compiles (the member has a default).
- [ ] **Step 4:** Run `ctest --preset dev -L 'window|pool|kernel|engine'`. Expected: PASS. At-rest check: clean.
- [ ] **Step 5:** Commit `feat(engine): Skew trades a grain's attack for its release at the same mean and energy (G9, G10, GB15)`.

### Task 5: The reverse draw and the Sample mirror (opus)

**Files:**
- Modify: `engine/include/gg/core/Rng.h:29-44` (`Field::Reverse = 0x70`, comment "whether a grain reads backward (M3 grain-shapes spec G1)"), `engine/include/gg/grain/CloudScheduler.h:26-119` (`CloudParams::reverse`, `CloudParams::skew`, after `busSplit`; `GrainDraw::skew` after `texture`, `GrainDraw::reversed` after `bus`), `:314-430` (`takeNext`), `engine/src/CloudScheduler.cpp:27` (`setParams`), `engine/src/Engine.cpp:748-778` (`sampleGrain`), `:780-831` (`liveGrain` copies `.skew` only; Task 6 does the rest), `core/src/Processor.cpp:178-198` (`sameCloud` and its `static_assert` to the new size, 136 unless padding moves)
- Create: `tests/engine/reverse_tests.cpp` (`[engine][grainshape]`; add to `tests/CMakeLists.txt`)
- Test: `tests/engine/cloud_tests.cpp`, `tests/engine/determinism_tests.cpp`

**Interfaces:**
- Produces: `CloudParams { …; double reverse{0.0}; double skew{0.0}; }` (sanitized: non-finite → 0, then clamped to [0, 1] and [−1, 1]); `GrainDraw::reversed` (`bool`, default false) and `GrainDraw::skew` (`double`, default 0.0); in `takeNext`, beside the bus draw, `reversed = params_.reverse > 0.0 && rng::uniform01(key(rng::Field::Reverse)) < params_.reverse` and `skew = params_.skew`; `sampleGrain`: with today's `start` and positive `rate`, `level` from the positive rate, then if `d.reversed`: `startPos = std::min(start + rate × d.durationSamples, span)`, `rate = −rate`; `.skew = d.skew` in both sources.

- [ ] **Step 1: Write the failing tests.** In `cloud_tests.cpp` (`[cloud][grainshape]`):
  - `reverse draws no other field`: two schedulers, `reverse` 0 and 0.7, same seed: over 2,000 draws every field but `reversed` equal (`GrainDraw` compared with `reversed` copied across), and at 0 every `reversed` false.
  - `reverse is a probability`: 10,000 draws at 0.5 give a reversed count in [4850, 5150]; at 1.0 all; at 1e-9 none of the first 10,000 below its draw; the same draws at block sizes 1, 7, 64 and 333.
  - `setParams sanitizes reverse and skew`: NaN and ±inf give 0; −0.5 → 0, 1.5 → 1 for reverse; −3 → −1, 3 → 1 for skew; the draw carries `params().skew`.

  In `reverse_tests.cpp`:
  - `a reversed sample grain mirrors its forward twin`: an Engine over a 4 s noise sample, Reverse 1 against Reverse 0, same seed: grain k's `startPos + rate × durationSamples` equals the forward grain's `startPos` (exact, or the forward `startPos` when the mirror clamped to `span`), its `rate` the forward's negated, its `level` the forward's.
  - `a reversed grain plays its forward twin backward`: Eco tier, Texture 0, Skew 0, rate 1, `onsetFrac` 0, an integer start and duration (sample-mode stream with Density chosen so onsets are integers): each reversed grain's output, isolated by rendering one grain, equals the forward grain's reversed in time within 1e-6.
  - `the mirror spans the forward read at a converted rate`: a 44.1 kHz sample in a 48 kHz engine, Pitch +7: the reversed start minus `|rate| × D` equals the forward start within 1e-9 frames.
  - `reversed grains at plus 48 semitones over a short sample end with the edge fade`: a 0.1 s sample at Position 1, Pitch +48, 200 ms grains, Reverse 1: every grain starts at `frames − 1` and `grainEdgeSample` is finite for each; the render's click score (`gg::metrics`, metricsVersion 2) under 15; every sample finite.
  - `the ladder treats a reversed grain as its forward twin`: an HQ layer at Budget 256 with grains asking for four times that, Reverse 1 against 0: `droppedGrains()`, the steal count and every grain's tier and weight equal; a lowered grain has `rate < 0` and `level == 0`.
  - `a reversed grain over a one-frame sample is cut at once` (GB17): a one-frame sample, Reverse 1: every grain's `startPos == 0`, its edge sample within two samples of its onset, the output finite and every grain silent past its edge.
  - `skew reaches the grain`: Skew −0.5 in `CloudParams` gives every event's `GrainSpec::skew == -0.5`.

  In `determinism_tests.cpp`: `reverse and skew are block-size invariant` (`[determinism][grainshape]`): a Sample engine at Reverse 0.5 with Skew moved every 32 samples through −1, −0.3, 0.6, 1 (via `setCloudParams` at the same sample in each run), blocks 1, 7, 32, 64, 256, 333: output bytes and grain events equal.
- [ ] **Step 2:** Run `ctest --preset dev -L 'grainshape|cloud|determinism'`. Expected: FAIL.
- [ ] **Step 3: Implement.** In `sampleGrain` compute the forward values exactly as today first, so a forward grain's code path and bits are today's. `sameCloud` gains `same(a.reverse, b.reverse) && same(a.skew, b.skew)`.
- [ ] **Step 4:** Run `ctest --preset dev -L 'engine|cloud|determinism|kernel|core'`. Expected: PASS. At-rest check: clean.
- [ ] **Step 5:** Commit `feat(engine): the reverse draw (Field::Reverse 0x70) and the Sample mirror (G1, G2, G5, GB1-GB3)`.

### Task 6: Reversed grains on the rings (opus)

**Files:**
- Modify: `engine/src/Engine.cpp:780-831` (`liveGrain`: GB3's reversed bounds and start; the class comment's derivation gains GB3's last paragraph), `engine/include/gg/engine/Engine.h:213` (the doc)
- Test: `tests/engine/live_tests.cpp`, `tests/engine/reverse_tests.cpp`, `tests/engine/spectral_tests.cpp`

**Interfaces:**
- Consumes: Task 5's `GrainDraw::reversed`.
- Produces: for a reversed draw, `hi = ρ − 3 − M`, `lo = ρ − W(t0) + 1 + M + span + rate × dur` (or with a bound splice start `windowOldestAt(t0) + stepOldest + 1 + M + spanOldest + rate × dur`), `startPos = clamp(ρ − positionDraw × sampleRate + rate × dur, lo, hi)`, `lo > hi` drops (counted), stored rate `−rate`, level `mipLevelFor(rate, …)` of the positive rate. The forward path's lines unchanged.

- [ ] **Step 1: Write the failing tests** (each `[grainshape]` beside its file's tags):
  - `live_tests.cpp`: `reversed live grains never read the future` — the forward case's analytic check with Reverse 1 (and a second run at 0.5): for each event, the reach at e = 0 (newest frame) and at e = `durationSamples` (oldest) within `[R − N + M, R − 1 − M]` at that output time, through a freeze; `reversed live grains never read overwritten data` — the forward case's two-input construction at Reverse 1; `a window change under sounding reversed grains doesnt click` — the forward case's schedules (Loop Length, Freeze released, a splice selected) at Reverse 1 and 0.5, at every tier and levels 0 to 2, three seeds: each change's click score and the render's under 15, output and events equal at blocks 1, 64 and 511.
  - `reverse_tests.cpp`: `reversed ring reads stay where the fade is one` — Live mode, a moving ring, a frozen one, Loop on, a selected splice (GB16); Eco, Normal and HQ; levels 0 to 3 (Pitch 0, +12, +24, +36): driving the engine in blocks of 64 and, after each `process()`, for every collected grain sounding in that call's samples, `Reader::fade(engine.live(), pos(t), headAt(t)) == 1.0` for the grain's tier's reader and every tap in the window; `a reversed grain at position 0 reads the newest frames backward` — Position 0, Spray 0: `startPos == hi` and every grain's read ends `rate × dur` behind it; `a span that fits neither way drops` — a 1 s ring, 2 s grains at rate 1: `droppedGrains()` counts each draw, forward and reversed alike.
  - `spectral_tests.cpp`: `spectral grains read the smear's ring backward` (`[spectral][grainshape]`): Spectral over noise at Reverse 1: every event's rate negative and its reads inside the smear ring's window (the check above against `engine.spectral()->ring()`); the smear's own output (Reverse 0 vs 1, no grain reading) bit-identical.
  - `reverse_tests.cpp`: `reversed rings are block-size invariant` (`[determinism]` too): Live moving, Live frozen and Spectral, Reverse 0.5, Skew −1, blocks 1, 7, 32, 64, 256, 333: output and events equal.
  - `reverse_tests.cpp`: `a source change under a sounding reversed grain lets it finish` (GB17): Live → Sample at a fixed sample with reversed grains sounding: the output up to the last such grain's end equals a render that never switched, for those grains' contribution (render the switch with the Sample silent).
- [ ] **Step 2:** Run `ctest --preset dev -L grainshape`. Expected: the new ring cases FAIL (until this task `liveGrain` ignores `reversed`, so ring grains still read forward).
- [ ] **Step 3: Implement** GB3's ring path as one branch on `d.reversed`, computing `step`, `span`, `stepOldest` and `spanOldest` as today. Leave a one-line comment at the start computation naming GB13's order (forward start, mirror, clamp) for sub-project 3b's merge.
- [ ] **Step 4:** Run `ctest --preset dev -L 'engine|live|spectral|determinism|kernel'` and `ctest --preset asan -L grainshape`. Expected: PASS. At-rest check: clean.
- [ ] **Step 5:** Commit `feat(engine): reversed grains on Live, Resample and Spectral rings never cross the head (G4, G8, GB3, GB16)`.

### Task 7: The reversed benchmark cases

**Files:**
- Modify: `tools/gg-bench/Bench.cpp:395-500` (`kEngineCases` gains `engine-sample-reverse` after `engine-sample-sse` and `engine-live-reverse` after it; the file's case-order comment), `tools/gg-bench/Bench.h` (`DirectionCounts`, `admittedDirections`)
- Test: `tests/tools/bench_tests.cpp`

**Interfaces:**
- Produces: `struct DirectionCounts { uint64_t forward; uint64_t reversed; }; DirectionCounts admittedDirections(const std::string &engineCase, uint32_t grains, uint32_t blockSize);` (throws `std::invalid_argument` for `reference` or an unknown name, as `admittedTiers` does). `engine-sample-reverse` is `engineSample` with `params.reverse = 1.0`; `engine-live-reverse` is `engine-live`'s setup with the same. Report-only; no baseline entry.

- [ ] **Step 1:** Test `the reverse cases admit only reversed grains` (`[bench]`): for both names at blocks 32, 64 and 256, `admittedDirections(name, 50, b)` has `forward == 0`, `reversed > 0`; for `engine-sample`, `reversed == 0`.
- [ ] **Step 2:** Run `ctest --preset dev -L bench`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run `ctest --preset dev -L bench`. Expected: PASS. Then on `release`: `gg-bench --cases reference,engine-sample,engine-sample-reverse,engine-live,engine-live-reverse --quick`. Expected (GB11): each reverse ratio within 10% of its twin's, and `engine-sample`'s ratio within its noise of `x-ratio-before.txt`. A larger gap is recorded as a ruling with its reason in the commit and in Task 11's records.
- [ ] **Step 5:** Commit `bench: engine-sample-reverse and engine-live-reverse (report-only)`, quoting the four ratios and the before/after X ratio.

### Task 8: The parameters, their placement and the canvas

**Files:**
- Modify: `core/include/gg/core/ParamIds.h:112-141` (the list `GG_LAYER_PARAMS_M3_GRAINSHAPE(X) X(Reverse, 0x70, "Reverse") X(Skew, 0x71, "Skew")` in both macros; the extremes comment's "0x68 to 0x7F stay unused" becomes "0x68 to 0x6F"), `:157` (`kLayerParamCount` 65), `:521-525` (`isReservedIndexInUse` gains `scope >= 1 && index >= 0x70u && index <= 0x71u`), the count comments; `core/src/ParamIds.cpp:200` (Reverse: `fresh().asPercent().withDefault(0.0f)`; Skew: `fresh().asPercentBipolar().withDefault(0.0f)`, both with Texture's flags), `core/src/ParamMap.cpp:136` (`p.reverse`, `p.skew` beside `p.texture`), `core/src/Mutate.cpp:46-50` (PD7), `ui/src/EditLayout.cpp:70-75` (§4.14), `ui/src/LaneModel.cpp:98-115` (`pitchY`, G15; comment amended), `plugin/src/GigaGrainsPlugin.cpp` (only if the flags or text paths need it)
- Test: `tests/support/ParamTable.h`, `tests/core/patch_tests.cpp`, `tests/plugin/identity_tests.cpp`, `tests/plugin/params_tests.cpp`, `tests/plugin/rt_tests.cpp`, `tests/ui/edit_layout_tests.cpp`, `tests/ui/lane_model_tests.cpp`, `tests/core/preset_tests.cpp`, `tests/core/mutate_tests.cpp`, `tests/sweep/` (floors)
- Create: `tests/core/grainshape_tests.cpp` (`[core][grainshape]`; add to `tests/CMakeLists.txt`)

**Interfaces:**
- Produces: `LayerParam::Reverse`, `LayerParam::Skew`; `ParamSlot::Reverse`, `B_Reverse` … `D_Skew`; CLAP IDs `0x010070` … `0x040070`, `0x010071` … `0x040071`; `kParamCount` 298. Edit view: `edit(Texture, Grain, 4, Skew)`, `edit(Skew, Grain, 5)`, Smear 6, Quality 7; `edit(Reverse, Pitch, 2)`. `gg-render` names `"A Reverse"` … `"D Skew"` follow from the macros.

- [ ] **Step 1: Write the failing tests:**
  - Update `ParamTable.h` and every count to 298 slots, 65 a layer, with the eight new rows (ranges 0..1 and −1..1, defaults 0, automatable and modulatable).
  - `grainshape_tests.cpp`: `reverse and skew are layer parameters` (IDs, ranges, defaults, flags for all four layers); `toCloudParams maps reverse and skew` (values 0.3 and −0.7 in layer C reach its `CloudParams` exactly as `double(float)`); `a preset without Reverse and Skew loads them as 0` (Review Focus 3: an instance at A Reverse 1, A Skew −1 loads a packed fixture preset (`tests/fixtures/presets/fx-test.json`) and INIT; both read 0; a `PatchState` written before this change, a byte fixture made in the test from today's encoder with the two values removed, reads both as 0); `mutate's default press leaves reverse and skew` (`defaultMutateLocks` holds all eight slots; a press at amount 1 on a preset with Reverse 0.4 leaves it 0.4; an explicit unlocked press moves it within [0, 1] and Skew within [−1, 1]).
  - `params_tests.cpp`: `skew and reverse display as percentages` — Skew's `value_to_text(−1)` equals `"-"` + Texture's text at 1, Skew's text at 0 equals Texture's at 0, Reverse's equals Texture's at the same value; `text_to_value` of `"-50"`, `"-50%"`, `"50 %"` gives −0.5, −0.5, 0.5 for Skew.
  - `edit_layout_tests.cpp`: the existing "every parameter has exactly one placement" passes with the new slots; add `skew sits beside texture and reverse after pitch spread`.
  - `lane_model_tests.cpp`, `pitch maps to the lane's vertical axis`: the loop's `-1.0f, -2.0f` move out of the "draws centered" list into `CHECK(pitchY(-r, g, r0) == pitchY(r, g, r0))` for r in {0.5, 1, 2, 16}; 0, −0, NaN, ±inf stay centered.
- [ ] **Step 2:** Run `ctest --preset dev -L 'core|plugin|ui' -LE slow`. Expected: the updated cases FAIL.
- [ ] **Step 3: Implement.** Confirm while doing it that `PatchState` reads absent values as defaults with no version bump, and say so in the commit.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`, the editor frame tests included ("every parameter is reachable from the UI", the elision checks at 960 × 600 and 640 × 360; use short labels "Rev" and "Skew" only if the elision check needs them). Run `ctest --preset dev -L sweep` and adjust a floor only with its reason. At-rest check: clean (the packed presets' bytes change; their goldens' WAVs and events don't).
- [ ] **Step 5:** Commit `feat(params): Reverse and Skew per layer (0x70, 0x71); the Edit view; reversed grains drawn at their pitch (G12, G13, G15, GB14)`.

### Task 9: Route and per-voice targets

**Files:**
- Modify: `core/src/Processor.cpp:218-226` (`kVoiceTargets` 12 → 14: `LayerParam::Reverse`, `LayerParam::Skew` appended), `core/include/gg/core/ModMatrix.h:206-220` (`isVoiceTarget`'s two cases)
- Test: `tests/core/grainshape_tests.cpp`, `tests/plugin/rt_tests.cpp`, `tests/core/mod_matrix_tests.cpp`

**Interfaces:**
- Consumes: Task 8's slots; sub-project 1's `applyVoiceRoutes`, `actsPerVoice`.

- [ ] **Step 1: Write the failing tests:**
  - `grainshape_tests.cpp`: `velocity to reverse and skew acts per voice` — layer A at Mode Voice, routes Velocity → A Reverse (Unipolar, depth 1) and Velocity → A Skew (Unipolar, depth −1), notes at velocities 20 and 127 held together: the 127 voice's events all `rate < 0` with `skew == −1` (from the engine's `GrainSpec`), the 20 voice's share of negative rates within 3σ of 20/127 over its grains and its `skew` ≈ −20/127 (as the route's float math gives); `a cloud layer's reverse route applies layer-wide` — the same routes at Mode Cloud move every grain alike; `an lfo on skew is block-size invariant in both products` (`[determinism]`): LFO 1 → A Skew at 3 Hz, A Reverse 0.5, blocks 1, 7, 32, 64, 256, 333, output bytes equal, instrument and FX.
  - `mod_matrix_tests.cpp`: `isVoiceTarget includes reverse and skew` (layer A's slots true; `isValidTarget` true for all four layers).
  - `rt_tests.cpp`: `plugin process with reversed skewed voices does not allocate` (`[plugin][rt][grainshape]`): the instrument, 16 voices on a Live (via Resample) ring and on a sample, Reverse 0.5, Skew −1, Velocity → Reverse routed; the tripwire on `process` and `params.flush`; the same for the FX on a Live layer.
- [ ] **Step 2:** Run `ctest --preset dev -L 'grainshape|mod|rt'`. Expected: FAIL.
- [ ] **Step 3:** Implement (two two-line edits; nothing else in `updateVoices` changes).
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`; `ctest --preset asan -L 'grainshape|rt'`. Expected: PASS. At-rest check: clean.
- [ ] **Step 5:** Commit `feat(mod): Reverse and Skew are route and per-voice targets (G12, GB7)`.

### Task 10: gg-render's keys and the event log

**Files:**
- Modify: `tools/gg-render/RenderSpec.h:194-209` (`StreamSpec::reverse`, `StreamSpec::skew`, documented), `tools/gg-render/RenderSpec.cpp` (sample-mode parse: optional, range-checked, exit 2 out of range), `tools/gg-render/Render.cpp` (sample mode's `toCloudParams(StreamSpec)`; `formatEventLine` at `:1202-1220` writes `"skew":<σ>` after `"tex"` when `g.skew != 0.0`, on Cloud and voice lines; its comment documents the sign and the field)
- Test: `tests/tools/render_spec_tests.cpp`, `tests/tools/preset_source_tests.cpp`, `tests/CMakeLists.txt` (refusal `add_test`s beside the existing `render_*` ones)

**Interfaces:**
- Produces: `StreamSpec { …; double reverse{0.0}; double skew{0.0}; }`; `formatEventLine` output for a reversed grain is today's format with a negative `"rate"`.

- [ ] **Step 1: Write the failing tests:**
  - `render spec reads the grain shape names`: an FX and an instrument spec with `"A Reverse": 0.5`, `"D Skew": -1` in `params`, in `automation` and as a route target; bare `"Reverse"` and `"Skew"` are unknown keys (the parser's message).
  - `render spec reads the sample stream's reverse and skew`: present and absent (absent equals today's `StreamSpec`); `render_stream_reverse_range` and `render_stream_skew_range` (`add_test`s): `"reverse": 1.5` and `"skew": -2` exit 2.
  - `the event line writes skew only when it is set`: a `GrainSpec` with skew 0 formats byte-identically to today's string (pinned literal); with skew −0.5, `"tex":0,"skew":-0.5,"tier":` appears; a voice grain likewise after its `"tex"`.
  - `preset_source_tests.cpp`: `a preset source takes reverse and skew` (packs, and the values read back).
- [ ] **Step 2:** Run `ctest --preset dev -L render`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run `ctest --preset dev -L 'render|golden'`. Expected: PASS. At-rest check: clean (every existing event log byte for byte).
- [ ] **Step 5:** Commit `feat(render): Reverse and Skew in specs and the sample stream; "skew" in the event log (G14)`.

### Task 11: The nine goldens

**Files:**
- Create: `tests/golden/cases/{sweep-stretch-reverse,inst-reverse,inst-reverse-edge,inst-reverse-hq,fx-reverse-ring,inst-skew,fx-skew-percussive,inst-voice-grainshape,fx-spectral-reverse}.json` (spec §4.12; seed 1, 4 to 6 s) and their expected files via `scripts/update-goldens.sh build/dev`
- Modify: `tests/metrics/click_injection_tests.cpp` (an entry, exact count and reason, for each case that hides steps), `.superpowers/sdd/2026-11-07-m3-grainshape/check-at-rest.sh` (its voice glob must not pick up `inst-voice-grainshape`: it reads names from `voice-baseline.txt` only)

- [ ] **Step 1:** Write the nine specs as §4.12 describes them (the builder picks exact notes, Position and inputs within it, §8).
- [ ] **Step 2:** Run `scripts/update-goldens.sh build/dev`, then the at-rest check. Expected: the nine are new; both at-rest lines clean.
- [ ] **Step 3:** Check each new event log: `sweep-stretch-reverse` and `inst-reverse-edge` every `"rate"` negative; `inst-reverse` none before 2 s, about half from 2 s, all from 4 s; `inst-reverse-hq` `"lvl"` 0, 1 and 2 appear; `inst-skew` `"skew"` on the lines after 0 s and absent from 2 s to 3 s (Skew 0); `inst-voice-grainshape` each voice's negative share and `"skew"` follow its velocity. Look at every new spectrogram (`build/dev/golden/<case>.png`).
- [ ] **Step 4:** Run `ctest --preset dev -L golden`, the click injection included; add each table entry with its reason in this commit.
- [ ] **Step 5:** Commit `test(golden): nine grain-shape cases; every earlier WAV hash unchanged`, listing the nine hashes and confirming the 108 + 8 unchanged. `git diff --cached --stat -- tests/golden` lists only the nine cases' files.

### Task 12: The metrics and sweep checks

**Files:**
- Create: `tests/metrics/grainshape_click_tests.cpp` (`[golden][metrics][grainshape]`; add to `tests/CMakeLists.txt`)
- Modify: `tests/sweep/sweep_params_tests.cpp`, `tests/sweep/sweep_extremes_tests.cpp`, `tests/sweep/SweepDriver.cpp` (the new parameters in the sweeps and corners; floors with reasons)

- [ ] **Step 1: Write the tests:**
  - `skewed and reversed grains score under the click limit`: in-process renders (the click-injection tests' renderer), Skew ∈ {−1, −0.5, 0.5, 1} × Size ∈ {5 ms, 20 ms, 100 ms, 1 s} × Texture ∈ {0, 0.5, 1} over 220 Hz and 2 kHz sines at Density 8 Hz, and Reverse 1 at the same corners with Skew 0: every click score below 15 (metricsVersion 2).
  - `inst-skew keeps its level`: `inst-skew`'s RMS over each 1 s step within 0.5 dB of the Skew 0 step's (G10).
  - `inst-reverse-hq keeps its top octave`: the energy above 16 kHz of `inst-reverse-hq` within 1 dB of the same spec at Reverse 0 (the stretch from |rate|; a sign bug aliases).
  - Sweeps: A Reverse and A Skew join `sweep: layer A grain params, fx` and `…, instrument`; the extremes' corners add Reverse 1 with Skew ±1 at Size 5 ms and 2 s on every source kind; finiteness, the peak bound and the click score as for the others.
- [ ] **Step 2:** Run `ctest --preset dev -L 'grainshape|sweep'`. Expected: PASS, or a real fault (fix it in the owning task's code, with a test there).
- [ ] **Step 3:** Run `ctest --preset asan -L sweep -LE slow` (about 200 s).
- [ ] **Step 4:** Commit `test(metrics): grain-shape click, level and alias checks; Reverse and Skew in the sweeps`.

### Task 13: Records

**Files:**
- Modify: `INTENT.md` (§14 Texture's paragraph gains the Skew law, its 1% floor and G10's gain argument; a Reverse paragraph: the draw, the mirror, the ring's reversed clamp, the edge at frame 0, the kernels' |rate|, the window in output time, Spectral, the held draw at Clock Pitch; the `rng::Field` list gains `Reverse = 0x70`; the layer-parameter count; §17 the Edit placement and G15 (VW-T2c amended); §18 the two parameters, no patch-version bump, Mutate's locks; §20 M3's row names sub-project 8; §22 a slope curve noted as later; §23 **#97** with G0 the director's and G1 to G16, GB1 to GB17 and PD1 to PD9 marked provisional, plus Task 3's and Task 7's rulings if any), `docs/superpowers/specs/2026-11-07-m3-program-design.md` (§8 row 8, the spec's §6 text), the views spec's VW-T2c entry (amended by G15), `CLAUDE.md` (the counts: 65 a layer, 298 slots; the `grainshape` label; `"A Reverse"` … `"D Skew"` and the sample stream's `"reverse"`, `"skew"`; the event log's sign and `"skew"`; the nine goldens, 77 cases and 125 with the presets; the two bench cases), `docs/m3-carry-over.md` (rows for anything deferred: a slope curve, remote-controls pages, factory presets, sub-project 2's per-note slots)

- [ ] **Step 1:** Write them.
- [ ] **Step 2:** Commit `docs(m3): sub-project 8's records (grain shapes; §23 #97)`.

### Task 14: The gate

- [ ] **Step 1:** If `m3-voice` moved, merge it and re-run Task 11's Step 2.
- [ ] **Step 2:** `scripts/check.sh --presets dev,asan,rtsan` (one gate on the machine at a time). Expected: `PASS`.
- [ ] **Step 3:** The at-rest check. Expected: both lines clean. `ctest --preset dev -L golden`: PASS.
- [ ] **Step 4:** The validators on `release`'s `gg-products` (`scripts/fetch-validators.sh tools-bin`; clap-validator, pluginval at strictness 10 under `xvfb-run -a --skip-gui-tests`, Steinberg's validator). Expected: PASS.
- [ ] **Step 5:** The X ratio: `gg-bench --cases reference,engine-sample --quick` against `x-ratio-before.txt`, within its noise (GB11).
- [ ] **Step 6:** Render the A/B listening files into `docs/listening/m3-grainshape/` (the nine cases' WAVs and the Reverse 0 and Skew 0 twins of `inst-reverse`, `fx-reverse-ring` and `fx-skew-percussive`; a README with what to listen for); commit `docs(listening): grain shapes A/B`.
- [ ] **Step 7:** Push and open a draft PR into `m3-voice`'s line (base as the program manager directs). Fix CI's GCC 13 diagnostics if any.
