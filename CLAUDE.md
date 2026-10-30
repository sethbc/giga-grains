# CLAUDE.md

Guidance for Claude Code sessions working in this repository.

## Start here

giga-grains builds two plugins from one shared C++23 grain engine:

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
- **Where we are** (INTENT §20, §23 #61, #64): M0 and M1 are done, and so are the installers and
  release pipeline (`docs/superpowers/specs/2026-10-29-release-pipeline-design.md`; the runbook
  is `docs/releasing.md`). **M2 is in progress** as seven sub-projects in four waves, run by one
  agent session each (`docs/superpowers/specs/2026-10-29-m2-programme-design.md`: read its §6
  for your sub-project's scope and what it owns, and its §7 for the rules every sub-project
  builds on). Sub-project 1, Layers and structure, is built (INTENT §23 #65): four layers, the
  morph, the Source kind, the mod matrix, render tasks and the patch at version 2, which wave 2
  builds on. M2, M3 and M4 are tagged 0.2.0, 0.3.0 and 0.4.0 as
  unpublished draft releases; nothing is public and there are no betas before M5, whose
  `0.5.0-beta.N` are the first public releases (§23 #62); v1.0.0 follows M5.
- **The records:** each sub-project's spec in `docs/superpowers/specs/`, the published benchmark
  JSON in `docs/benchmarks/`, the listening packs in `docs/listening/`, the M0 and M1 spikes in
  `docs/spikes/`, the release notes in `docs/releases/`, and the carry-over in
  `docs/m2-carry-over.md` (M1's, frozen as its record, is `docs/m1-carry-over.md`).

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
- **Stability contracts** (INTENT §18, §23 #61, #62). Frozen from the first public release,
  `v0.5.0-beta.1` (M5):
  - CLAP parameter IDs, which are append-only;
  - the AUv2 parameter order, which is ascending ID order and so follows from them;
  - plugin identities and AU codes.

  Frozen from 1.0.0: factory preset order.
- **Licensing:**
  - Only AGPLv3-compatible dependencies.
  - No GPLv2-only code and no unlicensed code.
  - Never use the names "Mutable Instruments", "Clouds" or "Beads" in the product.
  - Pin every dependency to an exact SHA or tag.
  - ASIO® (the Windows standalone only; a registered trademark of Steinberg Media Technologies
    GmbH) comes from sst-plugininfra's ASIO SDK 2.3.4: exactly `common/asio.cpp` (used under the
    SDK's GPLv3 option), `host/asiodrivers.cpp` and `host/pc/asiolist.cpp` (BSD-3-Clause-style,
    under the license each carries; INTENT §15), unmodified, compiled into RtAudio by
    `libs/CMakeLists.txt`. Never compile RtAudio's bundled ASIO files, which have
    no license grant, or put its `include/` on a path. The Windows configure fails if either
    happens, or if any of those SDK files' content changes (INTENT §23 #30, #56). Use no ASIO
    logo. `THIRD_PARTY_NOTICES.md` holds the notices a distributed Windows build carries.

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
- **Deferred items** (`docs/m2-carry-over.md`) are handled as superpowers' subagent-driven development
  handles them (the director, 2026-10-30, retiring the 2026-10-28 carry-over policy): a reviewer's
  minor is fixed or deferred at the controller's judgment; deferred minors and items blocked outside
  our control (upstream, the director's ears, a later feature) are listed in the sub-project's final
  report and recorded in that file as a plain log, with no count rules. A row stays until the thing
  it names is done; nothing is filed upstream.
- Reference templates for the CLAP-first build, CI and installers, all three GitHub-hosted under
  surge-synthesizer. A session may read their local clones, when they're present:
  - shortcircuit-xt, https://github.com/surge-synthesizer/shortcircuit-xt (local clone
    `~/src/shortcircuit-xt`)
  - also surge, https://github.com/surge-synthesizer/surge (local clone `~/src/surge`), and
    OB-Xf, https://github.com/surge-synthesizer/OB-Xf (local clone `~/src/OB-Xf`);
  - for the installers, sst-plugininfra's `scripts/installer_mac/make_installer.sh` (pinned in
    `libs/sst/sst-plugininfra`) and sst-cmake's `innosetup/installer.iss` (MIT,
    https://github.com/surge-synthesizer/sst-cmake; shortcircuit-xt vendors it under
    `libs/sst/sst-cmake`).

## Build and test

**Setup.**
- You need CMake 3.22 or newer, and Ninja (Visual Studio 2022 with ClangCL on Windows).
- Fetch the dependencies with `git submodule update --init --recursive` (what CI and
  `worktree-setup.sh` check out; today no nested submodule is built).
- **In a linked worktree** (`git worktree add …`), run its `scripts/worktree-setup.sh
  [--configure dev,asan,rtsan]` instead. Its `--configure` applies the same Homebrew-LLVM
  default for `rtsan` as `check.sh` does.
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
    commit. `--configure` runs `cmake --preset` for each preset named, and doesn't build. A name
    that isn't a configure preset fails before anything is cloned.
- The first configure of a build tree that builds the plugin formats downloads the VST3 and
  AudioUnit SDKs (clap-wrapper fetches them), and RtAudio for the standalone on Windows (its
  ASIO) and on Linux without the PipeWire and JACK headers, so it needs the network. (CI
  fetches them itself, with `scripts/fetch-sdks.sh <dir>`, which prints the `-DVST3_SDK_ROOT=...`
  style options that make clap-wrapper use those checkouts; a local configure needs none.)
  `asan` and `rtsan` build no formats and download nothing.

**Warnings.** Every first-party target builds with `-Wall -Wextra` and `-Werror` (on ClangCL
`/clang:-Wall /clang:-Wextra /WX`), through `gg::compiler-options`. Third-party code never links
it, and its headers are SYSTEM includes (`gg_mark_system_includes` in `libs/CMakeLists.txt`), so
fix a warning in first-party code and never edit a submodule. A new target links
`gg::compiler-options`, with one exception: `clients/clap-first/gg-clap-entry.cpp`, which
clap-wrapper's own targets compile without it. Never define a `*_IMPLEMENTATION` macro
(`STB_IMAGE_IMPLEMENTATION`, `DR_WAV_IMPLEMENTATION`, …) in a first-party file: the
implementation belongs in `libs/thirdparty-impl/`, built without `gg::compiler-options`, or the
library's own warnings fail the build. Only the Linux leg (GCC 13, Release, so `-O3`) shows
`-Wmaybe-uninitialized`, so a green macOS build proves nothing about it. Clang versions differ in
what `-Wextra` flags (the rtsan build's LLVM is newer than Apple's), so build `asan` and `rtsan`
too. `cmake/AsioGuard.cmake` fails the configure if anything but RtAudio's ASIO backend uses
`libs/.../asiosdk` (tests `asio_guard_*`).

**GCC 13's own diagnostics.** M2 sub-project 1 paid four CI rounds for these; each has a fix:
- `-Warray-bounds` on a `Count` sentinel indexing an array, even behind a check: index through a
  bounded accessor (`ParamMap.cpp`'s `read`, which clamps the index);
- `-Wmaybe-uninitialized` on a `std::optional` built in a ternary: use a plain value with a
  sentinel;
- `-Wrange-loop-construct` on a by-value structured binding in a range-for: bind `const auto &`;
- `-Wextra`'s conditional with an enumerator in one arm and an integer in the other: give both
  arms one integer type.

Homebrew's `gcc@13` on arm64 macOS fails in its own `arm_acle.h`, so it can't serve as a local
check. CI's Linux leg is the only GCC build: push early (as a draft pull request).

**The Linux products' libstdc++.** On Linux with GCC, outside the sanitizer builds, the four
products link libstdc++ and libgcc statically with their symbols kept local, and the CLAP and
VST3 export their entry points alone (`clients/clap-first/CMakeLists.txt`,
`linux-exports.map`; INTENT §15). CI's Linux leg checks it after the build: "Linux products need
no libstdc++ newer than Ubuntu 22.04's" fails on a `GLIBCXX` above 3.4.30, a `CXXABI` above
1.3.13 or an exported `std::` symbol, and lists each product's symbol versions in the step
summary.

**CI caches** (`.github/workflows/ci.yml`; none changes what is built):
- **The SDK checkouts** (VST3 SDK, AudioUnitSDK, RtAudio, RtMidi, wil): `scripts/fetch-sdks.sh`
  clones each at the tag clap-wrapper's `cmake/base_sdks.cmake` pins into `.sdk-cache`, and
  Configure passes `-DVST3_SDK_ROOT=...` and the rest, so clap-wrapper downloads nothing. The
  cache is keyed by leg and by the hash of `base_sdks.cmake` and the script. (CPM's own
  `CPM_SOURCE_CACHE` cached nothing: clap-wrapper gives every package a `SOURCE_DIR`, which makes
  CPM skip it.) The VST3-validator step builds from `VST3_SDK_ROOT`.
- **ccache**, macOS and Linux legs and the sanitizer job only, passed as
  `CMAKE_<LANG>_COMPILER_LAUNCHER` at CI configure (local presets are unchanged). Windows is
  uncached (the Visual Studio generator has no launcher support), and `mac-universal`'s
  two-architecture compiles are largely uncached. Keyed by OS, preset, runner image and commit
  (the sanitizer job's by the LLVM version and commit), restored by prefix.
- **The pinned LLVM archive** (sanitizer job), keyed by version and SHA-256. The SHA-256 is checked
  on a hit too.
- **Scoping.** Every run restores all of them; only runs on `main` (a push, the nightly, a
  dispatch on `main`) save them, also after a failed build. GitHub scopes a cache to the ref that
  saved it: a pull request reads `main`'s caches, but nobody reads a pull request's, so PRs never
  save. GitHub evicts a cache unused for 7 days and the least recently used ones past the
  repository's 10 GB; the nightly keeps them alive. After a change that alters a cache key (a pin,
  the LLVM version), merge it and run `gh workflow run ci.yml --ref main` once to seed the caches.

**Presets.** They are defined in `CMakePresets.json`. Each one builds into `build/<preset>/`,
except `windows-clangcl`, which builds into `build/windows/`.

| Preset | What it builds |
|---|---|
| `dev` | Debug, with everything. The everyday build. |
| `release` | Release. Time benchmarks and run the validators on this one. |
| `asan` | ASan + UBSan, Debug, with `GG_BUILD_EDITOR=OFF`: the engine, `gg-core`, `gg-media`, the UI core, the tools, the plugin core and the tests. No editor, JUCE or plugin formats. |
| `rtsan` | RealtimeSanitizer, RelWithDebInfo, built like `asan` (`GG_BUILD_EDITOR=OFF`). Needs upstream LLVM 20 or newer in `$GG_LLVM` (Apple clang has no RTSan). |
| `mac-universal` | Release for arm64 + x86_64. CI's macOS leg, which also runs the x86_64 slice's tests and golden checks under Rosetta. |
| `windows-clangcl`, `linux-gcc13` | CI's Windows and Linux legs. |

Every preset uses the same three commands:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

- **The top-level options** (`CMakeLists.txt`): `GG_BUILD_PLUGINS` (default ON; the plugin core
  and, with the editor, the formats), `GG_BUILD_EDITOR`, `GG_SANITIZE` (ASan + UBSan; the `asan`
  preset) and `GG_USE_RTSAN` (the `rtsan` preset).
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
  - `cmake --build --preset <p> --target gg-installer` builds `gg-products`, then packs it into
    `build/<preset>/gg-installer/`: the macOS PKG and DMG, the Windows zip and, when Inno Setup
    6's `ISCC.exe` is found at configure, its installer, or the Linux tarball. Unsigned unless
    the signing environment is set ("Releasing" below; `docs/releasing.md`).
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
- **The logs** are complete: `<build tree>/check/{configure,build,test}.log` and
  `build/check/format.log`, which `--no-format` deletes. The build tree is the preset's
  `binaryDir`, read from `CMakePresets.json` and `CMakeUserPresets.json` by
  `scripts/preset-dir.cmake`; a name that isn't a configure preset gets a `FAILED` line.
- **Exit status:** 0 only if every step passed; 1 if any failed; 2 for bad usage.
- **It refuses to build a tree whose cache sets `GG_COPY_PLUGIN_AFTER_BUILD` to anything CMake
  counts as true** (anything but empty, `0`, `OFF`, `NO`, `FALSE`, `N` or `IGNORE` in any case,
  or `NOTFOUND` or `*-NOTFOUND` in upper case: CMake's NOTFOUND match is case-sensitive, so
  `notfound` counts as true), because that build installs plugins, unless you pass
  `--allow-install`.
- **Where it runs:** `check.sh` and `worktree-setup.sh` run in bash 3.2 or newer on macOS and
  Linux, with CTest 3.22 or newer (the `scripts` test replays CTest 3.31's and 4.4's real
  output). Windows Git Bash is unsupported: nothing has run them there.
- Paste its summary as the evidence in a report.
- **One gate at a time per machine.** Two gates in two worktrees at once fail falsely:
  `scripts_release`'s keychain check (the keychain search list is per user) and
  `rtsan_negative_control`'s `atos`, which can hang. Rerun a failure of either alone before
  believing it.

**Tests.** `gg-tests` is the Catch2 suite, and each Catch2 tag becomes a CTest label. To run one
area, use `ctest --preset dev -L <label>`. `-L` takes a regular expression, and repeated `-L`
options must all match: `-L core -L rt` runs only the tests labelled both, and `-L 'core|rt'`
runs the tests labelled either. The labels are:
- engine: `engine`, `cloud` (the Cloud scheduler), `live` (the live ring), `pool`, `source`,
  `rng`, `math`, `interp`, `window`, `denormal`;
- `core`: `gg-core`'s patch (with its `<mod>` routes and the migration from version 1), the
  session state container (`GGST`) and the settings' XML, the parameter mapping, and the
  `Processor` with its layers, notes, source hand-off, Feedback and render tasks. `slow` marks
  one of them, Feedback 0.95 for 60 s (over a minute in `dev`);
  - `layers` (M2 sub-project 1): the four layers, in the engine (the layer's RNG key, the enable
    gate, the kind switch, the spreads) and in the `Processor` (the sum in order, the morph law,
    on and off, the kinds, the instrument's input into the rings, the event order);
  - `mod`: the mod matrix: its routes, curves and factory routes (M1's `macroOffsets` bit for
    bit), the modulation sources, and the `Processor`'s per-cell application;
  - `threads`: `TaskRunner`, `SerialRunner` and the tests' `StubPool` (`engine`), and the
    `Processor`'s render tasks (`core`): the same bytes with no pool and pools of 1, 2 and 8
    workers, a declining pool, and the tasks' denormal mode and allocations;
  - `publish`: the audio-to-UI structures (the grain queue, the UI snapshot, the live peaks) and
    the `Processor`'s publishing;
- `media`: `gg-media`, the JUCE-free sample-file library (`media/`) that the plugin's loader and
  `gg-render` share: decoding, rate conversion, SHA-256, loading a sample, the search for a
  missing one and the settings file. `slow` marks one of them, the search's 50,000-entry scan.
  - The WAV, AIFF and AIFC files they decode are written at run time
    (`tests/media/AudioFixtures.h`). dr_libs can't write FLAC or MP3, so
    `tests/fixtures/audio/` holds one tiny FLAC and one MP3, the only audio committed to the
    repository. Its `README.md` gives their SHA-256s and the commands that made them. Don't
    commit any other audio: generate it.
- `space`: Space (M1 sub-project 4): the diffuser, the patched Reverb 2 and `SpaceReverb`, the
  `Processor`'s Space chain, and the plugins' tail and "Space" remote-controls page. `slow` also
  marks four of them: the hour-long renders at infinite Decay (about 50 s each in `dev`, 2.5
  minutes under ASan), the block-size invariance render and the 60 s render at extreme settings
  (about 8 s and 13 s in `dev`); `-LE slow` leaves all six slow tests out (media's one, these
  four and core's Feedback one).
- `kernel`: the renderers' scalar references and the 4-wide SIMD sample and live kernels, which
  must match them bit for bit (`std::memcmp`);
- `determinism`: block-size invariance;
- `rt`: the allocation tripwire. In the `rtsan` build, two tests labelled both `rt` and `rtsan`
  check that RTSan is active: one checks the build flag, and `rtsan_negative_control` passes
  only if RTSan reports an allocation made inside a nonblocking function.
- tools: `render` (gg-render's CLI, `render_write_media_alone` and
  `render_spectrogram_utf8_path` among them), `metrics`, `bench` (gg-bench's command line: the
  `bench_rejects_*`, `bench_make_baseline_*`, `bench_baseline_*` and `bench_space_cli` tests);
- `golden`: the golden renders;
- `ui`: the splat renderer, the lane geometry (`LaneModel`), and the lane's clock and live-grain
  list (`LaneState`);
- `smoke`;
- `build`: checks of the build system itself: the `asio_guard_*` tests, run on scratch CMake
  projects (`cmake/AsioGuard.cmake`), and `gg-tests`' "the build is C++23" (also `smoke`);
- `scripts`: `check.sh` and `worktree-setup.sh`, in scratch trees with stub `cmake` and `ctest`
  and in scratch git repositories, and the release scripts in `scripts/release/`
  (`tests/scripts/release_scripts_tests.sh`: the tag check, the source tarball and the signing
  keychain, whose checks put a scratch keychain in your search list for a moment and restore
  it) (not on Windows);
- `installer` (with the editor): `installer_build_products` builds `gg-products` as the
  `gg_products` fixture the others require, so `ctest -L installer` needs no build step of its
  own (that test runs serially). Then the license set (`installer_license_set`); on macOS the
  unsigned PKG and DMG (`installer_macos_pkg`, and its refusals `_missing_product` and
  `_partial_env`), each bundle's numeric version (`version_bundle_*`) and, when a real identity
  is configured, the hardened runtime and the apps' entitlement (`codesign_hardened_*`); the
  Linux tarball (`installer_linux_*`); the Windows zip and, where `ISCC.exe` was found, the Inno
  Setup compile (`installer_windows_*`);
- `serial`: the Catch2 cases tagged `[serial]`, which measure time or memory and run with
  `RUN_SERIAL` (see "Tests run in parallel");
- `plugin`: the plugins, driven through their CLAP factory. Tests labelled both `plugin` and
  `rt` put the allocation tripwire on `process` and `params.flush`. On macOS this label also
  covers checks of the built bundles (with the editor only):
  - `codesign_*`: strict signature checks, which Ableton Live's scanner needs to pass;
  - `identity_*`: the frozen identities (INTENT §18), read back from the bundles: both
    standalone bundle IDs, the AU's three components and the VST3 class IDs. Run them before
    accepting any clap-wrapper update.
  - `gg-au-state-tests` (labels `plugin` and `au`): loads the built `.component` into its own
    process (registered with `AudioComponentRegister`; nothing is installed) and checks that
    ClassInfo carries the CLAP state (GGST) and round-trips it for all three AU identities. It
    also pins clap-wrapper's known AU state faults (T7 223). Run it (`ctest --preset dev -L au`)
    before accepting any clap-wrapper update too.
    - It runs on the native slice only: on CI's mac-universal leg it loads the AU natively
      (arm64), never under Rosetta like the x86_64 slice's `gg-tests`. Keep it so when the macOS
      leg changes, or the AU state goes unchecked there.
    - Building `gg-tests` builds the AU bundle too (`add_dependencies(gg-tests
      gg-au-state-tests)`), so a tests-only build takes longer.

**CI's mac-universal leg must keep running `gg-tests` natively (arm64)** as well as the x86_64
slice under Rosetta. The fp-contract tests (`fp_contract_fast.cpp`'s canary and the kernel cases)
can fail only on arm64 Clang; Linux, Windows and the x86_64 slice can't catch a fused
multiply-add. Keep that step whenever the macOS leg changes.

**Test case names are ASCII only** (tags too). CTest passes a name to `gg-tests` on its command
line, and on Windows a non-ASCII character there reaches Catch2's narrow `main` through the ANSI
code page, so the name matches nothing and the test fails with "No tests ran". Spell `×` as `x`,
`−` as `-`, and so on; comments and strings inside a test may use any UTF-8.

**Tests run in parallel.** Every test preset runs 8 tests at a time (`"jobs"` in the hidden
`base` test preset); `-j N` on the command line overrides it, and `-j 1` runs them serially.
- Each Catch2 test case runs in its own process.
- **Keep new tests parallel-safe:**
  - never write a path that another test writes: unit tests' scratch files go in
    `GG_TEST_OUTPUT_DIR` under a name of their own, and each golden case writes its own files;
  - never touch the user's per-machine settings file. The instrument (the FX reads none) reads
    the file `GG_SETTINGS_FILE` names, when it is set and not empty, instead of the user's;
    `tests/plugin/PluginHarness.h` points it at a scratch file, and a test that reads the
    settings points it at its own (`useSettingsFile`) before it creates the instance;
  - a test that has to share a file, or that measures time, must not run beside the others. For
    an `add_test` in `tests/CMakeLists.txt`, set its `RESOURCE_LOCK` or `RUN_SERIAL` property.
    Catch2 cases are discovered after the build, so `set_tests_properties` can't reach them.
    Tag the case `[serial]`: `tests/CMakeLists.txt` discovers those cases separately and leaves
    them out of the main discovery:

    ```cmake
    catch_discover_tests(gg-tests TEST_SPEC "~[serial]" ADD_TAGS_AS_LABELS)
    catch_discover_tests(gg-tests TEST_SPEC "[serial]" ADD_TAGS_AS_LABELS
            PROPERTIES RUN_SERIAL TRUE)
    ```

**Golden renders (`gg-render`).** `gg-render` renders a render spec offline and deterministically:

```
gg-render <spec.json> [-o out.wav] [--metrics out.json] [--events out.jsonl]
          [--spectrogram out.png] [--compare golden.metrics.json]
          [--compare-events golden.events.jsonl] [--block-size N] [--media-dir DIR]
gg-render --write-media DIR
```

- It's built at `build/<preset>/tools/gg-render/gg-render`.
- **Three modes** (the spec format is documented in `tools/gg-render/RenderSpec.h`). Every spec has
  `sampleRate`, `durationSeconds`, `blockSize` and `seed`; any unknown key is an error.
  - **Sample mode** (`"mode": "sample"`, the default) drives the engine's Cloud stream over a
    `source` (`sine`, `sweep`, `noise` or `file`) with a `stream` (`grainRateHz`,
    `grainDurationMs`, `position`, `positionJitter`, `pitchSemitones`, `gain`, and optional
    `distribution` and `quality`), plus a release tail. `quality` is the grains' interpolation
    tier, `"normal"` (Hermite, the default) or `"eco"` (linear). The four M0 cases and
    `dense-noise-eco` use sample mode.
  - **FX mode** (`"mode": "fx"`) runs Giga Grains FX's `gg-core` `Processor`, the one the plugin
    runs, over an `input`: a built-in `sine`, `sweep` or `noise`, or a `file` loaded and
    converted to the render's rate with `gg-media` (ruling M1-aa). The top-level `channels` key
    (1 or 2) sets a built-in input's channel count; a file keeps its own, and takes no
    `channels`. Optional `params` sets base values by parameter name, the name the plugin
    displays: a layer parameter's carries its layer's letter (`"A Size"`, `"B Size"`, …,
    `"D Step 16"`; M2 sub-project 1's spec §6.9), and the bare M1 names of layer A's eleven
    (`"Position"`, `"Size"`, `"Density"`, `"Distribution"`, `"Pitch"`, `"Spray"`, `"Stereo"`,
    `"Freeze"`, `"Texture"`, `"Feedback"`, `"Diffusion"`) mean layer A's, so every M1 spec
    still parses. The globals keep their names: Space's are `"Reverb Amount"`,
    `"Reverb Decay"`, `"Reverb Size"`, `"Reverb Damping"`, `"Reverb Pre-delay"`,
    `"Reverb Modulation"` and `"Dry to Reverb"`; the macros are `"Macro Size"`,
    `"Macro Density"`, `"Macro Pitch"`, `"Macro Spray"`, `"Macro Texture"`, `"Macro Freeze"`,
    `"Macro Feedback"` (Macro 7 in both products) and `"Macro Mix"`; the layers' are
    `"Morph X"`, `"Morph Y"`, `"Layer A On"` … `"Layer D On"` and `"Level B"` … `"Level D"`.
    Optional `automation` is a time-ordered list of `{"time", "param", "value"}` points in
    seconds, and optional `notes` a time-ordered list of `{"time", "key", "duration",
    "channel", "velocity"}` (channel optional; velocity optional, 0–127, default 100, reaching
    only the mod matrix's Velocity source). A note whose Off would fall on its On's frame is bad
    input (ruling R17). Values are in each parameter's CLAP value domain (Density 6.0 is 64 Hz;
    Reverb Decay is log2 seconds, so 1.0 is 2 s, and 6.068255709804611, `kDecayMax`, is ∞; Reverb
    Pre-delay is seconds, 0–0.5; Texture and the macros are 0–1, Macro Freeze 0 or 1, Feedback
    0–0.95). The render is exactly `durationSeconds` long.
  - **Layers, routes, controls and the transport** (M2 sub-project 1's spec §6.11), each
    optional in FX and instrument mode, and unknown keys in sample mode
    (`tools/gg-render/RenderSpec.h` documents each):
    - `samples` maps layer letters to source specs (`{"B": {"kind": "file", "path": …}}`),
      each handed to that layer before the render; the instrument's `sample` is layer A's, so
      `samples.A` beside it is an error, and an FX layer reads its sample only with its Source
      set to Sample (`"B Source": 0`).
    - `mod` is a list of routes `{"slot", "source", "sourceLayer", "target", "targetLayer",
      "depth", "curve"}` (slot 1–48, source and curve by their saved names, a target by its
      layer-A or global name with `targetLayer` `"A"`–`"D"` or `"all"`), set on top of the
      factory routes; a slot named twice is an error, and `"source": "None"` clears one.
    - `controls` is a time-ordered list of `{"time", "kind", "value", "channel"}`, kinds
      `modWheel`, `aftertouch` and `pitchBend`, values 0–1, sent as MIDI control events.
    - `transport` is `{"tempoBpm", "playing", "startBeat"}`, a steady transport set once before
      the render (playing by default, from beat 0); without it the transport sources hold at 0.
  - **Instrument mode** (`"mode": "instrument"`) runs Giga Grains' `Processor` the same way over
    a `sample` (a built-in signal or a `file`, loaded and converted with `gg-media`), with no
    `channels`. Its Position is the fraction of the sample, 0–1, and it is silent until a note.
    It takes Dry to Reverb, which does nothing there, and Feedback, which acts only on a layer
    whose Source is Live (an instrument spec has no input, so such a layer's ring holds
    silence). Its Macro 8 is
    `"Macro Level"` (Macro 7 is `"Macro Feedback"`, as in the FX; `"Macro Stereo"` names
    nothing since M2 sub-project 1): a spec takes only its own product's names, so
    `"Macro Level"` in an FX spec, or `"Macro Mix"` in an instrument spec, is an unknown key.
  - **FX inputs are faded** in at their start and out at their end over 10 ms, with a raised
    cosine (sin²), so the input's own edges don't click (ruling M1-v). Sample-mode sources and
    the M0 goldens are untouched.
- A relative file path is taken from the spec's directory, or from `--media-dir`. The golden
  cases' media are generated, never committed: `--write-media DIR` writes them, and the
  `golden_media` test does so into `build/<preset>/golden-media` as a fixture every golden test
  requires.
- Its exit status is 0 for a pass, 1 when a comparison fails, 2 for bad input and 3 for an
  internal error.
- **The cases:** each case's spec is in `tests/golden/cases/`, and its expected metrics and event
  log are in `tests/golden/expected/`. Every `*.json` there is a case; a new one gets a test the
  next time CMake runs. There are 36.
  - **M0's four,** in sample mode: `sparse-sine` (8 grains a second over a 220 Hz sine),
    `sparse-sine-44k1` (the same source converted to 44.1 kHz), `dense-noise` (400 grains a
    second over noise, up a fifth) and `sweep-stretch` (long grains over a sweep, down an
    octave); `dense-noise-eco` (sub-project 2) is `dense-noise` at the Eco tier.
  - **Sub-project 1's five,** the first FX cases: `fx-sine` (the defaults over a mono 220 Hz
    sine), `fx-noise-stereo` (stereo noise, Stereo 1, Pitch +7), `fx-automate` (a sweep with
    Density and Freeze automated), `fx-file` (the generated `loop-48k.wav`) and
    `fx-note-freeze` (that file, Mix 1, two notes freezing and repitching the ring).
  - **Sub-project 3's four,** the first instrument cases: `inst-note` (one note over a sweep),
    `inst-chord` (three notes over a sine), `inst-release` (a short note over noise, the
    release tail) and `inst-file-44k1` (two notes over the generated `tone-44k1.wav`, converted
    to 48 kHz).
  - **Space's five** (M1 sub-project 4): `fx-space` (a cloud over noise into the reverb at
    Decay 3 s), `fx-dry-send` (Mix 0, Dry to Reverb up: the input alone in the room),
    `inst-diffusion` (a sparse cloud with Diffusion stepped from 0 to 100%), `inst-infinite` (a
    note at infinite Decay, released, the tail holding) and `fx-space-automate` (Size, Decay and
    Pre-delay automated).
  - `fx-space-automate` moves Size only at 0.5 s and in the tail at 5.0 s. Moving Size clicks
    (accepted, S8): over a sine, any other mid-sound move scored 31 to 228 against metricsVersion
    1's click limit of 30, and the 0.5 s move scores 11.9 against version 2's limit of 15, the
    highest clean score in the goldens. Don't add mid-sound Size moves to a golden.
  - Every case that predates Space renders bit-identically with it, so its WAV's SHA-256 is
    unchanged. A Space change that moves one is a regression.
  - **Sub-project 5's three** (the Perform UI): `fx-macros` (each macro stepped 0, 0.25, 0.5,
    0.75, 1 and back to rest, in slot order over a stereo 330 Hz sine, Macro Freeze on last),
    `fx-feedback` (a 4 s stereo sweep, Feedback 0.5, then 0.9 at 3 s, Freeze from 5 s to 6.5 s)
    and `inst-texture` (one held note, Texture stepped 0 to 1 at 1 s intervals).
    - `fx-feedback` catches only 7 of the 16 injected click steps: the sweep's top octaves, then
      their regeneration at Feedback 0.9 and the frozen ring, hide the rest from about 2.8 s to
      6.6 s. Its entry in the click-injection table pins the 7 with that reason. The other two
      catch all 16.
    - The event log sees Texture (`tex`) and the macros' effect on the grains, but never
      Feedback's regenerated audio: across platforms only the metrics compare that.
    - Every older case renders bit-identically with Texture 0, Feedback 0 and the macros at
      rest; a Perform change that moves one is a regression.
  - **M2 sub-project 1's fourteen** (layers and the mod matrix; its spec §10), each 6 to 8 s:
    - layers: `fx-layers` (stereo noise, four Live layers, B Pitch +7, C Size 40 ms, D Pitch
      -12, the pad at rest), `inst-layers-morph` (four Sample layers, a sweep, a 330 Hz sine,
      noise and a lower sweep, one held note, Morph X and Y stepped around the square and back
      to the centre), `inst-pitch-spread` (one note over a sine, A Pitch Spread 0, 2, 7, 12, 24
      at 1 s intervals), `inst-size-spread` (one note over noise, Size 100 ms, A Size Spread 0,
      0.5, 1, 2 octaves), `fx-layer-switch` (Layer B On at 1 s, off at 2.5 s, on at 3 s, over a
      330 Hz sine), `fx-source-switch` (layer A Live, Sample over `loop-48k.wav` from 1 s, Live
      again from 2.5 s) and `fx-macros-layers` (Macro Size, then Macro Pitch stepped with layers
      A and B on);
    - the mod sources: `fx-mod-lfo` (LFO 1 sine 0.5 Hz to Position, LFO 2 square 2 Hz to Pitch
      ±12, over a 50–700 Hz sweep), `inst-mod-env` (Env 1 to Size and Level, Env 2 to Diffusion,
      two notes with a gap), `fx-mod-random` (Random 1 at 4 Hz to Pitch, Random 2 at 1 Hz,
      Smooth 50%, to Stereo), `fx-mod-step` (Step at 4 Hz, Length 12, to Pitch ±12: a figure),
      `inst-mod-midi` (mod wheel to Size, aftertouch to Density, pitch bend to Pitch, velocity to
      Level, key to Texture, over three notes), `fx-mod-follower` (the follower of
      `loop-48k.wav`'s bursts to Density) and `fx-mod-transport` (120 BPM playing, the beat
      phase to Position, the bar phase to Pitch, over the same sweep).
    - Four hide some injected click steps, pinned in the click-injection table: `fx-layers`
      catches none (the four layers' noise bed, as in dense-noise), `fx-mod-follower` 15 (a
      burst hides the step at 3.53 s), `inst-layers-morph` 5 (layer C's noise, near silent only
      on the pad's Y = 0 and X = 1 edges and near its corner X 0, Y 0) and
      `inst-size-spread` 3 (the note's noise bed). The other ten catch all 16; the two sweeps
      end at 700 Hz so that the octave the LFO and the bar phase add stays below the top
      octaves that hide steps.
    - Every one of the 22 M1 cases renders bit-identically with layers B to D off and the
      factory routes: a layers or matrix change that moves one is a regression.
  - **The event-log comparison never sees the reverb or the diffuser:** the log records grains,
    and Space runs after them. Across platforms, Space's five cases are checked by their metrics
    alone, within tolerances.
- **The `golden` tests** render every case into `build/<preset>/golden/<case>.wav` and `.png`.
  - They compare the metrics within tolerances.
  - They compare the grain-event log exactly per line (CR and final newline ignored). That
    comparison is the cross-platform determinism gate. Each `.events.jsonl` line is one grain:
    `i`, `layer` (0–3, since M2 sub-project 1), `onset`, `frac`, `pos`, `rate`, `dur`, `tex`
    (its Texture, since sub-project 5), `gain` and `pan` (`formatEventLine` in
    `tools/gg-render/Render.h`). Across layers the lines are in piece, layer, spawn order, so how
    two layers' grains interleave depends on the block size (INTENT §19, ruling LS-T10b): each
    layer's own lines, and the audio, are block-size invariant, and each golden renders at its
    spec's block size. Compare logs across block sizes per layer.
  - The click score (metricsVersion 2) is normalized by the level within ±5 ms. The
    `[golden][metrics]` tests in `tests/metrics/click_injection_tests.cpp` render every case in
    process, add a -20 dBFS step at sixteen positions in turn and require each to score above
    the limit, except at the positions a case's own noise or high frequencies hide it, which a
    table there pins per case as an exact count. A new case must catch all sixteen or be added
    to the table; a change that moves a count updates its entry, with the reason, in the same
    commit. The two dense-noise cases catch none of the steps, and none of the same positions'
    0 dBFS spikes (9.3 to 14.6 against 15): an accepted blind spot (row 58, director option A,
    2026-10-29).
  - `golden_tamper_events` and `golden_tamper_metrics` check that the gate can fail: each
    compares a render with a golden copy that has one value changed, and passes only on
    gg-render's failure message.
- **After a DSP change, run `scripts/update-goldens.sh [build-dir]`** (default `build/dev`; a
  relative path is taken from the repository root).
  - It rebuilds `gg-render`, writes the media into `<build-dir>/golden-media` and rewrites the
    expected files.
  - It leaves WAVs and spectrograms in `<build-dir>/golden/`.
  - It then checks the new goldens.
  - Last, it prints each case's WAV SHA-256 and lists the cases whose WAV changed, or are new or
    gone, since its previous run in that build tree (`<build-dir>/golden/wav-sha256.txt`). For a
    change meant to leave the output alone (a refactor, a feature at its defaults), run it before
    and after, and say in the commit that every hash matches: the goldens' metrics allow 0.5 dB,
    and the event log holds only the grains, so a small gain error after them passes the goldens
    (INTENT §19).
- Look at every spectrogram before committing new goldens, and say in the commit message why they
  changed.

**Benchmarks (`gg-bench`).** Time these from `release`, on an otherwise idle machine:

```sh
cmake --build --preset release --target gg-bench
b=build/release/tools/gg-bench/gg-bench
$b [--quick | --seconds S] [--rounds N] [--grains N] [--cases a,b] [--commit SHA] \
   [--json out.json] [--baseline baseline.json]                  # grain kernels and engine (§16)
$b --make-baseline <preset> out.json a.json b.json c.json        # write a CI leg's baseline
$b --make-baseline <preset> out.json --merge old.json a.json ... # add or remake models in one
$b --canvas [--quick] [--json out.json]                           # canvas renderer
$b --space [--quick | --seconds S] [--rounds N] [--commit SHA] [--json out.json]  # Space
```

- **The cases** (each at blocks 32, 64 and 256; by default 2,000 grains of 100 ms over a 60 s
  stereo noise source at 48 kHz):
  - **the engine cases,** through `Engine::process`, scheduler and pan included:
    - `engine-sample` is **X**, the §16 headline's case: Normal grains reading a stereo sample
      buffer in RAM (INTENT §23 #44);
    - `engine-sample-eco`: the same at the Eco tier (linear interpolation); the `[bench]` case
      "engine-sample-eco runs at the Eco tier" checks every grain it admits is Eco
      (`gg::bench::admittedTiers`);
    - `engine-live`: the same stream over a 16 s live ring, the FX's figure, published beside X;
    - `engine-full-pool`: admission when the pool is saturated (1 s grains ask for 10× its
      capacity);
    - `engine-layers`: four `engine-sample` engines (layers 0 to 3) sharing its 2,000 grains, 500
      each, rendered through `SerialRunner` and summed in layer order, as a host without a thread
      pool pays for four layers; report-only beside X, with no baseline entry yet (M2 layers
      spec §6.12);
    - `engine-layers-full`: the same with 2,000 grains in every layer, 8,000 in all; report-only
      beside X, with no baseline entry yet;
  - `reference`: M0's 4-wide Hermite kernel, frozen (`renderHermiteSimd` in
    `tools/gg-bench/Kernels.h`). It is the CPU gate's yardstick. `Kernels.h` includes no engine
    header: it holds its own frozen copies of the Hann table and the readers, so no engine
    change can move it, and the `[bench]` case "the reference kernel's output is pinned" hashes
    its output. Never edit those copies to follow the engine;
  - M0's prototype kernels: `linear-scalar`, `hermite-scalar`, `sinc-scalar`, `linear-simd`
    and `hermite-simd`.
- **Timing.** Every configuration warms up once. Then each of 5 rounds (`--rounds`) times every
  configuration once, for at least 2 s (`--seconds`; `--quick` is 0.2 s), in an order rotated by
  the round and reversed in odd rounds. Each result is the median, min and IQR over the rounds.
  The JSON (`--json`) keeps every run, the run length, the round count and `--commit`.
- **The ratio.** Each engine case's ns per grain-sample over `reference`'s in the same round,
  then the median and IQR over the rounds. Because the two run side by side in one process, the
  ratio cancels the machine's speed (though not its microarchitecture). `--cases` must include
  `reference` for any ratio to be printed.
- **Other options:** `--grains N` changes the grain count; `--cases a,b` runs only those cases
  (the default is all; `--help` lists them).
- **Run length:** the full default run is 36 configurations × 5 rounds × 2 s, about 6 minutes of
  timing; a CI leg's benchmark step took about 6 minutes before the two layer cases added one.
  `--canvas` times 240 frames after a 20-frame warm-up, or 30 with `--quick`.
- **`--space`** times Space's per-sample stage, the diffuser then the reverb, on stereo noise at
  48 kHz, active, with Diffusion and Amount at 100% (`tools/gg-bench/Bench.h` lists every
  setting). After a warm-up of 2 s of audio, each of 5 rounds times one run of at least 2 s
  (0.2 s with `--quick`). It prints the median, min and IQR of ns per stereo sample and the
  median's % of one core at 48 kHz, and writes its own JSON document (not schema 2).
  - It is report-only: no ratio, no baseline, no verdict. CI's benchmark step doesn't time it;
    ctest's `bench_space_cli` only checks that it runs. Its exit status is 0, 1 if it can't
    write the JSON, or 2 for bad usage.
  - Sub-project 4's fix wave measured 28.49 ns per stereo sample, 0.137% of one M5 Max core,
    at its final code (`ac81dfb`; INTENT §16), and sub-project 6's idle run 29.44 ns, 0.141%,
    published beside X (19.5% at block 256; `docs/benchmarks/`, INTENT §23 #60).
- **An option a mode doesn't take is bad usage** (exit 2, with the usage): `--space` takes only
  the options shown above, `--canvas` only `--quick` and `--json`, and `--make-baseline`
  nothing else (its `--merge` is one of its own values).
- **When profiling the plugins in a VST3 host,** remember that clap-wrapper's VST3 `process`
  takes a spin lock. Time spent there is the wrapper's, not ours.
- **Exit status:** 0 when done and no ratio regressed, 1 for an error (an unreadable or malformed
  baseline, say), 2 for bad usage, 3 when `--baseline` finds a regressed ratio or a missing one.
  A run on a CPU model the baseline has no entry for prints `no baseline for <machine>; no
  verdict` and exits 0.
  - **Every entry of the model's baseline is accounted for:** one the run lacks is printed as
    `missing from this run` and counts as a failure (exit 3); the verdict line then ends `, N
    baseline entries missing from this run`. So `--baseline` with a `--cases` subset fails. A
    ratio the baseline lacks is only `not in the baseline`.
  - **A verdict over no ratios is an error** (exit 1): a run without `reference` or any engine
    case, on a model the baseline has, never passes as "0 of 0".
- **In CI:** the build legs run the full length (with `--commit` and, when
  `tools/gg-bench/baselines/<preset>.json` exists, `--baseline`) on pushes to `main`, on the
  nightly schedule (03:17 UTC) and on `workflow_dispatch`. They print the output, and a verdict
  per ratio, in the step summary and upload `gg-bench-<preset>` (`gg-bench.json` and
  `gg-bench.txt`). On a pull request each leg runs `--quick` as a smoke check only: it checks no
  baseline, prints no verdict and uploads nothing, but a crash or exit 1 or 2 still fails the
  job.
  - **Baselines are per CPU model.** GitHub assigns a leg different CPU models from run to run
    (seen so far: Linux on AMD EPYC 7763, 9V45 and 9V74 and Intel Xeon 8370C; Windows on EPYC
    7763, 9V45 and 9V74). For unchanged code, ratios differ by up to ~20% between models and ~6%
    on one model. So a leg's baseline has one entry per model, keyed by gg-bench's `machine`
    string, and a run is checked only against its own model's entry. The committed baselines
    have entries for the mac leg's Apple M1 VM, Windows' EPYC 7763 and 9V74, and Linux's EPYC
    7763 and 9V45. A model with no entry gets no verdict (the summary says so) until later runs
    on it are added to the baseline.
  - **The gate is report-only until 1.0.0** (INTENT §19, §23 #44, #60, #61): exit 3 becomes a
    `::warning` and the step exits 0. A crash, exit 1 or 2, or the timeout still fails the job.
    The `bench_baseline_*` tests pin exit 3 (a regression or a missing entry), exit 0, the
    no-verdict line and exit 1 for a run with no ratios.
  - The gate turns blocking at 1.0.0, after M5 (INTENT §23 #60, #61), with the published X. The
    0.x releases, public from 0.5.0's first beta, keep it report-only.
- **Refreshing a baseline** (`tools/gg-bench/baselines/README.md` has the format):
  1. Make three separate full-length CI runs of the same engine code: runs on `main`, the
     nightly or `gh workflow run ci.yml --ref <branch>` (a pull request's runs are `--quick` and
     upload no artifact). One run's IQR says nothing about the next run's
     drift, which reached ~20% on Windows (INTENT §23 #47).
  2. Download each run's artifact: `gh run download <id> --pattern 'gg-bench-<preset>'`.
  3. Run `gg-bench --make-baseline <preset> tools/gg-bench/baselines/<preset>.json a.json b.json
     c.json`. It must be the last option, since it takes the rest of the command line.
     - It groups the runs by CPU model: runs on different models make separate entries.
     - Each ratio is the median of that model's runs, with a tolerance of
       `max(0.10, 2 × (max − min) / median)`.
     - A model seen in only one run gets the 0.10 floor.
     - It checks what it makes as `--baseline` would: a median that isn't positive and finite,
       or a tolerance that isn't finite, is an error (exit 1), and no file is written.
  4. Commit the file. A later CI run on a model in the file should show `pass`, but not always:
     the M1 VM's drift between runs can exceed even a tolerance from three runs (the
     verification run showed `engine-sample-eco` at block 32 at 1.1546 against a limit of
     1.0377, a false "regressed"). A run on a model the file lacks shows "no verdict".
  5. **To add a model, or remake one,** without the runs the other entries came from (CI's
     artifacts expire), merge: `gg-bench --make-baseline <preset>
     tools/gg-bench/baselines/<preset>.json --merge tools/gg-bench/baselines/<preset>.json
     d.json e.json f.json`. `--merge <old.json>` goes right after the output, and may be the same
     file. Every entry of the old file stays, in place, except the runs' models', which are made
     from the new runs alone; models the old file lacks are added at the end. It prints each
     entry as `(kept)`, `(replaced)` or `(new)`. The legs must match. The new runs must still be
     of engine code whose ratios match the kept entries' (no engine change since).
     **A replaced entry is rebaselined to its new runs, a regression included:** compare each
     `(replaced)` entry with the old one (`git diff`) before committing, and explain any ratio
     that moved.

  A baseline is valid only against the frozen `reference` kernel: if that kernel, or anything it
  calls, changes, make every baseline again. Its pinned hashes fail first.

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
  # A local build: CPM records the checkout. CI configures with VST3_SDK_ROOT instead.
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
  - It defaults to `-`, which signs ad hoc, exactly as before the release pipeline.
  - A real identity (`Developer ID Application: …`) also signs with the hardened runtime and a
    secure timestamp (which needs the network), and the two apps with the audio-input
    entitlement (`installer/macos/standalone.entitlements`). The bundles re-sign only when they
    relink, so change the identity in a tree without `gg_assets/` (delete it, then configure).
  - Notarization is `gg-installer`'s: `installer/macos/make-installer.sh` signs the packages,
    notarizes and staples the PKG, then signs, notarizes and staples the DMG, when its five
    signing variables are set, and prints `signed: yes` or `signed: no` last.
  - Strict hosts such as Ableton Live won't load a bundle that carries only the linker's
    signature (`docs/spikes/m0-host-matrix.md`).

**Releasing** (`docs/releasing.md`, the runbook; INTENT §19).
- **The version** lives in `CMakeLists.txt` alone: the numeric part in `project(GigaGrains
  VERSION X.Y.Z …)` and the pre-release suffix in `GG_VERSION_PRERELEASE`, `dev` on `main`, so a
  build from `main` reports `0.2.0-dev` and never claims a release. A release commit sets the
  suffix (empty, or `beta.N`) and adds `docs/releases/v<version>.md`;
  `scripts/release/check-version.sh v<version>` checks a tag against it.
- **Three run kinds** (`ci.yml`'s `plan` job): a pushed `v*` tag builds, signs and notarizes the
  installers and makes a draft GitHub Release, which the director publishes by hand; the nightly
  keeps them as run artifacts, and publishes the `nightly` pre-release only once
  `GG_PUBLISH_NIGHTLY` is `"true"`, from M5; a dispatch with `installers: true` is the dry run,
  which publishes nothing. Pull requests and pushes to `main` build no installer and see no
  secret.
- **The secrets** (Actions secrets, set by the director; never in the tree): `MAC_CERTS_P12`,
  `MAC_CERTS_P12_PASSWORD`, `MAC_SIGNING_IDENTITY`, `MAC_INSTALLER_IDENTITY`, `NOTARY_KEY_P8`,
  `NOTARY_KEY_ID`, `NOTARY_ISSUER_ID`. Locally, make-installer.sh reads the same names, with
  `NOTARY_KEY_FILE` (the `.p8`'s path) for `NOTARY_KEY_P8`.
- **A local installer:** `cmake --build --preset release --target gg-installer`, unsigned
  unless all five of make-installer.sh's variables are set in the build's environment (and the
  tree configured with a real identity); nothing is written into the source tree.
- **Nothing is public before M5:** the 0.2.0, 0.3.0 and 0.4.0 drafts stay drafts, and
  `v0.5.0-beta.1` is the first release the director publishes.
