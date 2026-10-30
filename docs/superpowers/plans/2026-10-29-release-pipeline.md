# Installers and Release Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A pushed `v*` tag builds, tests, validates, signs, packages and publishes a draft GitHub Release with a notarized macOS DMG, a Windows installer and zip, a Linux tarball, the corresponding source and their SHA-256s; one local command builds the same installers unsigned.

**Architecture:** Versioning moves into CMake (`0.2.0` plus a pre-release suffix, `dev` by default). A `gg-installer` CMake target, after `gg-products`, runs one packaging script per platform from `installer/`; the macOS script signs, notarizes and staples when identities are in the environment and falls back to unsigned otherwise. `ci.yml` grows a `plan` job, a conditional installer step per build leg and a `release` job; pull requests and pushes to `main` run exactly today's steps.

**Tech Stack:** CMake 3.22 scripts (`-P`) and custom targets; bash 3.2-compatible shell scripts (the macOS runner's bash is 3.2); `pkgbuild`, `productbuild`, `codesign`, `xcrun notarytool`, `xcrun stapler`, `spctl`, `hdiutil`; Inno Setup 6 (`ISCC.exe`, preinstalled on `windows-2022`); GitHub Actions with `gh release`; CTest labels `installer`, `scripts`, `build`.

**Spec:** `docs/superpowers/specs/2026-10-29-release-pipeline-design.md` (read it first; decisions 1–11 and §6 are the authority). INTENT §19 "CI and release pipeline" and "Versioning" already state the decisions (docs pass of 2026-10-29, §23 #61).

## Global Constraints

- Only AGPLv3-compatible code; anything adapted from sst-plugininfra's `make_installer.sh` or sst-cmake's `installer.iss` (both MIT) keeps its copyright notice at the top of our file.
- No secret, certificate, key, password or team ID in the tree, in a workflow file or in a log. Workflow steps name secrets; scripts take them from the environment or from a file path; nothing echoes them.
- Every third-party GitHub Action is pinned by commit SHA with its tag in a comment (`ci.yml` house rule). Prefer `gh` and shell over a new action.
- Shell scripts run on bash 3.2 (no `mapfile`, no associative arrays, no `${var,,}`), with `set -euo pipefail`, and carry the repository's SPDX header. Windows-side packaging is CMake `-P`, never bash.
- Product names contain a space (`Giga Grains.clap`, `Giga Grains FX`); every path is quoted.
- CTest names are ASCII; new tests are parallel-safe (own scratch dirs under `${CMAKE_BINARY_DIR}`); a test that builds a target is a `FIXTURES_SETUP` with `RUN_SERIAL TRUE`.
- The installers pack what `gg-products` holds: no re-linking, stripping or re-signing with another identity in the packaging step.
- Nothing in the audio path changes: `scripts/update-goldens.sh` before and after, every WAV SHA-256 identical.
- The branch ruleset `main: required CI` requires the job titles `build (mac-universal)`, `build (windows-clangcl)`, `build (linux-gcc12)`, `sanitizers (asan, rtsan)` and `clang-format`: keep them unchanged.
- `scripts/check.sh` (dev, asan, rtsan) and `scripts/format.sh --check` pass before every commit that touches C++ or CMake; CI runs via a draft PR.
- Commit messages end with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`; the implementer may use its own model's co-author line instead.

## Review Focus

1. **A tag whose version disagrees with CMake, or whose suffix is `dev`:** the run must stop in `plan`, before any leg signs. Pinned in Task 2's tests and Task 10's `plan` job.
2. **A product missing from `gg-products`** (a headless tree, or a renamed bundle): each packaging script exits 2 naming the missing product and writes nothing. Pinned in Tasks 5, 6 and 7.
3. **A pre-release suffix with characters semver forbids** (`beta_1`, a space): the configure fails naming `GG_VERSION_PRERELEASE`. Pinned in Task 1.
4. **A dry run or a fork without the secrets:** the macOS leg builds an unsigned installer and the step summary says so; a tag run without them fails in `plan`. Pinned in Task 10 (the condition is on `secrets.MAC_CERTS_P12 != ''`, and the dry run before the secrets exist exercises it).
5. **A partial signing environment** (an identity without the notary key): the macOS script refuses with exit 2 rather than shipping a signed but un-notarized DMG. Pinned in Task 5.

---

### Task 1: The version in CMake

**Files:**
- Create: `cmake/version.cmake`
- Modify: `CMakeLists.txt:25` (`project(... VERSION 0.0.1 ...)`), `CMakeLists.txt:44-55` (the identity comment), `engine/CMakeLists.txt:20-21`
- Modify: `tests/smoke_tests.cpp:11-19`, `tests/plugin/check-bundle-identity.cmake`, `tests/CMakeLists.txt` (near the `identity_*` tests, line ~408, and the `asio_guard_*` block, line ~457)
- Create: `tests/cmake/version/run.cmake`

**Interfaces:**
- Produces: `gg_compose_version(<numeric> <prerelease> <out>)` in `cmake/version.cmake`: sets `<out>` to `<numeric>` when `<prerelease>` is empty, else `<numeric>-<prerelease>`; `message(FATAL_ERROR ...)` naming `GG_VERSION_PRERELEASE` unless `<prerelease>` matches `^[0-9A-Za-z.-]*$` with no empty dot-separated identifier.
- Produces: cache variable `GG_VERSION_PRERELEASE` (STRING, default `dev`); normal variables `GG_VERSION_NUMERIC` (= `PROJECT_VERSION`) and `GG_VERSION_STRING`; compile definitions on `gg-engine` (PUBLIC): `GG_VERSION_STRING`, `GG_VERSION_NUMERIC`, `GG_VERSION_PRERELEASE`.
- Produces: `check-bundle-identity.cmake` mode `CHECK=bundle-version`: `CFBundleShortVersionString` and `CFBundleVersion` both equal `EXPECTED`.

- [ ] **Step 1: Write the failing CMake tests**

`tests/cmake/version/run.cmake` takes `-DGG_CASE=<name>`, includes `cmake/version.cmake` and exits non-zero unless the case behaves. Cases: `dev` (`0.2.0`, `dev` → `0.2.0-dev`), `release` (`0.2.0`, `` → `0.2.0`), `nightly` (`0.2.0`, `dev.20261029.95bee95` → `0.2.0-dev.20261029.95bee95`), `bad-underscore` (`beta_1` → the include must fail: run it in a nested `execute_process(${CMAKE_COMMAND} -P ...)` and require a non-zero status and `GG_VERSION_PRERELEASE` in stderr), `bad-empty-identifier` (`beta..1` → fails the same way). In `tests/CMakeLists.txt`, next to `asio_guard_*`, add `add_test(NAME version_<case> COMMAND ${CMAKE_COMMAND} -DGG_CASE=<case> -DGG_REPO=${PROJECT_SOURCE_DIR} -P .../run.cmake)` with `LABELS build` for the five cases.

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --preset dev && ctest --preset dev -R '^version_' --output-on-failure`
Expected: all five FAIL (`cmake/version.cmake` not found).

- [ ] **Step 3: Implement `cmake/version.cmake` and wire it in**

In `CMakeLists.txt`: `project(GigaGrains VERSION 0.2.0 LANGUAGES C CXX)`; after it, `set(GG_VERSION_PRERELEASE "dev" CACHE STRING "Pre-release suffix: dev on main, beta.N for a beta, empty for a release")`, `include(cmake/version.cmake)`, `gg_compose_version("${PROJECT_VERSION}" "${GG_VERSION_PRERELEASE}" GG_VERSION_STRING)`, `set(GG_VERSION_NUMERIC "${PROJECT_VERSION}")`, and change the status line to `message(STATUS "Giga Grains ${GG_VERSION_STRING}")`. In `engine/CMakeLists.txt` replace the one definition with the three. Rewrite the identity comment at `CMakeLists.txt:44-48` to say a real identity now also gets the hardened runtime, timestamp and entitlements (Task 4 adds them; write the comment for the end state).

- [ ] **Step 4: Update the smoke test and add the bundle-version check**

`tests/smoke_tests.cpp`: `REQUIRE(gg::kVersion == GG_VERSION_STRING)`; `REQUIRE(gg::kVersion.rfind(GG_VERSION_NUMERIC, 0) == 0)`; the numeric part has exactly two dots and only digits and dots; `if (std::string_view(GG_VERSION_PRERELEASE).empty()) REQUIRE(gg::kVersion == GG_VERSION_NUMERIC); else REQUIRE(gg::kVersion == std::string(GG_VERSION_NUMERIC) + "-" + GG_VERSION_PRERELEASE)`. `factory_tests.cpp` needs no change (it compares `d->version` with `GG_VERSION_STRING`). In `check-bundle-identity.cmake` add the `bundle-version` branch (extract both keys as `raw`, compare each with `EXPECTED`). In `tests/CMakeLists.txt`, inside the `APPLE AND GG_BUILD_PLUGINS AND GG_BUILD_EDITOR` block, add `version_bundle_<format>` for `clap vst3 auv2 standalone standalone_fx` with `-DEXPECTED=${GG_VERSION_NUMERIC}` and `LABELS installer`.

- [ ] **Step 5: Run the tests**

Run: `cmake --preset dev && cmake --build --preset dev && ctest --preset dev -R '^(version_|engine library)' --output-on-failure`
Expected: the five `version_*` build tests, the five `version_bundle_*` and the smoke test PASS; `cmake --preset dev` prints `Giga Grains 0.2.0-dev`. Also run `ctest --preset dev -L plugin` (the factory test).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt cmake/version.cmake engine/CMakeLists.txt tests/smoke_tests.cpp tests/plugin/check-bundle-identity.cmake tests/CMakeLists.txt tests/cmake/version/run.cmake
git commit -m "build: the version is 0.2.0 plus a pre-release suffix, dev by default (spec 6.1)"
```

---

### Task 2: The tag check script

**Files:**
- Create: `scripts/release/check-version.sh`
- Create: `tests/scripts/release_scripts_tests.sh` (Tasks 8 and 9 add to it)
- Modify: `tests/CMakeLists.txt:252-262` (the `scripts` block)

**Interfaces:**
- Produces: `scripts/release/check-version.sh <tag> [<CMakeLists.txt>]` (default: the repository's). Parses `project(GigaGrains VERSION X.Y.Z` and `set(GG_VERSION_PRERELEASE "<s>"`; prints `numeric=X.Y.Z`, `prerelease=<s>` and `version=<full>` (one per line, `key=value`, so a workflow can append them to `$GITHUB_OUTPUT`). Exit 0 when `<tag>` equals `v<full>`; exit 1 with a one-line reason on a mismatch or when `<s>` is `dev`; exit 2 on usage or an unparsable file.
- Produces: the test harness `tests/scripts/release_scripts_tests.sh <scratch dir>` with the same `check`/`expect_in` helpers and `ok:`/`FAIL:` lines as `scripts_tests.sh`; CTest `scripts_release` (label `scripts`, not on Windows).

- [ ] **Step 1: Write the failing tests**

In `release_scripts_tests.sh`, write a scratch `CMakeLists.txt` with `project(GigaGrains VERSION 0.2.0 LANGUAGES C CXX)` and `set(GG_VERSION_PRERELEASE "beta.1" CACHE STRING "...")` and check: `v0.2.0-beta.1` → exit 0 and output contains `version=0.2.0-beta.1`; `v0.2.0` → exit 1 (mismatch); `v0.2.1-beta.1` → exit 1; with the suffix line changed to `""`, `v0.2.0` → exit 0 and `v0.2.0-beta.1` → exit 1; with `"dev"`, `v0.2.0-dev` → exit 1 and the reason mentions `dev`; no arguments → exit 2; a file without a `project(` line → exit 2. Register `scripts_release` in `tests/CMakeLists.txt` beside `scripts_check_and_worktree_setup`, scratch dir `${CMAKE_CURRENT_BINARY_DIR}/release-scripts-tests`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --preset dev && ctest --preset dev -R scripts_release --output-on-failure`
Expected: FAIL (script not found).

- [ ] **Step 3: Implement the script**

Parse with `sed -n`, no bash 4 features; strip a leading `v` from the tag; compose as `cmake/version.cmake` does.

- [ ] **Step 4: Run to verify it passes**

Run: `ctest --preset dev -R scripts_release --output-on-failure`
Expected: PASS, every `ok:` line printed.

- [ ] **Step 5: Commit**

```bash
git add scripts/release/check-version.sh tests/scripts/release_scripts_tests.sh tests/CMakeLists.txt
git commit -m "scripts: check-version.sh compares a release tag with CMake's version"
```

---

### Task 3: The license set in `gg-products`

**Files:**
- Create: `installer/licenses/CMakeRC-LICENSE.txt`, `installer/licenses/flowbite-icons-LICENSE.txt`, `installer/licenses/README.md`
- Modify: `cmake/products.cmake` (after the `gg_add_product` calls), `THIRD_PARTY_NOTICES.md:8-12`
- Create: `tests/installer/check-license-set.cmake`
- Modify: `tests/CMakeLists.txt` (new `installer` block)

**Interfaces:**
- Produces: `${GG_PRODUCT_DIR}/licenses/` written by `gg-products`, holding exactly: `THIRD_PARTY_NOTICES.md`, `LICENSE`, `GPL-3.0.txt`, `CMakeRC-LICENSE.txt`, `flowbite-icons-LICENSE.txt`, plus `AudioUnitSDK-LICENSE.txt` on macOS and `ASIO-SDK-LICENSE.txt` on Windows.
- Produces: CMake variable `GG_LICENSE_FILES` (the list of source paths) and `GG_LICENSE_DIR` (`${GG_PRODUCT_DIR}/licenses`) for Tasks 5–7.
- Produces: the CTest fixture `gg_products`: test `installer_build_products` runs `${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --config $<CONFIG> --target gg-products`, `FIXTURES_SETUP gg_products`, `RUN_SERIAL TRUE`, label `installer`. Every later `installer_*` test sets `FIXTURES_REQUIRED gg_products`.

- [ ] **Step 1: Write the failing test**

`tests/installer/check-license-set.cmake` (`-DDIR=<licenses dir> -DPLATFORM=<Darwin|Windows|Linux>`): fails unless each expected file for the platform exists and is larger than 1,000 bytes, and no other file is present. Register `installer_license_set` with `-DDIR=${CMAKE_BINARY_DIR}/gg-products/licenses -DPLATFORM=${CMAKE_SYSTEM_NAME}`, `FIXTURES_REQUIRED gg_products`, `LABELS installer`, in a new block in `tests/CMakeLists.txt` guarded by `GG_BUILD_PLUGINS AND GG_BUILD_EDITOR` (the products exist only then), together with the `installer_build_products` fixture test.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --preset dev && ctest --preset dev -R installer_license_set --output-on-failure`
Expected: FAIL (no `licenses` directory).

- [ ] **Step 3: Implement the license set**

In `cmake/products.cmake`: the AudioUnitSDK text is `${AUDIOUNIT_SDK_ROOT}/LICENSE.txt` when `AUDIOUNIT_SDK_ROOT` is set and non-empty (CI), else `${CPM_PACKAGE_AudioUnitSDK_SOURCE_DIR}/LICENSE.txt` (a local tree; the cache entry exists once `clients/clap-first` has been processed, which precedes this include); if neither file exists, `message(FATAL_ERROR)` naming both variables. The GPLv3 text is `libs/sst/sst-basic-blocks/LICENSE.md`; the ASIO text `libs/sst/sst-plugininfra/libs/asiosdk/LICENSE.txt`. Check every source exists at configure time. Add `POST_BUILD` commands to `gg-products` that `rm -rf` then recreate the directory and copy each file under its published name. Fill `installer/licenses/` with the two MIT texts copied from the upstream URLs `THIRD_PARTY_NOTICES.md` names (the MIT text is already reproduced there; the files carry each project's copyright line) and a README giving the URLs and the upstream commit or tag each was copied from. Update `THIRD_PARTY_NOTICES.md:8-12` to say the installers carry the set (name the directory) and the About box is still to come.

- [ ] **Step 4: Run to verify it passes**

Run: `ctest --preset dev -L installer --output-on-failure`
Expected: `installer_build_products` then `installer_license_set` PASS; `ls build/dev/gg-products/licenses` shows the six macOS files.

- [ ] **Step 5: Commit**

```bash
git add cmake/products.cmake installer/licenses tests/installer/check-license-set.cmake tests/CMakeLists.txt THIRD_PARTY_NOTICES.md
git commit -m "build: gg-products assembles the license set every installer carries (row 221)"
```

---

### Task 4: Hardened runtime, timestamp and entitlements for a real identity

**Files:**
- Create: `installer/macos/standalone.entitlements`
- Modify: `clients/clap-first/CMakeLists.txt:76-103` (the signing loop)
- Create: `tests/installer/check-codesign-flags.cmake`
- Modify: `tests/CMakeLists.txt` (the `installer` block)

**Interfaces:**
- Produces: when `GG_MACOS_CODESIGN_IDENTITY` is not `-`, every bundle is signed with `--options runtime --timestamp`, and the two standalone apps also with `--entitlements ${CMAKE_SOURCE_DIR}/installer/macos/standalone.entitlements`. With `-`, the command is unchanged.
- Produces: CTests `codesign_hardened_<format>` (five), registered only when the identity is not `-`, label `installer`.

- [ ] **Step 1: Write the failing tests**

`check-codesign-flags.cmake` (`-DCODESIGN=<codesign> -DBUNDLE=<dir> -DEXPECT_ENTITLEMENT=<key or empty>`): runs `codesign -d --verbose=2 <bundle>` and requires `flags=` to contain `runtime`; with an expected key, runs `codesign -d --entitlements :- <bundle>` and requires the key in the output. Register the five tests inside `if (NOT GG_MACOS_CODESIGN_IDENTITY STREQUAL "-")`, with `com.apple.security.device.audio-input` expected for `standalone` and `standalone_fx` only.

- [ ] **Step 2: Run to verify they fail**

The director's keychain holds the Developer ID identities (spec decision 9). Configure a separate tree: `cmake --preset release -B build/release-signed -DGG_MACOS_CODESIGN_IDENTITY="Developer ID Application: <name> (<team>)"` (take the exact name from `security find-identity -v -p codesigning`; never write it into a file in the tree), build `gg-all`, run `ctest --test-dir build/release-signed -R codesign_hardened --output-on-failure`.
Expected: FAIL (no `runtime` flag). If no identity is available to the implementer, record that this step waits for the director and continue; the CI dry run in Task 12 runs these tests.

- [ ] **Step 3: Implement**

The entitlements plist holds one key, `com.apple.security.device.audio-input`, `true`. In the signing loop build a `sign_options` list, empty for `-`, else `--options runtime --timestamp`, plus the entitlements for the two apps; pass it before `--force --sign`. Keep the `LINK_DEPENDS` logic as is.

- [ ] **Step 4: Run to verify they pass**

Run (signed tree): `cmake --build build/release-signed --target gg-all && ctest --test-dir build/release-signed -R 'codesign' --output-on-failure`
Expected: `codesign_*` (strict) and `codesign_hardened_*` PASS. Run (ad hoc tree): `ctest --preset dev -R codesign_ --output-on-failure` still PASS and lists no `codesign_hardened_*`.

- [ ] **Step 5: Commit**

```bash
git add installer/macos/standalone.entitlements clients/clap-first/CMakeLists.txt tests/installer/check-codesign-flags.cmake tests/CMakeLists.txt
git commit -m "build(macos): a real identity signs with the hardened runtime, a timestamp and the apps' audio-input entitlement"
```

---

### Task 5: The macOS installer script and `gg-installer`

**Files:**
- Create: `installer/macos/make-installer.sh`, `installer/macos/distribution.xml.in`, `installer/macos/Readme.txt`
- Modify: `cmake/products.cmake` (the `gg-installer` target)
- Create: `tests/installer/check-macos-pkg.cmake`
- Modify: `tests/CMakeLists.txt` (the `installer` block)

**Interfaces:**
- Produces: `installer/macos/make-installer.sh --products <dir> --out <dir> --version <X.Y.Z> --full-version <v> --resources <installer/macos> [--licenses <dir>]`. Writes `<out>/giga-grains-<v>-macos.pkg` and `<out>/giga-grains-<v>-macos.dmg` (the DMG holds the PKG and a `Licenses` folder). Signed mode when **all** of `MAC_SIGNING_IDENTITY`, `MAC_INSTALLER_IDENTITY`, `NOTARY_KEY_FILE`, `NOTARY_KEY_ID`, `NOTARY_ISSUER_ID` are set in the environment; unsigned when none is; exit 2 when some are. Exit 2 also when a product is missing from `--products`, naming it. Prints `signed: yes|no` as its last line.
- Produces: component package identifiers and install locations exactly as spec §6.4's table; `distribution.xml.in` with `@GG_VERSION_NUMERIC@`, `@GG_VERSION_STRING@` placeholders filled by `sed`, `hostArchitectures="x86_64,arm64"`, `allowed-os-versions` min `11.0`, `customize="always"`, the AGPL as the license pane, `Readme.txt` as the readme pane, and a commented-out *Content* choice block for M2 (spec §6.9).
- Produces: `gg-installer` (macOS): `add_custom_target(gg-installer)` depending on `gg-products`, `POST_BUILD` runs the script with `--products ${GG_PRODUCT_DIR} --out ${CMAKE_BINARY_DIR}/gg-installer --version ${GG_VERSION_NUMERIC} --full-version ${GG_VERSION_STRING} --resources ${CMAKE_SOURCE_DIR}/installer/macos --licenses ${GG_LICENSE_DIR}`. `GG_INSTALLER_DIR` = `${CMAKE_BINARY_DIR}/gg-installer` for Tasks 6, 7 and 10.

- [ ] **Step 1: Write the failing test**

`installer_macos_pkg` (macOS, with the editor; `FIXTURES_REQUIRED gg_products`; label `installer`): a CMake `-P` driver (`check-macos-pkg.cmake`, `-DSCRIPT -DPRODUCTS -DOUT -DRESOURCES -DLICENSES -DNUMERIC -DFULL`) runs the script unsigned with the environment variables unset, then `pkgutil --expand <pkg> <OUT>/expanded`, and requires: `Distribution` contains the five `pkg-ref` identifiers from spec §6.4, `version="<NUMERIC>"`, `hostArchitectures="x86_64,arm64"` and the title `Giga Grains <FULL>`; each component's `PackageInfo` has the install-location of the table; the DMG exists and `hdiutil imageinfo <dmg>` reports a `UDZO` image; the script's last line is `signed: no`. A second driver call with `-DMISSING=1` runs the script on an empty products dir and requires exit 2 and `Giga Grains.clap` in the error. A third sets only `MAC_SIGNING_IDENTITY=x` and requires exit 2 (Review Focus 5).

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --preset dev && ctest --preset dev -R installer_macos_pkg --output-on-failure`
Expected: FAIL (script not found).

- [ ] **Step 3: Implement the script, the resources and the target**

Derive from sst-plugininfra's `make_installer.sh` (keep its MIT notice); stage each product alone in a work directory; `pkgbuild --root <stage> --identifier <id> --version <numeric> --install-location <loc>` (with `--sign "$MAC_INSTALLER_IDENTITY"` when signed); `productbuild --distribution ... --package-path ... --resources "$resources"` (`--sign` when signed); when signed: `xcrun notarytool submit <pkg> --key "$NOTARY_KEY_FILE" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER_ID" --wait`, `xcrun stapler staple <pkg>`, `spctl --assess --type install <pkg>`; build the DMG folder (the PKG, `Licenses/` from `--licenses`), `hdiutil create -ov -fs HFS+ -srcfolder ... -volname "Giga Grains <v>"` then `hdiutil convert -format UDZO`; when signed: `codesign --force --sign "$MAC_SIGNING_IDENTITY" --timestamp <dmg>`, notarize and staple the DMG, `spctl --assess --type open --context context:primary-signature <dmg>`. No `Rez`/`SetFile`. Apps install straight to `/Applications`. `Readme.txt`: what installs where, the first-class hosts, the project URL, where the notices are, and the corresponding-source directions (the tag, `git clone --recurse-submodules`, the source tarball on the release page, the SDK tags clap-wrapper pins). Add `gg-installer` in `products.cmake` as the Interfaces block says.

- [ ] **Step 4: Run to verify it passes**

Run: `ctest --preset dev -R installer_macos_pkg --output-on-failure && cmake --build --preset dev --target gg-installer && ls build/dev/gg-installer`
Expected: PASS; the directory holds `giga-grains-0.2.0-dev-macos.pkg` and `.dmg`. Open the DMG by hand once, run the PKG on this machine with every choice, and confirm each format loads in Bitwig or Live (ad hoc signed, so Gatekeeper may warn; that is expected here).

- [ ] **Step 5: Commit**

```bash
git add installer/macos cmake/products.cmake tests/installer/check-macos-pkg.cmake tests/CMakeLists.txt
git commit -m "installer(macos): gg-installer builds the PKG and DMG, signed and notarized when identities are present"
```

---

### Task 6: The Linux tarball

**Files:**
- Create: `installer/linux/make-tarball.sh`, `installer/linux/README.md`
- Modify: `cmake/products.cmake` (`gg-installer` on Linux)
- Create: `tests/installer/check-archive.cmake` (shared with Task 7)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `installer/linux/make-tarball.sh --products <dir> --out <dir> --full-version <v> --readme <installer/linux/README.md>`: writes `<out>/giga-grains-<v>-linux-x86_64.tar.gz` whose entries all sit under `giga-grains-<v>/`: `Giga Grains.clap`, `Giga Grains.vst3/` (recursively), `Giga Grains`, `Giga Grains FX`, `licenses/` (from `<products>/licenses`) and `README.md`. Exit 2 naming a missing product. Executable bits preserved.
- Produces: `check-archive.cmake` (`-DARCHIVE -DKIND=tar|zip -DEXPECT=<;-list of required entries> -DFORBID=<;-list>`): lists with `${CMAKE_COMMAND} -E tar tf` and fails on a missing or forbidden entry.

- [ ] **Step 1: Write the failing test**

`installer_linux_tarball` (Linux only, with the editor, `FIXTURES_REQUIRED gg_products`, label `installer`): a `-P` driver runs the script into `${CMAKE_BINARY_DIR}/tests/installer-linux/`, then `check-archive.cmake` with EXPECT `giga-grains-<v>/Giga Grains.clap`, `giga-grains-<v>/Giga Grains.vst3/Contents/x86_64-linux/Giga Grains.so`, `giga-grains-<v>/Giga Grains`, `giga-grains-<v>/Giga Grains FX`, `giga-grains-<v>/licenses/THIRD_PARTY_NOTICES.md`, `giga-grains-<v>/licenses/GPL-3.0.txt`, `giga-grains-<v>/README.md` and FORBID `giga-grains-<v>/licenses/ASIO-SDK-LICENSE.txt`; plus the missing-product case (exit 2).

- [ ] **Step 2: Run to verify it fails**

Run (on Linux, or in CI's draft-PR run if no Linux machine is at hand): `ctest --preset linux-gcc12 -R installer_linux_tarball --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Implement**

Stage into a temp dir, `tar -czf` from its parent so the top-level folder is `giga-grains-<v>/`. README: where to copy each file (`~/.clap` or `/usr/lib/clap`; `~/.vst3` or `/usr/lib/vst3`; the apps anywhere), built on Ubuntu 22.04 (glibc 2.35), x86_64 only, the license folder, the corresponding-source directions. In `products.cmake`, the Linux branch of `gg-installer` runs the script with `--full-version ${GG_VERSION_STRING}`.

- [ ] **Step 4: Run to verify it passes**

Run: `ctest --preset linux-gcc12 -R installer_linux --output-on-failure && cmake --build --preset linux-gcc12 --target gg-installer`
Expected: PASS; `tar -tzf build/linux-gcc12/gg-installer/giga-grains-0.2.0-dev-linux-x86_64.tar.gz | head` shows the folder.

- [ ] **Step 5: Commit**

```bash
git add installer/linux cmake/products.cmake tests/installer/check-archive.cmake tests/CMakeLists.txt
git commit -m "installer(linux): gg-installer writes the tarball"
```

---

### Task 7: The Windows zip and Inno Setup installer

**Files:**
- Create: `installer/windows/giga-grains.iss`, `installer/windows/make-zip.cmake`, `installer/windows/readme.txt`
- Modify: `cmake/products.cmake` (`gg-installer` on Windows)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `cmake -DPRODUCTS=<dir> -DOUT=<dir> -DFULL_VERSION=<v> -DREADME=<readme.txt> -P installer/windows/make-zip.cmake`: writes `<out>/giga-grains-<v>-windows-x64.zip` with the same tree as the tarball under `giga-grains-<v>/` (`Giga Grains.clap`, `Giga Grains.vst3/`, `Giga Grains.exe`, `Giga Grains FX.exe`, `licenses/`, `readme.txt`); fails naming a missing product.
- Produces: `giga-grains.iss` compiled as `ISCC.exe /DStagedAssets=<products> /DLicenses=<licenses> /DVersion=<X.Y.Z> /DFullVersion=<v> /DOutDir=<out> giga-grains.iss` → `<out>/giga-grains-<v>-windows-x64.exe`. `AppId` a fixed GUID generated once; `AppName` `Giga Grains`; `AppPublisher` `sethbc`; `AppVersion` `<v>`; `VersionInfoVersion` `<X.Y.Z>`; `LicenseFile` the AGPL from `<licenses>/LICENSE`; `ArchitecturesAllowed=x64compatible`; `ArchitecturesInstallIn64BitMode=x64compatible`; `CloseApplicationsFilter=*.exe,*.vst3`; components CLAP → `{commoncf}\CLAP\Giga Grains.clap`, VST3 → `{commoncf}\VST3\Giga Grains.vst3\` (recursive), the two apps → `{autopf}\Giga Grains\` with Start-menu entries; a fixed component installing `<licenses>\*` to `{autopf}\Giga Grains\licenses\`; a commented *Content* component for M2. Keep sst-cmake's MIT notice.
- Produces: `gg-installer` (Windows): the zip always; the `.exe` when `find_program(GG_ISCC ISCC PATHS "C:/Program Files (x86)/Inno Setup 6")` finds the compiler, with a `message(STATUS)` naming it or saying the zip alone will be built. CMake variable `GG_ISCC` for Task 10's existence check.

- [ ] **Step 1: Write the failing tests**

`installer_windows_zip` (WIN32, with the editor, fixture `gg_products`, label `installer`): runs `make-zip.cmake` into `${CMAKE_BINARY_DIR}/tests/installer-windows/` then `check-archive.cmake` (KIND zip) with the entries above, plus the missing-product case. `installer_windows_inno` (only when `GG_ISCC` is found): compiles the `.iss` into the same scratch dir and requires `giga-grains-<v>-windows-x64.exe` to exist and exceed 1 MB.

- [ ] **Step 2: Run to verify they fail**

Run (Windows, or CI's draft PR): `ctest --preset windows-clangcl -R installer_windows --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Implement**

`make-zip.cmake` stages with `file(COPY)` then `execute_process(${CMAKE_COMMAND} -E tar cf <zip> --format=zip giga-grains-<v>)` from the staging parent. The `.iss` is adapted from sst-cmake's template (single-product, the two apps, the licenses component, no `Data`). In `products.cmake` the Windows branch runs both.

- [ ] **Step 4: Run to verify they pass**

Run: `ctest --preset windows-clangcl -R installer_windows --output-on-failure && cmake --build --preset windows-clangcl --config Release --target gg-installer`
Expected: PASS; `build/windows/gg-installer/` holds the zip and the exe. If a Windows machine is available, run the installer once with every component, open the CLAP and VST3 in a host, uninstall.

- [ ] **Step 5: Commit**

```bash
git add installer/windows cmake/products.cmake tests/CMakeLists.txt
git commit -m "installer(windows): gg-installer writes the zip and the Inno Setup installer"
```

---

### Task 8: The source tarball

**Files:**
- Create: `scripts/release/source-tarball.sh`
- Modify: `tests/scripts/release_scripts_tests.sh`

**Interfaces:**
- Produces: `scripts/release/source-tarball.sh <repo> <full-version> <out.tar.gz>`: writes the tracked files of `<repo>` and of every submodule, recursively, at their checked-out commits, under `giga-grains-<full-version>/`, with no `.git` files or directories and no untracked files; exits 1 if any submodule in `.gitmodules` is not checked out. Implementation: `git -C <repo> archive` for the superproject plus `git submodule foreach --recursive` archiving each into the same staging directory, then `tar -czf`.

- [ ] **Step 1: Write the failing test**

In `release_scripts_tests.sh`: build a scratch upstream (`git init`) with one tracked file, a scratch submodule repository with one tracked file, add it as a submodule at `libs/sub`, commit; clone it recursively; run the script with version `9.9.9`; `tar -tzf` must list `giga-grains-9.9.9/README`, `giga-grains-9.9.9/libs/sub/<file>` and no entry containing `/.git`; an uncommitted extra file must be absent; with the submodule deinitialised, the script exits 1 naming `libs/sub`.

- [ ] **Step 2: Run to verify it fails**

Run: `ctest --preset dev -R scripts_release --output-on-failure`
Expected: FAIL on the new checks.

- [ ] **Step 3: Implement the script**

- [ ] **Step 4: Run to verify it passes**

Run: `ctest --preset dev -R scripts_release --output-on-failure`, then once on the real repository: `scripts/release/source-tarball.sh . 0.2.0-dev /tmp/gg-src.tar.gz && tar -tzf /tmp/gg-src.tar.gz | grep -c 'libs/JUCE/' ` (a large count) and `tar -tzf ... | grep -c '/\.git' ` (0). Delete the file.
Expected: PASS; the real archive unpacks and `cmake --preset dev` configures from it (the SDK fetch aside).

- [ ] **Step 5: Commit**

```bash
git add scripts/release/source-tarball.sh tests/scripts/release_scripts_tests.sh
git commit -m "scripts: source-tarball.sh archives the tagged tree with every submodule (corresponding source)"
```

---

### Task 9: The signing keychain script

**Files:**
- Create: `scripts/release/mac-keychain.sh`
- Modify: `tests/scripts/release_scripts_tests.sh`

**Interfaces:**
- Produces: `scripts/release/mac-keychain.sh import <p12 file> <keychain path>` reads the import password from `P12_PASSWORD` in the environment, creates the keychain with a random password (`openssl rand -hex 24`), imports the `.p12` for `codesign` and `productsign` (`security import ... -T /usr/bin/codesign -T /usr/bin/productsign`), adds it to the search list, runs `security set-key-partition-list -S apple-tool:,apple: -s`, and prints the identities found (`security find-identity -v -p codesigning <keychain>`). `scripts/release/mac-keychain.sh delete <keychain path>` removes it. Never prints a password.

- [ ] **Step 1: Write the failing test**

macOS only, in `release_scripts_tests.sh`: make a self-signed code-signing certificate and key with `openssl req -x509 -newkey rsa:2048 -keyout k.pem -out c.pem -days 2 -nodes -subj '/CN=gg test'` and `openssl pkcs12 -export -out t.p12 -inkey k.pem -in c.pem -passout pass:secret`; run `import` with `P12_PASSWORD=secret` into `<scratch>/t.keychain-db`; require `gg test` in the output; run `delete`; require the file gone. Check the output of `import` never contains `secret`.

- [ ] **Step 2: Run to verify it fails**

Run: `ctest --preset dev -R scripts_release --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Implement**

- [ ] **Step 4: Run to verify it passes**

Run: `ctest --preset dev -R scripts_release --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add scripts/release/mac-keychain.sh tests/scripts/release_scripts_tests.sh
git commit -m "scripts: mac-keychain.sh imports the signing certificates into a throwaway keychain"
```

---

### Task 10: The workflow

**Files:**
- Modify: `.github/workflows/ci.yml` (triggers lines 29–36; the header comment lines 1–26; the build job's `env`, Check out, Configure, the steps after the Steinberg validator; the file's end)
- Create: `installer/nightly-notes.md`
- Create: `docs/releases/README.md` (how to write `docs/releases/v<version>.md`; Task 11 may extend it)

**Interfaces:**
- Consumes: `scripts/release/check-version.sh` (Task 2), `gg-installer` and `GG_INSTALLER_DIR` (Tasks 5–7), `scripts/release/source-tarball.sh` (Task 8), `scripts/release/mac-keychain.sh` (Task 9).
- Produces, in `ci.yml`:
  - triggers: add `push: tags: ['v*']` and a `workflow_dispatch` input `installers` (boolean, default `false`, description "Build and sign the installers and upload them as artifacts; publish nothing");
  - a top-level `env: GG_PUBLISH_NIGHTLY: "false"` with a comment: flip to `"true"` when M5 begins (INTENT §23 #61);
  - job `plan` (`ubuntu-24.04`, 5 minutes): outputs `release_kind` (`release` for a `v*` tag, `nightly` for `schedule` on `main`, `dry-run` for a dispatch with `installers`, else `none`), `version` (the full string; for `nightly`: the CMake numeric part plus `-dev.<yyyymmdd>.<sha7>`; for `release`: `check-version.sh`'s `version`; else empty) and `prerelease_suffix` (the part after the first `-`, or `dev`); fails for `release` when `check-version.sh` fails, when `docs/releases/<tag>.md` is missing, when `secrets.MAC_CERTS_P12` is empty, or when the tagged commit is reachable from neither `origin/main` nor any `origin/release/*` branch (checked out with `fetch-depth: 0` in this job alone; `git merge-base --is-ancestor`), which is the hotfix rule of spec §1; runs only when `github.repository == 'sethbc/giga-grains'` for the signing kinds (a fork gets `none`);
  - `build` gets `needs: plan` and these conditional steps, every one guarded by `needs.plan.outputs.release_kind != 'none'`: **Import the signing certificates** (macOS; also `secrets.MAC_CERTS_P12 != ''`): decode the secret to `$RUNNER_TEMP/certs.p12`, run `mac-keychain.sh import` with `P12_PASSWORD` from the secret, decode `NOTARY_KEY_P8` to `$RUNNER_TEMP/AuthKey.p8`, and export `GG_SIGN=1` to `$GITHUB_ENV`; **Configure** adds `-DGG_VERSION_PRERELEASE=<suffix>` for `nightly` and `-DGG_MACOS_CODESIGN_IDENTITY="$MAC_SIGNING_IDENTITY"` when `GG_SIGN` is set; **Build the installer** (after the Steinberg validator, `steps.products.outcome == 'success'`): `cmake --build --preset <p> --config Release --target gg-installer` with `MAC_SIGNING_IDENTITY`, `MAC_INSTALLER_IDENTITY`, `NOTARY_KEY_ID`, `NOTARY_ISSUER_ID` from secrets and `NOTARY_KEY_FILE=$RUNNER_TEMP/AuthKey.p8` set only when `GG_SIGN` is set, then checks the expected files exist (macOS: the `.dmg`; Windows: the `.zip` and the `.exe`; Linux: the `.tar.gz`) and writes `signed: yes|no` to the step summary; **Upload the installer** (`installer-<preset>`, `if-no-files-found: error`); **Delete the signing keychain** (`always()`, macOS, when the import ran);
  - job `release` (`needs: [plan, build, sanitizers]`, `if: needs.plan.outputs.release_kind != 'none'`, `permissions: contents: write`, `ubuntu-24.04`, `fetch-depth: 1` with `submodules: recursive` for the source tarball): downloads the three `installer-*` artifacts, runs `source-tarball.sh . <version> giga-grains-<version>-source.tar.gz`, writes `SHA256SUMS.txt` (`sha256sum`), then by kind: `release`: `gh release create <tag> --draft --title "Giga Grains <version>" --notes-file docs/releases/<tag>.md` with `--prerelease` when the suffix is non-empty, uploading every file; `nightly`: only when `env.GG_PUBLISH_NIGHTLY == 'true'`: `git tag -f nightly && git push -f origin nightly`, `gh release view nightly` or `gh release create nightly --prerelease --title "Giga Grains nightly" --notes-file installer/nightly-notes.md`, delete assets not in this run's set, `gh release upload nightly <files> --clobber`; otherwise print "nightly publishing is off until M5" to the step summary; `dry-run`: nothing beyond a summary line listing the artifacts;
  - the header comment updated to describe the three kinds.
- Produces: `installer/nightly-notes.md` (what a nightly is, that it is unsupported, that sessions saved with it may not open in a later build, where the source is) and `docs/releases/README.md` (the notes file's name is the tag; what it should contain; a template).

- [ ] **Step 1: Make the changes**

Edit `ci.yml` as the Interfaces block specifies. Keep every existing step and job title unchanged. Use `gh` with `GH_TOKEN: ${{ github.token }}`; add no new third-party action.

- [ ] **Step 2: Verify the workflow statically**

Run: `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml'))"` (or `actionlint` if installed: `brew install actionlint && actionlint`).
Expected: parses; no actionlint errors.

- [ ] **Step 3: Push the branch as a draft PR and watch the PR run**

Run: `git push -u origin <branch> && gh pr create --draft --base main --title "Installers and the release pipeline" --body-file <body>` (the body ends with the Claude Code line), then `gh pr checks --watch`.
Expected: every required job green; the `plan` job reports `release_kind=none`; no installer step ran (the PR path is unchanged).

- [ ] **Step 4: Dry run without secrets**

Run: `gh workflow run ci.yml --ref <branch> -f installers=true` and `gh run watch`.
Expected: `plan` reports `dry-run`; each leg uploads `installer-<preset>`; the macOS summary says `signed: no`; the `release` job lists the artifacts and publishes nothing. Download `installer-mac-universal`, mount the DMG, confirm it holds the PKG and `Licenses/`.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/ci.yml installer/nightly-notes.md docs/releases/README.md
git commit -m "ci: tags, the nightly and an installers dispatch build, sign and publish the installers"
```

(Commit before Step 3; Steps 3 and 4 run on the pushed commit. Amend or follow up as the runs demand.)

---

### Task 11: The release procedure and the docs

**Files:**
- Create: `docs/releasing.md`
- Create: `docs/m2-carry-over.md` (Step 2b)
- Modify: `CLAUDE.md` ("Outputs" list ~line 196; the "Installing and signing the plugins" section ~line 620; the labels paragraph: add `installer`; a new "Releasing" paragraph after "Installing and signing"), `README.md` (the signing paragraph; a "Downloads" sentence: GitHub Releases, public from 0.5.0's first beta, nothing before M5), `docs/m1-carry-over.md` row 221, `INTENT.md` §19 (only if a name the docs pass wrote differs from what was built: `gg-installer`, the switch, the artifact names), `docs/superpowers/specs/2026-10-29-release-pipeline-design.md` (Status row: built, with corrections)

**Interfaces:**
- Consumes: everything above. `docs/releasing.md` covers spec §6.7 (the secrets table, the export commands, the API key, rotation, expiry dates) and §6.8 (the per-release checklist, the freeze step before 0.5.0-beta.1 (nothing public, no betas, before M5), the draft-then-publish step, the unsigned and the signed local builds, the hotfix branch), plus the nightly switch and the dry run.

- [ ] **Step 1: Write `docs/releasing.md`**

Sections, in order: Overview (the three run kinds, what each publishes); One-time setup (the seven secrets, exactly spec §6.7's table and commands, how to create the App Store Connect API key, the dry run that proves them, rotation, expiry dates to note); Each release (spec §6.8 steps 1–3 as a checklist, the version-bump commit, the notes file, the tag, the draft, the install check on a clean macOS user and on Windows, the publish click, the reset-to-`dev` commit); Hotfixes (the `release/0.x` branch); Local builds (unsigned; signed with the environment variables, nothing written to the tree); Nightlies (the switch, when it flips); Troubleshooting (notarization rejected: `xcrun notarytool log <id>`; Gatekeeper refusal; a tag that fails `plan`).

- [ ] **Step 2: Update CLAUDE.md, README.md, the carry-over and the spec status**

CLAUDE.md: add `gg-installer` to Outputs; rewrite the signing bullets for the end state (a real identity adds the hardened runtime, timestamp and entitlement; the installer script notarizes); add the `installer` label; add a "Releasing" paragraph: the version in CMake and `GG_VERSION_PRERELEASE`, `cmake --build --preset release --target gg-installer`, the three run kinds, `docs/releasing.md`. README: the signing paragraph's last sentence and a Downloads line. Carry-over row 221: the installer half **Closed** by this branch's commits (name them); the row now reads as the About box alone, "M2's views or later". Spec Status row: "Built <date> on <branch>" with any corrections.

- [ ] **Step 2b: Split the carry-over for M2** (the director, 2026-10-29)

Create `docs/m2-carry-over.md`: the policy header copied from the M1 file (the five rules, unchanged), then one row per row still open in `docs/m1-carry-over.md` after Step 2 (13 today; row 221 narrowed to the About box), each keeping its original number in the first column with a pointer `(M1 row N)` and its text, origin and "when" columns as they stand. Nothing else moves. Then freeze `docs/m1-carry-over.md`: add, under its title, one paragraph saying it is M1's closed record, that its open rows moved to `docs/m2-carry-over.md` on 2026-10-29 with their numbers, and that it is no longer edited; change nothing else in it. Point CLAUDE.md ("The carry-over" in Working style, the "Where we are"/"The records" bullets) and INTENT §20's M1 and M2 rows at the M2 file, and restate the no-net-growth count as the M2 file's row count. Count the rows in both files in the report.

- [ ] **Step 3: Check the docs against the build**

Run: `grep -n 'gg-installer\|GG_PUBLISH_NIGHTLY\|releasing.md' CLAUDE.md INTENT.md README.md docs/releasing.md | head -40`
Expected: every name matches the code (`grep -rn GG_PUBLISH_NIGHTLY .github` finds it; `cmake --build --preset dev --target gg-installer` works).

- [ ] **Step 4: Commit**

```bash
git add docs/releasing.md CLAUDE.md README.md docs/m1-carry-over.md docs/m2-carry-over.md INTENT.md docs/superpowers/specs/2026-10-29-release-pipeline-design.md
git commit -m "docs: the release procedure, and the installers in CLAUDE.md, README and the carry-over"
```

---

### Task 12: The signed dry run and the final gate

**Files:** none new; the report.

- [ ] **Step 1: The secrets**

Ask the director to set the seven secrets following `docs/releasing.md` (decision 9 says they are set during this work). Confirm with `gh api repos/sethbc/giga-grains/actions/secrets --jq '.secrets[].name'` (names only).

- [ ] **Step 2: The signed local build**

With the director's identities in the environment (`MAC_SIGNING_IDENTITY`, `MAC_INSTALLER_IDENTITY`, `NOTARY_KEY_FILE`, `NOTARY_KEY_ID`, `NOTARY_ISSUER_ID`; nothing written to the tree), run `cmake --preset release -B build/release-signed -DGG_MACOS_CODESIGN_IDENTITY="$MAC_SIGNING_IDENTITY" && cmake --build build/release-signed --target gg-installer`.
Expected: the script prints `signed: yes`; `spctl --assess --type install build/release-signed/gg-installer/*.pkg` and `spctl --assess --type open --context context:primary-signature *.dmg` both accept; `ctest --test-dir build/release-signed -R 'codesign|version_bundle|installer' --output-on-failure` passes.

- [ ] **Step 3: The CI dry run, signed**

Run: `gh workflow run ci.yml --ref <branch> -f installers=true`, `gh run watch`.
Expected: the macOS leg's summary says `signed: yes`; `codesign_hardened_*` passed on that leg; download `installer-mac-universal`, and on this machine `spctl --assess --type open --context context:primary-signature <dmg>` accepts, the PKG installs, and both plugins load in Live and Bitwig. Record the run ID.

- [ ] **Step 4: The rehearsal release (the director's choice)**

If the director agrees: `git tag v0.1.99-rc.1 <head> && git push origin v0.1.99-rc.1` on a commit whose CMake version is `0.1.99` + `rc.1` with `docs/releases/v0.1.99-rc.1.md` present (a throwaway commit on the branch, reverted after). Expected: `plan` passes, the draft pre-release appears with the four installers, the source tarball and `SHA256SUMS.txt`. Then delete the draft and the tag (`gh release delete v0.1.99-rc.1 --yes --cleanup-tag`) and revert the commit.

- [ ] **Step 5: The gate**

Run: `scripts/update-goldens.sh` (every WAV SHA-256 unchanged against the run before Task 1), `scripts/check.sh`, and count the carry-over's open rows (13 before; 13 or fewer after, row 221 narrowed, nothing opened).
Expected: `PASS`; paste the summary and the hashes line into the report.

- [ ] **Step 6: Report and hand over**

The final report: the dry-run run ID(s), the signed local build's `spctl` lines, the check.sh summary, the golden hash line, the carry-over count, the rehearsal's outcome, and anything the director must still do (publishing is theirs and starts with M5's `v0.5.0-beta.1`; the first tag, `v0.2.0` with M2, stays an unpublished draft).
