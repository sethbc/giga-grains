# M0 spike: MIDI into Giga Grains FX in Ableton Live

*M0 Task 12, 2026-10-26. INTENT §22 asked how Ableton Live routes MIDI into a VST3 or AU effect.
The answer sets how prominent the MIDI-driven FX features should be. Giga Grains FX is an audio
effect that also accepts MIDI (INTENT §2, §18).*

## Outcome

**MIDI reaches Giga Grains FX in Live, both as a VST3 and as an AU.** A MIDI track routed to the
FX's track raises the editor's note-on counter in both formats.

**Director decision: MIDI-driven FX features are first-class** (INTENT §23 #25), placed on the
roadmap by a second decision (§23 #32). See "Decision" below.

## What was tested

**Setup:**

- Ableton Live 12 Suite 12.4.7 on macOS 27.0.1 (Apple M5 Max);
- the `release` build at `c7e3a1a`, the same build as the host matrix
  (`docs/spikes/m0-host-matrix.md`).

**What the plugin offers:**

- The FX declares one note input port. It accepts the CLAP, MIDI and MIDI-MPE dialects and
  prefers CLAP (`GigaGrainsPlugin::notePortsInfo`).
- clap-wrapper turns that port into a VST3 event input. For AU, the FX registers both as
  `aufx`/`GGfx`/`Sbcx` and as `aumf`/`GGfx`/`Sbcx`.
- The editor shows "MIDI note-ons: N". It counts CLAP note-ons and MIDI note-ons (`0x9n` with a
  velocity above 0) that reach `process()`. The `[plugin]` test "note-ons are counted from clap
  and midi" covers that count.

**Routing steps in Live:**

1. Put Giga Grains FX on an audio track.
2. On a MIDI track, set **MIDI To** to that audio track.
3. In the chooser below it, pick **Giga Grains FX**.
4. Play notes on the MIDI track, and watch the FX editor's note-on counter.

Do this once with the VST3, then again with the AU.

## Results

| Format | Note-on counter rises |
|---|---|
| VST3 | yes |
| AU | yes, with at least one of the FX's AU identities |

The director didn't record which AU identity Live used, `aufx` or `aumf`, so the result counts
for at least one of them. Both identities pass `auval`. On `aufx`, auval warns "AU implements
MusicDeviceMIDIEvent but is of type 'aufx'". That warning is expected, because the FX takes MIDI
on its `aufx` identity too (M0 Task 8; `docs/spikes/m0-juce9.md`, "Tests and validators").

## Decision

**MIDI-driven FX features are first-class** (director, 2026-10-26).

- **Headline features:** Voice mode, MIDI pitch-sync and note-triggered freeze/capture are
  headline features of the FX. They belong in the main UI and in the factory presets.
- **Documentation:** the FX's presets and docs explain how to route MIDI to it, host by host.
  For Live, that's the steps above.

**On the roadmap** (director, a second decision; INTENT §20, §23 #32):

- **Note-triggered freeze/capture** is added to M1 (Cloud core), which already builds freeze.
- **MIDI-driven FX factory presets** are added to M2 (Giga), with the factory presets.
- **Voice mode and pitch-sync** stay in M3 (Playable), with the Voice scheduler and pitch-sync
  grains.
