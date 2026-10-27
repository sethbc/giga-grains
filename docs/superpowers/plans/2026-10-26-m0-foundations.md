# M0 Foundations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the giga-grains repository. That means:
- a headless, deterministic C++20 grain engine that renders one grain stream reproducibly;
- the `gg-render` CLI, with metric-based golden tests;
- real-time-safety and determinism harnesses;
- a kernel benchmark;
- both plugin shells (*Giga Grains*, *Giga Grains FX*) built as CLAP/VST3/AUv2/standalone with a minimal JUCE editor;
- CI on macOS, Windows and Linux.

This meets the M0 exit criteria in INTENT §20.

**Architecture:**
- `engine/` is a framework-free static library. Tools and tests use it directly.
- `plugin/` is the CLAP implementation library. One factory, two descriptors.
- `ui/` is the JUCE editor, attached through sst-clap-helpers' `clap_juce_shim`.
- `clients/clap-first/` turns the implementation library into every format via clap-wrapper's `make_clapfirst_plugins`.

This follows the Shortcircuit XT template at `~/src/shortcircuit-xt`.

**Tech Stack:**
- C++20, CMake ≥ 3.22 with presets, Ninja / VS2022 ClangCL
- free-audio/clap 1.2.10, clap-helpers, clap-wrapper (`next` @ `4e7afc3`)
- JUCE 9.0.4 @ `3fb5cbc` (GUI only; was 8.0.16-security until ruling T11), sst-clap-helpers, sst-cpputils, sst-basic-blocks, simde
- Catch2 v3, fmt, dr_libs, pffft, stb_image_write, CLI11, nlohmann/json

**Spec:** `INTENT.md` (repo root). Read §8 (principles), §14–§20 and §22 before starting any task.

## Global Constraints

These apply to every task.

**Language, build and platforms**
- C++20 (`CMAKE_CXX_STANDARD 20`, extensions OFF). CMake ≥ 3.22. CMakePresets.json schema version 3.
- macOS minimum 11.0. Universal macOS builds use `CMAKE_OSX_ARCHITECTURES=arm64;x86_64`.
- Windows builds with VS2022 and `-T ClangCL`.
- Linux baseline is ubuntu-22.04 with gcc-12.

**Pinned dependencies**
- Every dependency is a git submodule under `libs/`, pinned to an exact SHA. Record the tag in the commit message.
- clap: tag `1.2.10`.
- clap-wrapper: `next` @ `4e7afc3` (`v0.17.0-15-g4e7afc3`: v0.17.0 plus 15 commits on `next`, including the AUv2 loop/beat-0 transport fixes; there is no v0.17.1 tag). Never older than v0.17.0.
- JUCE: tag `9.0.4` @ `3fb5cbc` (ruling T11: the Task 11 spike passed, so the pin moved from `8.0.16-security`).
- Always pass `PLUGIN_FORMATS CLAP VST3 AUV2 STANDALONE` explicitly. Omitting it defaults to including AAX.
- The argument is spelled `BUNDLE_IDENTIFIER`. SCXT misspells it as `BUNDLE_IDENTIFER`; don't copy that.

**License**
- AGPL-3.0-or-later. Every first-party source file starts with:
  ```
  // Giga Grains — granular instrument and effect
  // SPDX-License-Identifier: AGPL-3.0-or-later
  // Copyright 2026, the Giga Grains authors (see git history)
  ```
- Dependencies must be AGPLv3-compatible (INTENT §12). No GPLv2-only code, no unlicensed code.
- Never use the names "Mutable Instruments", "Clouds" or "Beads" in code identifiers or product text.

**Frozen identities** (INTENT §18; never change after the first public release)

| Field | Value |
|---|---|
| vendor | `sethbc` |
| URL | `https://github.com/sethbc/giga-grains` |
| instrument CLAP ID | `org.sethbc.giga-grains` (name "Giga Grains") |
| FX CLAP ID | `org.sethbc.giga-grains-fx` (name "Giga Grains FX") |
| AU manufacturer | `Sbcx`, display name `sethbc` |
| instrument AU | `aumu`/`GGin`/`Sbcx` |
| FX AU, primary | `aufx`/`GGfx`/`Sbcx` |
| FX AU, extra identity | `aumf`/`GGfx`/`Sbcx` (via clap-wrapper's legacy-identity factory) |

**Determinism rules** (INTENT §14)
- Randomness comes only from `gg::rng`.
- No `std::` random distributions or engines.
- No libm calls in values that reach the grain-event log. Use `gg::exp2Det`.
- Positions, phases and onsets are 64-bit: `int64_t` plus a fractional part, or `double`.
- Grains are summed in spawn order.
- Compile with `-ffp-contract=off` (gcc/clang) or `/clang:-ffp-contract=off` (ClangCL). Never fast-math.

**Audio thread rules**
- `gg::Engine::process` and plugin `process` never allocate, lock, do I/O or block.
- They are annotated `SST_CPPUTILS_NONBLOCKING`.
- They must handle 0-frame and 1-frame calls, and calls with `numFrames > maxBlockSize`.

**Decision log**
- Any change to INTENT Part II adds a row to INTENT §23, dated with the commit date, in the same commit.

**Director checkpoints**
- Steps marked **[Director]** need the human director: listening, DAW testing, or an approval.
- The executing *session* handles them, not a subagent. A subagent reaching one stops and reports.

## Review Focus

These are the five failure modes most likely to bite users that the spec implies but no other test covers. Each has a test in the task listed.

1. **Host sample rates other than 48 kHz, and sources at a different rate than the host.** Grain timing in seconds and the pitch ratio must be correct at 44.1 / 96 / 192 kHz. *(Task 3: `"stream timing scales with sample rate"`; Task 6: golden `sparse-sine-44k1`.)*
2. **Extreme or garbage parameter values** (NaN, ±inf, rate 0, rate > 4 kHz, position outside [0,1], 1-sample grains) must produce finite, bounded audio. *(Task 3: `"extreme and non-finite params stay finite"`.)*
3. **`process` called with more frames than `maxBlockSize`, or with 0 frames.** There must be no overrun, and the result must be identical to normal-size blocks. *(Task 3: `"oversized and empty blocks"`.)*
4. **Degenerate sources:** a source shorter than one grain, with 1 frame, mono vs stereo, or no source at all. There must be no out-of-bounds reads (ASan job) and output must be silent or valid. *(Task 3: `"degenerate sources"`.)*
5. **Plugin lifecycle churn:** activate → process → deactivate → re-activate at a different sample rate. It must neither crash nor carry stale state. *(Task 8: `"reactivate at new sample rate"`.)*

---

### Task 1: Repository skeleton and build system

**Files:**
- Create: `CMakeLists.txt`, `CMakePresets.json`, `cmake/compiler-options.cmake`, `libs/CMakeLists.txt`, `.gitmodules`, `.gitignore`, `.clang-format` (copy from `~/src/shortcircuit-xt/.clang-format`), `LICENSE` (verbatim text of https://www.gnu.org/licenses/agpl-3.0.txt), `README.md`
- Create: `engine/CMakeLists.txt`, `engine/include/gg/core/Version.h`, `engine/src/Version.cpp`
- Create: `tests/CMakeLists.txt`, `tests/smoke_tests.cpp`
- Submodules: `libs/catch2` (catchorg/Catch2, latest v3.x tag), `libs/fmt` (tag `12.1.0`), `libs/simde` (tag `v0.8.2`), `libs/sst/sst-cpputils`, `libs/sst/sst-basic-blocks` (use the SHAs SCXT pins: `67601c8` and `9cee763`)

**Interfaces:**
- Produces:
  - CMake targets: `gg-compiler-options` (INTERFACE), `gg-engine` (STATIC, alias `gg::engine`), `gg-tests` (exe, Catch2WithMain, registered via `catch_discover_tests(gg-tests ADD_TAGS_AS_LABELS)` so Catch2 tags become CTest labels for `ctest -L`), `gg-all` (custom target that depends on everything built).
  - Options: `GG_BUILD_PLUGINS` (ON), `GG_SANITIZE` (OFF), `GG_USE_RTSAN` (OFF), `GG_COPY_PLUGIN_AFTER_BUILD` (OFF), `GG_JUCE_PATH` (default `${CMAKE_SOURCE_DIR}/libs/JUCE`).
  - Presets `dev` (Ninja, Debug, `build/dev`) and `release` (Ninja, Release, `build/release`), each with matching build and test presets.
  - `gg::kVersion` (`std::string_view`) in `gg/core/Version.h`, from compile definition `GG_VERSION_STRING="${PROJECT_VERSION}"`.
  - `project(GigaGrains VERSION 0.0.1 LANGUAGES C CXX)`.

- [ ] **Step 1: [Director] Install the local toolchain.** Ask the director to approve, then run `brew install cmake ninja`. Expected: `cmake --version` reports ≥ 3.22 and `ninja --version` succeeds.
- [ ] **Step 2: Add the five submodules** at the pins above with `git submodule add`. Run `git submodule status`. Expected: five entries at the pinned SHAs.
- [ ] **Step 3: Write the failing smoke test** in `tests/smoke_tests.cpp`:
  ```cpp
  TEST_CASE("engine library links and reports version", "[smoke]")
  {
      REQUIRE(gg::kVersion == "0.0.1");
      STATIC_REQUIRE(sizeof(void *) == 8);
  }
  ```
- [ ] **Step 4: Run** `cmake --preset dev && cmake --build --preset dev`. Expected: FAIL, because `gg/core/Version.h` is not found.
- [ ] **Step 5: Implement the build files and `Version.h`.**
  - `cmake/compiler-options.cmake`: adapt SCXT's `cmake/compiler-options.cmake`, renaming `sc-`/`SCXT_` to `gg-`/`GG_`.
    - Keep its sanitizer and RTSan option wiring.
    - Add `-ffp-contract=off` (gcc/clang) and `/clang:-ffp-contract=off` (ClangCL) to `gg-compiler-options`.
    - Keep `-Werror` on mac and Linux.
    - Set `CMAKE_OSX_DEPLOYMENT_TARGET 11.0`.
  - `libs/CMakeLists.txt`:
    - `simde` INTERFACE target (as SCXT does);
    - `add_subdirectory` for catch2, fmt, sst-cpputils and sst-basic-blocks;
    - set `SST_BASIC_BLOCKS_SIMD_OMIT_NATIVE_ALIASES ON`, as SCXT does.
  - `README.md`: one paragraph pointing to INTENT.md, plus the three preset commands.
  - `.gitignore`: `build*/`, `.DS_Store`, `/tools-bin/`.
- [ ] **Step 6: Run** `cmake --preset dev && cmake --build --preset dev && ctest --preset dev --output-on-failure`. Expected: PASS (1 test).
- [ ] **Step 7: Commit:** `build: CMake skeleton, presets, core submodules, AGPL license`.

### Task 2: Engine primitives

**Files:**
- Create: `engine/include/gg/core/Rng.h`, `engine/include/gg/core/MathDet.h`, `engine/include/gg/core/Window.h`, `engine/src/Window.cpp`, `engine/include/gg/core/Interpolate.h`, `engine/include/gg/core/DenormalGuard.h`, `engine/include/gg/source/SampleBuffer.h`, `engine/src/SampleBuffer.cpp`
- Test: `tests/engine/rng_tests.cpp`, `tests/engine/mathdet_tests.cpp`, `tests/engine/window_tests.cpp`, `tests/engine/interpolate_tests.cpp`, `tests/engine/denormal_tests.cpp`

**Interfaces:**
- Produces, all in namespace `gg`:
  - `rng::splitmix64(uint64_t) -> uint64_t` (constexpr)
  - `enum class rng::Field : uint32_t { PositionJitter = 1, SignalNoise = 100 }`. The values are part of the determinism contract: append only, never renumber.
  - `struct rng::Key { uint64_t seed; uint32_t layer; uint32_t stream; rng::Field field; uint64_t index; }`
  - `rng::hash(const Key&) -> uint64_t` (constexpr)
  - `rng::uniform01(uint64_t) -> double`, in [0,1)
  - `rng::bipolar(uint64_t) -> double`, in [-1,1)
  - `exp2Det(double) -> double`
  - `class HannTable { static constexpr uint32_t kSize = 4096; HannTable(); float at(double phase01) const noexcept; }`
  - `interp::linear(const float *data, int64_t frames, double pos) noexcept -> float`
  - `interp::hermite4(const float *data, int64_t frames, double pos) noexcept -> float`
  - `class ScopedDenormalsOff` (RAII)
  - `class SampleBuffer { SampleBuffer(uint32_t channels, int64_t frames, double sampleRate); float *channel(uint32_t); const float *channel(uint32_t) const; uint32_t channels() const; int64_t frames() const; double sampleRate() const; }`

- [ ] **Step 1: Write the failing tests:**
  ```cpp
  TEST_CASE("splitmix64 matches reference vector", "[rng]")
  { REQUIRE(gg::rng::splitmix64(0) == 0xE220A8397B1DCDAFull); }
  TEST_CASE("hash is order-sensitive and stable", "[rng]") {
      using namespace gg::rng;
      auto a = hash({1, 0, 0, Field::PositionJitter, 7});
      REQUIRE(a == hash({1, 0, 0, Field::PositionJitter, 7}));
      REQUIRE(a != hash({1, 0, 0, Field::PositionJitter, 8}));
      REQUIRE(a != hash({2, 0, 0, Field::PositionJitter, 7}));
      REQUIRE(a != hash({1, 1, 0, Field::PositionJitter, 7}));
  }
  TEST_CASE("uniform01 range and mean", "[rng]")  // 100000 draws: all in [0,1), |mean-0.5| < 0.005
  TEST_CASE("exp2Det matches std::exp2", "[math]") // x in [-8,8] step 1/64: rel err < 1e-14; exact for integers -8..8
  TEST_CASE("hann overlap-add at 50% sums to one", "[window]") // for p in [0,0.5) step 1e-3: |at(p)+at(p+0.5)-1| < 1e-6; at(0)==0
  TEST_CASE("interpolators reproduce polynomials", "[interp]") // linear exact on ramp; hermite4 exact (1e-5) on ramp and quadratic at fractional pos in [2, frames-3]
  TEST_CASE("interpolators read zero outside the buffer", "[interp]") // pos -10, frames+10 -> 0; frames==1 safe
  TEST_CASE("denormals flush inside guard", "[denormal]") // volatile float t=1e-38f; inside guard t*1e-3f == 0.0f; outside != 0
  ```
- [ ] **Step 2: Run** `ctest --preset dev -L "rng|math|window|interp|denormal"`. Expected: FAIL (compile errors: missing headers).
- [ ] **Step 3: Implement the primitives.** These are the algorithms the signatures don't determine:
  - **`splitmix64`:**
    ```
    z = x + 0x9E3779B97F4A7C15
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9
    z = (z ^ (z >> 27)) * 0x94D049BB133111EB
    return z ^ (z >> 31)
    ```
  - **`hash`:**
    ```
    h = splitmix64(seed)
    h = splitmix64(h ^ layer)
    h = splitmix64(h ^ stream)
    h = splitmix64(h ^ (uint32_t)field)
    h = splitmix64(h ^ index)
    ```
  - **`uniform01`:** `(h >> 11) * 0x1.0p-53`.
  - **`exp2Det`:**
    - Split `x = n + f` with `n = floor(x)` and `f` in [0,1).
    - `2^f = exp(f·ln2)`, using a fixed degree-13 Taylor/Horner polynomial with plain `*` and `+` only.
    - Return `std::ldexp(poly, n)`. `ldexp` is exact.
  - **`HannTable`:**
    - `kSize+1` floats, `w[i] = 0.5 - 0.5*cos(2π i/kSize)`.
    - `at()` interpolates linearly. It returns 0 for phase outside [0,1].
  - **`hermite4`:** 4-point Catmull-Rom over `floor(pos)-1 .. floor(pos)+2`.
  - **`ScopedDenormalsOff`:**
    - x86 / ClangCL: set MXCSR FTZ|DAZ (0x8040) through `_mm_getcsr`/`_mm_setcsr`.
    - AArch64: set FPCR bit 24.
    - Restore on destruction.
- [ ] **Step 4: Run the tests.** Expected: PASS.
- [ ] **Step 5: Commit:** `engine: counter RNG, deterministic exp2, Hann table, interpolators, denormal guard`.

### Task 3: One deterministic grain stream (pool, scheduler, engine)

**Files:**
- Create: `engine/include/gg/grain/GrainPool.h`, `engine/src/GrainPool.cpp`, `engine/include/gg/grain/PeriodicStream.h`, `engine/src/PeriodicStream.cpp`, `engine/include/gg/engine/Engine.h`, `engine/src/Engine.cpp`
- Modify: `INTENT.md` §14 (pool wording) and §23 (new decision row)
- Test: `tests/engine/pool_tests.cpp`, `tests/engine/stream_tests.cpp`, `tests/engine/engine_tests.cpp`

**Interfaces:**
- Consumes: everything from Task 2.
- Produces, all in namespace `gg`:
  - `struct GrainSpec { uint64_t index; int64_t onsetSample; double onsetFrac; double startPos; double rate; double durationSamples; float gain; }`
  - `class GrainPool`:
    - `explicit GrainPool(uint32_t capacity)`
    - `uint32_t capacity() const noexcept; uint32_t size() const noexcept;`
    - `bool spawn(const GrainSpec&) noexcept` — false when full
    - `void retireFinished(int64_t nowSample) noexcept` — **stable** compaction; keeps spawn order
    - `const GrainSpec &at(uint32_t i) const noexcept`
  - `struct StreamParams { double grainRateHz{20.0}; double grainDurationSec{0.1}; double position{0.5}; double positionJitter{0.0}; double pitchSemitones{0.0}; float gain{0.5f}; }`
  - `class PeriodicStream`:
    - `void prepare(double sampleRate) noexcept`
    - `void setParams(const StreamParams&) noexcept` — sanitizes
    - `void reset() noexcept`
    - `template <class Emit> void schedule(int64_t blockStart, uint32_t numFrames, int64_t sourceFrames, double sourceRate, uint64_t seed, Emit &&emit) noexcept`
  - `struct EngineConfig { double sampleRate{48000.0}; uint32_t maxBlockSize{4096}; uint32_t grainCapacity{4096}; uint64_t seed{0}; }`
  - `using GrainEventSink = void (*)(void *ctx, const GrainSpec &);`
  - `class Engine`:
    - `explicit Engine(const EngineConfig&)`
    - `void setSource(std::shared_ptr<const SampleBuffer>)` — **not real-time**; call while not processing
    - `void setStreamParams(const StreamParams&) noexcept`
    - `void setEventSink(GrainEventSink, void *ctx) noexcept`
    - `void reset() noexcept`
    - `void process(float *const *out, uint32_t numChannels, uint32_t numFrames) noexcept SST_CPPUTILS_NONBLOCKING` — overwrites `out`
    - `int64_t sampleClock() const noexcept; uint32_t activeGrains() const noexcept; uint64_t droppedGrains() const noexcept;`

**Rules the signatures don't determine:**
- **Parameter sanitizing.** Non-finite values become the defaults. Then:
  - `grainRateHz ≤ 0` turns the stream off;
  - `grainRateHz` is clamped to ≤ 4000;
  - `position` and `positionJitter` are clamped to [0,1];
  - `pitchSemitones` is clamped to [-48,48];
  - `gain` is clamped to [0,4];
  - duration is clamped to ≥ 16 samples (the minimum window length).
- **Onset lattice.** Grain `k` has onset `t_k = (k·sampleRate)/grainRateHz`. Multiply first, then divide: that rounds once and matches the test literal `floor(k*48000/7.0)`. This gives `onsetSample = floor(t_k)` and `onsetFrac = t_k - onsetSample`. A grain is emitted when `onsetSample ∈ [blockStart, blockStart+numFrames)`. `k` is counted from `reset()`, so the schedule is independent of block size. *(Ruling T3-f.)* When `grainRateHz` changes, the lattice re-anchors at the last spawned onset *(Ruling T3-a)*. The rate is also capped at `sampleRate`, so there is at most one grain per output sample.
- **Grain parameters.**
  - `startPos = clamp(position + positionJitter·bipolar(hash({seed,0,0,PositionJitter,k})), 0, 1)·(sourceFrames-1)`.
  - `rate = exp2Det(pitchSemitones/12)·sourceRate/sampleRate`.
- **Rendering.** For each output sample `t`:
  - elapsed `e = (t - onsetSample) - onsetFrac`;
  - envelope `env = hann.at(e / durationSamples)`;
  - output += `hermite4(src, startPos + rate·e)·env·gain`.
  - Grains are visited in pool order. Each grain contributes to every sample of the block before the next grain starts, so each sample's sum always follows spawn order.
  - A mono source feeds both channels. Channels ≥ 2 output zeros.
- **Process framing.** `process` splits calls larger than `maxBlockSize` internally. Spawning happens before rendering each block, and `retireFinished` runs after. Drops (pool full) increment `droppedGrains` and never stall.

- [ ] **Step 1: Write the failing tests** (values are exact):
  ```cpp
  TEST_CASE("pool keeps spawn order through retirement", "[pool]")      // spawn 0..9 with staggered durations, retire at t; remaining indices strictly increasing
  TEST_CASE("pool full drops deterministically", "[pool]")              // capacity 4, spawn 6 -> 4 accepted, spawn returns false twice
  TEST_CASE("onsets land on the lattice", "[stream]")                  // 48k, 100 Hz: onsets 0,480,960..., frac 0; 48k, 7 Hz: onsetSample==floor(k*48000/7.0), frac matches
  TEST_CASE("stream timing scales with sample rate", "[stream]")        // 20 Hz at 44100/96000/192000: onset k*sr/20 seconds-equal within 1 sample; rate == exp2Det(p/12)*srcSR/sr
  TEST_CASE("no source renders silence", "[engine]")                    // all output samples == 0.0f
  TEST_CASE("grain stream renders non-silent bounded audio", "[engine]")// sine source, 1 s: peak > 0.05, all |x| <= 4, all finite
  TEST_CASE("extreme and non-finite params stay finite", "[engine]")    // NaN/inf/-1/1e9 in every StreamParams field: all output finite
  TEST_CASE("oversized and empty blocks", "[engine]")                   // maxBlockSize 64: process(0) leaves clock; process(1000) == 1000/64 split result bit-exactly
  TEST_CASE("degenerate sources", "[engine]")                           // 1-frame mono, 10-frame stereo (grain 4800 samples), 2-ch 0-frame: finite output, no crash
  TEST_CASE("event sink sees every spawn", "[engine]")                  // count of sink calls == spawned grains; indices consecutive from 0
  TEST_CASE("random param sweep stays finite and bounded", "[engine]")  // 200 iterations: StreamParams fields drawn via gg::rng (seed 7) across 2x their legal range,
                                                                        // random block sizes 1..512, 0.25 s each: every sample finite and |x| <= 16
  ```
- [ ] **Step 2: Run** `ctest --preset dev -L "pool|stream|engine"`. Expected: FAIL (missing headers).
- [ ] **Step 3: Implement** `GrainPool`, `PeriodicStream` and `Engine` per the rules above.
  - `Engine` owns `GrainPool`, `PeriodicStream` and `HannTable`, all allocated in its constructor.
  - `setSource` stores the `shared_ptr`, which keeps the buffer alive, and caches its raw pointer. `process` reads only the raw pointer, so it never touches reference counts.
  - `process` wraps its work in `ScopedDenormalsOff`.
- [ ] **Step 4: Run the tests.** Expected: PASS.
- [ ] **Step 5: Amend INTENT §14.** Change "dense index arrays with swap-remove compaction" to "dense index arrays with **stable** compaction that preserves spawn order, so per-sample summation order (and therefore output) is independent of host block size". Add a §23 row: decision "Grain pool uses stable compaction, not swap-remove"; why "Float summation order must not depend on block boundaries (§8.6)".
- [ ] **Step 6: Commit:** `engine: periodic grain stream, SoA pool with stable compaction, Engine API`.

### Task 4: Determinism and real-time-safety harness

**Files:**
- Create: `tests/support/AllocTripwire.h`, `tests/support/AllocTripwire.cpp`, `tests/engine/determinism_tests.cpp`, `tests/engine/rt_tests.cpp`
- Modify: `CMakePresets.json`. Add presets:
  - `asan` (Debug, `GG_SANITIZE=ON`, `GG_BUILD_PLUGINS=OFF`, `build/asan`);
  - `rtsan` (RelWithDebInfo, `GG_USE_RTSAN=ON`, `GG_BUILD_PLUGINS=OFF`, `CMAKE_CXX_COMPILER=$env{GG_LLVM}/bin/clang++`, `CMAKE_C_COMPILER=$env{GG_LLVM}/bin/clang`, `build/rtsan`).
- Modify: `engine/src/Engine.cpp` (confirm the `SST_CPPUTILS_NONBLOCKING` annotation compiles under RTSan)

**Interfaces:**
- Consumes: the `gg::Engine` API from Task 3.
- Produces:
  - `namespace gg::test { struct AllocTripwire { struct Scope { Scope(); ~Scope(); }; static uint64_t count() noexcept; static void resetCount() noexcept; }; }`
  - All global `operator new` variants (plain, array, aligned, nothrow) increment `count()` while a `Scope` is alive on the current thread (thread-local arming). They still allocate normally.

- [ ] **Step 1: Write the failing tests:**
  ```cpp
  TEST_CASE("render is identical across block sizes", "[determinism]")
  // Engine{48000, 4096, 4096, seed 42}, sine source 4 s, StreamParams{37 Hz, 0.083 s, 0.4, 0.3, 5.0, 0.7}, 3 s output.
  // Reference = block 4096. For block sizes {1, 32, 64, 511, 4096} AND a random split
  // (sizes 1..4096 from gg::rng with seed 99): std::memcmp(ref, other) == 0 for both channels.
  TEST_CASE("render twice is identical", "[determinism]")      // two fresh Engines, same config -> memcmp == 0
  TEST_CASE("different seeds differ", "[determinism]")         // seed 1 vs 2 with jitter 0.3 -> memcmp != 0
  TEST_CASE("tripwire detects allocation", "[rt]")             // inside Scope: auto *p = new int(1); delete p; count()==1
  TEST_CASE("engine process does not allocate", "[rt]")        // warm engine; Scope around 100 process() calls of 64 frames with active grains -> count()==0
  #if GG_USE_RTSAN
  TEST_CASE("rtsan is active in this build", "[rt][rtsan]") { REQUIRE(SST_CPPUTILS_HAS_RTSAN == 1); }
  #endif
  ```
- [ ] **Step 2: Run** `ctest --preset dev -L "determinism|rt"`. Expected: the tripwire tests FAIL (link error: no `AllocTripwire`). If a determinism test fails, fix the engine (Task 3 rules), not the test.
- [ ] **Step 3: Implement `AllocTripwire`** and link it into `gg-tests`. Make `GG_USE_RTSAN` a compile definition (0 or 1) on `gg-tests`.
- [ ] **Step 4: Run the default suite** with `ctest --preset dev --output-on-failure`. Expected: PASS.
- [ ] **Step 5: Run the sanitizer presets locally:**
  - `cmake --preset asan && cmake --build --preset asan && ctest --preset asan` → PASS, with no ASan/UBSan reports.
  - `GG_LLVM=/opt/homebrew/opt/llvm cmake --preset rtsan && cmake --build --preset rtsan && ctest --preset rtsan` → PASS, including `rtsan is active`. If Homebrew LLVM can't build on macOS, note it in the task report; the CI Linux job (Task 10) is authoritative.
- [ ] **Step 6: Commit:** `test: block-size invariance, allocation tripwire, ASan/RTSan presets`.

### Task 5: Audio metrics library

**Files:**
- Submodules: `libs/dr_libs` (mackron/dr_libs, pin the current master SHA), `libs/pffft` (marton78/pffft, pin SHA; build `pffft.c` only, as a static lib `pffft`), `libs/stb` (nothings/stb, pin SHA; header-only `stb_image_write.h`), `libs/nlohmann_json` (nlohmann/json, tag `v3.12.0`; used for metrics JSON here and render specs in Task 6)
- Create: `tools/audiometrics/CMakeLists.txt`, `tools/audiometrics/include/gg/metrics/AudioData.h`, `tools/audiometrics/include/gg/metrics/WavIO.h`, `tools/audiometrics/src/WavIO.cpp`, `tools/audiometrics/include/gg/metrics/Metrics.h`, `tools/audiometrics/src/Metrics.cpp`, `tools/audiometrics/include/gg/metrics/Spectrogram.h`, `tools/audiometrics/src/Spectrogram.cpp`
- Modify: `libs/CMakeLists.txt`, `CMakeLists.txt` (`add_subdirectory(tools/audiometrics)`), INTENT §15 dependency table (add rows: stb_image_write, public domain / MIT, spectrogram PNGs; nlohmann/json, MIT, metrics and render-spec JSON)
- Test: `tests/metrics/metrics_tests.cpp`

**Interfaces:**
- Produces, all in namespace `gg::metrics`:
  - `struct AudioData { double sampleRate; std::vector<std::vector<float>> channels; }`
  - `std::optional<AudioData> readAudioFile(const std::string &path)` — WAV, FLAC, MP3
  - `bool writeWavFloat32(const std::string &path, const AudioData&)`
  - `struct Metrics { double peakDb; std::vector<double> dc; std::vector<double> rmsEnvelopeDb; std::vector<double> bandProfileDb; double stereoCorrelation; double clickScore; uint64_t nonFinite; uint64_t denormals; }`
  - `Metrics computeMetrics(const AudioData&)`
  - `struct Tolerances { double rmsEnvelopeDb{0.5}; double bandProfileDb{1.0}; double peakDb{0.5}; double dc{1e-4}; double correlation{0.02}; double maxClickScore{30.0}; }`
  - `struct CompareResult { bool pass; std::vector<std::string> failures; }`
  - `CompareResult compare(const Metrics &golden, const Metrics &actual, const Tolerances& = {})`
  - `std::string toJson(const Metrics&)`, `std::optional<Metrics> metricsFromJson(const std::string&)` — both use nlohmann/json; doubles round-trip exactly
  - `bool writeSpectrogramPng(const std::string &path, const AudioData&)`

**Metric definitions.** These are the contract that goldens depend on.
- **`peakDb`:** `20·log10(max|x|)` over all channels, with a floor of -200.
- **`dc`:** the mean of each channel.
- **`rmsEnvelopeDb`:** the mean of all channels, in 480-sample frames (10 ms at 48 k; use `round(0.01·sr)`), `20·log10(rms)` with a floor of -200. `compare` ignores frames where both values are below -60.
- **`bandProfileDb`:**
  - frames of 2048 samples, Hann window, hop 1024, using pffft on the mono mix;
  - 32 log-spaced bands from 20 Hz to Nyquist;
  - mean power per band across frames, in dB, with a floor of -200;
  - `compare` ignores bands where both values are below -90.
- **`stereoCorrelation`:** Pearson correlation of L/R. 1.0 for mono, or when either channel is silent.
- **`clickScore`:**
  - `d[n] = |x[n] - 2x[n-1] + x[n-2]|` on the mono mix; `R = rms(d)` over the whole signal;
  - `clickScore = max_n(d[n] / R)`, counting only samples with `d[n] > 1e-3`; 0 if there are none.
  - `compare` checks that `actual.clickScore <= maxClickScore`. It is absolute, not relative to the golden.
- **`nonFinite` / `denormals`:** counts of non-finite samples and of float subnormals. `compare` requires both to be 0.
- **Spectrogram:**
  - STFT 2048 / hop 512 on the mono mix;
  - linear frequency, image height 1024 with Nyquist at the top, one column per frame;
  - grayscale `clamp((dB+120)/120, 0, 1)`.

- [ ] **Step 1: Write the failing tests:**
  ```cpp
  TEST_CASE("peak, dc and rms of a known sine", "[metrics]")       // 1 kHz amp 0.5, 1 s @48k: peakDb≈-6.02(±0.01), |dc|<1e-6, envelope ≈ -9.03 dB (±0.05)
  TEST_CASE("band profile peaks at the sine band", "[metrics]")    // 1 kHz -> argmax band contains 1 kHz
  TEST_CASE("correlation of identical, inverted, silent", "[metrics]") // 1.0, -1.0 (±1e-6), 1.0
  TEST_CASE("click score separates clicks from clean audio", "[metrics]")
  // clean 220 Hz sine amp 0.5: score < 30; uniform noise via gg::rng: score < 30; sine with +0.5 step at n=24000: score > 30
  TEST_CASE("non-finite and denormal counting", "[metrics]")      // inject 1 NaN, 1 inf, 2 subnormals -> nonFinite==2, denormals==2
  TEST_CASE("compare passes self and fails on gain change", "[metrics]") // self: pass; actual scaled by 0.8 (-1.94 dB): fail mentioning "rmsEnvelope"
  TEST_CASE("wav round trip is bit exact", "[metrics]")           // write float32 then read -> identical samples, same sample rate
  ```
- [ ] **Step 2: Run** `ctest --preset dev -L metrics`. Expected: FAIL (missing headers).
- [ ] **Step 3: Implement** `gg-audiometrics` (STATIC, links pffft, dr_libs, stb, fmt) to the definitions above.
- [ ] **Step 4: Run the tests.** Expected: PASS.
- [ ] **Step 5: Commit:** `tools: audio metrics, compare, WAV IO, spectrogram`.

### Task 6: `gg-render` CLI and golden renders

**Files:**
- Submodules: `libs/CLI11` (CLIUtils/CLI11, latest v2.x tag)
- Create: `tools/gg-render/CMakeLists.txt`, `tools/gg-render/RenderSpec.h`, `tools/gg-render/RenderSpec.cpp`, `tools/gg-render/Signals.h`, `tools/gg-render/Signals.cpp`, `tools/gg-render/Render.h`, `tools/gg-render/Render.cpp`, `tools/gg-render/main.cpp`
- Create: `tests/golden/cases/{sparse-sine,sparse-sine-44k1,dense-noise,sweep-stretch}.json`, `tests/golden/expected/<case>.metrics.json`, `tests/golden/expected/<case>.events.jsonl`, `scripts/update-goldens.sh`
- Modify: `tests/CMakeLists.txt` (golden ctest entries), INTENT §15 dependency table (CLI11, BSD-3-Clause, render CLI), INTENT §23 (one row: "M0 tooling dependencies: dr_libs, pffft, stb_image_write, nlohmann/json, CLI11")
- Test: `tests/tools/render_spec_tests.cpp`, `tests/tools/signals_tests.cpp`

**Interfaces:**
- Consumes: `gg::Engine` (Task 3), `gg::metrics` (Task 5).
- Produces:
  - **The render-spec JSON schema:**
    ```json
    {"sampleRate":48000,"durationSeconds":3.0,"blockSize":128,"seed":1,
     "source":{"kind":"sine|sweep|noise|file","frequencyHz":220,"startHz":50,"endHz":8000,
               "seconds":4.0,"amplitude":0.5,"path":"relative/to/spec.wav"},
     "stream":{"grainRateHz":8,"grainDurationMs":60,"position":0.5,"positionJitter":0.2,
               "pitchSemitones":0,"gain":0.8}}
    ```
  - `std::optional<RenderSpec> parseRenderSpec(const std::string &json, std::string &error)`
  - `gg::metrics::AudioData makeSignal(const SourceSpec&, double sampleRate, uint64_t seed)`. Noise uses `rng::Field::SignalNoise`. Sweep is exponential.
  - `gg::metrics::AudioData render(const RenderSpec&, std::vector<std::string> *eventLines)`
  - **CLI:** `gg-render <spec.json> [-o out.wav] [--metrics out.json] [--events out.jsonl] [--spectrogram out.png] [--compare golden.metrics.json] [--compare-events golden.events.jsonl] [--block-size N]`
    - Exit codes: 0 = pass, 1 = comparison failed, 2 = bad input.
  - **Event line format:** `{"i":<index>,"onset":<onsetSample>,"frac":<d>,"pos":<d>,"rate":<d>,"dur":<d>}`. Every `<d>` is formatted with `fmt::format("{}", double)` (shortest round-trip), so lines are byte-identical across platforms.

**The golden cases** (all `seed:1`, `durationSeconds:3`, `blockSize:128`):

| Case | Source | Stream (rateHz / durMs / pos / jitter / pitch / gain) | sampleRate |
|---|---|---|---|
| sparse-sine | sine 220 Hz, 4 s, amp 0.5 | 8 / 60 / 0.5 / 0.2 / 0 / 0.8 | 48000 |
| sparse-sine-44k1 | same | same | 44100 |
| dense-noise | noise 4 s, amp 0.5 | 400 / 50 / 0.3 / 0.5 / 7 / 0.1 | 48000 |
| sweep-stretch | sweep 50→8000 Hz, 4 s, amp 0.5 | 30 / 200 / 0.75 / 0.01 / -12 / 0.4 | 48000 |

- [ ] **Step 1: Write the failing tests:**
  ```cpp
  TEST_CASE("render spec parses all fields", "[render]")      // parse the sparse-sine case; every field equals the table
  TEST_CASE("render spec rejects bad input", "[render]")      // missing "stream", negative sampleRate, unknown kind -> nullopt + non-empty error
  TEST_CASE("signals are deterministic", "[render]")          // makeSignal(noise, 48k, seed 1) twice -> identical; seed 2 differs
  TEST_CASE("render block size does not change output", "[render]") // render(spec) vs blockSize 1 -> identical samples and identical event lines
  ```
- [ ] **Step 2: Run** `ctest --preset dev -L render`. Expected: FAIL.
- [ ] **Step 3: Implement** `gg-render` per the interfaces above.
- [ ] **Step 4: Run the tests.** Expected: PASS.
- [ ] **Step 5: Generate the goldens.** `scripts/update-goldens.sh` renders each case into `tests/golden/expected/` (`.metrics.json` and `.events.jsonl`) and writes the WAV and PNG to `<build dir>/golden/`. Run it, then inspect each spectrogram PNG (Read tool). Expected: visible grain streaks matching the table, no vertical full-band lines (clicks), `clickScore < 30`, `nonFinite == denormals == 0`.
- [ ] **Step 6: Register the goldens in CTest.** For each case, add `golden_<case>`, which runs `gg-render <case>.json -o ${CMAKE_BINARY_DIR}/golden/<case>.wav --spectrogram ${CMAKE_BINARY_DIR}/golden/<case>.png --compare expected/<case>.metrics.json --compare-events expected/<case>.events.jsonl`, with CTest label `golden`. Run `ctest --preset dev -L golden`. Expected: PASS (4 tests).
- [ ] **Step 7: Commit** the goldens, CLI and INTENT edits: `tools: gg-render CLI, golden cases with metric and event-log comparison`.

### Task 7: Kernel benchmark spike (`gg-bench`)

**Files:**
- Create: `tools/gg-bench/CMakeLists.txt`, `tools/gg-bench/Kernels.h`, `tools/gg-bench/main.cpp`, `docs/spikes/m0-kernel-benchmark.md`
- Modify: INTENT §16 (measured numbers) and §23 (new row)
- Test: `tests/tools/bench_kernel_tests.cpp`

**Interfaces:**
- Consumes: `gg::interp`, `gg::HannTable`, `gg::rng`; `sst/basic-blocks/simd/setup.h` (`SIMD_M128`, `SIMD_MM`).
- Produces: **prototype** kernels, to be promoted or rewritten in M1. They live in `namespace gg::bench`:
  - `struct GrainBatch { std::vector<double> pos, rate; std::vector<float> envPhase, envInc, gain; }` (SoA)
  - `void renderLinearScalar(const GrainBatch&, const float *srcL, const float *srcR, int64_t frames, float *outL, float *outR, uint32_t n)`
  - `renderHermiteScalar` and `renderSincScalar` (16-tap windowed sinc, 512-phase table), same signature
  - `renderLinearSimd` and `renderHermiteSimd`, same signature: grain-major, 4 output samples per SIMD step
  - **CLI:** `gg-bench [--quick] [--json out.json]`

- [ ] **Step 1: Write the failing test:**
  ```cpp
  TEST_CASE("simd kernels match scalar reference", "[bench]")
  // 64 grains, random pos over a 10 s stereo noise buffer, rate in [0.5, 2], block 256:
  // for each SIMD kernel, max |simd - scalar| <= 1e-5 * max|scalar| on both channels
  ```
- [ ] **Step 2: Run** `ctest --preset dev -L bench`. Expected: FAIL.
- [ ] **Step 3: Implement the kernels and `gg-bench`.**
  - **Workload:** 60 s stereo noise source at 48 kHz; 2000 grains of 100 ms; random positions and rates in [0.5, 2]; block sizes 32 / 64 / 256.
  - **Timing:** each configuration runs ≥ 2 s; take the median of 5 runs. `--quick` runs 0.2 s.
  - **Output JSON:** `{"machine","compiler","results":[{"kernel","grains","blockSize","nsPerStereoGrainSample","cpuPctOneCoreAt48k"}]}`, where `cpuPctOneCoreAt48k = ns·grains·48000/1e9·100`.
- [ ] **Step 4: Run the test.** Expected: PASS.
- [ ] **Step 5: Benchmark locally.** Run `build/release/tools/gg-bench/gg-bench --json build/bench-m5max.json` from the `release` preset. Expected: a JSON file with 15 results (5 kernels × 3 block sizes).
- [ ] **Step 6: Write `docs/spikes/m0-kernel-benchmark.md`:**
  - the local results table;
  - the comparison against INTENT §16's estimates (1–1.5 / 2 / 7–13 ns);
  - a note that M1-runner and x86 numbers come from CI (Task 10).

  Then update INTENT §16: replace the first-principles estimate bullet with the measured M5 Max numbers, keep the headline target, and add a §23 row. **If Hermite at 2000 grains exceeds 25% of one core even on the M5 Max, stop and report to the director** before changing the headline target.
- [ ] **Step 7: Commit:** `bench: prototype grain kernels and gg-bench; record M0 kernel numbers`.

### Task 8: CLAP plugin shells (two products, every format)

**Files:**
- Submodules:
  - `libs/clap/clap` (tag `1.2.10`)
  - `libs/clap/clap-helpers` (SHA `a380c97`)
  - `libs/clap/clap-wrapper` (SHA `4e7afc3`)
- Create:
  - `plugin/CMakeLists.txt`
  - `plugin/include/gg/plugin/Identity.h`
  - `plugin/src/HelpersConfig.h`, `plugin/src/HelpersConfig.cpp` — mirrors SCXT's `clap-helpers-config`
  - `plugin/src/Factory.h`, `plugin/src/Factory.cpp`
  - `plugin/src/GigaGrainsPlugin.h`, `plugin/src/GigaGrainsPlugin.cpp`
  - `clients/clap-first/CMakeLists.txt`, `clients/clap-first/gg-clap-entry.cpp` (copy SCXT's `scxt-clap-entry.cpp` pattern, including the RTSan default-options block)
  - `cmake/products.cmake` — `gg-products` target copying artifacts to `build/<preset>/gg-products/`, adapted from SCXT `cmake/basic-installer.cmake`
  - `scripts/fetch-validators.sh <dest>` — downloads pinned release binaries of clap-validator and pluginval for the host OS; pin the latest releases and write the versions into the script
- Modify: `libs/CMakeLists.txt` (clap, clap-helpers, clap-wrapper with `CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE`, `CLAP_WRAPPER_DONT_ADD_TARGETS TRUE`, `CLAP_WRAPPER_BUILD_AUV2 TRUE`), root `CMakeLists.txt` (guarded by `GG_BUILD_PLUGINS`)
- Modify: INTENT §18 identities table (fill in the subtypes), §22 (mark the identities question resolved), §23 (row: "Plugin identities fixed", listing every value from Global Constraints)
- Test: `tests/plugin/factory_tests.cpp`, `tests/plugin/process_tests.cpp`

**Interfaces:**
- Consumes: `gg::Engine` (Task 3).
- Produces:
  - **`gg::plugin::identity` constants:** `kVendor`, `kUrl`, `kInstrumentId`, `kFxId`, `kInstrumentName`, `kFxName`, `kAuManufacturer`, `kAuManufacturerName`, `kAuInstrumentSubtype` (`"GGin"`), `kAuFxSubtype` (`"GGfx"`). Values per Global Constraints.
  - **Factory functions** in `gg::plugin`:
    - `const void *getFactory(const char *factoryId)` answers `CLAP_PLUGIN_FACTORY_ID`, `CLAP_PLUGIN_FACTORY_INFO_AUV2` and `CLAP_PLUGIN_FACTORY_INFO_AUV2_LEGACY`.
    - `bool clapInit(const char *)`, `void clapDeinit()`.
  - **The AUv2 factory:**
    - manufacturer `Sbcx`/`sethbc`;
    - index 0 → `au_type "aumu"`, `au_subt "GGin"`;
    - index 1 → `au_type "aufx"`, `au_subt "GGfx"`.
  - **The legacy-identity factory:**
    - `count(index 1) == 1` → `{aumf, GGfx, Sbcx}`;
    - `count(index 0) == 0`.
  - **Descriptors:**
    - Instrument: id `org.sethbc.giga-grains`, features `instrument, synthesizer, granular, stereo`.
    - FX: id `org.sethbc.giga-grains-fx`, features `audio-effect, granular, stereo`.
    - Both: vendor `sethbc`, url `kUrl`, version `GG_VERSION_STRING`.
  - **`enum class Product { Instrument, Fx }`** and **`class GigaGrainsPlugin : public plugHelper_t`**:
    - `GigaGrainsPlugin(const clap_host *, Product)`
    - `std::atomic<uint32_t> noteOnCount{0}`
    - `Product product() const noexcept`
  - **Audio ports:**
    - Instrument: 0 in, 1 out (id 0, stereo, main).
    - FX: 1 in (id 0, stereo, main, `in_place_pair = 1`) and 1 out (id 1, stereo, main, `in_place_pair = 0`).
  - **Note ports:** both have 1 input, dialects `CLAP | MIDI | MIDI_MPE`, preferred `CLAP`.
  - **`activate()`** builds the `gg::Engine` with the host sample rate and `maxFrames`.
  - **`process()`:**
    - Instrument: `engine.process` into the outputs.
    - FX: copy the input to the output (handle in-place aliasing), then add `engine.process` output. With no source, that output is silence.
    - Both: count CLAP `NOTE_ON` events, plus MIDI 0x9n with velocity > 0, into `noteOnCount`.
    - `process` is `SST_CPPUTILS_NONBLOCKING` and never allocates.
  - **CMake:**
    - `make_clapfirst_plugins(TARGET_NAME gg_clapfirst IMPL_TARGET gg-plugin-impl OUTPUT_NAME "Giga Grains" ENTRY_SOURCE gg-clap-entry.cpp BUNDLE_IDENTIFIER "org.sethbc.giga-grains" BUNDLE_VERSION ${PROJECT_VERSION} COPY_AFTER_BUILD ${GG_COPY_PLUGIN_AFTER_BUILD} PLUGIN_FORMATS CLAP VST3 AUV2 STANDALONE WINDOWS_FOLDER_VST3 TRUE ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/gg_assets STANDALONE_CONFIGURATIONS standalone "Giga Grains" "org.sethbc.giga-grains" standalone_fx "Giga Grains FX" "org.sethbc.giga-grains-fx")`
    - The products are one bundle per format (`Giga Grains.clap`, `.vst3`, `.component`) containing both plugins, plus two standalone apps.

- [ ] **Step 1: Write the failing tests:**
  ```cpp
  TEST_CASE("factory exposes two plugins with frozen ids", "[plugin]")   // count==2; ids, names, vendor, features[0] per Interfaces
  TEST_CASE("auv2 identities", "[plugin]")                              // get_auv2_info(0) -> aumu/GGin; (1) -> aufx/GGfx; manu Sbcx
  TEST_CASE("fx also answers to aumf", "[plugin]")                      // legacy count(1)==1 -> {aumf,GGfx,Sbcx}; count(0)==0
  TEST_CASE("fx passes audio through bit-exactly", "[plugin]")         // stub clap_host; activate 48k/512; 512-frame noise in -> out memcmp==0, also with in==out aliasing
  TEST_CASE("instrument outputs silence", "[plugin]")                  // all zeros
  TEST_CASE("note-ons are counted from clap and midi", "[plugin]")     // 2 CLAP NOTE_ON + 1 MIDI 0x90 vel 100 + 1 MIDI 0x90 vel 0 -> noteOnCount==3
  TEST_CASE("zero-frame process is safe", "[plugin]")                  // frames_count 0 -> CLAP_PROCESS_CONTINUE
  TEST_CASE("reactivate at new sample rate", "[plugin]")               // activate 48k -> process -> deactivate -> activate 96k -> process: no crash, engine sampleRate==96000
  ```
  The stub host is a minimal `clap_host` whose `get_extension` returns nullptr. Test via `gg-plugin-impl` directly, not the built `.clap`. The `tests/plugin/*` sources are added to `gg-tests` only when `GG_BUILD_PLUGINS` is ON.
- [ ] **Step 2: Run** `ctest --preset dev -L plugin`. Expected: FAIL.
- [ ] **Step 3: Implement** the plugin library, the entry point, `make_clapfirst_plugins` and `gg-products` per the interfaces above.
- [ ] **Step 4: Run** `ctest --preset dev --output-on-failure`. Expected: every test passes.
- [ ] **Step 5: Validate locally.**
  - Run `scripts/fetch-validators.sh tools-bin`, then `cmake --build --preset release --target gg-products`.
  - Run:
    - `tools-bin/clap-validator validate --exclude '^param-conversions$' "build/release/gg-products/Giga Grains.clap"`. The exclusion works around a divide-by-zero in clap-validator itself on zero-parameter plugins (free-audio/clap-validator#40). Drop it once the plugins have real parameters (M1). *(Ruling T8-c.)*
    - `tools-bin/pluginval --strictness-level 10 --validate "build/release/gg-products/Giga Grains.vst3"`
  - Copy `Giga Grains.component` to `~/Library/Audio/Plug-Ins/Components/`, run `killall -9 AudioComponentRegistrar`, then run `auval -v aumu GGin Sbcx`, `auval -v aufx GGfx Sbcx` and `auval -v aumf GGfx Sbcx`.
  - Expected: every validator reports pass, and all three auval runs end with `AU VALIDATION SUCCEEDED`.
  - **If the `aumf` legacy identity fails auval, stop and report to the director.** The options are an aumf-only FX or a third descriptor. Don't pick one yourself.
- [ ] **Step 6: Update INTENT** §18, §22 and §23 per Files.
- [ ] **Step 7: Commit:** `plugin: CLAP-first shells for Giga Grains and Giga Grains FX in CLAP/VST3/AUv2/standalone`.

### Task 9: GUI shell and canvas stress spike

**Files:**
- Submodules: `libs/JUCE` (tag `8.0.16-security` when Task 9 ran; now tag `9.0.4` @ `3fb5cbc` per ruling T11), `libs/sst/sst-clap-helpers` (SHA `f17567a`)
- Create: `ui/CMakeLists.txt` with two targets: `gg-ui-core` (STATIC, framework-free: `SplatRenderer`) and `gg-ui` (STATIC, JUCE: `Editor`, links `gg-ui-core`, `clap_juce_shim`, `clap_juce_shim_headers`). Plus `ui/include/gg/ui/SplatRenderer.h`, `ui/src/SplatRenderer.cpp`, `ui/include/gg/ui/Editor.h`, `ui/src/Editor.cpp`.
- Modify:
  - `libs/CMakeLists.txt`: `add_subdirectory(sst/sst-clap-helpers)` and `add_clap_juce_shim(JUCE_PATH ${GG_JUCE_PATH})`
  - `plugin/src/GigaGrainsPlugin.h/.cpp`: inherit `sst::clap_juce_shim::EditorProvider`; add `ADD_SHIM_IMPLEMENTATION(clapJuceShim)` and `ADD_SHIM_LINUX_TIMER(clapJuceShim)`; implement `createEditor`, timer and posix-fd registration as SCXT's `scxt-plugin.h` does
  - `tools/gg-bench/main.cpp`: add `--canvas` (links `gg-ui-core` only)
- Test: `tests/ui/splat_tests.cpp`. It links `gg-ui-core` only, so it also runs in the sanitizer jobs (`GG_BUILD_PLUGINS=OFF`).

**Interfaces:**
- Consumes: `GigaGrainsPlugin::noteOnCount`, `Product`; `gg::rng`.
- Produces:
  - **`gg::ui::SplatRenderer`** (framework-free):
    - `SplatRenderer(uint32_t width, uint32_t height)`
    - `void clear() noexcept`
    - `void add(float x, float y, float radius, float intensity) noexcept`
    - `void resolveToArgb(uint32_t *dst, uint32_t strideU32, uint32_t rgbTint) const noexcept`
    - `float accumAt(uint32_t x, uint32_t y) const noexcept`
  - **Splat kernel:**
    - adds `intensity·max(0, 1 - d²/r²)` to each pixel whose center is within radius `r`;
    - clips to bounds;
    - `resolveToArgb` maps each pixel value `a` to `v = 1 - exp(-a)` and writes `tint·v`, alpha 255, so the output saturates smoothly.
  - **`gg::ui::Editor`** (a `juce::Component`, 900×500, resizable). It shows:
    - the product name and `gg::kVersion`;
    - "MIDI note-ons: N", polled from `noteOnCount` at 30 Hz;
    - a canvas stress panel: a combo box for N ∈ {0, 4096, 8192, 16384}; N synthetic grains animated by `rng::hash` of (frame, i) and splatted at physical-pixel resolution; then `drawImage`; and a readout "frame ms (avg 60)" covering splat, resolve and paint.
  - **`gg-bench --canvas`:** headless splat and resolve timing at 3200×1200 for N = 4096, 8192 and 16384 grains, printed as JSON.

- [ ] **Step 1: Write the failing tests:**
  ```cpp
  TEST_CASE("splat writes intensity at center and nothing outside radius", "[ui]") // add(10,10,3,0.7): accumAt(10,10)==0.7f; accumAt(14,10)==0
  TEST_CASE("splats are additive", "[ui]")                                    // two add() at same point -> 2x
  TEST_CASE("offscreen splats are clipped safely", "[ui]")                    // add(-5,-5,8,1) and add(w+3,h+3,8,1): no crash (ASan), in-bounds pixels updated
  TEST_CASE("resolve saturates monotonically", "[ui]")                         // intensities 0,0.5,1,4,100 -> channel values strictly increasing up to <=255
  ```
- [ ] **Step 2: Run** `ctest --preset dev -L ui`. Expected: FAIL.
- [ ] **Step 3: Implement** `SplatRenderer`, `Editor`, the plugin shim integration and `gg-bench --canvas`.
- [ ] **Step 4: Run** `ctest --preset dev`, rebuild `gg-products`, and rerun all Task 8 Step 5 validators, with pluginval GUI tests enabled on macOS. Expected: all pass.
- [ ] **Step 5: Spot-check the standalone.** Open `build/release/gg-products/Giga Grains FX.app` and screenshot its window (`screencapture -l <windowid>`, or ask the director). Expected: the name, version and MIDI counter are visible, and the stress panel updates.
- [ ] **Step 6: Record canvas numbers.** Run `gg-bench --canvas` and record the results in `docs/spikes/m0-canvas-budget.md`, against the INTENT §16 budget of ≤ 3 ms with 8k grains.
- [ ] **Step 7: Commit:** `ui: JUCE editor via clap_juce_shim, splat renderer and canvas stress spike`.

### Task 10: Continuous integration

**Files:**
- Create: `.github/workflows/ci.yml`, `.github/workflows/format.yml`
- Modify: `CMakePresets.json`. Add presets:
  - `mac-universal` (inherits `release`, `CMAKE_OSX_ARCHITECTURES=arm64;x86_64`);
  - `windows-clangcl` (generator `Visual Studio 17 2022`, `architecture x64`, `toolset ClangCL`, Release, `build/windows`);
  - `linux-gcc12` (inherits `release`, gcc-12/g++-12).

**Interfaces:**
- Consumes: the presets, `gg-all`, `gg-products`, `scripts/fetch-validators.sh`, `gg-bench`, and the golden tests.
- Produces: required CI checks. In `ci.yml`, triggered on `push` to `main`, on `pull_request` and by `workflow_dispatch` (ruling T10-a):
  1. **`build`**, a matrix over `macos-latest`/`mac-universal`, `windows-2022`/`windows-clangcl` and `ubuntu-22.04`/`linux-gcc12`:
     - check out with `submodules: recursive`;
     - `surge-synthesizer/sst-githubactions/prepare-for-juce`, pinned by commit SHA, with `gccversion: 12`;
     - configure and build `gg-all` plus `gg-products`, then run `ctest --preset <p> --output-on-failure`. This includes the golden event-log comparison, so it is the cross-platform determinism gate.
     - Run `scripts/fetch-validators.sh`, then clap-validator on `Giga Grains.clap` and pluginval at strictness 10 on `Giga Grains.vst3` (on Linux add `--skip-gui-tests` and run under `xvfb-run`).
     - On macOS only, the three `auval` runs from Task 8.
     - Steinberg's VST3 validator: build the `validator` target from the VST3 SDK that clap-wrapper downloads (`public.sdk/samples/vst-hosting/validator`), then run it on `Giga Grains.vst3`. If that SDK checkout doesn't contain the validator sources, add an INTENT §22 item "Steinberg VST3 validator in CI — M1" and continue. pluginval still covers VST3.
     - The benchmark is **reported, not gated**, in M0. It runs at full length, `gg-bench --json` without `--quick` (ruling T10-c). The CPU gate from INTENT §19 turns on in M1 as a regression gate at the measured baseline, and the §16 headline becomes the gate once M1 meets it (director ruling, Task 10b Step 5; INTENT §23 #23).
     - Upload `build/<preset>/golden/` and the full-length `gg-bench --json` output as artifacts.
  2. **`sanitizers`** on `ubuntu-24.04`: install clang-20 via `apt.llvm.org/llvm.sh 20`, then run the `asan` preset with ctest, and the `rtsan` preset with `GG_LLVM=/usr/lib/llvm-20` and ctest.
  3. **`format.yml`:** `scripts/format.sh --check`, which runs clang-format 23 with `--dry-run --Werror` over `engine plugin ui tools tests clients`. Its major version matches developers' Homebrew LLVM (ruling R6; this replaced clang-format-20). Only clang-format is installed, from a hash-pinned PyPI wheel (ruling T10-a).
  - Every third-party action is pinned by commit SHA.

- [ ] **Step 1: Write the workflows and presets.**
- [ ] **Step 2: Verify the presets locally.** Run `cmake --list-presets` (expect every preset listed) and `cmake --preset mac-universal && cmake --build --preset mac-universal --target gg-all gg-products` (expect a build with `lipo -archs` reporting `x86_64 arm64` on the `.clap` binary).
- [ ] **Step 3: [Director] Approve publishing.** Get approval to push branch `m0-foundations` and open a draft PR to `sethbc/giga-grains`, then push.
- [ ] **Step 4: Check the run.** Run `gh run watch` (or `gh pr checks --watch`). Expected: every job green. Fix failures with new commits. Never weaken a check to make it pass; if a check is wrong, explain why in the commit message.
- [ ] **Step 5: Record CI benchmark numbers.** Download the bench artifacts (`gh run download`) and add the M1-runner and x86 numbers to `docs/spikes/m0-kernel-benchmark.md` and INTENT §16, with a §23 row if the headline target changes. **If CI numbers miss the headline target (2,000 Normal grains < 25% of one M1 P-core), stop and report to the director.**
- [ ] **Step 6: Commit and push:** `ci: build, test, golden, validators, sanitizers and format checks on mac/win/linux`.

### Task 11: JUCE 9 spike

**Files:**
- Create: `docs/spikes/m0-juce9.md`
- Modify (only if the spike succeeds): the `libs/JUCE` submodule pin, plus INTENT §15, §22 and §23

**Interfaces:**
- Consumes: the `GG_JUCE_PATH` option (Task 1).

- [ ] **Step 1: Check out JUCE 9.0.4** into the scratch area: `git clone --depth 1 --branch 9.0.4 https://github.com/juce-framework/JUCE ../gg-juce9`.
- [ ] **Step 2: Build against it.** Run `cmake --preset release -B build/juce9 -DGG_JUCE_PATH=$(realpath ../gg-juce9) && cmake --build build/juce9 --target gg-all gg-products`. Capture the full error output if it fails.
- [ ] **Step 3: If it builds,** run the Task 8 Step 5 validators and the Task 9 Step 5 spot check against `build/juce9`.
- [ ] **Step 4: Write `docs/spikes/m0-juce9.md`** with the outcome, errors and validator results. Then:
  - **If everything passes:** move the submodule to the 9.0.4 SHA, update INTENT §15 ("Start on JUCE 9.0.4"), mark §22 resolved, and add a §23 row.
  - **Otherwise:** stay on 8.0.16-security, note the blocking errors in §22, and add a §23 row "Stay on JUCE 8.0.16-security".
- [ ] **Step 5: Commit:** `spike: JUCE 9.0.4 compatibility of clap_juce_shim`.

### Task 12: Director checkpoints and M0 close-out

**Files:**
- Create: `docs/spikes/m0-ableton-midi-fx.md`, `docs/spikes/m0-host-matrix.md`
- Modify: `docs/spikes/m0-canvas-budget.md`, INTENT §20 (mark M0 done), §22 (resolve the Ableton and canvas items), §23 (rows), `CLAUDE.md` ("Build and test": the real preset commands, `gg-render` usage, the validator script, the sanitizer presets), `README.md`

- [ ] **Step 1: [Director] Host load test.** With `GG_COPY_PLUGIN_AFTER_BUILD=ON` and a release build installed, the director:
  - loads both plugins in **Ableton Live 12** (VST3 and AU) and **Bitwig Studio** (CLAP and VST3);
  - opens and closes each editor three times;
  - saves, closes and reopens a project.

  Record a pass/fail per host, format and plugin in `docs/spikes/m0-host-matrix.md`. Expected: all pass, which is the M0 exit criterion.
- [ ] **Step 2: [Director] Ableton MIDI routing.** In Ableton, put Giga Grains FX (VST3, then AU) on an audio track and route a MIDI track to it. Does the editor's note-on counter increase? Record the routing steps and results in `docs/spikes/m0-ableton-midi-fx.md`, resolve that §22 item, and add a §23 row stating how prominent MIDI-driven FX features should be.
- [ ] **Step 3: [Director] Canvas stress in the hosts.** Read the editor's "frame ms" at N = 8192 in Ableton and Bitwig at the display's native scale. Record the readings in `docs/spikes/m0-canvas-budget.md`. Decide whether the GPU backend spike moves into M1 or stays in M2; record that in §22 and §23.
- [ ] **Step 4: [Director] Listening gate #0.** Give the director the four golden WAVs from `build/dev/golden/` with a short checklist: grains audible as a stream, no clicks, pitch shift correct in `dense-noise` (+7) and `sweep-stretch` (−12), the 44.1 k case matching the 48 k one. Record the verdict as a §23 row.
- [ ] **Step 5: Final docs.** Update `CLAUDE.md` "Build and test" and `README.md` with the exact commands from Tasks 1, 4, 6 and 8. Mark M0 complete in INTENT §20 only if Steps 1–4 passed.
- [ ] **Step 6: Final verification.** Run:
  - `ctest --preset dev --output-on-failure`
  - `ctest --preset asan`
  - `grep -nE 'TBD|TODO|FIXME' INTENT.md CLAUDE.md docs/spikes/*.md`

  Expected: the tests pass, grep finds nothing, and CI is green on the PR.
- [ ] **Step 7: Commit and push:** `docs: M0 close-out — host matrix, Ableton MIDI spike, canvas budget, listening gate`. Then hand off to `superpowers:finishing-a-development-branch` to merge the PR.
