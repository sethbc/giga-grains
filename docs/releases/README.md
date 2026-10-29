# Release notes

Each release's notes are a file here named after its tag: `docs/releases/v0.2.0.md` for the tag
`v0.2.0`, `docs/releases/v0.5.0-beta.1.md` for `v0.5.0-beta.1`. The release commit adds it, beside
the version change (`docs/releasing.md`). CI's `plan` job fails a tagged run whose notes file is
missing, and the `release` job uses the file, unchanged, as the body of the draft GitHub Release
(`.github/workflows/ci.yml`).

The 0.2.0, 0.3.0 and 0.4.0 releases (M2, M3 and M4) are drafts the director never publishes:
their own installers for host testing, and the pipeline's rehearsals (INTENT §23 #62). Their notes
are written all the same. `v0.5.0-beta.1`, M5's first beta, is the first public release, and the
plugin identities, AU codes and CLAP parameter IDs freeze there (INTENT §18): from its notes on,
each release says that sessions saved with it open in every later release.

## What the notes hold

- **The version and the milestone:** the title line names both (`Giga Grains 0.2.0 (M2, Giga)`),
  and says whether the release is a beta.
- **What changed** since the previous release, for a musician: what you can now do, hear or
  load, not the commits. Name a changed behaviour that affects existing sessions.
- **Known issues:** the open carry-over rows a user could meet, and the hosts or platforms with a
  known problem. Say "None known." rather than leave the section out.
- **The installers:** which file is for which platform, and that the Windows installer is
  unsigned (SmartScreen warns once).
- **The corresponding source** (spec decision 11): the tag, the `git clone --recurse-submodules`
  line, the source tarball attached to the release, and where the SDK tags the build downloads
  are named. Copy the section from the template and change only the tag and version.

## Template

````markdown
# Giga Grains <version> (<milestone>)

<One or two sentences: what this release is, and for whom.>

## What changed

- <A change, as a musician meets it.>

## Known issues

- <An issue, and its workaround if there is one.> (Or: None known.)

## The installers

- macOS (11 or later, Apple silicon and Intel): `giga-grains-<version>-macos.dmg`, signed and
  notarized. Open it and run the installer it holds.
- Windows (x64): `giga-grains-<version>-windows-x64.exe`, an installer, or
  `giga-grains-<version>-windows-x64.zip` to copy the files by hand. Both are unsigned, so
  SmartScreen warns about an unknown publisher once.
- Linux (x86_64): `giga-grains-<version>-linux-x86_64.tar.gz`; its README says where each file
  goes.

`SHA256SUMS.txt` holds every file's checksum.

## The corresponding source

Giga Grains is free software, licensed under the GNU Affero General Public License, version 3 or
later. The source of this release is the tag `v<version>` in
https://github.com/sethbc/giga-grains:

```sh
git clone --recurse-submodules --branch v<version> https://github.com/sethbc/giga-grains
```

or the source tarball attached to this release, `giga-grains-<version>-source.tar.gz`, which
holds the tagged tree with every submodule at its pinned commit. The build also uses the VST3
SDK, Apple's AudioUnitSDK, RtAudio, RtMidi and wil, which it downloads: the tags it fetches are
named in `libs/clap/clap-wrapper/cmake/base_sdks.cmake`, and `scripts/fetch-sdks.sh` fetches
those same tags. The README in the source says how to build.
````
