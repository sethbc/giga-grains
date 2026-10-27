# M0 host matrix: Ableton Live 12 and Bitwig Studio

*M0 Task 12, 2026-10-26. Checks the second half of the M0 exit criterion (INTENT §20): the empty
plugins load in the first-class hosts (INTENT §11). The director ran the checks by hand. The
other half, passing every validator, is checked by CI on every build.*

## Outcome

**Pass, after one defect was fixed.** Both plugins load and work in every format each host offers.
Director: "the matrix all works now".

The defect: Ableton Live didn't list the VST3 at all. Each bundle carried only the signature the
linker put on its executable, and Live's VST3 scanner rejects that. Re-signing the bundle fixed
it, and commit `05731d1` now signs every macOS bundle at build time. See "Defect: Ableton didn't
list the VST3" below.

## Hosts and build

- **Ableton Live 12 Suite 12.4.7.** The build is `2026-10-16_cf213561fb`, from the app's
  `Info.plist` and from the scanner log's header (`Started: PluginScanner 12.4.7 Build:
  2026-10-16_cf213561fb`).
- **Bitwig Studio 6.1.5**, from `CFBundleShortVersionString` in
  `/Applications/Bitwig Studio.app/Contents/Info.plist`.
- **Machine:** macOS 27.0.1 on an Apple M5 Max.
- **Build:**
  - the `release` preset at `c7e3a1a`, with JUCE 9.0.4, arm64 only;
  - configured with `GG_COPY_PLUGIN_AFTER_BUILD=ON`, so the build installed the CLAP, VST3 and AU
    into `~/Library/Audio/Plug-Ins/`;
  - the installed CLAP, VST3 and AU binaries hash-matched `build/release/gg-products`.
  - `c7e3a1a` predates the signing fix. The installed VST3 was re-signed by hand (see the defect
    below), and the matrix then ran against it.

## Procedure

For each host and each format it offers, for both **Giga Grains** and **Giga Grains FX**:

1. Load the plugin in a project.
2. Open and close its editor three times.
3. Save the project, close it and reopen it.

The formats are VST3 and AU in Ableton Live, and CLAP and VST3 in Bitwig Studio.

## Results

A pass means the plugin loaded, its editor opened and closed three times, and the project saved,
closed and reopened.

| Host | Format | Giga Grains | Giga Grains FX |
|---|---|---|---|
| Ableton Live 12.4.7 | VST3 | pass, after the signing fix | pass, after the signing fix |
| Ableton Live 12.4.7 | AU | pass | pass |
| Bitwig Studio 6.1.5 | CLAP | pass | pass |
| Bitwig Studio 6.1.5 | VST3 | pass | pass |

The FX registers two AU identities, `aufx` and `aumf` (INTENT §18). Which one Live used wasn't
recorded. MIDI into the FX in Live is covered in `docs/spikes/m0-ableton-midi-fx.md`.

## Defect: Ableton didn't list the VST3

**Symptom.** Live listed neither plugin as a VST3. It listed the AU. Bitwig loaded the same VST3
without complaint.

**Evidence.** Live's scanner log, `~/Library/Preferences/Ableton/Live 12.4.7/PluginScanner.txt`,
shows the same failure on both scans, at 22:09:30 and at 22:10:25 local time:

```
2026-10-26T22:09:30.507185: info: VST3: check plugin at path: "/Users/sethbc/Library/Audio/Plug-Ins/VST3/Giga Grains.vst3"
2026-10-26T22:09:30.530368: error: Failed to load plugin: code has no resources but signature indicates they must be present
2026-10-26T22:09:30.530389: info: VST3: not a plugin
```

**Root cause.**

- The only signature on each bundle was the linker's ad-hoc signature on its executable.
  `codesign -dvvv` on the VST3 showed:
  - `flags=0x20002(adhoc,linker-signed)`;
  - `Info.plist=not bound`;
  - `Sealed Resources=none`.
- No step signed the bundles as a whole. clap-wrapper's one `codesign` call signs its AUv2
  build helper, a build-time tool, not a product.
- A bundle in that state fails `codesign --verify --deep --strict` with exactly the message Live
  printed.
- The AU and CLAP bundles had the same linker-only signature. No host in this matrix rejected them,
  but strict verification fails them too, so the fix covers every bundle.

**End-to-end check.**

- The controller ran `codesign --force --sign -` on the installed `Giga Grains.vst3`.
- On the next scans, Live found both plugins: at 22:17:05 and 22:17:21, and again at 22:18:25.
  From the 22:17:05 scan:

  ```
  2026-10-26T22:17:05.696661: info: VST3: found: Giga Grains
     device-class-id: device:vst3:instr:56e869bf-edd7-5019-bd0c-3105fa80b835?n=Giga%20Grains
  2026-10-26T22:17:05.696972: info: VST3: found: Giga Grains FX
     device-class-id: device:vst3:audiofx:dbaf5c12-ea09-5fef-b3a5-cbbbb254442a?n=Giga%20Grains%20FX
  ```

- The full matrix then passed.

**Permanent fix:** commit `05731d1`, "plugin: ad-hoc sign macOS bundles so strict hosts load them"
(ruling T12-a).

- **Signing:** every macOS product bundle is signed as a whole once it's built. That covers
  `Giga Grains.clap`, `.vst3` and `.component`, and both standalone apps.
  - The command is `codesign --force --sign "$GG_MACOS_CODESIGN_IDENTITY"`. The identity defaults
    to `-`, which signs ad hoc.
- **Order:** the signing runs before clap-wrapper's copy-after-build, so with
  `GG_COPY_PLUGIN_AFTER_BUILD=ON` the installed copy is the signed one.
- **Tests:** five CTest entries, `codesign_{clap,vst3,auv2,standalone,standalone_fx}` (label
  `plugin`), run `codesign --verify --deep --strict` on each bundle in the build tree.
  - Before the fix, all five failed with Live's message.
  - The `dev` suite is now 84 tests.
- **AU `Info.plist`:** the fix also cures a second, older defect. Every CMake reconfigure replaced
  the AU's `Info.plist` with a generic one that has no `AudioComponents`. The real one came back
  only when the AU relinked. Each bundle now relinks, and is signed again, whenever its
  `Info.plist` can have changed.

**Not yet repeated in the hosts.** The matrix ran on the `c7e3a1a` build with its VST3 signed by
hand. `05731d1` runs the same command automatically, and the `codesign_*` tests check the result
on every macOS build. Signing for distribution is release work, not just a change of identity: a
Developer ID plus the hardened runtime, a timestamp and notarization.

## Reproduce

```sh
cmake --preset release -DGG_COPY_PLUGIN_AFTER_BUILD=ON
cmake --build --preset release --target gg-products     # installs into ~/Library/Audio/Plug-Ins/
codesign --verify --deep --strict --verbose=2 ~/Library/Audio/Plug-Ins/VST3/"Giga Grains.vst3"
```

Then rescan plug-ins in Live and Bitwig, and repeat the procedure above. Live's scanner log is
`~/Library/Preferences/Ableton/Live <version>/PluginScanner.txt`.
