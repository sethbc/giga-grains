# Giga Grains

Giga Grains is an open-source (AGPL-3.0-or-later) pair of plugins: *Giga Grains*, a granular
synthesizer, and *Giga Grains FX*, a granular effect, both built from one shared C++20 grain
engine as CLAP, VST3, AU v2 and standalone for macOS, Windows and Linux. Read
[`INTENT.md`](INTENT.md) first: it says what the project is for and how it is built.

**Status (2026-10-29):** M0 (foundations) and M1 (the Cloud core: both plugins make textures)
are done; next come the installers and release pipeline, then M2. Nothing has been released yet.
Each milestone from M2 on ships as a 0.x release (M2 as 0.2.0), and v1.0.0 follows M5
(INTENT §20).

To build,
fetch the dependencies with `git submodule update --init`, then configure, build and test with
CMake 3.22 or newer and Ninja (on Windows, Visual Studio 2022 with ClangCL: the
`windows-clangcl` preset). The first configure downloads the VST3 and AudioUnit SDKs.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

The plugins and standalone apps are built into `build/<preset>/gg_assets/`. To copy the plugins
into your user plugin folders after each build, configure with `-DGG_COPY_PLUGIN_AFTER_BUILD=ON`.
On macOS that copies the CLAP, VST3 and AU into `~/Library/Audio/Plug-Ins/`; on Linux it copies
the CLAP and VST3 into `~/.clap` and `~/.vst3`; on Windows it does nothing. On macOS each bundle
is signed ad hoc once it's built, before it's copied: strict hosts such as Ableton Live won't
load a bundle whose executable carries only the linker's signature. To sign with a real identity
instead, configure with
`-DGG_MACOS_CODESIGN_IDENTITY="Developer ID Application: <name> (<team id>)"`. That alone doesn't
make the bundles distributable: distribution also needs the hardened runtime, a secure timestamp
and notarization, which the release pipeline will add
(`docs/superpowers/specs/2026-10-29-release-pipeline-design.md`). The `codesign_*` tests check
every signature.

The other presets (`release`; the `asan` and `rtsan` sanitizer builds, which build the plugin
core without its editor or formats; `mac-universal`), the golden renders (`gg-render`), the
benchmarks (`gg-bench`), the plugin validators and the formatting script are described in
[`CLAUDE.md`](CLAUDE.md), under "Build and test".
