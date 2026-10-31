Giga Grains
===========

Giga Grains is a granular synthesizer, and Giga Grains FX a granular effect. Both come in every
plugin format below, and each has a standalone app.

What this installer puts where
------------------------------

Every choice is on by default; turn off any you don't want with Customize.

  Giga Grains CLAP          /Library/Audio/Plug-Ins/CLAP/Giga Grains.clap
  Giga Grains VST3          /Library/Audio/Plug-Ins/VST3/Giga Grains.vst3
  Giga Grains Audio Unit    /Library/Audio/Plug-Ins/Components/Giga Grains.component
  Giga Grains app           /Applications/Giga Grains.app
  Giga Grains FX app        /Applications/Giga Grains FX.app
  Giga Grains Content       /Library/Application Support/sethbc/Giga Grains
                            (the factory presets and the CC0 samples they use)

Each plugin bundle holds both plugins, Giga Grains and Giga Grains FX. It runs on Apple silicon
and Intel Macs, on macOS 11 or later.

Hosts
-----

Ableton Live 12 and Bitwig Studio are the first-class hosts: they're tested by hand at every
milestone, and a bug in either blocks a release.

The project
-----------

https://github.com/sethbc/giga-grains

Licenses and notices
--------------------

Giga Grains is free software, licensed under the GNU Affero General Public License, version 3 or
later (AGPL-3.0-or-later); the installer shows it before installing. The disk image's Licenses
folder holds that license, THIRD_PARTY_NOTICES.md (the notices of everything Giga Grains is built
with) and the license texts those notices name, among them the CC0 1.0 legal code of the factory
content (CC0-1.0.txt). The content's PROVENANCE.md says where each sample came from.

The corresponding source
------------------------

Each release is a tag in the repository above, named v<version> (v0.2.0, say). To get the source
of this release:

  git clone --recurse-submodules --branch v<version> https://github.com/sethbc/giga-grains

or download the source tarball attached to the release on the project's Releases page,
giga-grains-<version>-source.tar.gz, which holds the tagged tree with every submodule at its
pinned commit. The build also uses the VST3 and AudioUnit SDKs, which the build downloads: the
tags it fetches are named in libs/clap/clap-wrapper/cmake/base_sdks.cmake, and
scripts/fetch-sdks.sh fetches those same tags. The README in the source says how to build.
