# CLAUDE.md

Guidance for Claude Code sessions working in this repository.

## Start here

giga-grains builds two plugins from one shared C++20 grain engine:

- **Giga Grains**, a granular synthesizer;
- **Giga Grains FX**, a granular effect.

They ship as CLAP, VST3, AU v2 and standalone, for macOS, Windows and Linux. The license is
AGPL-3.0-or-later.

**Read [`INTENT.md`](INTENT.md) before doing anything.** It is the north star.

- **Part I (Intent)** changes only with the director's explicit sign-off.
- **Part II (Design direction)** may evolve. Every change to it adds a dated entry to the decision
  log (§23) in the same change.
- **When a task conflicts with INTENT.md, stop.** Propose an INTENT.md amendment; don't diverge
  silently.
- **The principles in §8 are the tie-breakers** when goals conflict.

Development is Claude-led. The director steers, listens and reviews. Ears are the scarce
resource, so machines must catch everything they can catch.

## Non-negotiables

- **Audio thread:**
  - no allocation, locks, I/O, syscalls or unbounded loops;
  - must be RealtimeSanitizer-clean;
  - must handle 0- and 1-sample blocks;
  - `params.flush` stays trivial.
- **Determinism:**
  - Randomness only from the engine's counter-based RNG, keyed by (seed, layer, stream, field,
    grain index).
  - No `std::` random distributions.
  - No behavior that adapts to measured CPU.
  - No render-mode-dependent behavior unless the user opts in with an explicit parameter.
  - Layers are summed in a fixed order.
  - Output must be bit-identical regardless of host block size.
- **Positions and phases are 64-bit** (int64 + fraction, or double). float32 is allowed only for
  offsets within a block.
- **Overload degrades deterministically:**
  1. lower the interpolation tier;
  2. then drop new grains;
  3. then steal grains with fades.

  It never clicks and never drops out.
- **Stability contracts** (frozen from the first public release):
  - CLAP parameter IDs, which are append-only;
  - AUv2 parameter order;
  - factory preset order;
  - plugin identities and AU codes.
- **Licensing:**
  - Only AGPLv3-compatible dependencies.
  - No GPLv2-only code and no unlicensed code.
  - Never use the names "Mutable Instruments", "Clouds" or "Beads" in the product.
  - Pin every dependency to an exact SHA or tag.

## Working style

- Every DSP change ships with:
  - unit tests;
  - updated `gg-render` golden renders, compared by metrics;
  - the determinism tests.
- Run the sanitizer builds (ASan/UBSan, RTSan) and the format validators before claiming work is
  done.
- When you report a result, include its evidence: test output, benchmark numbers, render
  statistics or spectrograms.
- Each milestone ends with a listening gate: render A/B files plus a short checklist for the
  director.
- Reference templates for the CLAP-first build, CI and installers, all three GitHub-hosted under
  surge-synthesizer. A session may read their local clones, when they're present:
  - shortcircuit-xt, https://github.com/surge-synthesizer/shortcircuit-xt (local clone
    `~/src/shortcircuit-xt`)
  - also surge, https://github.com/surge-synthesizer/surge (local clone `~/src/surge`), and
    OB-Xf, https://github.com/surge-synthesizer/OB-Xf (local clone `~/src/OB-Xf`).

## Build and test

**Setup.**
- You need CMake 3.22 or newer, and Ninja (Visual Studio 2022 with ClangCL on Windows).
- Fetch the dependencies with `git submodule update --init`.
- **In a linked worktree** (`git worktree add …`), run its `scripts/worktree-setup.sh
  [--configure dev,asan,rtsan]` instead.
  - It checks out every submodule recursively at the commits HEAD pins, taking the objects from
    the main checkout's submodule repositories, so it works offline. A commit the main checkout
    lacks is fetched from upstream, and the script says which.
  - It changes no URL in the shared `.git/config`, in the main checkout's submodule configs or in
    any other worktree's. The only `submodule.*.url` entries it writes are the nested
    submodules' canonical `.gitmodules` URLs, into the configs of the new worktree's own clones
    of their parents. Each new clone's own config also gets a `remote.origin.url`: its
    `.gitmodules` URL, as a clone from upstream would record.
  - Don't rewrite `submodule.*.url` to local paths to get a worktree going: every worktree shares
    that config.
  - It fails unless `git submodule status --recursive` then shows every submodule at its pinned
    commit. `--configure` runs `cmake --preset` for each preset named, and doesn't build.
- The first configure of a build tree that builds the plugin formats downloads the VST3 and
  AudioUnit SDKs (clap-wrapper fetches them), so it needs the network. `asan` and `rtsan` build
  no formats and download nothing.

**Presets.** They are defined in `CMakePresets.json`. Each one builds into `build/<preset>/`,
except `windows-clangcl`, which builds into `build/windows/`.

| Preset | What it builds |
|---|---|
| `dev` | Debug, with everything. The everyday build. |
| `release` | Release. Time benchmarks and run the validators on this one. |
| `asan` | ASan + UBSan, Debug, with `GG_BUILD_EDITOR=OFF`: the engine, `gg-core`, the UI core, the tools, the plugin core and the tests. No editor, JUCE or plugin formats. |
| `rtsan` | RealtimeSanitizer, RelWithDebInfo, built like `asan` (`GG_BUILD_EDITOR=OFF`). Needs upstream LLVM 20 or newer in `$GG_LLVM` (Apple clang has no RTSan). |
| `mac-universal` | Release for arm64 + x86_64. CI's macOS leg, which also runs the x86_64 slice's tests and golden checks under Rosetta. |
| `windows-clangcl`, `linux-gcc12` | CI's Windows and Linux legs. |

Every preset uses the same three commands:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

- **`GG_BUILD_EDITOR`** (default ON) builds the JUCE editor and clap-wrapper's CLAP, VST3, AUv2
  and standalone targets. With it OFF (and `GG_BUILD_PLUGINS` ON), the plugins build headless:
  - only `gg-plugin-core`, the JUCE-free audio side and CLAP factory, with no gui extension;
  - the `plugin` tests drive it through the factory, so `process` and `params.flush` run under
    the sanitizers;
  - the GUI test, the bundles and the `codesign_*`/`identity_*` tests need the editor, so they
    aren't built.
- **RTSan:** set `GG_LLVM` when configuring:
  - `GG_LLVM=/opt/homebrew/opt/llvm cmake --preset rtsan` with Homebrew LLVM on macOS;
  - CI uses LLVM's 20.1.8 release for Linux, checked against its SHA-256, in `/opt/llvm-20.1.8`.
  - Configuring fails, naming `GG_LLVM`, if the compiler isn't upstream Clang 20 or newer.
- **Outputs:**
  - The plugins and standalone apps land in `build/<preset>/gg_assets/`.
  - `cmake --build --preset <p> --target gg-products` gathers them into
    `build/<preset>/gg-products/` for validation.
  - CI builds `--target gg-all gg-products`.
- **If you delete a bundle or `gg_assets/`, configure again before building.** The `Info.plist`
  files of the CLAP, VST3 and app bundles are link dependencies, and CMake writes them at
  configure time.

**The pre-commit gate (`scripts/check.sh`).** One command configures, builds and tests each
preset, then checks the formatting:

```sh
scripts/check.sh [--presets dev,asan,rtsan] [-L label]... [--no-format] [--allow-install]
```

- **Per preset, in order** (`dev,asan,rtsan` by default):
  1. It configures only when the tree has no `CMakeCache.txt`. For `rtsan` on macOS with
     `GG_LLVM` unset, it uses `/opt/homebrew/opt/llvm` if that exists, and says so.
  2. It builds `gg-all` for `asan` and `rtsan`, as CI's sanitizer job does, and the default
     target otherwise.
  3. It runs `ctest --preset <p>`, passing every `-L` through. Repeated `-L` options must all
     match (see Tests below), so `-L core -L rt` narrows; `-L 'core|rt'` runs both areas.
- **A failed step** skips the rest of its preset; the other presets still run. `format.sh --check`
  runs last.
- **The output** is only a short summary:
  - one line per preset: the configure and build status, tests passed/total, skipped, wall time,
    and the failed step's log;
  - under it, each failing test (at most 10), or the first configure or build errors;
  - the format result, then `PASS` or `FAIL`.
- **The logs** are complete: `build/<preset>/check/{configure,build,test}.log` and
  `build/check/format.log`.
- **Exit status:** 0 only if every step passed; 1 if any failed; 2 for bad usage.
- **It refuses to build a tree whose cache sets `GG_COPY_PLUGIN_AFTER_BUILD` to anything CMake
  counts as true** (anything but empty, `OFF`, `0`, `NO`, `FALSE`, `N`, `IGNORE`, `NOTFOUND` or
  `*-NOTFOUND`, in any case), because that build installs plugins, unless you pass
  `--allow-install`.
- Paste its summary as the evidence in a report.

**Tests.** `gg-tests` is the Catch2 suite, and each Catch2 tag becomes a CTest label. To run one
area, use `ctest --preset dev -L <label>`. `-L` takes a regular expression, and repeated `-L`
options must all match: `-L core -L rt` runs only the tests labelled both, and `-L 'core|rt'`
runs the tests labelled either. The labels are:
- engine: `engine`, `cloud` (the Cloud scheduler), `live` (the live ring), `pool`, `source`,
  `rng`, `math`, `interp`, `window`, `denormal`;
- `core`: `gg-core`'s patch and its state, the parameter mapping and the `Processor`;
- `determinism`: block-size invariance;
- `rt`: the allocation tripwire. In the `rtsan` build, two tests labelled both `rt` and `rtsan`
  check that RTSan is active: one checks the build flag, and `rtsan_negative_control` passes
  only if RTSan reports an allocation made inside a nonblocking function.
- tools: `render`, `metrics`, `bench`;
- `golden`: the golden renders;
- `ui`: the splat renderer;
- `smoke`;
- `plugin`: the plugins, driven through their CLAP factory. Tests labelled both `plugin` and
  `rt` put the allocation tripwire on `process` and `params.flush`. On macOS this label also
  covers checks of the built bundles (with the editor only):
  - `codesign_*`: strict signature checks, which Ableton Live's scanner needs to pass;
  - `identity_*`: the frozen identities (INTENT §18), read back from the bundles: both
    standalone bundle IDs, the AU's three components and the VST3 class IDs. Run them before
    accepting any clap-wrapper update.

**Tests run in parallel.** Every test preset runs 8 tests at a time (`"jobs"` in the hidden
`base` test preset); `-j N` on the command line overrides it, and `-j 1` runs them serially.
- Each Catch2 test case runs in its own process.
- **Keep new tests parallel-safe:**
  - never write a path that another test writes: unit tests' scratch files go in
    `GG_TEST_OUTPUT_DIR` under a name of their own, and each golden case writes its own files;
  - a test that has to share a file, or that measures time, must not run beside the others. For
    an `add_test` in `tests/CMakeLists.txt`, set its `RESOURCE_LOCK` or `RUN_SERIAL` property.
    Catch2 cases are discovered after the build, so `set_tests_properties` can't reach them.
    Tag the case (`[serial]`, say), discover it separately, and leave it out of the main
    discovery:

    ```cmake
    catch_discover_tests(gg-tests TEST_SPEC "~[serial]" ADD_TAGS_AS_LABELS)
    catch_discover_tests(gg-tests TEST_SPEC "[serial]" ADD_TAGS_AS_LABELS
            PROPERTIES RUN_SERIAL TRUE)
    ```

**Golden renders (`gg-render`).** `gg-render` renders a render spec offline and deterministically:

```
gg-render <spec.json> [-o out.wav] [--metrics out.json] [--events out.jsonl]
          [--spectrogram out.png] [--compare golden.metrics.json]
          [--compare-events golden.events.jsonl] [--block-size N]
```

- It's built at `build/<preset>/tools/gg-render/gg-render`.
- **Two modes** (the spec format is documented in `tools/gg-render/RenderSpec.h`). Every spec has
  `sampleRate`, `durationSeconds`, `blockSize` and `seed`; any unknown key is an error.
  - **Sample mode** (`"mode": "sample"`, the default) drives the engine's Cloud stream over a
    `source` (`sine`, `sweep`, `noise` or `file`) with a `stream` (`grainRateHz`,
    `grainDurationMs`, `position`, `positionJitter`, `pitchSemitones`, `gain`, and optional
    `distribution`), plus a release tail. The four M0 cases use it.
  - **FX mode** (`"mode": "fx"`) runs Giga Grains FX's `gg-core` `Processor`, the one the plugin
    runs, over an `input`: a built-in `sine`, `sweep` or `noise` (no files; a WAV-file input
    waits for sub-project 3's file loader, ruling M1-aa). The top-level `channels` key (1 or 2)
    sets the input's channel count. Optional `params` sets base values by parameter name
    (`"Size"`, `"Density"`, …), and optional `automation` is a time-ordered list of
    `{"time", "param", "value"}` points in seconds. Values are in each parameter's CLAP value
    domain (Density 6.0 is 64 Hz). The render is exactly `durationSeconds` long.
  - **FX inputs are faded** in at their start and out at their end over 10 ms, with a raised
    cosine (sin²), so the input's own edges don't click (ruling M1-v). Sample-mode sources and
    the M0 goldens are untouched.
- Its exit status is 0 for a pass, 1 when a comparison fails, and 2 for bad input.
- **The cases:** each case's spec is in `tests/golden/cases/`, and its expected metrics and event
  log are in `tests/golden/expected/`. Every `*.json` there is a case; a new one gets a test the
  next time CMake runs.
- **The `golden` tests** render every case into `build/<preset>/golden/<case>.wav` and `.png`.
  - They compare the metrics within tolerances.
  - They compare the grain-event log exactly per line (CR and final newline ignored). That
    comparison is the cross-platform determinism gate.
  - `golden_tamper_events` and `golden_tamper_metrics` check that the gate can fail: each
    compares a render with a golden copy that has one value changed, and passes only on
    gg-render's failure message.
- **After a DSP change, run `scripts/update-goldens.sh [build-dir]`** (default `build/dev`; a
  relative path is taken from the repository root).
  - It rebuilds `gg-render` and rewrites the expected files.
  - It leaves WAVs and spectrograms in `<build-dir>/golden/`.
  - It then checks the new goldens.
- Look at every spectrogram before committing new goldens, and say in the commit message why they
  changed.

**Benchmarks (`gg-bench`).** Time these from `release`, on an otherwise idle machine:

```sh
cmake --build --preset release --target gg-bench
build/release/tools/gg-bench/gg-bench [--quick] [--json out.json]            # grain kernels (§16)
build/release/tools/gg-bench/gg-bench --canvas [--quick] [--json out.json]   # canvas renderer
```

- **Run lengths:**
  - kernels: a full run is a median of 5 runs of 2 s each, about 2.6 minutes on an M5 Max;
    `--quick` uses runs of 0.2 s;
  - `--canvas`: 240 timed frames after a 20-frame warm-up, or 30 with `--quick`.
- **In CI:** each build leg runs the full length and uploads `gg-bench-<preset>`. In M0 the
  results are reported, not gated. M1 starts a regression gate at the measured baseline, and the
  §16 headline becomes the gate once M1 meets it (INTENT §16, §19).

**Validators.** CI runs them on every build leg; auval runs on macOS only.

- **Fetch the validators.** `scripts/fetch-validators.sh tools-bin` fetches clap-validator 0.4.1
  and pluginval v1.0.4 into `tools-bin/`, which git ignores. Each download is checked against a
  pinned SHA-256.
- **Run them against the products** (macOS shown):

  ```sh
  cmake --build --preset release --target gg-products
  p=build/release/gg-products
  tools-bin/clap-validator validate "$p/Giga Grains.clap"
  tools-bin/pluginval --strictness-level 10 --validate "$p/Giga Grains.vst3"
  ```

- **On Linux,** CI runs pluginval under `xvfb-run -a` with `--skip-gui-tests`.
- **auval (macOS)** needs the AU installed for your user:

  ```sh
  rm -rf ~/Library/Audio/Plug-Ins/Components/"Giga Grains.component"
  cp -R "$p/Giga Grains.component" ~/Library/Audio/Plug-Ins/Components/
  killall -9 AudioComponentRegistrar
  # Wait up to 30 s for the registrar to list the new component, as CI does.
  for _ in $(seq 1 15); do
    auval -a > "$TMPDIR/auval-list.txt" 2>&1
    grep -q Sbcx "$TMPDIR/auval-list.txt" && break
    sleep 2
  done
  auval -v aumu GGin Sbcx; auval -v aufx GGfx Sbcx; auval -v aumf GGfx Sbcx
  ```

  Each of the three runs must end with `AU VALIDATION SUCCEEDED`.
- **Steinberg's VST3 validator** is built from the VST3 SDK that clap-wrapper downloaded:

  ```sh
  sdk="$(sed -n 's/^CPM_PACKAGE_vst3sdk_SOURCE_DIR:INTERNAL=//p' build/release/CMakeCache.txt)"
  cmake -S "$sdk" -B build/release/vst3-validator -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
    -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF -DSMTG_CREATE_PLUGIN_LINK=OFF \
    -DSMTG_BUILD_UNIVERSAL_BINARY=OFF
  cmake --build build/release/vst3-validator --target validator
  build/release/vst3-validator/bin/Release/validator "$p/Giga Grains.vst3"
  ```

**Formatting.**
- Run `scripts/format.sh` to format every first-party source in place.
- `scripts/format.sh --check` changes nothing and fails if any file isn't formatted. CI's Format
  workflow runs it.
- It needs clang-format 23. It uses `$CLANG_FORMAT` if that's set, and otherwise Homebrew LLVM's
  copy, `clang-format-23` or `clang-format`.

**Installing and signing the plugins.**
- **`-DGG_COPY_PLUGIN_AFTER_BUILD=ON`** (off by default) copies the plugins into your user plugin
  folders each time a bundle is rebuilt:
  - macOS: the CLAP, VST3 and AU, into `~/Library/Audio/Plug-Ins/`;
  - Linux: the CLAP and VST3, into `~/.clap` and `~/.vst3`;
  - Windows: nothing.
- **Check a tree's `CMakeCache.txt` before building one you didn't configure:** with this option
  on, a build installs plugins.
- **On macOS every bundle is signed** after it's built, before it's copied, with
  `GG_MACOS_CODESIGN_IDENTITY`.
  - It defaults to `-`, which signs ad hoc.
  - Pass a Developer ID to sign with a real identity. A release build needs more than that:
    hardened runtime, a timestamp and notarization.
  - Strict hosts such as Ableton Live won't load a bundle that carries only the linker's
    signature (`docs/spikes/m0-host-matrix.md`).
