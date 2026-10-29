# Third-party notices

Giga Grains is licensed under the GNU Affero General Public License, version 3 or later (see
`LICENSE`). It includes third-party code under its own licenses. The license of each dependency
is recorded in `INTENT.md` §15. This file holds the notices that a distributed build must carry.
For now it covers ASIO; the other dependencies' notices come in the M1 carry-over burn-down's
final wave.

A distributed build includes this file, in the installer and in its documentation or about
box.

## ASIO (Windows standalone only)

ASIO is a registered trademark of Steinberg Media Technologies GmbH.

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

### RtAudio

RtAudio 6.0.1 (MIT-style license) is built for that backend. Its own bundled copies of Steinberg's
ASIO files carry no license grant, and they are never compiled.
