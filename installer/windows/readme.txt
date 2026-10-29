Giga Grains
===========

Giga Grains is a granular synthesizer, and Giga Grains FX a granular effect. Both come as a CLAP
and a VST3 plugin, and each has a standalone app.

What this folder holds, and where each file goes
------------------------------------------------

The installer, giga-grains-<version>-windows-x64.exe, puts every file below in its place. To
install from this zip instead, copy each file where you want it:

  Giga Grains.clap                    C:\Program Files\Common Files\CLAP\
  Giga Grains.vst3 (a folder)         C:\Program Files\Common Files\VST3\
  Giga Grains.exe, Giga Grains FX.exe anywhere you like: they're the standalone apps

Copying into C:\Program Files needs an administrator's permission. The CLAP and the VST3 each
hold both plugins, Giga Grains and Giga Grains FX.

What it runs on
---------------

Windows 10 or later, on x64 only.

The project
-----------

https://github.com/sethbc/giga-grains

Licenses and notices
--------------------

Giga Grains is free software, licensed under the GNU Affero General Public License, version 3 or
later (AGPL-3.0-or-later); the installer shows it before installing. The licenses folder holds
that license (LICENSE), THIRD_PARTY_NOTICES.md (the notices of everything Giga Grains is built
with) and the license texts those notices name, among them the ASIO SDK's, which the standalone
apps are built with (ASIO-SDK-LICENSE.txt). The installer puts the same folder in
C:\Program Files\Giga Grains\licenses\. ASIO is a registered trademark of Steinberg Media
Technologies GmbH.

The corresponding source
------------------------

Each release is a tag in the repository above, named v<version> (v0.2.0, say). To get the source
of this release:

  git clone --recurse-submodules --branch v<version> https://github.com/sethbc/giga-grains

or download the source tarball attached to the release on the project's Releases page,
giga-grains-<version>-source.tar.gz, which holds the tagged tree with every submodule at its
pinned commit. The build also uses the VST3 SDK, RtAudio, RtMidi and wil, which the build
downloads: the tags it fetches are named in libs/clap/clap-wrapper/cmake/base_sdks.cmake, and
scripts/fetch-sdks.sh fetches those same tags. The README in the source says how to build.
