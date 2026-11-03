# The dependency watch

**The director's list (2026-11-03; INTENT §23 #90):** pins that trail their upstream but stay where
they are for now, each with what would move it and what to check when it moves. This isn't
carry-over: nothing here is a defect or a deferred fix, and a row leaves the list when its pin
moves (or when the pin stops trailing). Every pin is still exact (CLAUDE.md, "Licensing"). Moving
one is a change of its own, with that pin's checks and the evidence in its commit.

*Made for M2's close (Task 22), after the clap-wrapper bump to `next` at `4585bc6`
(2026-10-28), which left `cmake/base_sdks.cmake`, and so every SDK row below, as it was. Remake
the SDK rows after any clap-wrapper bump: its `cmake/base_sdks.cmake` holds them, and
`scripts/fetch-sdks.sh` and CI's SDK cache key follow that file.*

## Trailing, waiting on time

The watch's default is to let a release reach about two weeks' age before a pin moves to it. That
is a default, not a gate: the director pinned Inno Setup 7.1.1 (Task 22, §23 #90) at five days
old (published 2026-10-28), by choice.

| Pin | Ours → upstream | What would move it | What to check |
|---|---|---|---|
| JUCE (`libs/JUCE`) | 9.0.4 → 9.0.5 (released 2026-11-02) | Two weeks' age: about 2026-11-16, unless a fix we need lands first. | The editor and every GUI test (`ctest -L 'plugin\|ui'`), `gg-snap`'s 32 views looked at, the validators (clap-validator, pluginval at strictness 10, auval through the PM, Steinberg's validator), and the Linux packages `scripts/bootstrap-linux.sh` installs (a JUCE release can need a new one, as JUCE 9 needed `libxi-dev`). |

## Trailing, pinned by clap-wrapper

clap-wrapper's `cmake/base_sdks.cmake` pins these, and we take them from it: `scripts/fetch-sdks.sh`
fetches what it names, and a local configure downloads the same. They move only with a
clap-wrapper bump that moves them (never by overriding the wrapper's pins: Task 22 took no SDK beyond
what clap-wrapper's bump brought).

| Pin | Ours → upstream | What would move it | What to check |
|---|---|---|---|
| VST3 SDK | v3.8.0_build_66 → v3.8.1_build_84 | A clap-wrapper `next` commit that moves it. | `identity_*` (the VST3 class IDs and the preset selector), pluginval, Steinberg's validator (it builds from this SDK), the Linux leg (base_sdks.cmake drops the SDK's timer code on Linux below 3.7.9) and the SDK cache key, which changes with base_sdks.cmake. |
| AudioUnitSDK | 1.1.0 → 1.4.0 | As above. | `identity_*` (the AU's three components), `ctest -L au` (gg-au-state-tests, the known AU state faults it pins: carry-over rows 87 and 88), and auval through the PM. |
| RtMidi | 6.0.0 → 6.1.0 | As above. | The standalone apps on every leg: MIDI input in the two apps by hand, and the Windows and Linux standalone builds (CI). |
| wil | v1.0.240803.1 → v1.0.260126.7 | As above. | The Windows leg's build (clap-wrapper's Windows standalone uses it) and the standalone apps there. |

RtAudio, the fifth SDK it pins, is at 6.0.1, its newest release.

## Trailing, no release to move to

| Pin | Ours | What would move it | What to check |
|---|---|---|---|
| nlohmann/json (`libs/nlohmann_json`) | v3.12.0, its newest release (761 commits on `develop` since, unreleased) | A tagged release. | The tools that read and write JSON: `gg-render`'s specs, preset sources and metrics, and `gg-bench`'s JSON and baselines (`ctest -L 'render\|golden\|metrics\|bench'`), and every golden's WAV SHA-256 (`scripts/update-goldens.sh`, before and after). |
| simde (`libs/simde`) | v0.8.2, its newest release (420 commits since, only release candidates) | A final release past 0.8.2. | The SSE kernels on arm64, where simde maps them to NEON: `ctest -L kernel` bit for bit (natively on arm64, for the fp-contract cases), `-L determinism`, every golden WAV SHA-256 on every leg, and `gg-bench`'s ratios against the baselines (a full CI run). |

## Runner images

| Pin | Ours → upstream | What would move it | What to check |
|---|---|---|---|
| Windows leg | `windows-2022` → `windows-2025` | A decision to move, or GitHub retiring `windows-2022`. | Visual Studio 2022's clang-cl on the new image (INTENT §23 #76; the image may carry a newer Visual Studio), the Inno Setup install step, and the Windows benchmark baselines: they are per CPU model (`tools/gg-bench/baselines/windows-clangcl.json`), so a new image's models need three full-length runs each before a verdict. |

## Deliberate pins

Held on purpose, recorded so a reader doesn't take them for neglect. Each moves only by a decision.

| Pin | Why | What would move it | What to check |
|---|---|---|---|
| Linux leg `ubuntu-24.04` with gcc-13 from its archive | INTENT §15's Linux baseline (§23 #91): the products are built there and need its glibc (2.39). | A decision to move the baseline (the products would then need that image's glibc). | INTENT §15, the Linux check in `ci.yml` (it reads the ceilings from the runner itself), `installer/linux/README.md`'s stated requirement, the fresh-container job's image, and the Linux benchmark baselines (per CPU model; the remake uses runs from the new image). |
| macOS leg `macos-26` | The M1 VM's benchmark baseline (`tools/gg-bench/baselines/mac-universal.json`; M0's runs were on this image); `macos-latest` would let it float. | A decision to move, or GitHub retiring the image. | The arm64 native `gg-tests` step (the fp-contract canary), Rosetta's x86_64 slice, auval and the AU tests on the new image, and a new baseline entry from three full-length runs. |
