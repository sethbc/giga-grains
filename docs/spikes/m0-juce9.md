# M0 spike: JUCE 9.0.4 and clap_juce_shim

*M0 Task 11, 2026-10-26. Asks whether the GUI side of the stack (sst-clap-helpers'
`clap_juce_shim` plus our `gg::ui::Editor`) works on JUCE 9.0.4 instead of the
8.0.16-security that Shortcircuit XT ships (INTENT §15, §22).*

## Outcome

**Pass, on macOS arm64 and then on every CI leg. The project moves to JUCE 9.0.4** (`libs/JUCE` at
`3fb5cbc0340758596dac39f7c8de47d562b4896e`, tag `9.0.4`). Before this it was pinned to
`ea77444750de45e036e0279bb0291144bf8e3688`, tag `8.0.16-security`.

All of the following is on macOS arm64:

- **Build:** succeeds with no source changes. It adds no warnings.
- **Tests and validators:** every test and validator passes.
- **Editor:** the live window looks right.
- **License:** the dual-license terms are unchanged. AGPLv3 is still an option.

JUCE 9 needs two changes, neither of them in `libs/`:

- **Linux CI installs `libxi-dev`.** On Linux, JUCE 9 includes the XInput2 headers (see "Linux"
  below).
- **`JUCE_USE_WEBP=0`, set in `ui/CMakeLists.txt`.** JUCE 9.0.4's libwebp SSE4.1 translation
  unit doesn't compile under Windows ClangCL (see "Windows" below; ruling T11-g).

**CI** (ruling T11-f: CI on all three legs was the real gate):

- Integration CI run 40442418294, on `c7e3a1a` before the WebP fix, passed `mac-universal`,
  `linux-gcc12` and the sanitizers, and failed `windows-clangcl` on the WebP issue.
- **CI run 40445561691, on `933bfda` with `JUCE_USE_WEBP=0`, passed on every leg.** That
  includes the first full `windows-clangcl` run on JUCE 9: the build, ctest, clap-validator,
  pluginval and Steinberg's validator. So the fallback to 8.0.16-security wasn't needed.
- The x86_64 slice of the `mac-universal` build was built in those runs but not run. JUCE 9 adds
  x86-only C translation units to the shim, libwebp's `*_avx2.c` and `*_sse41.c`; with
  `JUCE_USE_WEBP=0` they compile to nothing. Since the M0 final fix wave, the macOS leg also
  runs the x86_64 slice's tests and golden checks under Rosetta (INTENT §23 #33).

The x86_64 slice was also tried locally during integration:

- `cmake --preset mac-universal` built `gg-all` and `gg-products` with only the known VST3 SDK
  and `ld` warnings. Every product is `x86_64 arm64`, and the libwebp avx2 and sse41 units
  compiled. That was before the WebP fix; after it, see "Windows".
- Run under Rosetta (`arch -x86_64`), on the x86_64 slice:
  - `gg-tests` reported "All tests passed (15039 assertions in 75 test cases)";
  - pluginval at strictness 10 with GUI tests gave `SUCCESS`;
  - clap-validator reported 79 run, 51 passed and 0 failed.
- That is supporting evidence only. The x86_64 slice hasn't run on real Intel hardware.

## Method

- **Spike build:**
  - `git clone --depth 1 --branch 9.0.4 https://github.com/juce-framework/JUCE build/gg-juce9`.
    The tag resolves to `3fb5cbc0340758596dac39f7c8de47d562b4896e`.
  - `cmake --preset release -B build/juce9 -DGG_JUCE_PATH=<abs>/build/gg-juce9`
  - `cmake --build build/juce9 --target gg-all gg-products`
- **Baseline:** the same commit, built with `cmake --preset release` against JUCE 8.0.16-security,
  for the warning comparison.
- **Final build:** after `libs/JUCE` moved to 9.0.4, a clean `build/release` was rebuilt from the
  submodule, and every check below was run again against it.
- **Toolchain:**
  - macOS 27.0.1 on an Apple M5 Max. All the spike and final builds are **arm64 only**: the
    `release` and `dev` presets don't set `CMAKE_OSX_ARCHITECTURES`;
  - AppleClang 21.0.0.21000334;
  - CMake 4.4.4 and Ninja 1.13.2;
  - deployment target 11.0.
- **The binaries carry JUCE 9.** `strings` on the built `.clap`, `.vst3` and `.component` finds
  `JUCE v9.0.4`.

## Build

The build ran all 316 steps with 0 errors, in both `build/juce9` and the final clean
`build/release`.

The warning lines are identical to the JUCE 8.0.16-security baseline, apart from the build-dir
prefix. A `diff` of the sorted warning lines from the baseline and the final build is empty. All
of these warnings predate JUCE 9 and none comes from JUCE or our code:

- `vst3sdk/pluginterfaces/base/fvariant.h:51` and `:260`: `-Wnontrivial-memcall` (VST3 SDK
  3.8.0, fetched by clap-wrapper);
- `vst3sdk/base/source/fstring.cpp:3371`: `-Wimplicit-const-int-float-conversion` (VST3 SDK);
- `ld: warning: ignoring duplicate libraries: 'engine/libgg-engine.a', 'ui/libgg-ui-core.a'`.

Our own targets still build with `-Werror`. JUCE's headers reach them as `SYSTEM` includes
(`gg::juce-headers`), as before.

**Code changes needed:** none in `plugin/`, `clients/` or `libs/`. In `ui/CMakeLists.txt`, one
definition, `JUCE_USE_WEBP=0`, works around the ClangCL build failure (see "Windows"). The macOS
build didn't need it.

## Tests and validators

Run on the JUCE 9 builds, both `build/juce9` and the final `build/release` (both arm64). The
results were the same each time.

| Check | Result |
|---|---|
| `ctest` (full suite, release) | `100% tests passed out of 79` |
| clap-validator 0.4.1 `validate --exclude '^param-conversions$'` (ruling T8-c) | `79 tests run, 51 passed, 0 failed, 0 warnings, 28 skipped` |
| pluginval v1.0.4 `--strictness-level 10`, GUI tests on (Editor, Open editor whilst processing, Editor Automation) | `SUCCESS`, for both plugins (VST3 class IDs `…-44681f8a` and `…-fc6462ac`) |
| `auval -v aumu GGin Sbcx` | `AU VALIDATION SUCCEEDED.` |
| `auval -v aufx GGfx Sbcx` | `AU VALIDATION SUCCEEDED.` |
| `auval -v aumf GGfx Sbcx` | `AU VALIDATION SUCCEEDED.` |
| Steinberg VST3 validator (built from the VST3 SDK 3.8.0 that clap-wrapper fetches, as CI does) | `Result: 94 tests passed, 0 tests failed` |

These are the same counts as on JUCE 8 (T8-fix: clap-validator 0 failed, Steinberg 94/0).

The **Debug build** (`dev` preset) was checked too, because it turns on JUCE's assertions:

- it built with the same warnings;
- `ctest` gave `100% tests passed out of 79`;
- pluginval at strictness 10 with GUI tests gave `SUCCESS` on its VST3 (`build/dev/gg_assets/`),
  with no JUCE assertion output.

The auval warnings are also the same as on JUCE 8: "Recommended Property is not supported"
(tail time and bypass) on both FX identities, and on `aufx`, "AU implements
MusicDeviceMIDIEvent but is of type 'aufx'". Both are known; see Task 8.

## Editor spot check (Task 9 Step 5)

**Method:** live window screenshots (`screencapture -l <windowid>`). Screen Recording is
granted now, so no offscreen render was needed.

Both standalones were opened from `build/juce9/gg-products/`, and each window showed:

- **Giga Grains FX.app:**
  - the name "Giga Grains FX" and "version 0.0.1";
  - "MIDI note-ons: 0";
  - the canvas stress panel at 8192 grains in four lanes.
  - Two captures one second apart showed different grain fields. The readout went from
    "frame ms (avg 60): 2.33" to "2.52", at 1736 × 720 px. The panel animates.
- **Giga Grains.app** (instrument): the same layout. The readout said 2.15 ms.

This matches what Task 9 showed with JUCE 8's offscreen render. The live readings are no slower
than that render's 2.50 and 3.56 ms. The two aren't directly comparable: the live window paints
through CoreGraphics, while the offscreen render used JUCE's software renderer. On macOS this
readout counts UI-thread time only (Task 9 minor note).

No MIDI was sent, so the note-on counter was only seen at 0. The MIDI paths are covered by the
`[plugin]` tests and by the Task 12 host checks.

## License

JUCE 9.0.4's `LICENSE.md` dual-licenses the JUCE modules under the **AGPLv3** and the commercial
JUCE 9 licence. Each module header still says "You may also use this code under the terms of
the AGPLv3".

The dual-license terms are unchanged from 8.0.16-security. In the license text, the EULA link
changed from juce-8-licence to juce-9-licence. `LICENSE.md` also gained a paragraph saying the
JUCE team gives no legal advice or compliance confirmations.

The dependency inventory moved out of `LICENSE.md` into a new SPDX file, `JUCE.spdx.json`.
8.0.16-security has no such file; there, the dependencies were listed in `LICENSE.md` itself.
The SPDX file lists every JUCE module as `AGPL-3.0-only OR LicenseRef-JUCE-Commercial`.
Combining AGPL-3.0-only JUCE with our AGPL-3.0-or-later code is distributable under AGPLv3, as
before.

Comparing that inventory with 8.0.16's list in `LICENSE.md`, JUCE 9 adds third-party code:
LunaSVG and PlutoVG (MIT), libwebp (BSD-3-Clause), Opus (BSD-3-Clause) and a Chromium
`cubic_bezier` (BSD-3-Clause). All of these are AGPLv3-compatible.
The ASIO and AAX SDKs are `… OR GPL-3.0-only`, as before, and we don't build JUCE's ASIO or
AAX support. The license is compatible with INTENT §12.

**Correction (final review, ruling F4):** that didn't mean no ASIO code was built. Until the M0
final fix wave, `libs/CMakeLists.txt` turned on RtAudio's ASIO for the Windows standalone, and
RtAudio 6.0.1 compiles its own bundled copy of Steinberg's ASIO SDK files, which have no license
grant. ASIO is now off, and Windows no longer builds RtAudio (INTENT §15, §22, §23 #30).

## Linux

JUCE 9's `juce_gui_basics` now includes `<X11/extensions/XInput2.h>` when `JUCE_USE_XINPUT` is 1,
which is the default. It also loads `libXi.so.6` at runtime. JUCE 8.0.16 used neither.

The pinned `sst-githubactions/prepare-for-juce` action doesn't install `libxi-dev`, and the
ubuntu-22.04 runner toolset doesn't list it. So `.github/workflows/ci.yml` adds `libxi-dev` to
the Linux install step.

JUCE 9 also lists `libegl-dev` for `juce_opengl`. We don't use that module: the shim links only
`juce_gui_basics` and `juce_gui_extra`.

## Windows

Windows can't be built locally. JUCE 9's documented minimums are the same as 8's: VS2019 and
Windows 10 1607. CI's `windows-clangcl` leg checks it.

### The ClangCL failure (fix round 1, ruling T11-g)

Integration CI run 40442418294 failed on `windows-clangcl` in `clap_juce_shim`:

```
libs\JUCE\modules\juce_graphics\juce_graphics_libwebp_dsp_dec_sse41.c:71
  → libwebp/src/dsp/dec_sse41.c:23 → libwebp/src/utils/utils.h:133
  → clang\19\include\intrin.h:22 → x86intrin.h → immintrin.h:549
clang\19\include\rtmintrin.h(36,1): error : attribute 'target' multiversioning cannot be
  combined with attribute 'always_inline'
clang\19\include\intrin.h(166,6): message : function multiversioning caused by this declaration
```

**Cause** (a JUCE 9.0.4 bug that only affects ClangCL):

1. Under clang-cl, `JUCE_CLANG` is true, so JUCE wraps the libwebp SSE4.1 translation unit in
   `#pragma clang attribute push(__attribute__((target("sse4.1"))), apply_to=function)`.
2. Under `_MSC_VER`, libwebp's `utils.h` includes MSVC-mode `<intrin.h>`. The pragma tags those
   intrinsic declarations with `target("sse4.1")`.
3. The tagged declarations clash with the `always_inline` redeclarations in `rtmintrin.h`.

macOS and Linux never include `<intrin.h>`, so they pass. All four x86 units (decode and encode,
SSE4.1 and AVX2) use the same pragma pattern.

**Fix:** `ui/CMakeLists.txt` sets `JUCE_USE_WEBP=0` on `juce_graphics`' interface. That reaches
every translation unit that compiles or includes JUCE, with the same value everywhere. In the
dev build, all 36 such units get exactly one `JUCE_USE_WEBP=0`, and none gets `=1`. They include:

- the module sources compiled into `clap_juce_shim`;
- the shim's own sources;
- `gg-ui`;
- the plugin translation units, through `gg::juce-headers`.

Every libwebp unit is guarded by `#if JUCE_USE_WEBP && JUCE_INCLUDE_WEBPLIB_CODE`, so all of them
now compile to nothing. We never load WebP images, so nothing is lost.

**Verified on macOS:**

- **Shim library:** `nm` on the `mac-universal` `libclap_juce_shim.a` shows 0 `WebP`, `VP8` or
  `SharpYuv` symbols on x86_64 and on arm64. Before the fix it showed 831 and 767, and the
  `*_sse41.c.o` and `*_avx2.c.o` objects are now empty. The final `.clap` has none either.
- **dev:** builds, and `ctest` gives `100% tests passed out of 79`.
- **mac-universal:** builds with only the known warnings, and `ctest` gives `100% tests passed
  out of 79`.
- **Validators** on the `mac-universal` products:
  - clap-validator (with the exclusion): 79 run, 51 passed, 0 failed;
  - pluginval at strictness 10 with GUI tests: `SUCCESS`;
  - Steinberg VST3 validator: 94 passed, 0 failed.
- **Under Rosetta (x86_64):** `gg-tests` passes all 75 test cases, and pluginval gives `SUCCESS`.

**Upstream:** this is worth reporting to JUCE. A fix would either:

- skip the `#pragma clang attribute` wrapper under `_MSC_VER`/clang-cl, so that clang-cl relies
  on `/arch` or per-function target attributes; or
- include `<intrin.h>` before the push.

The director decides whether to file it. Once a fixed JUCE is pinned, `JUCE_USE_WEBP=0` can be
dropped, but only if WebP is ever wanted.

## Extra: sst-jucegui

INTENT §15 plans sst-jucegui widgets for M1, so the spike also checked it against JUCE 9.

sst-jucegui `main` (`22a89ca4e54179cbda0b8164e54e06dae5f30490`, 2026-10-14) built with
`-DSST_JUCEGUI_BUILD_EXAMPLES=TRUE -DCPM_JUCE_SOURCE=<build/gg-juce9>`:

- its component demo built against JUCE 9.0.4 with 0 errors and 0 warnings;
- it ran: its Markdown windows rendered correctly.

This isn't a pass criterion, but it removes the obvious M1 risk of moving off Shortcircuit XT's
JUCE version.

## Reproduce

```sh
git clone --depth 1 --branch 9.0.4 https://github.com/juce-framework/JUCE build/gg-juce9
cmake --preset release -B build/juce9 -DGG_JUCE_PATH="$PWD/build/gg-juce9"
cmake --build build/juce9 --target gg-all gg-products
ctest --test-dir build/juce9 --output-on-failure
```

Then run the Task 8 Step 5 validators and the Task 9 Step 5 spot check against
`build/juce9/gg-products/`. Now that `libs/JUCE` is on 9.0.4, the plain `release` preset builds
the same thing.
