# Installers and the release pipeline

| | |
|---|---|
| **Status** | Approved by the director 2026-10-29 (INTENT §23 #61). **Built 2026-10-29 on `release-pipeline` (PR #13)**; the runbook is `docs/releasing.md`. Corrections the build made to the text below: the signing environment is `MAC_SIGNING_IDENTITY`, `MAC_INSTALLER_IDENTITY`, `NOTARY_KEY_FILE` (the `.p8`'s path), `NOTARY_KEY_ID` and `NOTARY_ISSUER_ID`, the secrets' names (§6.4's `GG_MAC_INSTALLER_IDENTITY` was a slip); the macOS readme is `installer/macos/Readme.txt`, not `Readme.rtf` (§6.3, §6.4); the license pane's file is `LICENSE.txt`, the AGPL copied from the license set (Installer picks a pane's format by its extension); `scripts/release/check-version.sh` also takes `--print`, CMake's version with no tag check, for the nightly's numeric part and the dry run's version; `plan` also outputs `tag` and `sign` (true when the kind isn't `none` and all seven signing secrets are set, as a step's `if:` can't read `secrets`), and fails a release without them; the `release` job has `actions: read` beside `contents: write`, to download the run's artifacts; the nightly tag is moved through the GitHub API, as the checkout keeps no credentials; the dry run's version is CMake's own (`0.2.0-dev`), not empty. **The final-review fix wave (2026-10-29)** then made these changes: `plan` fails any installer run with some but not all of the seven secrets, and a nightly that would publish (`GG_PUBLISH_NIGHTLY` true) without them, naming the missing secrets and never a value, and the nightly publishes only when `sign` is true; the macOS import step checks that `MAC_SIGNING_IDENTITY` and `MAC_INSTALLER_IDENTITY` each name a valid identity in the `.p12`, by name or SHA-1 hash; the nightly suffix is `dev.<yyyymmdd>.g<sha7>` (§6.1); `CloseApplicationsFilter` adds `*.clap` (§6.5); notarytool waits at most an hour (`--timeout 1h`) and the DMG's signing retries only a timestamp failure (§6.2); after the signed dry runs hung in the installer step, the keychain ACL fix: the imported key's access list also authorises `pkgbuild` and `productbuild`, which sign the installer, and the import step probes package signing within 90 s (§6.2). The signed dry run is CI run 40978830982 |
| **Milestone** | Between M1 and M2: its own spec, as the director deferred it on 2026-10-29 (INTENT §23 #60). It serves every release from M2's (0.2.0) to 1.0.0, which now follows M5 (decision 8) |
| **Depends on** | M1 closed (PR #11, `95bee95`); the director's Apple Developer Program membership |
| **Authority** | INTENT.md. Part II changes below each add a dated §23 row |

## 1. Intent

Turn a commit into something a musician can install: a signed and notarized macOS installer, a
Windows installer, a Linux archive, published as a GitHub Release by CI, with every license and
notice the products must carry. Today the build stops at `gg-products`, a folder of ad-hoc-signed
bundles; the project version is 0.0.1; the repository has no tags or releases.

**Its first job is the releases: the unpublished milestone drafts 0.2.0 to 0.4.0, then the
public 0.5.0 betas, 0.5.0 and 1.0.0** (decision 12). Nightly builds come only because the same
job on the existing nightly schedule costs nothing extra.

**Director decisions (2026-10-29, in brainstorming):**

1. **The pipeline is for the 1.0 release;** nightlies only if they are cheap. (They are: §6.6.)
2. **macOS builds are signed with the director's Developer ID and notarized,** from GitHub
   Actions secrets. Nothing private is ever committed; the workflow names the secrets and never
   holds their values.
3. **Windows ships unsigned at 1.0** ("route 1"), as Surge XT, Shortcircuit XT and OB-Xf do;
   users see SmartScreen's unknown-publisher warning once. Azure Trusted Signing (about $10 a
   month) and SignPath Foundation's free open-source program are recorded in INTENT as the
   upgrade path, to revisit once downloads make the warning matter.
4. **Releases are numbered by milestone:** M2 is tagged 0.2.0, M3 0.3.0, M4 0.4.0, M5 0.5.0,
   then 1.0.0. "That seems cleaner" than a year of `1.0.0-beta.N`. (Amended by decision 12:
   no betas and nothing public before M5.)
5. **The freeze point splits.** INTENT §18 freezes the stability contracts "at the first public
   release". The first public release (0.5.0's first beta, decision 12) freezes the plugin
   identities and AU codes and the CLAP parameter IDs (with the AUv2 order that follows from
   them), because a session saved with any public build must open in every later one and IDs
   are append-only anyway. Factory preset order and the blocking CPU gate (carry-over rows 144,
   148) wait for 1.0.0, because the presets will still be moving through the 0.5.x releases.
6. **Approach: extend the existing CI workflow** (§4), rather than a second release workflow.
7. The shape the director accepted as "fine": a PKG with one component per format inside a DMG,
   installed system-wide; an Inno Setup installer plus a plain zip; a tar archive; every
   installer carries the notices and license texts (carry-over row 221).
8. **v1.0 moves in the roadmap to after M5 and before M6** (INTENT §20; Part II, so a §23 row).
   M2 Giga, M3 Playable, M4 Rhythmic and M5 Synthesis all ship as 0.x releases first; 1.0.0 is
   the polish of M0 to M5 against §10's success criteria, and M6 Reach follows it. A
   consequence the director has seen: the blocking CPU gate and the baselines for unseen CPU
   models (rows 144, 148) wait until after M5 as well, unless pulled to an earlier milestone
   later.

**Director answers to the builder check (2026-10-29):**

9. **The secrets are set during this work,** so the CI dry run (§8) is part of the deliverable,
   and **the builder may sign locally** with the identities already in the director's login
   keychain, through environment variables only; nothing is written to the tree.
10. **The public nightly starts with M5.** Until then the nightly run still builds and signs the
    installers and keeps them as run artifacts, so the path stays exercised, but publishes no
    `nightly` release. The switch is one committed setting (§6.6), flipped when M5 begins.
11. **Each release ships its complete corresponding source:** a tarball of the tagged tree with
    every submodule at its pin, attached to the release, with the readmes and release notes
    pointing to it and to the SDK tags clap-wrapper fetches (§6.6).
12. **Nothing is public, and there are no betas, before M5** (the director's correction, given
    while Task 1 ran; INTENT §23 #62). M2, M3 and M4 are tagged `v0.2.0`, `v0.3.0`, `v0.4.0`
    and each produces a draft release the director never publishes: their own installers for
    host testing, and the pipeline's rehearsals. M5's `v0.5.0-beta.N` are the first public
    releases, then `v0.5.0`, then `v1.0.0`. The freeze (decision 5) moves to `v0.5.0-beta.1`;
    the public nightly still starts with M5 (decision 10).

**My call, after the builder check:** hotfixes of a superseded minor (a `v0.2.1` while `main` is
`0.3.0-dev`) are supported when needed, not planned for: a `release/0.2` branch is cut from the
`v0.2.0` tag at that moment, the fix is cherry-picked, versioned and tagged there, and the `plan`
job accepts a tag whose commit is on `main` or on a `release/*` branch. `docs/releasing.md`
describes it in a short section.

## 2. Goals, non-goals, anti-goals

**Goals**

- One command builds an installer locally from a configured tree, unsigned when no identity is
  given: `cmake --build --preset release --target gg-installer`.
- A pushed version tag builds, tests, validates, signs, packages and publishes a GitHub Release
  (a draft; §6.6) with the three platforms' installers and their SHA-256s.
- The nightly schedule builds the same, keeps the installers as run artifacts, and from M5 on
  publishes them to a rolling `nightly` pre-release (decision 10).
- Each release carries its complete corresponding source as a tarball (decision 11).
- A pull request or an ordinary push behaves exactly as today: no installer, no secrets.
- The products carry the version the tag names, in the bundles, the CLAP descriptors and the
  installers, and the plugins can say which build they are (`0.2.0`, `0.5.0-beta.1`, or a
  nightly's date and commit).
- Every installer and archive carries `THIRD_PARTY_NOTICES.md`, `LICENSE` (the AGPL), the GPLv3
  text, the AudioUnitSDK's Apache-2.0 text (macOS) and the ASIO SDK's `LICENSE.txt` (Windows),
  closing carry-over row 221's installer half.
- A written release procedure (`docs/releasing.md`) the director can follow, including the
  one-time setup of the secrets and how to rotate them.

**Non-goals**

- Windows code signing (decision 3; the upgrade path is recorded, not built).
- Windows on arm64: the one Windows preset is x64, and stays so.
- Linux distribution packages (deb, rpm, Flatpak), AppImage, or a Linux installer: a tar archive
  with a README, as INTENT §19 says.
- An in-app update check or auto-update, a download page or website, release announcements.
- The About box (row 221's other half): M2's views or later. The notices ship in the installer
  only for now.
- Factory content (CC0 samples, presets; INTENT §12): M2 adds it. §6.9 reserves where it goes
  so M2 doesn't redesign the installers.
- The blocking CPU gate and the baselines for unseen CPU models (rows 144, 148): 1.0.0, after
  M5 (decision 8).
- Any DSP, engine, plugin or UI change. The goldens' WAV hashes don't move.

**Anti-goals**

- A secret, certificate, key, password or team ID in the repository, in a workflow file, in a
  log, or in a build tree that `gg-products` or the installer step copies from.
- A signed installer built from products that skipped a test or a validator.
- A release whose binaries don't say the version its tag names.
- Two copies of the build matrix (SDK fetch, LLVM pin, ccache, validators) to keep in sync.
- A packaging step that changes the bytes the tests ran against (re-linking, re-signing with a
  different identity, stripping): the installer packs what `gg-products` holds.

## 3. Constraints

- INTENT §19: adapted from Shortcircuit XT's GitHub Actions and sst-plugininfra's scripts;
  macOS a signed and notarized PKG in a DMG, Windows Inno Setup, Linux a tar archive.
- INTENT §15: universal macOS binaries (arm64 + x86_64), minimum macOS 11.0; Linux built against
  ubuntu-22.04 so the binaries run on older glibc (CI's Linux leg already runs there).
- INTENT §18: the identities and the parameter IDs are frozen from the first public release,
  0.5.0's first beta (decisions 5 and 12);
  the existing `identity_*` and `codesign_*` tests and `tests/core/patch_tests.cpp` keep pinning
  them.
- Licensing (CLAUDE.md, INTENT §15): only AGPLv3-compatible code; anything taken from
  sst-plugininfra's or sst-cmake's installer templates (both MIT) keeps its notice. The ASIO SDK
  rules are unchanged: the Windows installer carries the SDK's `LICENSE.txt` and uses no ASIO
  logo.
- CI policy (`.github/workflows/ci.yml`): every third-party action pinned by commit SHA; the
  workflow's default permission is `contents: read`; the nightly runs at 03:17 UTC; caches are
  saved only from `main`.
- `scripts/check.sh` and the format check stay the gate; CI via a draft PR.
- The carry-over must not grow: this work closes row 221's installer half and opens nothing.
- Determinism (INTENT §14): nothing here touches the audio path. The version string is a
  compile-time constant already (`GG_VERSION_STRING`).

## 4. Approaches

- **Extend `ci.yml` (chosen).** A tag or the schedule turns on a package step per build leg and
  one publish job. Packaging is a CMake target, `gg-installer`, so it runs locally too. The
  installer is packed from the very products the leg's tests and validators just passed, with
  one build matrix and one set of caches. Cost: `ci.yml` grows, and its conditions must keep
  pull requests away from the secrets and the publish step (§6.6 spells them out).
- *A separate `release.yml`, as Shortcircuit XT has:* rejected. Cleaner to read, but a second
  copy of the build setup that drifts from the first; the SDK cache, the LLVM pin and the
  Rosetta step were each fixed once already.
- *CPack:* rejected. Its productbuild and NSIS generators fight per-format install locations,
  notarization and Inno Setup, which INTENT names.

## 5. What exists, and what changes

| Piece | Today | After |
|---|---|---|
| `CMakeLists.txt` | `project(GigaGrains VERSION 0.0.1)`; `GG_VERSION_STRING` = the numeric version | `0.2.0` plus a pre-release suffix, `dev` by default (§6.1) |
| `clients/clap-first/CMakeLists.txt` | Signs each bundle ad hoc (or with `GG_MACOS_CODESIGN_IDENTITY`) after it links | With a real identity, also hardened runtime, a secure timestamp and the standalones' entitlements (§6.2) |
| `cmake/products.cmake` | `gg-products` gathers the bundles | Also copies the license set next to them; new `gg-installer` target (§6.3) |
| `.github/workflows/ci.yml` | Builds, tests, validates, benchmarks; uploads goldens and bench JSON | Adds the signing setup (macOS, releases only), the installer step and artifact per leg, and the `release` job (§6.6) |
| Repository secrets | None (`gh api .../actions/secrets` reports 0) | The seven in §6.7, set by the director by hand |
| `THIRD_PARTY_NOTICES.md` | Says the installers must carry it | Its paragraph about the installer work is updated to say they do |
| `docs/m1-carry-over.md` | Row 221 blocked on the installers | Row 221's installer half closed; the About box half stays, reworded |
| INTENT | §19's three-line sketch; §22's open Windows question | §7 below |
| CLAUDE.md | No release section | A short "Releasing" paragraph pointing at `docs/releasing.md` |

## 6. The how, as decided

### 6.1 Versioning (my call, accepted in outline; the milestone numbering is decision 4)

- **Semantic versions, numbered by milestone before 1.0.** Tags are `v<major>.<minor>.<patch>`
  with an optional pre-release suffix. M2 is tagged `v0.2.0`, M3 `v0.3.0`, M4 `v0.4.0` (drafts,
  never published; no betas, decision 12), M5 `v0.5.0` with betas `v0.5.0-beta.1`,
  `v0.5.0-beta.2`, … before it, then `v1.0.0`; a fix after a milestone release bumps the patch
  (`v0.2.1`). A tag with a suffix makes the draft a pre-release; a bare tag a full release.
  Before 1.0.0, semver promises nothing about compatibility between minors; INTENT §18's
  freeze (decision 5) promises more from `v0.5.0-beta.1`, and the release notes say so.
- **The version lives in `CMakeLists.txt`**, nothing is derived from git at configure time, so
  two checkouts of one commit build the same bytes:
  - `project(GigaGrains VERSION 0.2.0 …)` holds the numeric part;
  - a new variable next to it, `GG_VERSION_PRERELEASE`, holds the suffix, **`dev` by default.**
    A build from `main` therefore reports `0.2.0-dev`, and never claims to be a release;
  - `GG_VERSION_STRING` becomes `<numeric>` when the suffix is empty, else
    `<numeric>-<suffix>`. It already reaches the CLAP descriptors (`GigaGrainsPlugin.cpp`),
    `gg::kVersion` and the tests (`smoke_tests.cpp` expects `MAJOR.MINOR.PATCH`; it is updated
    to accept the suffix).
- **Release commits.** A release is one commit that sets the suffix (`beta.1`, or empty for
  the milestone release) and adds the release notes (§6.6), tagged with the matching tag. The
  next commit on `main` sets the suffix back to `dev`, and after a milestone release bumps the
  minor to the next milestone's (`0.3.0` + `dev` after `v0.2.0`). The release job
  **fails before it signs anything** if the tag and the CMake version disagree, or if the suffix
  is `dev`.
- **Nightlies** configure with `-DGG_VERSION_PRERELEASE=dev.<yyyymmdd>.g<sha7>` on the schedule
  run only, so a nightly plugin reports, say, `0.2.0-dev.20261029.g95bee95`. The `g` before the
  hash, as `git describe` writes it, keeps a hash of digits alone with a leading zero a valid
  semver identifier (semver forbids a numeric identifier's leading zero). (Only the nightly run
  differs from a plain CI build, and only in that string.)
- **Bundle metadata takes the numeric part alone.** Apple's `CFBundleShortVersionString` and
  `CFBundleVersion` and Inno Setup's `VersionInfoVersion` must be numeric; they get `0.2.0`. The
  suffix appears in the full version string, the installer file names and the release name.
  The AU's `version` integer that clap-wrapper derives from the bundle version follows the
  numeric part too.
- **Where the current number goes now:** this change sets `main` to `0.2.0` + `dev`: M2 is the
  next milestone and the first to ship, and nothing has shipped under 0.0.1.

### 6.2 macOS signing (release builds only)

- **Identity.** The CI macOS leg configures with `-DGG_MACOS_CODESIGN_IDENTITY="Developer ID
  Application: …"` on a tag or the nightly, so the products are signed as they are built, by the
  existing per-bundle step, and the `codesign_*` tests then verify a Developer ID signature with
  `--strict`. Locally and on every other run the identity stays `-`.
- **What a real identity adds** (in `clients/clap-first/CMakeLists.txt`, only when the identity
  isn't `-`): `--options runtime` (hardened runtime, which notarization requires),
  `--timestamp`, and for the two standalone apps an entitlements file,
  `installer/macos/standalone.entitlements`, with `com.apple.security.device.audio-input`: the
  FX standalone records live input, and the hardened runtime blocks the microphone without it
  (`NSMicrophoneUsageDescription` is already in the apps' `Info.plist`). The plugin bundles get
  no entitlements; they run in the host's process. Signing stays inside-out and per bundle, as
  today, with no `--deep`.
- **The keychain.** A step runs only on a tag or the schedule: it decodes the `.p12` secret, makes
  a throwaway keychain with a random password, imports the certificates, lets `codesign`,
  `pkgbuild`, `productbuild`, `productsign` and `security` use them without a prompt (`security
  import -T` and `security set-key-partition-list`), and deletes the keychain in an `always()`
  step at the end. My call: a dozen lines in `scripts/release/mac-keychain.sh` rather than
  `apple-actions/import-codesign-certs`, so there's no third-party action to pin and the script
  can be read and run locally; the builder may use the action, pinned by SHA, if the script
  proves awkward.
- **Notarization** uses an App Store Connect API key (`xcrun notarytool submit --key … --key-id
  … --issuer … --wait`), not the director's Apple ID and an app-specific password (my call: the
  key is revocable on its own and isn't tied to the account password; Apple recommends it).
  The installer script notarizes and staples the PKG, then signs, notarizes and staples the DMG
  (two submissions, so both the DMG and a PKG dragged out of it pass Gatekeeper offline).
- **A Gatekeeper check** closes the step: `spctl --assess --type install` on the PKG and
  `spctl --assess --type open --context context:primary-signature` on the DMG must accept them.
  `codesign --verify --strict --deep` on every product inside the staged root as well.

### 6.3 The `gg-installer` target

In `cmake/products.cmake`, after `gg-products`:

- `gg-installer` depends on `gg-products` and writes into `build/<preset>/gg-installer/`.
- `gg-products` also assembles the **license set**, `build/<preset>/gg-products/licenses/`:
  `THIRD_PARTY_NOTICES.md`; `LICENSE` (AGPL-3.0); `GPL-3.0.txt` (the GPLv3 text, copied from
  `libs/sst/sst-basic-blocks/LICENSE.md`, which is that text); on macOS `AudioUnitSDK-LICENSE.txt`
  from the SDK checkout clap-wrapper uses (its path is in the CMake cache as the AudioUnitSDK's
  source dir); on Windows `ASIO-SDK-LICENSE.txt` from `libs/sst/sst-plugininfra/libs/asiosdk/
  LICENSE.txt`; and the two MIT texts the pinned checkouts lack, CMakeRC's and flowbite-icons',
  which `THIRD_PARTY_NOTICES.md` already reproduces: the builder puts them in
  `installer/licenses/` as files, copied from upstream, with their URLs in a README there.
  A CTest test (label `installer`) checks that every file the set must have is present and
  non-empty in a configured tree, per platform.
- Per platform, the target runs one script with the product directory, the version strings and
  the output directory as arguments. The scripts live in `installer/` (new, top level):
  `installer/macos/make-installer.sh`, `installer/windows/giga-grains.iss` and
  `installer/windows/make-zip.cmake`, `installer/linux/make-tarball.sh`, plus the resources
  (`installer/macos/{distribution.xml.in, Readme.rtf, standalone.entitlements}`,
  `installer/windows/{readme.txt}`, `installer/linux/README.md`, `installer/licenses/`).
- **Output names**, with `<v>` the full version string (`0.2.0`, `0.5.0-beta.1`, `1.0.0`,
  `0.2.0-dev.20261029.g95bee95`):
  - `giga-grains-<v>-macos.dmg`
  - `giga-grains-<v>-windows-x64.exe` and `giga-grains-<v>-windows-x64.zip`
  - `giga-grains-<v>-linux-x86_64.tar.gz`
  - `giga-grains-<v>-source.tar.gz`, made by the `release` job from the tagged tree with every
    submodule checked out at its pin (`git archive` can't do it alone; a script in
    `scripts/release/` walks `git submodule foreach`), with `.git` directories left out

  The `release` job adds `SHA256SUMS.txt` over all of them.

### 6.4 macOS: the PKG and the DMG

`installer/macos/make-installer.sh` is ours, derived from sst-plugininfra's
`scripts/installer_mac/make_installer.sh` (MIT; its notice kept at the top), because that
script hard-codes Surge's package identifiers, handles one app, and notarizes with an Apple ID.
Ours:

- Builds one component package per product with `pkgbuild`, each from a staging directory that
  holds that product alone, signed with the Developer ID **Installer** certificate when
  `GG_MAC_INSTALLER_IDENTITY` is set:

  | Choice (default on) | Identifier | Installs to |
  |---|---|---|
  | Giga Grains CLAP | `org.sethbc.giga-grains.clap.pkg` | `/Library/Audio/Plug-Ins/CLAP` |
  | Giga Grains VST3 | `org.sethbc.giga-grains.vst3.pkg` | `/Library/Audio/Plug-Ins/VST3` |
  | Giga Grains Audio Unit | `org.sethbc.giga-grains.component.pkg` | `/Library/Audio/Plug-Ins/Components` |
  | Giga Grains app | `org.sethbc.giga-grains.app.pkg` | `/Applications` |
  | Giga Grains FX app | `org.sethbc.giga-grains-fx.app.pkg` | `/Applications` |

  Each bundle holds both plugins (the two-descriptor CLAP), so one bundle per format is right.
  The apps install straight to `/Applications` with `--install-location`; sst-plugininfra's
  detour through `/tmp` and a postinstall `rsync` is not needed (my call; the builder confirms
  an installed app launches for a non-admin user, and adds a postinstall `chown` only if it
  doesn't).
- `productbuild` with `installer/macos/distribution.xml.in` (configured with the version):
  the title `Giga Grains <v>`, the AGPL as the license pane, `Readme.rtf` (what gets installed
  where, the first-class hosts, the project URL, where the notices are), `hostArchitectures=
  "x86_64,arm64"`, `allowed-os-versions` 11.0, and `customize="always"` so the choices show.
  Signed with the Installer identity, notarized and stapled (§6.2).
- The DMG (`hdiutil`, UDZO) holds the PKG and a `Licenses` folder with the license set. Signed
  with the Application identity, notarized and stapled. No custom icon or background
  (sst-plugininfra's `Rez`/`SetFile` steps are dropped; `SetFile` is deprecated).
- Unsigned mode (no identities in the environment): the same steps without `--sign`,
  notarization or stapling, for local builds and for the `installer` tests.
- The PKG's version is the numeric part; the choices carry the full string in their titles.

### 6.5 Windows and Linux

**Windows.** `installer/windows/giga-grains.iss`, ours, adapted from sst-cmake's
`innosetup/installer.iss` (MIT, notice kept), compiled by the `ISCC.exe` the `windows-2022`
runner image ships (found with `find_program`; its version is printed in the log; a tree
without it builds the zip only and says so, and the release job requires the installer):

- `AppId` a fixed GUID (generated once, committed), `AppName` "Giga Grains", `AppPublisher`
  "sethbc" (the vendor in the CLAP descriptors and INTENT §23 #20), `AppVersion` the full string,
  `VersionInfoVersion` the numeric part, `LicenseFile` the AGPL, `ArchitecturesAllowed=
  x64compatible`, `ArchitecturesInstallIn64BitMode=x64compatible`, 64-bit only.
- Components, all on by default: CLAP → `{commoncf}\CLAP\Giga Grains.clap`; VST3 →
  `{commoncf}\VST3\Giga Grains.vst3\` (a folder bundle, as the build makes it); the two
  standalones → `{autopf}\Giga Grains\`, with Start-menu entries. The license set goes to
  `{autopf}\Giga Grains\licenses\` always (a fixed component).
- `CloseApplicationsFilter=*.exe,*.vst3,*.clap`, sst-cmake's `*.exe,*.vst3` plus the CLAP, so a
  running host is asked to close.
- The zip holds the same tree as `gg-products` plus `licenses/` and `readme.txt` (where to put
  what), made with `cmake -E tar cf … --format=zip`.

**Linux.** `installer/linux/make-tarball.sh` writes `giga-grains-<v>-linux-x86_64.tar.gz`
containing a top-level `giga-grains-<v>/` with `Giga Grains.clap`, `Giga Grains.vst3/`, the two
standalones, `licenses/` and `README.md` (copy the CLAP to `~/.clap` or `/usr/lib/clap`, the
VST3 to `~/.vst3` or `/usr/lib/vst3`, the apps anywhere; built on Ubuntu 22.04, glibc 2.35, x86_64
only). Executable bits are preserved (tar, not zip; INTENT §19). No install script.

### 6.6 The workflow

Changes to `.github/workflows/ci.yml`, all conditional, so a pull request and a push to `main`
run exactly today's steps:

- **Triggers:** add `push: tags: ['v*']`. The schedule and `workflow_dispatch` already exist.
  A new dispatch input, `installers` (boolean, default false), builds and signs the installers
  and uploads them as artifacts without publishing anything: the dry run that proves the
  secrets before the first tag, and the way to rebuild an installer for a look.
- **A `release_kind` expression** used by every new condition, computed once in a small first
  job (`plan`) that also checks the tag against the CMake version (§6.1) and fails fast:
  `release` (a `v*` tag), `nightly` (the schedule on `main`), `dry-run` (dispatch with
  `installers: true`), else `none`. Every new step runs only when it isn't `none`, and only when
  `github.repository == 'sethbc/giga-grains'`, so a fork's schedule never tries to sign.
- **Build legs** (after the validators, so an installer is packed only from products that passed
  everything): on macOS, the keychain step (only `release` and `nightly`, and `dry-run`), the
  configure gets the identity and the standalones' entitlements; then `cmake --build --target
  gg-installer`; then `upload-artifact installer-<preset>`. The nightly passes the
  `GG_VERSION_PRERELEASE` suffix at configure. The signing environment variables are set only on
  the installer step, from secrets, never echoed; the keychain is deleted in an `always()` step.
- **The `release` job** (`needs: [build, sanitizers]`, so a red sanitizer run blocks a release;
  `permissions: contents: write` on this job alone; `runs-on: ubuntu-24.04`): downloads the
  three installer artifacts, writes `SHA256SUMS.txt`, and with `gh release` and the
  `GITHUB_TOKEN` (no third-party action):
  - `release`: creates a **draft** release for the tag, pre-release if the version has a suffix,
    titled `Giga Grains <v>`, body from `docs/releases/<tag>.md` (the release commit adds it;
    the `plan` job fails if it is missing), with the installers, the source tarball and the
    checksums attached. The body and every readme carry the corresponding-source directions:
    the tag, `git clone --recurse-submodules`, the source tarball, and the SDK tags
    clap-wrapper's `base_sdks.cmake` pins (`scripts/fetch-sdks.sh` fetches them), which GPLv3
    §6(d) asks to be stated next to the binaries. **The director
    publishes the draft by hand** after installing from the real DMG and EXE and running the host
    smoke in Live and Bitwig (`docs/releasing.md`). That click is the one human gate on the
    exact bytes users get.
  - `nightly`: **only when the committed switch is on** (an `env` constant at the top of
    `ci.yml`, `GG_PUBLISH_NIGHTLY: "false"` until M5 begins; decision 10): moves the `nightly`
    tag to the commit, and creates or updates the `nightly` pre-release (`gh release upload
    --clobber`, old assets of other versions deleted first), body from
    `installer/nightly-notes.md` (fixed text: what a nightly is, the commit, that it is
    unsupported and that sessions saved with it may not open later). Published directly, not a
    draft. With the switch off, the job stops after the artifacts, and says so in the summary.
  - `dry-run`: nothing; the artifacts are the output.
- **Secrets never reach a pull request**: the new steps' conditions exclude `pull_request`
  outright, besides GitHub withholding secrets from fork PRs. The concurrency group is unchanged
  (a tag run has its own group).
- **What a release run costs:** one normal CI run plus notarization (typically a few minutes per
  submission, two submissions) and the release job. The nightly already runs the full benchmark;
  the installers add a few minutes.

### 6.7 Secrets (set by the director, by hand, in the repository's Actions secrets)

| Secret | Holds | Made how |
|---|---|---|
| `MAC_CERTS_P12` | Base64 of a `.p12` exporting both certificates, *Developer ID Application* and *Developer ID Installer*, with their private keys | Keychain Access → export both → `base64 -i certs.p12 \| pbcopy` |
| `MAC_CERTS_P12_PASSWORD` | The export password | |
| `MAC_SIGNING_IDENTITY` | The Application certificate's full name, `Developer ID Application: <name> (<team>)` | `security find-identity -v -p codesigning` |
| `MAC_INSTALLER_IDENTITY` | The Installer certificate's full name | |
| `NOTARY_KEY_P8` | Base64 of the App Store Connect API key (`AuthKey_<id>.p8`), Developer role | App Store Connect → Users and Access → Integrations → Team keys |
| `NOTARY_KEY_ID` | Its key ID | |
| `NOTARY_ISSUER_ID` | The issuer ID | |

`docs/releasing.md` carries this table, the export steps in full, the dry run that proves them
(dispatch with `installers: true`, then `spctl` on the downloaded DMG), and rotation: replace the
secret, dispatch a dry run. Certificates expire (Developer ID Application five years, Installer
five years); the doc says to note the dates. No secret is needed for Windows or Linux.

### 6.8 The release procedure (`docs/releasing.md`, written by this work)

1. **Before the first public release** (0.5.0's first beta, M5), once: the freeze. INTENT §18's
   freeze clause is amended (§7) and the release notes say that sessions saved from this build
   onward will open in every later release, 1.0.0 included. The `identity_*`, `codesign_*` and
   parameter-ID tests already pin what is frozen. The 0.2.0, 0.3.0 and 0.4.0 drafts are never
   published (decision 12); the procedure's publish step is skipped for them.
2. **Each release:** the carry-over and INTENT are current; the first-class host matrix passed on
   a build of the release candidate (INTENT §11: bugs in Live or Bitwig block a release); for a
   milestone release, its listening gate passed; `scripts/check.sh` green; the release notes
   written at `docs/releases/v<version>.md`; the version set and the suffix not `dev`; one
   commit, merged to `main` through a PR; the tag pushed.
3. **After the run:** download the DMG and the EXE from the draft, install them on a clean user
   account (macOS) and on Windows, open each format in Live and Bitwig, then publish the draft.
   The next commit resets the suffix to `dev`.
4. **Unsigned local build,** for anyone without the secrets: `cmake --build --preset release
   --target gg-installer`, with a note that the PKG will be unsigned and Gatekeeper will object.
5. **A signed local build,** for the director: the same with `GG_MACOS_CODESIGN_IDENTITY` set at
   configure and the installer identities and notary key in the environment (the variable names
   match §6.7's secrets), nothing in the tree (decision 9).
6. **A hotfix of an earlier minor** (my call, above): the `release/0.x` branch, the cherry-pick,
   the patch bump and the tag.

### 6.9 Reserved for M2: factory content

So the installers don't get redesigned when M2 adds the CC0 samples and presets (INTENT §12), the
layout is fixed now and left empty: a *Content* choice (macOS `/Library/Application Support/
sethbc/Giga Grains/`, Windows `{commonappdata}\sethbc\Giga Grains\`, Linux inside the tarball
as `content/`, with the README naming `~/.local/share/sethbc/Giga Grains/`), installed only when
the content exists in the tree. The plugins' content search path is M2's design. Nothing is built
for it now beyond leaving room in `distribution.xml.in` and the `.iss`. (My call.)

## 7. INTENT changes (Part II, with the §23 row in the same change)

**When:** the director asked for a docs consistency pass on 2026-10-29, the same day. That pass
applies every amendment below, with §23 #61 recording decisions 1 to 11, ahead of the build, so
INTENT states the decisions as soon as they are made. The build then changes only what it
learns: the `gg-installer` command, the switch's name, `docs/releasing.md`'s path, and CLAUDE.md's
"Releasing" paragraph.

- **§19, "CI and release pipeline":** replace the three-line sketch with the shape above: the
  `gg-installer` target, what each platform ships, signing and notarization from secrets,
  the draft release and the director's publish, the nightly, the dry run. Add a "Versioning"
  paragraph (§6.1): semver, the `dev` suffix, the tag check, nightly suffixes.
- **§20, the roadmap:** the "→ v1.0" row moves from after M2 to after M5, before M6: "M0 to M5
  polished; success criteria (§10) met". M2 to M4 each gain "tagged 0.<n>.0, unpublished" and
  M5 "ships as 0.5.0; its betas are the first public releases" in their Delivers column, and the paragraph "Why this order after v1.0" becomes "Why this order before v1.0",
  its three reasons unchanged. Part I (§10) is untouched. (Decision 8.)
- **§18, "Frozen at first release":** "Frozen from the first public release (v0.5.0-beta.1):
  CLAP parameter IDs, the AUv2 order that follows from them, plugin identities and AU codes.
  Frozen from 1.0.0: factory preset order." The parameter table's "frozen at the first public
  release" follows.
- **§16, §19, §20, and carry-over rows 144 and 148:** "at 1.0" is read as 1.0.0, after M5, not
  the 0.x releases (decisions 5 and 8). One sentence each.
- **§19, "Versioning":** the milestone numbering (§6.1), the nightly switch (decision 10), the
  source tarball (decision 11) and the hotfix branch.
- **§21, the Windows signing risk row:** the mitigation becomes decision 3 (unsigned at 1.0;
  Trusted Signing or SignPath later).
- **§22:** "Windows signing route and budget" resolved 2026-10-29: route 1; the upgrade path is
  recorded in §19.
- **§23 #61:** the director's decisions 1 to 8 and this spec's path.
- **Carry-over row 221:** the installer half closed by this work's commits; the row reworded to
  the About box alone, "M2's views or later".

## 8. Verification

- **Tests, label `installer`, run by `check.sh` as part of the suite:**
  - the license set is complete per platform in a configured tree (§6.3);
  - the unsigned macOS installer builds from a `gg-products` tree, and `pkgutil --expand` of the
    PKG lists the five components with their identifiers and install locations, and the
    `distribution.xml` carries the version and the two architectures (macOS only, with the
    editor; marked `RUN_SERIAL` with the `codesign_*` tests' bundles as its input);
  - the Windows zip and the Linux tarball list exactly the expected entries, including
    `licenses/` and the readme (their platforms only);
  - the version string: `GG_VERSION_STRING` matches `<numeric>[-<suffix>]` with the default
    suffix `dev`, the bundles' `CFBundleShortVersionString` is the numeric part, and the CLAP
    descriptors report the full string (`factory_tests.cpp`, `smoke_tests.cpp` updated);
  - the tag check script (§6.6) accepts `v0.5.0-beta.1` against `0.5.0` + `beta.1` and `v1.0.0`
    against `1.0.0` + an empty suffix, rejects a mismatch and rejects the suffix `dev` (label
    `scripts`).
- **The signed path is verified locally first** (decision 9: the director's identities, in the
  environment), then **by a dry run on CI** (dispatch, `installers: true`) once the secrets are
  set, before the first tag: the artifacts' DMG passes `spctl` on the director's machine, the
  PKG installs, and the `codesign_*` tests passed on the leg with the Developer ID signature.
  Its run ID goes in the build report.
- **The source tarball** unpacks, and `cmake --preset dev` configures from it offline apart from
  the SDK fetch (a test of the script, label `scripts`: every submodule path in `.gitmodules`
  is present and non-empty in the archive).
- **A rehearsal release:** the first real tag, `v0.2.0`, comes with M2 and is itself an
  unpublished draft (decision 12). The builder's final report includes a dry-run run ID and, if
  the director agrees to tag one, a rehearsal draft release from a throwaway tag
  (`v0.1.99-rc.1`, say, deleted afterwards with its tag), so the whole path has run once before
  M2 needs it.
- **No behaviour change:** `scripts/update-goldens.sh` before and after, every WAV SHA-256
  matching; `scripts/check.sh` green on `dev`, `asan`, `rtsan`; the format check; the carry-over
  open-row count not higher at the end than at the start (13).

## 9. Left to the builder

- The scripts' exact shape, error handling and logging; whether the macOS keychain step is the
  script or the pinned action (§6.2); how `plan` passes `release_kind` and the version to the
  legs (job outputs).
- The exact `distribution.xml` and `.iss` text, the readme wording, the Inno `AppId` GUID.
- How the AudioUnitSDK license path is found in the CMake cache, and the fallback if clap-wrapper
  renames it (fail the configure with a message naming the variable).
- Whether `gg-products` or `gg-installer` owns the license set copy (the spec puts it in
  `gg-products` so the zip and tarball and the tests share it).
- The `installer` tests' mechanics (a CMake script test or a Catch2 case; they need no audio).
- `docs/releasing.md`'s structure, within §6.7 and §6.8.
- The order of commits, so that at no point does `main` claim a release it hasn't made.
