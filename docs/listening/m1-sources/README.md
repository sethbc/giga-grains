# Listening pack: M1 sub-project 3 (Sources)

This pack feeds listening gate #1 (sub-project 6). It has two parts:

- A/B renders from `gg-render`, to hear offline;
- a host checklist for Ableton Live 12 and Bitwig Studio (spec §9).

The spec is `docs/superpowers/specs/2026-10-28-m1-sources-design.md`.

## Rendering the files

The renders aren't committed. The specs are in [`specs/`](specs/). To make the files, from the
repository root:

```sh
cmake --preset release && cmake --build --preset release --target gg-render
r=build/release/tools/gg-render/gg-render
L=build/release/listening
$r --write-media "$L/media"             # tone-44k1.wav and loop-48k.wav
for s in docs/listening/m1-sources/specs/*.json; do
  n=$(basename "$s" .json)
  $r "$s" --media-dir "$L/media" -o "$L/$n.wav" --spectrogram "$L/$n.png"
done
```

Each file is a 48 kHz, stereo, 32-bit float WAV, with a spectrogram PNG beside it. Every render
is deterministic, so on one platform the same commit gives the same bytes.

## The files

Every file runs at **Level −6 dB**, the FX files (`09`, `10`) included, except `01`, which uses
the default **Level 0 dB**. Compare `01` with `02`; the "Loudness at the default Level"
checklist item below explains why.

The single-note and ladder files share one sample: a 4 s sine sweep, 50 Hz to 8 kHz, at amplitude
0.5. They use Position 0.5 and Density 5.0 (32 Hz), and the note starts at 0.25 s.

| File | Length | What it is | Listen for |
|---|---|---|---|
| `01-note-60-level-0db.wav` | 3.0 s | Key 60 held for 2 s, at **Level 0 dB** (the default). | How loud it is, and whether it sounds squashed. Its peak is −3.1 dBFS and its RMS −12.9 dBFS over 0.5–2 s. |
| `02-note-60.wav` | 3.0 s | The same at **Level −6 dB**. | The same texture with headroom. Its peak is −7.7 dBFS and its RMS −18.9 dBFS. The RMS is 6.0 dB down but the peak only 4.5 dB, because the soft limiter is already shaving `01`'s peaks. |
| `03-note-48.wav` | 3.0 s | Key 48: an octave down (rate 0.5). | An exact octave below `02`; no zipper noise or grit in the low end. |
| `04-note-72.wav` | 3.0 s | Key 72: an octave up (rate 2). | An exact octave above `02`. The sweep's top (8 kHz) lands at 16 kHz, so this is the first file where aliasing could start. |
| `05-chord.wav` | 4.0 s | Keys 48, 55 and 64 held together for 3 s, over a 220 Hz sine (Position 0.25, Density 6.0, 64 Hz). | Three clear pitches, with grains spread over the held keys rather than stacked into one. The level stays put when the chord starts. |
| `06-release.wav` | 3.0 s | Noise sample, default Size (150 ms). A 0.25 s note at 0.25 s, then a 1 s note at 1.0 s. | Each release dies away smoothly within about Size of the note-off, with no click and no cut-off grain. The short note still speaks. |
| `07-file-44k1.wav` | 3.0 s | The 44.1 kHz file `tone-44k1.wav` (left: a 50 Hz–8 kHz sweep; right: a 220 Hz sine, both at −6 dBFS), converted to 48 kHz on load. Key 60 at 0.25 s, then key 72 at 1.5 s, 1 s each. | The right channel's 220 Hz is exactly 220 Hz at key 60 (not 239.5 Hz, the 48/44.1 error), and exactly 440 Hz at key 72. No conversion artefacts. |
| `08-ladder.wav` | 7.5 s | The sweep sample at keys 60, 67, 72, 79, 84, 91 and 96, one each second, 0.8 s long. | **How far up stays usable** (see the checklist). Notes far above 60 alias until M2's mipmaps. |
| `09-fx-note-freeze.wav` | 4.0 s | The FX over `loop-48k.wav` (noise bursts every 250 ms), Mix 1 (wet only), Density 6.0, Size −2.0 (250 ms). Key 60 held 1.0–2.0 s; key 67 held 2.5–3.0 s. | While a key is held the grains repeat the moment of the loop the note froze, at the key's pitch (67 is a fifth up). On release the texture follows the live loop again. **Expect a quiet file:** its peak is −15.2 dBFS and its RMS −41.9 dBFS over 0.5–2 s, about 23 dB below the instrument files, because the loop is short bursts with silence between them. |
| `10-fx-detached-then-legato.wav` | 8.0 s | The FX over a rising 110 Hz–1760 Hz sweep, Mix 1, Density 6.0. **Detached:** keys 60, 62 and 64 at 0.5, 1.5 and 2.5 s, 0.7 s each, with gaps. **Legato:** keys 60, 62 and 64 at 4.5, 5.5 and 6.5 s, overlapping. | Detached notes re-freeze the input each time, so each one holds a newer, higher part of the sweep. Legato notes keep the first freeze (4.5 s) and only repitch it. Its peak is −7.5 dBFS and its RMS −19.3 dBFS. |

## Host checklist

Do each item in **Ableton Live 12** and in **Bitwig Studio**. Use CLAP in Bitwig, and VST3 and
AU in Live. Note the host and format of anything that fails.

### Instrument (Giga Grains)

- [ ] **Load by button.** Click the loader strip's button, choose a WAV, and play notes. The
  strip shows the file's name.
- [ ] **Load by drop.** Drag a file from the Finder or Explorer onto the editor, then play
  notes. Try AIFF, FLAC and MP3 too.
- [ ] **Notes and chords.** Play single notes across the keyboard, then chords. Each pitch is
  right, and a chord doesn't lurch in level.
- [ ] **Release tail.** Release a note: the grains fade out within about Size, with no
  click. Try a short Size and a long one.
- [ ] **A clean swap.** Hold a chord, or loop a clip, and load a new sample. The old grains
  finish and the new sample takes over, with no click, dropout or stuck note.
- [ ] **Save and reopen.** Save the project, close it and reopen it. The same sample plays, and
  sounds the same from the first note: the restore loads the sample before playback starts.
- [ ] **Found in the sample folder.** Set the sample folder once with the loader strip's
  "Sample folder…" button. Close the project and move the sample file into that folder. Reopen:
  the sample is found, with no prompt.
- [ ] **Missing, then relinked.** Close, delete the file (or move it somewhere unsearched), and
  reopen. The strip shows *missing*. Relink the file through the strip; it plays.
- [ ] **Relinked to a different file.** With the strip showing *missing*, relink a different
  file. It loads as a replacement, and the strip says it is different audio than the saved
  sample (spec §6.7).
- [ ] **Undo and preset recall keep the sample.** Load a long sample, change a parameter, then
  undo it (or switch presets, or A/B compare, where the host has it). The parameter returns at
  once, with no pause and no gap in the sound (ruling R19).
- [ ] **A new sample rate pauses, then sounds the same (ruling R18).** With a long sample
  loaded, change the host's sample rate (the audio device's, or the project's). Expect a pause
  while the sample is converted at the new rate; then the sample plays at the same pitch and
  sounds as it did.
- [ ] **Loudness at the default Level.** Play a chord on a loud sample at Level 0 dB (the
  default), then at −6 dB. The golden cases needed −6 dB because 0 dB drove the soft limiter
  into saturation (compare files `01` and `02`). Does 0 dB sound squashed in a real session?
  Should the default change? **Your call.**
- [ ] **How high is usable.** Play a bright sample up the keyboard from 60. Notes far above 60
  alias until M2's mipmaps (file `08`). **Note the highest key that still sounds acceptable.**

### Effect (Giga Grains FX)

- [ ] **Route MIDI in.** In Live, put the FX on an audio track and send a MIDI track to it with
  "MIDI To". In Bitwig, use the FX's note input. Notes reach the FX.
- [ ] **Hold notes over a playing track.** While the track plays, hold a note. The grains freeze
  on the input and play it at the note's pitch.
- [ ] **Release.** Release every note. The texture follows the live input again.
- [ ] **Legato and staccato.** Play legato, then staccato (file `10`). Detached notes re-freeze
  newer input each time; legato notes keep the first freeze and only repitch it. Is the
  difference musical and clear?

### Freeze after a reset (ruling M1-ac)

`reset()` disarms Freeze. It re-arms only when Freeze is seen off: at a call's start, at a grid
boundary, or in a `setBase` (a params.flush or a state load). So an emptied ring is never frozen.

- [ ] **A project reopened with Freeze on plays live.** Save with Freeze on and reopen. The FX
  plays the live input, not a frozen (empty) ring, until Freeze is toggled.
- [ ] **Toggling engages.** After that, turn Freeze off and on. It freezes.
- [ ] **Stop and start in a VST3 host.** In Live, with the VST3, freeze, then stop and start
  transport (a suspend). Listen for what Freeze does after the restart: it plays live until
  toggled.
- [ ] **Freeze turned on while suspended.** Stop transport (or deactivate the device), turn
  Freeze on, then start again. The FX plays live until Freeze is toggled. Nothing clicks or
  goes silent.
