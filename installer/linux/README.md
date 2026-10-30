# Giga Grains

Giga Grains is a granular synthesizer, and Giga Grains FX a granular effect. Both come as a CLAP
and a VST3 plugin, and each has a standalone app.

## What this folder holds, and where each file goes

There is no install script: copy each file where you want it.

| File | Copy it to |
|---|---|
| `Giga Grains.clap` | `~/.clap` for your user alone, or `/usr/lib/clap` for every user |
| `Giga Grains.vst3` (a folder) | `~/.vst3` for your user alone, or `/usr/lib/vst3` for every user |
| `Giga Grains`, `Giga Grains FX` | anywhere you like: they're the standalone apps |

For example, for your user alone:

```sh
mkdir -p ~/.clap ~/.vst3
cp "Giga Grains.clap" ~/.clap/
cp -R "Giga Grains.vst3" ~/.vst3/
```

The CLAP and the VST3 each hold both plugins, Giga Grains and Giga Grains FX.

## What it runs on

The plugins and the apps are built on Ubuntu 22.04 (glibc 2.35), for x86_64 only. A
distribution with an older glibc may not run them. They carry their own libstdc++, so the
system's libstdc++6 version doesn't matter; its glibc does.

## The project

https://github.com/sethbc/giga-grains

## Licenses and notices

Giga Grains is free software, licensed under the GNU Affero General Public License, version 3 or
later (AGPL-3.0-or-later). The `licenses` folder holds that license (`LICENSE`),
`THIRD_PARTY_NOTICES.md` (the notices of everything Giga Grains is built with) and the license
texts those notices name.

## The corresponding source

Each release is a tag in the repository above, named v<version> (v0.2.0, say). To get the source
of this release:

```sh
git clone --recurse-submodules --branch v<version> https://github.com/sethbc/giga-grains
```

or download the source tarball attached to the release on the project's Releases page,
giga-grains-<version>-source.tar.gz, which holds the tagged tree with every submodule at its
pinned commit. The build also uses the VST3 SDK, RtAudio and RtMidi, which the build downloads:
the tags it fetches are named in libs/clap/clap-wrapper/cmake/base_sdks.cmake, and
scripts/fetch-sdks.sh fetches those same tags. The README in the source says how to build.
