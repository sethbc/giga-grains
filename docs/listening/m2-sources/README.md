# Listening pack: M2 sub-project 2 (Sources and persistence)

This pack is rendered for the record now and heard at M2's close (sub-project 7; the programme
design's decision 1). It has two parts:

- renders from `gg-render`, to hear offline, one per item of the spec's table (§11);
- a host checklist for Ableton Live 12 and Bitwig Studio, for the close's sitting.

The spec is `docs/superpowers/specs/2026-10-30-m2-sources-design.md`: the ring's window, Loop,
Overdub and splices are its §6.1 (the splice numbering is decision D6, Overdub D5), the Resample
source §6.2 (its tap, D4), the five ring parameters §6.3, capture §6.4 and the session format
§6.6.

## Rendering the files

The renders aren't committed. The specs are in [`specs/`](specs/). To make the files, from the
repository root:

```sh
cmake --preset release && cmake --build --preset release --target gg-render
r=build/release/tools/gg-render/gg-render
L=build/release/listening/m2-sources
$r --write-media "$L/media"   # the test media and the two state fixtures, frozen-ring.ggst among them
for s in docs/listening/m2-sources/specs/*.json; do
  n=$(basename "$s" .json)
  extra=""
  [ "$n" = 08a-ring-restore-a ] && extra="--save-state $L/media/08-saved.ggst"
  $r "$s" --media-dir "$L/media" --captures-dir "$L/captures" -o "$L/$n.wav" \
     --spectrogram "$L/$n.png" --metrics "$L/$n.metrics.json" --events "$L/$n.events.jsonl" $extra
done
shasum -a 256 "$L"/08*.wav        # the two must be identical
```

`08a` runs before `08b` (the glob's order) and leaves the state `08b` restores. Each file is a
48 kHz, 32-bit float WAV (mono input rings, stereo output), with a spectrogram PNG, the audio
metrics and the grain-event log beside it. Every render is deterministic, so on one platform the
same commit gives the same bytes. `03` is 130 s long and takes the longest to render.

## The files

The FX files run at Mix 100%. Most read a sweep at amplitude 0.5, 110 Hz to 1760 Hz, as long as
the input they name, so a grain from further back sits lower on the spectrogram. The peaks and RMS
levels below were measured on the `release` renders at `489d85e` (Task 13), over both channels and the whole file. No
file peaks above -0.7 dBFS.

| File | Length | What it is | Listen for |
|---|---|---|---|
| `01-resample-feedback.wav` | 13.0 s | The instrument over a 220 Hz sine (Position 0.25, Level -6 dB), one note held 0.25-12.25 s. Layer B is a Resample layer at Pitch +12; its Feedback rises 0 to 0.8 in 1/15 steps every second. Peak -12.3 dBFS. RMS -25.1 dBFS. | The instrument eating itself: layer B re-grains the output an octave up, and each step of Feedback adds a further octave to the cascade. It rises and thickens but stays bounded: the peak is the file's own. |
| `02-fx-resample-octave.wav` | 10.0 s | The FX over a stereo sweep (to 9.5 s), Reverb Amount 30% at Decay 2 s. Layer B is Resample at Pitch -12 (Position 0.5 s), then -24 from 5.0 s. Peak -7.6 dBFS. RMS -18.9 dBFS. | The output re-grained down: a second line an octave below the sweep, two octaves below from 5 s, and the reverb's tail recursing through it (D4: the tap is the output the host hears, reverb included). |
| `03-long-ring-scan.wav` | 130.0 s | The FX with a 120 s ring over a 110-1760 Hz sweep that runs 90 s. From 90 s Position is swept from 0 to 80 s behind over 40 s, in 0.5 s steps (Density 8 Hz, Size 125 ms). Peak -4.6 dBFS. RMS -13.3 dBFS. A sweep stands in for the spec's "sparse loop": `gg-render` has no built-in rhythmic input longer than 4 s, and a sweep's pitch tells where in the ring each grain comes from. | Scanning back over a minute and more of history: the pitch falls from the sweep's top toward its bottom as Position goes from the present to 80 s behind with no seam or click, well past the 16 s M1's ring held. |
| `04-loop-overdub.wav` | 24.0 s | The FX with Loop on, Loop Length 2 s, over a 24 s sweep (Position 0.3, Density 8 Hz). Overdub 0, then 0.5 at 6 s, 0.8 at 12 s and 1.0 at 18 s. Peak -0.7 dBFS. RMS -11.0 dBFS. A sweep stands in for the spec's "rhythmic input" (see `03`). | Sound-on-sound building: at Overdub 0 the loop is just the last 2 s of input; at 0.5 and 0.8 every pass of the input stacks on its predecessors, each pass decaying by the Overdub gain, so the lower the setting the faster the older passes fade; at 1.0 nothing fades and the texture only thickens (D5). |
| `05-frozen-loop.wav` | 12.0 s | The FX with Loop on, Loop Length 1 s, over a 4 s sweep; Freeze at 4 s. From 6 s Spray steps from 10% to 80% of its range in eight steps of 0.75 s. Peak -3.0 dBFS. RMS -12.3 dBFS. | The held loop's seam: every grain is admitted at least 256 frames inside the loop's two ends and stays there, so none reads across the seam and nothing clicks where its end meets its start. Spray then smears the grains across the loop without leaving it. |
| `06-splices.wav` | 20.0 s | The FX with an 8 s ring over an 8 s sweep, frozen at 8 s and cut into four splices (marks 7, 5, 3 and 1 s behind the head, so spans of 2, 2, 2 and 1 s). Splice 1 at 9 s, 2 at 11 s, 3 at 13 s, 4 at 15 s, then 0 (the whole ring) at 17 s. Position 0.2 s behind each span's end, Density 8 Hz, Size 50 ms. Peak -4.6 dBFS. RMS -17.3 dBFS. | Each span alone, a different part of the sweep each time, then the whole; no click at a change (D6), though grains are sounding at the changes: each grain finishes in the span it started in (ruling SP-T12aa). |
| `07-capture-to-layer.wav` | 20.0 s | `06` with splice 2 captured into layer B at 8.2 s (B Source Sample, Pitch +7, Position 0.2 s) while layer A steps through the splices as in `06`. Peak -4.5 dBFS. RMS -19.6 dBFS. | The capture plays as a sample under the ring: the second span of the sweep, a fifth up, beside whatever span A is on. |
| `08a-ring-restore-a.wav`, `08b-ring-restore-b.wav` | 6.0 s each | `a`: `frozen-ring.ggst` (a generated 20 s sweep ring, frozen, with two splices) restored with Freeze on and scanned for 6 s (Position 0.5 stepped to 1.1 each second); the state at the end of the render goes to `08-saved.ggst`. (A deviation from spec §11, which saves it at 0 s of the scan; harmless, because the ring is frozen.) `b`: that saved state restored and the same 6 s rendered. Peak -4.0 dBFS. RMS -13.8 dBFS. SHA-256 of both: `c60c0a190f28f73c0eb264551b1c97dc94646fad7be78ef051ecb81376848e2e`. | Identical files: the reopened pad is the saved one, from its first sample. The check is the hash, not the ear. |
| `09-resize-live.wav` | 20.0 s | The FX with a 60 s ring over a 19.5 s sweep, scanning 3 s behind. Peak -4.3 dBFS. RMS -13.9 dBFS. `gg-render` sets a ring's length before the render and can't resize one mid-render, so this file is the 60 s ring's steady state, not the resize. | A steady scan from a ring that holds the whole file. The resize itself (16 s to 60 s to 8 s while scanning) is heard only in a host: see the checklist. |
| `10-fx-sample-layer.wav` | 8.0 s | The FX over a stereo sweep, layer B on with Source Sample, loaded with `loop-48k.wav` (Position 0.25, Density 16 Hz, Size 125 ms), the pad at the centre. Peak -5.9 dBFS. RMS -19.5 dBFS. | A sample layer inside the effect: the loop's bursts re-grained by layer B beside layer A's grains of the input. |

## Host checklist

Do each item in **Ableton Live 12** and in **Bitwig Studio**. Use CLAP in Bitwig, and VST3 and
AU in Live. Note the host and format of anything that fails.

### The ring

- [ ] **A frozen ring survives a save.** Save a project with a frozen ring, close it and reopen
  it: the ring is held (Freeze on), in VST3 and in AU, from the first sample.
- [ ] **A project and its captures folder move together.** Move the project and its captures
  folder to the other machine and reopen: the ring and the captures are there.
- [ ] **A ring over 32 MB goes to the captures folder.** Make a ring longer than 32 MB (about
  2 minutes of stereo at 48 kHz), save: it is written to the captures folder, not embedded, and
  reopens.
- [ ] **A long ring from the generic editor.** Set a Ring Length of 2 minutes from the generic
  editor, while playing: nothing is cut or clicks at the resize, in both directions.
- [ ] **The cap's refusal.** Raise Ring Length until the memory cap refuses it: the status says
  why (through sub-project 6's readout once it lands).

### Sources

- [ ] **A Resample layer from the instrument's Macro 7.** Set a layer's Source to Resample and
  drive its Feedback from Macro 7: the cascade of `01`, bounded.
- [ ] **The FX restores a sample on layer B.** Load a file on layer B in the FX, save, close,
  reopen: layer B's sample is restored from the saved state.
