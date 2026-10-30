# Giga Grains nightly

This is a nightly build of Giga Grains and Giga Grains FX: the installers CI makes every night
from the latest commit on `main`, after every test and validator passed. The release always holds
only the latest night's files, and the `nightly` tag moves to the commit they were built from.
Its version is the next release's number with the suffix `dev.<yyyymmdd>.g<sha7>`: the date, and
`g` and the commit's first seven hex digits.

**A nightly is unsupported.** It is for trying what is coming, not for work you need to keep:
anything in it may change or break the next night. **A session saved with a nightly may not open
in a later build,** nightly or release, since parameters, presets and the saved state can still
change between them. Use a release for anything you want to reopen.

The macOS installer is signed and notarized; the Windows installer is unsigned, so SmartScreen
warns about an unknown publisher once. `SHA256SUMS.txt` holds every file's checksum.

## The corresponding source

The source of this build is the commit the `nightly` tag points at, in
https://github.com/sethbc/giga-grains:

```sh
git clone --recurse-submodules --branch nightly https://github.com/sethbc/giga-grains
```

or the source tarball attached here, `giga-grains-<version>-source.tar.gz`, which holds that tree
with every submodule at its pinned commit. The build also uses the VST3 SDK, Apple's
AudioUnitSDK, RtAudio, RtMidi and wil, which it downloads: the tags it fetches are named in
`libs/clap/clap-wrapper/cmake/base_sdks.cmake`, and `scripts/fetch-sdks.sh` fetches those same
tags. The README in the source says how to build. Giga Grains is free software, licensed under
the GNU Affero General Public License, version 3 or later.
