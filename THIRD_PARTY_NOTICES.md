# Third-party notices

Giga Grains and Giga Grains FX are licensed under the GNU Affero General Public License, version 3
or later (see `LICENSE`). They include third-party code under the licenses below. INTENT §15
records why each dependency was chosen and how it is pinned; this file holds the notices that a
distributed build must carry: for each dependency that ships in a product, its license, its
copyright line, the license text or a precise pointer to it, and the products that contain it.

A distributed build includes this file, in the installer and in its documentation or about box,
together with `LICENSE` (the AGPL), the GPLv3 text, CMakeRC's and flowbite-icons' MIT texts, the
AudioUnitSDK's `LICENSE.txt` in a macOS build and the ASIO SDK's `LICENSE.txt` in a Windows build.
Every installer carries them in its `licenses/` directory, which `gg-products` assembles
(`cmake/products.cmake`) from the pinned checkouts and, for the two MIT texts no checkout holds,
from `installer/licenses/`. The editor's about box doesn't show them yet (carry-over row 221).

"Both products" means Giga Grains (the instrument) and Giga Grains FX (the effect), in every
format they ship in: CLAP, VST3, AU v2 and standalone, on macOS, Windows and Linux, unless a
section says otherwise. A pointer such as `libs/<name>/LICENSE` is to the dependency's checkout
at the commit this repository pins, which is the exact text the build used.

Not shipped, so not listed as notices: Catch2, nlohmann/json, CLI11, pffft and stb (the test
suite and the `gg-render`, `gg-bench` and metrics tools use them; no plugin or standalone links
them). Their licenses are recorded in INTENT §15 and sit in `libs/`.

## The framework and the GPL libraries

### JUCE (editor, both products)

JUCE 9 (`libs/JUCE`), the GUI modules only, used under the **AGPLv3** option of its dual
license; its commercial licence is not used. Copyright (c) Raw Material Software Limited.
License text: `libs/JUCE/LICENSE.md` (which points to the AGPLv3) and the AGPLv3 in `LICENSE`.
Both products' source, which the AGPL requires us to offer, includes JUCE as a pinned submodule.

### sst-basic-blocks, sst-filters, sst-effects (both products)

GPL-3.0-or-later (`libs/sst/sst-basic-blocks/LICENSE.md`, `libs/sst/sst-filters/LICENSE.md`,
`libs/sst/sst-effects/LICENSE.md`, each the GPLv3 text). Copyright 2018-2026, various authors, as
described in the GitHub transaction log of surge-synthesizer/sst-basic-blocks, sst-filters and
sst-effects. The grain engine uses SIMD wrappers, sinc tables and envelopes from sst-basic-blocks;
Space's diffuser and reverb use sst-filters and sst-effects. GPLv3 section 13 lets this code be
combined with AGPLv3 code; the combined work is AGPLv3-or-later. (sst-basic-blocks also carries
Signalsmith Audio's MIT-licensed `elliptic-blep` under `libs/elliptic-blep`, with its own
`LICENSE.txt`: Copyright 2024 Signalsmith Audio Ltd. / Geraint Luff. No first-party code includes
it, so it is not compiled into a product.)

## MIT-licensed libraries

All of the following are under the MIT license, whose text is below ("The MIT license"). Each
keeps its own copyright line.

| Library | Copyright | Where its license is | Products |
|---|---|---|---|
| clap | Copyright (c) 2021 Alexandre BIQUE | `libs/clap/clap/LICENSE` | Both products, all formats |
| clap-helpers | Copyright (c) 2021 Alexandre BIQUE | `libs/clap/clap-helpers/LICENSE` | Both products, all formats |
| clap-wrapper | Copyright (c) 2022 defiantnerd | `libs/clap/clap-wrapper/LICENSE` | VST3, AU v2 and standalone (it wraps the CLAP plugin); its CLAP extension headers are in the CLAP builds too |
| VST3 SDK 3.8.0 (`v3.8.0_build_66`, fetched by clap-wrapper) | Copyright (c) 2025, Steinberg Media Technologies GmbH | `LICENSE.txt` in the SDK checkout (the MIT text below) | VST3 |
| sst-clap-helpers (`clap_juce_shim`) | Copyright 2019-2020, Paul Walker | `libs/sst/sst-clap-helpers/LICENSE.md` | Both products (the editor) |
| sst-jucegui (controls and theme) | Copyright 2019-2021, Authors as indicated in the Git Transaction Log | `libs/sst/sst-jucegui/LICENSE.md` | Both products (the editor) |
| md4c 0.5.2 (vendored in sst-jucegui) | Copyright © 2016-2024 Martin Mitáš | `libs/sst/sst-jucegui/src/sst/jucegui/markdown/md4c/LICENSE.md` | Both products (the editor) |
| CMakeRC 2.0.0 (vendored in sst-jucegui as `cmake/CmakeRC.cmake`; its generated resource code is compiled into the editor) | Copyright (c) 2017 vector-of-bool <vectorofbool@gmail.com> | Not in the checkout: upstream's `LICENSE.txt`, https://github.com/vector-of-bool/cmrc (the MIT text below) | Both products (the editor) |
| flowbite-icons (some of sst-jucegui's `res/glyphs`, as its `README.md` states; upstream commit 55af8e4, "Add a couple of glyphs from flowbite (#134)", added `folder.svg` and `file-music.svg`; embedded in the editor by `sst-jucegui-resources`) | Copyright (c) 2024 Themesberg | Not in the checkout: upstream's `LICENSE`, https://github.com/themesberg/flowbite-icons (the MIT text below) | Both products (the editor) |
| sst-cpputils | Copyright 2019-2021, Authors as indicated in the Git Transaction Log | `libs/sst/sst-cpputils/LICENSE.md` | Both products |
| sst-plugininfra (patch state; all but tinyxml) | Copyright 2019-2020, Paul Walker | `libs/sst/sst-plugininfra/LICENSE_MIT.md` | Both products |
| simde | Copyright (c) 2017 Evan Nemerson | `libs/simde/COPYING` | Both products (SSE intrinsics on ARM) |
| r8brain-free-src 7.6 | Copyright (c) 2013-2026 Aleksey Vaneev | `libs/r8brain/LICENSE` | Both products (sample-rate conversion; header-only) |
| utf8proc 2.12.0 | See its own section below | `libs/utf8proc/LICENSE.md` | Both products (`gg-media`'s search for a missing sample) |
| wil (v1.0.240803.1, fetched by clap-wrapper) | Copyright (c) Microsoft Corporation. All rights reserved. | `LICENSE` in the wil checkout | Windows standalone |

The same resource library (`sst-jucegui-resources`) also embeds `surge-logo.svg` and
`shortcircuit-logo.svg`, other projects' marks; neither product ever displays them.

### The MIT license

```
Permission is hereby granted, free of charge, to any person obtaining a copy of this software
and associated documentation files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### r8brain-free-src: bundled FFT

r8brain builds its resampler on Takuya Ooura's FFT (`fft/fft4g.h`): Copyright (C) 1996-2001
Takuya OOURA, http://www.kurims.kyoto-u.ac.jp/~ooura/fft.html, used and modified (a one-time
table initialization) with the permission that package grants for any purpose without fee; its
terms are on that page. Its PFFFT code (`fft/pffft*.c`) is not compiled: none of `R8B_PFFFT`,
`R8B_PFFFT_DOUBLE` or `R8B_IPP` is defined (`libs/CMakeLists.txt`).

### fmt (both products)

fmt, Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors, MIT license (above),
text in `libs/fmt/LICENSE`. Its optional exception applies to our builds: portions embedded in
machine-executable object form may be redistributed without the copyright and permission
notices. We carry them anyway.

## utf8proc (both products)

utf8proc 2.12.0, `libs/utf8proc/LICENSE.md`: the MIT license (above) for the code, with this
copyright, and the Unicode data permission notice below for the data in `utf8proc_data.c`.

```
Copyright (c) 2014-2021 by Steven G. Johnson, Jiahao Chen, Tony Kelman, Jonas Fonseca, and
other contributors listed in the git history.
Copyright (c) 2009, 2013 Public Software Group e. V., Berlin, Germany
```

```
COPYRIGHT AND PERMISSION NOTICE

Copyright (c) 1991-2007 Unicode, Inc. All rights reserved. Distributed under the Terms of Use
in http://www.unicode.org/copyright.html.

Permission is hereby granted, free of charge, to any person obtaining a copy of the Unicode
data files and any associated documentation (the "Data Files") or Unicode software and any
associated documentation (the "Software") to deal in the Data Files or Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, and/or sell copies of the Data Files or Software, and to permit persons to whom the
Data Files or Software are furnished to do so, provided that (a) the above copyright notice(s)
and this permission notice appear with all copies of the Data Files or Software, (b) both the
above copyright notice(s) and this permission notice appear in associated documentation, and (c)
there is clear notice in each modified Data File or in the Software as well as in the
documentation associated with the Data File(s) or Software that the data or software has been
modified.

THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR HOLDERS INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR
CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
CONNECTION WITH THE USE OR PERFORMANCE OF THE DATA FILES OR SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall not be used in
advertising or otherwise to promote the sale, use or other dealings in these Data Files or
Software without prior written authorization of the copyright holder.

Unicode and the Unicode logo are trademarks of Unicode, Inc., and may be registered in some
jurisdictions. All other trademarks and registered trademarks mentioned herein are the property
of their respective owners.
```

## dr_libs: dr_wav, dr_flac, dr_mp3 (both products)

Copyright 2020 David Reid (MIT No Attribution alternative); the author dedicates the code to the
public domain under the other alternative, the Unlicense. We use it under either: neither
requires a notice. The text of both is in `libs/dr_libs/LICENSE`. Used by `gg-media` to decode
WAV, AIFF, FLAC and MP3 samples.

## tinyxml (both products)

TinyXML 1.0, original code by Lee Thomason (www.grinninglizard.com), in a modified copy bundled in
sst-plugininfra (`libs/sst/sst-plugininfra/libs/tinyxml`); the patch state's XML, private to
`gg-core`. zlib license; its notice, which every source file carries and which must not be
removed or altered:

```
This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must
not claim that you wrote the original software. If you use this
software in a product, an acknowledgment in the product documentation
would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
```

## AudioUnitSDK (AU v2 only)

AudioUnitSDK-1.1.0 (fetched by clap-wrapper), Copyright (C) 2021 Apple Inc. All rights reserved.
Apache License 2.0: the full text is `LICENSE.txt` in the SDK checkout and at
https://www.apache.org/licenses/LICENSE-2.0. The SDK has no NOTICE file. A distributed AU
carries that license text.

## RtAudio and RtMidi

Copyright (c) 2001-2023 Gary P. Scavone (RtAudio 6.0.1) and Copyright (c) 2003-2023 Gary P.
Scavone (RtMidi 6.0.0), both fetched by clap-wrapper. RtAudio is built into the **Windows
standalone** (for the ASIO backend, below) and the **Linux standalone**. RtMidi is compiled only
with `CLAP_WRAPPER_STANDALONE_RTAUDIO`, which stays off, so no product contains it today; the
notice is here so that switching it on needs no new text. The license (the `LICENSE` file of each
checkout) is the MIT license with this paragraph added:

```
Any person wishing to distribute modifications to the Software is
asked to send the modifications to the original developer so that
they can be incorporated into the canonical version.  This is,
however, not a binding provision of this license.
```

## ASIO® SDK (Windows standalone only)

ASIO® is a registered trademark of Steinberg Media Technologies GmbH.

The Windows standalone apps (Giga Grains and Giga Grains FX) support ASIO® drivers through
RtAudio's ASIO backend. That backend is built from the Steinberg ASIO SDK 2.3.4, which is
bundled in sst-plugininfra at `libs/sst/sst-plugininfra/libs/asiosdk`. These SDK files are
compiled, unmodified: `common/asio.cpp`, `host/asiodrivers.cpp` and `host/pc/asiolist.cpp`. They
include `common/asio.h`, `common/asiosys.h`, `common/iasiodrv.h`, `host/asiodrivers.h`,
`host/ginclude.h` and `host/pc/asiolist.h`. The CLAP, VST3 and AU plugins and the macOS and Linux
builds contain no ASIO code.

The ASIO logo is not used. The word "ASIO" in the standalone's audio settings names the driver
type.

### `common/` files: the SDK's dual license, GPLv3 elected

The SDK's `LICENSE.txt` offers "the Steinberg ASIO License, or alternatively ... the General
Public License (GPL) Version 3". **Giga Grains uses these files under the GNU General Public
License, version 3.** A distributed Windows build carries:
- the SDK's `LICENSE.txt`;
- the full GPLv3 text (https://www.gnu.org/licenses/gpl-3.0.html);
- the offer of the complete corresponding source, which the AGPL already requires.

### `host/` files: BSD-style license

`host/asiodrivers.cpp`, `host/asiodrivers.h`, `host/ginclude.h`, `host/pc/asiolist.cpp` and
`host/pc/asiolist.h` carry this notice:

```
(c) 2025, Steinberg Media Technologies GmbH, All Rights Reserved

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
  * Neither the name of the Steinberg Media Technologies nor the names of its
    contributors may be used to endorse or promote products derived from this
    software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE  OF THIS SOFTWARE, EVEN IF ADVISED
OF THE POSSIBILITY OF SUCH DAMAGE.
```

### RtAudio's own files

RtAudio 6.0.1 (its license is in the RtAudio section above) is built for that backend. Its own bundled copies of Steinberg's
ASIO files carry no license grant, and they are never compiled.
