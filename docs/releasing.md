# Releasing Giga Grains

The runbook for the director: the one-time setup of the signing secrets, each release from the
version commit to the publish click, a hotfix of an older minor, local installer builds, the
nightly, and what to do when a step fails. The design is
`docs/superpowers/specs/2026-10-29-release-pipeline-design.md` (§6.7 and §6.8 are this file);
INTENT §19 ("CI and release pipeline", "Versioning") states the decisions. The workflow,
`.github/workflows/ci.yml`, is the source of truth for what CI does; its header says the same
in brief.

**Nothing is public, and there are no betas, before M5** (INTENT §23 #62). M2, M3 and M4 are
tagged `v0.2.0`, `v0.3.0` and `v0.4.0`, and each makes a draft release that stays a draft: the
director's own installers for host testing, and the pipeline's rehearsals. M5's `v0.5.0-beta.1`
is the first release anyone else sees; then the further betas, `v0.5.0`, and `v1.0.0`.

## Overview

CI's `plan` job decides what a run releases, its `release_kind`, before anything builds. Three
kinds build installers; everything else (every pull request, every push to `main`, a dispatch
without `installers`, any run in a fork) is `none` and builds none, and no step that reads a
secret runs.

| Run | `release_kind` | Builds | Publishes |
|---|---|---|---|
| A pushed `v*` tag | `release` | The installers, always signed; the source tarball; `SHA256SUMS.txt` | A **draft** GitHub Release of the tag, a pre-release when the version has a suffix (`v0.5.0-beta.1`). The director publishes it by hand, and only from `v0.5.0-beta.1` on |
| The nightly schedule (03:17 UTC) on `main` | `nightly` | The same, with the version `<X.Y.Z>-dev.<yyyymmdd>.<sha7>`, signed when the secrets exist | Nothing while `GG_PUBLISH_NIGHTLY` in `ci.yml` is `"false"`: the installers are the run's artifacts. From M5, when it is `"true"`, the rolling `nightly` pre-release |
| `workflow_dispatch` with `installers: true` | `dry-run` | The same, at CMake's own version (`0.2.0-dev`), signed when the secrets exist, else unsigned | Nothing: the installers are the run's artifacts, and the step summary lists the files and their checksums |

Each build leg packs its installer only after every test and validator on it passed, from the
very products they ran against (`cmake --build ... --target gg-installer`), and uploads it as the
artifact `installer-<preset>`. The `release` job runs only after `plan`, all three legs and the
sanitizers passed. The files, with `<v>` the full version:

- `giga-grains-<v>-macos.dmg`: the PKG (five choices: the CLAP, VST3 and AU into
  `/Library/Audio/Plug-Ins/`, the two apps into `/Applications`) and a `Licenses` folder;
- `giga-grains-<v>-windows-x64.exe` (Inno Setup) and `giga-grains-<v>-windows-x64.zip`,
  unsigned (INTENT §19);
- `giga-grains-<v>-linux-x86_64.tar.gz`;
- `giga-grains-<v>-source.tar.gz`, the tagged tree with every submodule at its pin (the
  complete corresponding source), made by the `release` job with
  `scripts/release/source-tarball.sh`;
- `SHA256SUMS.txt` over all of them.

## One-time setup

### The seven secrets

They are the repository's Actions secrets (Settings → Secrets and variables → Actions), set by
hand. The workflow names them and never holds a value; nothing private is ever committed. None
is needed for Windows or Linux.

| Secret | Holds | Made how |
|---|---|---|
| `MAC_CERTS_P12` | Base64 of a `.p12` exporting both certificates, *Developer ID Application* and *Developer ID Installer*, with their private keys | Keychain Access → export both → `base64 -i certs.p12 \| pbcopy` |
| `MAC_CERTS_P12_PASSWORD` | The export password | |
| `MAC_SIGNING_IDENTITY` | The Application certificate's full name, `Developer ID Application: <name> (<team>)` | `security find-identity -v -p codesigning` |
| `MAC_INSTALLER_IDENTITY` | The Installer certificate's full name | |
| `NOTARY_KEY_P8` | Base64 of the App Store Connect API key (`AuthKey_<id>.p8`), Developer role | App Store Connect → Users and Access → Integrations → Team keys |
| `NOTARY_KEY_ID` | Its key ID | |
| `NOTARY_ISSUER_ID` | The issuer ID | |

**The certificates.**

1. In Keychain Access, under the login keychain's *My Certificates*, select both Developer ID
   certificates (each with its private key beneath it) and the **Developer ID Certification
   Authority** intermediate that issued them. Include the intermediate: without it CI's
   throwaway keychain may list the identities as not valid, and `codesign` won't use them.
   Keychain Access exports the chain when the certificates and their chain are selected. If the
   intermediate isn't in the login keychain (macOS keeps a copy in *System Roots*, which can't
   be exported with the others), download it from https://www.apple.com/certificateauthority/
   and import it into the login keychain first.
2. File → Export Items…, format *Personal Information Exchange (.p12)*, with a strong export
   password. Save it outside the repository.
3. Set the secrets. `gh secret set` reads a piped value, so nothing lands in the shell's history
   or the clipboard:

   ```sh
   base64 -i certs.p12 | gh secret set MAC_CERTS_P12 --repo sethbc/giga-grains
   gh secret set MAC_CERTS_P12_PASSWORD --repo sethbc/giga-grains   # prompts for the value
   ```

   (Or `base64 -i certs.p12 | pbcopy` and paste it in the web page, as the table says.) Then
   delete `certs.p12`.
4. The names: `security find-identity -v -p codesigning` lists the Application identity, and
   `security find-identity -v -p basic` lists both (the Installer certificate isn't a
   code-signing one). Copy each full name, in its quotes' contents, `Developer ID
   Application: <name> (<team>)` and `Developer ID Installer: <name> (<team>)`, into
   `MAC_SIGNING_IDENTITY` and `MAC_INSTALLER_IDENTITY` with `gh secret set` (it prompts).

**The App Store Connect API key** (notarization; spec §6.2: a key, not the Apple ID and an
app-specific password, so it can be revoked on its own):

1. App Store Connect → Users and Access → Integrations → App Store Connect API → *Team Keys* →
   generate a key with the **Developer** role.
2. Download `AuthKey_<key id>.p8`. Apple offers the download once; keep the file somewhere safe
   outside the repository (the local signed build below uses it too).
3. The page shows the key's ID beside it and the **Issuer ID** above the list.
4. Set the secrets:

   ```sh
   base64 -i AuthKey_<key id>.p8 | gh secret set NOTARY_KEY_P8 --repo sethbc/giga-grains
   gh secret set NOTARY_KEY_ID --repo sethbc/giga-grains      # prompts
   gh secret set NOTARY_ISSUER_ID --repo sethbc/giga-grains   # prompts
   ```

`gh secret list --repo sethbc/giga-grains` should then name all seven.

### The dry run that proves them

```sh
gh workflow run ci.yml --ref main -f installers=true
```

(`--ref <branch>` runs a branch's workflow instead.) When the run is green:

- `plan`'s summary says `release_kind=dry-run, … sign=true`;
- the macOS leg's *Build the installer* summary says `signed: yes` (the step fails if
  make-installer.sh's last line disagrees with whether the secrets were there), and its
  `codesign_hardened_*` tests ran and passed (they exist only with a real identity);
- the `release` job's summary says `dry-run: nothing published` and lists the five files and
  their checksums.

Then check the DMG on your own Mac:

```sh
gh run download <run id> --repo sethbc/giga-grains -n installer-mac-universal -D /tmp/gg-dry
spctl --assess -vv --type open --context context:primary-signature /tmp/gg-dry/giga-grains-*-macos.dmg
xcrun stapler validate /tmp/gg-dry/giga-grains-*-macos.dmg
```

`spctl` must say `accepted` and `source=Notarized Developer ID`. Open the DMG and check the PKG
the same way: `spctl --assess -vv --type install <the PKG>`. Install it once (it asks for an
administrator's password) and open the apps and the plugins in a host.

Without the secrets the same dispatch still runs, unsigned: `sign=false` and `signed: no`. That
is how it ran before the secrets existed (run 40951179569, PR #13).

### Rotation and expiry

**To rotate any secret:** replace it (`gh secret set <name>` again, or the web page), then
dispatch a dry run as above. Nothing else holds a copy. To revoke the API key, revoke it in App
Store Connect and make a new one.

**To replace a certificate,** issue a new one in the Apple Developer account (Certificates,
Identifiers & Profiles; an account may hold several Developer ID certificates at once), export
it as above, and rotate the secrets. Leave the old one to expire; don't revoke it. **Revoke a
Developer ID certificate only if its private key is compromised.** The account can't do it:
Apple revokes one on a request to product-security@apple.com, and from then on nothing signed
with it installs, or launches if already installed, notarized or not: every installer made with
it stops working, the 0.x drafts included.

**Both certificates expire,** and CI can't sign after that, so note the dates where you will see
them coming. Keychain Access shows each certificate's expiry when you select it. From the shell,
for the first certificate of a name (a keychain may hold several):

```sh
security find-certificate -c "Developer ID Application" -p | openssl x509 -noout -enddate
security find-certificate -c "Developer ID Installer" -p | openssl x509 -noout -enddate
```

Expiry is far gentler than revocation: installed apps and plugins keep running, and an app
signed while its certificate was valid still downloads and runs. For a PKG, Apple's pages
differ: its Certificates page says a package with a trusted timestamp still installs after its
Installer certificate expires, its Developer ID page that it must be re-signed. So rebuild any
installer still offered after an expiry (a dry run or a new release does).

The director's login keychain on 2026-10-29, checked that day: the identities, the pair made
that day with its private keys, **expire on 2027-02-01**, cut short by the intermediate that
issued them (Apple's original *Developer ID Certification Authority*, which itself expires on
2027-02-01). An older pair, made on 2021-11-07 and held without its keys, expires on 2026-11-08;
it can't sign, and needn't be exported. CI can't sign a release or a nightly after 2027-02-01,
so replace them before it: make the new ones from the **G2 Sub-CA** (the Developer ID
certificate form in the Apple Developer account offers it), whose intermediate expires years
later, check their dates, include the G2 intermediate in the `.p12`, and rotate as above. The
API key doesn't expire.

## Each release

### Once, before the first public release (`v0.5.0-beta.1`, M5)

The freeze. From `v0.5.0-beta.1` on, the CLAP parameter IDs (with the AUv2 order that follows
from them) and the plugin identities and AU codes never change (INTENT §18, already amended for
it; factory preset order freezes at `v1.0.0`). The `identity_*`, `codesign_*` and parameter-ID
tests (`tests/core/patch_tests.cpp`) already pin them. The release notes of `v0.5.0-beta.1`, and
of every release after it, say that sessions saved with it open in every later release, 1.0.0
included (`docs/releases/README.md`).

The 0.2.0, 0.3.0 and 0.4.0 drafts are never published, so skip the publish step for them.

### The checklist

1. **The records are current:** INTENT.md (§20's row for the milestone; §23 for any decision)
   and the carry-over (`docs/m2-carry-over.md` during M2), with no open row a user would meet
   unmentioned in the notes.
2. **The host matrix passed on the release candidate,** a build of the commit you will tag:
   every format in Ableton Live 12 and Bitwig Studio (INTENT §11: a bug in either blocks a
   release; `docs/spikes/m0-host-matrix.md` is the matrix). The installers of a dry run of that
   commit are a convenient candidate.
3. **For a milestone release, its listening gate passed** (`docs/listening/`).
4. **`scripts/check.sh` is green** on the commit (paste its summary in the PR).
5. **The release notes** are written at `docs/releases/v<version>.md` (`docs/releases/README.md`
   has the template; the release job uses the file unchanged as the draft's body).
6. **The version is set** in `CMakeLists.txt`:
   - `project(GigaGrains VERSION X.Y.Z LANGUAGES C CXX)` holds the numeric part;
   - `set(GG_VERSION_PRERELEASE "<suffix>" CACHE STRING …)` beside it holds the suffix: empty
     (`""`) for a milestone release, `beta.N` for a beta, **never `dev`** for a release.

   The notes file and the version change are **one commit**, merged to `main` through a pull
   request (CI's checks green). Between M2 and its release, `main` reads `0.2.0` + `dev`, so
   M2's release commit only empties the suffix.
7. **Check the tag locally** against the merged `main`:

   ```sh
   git switch main && git pull --ff-only
   scripts/release/check-version.sh v<version>
   ```

   It prints `numeric=`, `prerelease=` and `version=` and exits 0 when `v<version>` is `v` plus
   CMake's full version; otherwise it prints why and exits 1. (`--print` shows CMake's version
   with no tag check.)
8. **Tag that commit and push the tag:**

   ```sh
   git tag -a v<version> -m "Giga Grains <version>"
   git push origin v<version>
   ```

   The tag's run is `release`. `plan` checks the tag again, and that the notes file exists,
   `MAC_CERTS_P12` is set and the commit is on `origin/main` (or a `release/*` branch, below),
   before anything builds or signs.
9. **After the run:** the release page (Releases, or `gh release list --repo
   sethbc/giga-grains`) has a draft, `Giga Grains <version>`, with the DMG, the EXE, the zip,
   the Linux tarball, the source tarball and `SHA256SUMS.txt`. Download the DMG and the EXE from
   the draft's assets on that page, then:
   - **macOS:** on a clean user account (a new standard user, no earlier Giga Grains in it or
     in `/Library/Audio/Plug-Ins/`), open the DMG and run the PKG. Gatekeeper must not object.
     Check the five choices installed where the overview says.
   - **Windows:** run the EXE (SmartScreen warns about an unknown publisher once: *More info* →
     *Run anyway*). It installs the CLAP and VST3 under `Common Files` and the apps under
     `Program Files\Giga Grains`.
   - **In Live and Bitwig,** on both: open every format (CLAP in Bitwig; VST3 and AU, on macOS,
     in Live; VST3 in both), both plugins, and launch both standalone apps.
10. **Publish** (from `v0.5.0-beta.1` on only): Edit the draft → *Publish release*, or `gh
    release edit v<version> --repo sethbc/giga-grains --draft=false`. That click is the one
    human gate on the exact bytes users get. A draft of 0.2.0, 0.3.0 or 0.4.0 stays a draft.
11. **The next commit on `main` resets the suffix to `dev`** and, after a milestone release,
    bumps the minor to the next milestone's: after `v0.2.0`, `0.3.0` + `dev`; after
    `v0.5.0-beta.1`, `0.5.0` + `dev` again. Through a PR, like any change.

**A tag whose run failed** before a draft was made can be moved, since nothing was published:
fix the cause on `main` (through a PR), then `git push origin :refs/tags/v<version>`, `git tag
-d v<version>`, and tag and push again. If a draft exists already, the release job refuses to
make a second one; delete the draft first (`gh release delete v<version> --repo
sethbc/giga-grains --yes`, which keeps the tag) and re-run the failed jobs. Never move the tag
of a published release: make a patch release instead.

## Hotfixes of an earlier minor

When a fix must ship for a superseded minor (a `v0.2.1` while `main` is `0.3.0-dev`), cut a
release branch from the tag at that moment:

```sh
git fetch origin --tags
git switch -c release/0.2 v0.2.0
git push origin release/0.2
```

Then, on a branch of `release/0.2`:

1. `git cherry-pick -x <commit>` the fix, which normally lands on `main` first;
2. bump the patch (`project(GigaGrains VERSION 0.2.1 …)`, the suffix stays empty) and add
   `docs/releases/v0.2.1.md`, in one commit;
3. open a pull request **into `release/0.2`**, so CI runs on it (a push to `release/*` alone
   runs no CI), and merge it;
4. check and tag the merged `release/0.2` as above (`check-version.sh v0.2.1`, `git tag -a
   v0.2.1`, `git push origin v0.2.1`).

`plan` accepts a tag whose commit is reachable from `origin/main` or any `origin/release/*`
branch. The rest of the procedure is the same.

## Local builds

**Unsigned,** for anyone without the secrets, from a configured tree:

```sh
cmake --preset release
cmake --build --preset release --target gg-installer
```

It builds `gg-products` first, then writes into `build/release/gg-installer/`: on macOS
`giga-grains-<v>-macos.pkg` and `.dmg` (and the last line `signed: no`), on Windows the zip and,
when Inno Setup 6's `ISCC.exe` is found at configure, the EXE, on Linux the tarball. The bundles
are signed ad hoc, and the PKG is unsigned, so Gatekeeper will object to it: right-click → Open,
or allow it in System Settings → Privacy & Security. Any preset works (`dev` too); the
`installer` tests run the same scripts on the products.

**Signed and notarized,** for the director, with the identities already in the login keychain:

```sh
rm -rf build/release/gg_assets      # the bundles re-sign only when they relink
cmake --preset release -DGG_MACOS_CODESIGN_IDENTITY="Developer ID Application: <name> (<team>)"
export MAC_SIGNING_IDENTITY="Developer ID Application: <name> (<team>)"
export MAC_INSTALLER_IDENTITY="Developer ID Installer: <name> (<team>)"
export NOTARY_KEY_FILE=/path/to/AuthKey_<key id>.p8
export NOTARY_KEY_ID=<key id>
export NOTARY_ISSUER_ID=<issuer id>
cmake --build --preset release --target gg-installer
```

- A real identity at configure makes every bundle's signature carry the hardened runtime and a
  secure timestamp, and the two apps the audio-input entitlement
  (`installer/macos/standalone.entitlements`). The timestamp needs the network.
- `gg-installer`'s script (`installer/macos/make-installer.sh`) signs the packages, notarizes
  and staples the PKG, signs, notarizes and staples the DMG, and asks Gatekeeper to accept both;
  its last line is `signed: yes`. It needs all five variables (the names match the secrets;
  `NOTARY_KEY_FILE` is the `.p8` itself, not its base64), or none: a partial set is refused
  before anything is written.
- Nothing is written into the source tree; the variables live only in that shell. (The build
  tree's `CMakeCache.txt` records the identity's name, which isn't key material.) To go back to
  ad hoc, delete `gg_assets/` again and configure with `-DGG_MACOS_CODESIGN_IDENTITY=-`.
- macOS may ask, once per tool, whether `productbuild` or `codesign` may use the key: *Always
  Allow*.
- An existing tree keeps the `GG_VERSION_PRERELEASE` it was configured with (it's a cache
  variable), so a tree configured before a version commit still says `dev` until you configure
  it with `-DGG_VERSION_PRERELEASE=<suffix>` or start a fresh one. CI configures every run
  afresh.

**A throwaway keychain, as CI makes,** to sign from a `.p12` without the login keychain:

```sh
read -rs P12_PASSWORD && export P12_PASSWORD
scripts/release/mac-keychain.sh import certs.p12 "$TMPDIR/gg-signing.keychain-db"
# ... the signed build above ...
scripts/release/mac-keychain.sh delete "$TMPDIR/gg-signing.keychain-db"
```

The path must end in `.keychain-db` and not exist yet. `import` puts the keychain first in your
search list and prints the identities it finds; `delete` takes it out of the list again. Neither
prints a password.

## Nightlies

The schedule runs every night at 03:17 UTC on `main` as `nightly`: a normal CI run, then the
installers with the version `<X.Y.Z>-dev.<yyyymmdd>.<sha7>` (say `0.2.0-dev.20261029.95bee95`),
signed when the secrets exist. **Until M5 it publishes nothing:** `GG_PUBLISH_NIGHTLY` at the top
of `ci.yml` is `"false"`, the installers stay the run's `installer-*` artifacts, and the release
job's summary says "nightly publishing is off until M5".

**When M5 begins,** a commit (through a PR) sets `GG_PUBLISH_NIGHTLY: "true"`. From then on each
nightly moves the `nightly` tag to its commit and replaces the files of the rolling `nightly`
pre-release, published directly (not a draft), with `installer/nightly-notes.md` (and a line
naming the version and the commit) as its body.

## Troubleshooting

**Notarization rejected.** make-installer.sh prints `notarytool: <file>: submission <id>, status
<status>` and fails, naming the command for the log:

```sh
xcrun notarytool log <submission id> --key AuthKey_<key id>.p8 --key-id <key id> --issuer <issuer id>
```

The log lists each file Apple refused and why: most often a bundle without the hardened runtime
or a secure timestamp (the tree was configured ad hoc and not relinked; see the signed build), or
a binary without a Developer ID signature. A failure before a submission ID was assigned (an
upload or network error, or a bad key) says so; check the three `NOTARY_*` values.

**Gatekeeper refuses an installer.** make-installer.sh ends with `spctl --assess -vv` on the PKG
(`--type install`) and the DMG (`--type open --context context:primary-signature`); run them
yourself on a downloaded copy. `source=Unnotarized Developer ID` means the ticket is missing:
`xcrun stapler validate <file>` says whether it is stapled. `no usable signature` means it
wasn't signed: `pkgutil --check-signature <pkg>` and `codesign -dvv <dmg>` show the signer. For
a product inside, `codesign --verify --strict --deep -vv <bundle>` and `codesign -dvv
--entitlements - <app>`. A dry run without the secrets is unsigned by design (`signed: no`).

**A tag that fails `plan`.** Its summary and the `::error` line give one of these reasons:

- *the tag does not match CMake's version*: `check-version.sh` printed why just above it,
  either a mismatch (the tag `v0.2.0` against CMake's `0.2.1`) or the suffix `dev`, which a
  release never has (the release commit didn't set `GG_VERSION_PRERELEASE`). Fix
  `CMakeLists.txt` through a PR, then move the tag (above);
- *`docs/releases/<tag>.md` is missing* from the tagged commit;
- *the `MAC_CERTS_P12` secret is not set*: a release is always signed (set up the secrets);
- *the tagged commit is on neither `origin/main` nor an `origin/release/*` branch*: tag the
  merged commit on `main`, not the PR branch's.

**The source tarball refuses** (the release job's *Gather the release files* step): a submodule
not checked out, or a tracked file with the `export-ignore` or `export-subst` attribute, which
`git archive` would drop or rewrite. No first-party file has one; a dependency bump could bring
one in a submodule's `.gitattributes`. The remedy is in `scripts/release/source-tarball.sh`:
drop that guard, or stage the files from `git ls-files` instead of `git archive`, so the archive
still holds every tracked file.

**Apple's timestamp server fails** now and then ("timestamps differ by 300 seconds", or a
timeout). make-installer.sh retries the DMG's signing up to three times, 10 s apart. The bundles
are signed while they build, without a retry, so a signing failure in the macOS leg's *Build
gg-all and gg-products* step means re-running the failed job.

**The Rosetta kernel test fails on the macOS leg.** "live interior is exactly where the fade is
one - Hermite" (`tests/engine/kernel_tests.cpp`) fails now and then in *Test the x86_64 slice
under Rosetta* (first seen in run 40920010135, on `main`), and passes on the same code's other
runs. It is a CI-VM flake, not a regression: re-run the failed job once. A second failure is a
real one.

**A step needs a re-run.** `gh run rerun <run id> --failed --repo sethbc/giga-grains` re-runs
the failed jobs of a tag's run; the `release` job then runs once they pass.
