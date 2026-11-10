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
- **Where we are** (INTENT §20, §23 #61, #64, #71, #93, #95): M0 and M1 are done, and so are the installers and
  release pipeline (`docs/superpowers/specs/2026-10-29-release-pipeline-design.md`; the runbook
  is `docs/releasing.md`). **M2 is closed** (2026-11-03) as seven sub-projects in four waves, run by one
  agent session each (`docs/superpowers/specs/2026-10-29-m2-program-design.md`: read its §6
  for your sub-project's scope and what it owns, and its §7 for the rules every sub-project
  builds on). Sub-project 1, Layers and structure, is built (INTENT §23 #65): four layers, the
  morph, the Source kind, the mod matrix, render tasks and the patch at version 2, which wave 2
  builds on. Sub-project 3, Engine scale and quality, is built (INTENT §23 #66; PR #16): the HQ
  sinc tier and each layer's Quality, file mipmaps, the deterministic CPU budget (Budget, the
  quotas and the overload ladder), the pool to 16,384, the `[sweep]` harness and AVX2 kernels
  chosen at load (kept). Sub-project 2, Sources and persistence, is built (INTENT §23
  #68; PR #19): the Resample source, rings of 1 to 300 s with loop, overdub, splices and capture,
  the memory cap, the sample pool, `MEDI` v2, `AUDI`, `INST`, rings saved with the session and
  the rings' mip pyramids on sub-project 3's half-band. Sub-project 4, Extremes and the FX chain,
  is built too (INTENT §23 #69): Smear, the Spectral kind, two filtered grain buses per layer, a
  global delay and limiter, forty parameters (283 in all with sub-project 2's twenty) and thirteen
  remote-controls pages. Sub-project 5, Presets and content, is built (INTENT §23 #70; PRs #17
  and #25): the `.ggpreset` file, INIT, CLAP preset discovery, the browser and Mutate, content
  v2 (51 CC0 files, VSCO 2: CE strings and brass beside VCSL), content-relative preset samples,
  the 48 factory presets and their 48 goldens (108 in all). Sub-project 6, Views, is built (INTENT
  §23 #71; PRs #18 and #24): the Perform, Edit, Mod and FX views, four lanes with a pitch axis,
  the layer strip and XY pad, the Mod view's route path, undo, two themes, the About box,
  `gg-snap` and the GPU spike. Sub-project 7, the close, is built (INTENT §23 #93; PR #31): the
  consistency pass, the carry-over burned down to 15 open rows, the from-scratch build setup, the
  gate, the idle figures and the release commit for v0.2.0 (the program manager tags it). **M3 is
  in progress** (since 2026-11-07). It began as M1 was run, one session with serial
  sub-projects (§23 #87), and since 2026-11-07 its sub-projects run in parallel on their own
  branches, merging as each is ready (the director; INTENT §20, §23 #87, #94, #96;
  `docs/superpowers/specs/2026-11-07-m3-program-design.md`: 0 row 405, 1 Voice slice, 2 Full
  expression, 3 Pitch-sync, 4 Microtuning, 5 Views and presets, 6 Close). **Sub-projects 2, 3
  and 4 are specified and being planned in parallel**, on `m3-sp2-expression`,
  `m3-sp3-pitchsync` and `m3-sp4-tuning`. **M3 sub-project 1, the Voice slice, is built** (INTENT
  §23 #95; branch `m3-voice`, draft PR #37; spec
  `docs/superpowers/specs/2026-11-07-m3-voice-design.md`): a per-layer Mode (Cloud or Voice), up
  to 16 voices a layer with automatic headroom (V7), the note table and the sustain pedal, MPE
  and CLAP note expressions, per-voice modulation with the Voice-only routes (factory slots 44
  to 48), 290 parameter slots (four parameters: Mode in each of four layers, plus MPE, MPE Bend
  Range and Bend Range, seven slots), eight voice goldens, and the carry-over in
  `docs/m3-carry-over.md` (`docs/m2-carry-over.md` is M2's frozen record). **M3 sub-project 3b,
  the playhead, is built** (INTENT §23 #97; branch `m3-sp3b-playhead`; spec
  `docs/superpowers/specs/2026-11-07-m3-playhead-design.md`): Scan Speed, Scan Edge and Scan Track
  per layer (a playhead per Cloud stream and per voice; Position is where reading starts), 66
  parameters per layer and 302 slots on that branch (the sums once the parallel sub-projects
  merge), seven goldens, and tempo-locked scanning left to sub-project 7's Scan Sync. M2, M3 and M4 are tagged 0.2.0, 0.3.0
  and 0.4.0 as unpublished draft releases; nothing is public and there are no betas before M5,
  whose `0.5.0-beta.N` are the first public releases (§23 #62); v1.0.0 follows M5.
- **The records:** each sub-project's spec in `docs/superpowers/specs/`, the published benchmark
  JSON in `docs/benchmarks/`, the listening packs in `docs/listening/`, the M0 and M1 spikes in
  `docs/spikes/`, the release notes in `docs/releases/`, and the carry-over in
  `docs/m3-carry-over.md` (M2's, frozen as its record, is `docs/m2-carry-over.md`; M1's is
  `docs/m1-carry-over.md`), and the
  dependency watch, `docs/dependency-watch.md`: the pins that trail upstream on purpose, each with
  what would move it and what to check (not carry-over; INTENT §23 #90).

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

  The order is by load, and the engine's D4 decides between the last two (INTENT §23 #81, the
  director, 2026-11-02): lower the tier; while the oldest sounding grain is under half its window,
  drop the new grain; past it, steal it with a fade.

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
- **Deferred items** (`docs/m3-carry-over.md`) are handled as superpowers' subagent-driven development
  handles them (the director, 2026-10-30, retiring the 2026-10-28 carry-over policy): a reviewer's
  minor is fixed or deferred at the controller's judgment; deferred minors and items blocked outside
  our control (upstream, the director's ears, a later feature) are listed in the sub-project's final
  report and recorded in that file as a plain log, with no count rules. A row stays until the thing
  it names is done; nothing is filed upstream.
- **Models** (the director, 2026-10-30): no Fable, ever. Name `model` on every dispatch. Where
  superpowers' Model Selection says "the most capable model", use opus; otherwise haiku for
  mechanical work and sonnet for integration and as the reviewer floor.
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
- You need CMake 3.22 or newer and Ninja. On Windows, Visual Studio 2022 (or its Build Tools) with
  the C++ x64 tools and the C++ Clang (ClangCL) component; configure and build `windows-clangcl`
  from an "x64 Native Tools" developer shell with Visual Studio's `VC\Tools\Llvm\x64\bin` first
  on the PATH, so `clang-cl` is Visual Studio's own (INTENT §23 #76).
- **A fresh machine:** `scripts/bootstrap-macos.sh [--ci] [--clone DIR]`,
  `scripts/bootstrap-linux.sh [--with-format] [--clone DIR]` and
  `scripts/bootstrap-windows.ps1 [-Verify] [-Clone DIR]` install what the build needs (the README's
  "Building from source" has their one-liners); `scripts/dev-shell-windows.ps1` enters the x64
  developer shell. Their `--clone` runs `ctest -LE serial` (the `[serial]` timing and memory cases are left out of a
  newcomer's first run), and `GG_REPO_REF` picks the commit to check out, in a new or an existing
  clone. The Linux one installs a font (`fonts-dejavu-core`) beside the JUCE libraries: the GUI
  tests lay out text. CI's legs run them (the `scripts_bootstrap` test checks them against stubs).
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
- **The content** (M2 sub-project 5's spec §6.5): the CC0 samples the factory presets load, a
  zip released in https://github.com/sethbc/giga-grains-content and pinned by
  `content/pin.txt` (tag, URL, SHA-256). Configure downloads nothing, in any preset. The
  `content_fixture` test (label `golden`) builds the `gg-content` target, which fetches the zip
  once per build tree into `build/<preset>/content-cache/content/`
  (`scripts/fetch-content.cmake`: the SHA-256 checked, a mismatch deleted, `.gg-pin` written
  last), so the first `ctest` of a tree needs the network and later ones are offline.
  `-DGG_CONTENT_DIR=<dir>/content` points a tree at a folder `scripts/fetch-content.sh <dir>`
  filled, as CI does; `gg-content` then only checks its `.gg-pin` against the pin.

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
`linux-exports.map`; INTENT §15). The Linux baseline is Ubuntu 24.04 (§23 #91): the products
need its glibc, 2.39, and no longer run on 22.04. CI's Linux leg (`ubuntu-24.04`, gcc-13 from the
archive) checks it after the build: "Linux products need no libstdc++ or glibc newer than Ubuntu
24.04's" reads the ceilings from the runner's own `libstdc++.so.6` and `libc.so.6` (after
checking both packages are Ubuntu's, and that the runner's glibc is exactly the documented 2.39,
`GG_BASELINE_GLIBC`), fails on a `GLIBCXX`, `CXXABI` or `GLIBC` version
above them or an exported `std::` symbol, and lists the ceilings and each product's symbol
versions in the step summary.

**The required checks** (INTENT §23 #77). `main`'s ruleset requires two: `ci-ok` and
`clang-format`. `ci-ok` passes only when `plan`, every build leg and every sanitizer job passed. A
pull request whose every changed file is under `docs/`, or is `INTENT.md` or `CLAUDE.md`, is
documents only: `plan` says so, the build legs and the sanitizers skip, and `ci-ok` passes on the
skip in about a minute. Any other Markdown file counts as code (`THIRD_PARTY_NOTICES.md` is
embedded in the editor, the content and installer READMEs are staged into the products, and the
root `README.md` is checked by a test). Pushes, the nightly, dispatches and tags always run
everything.

**CI's sanitizer jobs** (INTENT §23 #74). ASan + UBSan runs in six shards side by side, each
running every sixth test (`ctest -I <shard>,,6`; a fixture's setup test joins any shard that needs
it), and RTSan in a seventh job. `sanitizers (asan, rtsan)`, the check the release job needs, passes
only when all seven pass. ASan leaves out the `slow` tests
(`-LE slow`) on a pull request and a push to `main`, and runs them on the nightly, a dispatch and a
tag; RTSan runs everything. To reproduce a shard locally: `ctest --preset asan -I 3,,6 -LE slow`.

**CI caches** (`.github/workflows/ci.yml`; none changes what is built):
- **The SDK checkouts** (VST3 SDK, AudioUnitSDK, RtAudio, RtMidi, wil): `scripts/fetch-sdks.sh`
  clones each at the tag clap-wrapper's `cmake/base_sdks.cmake` pins into `.sdk-cache`, and
  Configure passes `-DVST3_SDK_ROOT=...` and the rest, so clap-wrapper downloads nothing. The
  cache is keyed by leg and by the hash of `base_sdks.cmake` and the script. (CPM's own
  `CPM_SOURCE_CACHE` cached nothing: clap-wrapper gives every package a `SOURCE_DIR`, which makes
  CPM skip it.) The VST3-validator step builds from `VST3_SDK_ROOT`.
- **ccache**, every build leg and the sanitizer jobs, passed as
  `CMAKE_<LANG>_COMPILER_LAUNCHER` at CI configure (local presets are unchanged). Windows uses
  ccache's own pinned release (4.14.2, SHA-256 checked) since the leg moved to Ninja (INTENT §23
  #76); `mac-universal`'s two-architecture compiles are largely uncached. Keyed by OS, preset,
  runner image (`macos-26`, `windows-2022`, `ubuntu-24.04`; a new image misses once, as the Linux
  leg's move from `ubuntu-22.04` did, §23 #91) and commit (the sanitizer jobs' by sanitizer, LLVM
  version and commit; one ASan shard and the RTSan job save), restored by prefix.
- **The pinned LLVM archive** (sanitizer jobs), keyed by version and SHA-256. The SHA-256 is
  checked on a hit too.
- **The content** (every build leg and the sanitizer jobs): `scripts/fetch-content.sh` fetches
  the zip `content/pin.txt` pins into `.content-cache`, and every configure passes
  `-DGG_CONTENT_DIR=$CONTENT_CACHE_DIR/content`. Keyed by the hash of `content/pin.txt` and
  `scripts/fetch-content.cmake` alone (the content is the same on every leg); a hit downloads
  nothing.
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
| `asan` | ASan + UBSan, Debug, with `GG_BUILD_EDITOR=OFF`: the engine, `gg-core`, `gg-media`, the UI core, the tools, the plugin core and the tests. No editor, JUCE or plugin formats. With Apple clang it misses warnings CI's LLVM 23 turns into errors: also configure the CI-equivalent tree once, `CC=/opt/homebrew/opt/llvm/bin/clang CXX=/opt/homebrew/opt/llvm/bin/clang++ cmake --preset asan -B build/asan-llvm`, and build `gg-tests` in it. |
| `rtsan` | RealtimeSanitizer, RelWithDebInfo, built like `asan` (`GG_BUILD_EDITOR=OFF`). Needs upstream LLVM 20 or newer in `$GG_LLVM` (Apple clang has no RTSan). |
| `mac-universal` | Release for arm64 + x86_64. CI's macOS leg, which also runs the x86_64 slice's tests and golden checks under Rosetta. |
| `windows-clangcl`, `linux-gcc13` | CI's Windows and Linux legs. `windows-clangcl` is Ninja with clang-cl, Release, from a VS 2022 x64 developer shell (into `build/windows/`). `linux-gcc13` is gcc-13 on Ubuntu 24.04, the Linux baseline (INTENT §15). |

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
  - the GUI tests, `gg-snap`, the bundles and the `codesign_*`/`identity_*` tests need the
    editor, so they aren't built.
- **RTSan:** set `GG_LLVM` when configuring:
  - `GG_LLVM=/opt/homebrew/opt/llvm cmake --preset rtsan` with Homebrew LLVM on macOS;
  - CI uses LLVM's 23.1.4 release for Linux, checked against its SHA-256, in `/opt/llvm-23.1.4`
    (Homebrew's `llvm` was at 23.1.4 too on 2026-11-03, so CI's sanitizer builds and a local
    `rtsan` matched then).
  - Configuring fails, naming `GG_LLVM`, if the compiler isn't upstream Clang 20 or newer.
- **Outputs:**
  - The plugins and standalone apps land in `build/<preset>/gg_assets/`.
  - `cmake --build --preset <p> --target gg-products` gathers them into
    `build/<preset>/gg-products/` for validation.
  - CI builds `--target gg-all gg-products`.
  - `gg-products` also stages `content/` (the factory presets `gg-presets` packs with
    `gg-render --pack-preset` from `content/presets/`, the fetched samples, `PROVENANCE.md`, the
    CC0 text and `content-version.txt`): the installers' Content component. It depends on
    `gg-content`, so a tree needs the fetched release (or `GG_CONTENT_DIR`).
  - `cmake --build --preset <p> --target gg-installer` builds `gg-products`, then packs it into
    `build/<preset>/gg-installer/`: the macOS PKG and DMG, the Windows zip and, when Inno
    Setup's `ISCC.exe` is found at configure, its installer, or the Linux tarball. CI's Windows
    leg installs Inno Setup 7.1.1 (pinned, SHA-256 checked) and names its `ISCC.exe` with
    `-DGG_ISCC`; locally `cmake/products.cmake` looks in `Program Files\Inno Setup 7`, then Inno
    Setup 6's folder (the script needs 6.3 or later). Unsigned unless
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
  2. It builds `gg-all` for `asan` and `rtsan`, as CI's sanitizer jobs do, and the default
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
options must all match: `-L core -L rt` runs only the tests labeled both, and `-L 'core|rt'`
runs the tests labeled either. The labels are:
- engine: `engine`, `cloud` (the Cloud scheduler), `live` (the live ring), `pool`, `source`,
  `rng`, `math`, `interp`, `window`, `denormal`, and since M2 sub-project 3 `budget` (the
  weights, the ledger, the quotas, the ladder's three rungs, the steal fade, the exit case and the
  ladder's block-size invariance) and `mip` (the file pyramid's half-band, level count, level per
  rate and the accessor's fallback);
- `core`: `gg-core`'s patch (with its `<mod>` routes and the migration from version 1), the
  session state container (`GGST`, with `MEDI` v2, `AUDI` and `INST`) and the settings' XML,
  the parameter mapping, and the `Processor` with its layers, notes, source hand-off, Feedback
  and render tasks. `slow` marks two of them, Feedback 0.95 for 60 s (over a minute in `dev`)
  and the worst Resample loop for 60 s;
  - `source` (also an engine label; in `core` since M2 sub-project 2): the `Processor`'s
    Resample tap, ring parameters, commands, ring exchange (resize and restore) and ring read,
    with their determinism and tripwire cases (among them `tests/core/source_tests.cpp` and the
    ring-exchange cases of `tests/core/exchange_tests.cpp`);
  - `sweep` (M2 sub-project 3): the sweep harness, 30 cases in `tests/sweep/` over the
    `Processor` in both products (parameter sweeps, extremes, automation bursts and the overload
    sweeps), each checking finiteness, the peak bound, no runaway and the goldens' click score
    outside named exemptions (`SweepDriver.h`; carry-over rows 217 and 302), and pinning a floor
    under the share of its render that it scores. About 26.5 s in `dev` (eight at a time) and
    about 194 s under ASan, both timed over the first 20 cases (a re-time of all 30 is the close's);
    none is `slow`;
  - `layers` (M2 sub-project 1): the four layers, in the engine (the layer's RNG key, the enable
    gate, the kind switch, the spreads) and in the `Processor` (the sum in order, the morph law,
    on and off, the kinds, the instrument's input into the rings, the event order);
  - `voice` and `notes` (M3 sub-project 1): `voice` is the Voice slice's tests, in the engine (a
    voice stream's key, the merge by onset, the steal, the clears), the `Processor` (Mode, the
    per-voice sources and routes, bend with MPE Off and On, expression, the tail, the voice
    goldens' MPE-against-expression equality) and the plugin (CLAP note IDs and expressions,
    MIDI-MPE, the pedal, `clap_plugin_as_vst3`, `NOTE_END`, the tripwire with voices); `notes` is
    the note table (identity by ID or pair, the pedal, expression state, the held set equal to
    M2's on M2 sequences);
  - `playhead` (M3 sub-project 3b): the playhead, in the engine (`scanFold` at each edge, the
    closed form, the rebase rules, the restarts, the ring's lag, block-size invariance at six
    block sizes) and the `Processor` (the `8 v^3` law, per-voice Scan Speed routes, Scan Track's
    ratio, the Cloud's restart, Scan Speed automation at several block sizes, the event log's
    `pos` against the closed form), the comb case for a slow scan, the preset load and INIT and
    the tripwire with scanning voices;
  - `mod`: the mod matrix: its routes, curves and factory routes (M1's `macroOffsets` bit for
    bit), the modulation sources, and the `Processor`'s per-cell application; also the editor's
    rings (`tests/ui/rings_tests.cpp`, tagged `[ui][mod]`);
  - `threads`: `TaskRunner`, `SerialRunner` and the tests' `StubPool` (`engine`), and the
    `Processor`'s render tasks (`core`): the same bytes with no pool and pools of 1, 2 and 8
    workers, a declining pool, and the tasks' denormal mode and allocations;
  - `spectral` (M2 sub-project 4): the Spectral Smear source in the engine (the hop on the clock,
    the keyed phases, the RMS, the kind switches, Freeze) and in the `Processor` (the material,
    its units, Position, Feedback 0, the switch-away drain);
  - `buses` (M2 sub-project 4): the two grain buses and their filters in the `Processor` (bus 2
    empty until a grain takes it, each filter type, the per-sample ramps, the declicked type
    change, Resonance 100% stable);
  - `fx` (M2 sub-project 4): the global delay and limiter units and their `Processor` chain (the
    identity at rest, the echoes, the hold, ping-pong, Time's glide, the limiter's modes);
  - `publish`: the audio-to-UI structures (the grain queue, the UI snapshot, the live peaks) and
    the `Processor`'s publishing;
  - `preset` (M2 sub-project 5): the preset element in `PTCH` (`encodePreset`, `readPreset`,
    `presetInfoOf`, INIT) in `core`, and in the plugins a preset load (`preset-load`, INIT, what
    a load keeps (a preset file keeps each layer's five ring parameters; INIT keeps Ring Length
    alone), the preset's name and the edited mark, which ignores the ring parameters). `-L` is a
    regex, so `-L preset` also matches `presets` (the 48 preset goldens, `presets_built` and the
    content fixture, which needs the network on a tree's first run): use `-L '^preset$'` for the
    preset unit tests alone;
  - `mutate`: Mutate's model (`core/Mutate.h`: the law, the locks, determinism) and the plugin's
    (the press, the batched sender, the drain witness, the panel's lock list), and `gg-render
    --mutate-preset`'s (with `render`);
- `discovery`, `catalog` and `queue` (with `plugin`; M2 sub-project 5): `discovery` is the CLAP
  preset-discovery provider driven through the entry point (its locations, soundpack and
  `get_metadata`); `catalog` is the browser's model (`PresetCatalog`: the list, search and tag
  filters, favorites, save as and delete, the 5,000-file cap); `queue` is the UI queue's batched
  sender, `sendValues`, which never overflows it. The case "a crawl of the 48 packed factory
  presets takes under 50 ms" (`discovery_tests.cpp`; also `presets`, `serial` and `crawl48`)
  requires the `presets_built` fixture (so the first run of a tree needs the network, as the
  content does), crawls the packed factory presets as a host would (a `get_metadata` per file),
  builds both catalogs, requires 48 seen (24 per product) and the crawl under 50 ms in `dev`
  (spec §10); under ASan it checks the counts alone, the bound meaning nothing there;
- `content`: the content roots and paths in `gg-media` (`ContentPaths.h`: the roots in order,
  a reference resolving to the first that holds it, the user presets and favorites files),
  with `media`; the fetched release itself is the `content_fixture` test (below, under
  "Golden renders");
- `media`: `gg-media`, the JUCE-free sample-file library (`media/`) that the plugin's loader and
  `gg-render` share: decoding, rate conversion, SHA-256, loading a sample, the search for a
  missing one (with the content roots and the captures folder) and the settings file, and since
  M2 sub-project 2 the memory cap's accounting (`MemoryBudget`; the 600 s file cap is gone), the
  sample pool, float32 WAV bytes, a ring's saved audio and collect and save. `slow` marks one of
  them, the search's 50,000-entry scan.
  - The memory cap counts the rings and loaded samples, not the fixed costs. One of those is
    `LivePeaks` (the editor's peak strip): each layer's is sized for a 300 s ring in every
    instance, about 0.9 MB a layer at 48 kHz (scaling with the rate), outside the cap.
  - The WAV, AIFF and AIFC files they decode are written at run time
    (`tests/media/AudioFixtures.h`). dr_libs can't write FLAC or MP3, so
    `tests/fixtures/audio/` holds one tiny FLAC and one MP3, the only audio committed to the
    repository. Its `README.md` gives their SHA-256s and the commands that made them. Don't
    commit any other audio: generate it.
- `space`: Space (M1 sub-project 4): the diffuser, the patched Reverb 2 and `SpaceReverb`, the
  `Processor`'s Space chain, and the plugins' tail and "Space" remote-controls page. `slow` also
  marks four of them: the hour-long renders at infinite Decay (about 50 s each in `dev`, 2.5
  minutes under ASan), the block-size invariance render and the 60 s render at extreme settings
  (about 8 s and 13 s in `dev`); `-LE slow` leaves all eleven slow tests out (media's one, these
  four, core's two (Feedback and the Resample loop), render's embed-cap save, about 8 s, and
  since M2 sub-project 3 two in `engine`: `the overload ladder is block-size invariant`, 125 s in
  `dev`, which has a fast sibling that always runs, and the budget's exit case, `the exit case
  never clicks or drops out`; and `snap_cli`, below, about 66 s in `dev`).
- `kernel`: the renderers' scalar references and the SIMD sample and live kernels (4-wide, and
  since M2 sub-project 3 the 8-wide AVX2 ones beside them, chosen at load), which must match them
  bit for bit (`std::memcmp`); every case runs through both tables, and the AVX2-against-SSE case
  skips in a build or on a CPU with no AVX2 kernels (Rosetta's x86_64 slice tests the fallback).
  CI's Linux and Windows test steps set `GG_REQUIRE_AVX2=1`, and then `kernel_require_avx2` fails
  if there is no 8-wide table, so those legs can't fall back unnoticed; a ClangCL below 19 builds
  the 4-wide table alone. Set `GG_FORCE_SSE` (set and not empty) to make a process, or a ctest,
  use the 4-wide table; `kernel_force_sse_env` checks it;
- `determinism`: block-size invariance;
- `rt`: the allocation tripwire. In the `rtsan` build, two tests labeled both `rt` and `rtsan`
  check that RTSan is active: one checks the build flag, and `rtsan_negative_control` passes
  only if RTSan reports an allocation made inside a nonblocking function.
- tools: `render` (gg-render's CLI, `render_write_media_alone`, `render_spectrogram_utf8_path`,
  `render_write_media_fixtures`, `render_write_media_lists_fixtures` and `render_state_roundtrip`,
  and the presets' `render_pack_preset*` and `render_preset_*` among them, `render_preset_empty`
  (an empty `--preset` argument) and `render_preset_sample_mode` (a preset in a sample-mode spec)
  refusals included; and its preset sources, `tests/tools/preset_source_tests.cpp`), `metrics`,
  `bench` (gg-bench's command line: the `bench_rejects_*`, `bench_make_baseline_*`,
  `bench_baseline_*` and `bench_space_cli` tests; and the in-process cases, `engine-live-long`'s
  at `--quick`'s warm-up among them);
- `golden`: the golden renders;
- `presets` (M2 sub-project 5's group 2): the factory presets as built: the 48 preset goldens
  (`golden_preset_*`, with `golden`) and their fixture `presets_built`, the click-score check
  (`tests/metrics/preset_goldens_tests.cpp`, with `golden`) and the counts
  (`tests/tools/factory_presets_tests.cpp`, with `build`: 24 sources per product named
  `NN Name.json`, NN 01 to 24, 8 FX ones tagged `midi`);
- `ui`: `gg-ui-core`, the editor's framework-free math, so it runs on every leg and under the
  sanitizers: the splat renderer and its banded resolve (one tint per lane; the one-band case is
  M1's resolve bit for bit), the lane geometry (`LaneModel`: the pitch axis, the scan rectangle,
  its hit test with every region and the corner rule, and the axis lock), the lane's clock and
  live-grain list (`LaneState`), and since M2 sub-project 6 the pad's law (`PadModel`, equal to
  the `Processor`'s morph weights), the views' layout table (`EditLayout`: every parameter slot
  placed exactly once), the rings (`[ui][mod]`), the Mod view's route edits and the undo stack.
  With the editor only, `notices_tests.cpp` checks the texts embedded into the editor against
  the license set, byte for byte;
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
  Setup compile (`installer_windows_*`); and the Content component: `installer_content_staged`
  (the staged `content/`: presets, samples against `PROVENANCE.md`, the version file) and
  `installer_windows_iss_content` (the `.iss` declares the component; it runs on every OS) and
  `installer_windows_iss_commented` (a commented-out `[Files]` line doesn't count, checked over a
  fixture); `installer_cc0_text` (the CC0 text the installers carry is the content release's
  LICENSE, byte for byte) sits outside the editor guard, so it runs without the editor too;
- `serial`: the Catch2 cases tagged `[serial]`, which measure time or memory and run with
  `RUN_SERIAL` (see "Tests run in parallel");
- `plugin`: the plugins, driven through their CLAP factory. Tests labeled both `plugin` and
  `rt` put the allocation tripwire on `process` and `params.flush`. `routes_tests.cpp` checks
  the editor's route path (`uiSetRoutes`: the render, the saved state, a load and an edit in one
  block in both orders, an inactive edit, the tripwire). Since M2 sub-project 2,
  `tests/plugin/persist_tests.cpp` covers the four loaders, rings saved and restored, the Ring
  Length resize, the memory cap, capture and collect and save (two of them, the ring reader's
  stress tests, also `threads`).
  - **The GUI tests** (with the editor only, on every leg that builds it; the sanitizer presets
    build none) make the real editor headless and drive its components:
    `tests/plugin/factory_tests.cpp` the parts (the canvas's lanes and drags, the strip, the
    pad, the panels, the Mod and FX views, the About box, the theme setting) and
    `tests/plugin/editor_frame_tests.cpp` the frame (since M2 sub-project 6): every view opens
    and closes in both products and both themes at 960 × 600 and 640 × 360, with every control
    inside its view; "every parameter is reachable from the UI" walks the views for each slot's
    control; undo replays through the gesture path and survives the editor; the theme switch
    persists and a second editor follows; the keyboard reaches every control; the view is kept
    per instance; no label is elided or collides at 960 × 600 or 640 × 360, a menu's every choice
    and caption included (the strip's toggle reads "On", its Level slider has no label, and a
    narrow strip's Source button draws a kind's short form, "Osc"); the light theme reads at
    4.5:1, the strip's column following the theme while the lanes stay dark; a route added in
    slot 13 scrolls into the Mod view's list. A GUI test that wants one control searches below its
    view's ID (`view.perform`, `view.edit`, `view.mod`, `view.fx`): the strip's controls share their
    `param.<id>` with the Edit panels'.
  - `snap_cli` (macOS, with the editor; also `slow`) runs `gg-snap` ("The views as PNGs"
    below).

  On macOS this label also covers checks of the built bundles (with the editor only):
  - `codesign_*`: strict signature checks, which Ableton Live's scanner needs to pass;
  - `identity_*`: the frozen identities (INTENT §18), read back from the bundles: both
    standalone bundle IDs, the AU's three components and the VST3 class IDs. Run them before
    accepting any clap-wrapper update. `identity_vst3_preset_selector_inst` and `_fx` (with
    their setup test `identity_vst3_preset_content`, which packs the two fixture presets into a
    scratch content root) read the built VST3's program-change parameter through the raw
    vtables: present, with at least INIT and the product's own preset in its list (min steps 2).
    The wrapper publishes the list from a background crawl, so the executable makes new instances
    every 0.5 s (20 s cap) until the count suffices.
  - `gg-au-state-tests` (labels `plugin` and `au`): loads the built `.component` into its own
    process (registered with `AudioComponentRegister`; nothing is installed) and checks that
    ClassInfo carries the CLAP state (GGST) and round-trips it for all three AU identities. It
    also pins clap-wrapper's known AU state faults (T7 223). Run it (`ctest --preset dev -L au`)
    before accepting any clap-wrapper update too. Its "the au lists the factory presets and loads
    one" case checks `kAudioUnitProperty_FactoryPresets` against a content root of its own and
    loads one preset by `PresentPreset`.
    - It runs on the native slice only: on CI's mac-universal leg it loads the AU natively
      (arm64), never under Rosetta like the x86_64 slice's `gg-tests`. Keep it so when the macOS
      leg changes, or the AU state goes unchecked there.
    - Building `gg-tests` builds the AU bundle too (`add_dependencies(gg-tests
      gg-au-state-tests)`), so a tests-only build takes longer.

**CI's mac-universal leg must keep running `gg-tests` natively (arm64)** as well as the x86_64
slice under Rosetta. The fp-contract tests (`fp_contract_fast.cpp`'s canary and the kernel cases)
can fail only on arm64 Clang; Linux, Windows and the x86_64 slice can't catch a fused
multiply-add. Keep that step whenever the macOS leg changes.

**The x86_64 slice under Rosetta** (CI's macOS leg; INTENT §23 #75) runs `gg-tests` in three
Catch2 shards at once (`--order decl --shard-count 3`, so they're disjoint), then the `[serial]`
cases alone, then all 123 golden cases (the 75 and the 48 preset goldens, whose packed presets
the native `ctest` built) three `gg-render` processes at a time, each with `--content-dir` set to
the fetched content. On a pull request and a
push to `main` it leaves out `[slow]`; the nightly, a dispatch and a tag run it all. To reproduce a
shard locally: `arch -x86_64 build/mac-universal/tests/gg-tests '~[serial]~[slow]' --order decl
--shard-count 3 --shard-index 0`.

**Test case names are ASCII only** (tags too). CTest passes a name to `gg-tests` on its command
line, and on Windows a non-ASCII character there reaches Catch2's narrow `main` through the ANSI
code page, so the name matches nothing and the test fails with "No tests ran". Spell `×` as `x`,
`−` as `-`, and so on; comments and strings inside a test may use any UTF-8.

**Tests run in parallel.** Every test preset runs 8 tests at a time (`"jobs"` in the hidden
`base` test preset); `-j N` on the command line overrides it, and `-j 1` runs them serially.
- Each Catch2 test case runs in its own process.
- **ThreadSanitizer** (no preset, no CI job): `tests/tsan-suppressions.txt` holds one suppression,
  the ring reader's designed reader/writer slot race (`race:gg::media::ringToBuffer`; a seqlock
  validates what it keeps; carry-over row 403). A local TSan tree runs with
  `TSAN_OPTIONS="suppressions=$PWD/tests/tsan-suppressions.txt"`.
- **Keep new tests parallel-safe:**
  - never write a path that another test writes: unit tests' scratch files go in
    `GG_TEST_OUTPUT_DIR` under a name of their own, and each golden case writes its own files;
  - never touch the user's per-machine settings file. Both products read it (since M2
    sub-project 2 both products' media; both products' preset catalog, which also writes its
    `contentFolder`; and since M2 sub-project 6 both products' editor, for its theme: the `theme`
    attribute, `plugin/src/editor/UiSettings.cpp`, which reads the variable at each call, so a
    test may switch files between cases) from the file `GG_SETTINGS_FILE` names, when it is set
    and not empty, instead of the user's; `tests/plugin/PluginHarness.h` points it at a scratch
    file, and a test that reads or writes the settings points it at its own (`useSettingsFile`)
    before it creates the instance or the editor;
  - never write the user's captures folder (`~/Music/Giga Grains/Captures` by default): a test
    that saves rings or captures uses `useMediaScratch(name)`, a settings file of its own whose
    captures folder is in its scratch directory, and `gg-render --save-state` files audio only
    in the `--captures-dir` it is given;
  - never see the machine's installed content: a test that lists presets or content roots sets
    `GG_SYSTEM_CONTENT_DIR`, the test seam in `media/include/gg/media/ContentPaths.h` that
    replaces the per-user and system install roots with one folder (a scratch one), as the
    catalog, browser, discovery and AU tests do;
  - a test that has to share a file, or that measures time, must not run beside the others. For
    an `add_test` in `tests/CMakeLists.txt`, set its `RESOURCE_LOCK` or `RUN_SERIAL` property.
    Catch2 cases are discovered after the build, so `set_tests_properties` can't reach them.
    Tag the case `[serial]`: `tests/CMakeLists.txt` discovers those cases separately and leaves
    them out of the main discovery:

    ```cmake
    catch_discover_tests(gg-tests TEST_SPEC "~[serial]" ADD_TAGS_AS_LABELS)
    catch_discover_tests(gg-tests TEST_SPEC "[serial]~[crawl48]" ADD_TAGS_AS_LABELS
            PROPERTIES RUN_SERIAL TRUE)
    catch_discover_tests(gg-tests TEST_SPEC "[crawl48]" ADD_TAGS_AS_LABELS
            PROPERTIES RUN_SERIAL TRUE FIXTURES_REQUIRED presets_built)
    ```

    The third is the preset crawl, serial and requiring the `presets_built` fixture.

**Golden renders (`gg-render`).** `gg-render` renders a render spec offline and deterministically:

```
gg-render <spec.json> [-o out.wav] [--metrics out.json] [--events out.jsonl]
          [--spectrogram out.png] [--compare golden.metrics.json]
          [--compare-events golden.events.jsonl] [--block-size N] [--media-dir DIR]
          [--preset file.ggpreset] [--content-dir DIR] [--captures-dir DIR]
          [--save-state out.ggst]
gg-render --write-media DIR
gg-render --pack-preset <source.json> -o <out.ggpreset> [--content-dir DIR]
gg-render --mutate-preset <in.ggpreset> -o <out.ggpreset> --amount A --counter N
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
    `"Macro Feedback"` (Macro 7 in both products) and `"Macro Mix"`; the extremes' (M2
    sub-project 4) are a layer's `"A Smear"`, `"A Bus Split"`, `"A Bus 1 Filter"` (0 Off, 1
    Low-pass, 2 High-pass, 3 Band-pass, 4 Notch), `"A Bus 1 Cutoff"` (log2 Hz), `"A Bus 1
    Resonance"` and bus 2's three (`"D Bus 2 Resonance"` last), and the globals `"Delay Amount"`,
    `"Delay Time"`, `"Delay Feedback"`, `"Delay Damping"`, `"Delay Ping-pong"`, `"Limiter Mode"`,
    `"Limiter Ceiling"` and `"Limiter Release"`; `"A Source": 3` selects the Spectral kind, which
    smears the layer's `samples` entry when it has one and the render's input ring otherwise;
    the layers' are
    `"Morph X"`, `"Morph Y"`, `"Layer A On"` … `"Layer D On"` and `"Level B"` … `"Level D"`.
    Since M2 sub-project 3 each layer has `"A Quality"` … `"D Quality"` (0 Eco, 1 Normal, 2 HQ;
    bare `"Quality"` means layer A's) and there is a global `"Budget"` (a count of Normal grains,
    256 to 16,384, default 4,096): 223 parameters in all, 283 with M2 sub-project 2's twenty ring
    parameters and M2 sub-project 4's forty, 290 with M3 sub-project 1's four parameters,
    seven slots (Mode in each of four layers, plus MPE, MPE Bend Range and Bend Range), 302 with
    M3 sub-project 3b's three parameters, twelve slots (66 per layer): a layer's `"A Scan Speed"`
    (the CLAP value v in -1 to 1, speed 8 v^3, so 0.5 is 1x), `"A Scan Edge"` (0 Wrap, 1 Bounce, 2
    Stop) and `"A Scan Track"` (0 Off, 1 On), and B to D's, twelve names, none bare (only layer
    A's eleven M1 names are).
    Sample mode takes `"quality": "hq"` beside `"eco"` and `"normal"`, and sets no quota.
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
      `modWheel`, `aftertouch` and `pitchBend`, and since M3 sub-project 1 `sustain` (CC 64; 0.5
      and above is down), `timbre` (CC 74) and `polyPressure` (which alone needs a `"key"`),
      values 0–1, sent as MIDI control events; `aftertouch` and `pitchBend` keep their channel,
      so MPE as MIDI renders. A route in `mod` takes an optional `"voiceOnly"` (skipped on Cloud
      layers, per voice on a Voice layer). A note in `notes` takes optional `"id"` (its CLAP note
      ID, from 0), `"releaseVelocity"` (0–127) and `"expressions"`, a time-ordered list of
      `{"time", "kind", "value"}` with kinds `tuning` (semitones), `pressure`, `brightness` and
      `volume`, sent as CLAP note expressions on that note (a note with expressions and no `id`
      is given 1,000,000 plus its index); `params` takes `"A Mode"` … `"D Mode"` (0 Cloud, 1
      Voice), `"MPE"`, `"MPE Bend Range"` and `"Bend Range"`.
    - `transport` is `{"tempoBpm", "playing", "startBeat"}`, a steady transport set once before
      the render (playing by default, from beat 0); without it the transport sources hold at 0.
    - `preset` (M2 sub-project 5's spec §6.10) is a `.ggpreset` file's path, resolved like a
      file's. It is applied first: an FX preset in an instrument spec, or the reverse, is bad
      input (exit 2: "the preset is for Giga Grains FX, not Giga Grains"); then its values and
      routes replace the defaults and the factory routes, and the spec's `params`, `mod` and
      `samples` go on top. Its seed is ignored: the spec's is the render's. So are its ring
      parameters (Ring Length, Loop, Loop Length, Overdub, Splice), as a plugin's preset load
      keeps the instance's; the spec's `params` may still set them. Its samples, named by their
      content-relative references, are loaded from `--content-dir` alone (below), before the
      spec's `samples`; an instrument spec may leave out `sample` beside a `preset` that names
      layer A's, which is then layer A's (one that names none is bad input: no sample).
  - **Sessions and ring operations** (M2 sub-project 2's spec §6.10; INTENT §19), each optional
    in FX and instrument mode, and unknown keys in sample mode:
    - `state` is a `GGST` file (a relative path as a file source's), restored before the render
      as the plugin's `stateLoad` would: values, routes, samples, rings and the memory cap;
      `params` and `mod` then override it. Its samples are found only by content path under
      `--content-dir`, then in the spec's directory (or `--media-dir`), never searched by name,
      and its filed rings in `--captures-dir`. The spec's seed is the render's, not the state's.
      An instrument spec may leave out `sample` beside `state`; layer A's is then the state's.
    - `operations` is a time-ordered list of `{"time", "layer", "op", …}`: `splice` (a mark
      `behind` seconds behind the window's end (the head, or a selected splice's end), default
      0), `clearSplices`, and `capture` (`splice` 0
      for the loop window or k for its k-th span, `into` a layer letter whose sample it becomes,
      and/or `file`, written as a float32 WAV). A capture's `file` is used as written, so a
      relative one is relative to the working directory, like `-o`, not to the spec.
      `gg-render` cuts its calls at a capture's frame, so it lands on that sample at any block
      size; a splice or clear is a command event.
    - `memoryCapMiB` (64–16384; the state's, or 1024) sets the cap; a ring or sample past it is
      bad input (exit 2).
    - The ring parameters are `"A Ring Length"` (log2 seconds: 4.0 is 16 s; set in `params`
      only, an error in `automation`, since it isn't automatable), `"A Loop"`, `"A Loop Length"`
      (log2 seconds), `"A Overdub"` (0–1) and `"A Splice"` (0–32), and B to D's; a Resample
      layer is `"B Source": 2`.
    - `--save-state out.ggst` writes the session at the render's end: the values, routes, cap,
      file samples by reference, captures, and the rings of Live or Resample layers whose Freeze
      or Loop is on (their loop window, with the marks inside it), embedded up to 32 MiB and filed past it as
      `<sha256>.wav` in `--captures-dir` (which it then needs). A built-in signal sample has no
      file, so it isn't saved. Sample mode with `--save-state` is bad input.
  - **Instrument mode** (`"mode": "instrument"`) runs Giga Grains' `Processor` the same way over
    a `sample` (a built-in signal or a `file`, loaded and converted with `gg-media`; optional
    beside `state`), with no `channels`. Its Position is the fraction of the sample, 0–1, and
    it is silent until a note.
    It takes Dry to Reverb, which does nothing there, and Feedback, which acts only on a layer
    whose Source is Live or Resample (an instrument spec has no input, so a Live layer's ring
    holds silence; a Resample layer's hears the instrument's output). Its Macro 8 is
    `"Macro Level"` (Macro 7 is `"Macro Feedback"`, as in the FX; `"Macro Stereo"` names
    nothing since M2 sub-project 1): a spec takes only its own product's names, so
    `"Macro Level"` in an FX spec, or `"Macro Mix"` in an instrument spec, is an unknown key.
  - **FX inputs are faded** in at their start and out at their end over 10 ms, with a raised
    cosine (sin²), so the input's own edges don't click (ruling M1-v). Sample-mode sources and
    the M0 goldens are untouched.
- **`--preset FILE`** sets the spec's `preset`, or overrides it, and is resolved as the key is
  (so a relative one from the spec's directory or `--media-dir`). **`--content-dir DIR`** is a
  content root: a restored `state`'s samples are found under it by content path, and a
  preset's content-relative samples under it alone (never searched; spec §6.4). A preset that
  names a sample is bad input (exit 2, naming the reference) without `--content-dir`, when the
  file isn't under it, or when its size or SHA-256 isn't the preset's.
- **`--pack-preset`** compiles a preset source (`tools/gg-render/PresetSource.h`: `product`,
  `name`, `tags` from the vocabulary, optional `author` and `description`, and `params`, `mod`
  and `samples` in the spec's vocabulary) into the preset file `-o` names, the product's defaults
  and factory routes under the source's. It takes no other option but `--content-dir`, and a
  bad source exits 2 with the parser's message. Each of its `samples` (content-relative paths)
  is checked under `--content-dir`, which a source naming any needs, and recorded in `MEDI`
  version 2 as its reference with the file's fingerprint, format, channels, rate and frames (no
  absolute path). A source may not set a ring parameter (exit 2, naming it): a preset load keeps
  the instance's. `tests/fixtures/presets/` holds the test sources (`fx-test.json`,
  `inst-test.json`, and `fx-sample-test.json`, whose layer A names `loop-48k.wav` under
  golden_media's folder).
- **`--mutate-preset`** (the listening pack's Mutate ladder, M2 sub-project 5's spec §11) writes
  the preset file it reads, of either product, with its values moved by one Mutate press: the
  plugin's `gg::core::mutate` with seed 0, the default locks (`defaultMutateLocks`), `--amount`
  (0 to 1) and the press `--counter`; its name, tags, routes and samples unchanged
  (`mutatePreset` in `tools/gg-render/PresetSource.h`). It takes no other option; the same input,
  amount and counter give the same bytes, amount 0 gives the input back, and a bad amount (NaN
  included) or counter, or a file that isn't a preset, exits 2 (`render_mutate_preset*`).
- Its exit status is 0 for a pass, 1 when a comparison fails, 2 for bad input and 3 for an
  internal error.
- A relative file path is taken from the spec's directory, or from `--media-dir`. The golden
  cases' media are generated, never committed: `--write-media DIR` writes them, and the
  `golden_media` test does so into `build/<preset>/golden-media` as a fixture every golden test
  requires. Since M2 sub-project 2 they include `sine-330.wav`, `noise-2s.wav` and two session
  states rendered by `gg-render` itself, `frozen-ring.ggst` (a 20 s ring frozen with two
  splices, embedded) and `four-samples.ggst` (four Sample layers by content path, no machine
  path in it), the same bytes on every run on one machine.
- The `content_fixture` test (label `golden`, fixture `content`) fetches the pinned CC0 content
  into the build tree (see "Setup"), for the cases and tests that read it.
- **The cases:** each case's spec is in `tests/golden/cases/`, and its expected metrics and event
  log are in `tests/golden/expected/`. Every `*.json` there is a case; a new one gets a test the
  next time CMake runs. There are 75, and 48 preset goldens beside them (below): 123.
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
      to the center), `inst-pitch-spread` (one note over a sine, A Pitch Spread 0, 2, 7, 12, 24
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
  - **M2 sub-project 3's seven** (the engine; spec §6.9), 3 to 8 s each:
    - `sparse-sine-hq` and `sweep-stretch-hq`: M0's `sparse-sine` and `sweep-stretch` at HQ
      (sample mode, `"quality": "hq"`);
    - `fx-hq-pitch-ladder`: layer A at HQ reading a 20 kHz to 50 Hz sweep as its Sample, Pitch
      stepped 0, +12 and +24, then two notes for +36 and +47 (mip levels 0 to 3, stretched at the
      top). The FX's own 5 s input is never heard: layer A reads its Sample at Mix 1;
    - `fx-quality-switch`: Quality stepped Eco, Normal, HQ, Normal at 1 s intervals over a 330 Hz
      sine at Pitch +7;
    - `fx-overload`: four layers over a 330 Hz sine, A and B at HQ, C Normal, D Eco, Budget 4,096,
      1,024, 256 and 4,096, layer D off at 5 s and on at 5.5 s. Its event log is 1.6 MB;
    - `inst-overload`: two notes, one HQ layer of 1 s grains at Density 400 Hz, Budget 256
      (sustained stealing);
    - `fx-hq-live`: layer A Live at HQ, Pitch +12, over a sweep; the ring's level 1 serves the
      grains, so every line reads `"lvl":1` (sub-project 2's Task 12 regenerated it from
      `fx-hq-live-level0`, whose lines read level 0).
    - Two hide some injected click steps, pinned in the click-injection table: `fx-hq-live`
      catches 10 (the ring's sweep read an octave up hides the steps from about 2.1 s) and
      `fx-hq-pitch-ladder` 3 (content near Nyquist at every step). The other five catch all 16.
    - Every one of the 36 earlier cases' WAV SHA-256 was unchanged through every task; their event
      logs were rewritten once, by inserting `"tier"` and `"lvl"`. A change that moves one WAV is
      a regression.
  - **Its follow-up's one** (carry-over row 300; INTENT §23 #66, amended): `inst-sample-end`, the
    instrument over a 2 s 660 Hz sine at Position 0.97, 200 ms grains at 8 Hz, notes at rates
    0.5, 1, 1.5 and 2. Every grain reaches the sample's end inside its window and fades out with
    the raised cosine (1.94; 36.03 with the old hard cut). It catches all 16 injected steps. The cut grains' window
    phases at the sample's end run from 0.027 to 0.97. Its grains are all Normal-tier with no
    short spans (none has fewer than 256 samples to its cut), and the cut is invisible to its event
    log (`dur` is unchanged), so its cross-platform check is its metrics alone: Eco, HQ and short
    spans rest on the kernel and engine tests.
    - The follow-up made the steal fade a raised cosine too (EF-8), which moved `fx-overload` and
      `inst-overload` (WAVs and metrics; their event logs hold no fade shape). Every other case,
      none of which steals or reaches its sample's end, kept its WAV SHA-256.
  - **M2 sub-project 2's seven** (sources and persistence; its spec §10), 4 to 24 s:
    - `inst-resample-feedback` (the instrument: layer A Sample over a 220 Hz sine, one held
      note, layer B a Resample layer at Pitch +12 with Feedback 0.6), `fx-resample` (a stereo
      sweep, layer B Resample at Pitch -12, Position 0.5 s), `fx-long-ring` (24 s: Ring Length
      60 s, an 8 s noise burst, then Position stepped to 10, 18 and 22 s behind at 12, 18 and
      22 s), `fx-loop-overdub` (`loop-48k.wav`, Loop on, Loop Length 1 s, Overdub 0.7, Freeze at
      4 s, Overdub to 0 at 6 s), `fx-splice-capture` (a sweep, Freeze at 2 s, two splices 1.5 and
      0.5 s behind, Splice 1 at 2.5 s and 2 at 3.5 s, splice 2 captured into layer B at 4 s,
      layer B Sample at Pitch +7; Size 50 ms, Density 32 Hz), `fx-ring-restore` (`state:
      frozen-ring.ggst`, a 20 s ring frozen with two splices, embedded; restored with Freeze on
      and scanned for 4 s: the restore gate) and `inst-four-samples` (`state: four-samples.ggst`,
      four Sample layers by content path under `--content-dir`, one held note).
    - Five hide some injected click steps, pinned in the click-injection table: `fx-loop-overdub`
      12 (the bursts' own edges), `fx-long-ring` 4 (an 8 s noise burst held in the ring, a noise
      bed everywhere but the silence from 8 to 12 s), `fx-splice-capture` 12 (the sweep's top
      octaves, to 2 kHz and +7 semitones, under layer A's dense cloud hide the four steps after
      4 s), `inst-four-samples` 3 (one layer is noise: a noise
      bed under the note) and `inst-resample-feedback` 6 (the cascade of octaves fills the
      spectrum). `fx-resample` and `fx-ring-restore` catch all 16.
    - `fx-splice-capture`'s two Splice changes fall under sounding grains (Density 32 Hz of
      50 ms grains), so it gates Task 12a's window fix (ruling SP-T12aa): a window change never
      clicks.
    - 43 of the 44 earlier cases (the 36 before sub-project 3, its seven and its follow-up's
      one) render bit-identically with Ring Length 16 s, Loop off, Overdub 0, no splice, no
      Resample layer, nothing embedded and the cap at its default: a sources change that moves
      one is a regression. The 44th, `fx-hq-live`, moved with the ring pyramids (Task 12, which
      regenerated it from `fx-hq-live-level0`); a later level-selection change names any case it
      moves.
    - The event log sees neither the tap's audio nor a restored ring's frames, only the grains
      that read them, so across platforms the metrics compare those.
  - **M2 sub-project 4's nine** (the extremes; its spec §10), each 6 to 8 s:
    - Smear: `inst-smear` (one note over noise at a static Position, Smear stepped 0, 0.25, 0.5,
      1) and `inst-stretch-smear` (`loop-48k.wav` at a 1000x stretch through LFO 1 to Position,
      Smear 0 for 4 s, then 1);
    - the Spectral kind: `inst-spectral` (layer A Spectral over a sweep sample, Position stepped
      0.1, 0.4, 0.7) and `fx-spectral-freeze` (layer A Spectral over `loop-48k.wav` as the input
      ring, Freeze on from 2 s to 4 s);
    - the buses: `fx-bus-filters` (stereo noise, Bus Split 50%, a low-pass and a high-pass, the
      cutoff and a type automated) and `inst-filter-lfo` (a note over noise, LFO 1 to Bus 1
      Cutoff, a route to a new target);
    - the delay and the limiter: `fx-delay` (the loop, ping-pong), `fx-delay-automate` (Time's
      glide, Feedback to 90%, Amount to 0) and `fx-limiter` (four layers at +6 dB, Mode Off, Soft
      at 2 s, Peak at 4 s, Ceiling -6 dB).
    - `fx-limiter`, `inst-smear` and `fx-bus-filters` (a noise bed) hide most injected steps,
      and four more hide some, pinned in the click-injection table: `inst-filter-lfo` 13,
      `inst-stretch-smear` 9, `fx-delay` 12 and `fx-spectral-freeze` 9. The other two catch
      all 16. `tests/metrics/comb_tests.cpp` renders Smear 0 and 1 over noise, static and at
      1000x, and requires Smear 1's cepstral comb score well below Smear 0's.
    - Every one of the 51 earlier cases renders bit-identically with Smear 0, Bus Split 0, the
      filters Off, the delay at Amount 0 and the limiter Off: an extremes change that moves one
      is a regression.
  - **M3 sub-project 1's eight voice goldens** (the Voice slice; its spec §4.9), each
    catching all 16 injected steps (no table entry):
    - `inst-voice-chord`: a pedaled C-major chord at Mode Voice over a 220 Hz sine, the pedal up
      at 3 s, the release still spawning to about 4 s;
    - `inst-voice-release`: one note, Env 1 Release 2 s, Density 32 Hz, the stream through the
      release;
    - `inst-voice-mpe`: three MPE notes on channels 2 to 4 (MPE On) with bend slides, pressure
      swells and CC 74 moves, as MIDI;
    - `inst-voice-expr`: the same gestures as CLAP note expressions; its event log equals
      `inst-voice-mpe`'s with `i` dropped, and its WAV is byte-identical (a test in
      `tests/core/voice_processor_tests.cpp`);
    - `inst-voice-steal`: 20 notes held at once on one layer, four voice steals, no click;
    - `inst-voice-dup`: two notes on one (channel, key) with IDs 1 and 2, released one at a time
      (row 184);
    - `fx-voice`: the FX, layer A Voice over a live 330 Hz sine at amplitude 0.1, a three-note
      chord (at 0.5 it saturated before V7's headroom, carry-over row 411);
    - `inst-voice-layers`: layers A Voice and B Cloud (a sweep sample, +7) together, three notes.
    - V7's headroom (`1 / sqrt(n)` per voice grain) regenerated seven of them (all but
      `inst-voice-release`); every one of the 108 earlier cases' WAV SHA-256 was unchanged: a voice
      change that moves one is a regression.
  - **M3 sub-project 3b's seven playhead goldens** (the playhead; its spec §4.9), 5.5 to 6.5 s
    each at block 256, seed 1:
    - `inst-scan-voice`: layer A Voice over a 4 s sweep sample, Scan Speed 0.5 (1x), Wrap, three
      notes staggered 0.75 s, each voice playing through from Position on its own;
    - `inst-scan-stretch`: one held note over `loop-48k.wav`, Scan Speed 0.0625 (1/512x), then
      -0.25 (-1/8x) from 3 s;
    - `inst-scan-edges`: one note over a 2 s sweep sample at Scan Speed 0.75 (3.375x), Wrap, then
      Bounce at 2 s, then Stop at 4 s;
    - `inst-scan-track`: Scan Track On, Scan Speed 0.5, notes C3, C4 and C5 (0.5x, 1x, 2x);
    - `inst-scan-cloud`: the instrument at Mode Cloud, Scan Speed 0.5, two phrases with a silent
      gap: the playhead restarts at the second phrase's first note;
    - `fx-scan-ring`: layer A Live over a stereo sweep, Freeze at 1 s with Scan Speed -0.5 (Wrap,
      Bounce from 3 s), unfrozen at 4.5 s with Scan Speed -0.25 and Stop;
    - `fx-scan-spectral`: layer A Spectral over `loop-48k.wav`, Scan Speed 0.25: the smear's
      analysis point moves (the log shows Spectral grains only).
    - Five hide some injected click steps, pinned in the click-injection table with reasons
      marked "likely" (not isolated by experiment, carry-over row 427): `inst-scan-edges` 13,
      `inst-scan-stretch` 8, `inst-scan-track` 15, `fx-scan-ring` 15 and `fx-scan-spectral` 11.
      `inst-scan-voice` and `inst-scan-cloud` catch all 16.
    - Every one of the 108 M2-era cases and the eight voice goldens renders bit-identically at
      Scan Speed 0: a playhead change that moves one is a regression.
  - **M2 sub-project 5's 48 preset goldens** (its spec §10; labels `golden` and `presets`), one
    per factory preset, in their own folder: `tests/golden/presets/cases/<fx|inst>-NN-<slug>.json`,
    expected files in `tests/golden/presets/expected/`, tests `golden_preset_<case>`, each 6 s
    at block 256, seed 1. A case's `"preset": "presets/<Product>/NN Name.ggpreset"` resolves
    through `--media-dir <build>/golden-media`, into whose `presets/` the fixture
    `presets_built` (`tests/golden/presets/presets-built.cmake`: it builds `gg-presets`, after
    the `content` fixture) copies the packed presets, and their samples come from
    `--content-dir`, the fetched content (`GG_CONTENT_ROOT`). The FX's 24 run over the
    generated sweep (22) or `loop-48k.wav` (18, 21), the eight `midi` ones with notes at 2.0
    and 3.5 s; the instrument's 24 play a two-note phrase (0.25 and 1.5 s) and a chord at 3.0 s,
    in each preset's register. They don't join the click-injection table: in its place
    `tests/metrics/preset_goldens_tests.cpp` requires each expected click score under 15 (and
    peak under -1 dBFS, nothing non-finite or silent), and the cases and the sources to match
    one to one. Their click scores run 0 to 8.8 (`inst-23-locust-cloud`). A preset source's
    change moves its golden; a change to `tools/`, `core/` or `plugin/` that moves one of the 60
    other cases' WAVs is a regression as before.
  - **The event-log comparison never sees the reverb or the diffuser:** the log records grains,
    and Space runs after them. Across platforms, Space's five cases are checked by their metrics
    alone, within tolerances.
- **The `golden` tests** render every case into `build/<preset>/golden/<case>.wav` and `.png`.
  - They compare the metrics within tolerances.
  - They compare the grain-event log exactly per line (CR and final newline ignored). That
    comparison is the cross-platform determinism gate. Each `.events.jsonl` line is one grain:
    `i`, `layer` (0–3, since M2 sub-project 1), `v` (a voice grain's ordinal, since M3
    sub-project 1; absent on Cloud lines, which are byte-identical), `onset`, `frac`, `pos`, `rate`, `dur`, `tex`
    (its Texture, since sub-project 5), `tier` (0 Eco, 1 Normal, 2 HQ, after the ladder), `lvl`
    (its latched mip level; both since M2 sub-project 3), `gain` and `pan` (`formatEventLine` in
    `tools/gg-render/Render.h`). A grain the overload ladder stole also has a line,
    `{"i":k,"layer":L,"steal":o}` (`formatStealLine`), right before the line of the grain it made
    room for, `o` that grain's onset sample. Across layers the lines are in piece, layer, spawn
    order, so how two layers' grains interleave depends on the block size (INTENT §19, ruling
    LS-T10b): each layer's own lines, and the audio, are block-size invariant, and each golden
    renders at its spec's block size. Compare logs across block sizes per layer.
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
  - It rebuilds `gg-render` and `gg-presets` (so the first run of a tree fetches the content),
    writes the media into `<build-dir>/golden-media` and the packed presets into its
    `presets/`, and rewrites the expected files of both case folders (the preset goldens' with
    `--content-dir`, `GG_CONTENT_DIR` or `<build-dir>/content-cache/content`).
  - It leaves WAVs and spectrograms in `<build-dir>/golden/` (the preset goldens' in
    `<build-dir>/golden/presets/`).
  - It then checks the new goldens.
  - Last, it prints each case's WAV SHA-256 (a preset golden's as `presets/<case>`) and lists
    the cases whose WAV changed, or are new or gone, since its previous run in that build tree (`<build-dir>/golden/wav-sha256.txt`). For a
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
$b --chain [--quick | --seconds S] [--rounds N] [--commit SHA] [--json out.json]  # FX units
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
    - `engine-live-long`: `engine-live`'s stream over a 300 s ring, Position 150 s and Spray
      300 s so the reads span it, warmed up for 300 s of audio so the ring is full: a
      minutes-long buffer's cache cost; report-only beside X, with no baseline entry yet (M2
      sources spec §6.11). Each of its three configurations holds a ~216 MB ring with its levels,
      about 650 MB in all, and gg-bench builds every configuration before warming any up;
    - `engine-full-pool`: admission when the pool is saturated (1 s grains ask for 10× its
      capacity);
    - `engine-layers`: four `engine-sample` engines (layers 0 to 3) sharing its 2,000 grains, 500
      each, rendered through `SerialRunner` and summed in layer order, as a host without a thread
      pool pays for four layers; report-only beside X (M2 layers spec §6.12);
    - `engine-layers-full`: the same with 2,000 grains in every layer, 8,000 in all; report-only
      beside X;
    - `engine-sample-hq`: `engine-sample` at the HQ tier (the 16-tap sinc, reading level 0), the
      HQ figure and the source of HQ's weight; `engine-sample-hq-up`: the same at Pitch +19,
      each grain reading mip level 1 at residual 1.5 (the `[bench]` cases check the tier and the
      level they admit); `engine-overload`: one engine at capacity 16,384 and a quota of 2,000
      Normal grains' worth, 1 s grains asked for at four times that rate, so the drop and steal
      rungs run steadily (ns per rendered grain-sample; a test pins `lowered == 0` beside
      `dropped > 0` and `stolen > 0`); all three report-only beside X (M2 engine spec §6.10);
    - `engine-sample-sse`: `engine-sample` with `EngineConfig::kernels` = `Sse` (the 4-wide
      kernels), run beside `engine-sample` (dispatched) in the same process and rounds, the
      fallback's report-only case. `gg-bench` prints a `kernels:` line (`8-wide (AVX2)` or
      `4-wide`) and its JSON records `kernelLanes` (8 or 4); `GG_FORCE_SSE` makes `engine-sample`
      run 4-wide too. `--grains` is bounded by `kMaxGrainCapacity` − 48 (16,336);
    - `engine-sample-smear`: `engine-sample` with Smear's position jitter (50 ms) and
      micro-detune (0.5 semitones); report-only, with no baseline entry yet (M2 extremes spec
      §6.10);
    - `engine-buses`: `engine-sample` with half its grains on the layer's second bus, rendered
      through the two-bus `Engine::process` into two pairs of buffers that are summed after;
      report-only, with no baseline entry yet;
    - `engine-spectral`: `engine-sample` with Spectral grains (Position 0.5, 1 s of Spray) over
      the Spectral Smear's ring, which the engine fills from the same noise as sample material,
      after a 2 s warm-up so the ring holds smear; report-only, with no baseline entry yet;
    - `engine-voices` (M3 sub-project 1): `engine-sample`'s stream split across the engine's 16
      voices in voice mode, each held at full level at 1/16 of the grain rate, so the admitted
      grains total X's; report-only, with no baseline entry yet;
  - `reference`: M0's 4-wide Hermite kernel, frozen (`renderHermiteSimd` in
    `tools/gg-bench/Kernels.h`). It is the CPU gate's yardstick. `Kernels.h` includes no engine
    header: it holds its own frozen copies of the Hann table and the readers, so no engine
    change can move it, and the `[bench]` case "the reference kernel's output is pinned" hashes
    its output. Never edit those copies to follow the engine;
  - M0's prototype kernels: `linear-scalar`, `hermite-scalar`, `sinc-scalar`, `linear-simd`
    and `hermite-simd`;
  - `hermite-simd-grainfilter` (M2 sub-project 4; `GrainFilterKernel.h`): `hermite-simd` plus
    a 1 kHz `CytomicSVF` per grain, INTENT §22's per-grain filter measurement, not an M0
    kernel.
- **Timing.** Every configuration warms up once. Then each of 5 rounds (`--rounds`) times every
  configuration once, for at least 2 s (`--seconds`; `--quick` is 0.2 s), in an order rotated by
  the round and reversed in odd rounds. `--quick` also caps every engine case's warm-up at 16 s
  of audio, the longest any case but `engine-live-long` uses, so only that case's quick run
  changes: it reads a partly filled ring, and its quick ratios don't compare with a full run's
  (ruling SP-T9a; the full-length run keeps the 300 s fill). The last of `--quick` and
  `--seconds` wins. Each result is the median, min and IQR over the rounds.
  The JSON (`--json`) keeps every run, the run length, the round count and `--commit`.
- **The ratio.** Each engine case's ns per grain-sample over `reference`'s in the same round,
  then the median and IQR over the rounds. Because the two run side by side in one process, the
  ratio cancels the machine's speed (though not its microarchitecture). `--cases` must include
  `reference` for any ratio to be printed.
- **Other options:** `--grains N` changes the grain count; `--cases a,b` runs only those cases
  (the default is all; `--help` lists them).
- **Run length:** the full default run is 66 configurations × 5 rounds × 2 s, about 11
  minutes of timing (36 and about 6 minutes before the four cases of M2 sub-project 3 joined, 48
  before M2 sub-project 2's `engine-live-long` and M2 sub-project 4's four), plus
  `engine-live-long`'s three 300 s warm-ups, a few minutes more of wall time; a CI leg's benchmark
  step took about 6 minutes before the layer cases added one; with the later cases, Linux's EPYC
  7763 spent 30 minutes on the warm-ups and round 1 alone (the nightly of 2026-11-02), so the step's
  limit is 60 minutes (INTENT §23 #73).
  `--canvas` times 240 frames after a 20-frame warm-up, or 30 with `--quick`.
- **`--canvas`** times the canvas's CPU renderer headless: a 3200 × 1200 canvas (1600 × 600 at
  2×) of four lanes, M0's stress workload at 4,096, 8,192 and 16,384 grains, each frame's
  generate, clear, splat and resolve. Since M2 sub-project 6 it times the shipped path, the
  banded resolve with the four layers' tints, and its JSON says so: `"lanes": 4` and
  `"resolve": "banded"` beside the M1 keys, so its figures aren't read as M0's or M1's one-tint
  resolve. The 8,192-grain figure is INTENT §16's UI budget check (at most 3 ms, idle, on the
  reference core). The `[bench]` case "the canvas bench reports four banded lanes" runs it at
  `--quick`'s length (about 15 s in `dev`). The live editor's own readout includes `drawImage`
  and the overlays, which the benchmark leaves out.
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
    published beside X (19.5% at block 256; `docs/benchmarks/`, INTENT §23 #60). The M2 close's
    idle re-measurement (`docs/benchmarks/2026-11-03-m2-*.json`) is 28.72 ns, 0.138%, and X
    19.4% at block 256 (the published headline stays M1's 19.5%); its canvas figure at 8,192
    grains is 2.85 ms.
- **`--chain`** times the units the extremes sub-project adds after the grains, per stereo
  sample on stereo noise at 48 kHz: two `BusFilter`s (Low-pass, 1 kHz, resonance 0.5), the
  `Delay` (375 ms, feedback 0.5, damping 0.3, ping-pong, amount 1) and the `Limiter` (Peak,
  ceiling 0.5, release 100 ms), and in the same rounds a second stage, `spectral` (row 372): one
  Spectral Smear source's STFT, active, over stereo noise as a sample with no grain reading it,
  in ns per stereo sample (`tools/gg-bench/Bench.h` lists every setting). It takes
  `--space`'s options and prints and writes as it does (its own JSON, `"benchmark": "chain"`,
  the stage under `"spectral"`).
  - It is report-only: no ratio, no baseline, no verdict. CI's benchmark step doesn't time it;
    ctest's `bench_chain_cli` only checks that it runs. Its exit status is as `--space`'s.
- **An option a mode doesn't take is bad usage** (exit 2, with the usage): `--space` and `--chain`
  take only the options shown above, `--canvas` only `--quick` and `--json`, and `--make-baseline`
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
  `tools/gg-bench/baselines/<preset>.json` exists, `--baseline`) on the nightly schedule
  (03:17 UTC), on `workflow_dispatch` and on a pushed `v*` tag. They print the output, and a
  verdict per ratio, in the step summary and upload `gg-bench-<preset>` (`gg-bench.json` and
  `gg-bench.txt`). On a pull request and a push to `main` each leg runs `--quick --rounds 1` as
  a smoke check only (INTENT §23 #73, #75): it checks no baseline, prints no verdict and uploads nothing, but
  a crash or exit 1 or 2 still fails the job. A full run is limited to 60 minutes a leg, the
  build job to 120: it outgrew 30 once M2's cases joined.
  - **Baselines are per CPU model.** GitHub assigns a leg different CPU models from run to run
    (seen so far: Linux on AMD EPYC 7763, 9V45 and 9V74 and Intel Xeon 8370C; Windows on EPYC
    7763, 9V45 and 9V74 and Intel Xeon 8370C). For unchanged code, ratios differ by up to ~20% between models and ~6%
    on one model. So a leg's baseline has one entry per model, keyed by gg-bench's `machine`
    string, and a run is checked only against its own model's entry. The committed baselines
    have entries for the mac leg's Apple M1 VM (three runs), and for the x86 legs, as the M2
    close remade them (2026-11-03, from the three full-length dispatches at `ca60f54` and the
    earlier ones at `630d740`, carry-over rows 304 and 305; the commit `cf37b83` has the
    comparisons):
    - `linux-gcc13`: EPYC 7763 from 2 runs at `ca60f54`, EPYC 9V74 from 1 run at `ca60f54`,
      EPYC 9V45 from 1 run at `630d740`;
    - `windows-clangcl`: EPYC 7763 from 1 run and EPYC 9V74 from 2 runs at `ca60f54`, EPYC 9V45
      and Xeon 8370C from 1 run each at `630d740`;
    - `mac-universal` is unchanged.

    The entries made at `ca60f54` carry 42 ratios, the kept ones at `630d740` 30. The x86 entries
    were first made at the AVX2 task from three full-length runs (INTENT §23 #66), and the
    six report-only cases (`engine-layers`, `engine-layers-full`, `engine-sample-hq`,
    `engine-sample-hq-up`, `engine-overload`, `engine-sample-sse`) have entries beside the four
    gated ones there, none on `mac-universal`; they stay report-only, as the whole gate does. A
    single-run entry carries the 0.10 floor tolerance (row 305 stays open for them, with row 144).
    A model with no entry gets no verdict (the summary says so) until later runs on it are
    added to the baseline.
  - **The gate is report-only until 1.0.0** (INTENT §19, §23 #44, #60, #61): exit 3 becomes a
    `::warning` and the step exits 0, when the output also holds gg-bench's `verdict: N of M`
    line (row 148); exit 3 without it fails the step. A crash, exit 1 or 2, or the timeout still
    fails the job.
    The `bench_baseline_*` tests pin exit 3 (a regression or a missing entry), exit 0, the
    no-verdict line and exit 1 for a run with no ratios.
  - The gate turns blocking at 1.0.0, after M5 (INTENT §23 #60, #61), with the published X. The
    0.x releases, public from 0.5.0's first beta, keep it report-only.
- **Refreshing a baseline** (`tools/gg-bench/baselines/README.md` has the format):
  1. Make three separate full-length CI runs of the same engine code: the nightly or
     `gh workflow run ci.yml --ref <branch>` (a pull request's runs and a push to `main` are
     `--quick` and upload no artifact). One run's IQR says nothing about the next run's
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

**The views as PNGs (`gg-snap`).** Since M2 sub-project 6, `gg-snap` draws the editor's views
without a window or a host:

```
gg-snap <out-dir> [--sizes 960x600,640x360]
```

- It's built with the editor (it's in `gg-all`), at `build/<preset>/tools/gg-snap/gg-snap`.
- **What it does,** for each product: it creates the plugin through its CLAP factory, activates
  it and creates the editor; it loads a generated 2 s sine into the instrument, turns layers B
  and C on, adds one route (LFO 1 to layer A's Pitch) and runs 200 blocks. Then, for each theme,
  view and size, it draws the editor with `createComponentSnapshot`.
- **What it writes:** `<product>-<view>-<theme>-<w>x<h>.png`, the product `fx` or `inst`, the
  view `perform`, `edit`, `mod` or `fx`, the theme `dark` or `light`: 32 files at the default
  sizes.
- **It never touches your settings:** it sets `GG_STRESS_GRAINS=2048` (below) and points
  `GG_SETTINGS_FILE` at a scratch file in the system's temporary folder, removed afterwards.
- **Exit status:** 0, 1 with a message on stderr, 2 for bad usage.
- **`snap_cli`** (labels `plugin` and `slow`, a 900 s timeout and `PROCESSORS 2`;
  macOS, with the editor) runs it
  into `build/<preset>/tests/test-output/snap_cli` and requires exactly the 32 names, each a PNG
  of at least 1,000 bytes. It takes about 66 s in `dev`, so `-LE slow` leaves it out.
- **CI's macOS leg** runs it after the tests and uploads the files as the `gg-snapshots`
  artifact. Only a tool failure, or no files at all, fails the job: the pictures are looked at,
  not compared.
- **The listening pack** commits the 960 × 600 set, 16 PNGs, in
  `docs/listening/m2-views/snapshots/`; its `README.md` says how to remake them. Regenerate them
  when a view changes, and look at every one before committing.

**The canvas's stress seam (`GG_STRESS_GRAINS`).** With `GG_STRESS_GRAINS=N` in the environment
when a canvas is made, it draws M0's synthetic workload of N grains over its four lanes
(`gg::ui::stressGrains`) in place of the grain queue. The canvas reads the variable once, at
construction; unset, empty, not a number or not positive means off, and N is capped at 2^20. Use
it to read the live frame readout at a known load in a standalone app or a host
(`GG_STRESS_GRAINS=8192`, as the GPU spike did, `docs/spikes/m2-gpu-canvas.md`); `gg-snap`
sets it, and the test "the stress seam draws four lanes" covers it.

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
- **On Windows,** CI runs clap-validator without `preset-discovery-crawl`,
  `-descriptor-consistency` and `-load`: 0.4.1-127 demands `/C:\...` for a FILE location, which
  no Windows file function opens (M2 carry-over row 233). Run all three on macOS and Linux.
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
  if grep -q Sbcx "$TMPDIR/auval-list.txt"; then
    auval -v aumu GGin Sbcx; auval -v aufx GGfx Sbcx; auval -v aumf GGfx Sbcx
  else echo "the registrar never listed Sbcx" >&2; fi
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
