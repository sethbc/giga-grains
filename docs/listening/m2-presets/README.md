# Listening pack: M2 sub-project 5 (Presets and content)

This pack is rendered for the record now, in the sub-project's second group, and heard at M2's
close (sub-project 7; the programme design's decision 1). It has two parts:

- renders from `gg-render`, to hear offline, one per item of the spec's table (§11): the 48
  factory presets, INIT in each product and the Mutate ladder;
- a host checklist for Ableton Live 12 and Bitwig Studio, for the close's sitting.

The spec is `docs/superpowers/specs/2026-10-30-m2-presets-design.md`: the preset file §6.1,
loading and INIT §6.2, discovery and the wrapper's bridges §6.3, the content §6.4 and §6.5, the
factory presets §6.6, the browser §6.8 and Mutate §6.9 (the law is decision D4, the locks D3).

## Rendering the files

The renders aren't committed. The specs are in [`specs/`](specs/). Each preset spec has a
`"preset"` key, `presets/<Product>/NN Name.ggpreset`, resolved through `--media-dir`; the presets
are packed from `content/presets/` by the `gg-presets` target (which fetches the pinned content
first), and the samples they name come from that content (`--content-dir`). From the repository
root:

```sh
cmake --preset release && cmake --build --preset release --target gg-render gg-presets
b=build/release
r=$b/tools/gg-render/gg-render
c=$b/content-cache/content                     # or the folder GG_CONTENT_DIR names
L=$b/listening/m2-presets
M=$L/media
rm -rf "$L" && mkdir -p "$M/presets/mutate"
$r --write-media "$M"
cp -R "$b/content/Presets/Giga Grains" "$b/content/Presets/Giga Grains FX" "$M/presets/"
cp "$c/Samples/Strings/violin-c4-vibrato.flac" "$M/"   # the FX's input and INIT's sample
# The Mutate ladder: one press each of FX 01 Soft Focus (seed 0, the default locks).
fx01="$M/presets/Giga Grains FX/01 Soft Focus.ggpreset"
$r --mutate-preset "$fx01" -o "$M/presets/mutate/01 Soft Focus 10.ggpreset" --amount 0.1 --counter 1
$r --mutate-preset "$fx01" -o "$M/presets/mutate/01 Soft Focus 30.ggpreset" --amount 0.3 --counter 2
$r --mutate-preset "$fx01" -o "$M/presets/mutate/01 Soft Focus 60.ggpreset" --amount 0.6 --counter 3
for s in docs/listening/m2-presets/specs/*.json; do
  n=$(basename "$s" .json)
  $r "$s" --media-dir "$M" --content-dir "$c" -o "$L/$n.wav" --spectrogram "$L/$n.png" \
     --metrics "$L/$n.metrics.json"
done
# The ladder's four 4 s parts, joined (ffmpeg; or hear the four parts in order).
ffmpeg -v error -y -i "$L/mutate-ladder-0-original.wav" -i "$L/mutate-ladder-1-10.wav" \
  -i "$L/mutate-ladder-2-30.wav" -i "$L/mutate-ladder-3-60.wav" \
  -filter_complex '[0:a][1:a][2:a][3:a]concat=n=4:v=0:a=1' -c:a pcm_f32le "$L/mutate-ladder.wav"
```

Each file is a 48 kHz, stereo, 32-bit float WAV, with a spectrogram PNG and the audio metrics
beside it. A spec's seed is the render's (D6). Every render is deterministic, so on one platform
the same commit gives the same bytes.

**The FX input** is one content file, `Samples/Strings/violin-c4-vibrato.flac` (VSCO 2: CE's solo
violin, arco vibrato, a held middle C, 13 s at 44.1 kHz, converted to 48 kHz), not the
"voice-and-strings phrase" the spec words: release `v2` adds orchestral strings and brass from
VSCO 2: CE (the director's decision, INTENT §23 #70) to `v1`'s files, but neither VCSL nor VSCO 2:
CE has a voice, so the `voice` tag stays unused. A file input plays one file, so the "phrase" is
one bowed note with its own onset and vibrato; the FX presets' own movement (their routes, the
`midi` notes) does the rest. The same file is INIT's material in both products: the FX's input,
and the instrument's sample under the phrase-and-chord.

## The files

The specs are the preset goldens' (`tests/golden/presets/cases/`), made 8 s long:

- **FX** (`fx-NN-<slug>`): the violin as input; the eight `midi` presets (09 to 14, 08 and 23)
  get two held notes from 3 s (key 55 at 3.0 s for 1.5 s, then key 67 at 5.0 s for 2.0 s);
  17 and 18 keep their golden's transport, 120 BPM playing.
- **Instrument** (`inst-NN-<slug>`): each golden's two-note phrase (0.25 s and 1.5 s) and its
  chord at 3.0 s, held 3 s here (to 6.0 s), so 2 s of tail; 16 and 18 keep the transport.
- **INIT** (`init-fx`, `init-inst`): no preset, so every parameter at its default and the factory
  routes (D8); the instrument plays C4 and G4, then C major, at the same times.
- **The Mutate ladder** (`mutate-ladder`, 16 s): FX 01 Soft Focus over the violin, then the same
  4 s with the preset mutated once at 10% (counter 1), 30% (counter 2) and 60% (counter 3), each
  press made from the original preset (not one after the other), seed 0, the default locks
  (`gg-render --mutate-preset`, the plugin's press, `gg/core/Mutate.h`). Each part starts its
  input and ring from silence.

| File | Length | What it is | Listen for |
|---|---|---|---|
| `fx-NN-<slug>.wav` × 24 | 8 s | Each FX preset over the violin; the eight `midi` ones with two held notes from 3 s | The preset's character; the MIDI ones freezing and repitching on the notes (D7) |
| `inst-NN-<slug>.wav` × 24 | 8 s | Each instrument preset: a two-note phrase, then a chord | Playable from note 1; the focus the tags promise |
| `init-fx.wav`, `init-inst.wav` | 8 s | INIT in each product over the same material | The plain defaults (D8) |
| `mutate-ladder.wav` | 16 s | FX 01 Soft Focus, then Mutate at 10%, 30% and 60% (seed fixed, counter 1 to 3), 4 s each | Variations that stay in the family at 10% and leave it by 60% (D3, D4) |

### Measured (the M5 Max, `build/dev` at this commit)

Peak is `peakDb`; RMS is the power mean of the 10 ms `rmsEnvelopeDb` frames (the mono mix) over
the whole file; click is `clickScore` (metricsVersion 2; the goldens' limit is 15). No file has a
non-finite or denormal sample.

| File | Peak dBFS | RMS dBFS | Click |
|---|---|---|---|
| fx-01-soft-focus | -3.87 | -19.99 | 4.12 |
| fx-02-haze-bloom | -7.62 | -22.80 | 4.51 |
| fx-03-slow-hours | -5.88 | -21.01 | 4.36 |
| fx-04-undertow | -8.48 | -22.93 | 3.78 |
| fx-05-shimmer-veil | -7.29 | -21.95 | 4.20 |
| fx-06-spectral-mist | -4.41 | -21.91 | 3.96 |
| fx-07-dust-motes | -4.06 | -20.70 | 5.24 |
| fx-08-still-water | -4.28 | -21.22 | 4.16 |
| fx-09-snapshot-keys | -4.31 | -19.06 | 4.25 |
| fx-10-harmonic-hold | -6.42 | -20.97 | 4.23 |
| fx-11-crystal-keys | -5.98 | -20.78 | 4.60 |
| fx-12-drawn-bow | -4.77 | -20.07 | 4.21 |
| fx-13-plucked-ring | -4.02 | -19.90 | 4.03 |
| fx-14-wobble-keys | -5.28 | -20.38 | 4.64 |
| fx-15-step-riff | -4.37 | -20.64 | 4.33 |
| fx-16-grain-gate | -5.03 | -23.07 | 4.18 |
| fx-17-bar-climber | -4.17 | -20.70 | 4.39 |
| fx-18-dotted-echoes | -1.78 | -18.52 | 4.35 |
| fx-19-infinite-regress | -4.54 | -20.66 | 4.15 |
| fx-20-melting-clock | -7.41 | -24.05 | 4.06 |
| fx-21-event-horizon | -2.86 | -13.69 | 6.03 |
| fx-22-shard-storm | -5.42 | -26.65 | 5.20 |
| fx-23-splinter-keys | -3.57 | -19.94 | 4.40 |
| fx-24-sinking-spiral | -8.36 | -23.65 | 4.27 |
| inst-01-bowed-horizon | -4.21 | -20.89 | 4.34 |
| inst-02-glass-orchard | -3.31 | -20.65 | 5.62 |
| inst-03-ember-field | -6.86 | -20.31 | 0.00 |
| inst-04-low-tide-brass | -2.54 | -18.64 | 6.54 |
| inst-05-frost-lattice | -5.06 | -19.79 | 4.61 |
| inst-06-reed-mist | -4.59 | -20.83 | 4.42 |
| inst-07-ocean-archive | -7.48 | -25.26 | 4.64 |
| inst-08-earth-hum | -5.99 | -21.19 | 5.19 |
| inst-09-velvet-strings | -4.23 | -20.28 | 4.43 |
| inst-10-felt-mallets | -4.30 | -20.25 | 3.38 |
| inst-11-gilded-section | -4.28 | -19.26 | 5.55 |
| inst-12-harp-lantern | -3.95 | -21.98 | 4.78 |
| inst-13-breath-organ | -6.55 | -18.36 | 5.51 |
| inst-14-low-bow-bass | -5.38 | -19.16 | 4.87 |
| inst-15-kalimba-steps | -7.10 | -20.34 | 4.59 |
| inst-16-pulse-bells | -5.52 | -24.35 | 3.71 |
| inst-17-tremolo-engine | -4.06 | -23.73 | 5.33 |
| inst-18-clockwork-brass | -4.97 | -20.81 | 4.21 |
| inst-19-gong-abyss | -4.98 | -19.32 | 0.00 |
| inst-20-shatter-glass | -5.73 | -20.16 | 4.57 |
| inst-21-smoke-cathedral | -5.62 | -19.60 | 5.12 |
| inst-22-hall-of-mirrors | -5.36 | -20.30 | 4.52 |
| inst-23-locust-cloud | -6.45 | -19.29 | 8.80 |
| inst-24-time-dilation | -4.68 | -19.93 | 4.93 |
| init-fx | -4.20 | -19.84 | 4.42 |
| init-inst | -20.34 | -38.99 | 3.68 |
| mutate-ladder-0-original | -4.75 | -20.43 | 4.09 |
| mutate-ladder-1-10 | -4.97 | -20.35 | 4.03 |
| mutate-ladder-2-30 | -6.23 | -20.80 | 3.77 |
| mutate-ladder-3-60 | -4.07 | -20.13 | 3.99 |

Notes for the sitting:

- **Most presets sit within a few dB of -20 dBFS RMS.** The outliers: `fx-21-event-horizon` is
  the loudest (-13.7: a long room fed by a sustained input; peak -2.9), `fx-18-dotted-echoes`
  peaks highest (-1.8), and `fx-22-shard-storm` (-26.7) and the pulsed `inst-07`, `inst-16` and
  `inst-17` are quietest. `init-inst` is -39 dBFS RMS (peak -20.3): INIT's Position reads the
  very start of the sample, and the violin's bow swells in there (the same spec at Position 0.5
  peaks at -4.9 dBFS). Whether INIT should start the instrument further into a sample is a
  question for the sitting (D8), not a fault of the render.
- **The FX presets' character over a held note.** With one bowed note as input, a preset whose
  Mix keeps much of the dry signal sounds close to the violin; listen for what each adds on top.
  The `midi` presets show the freeze plainly: the held note's frozen copy at the key's pitch.
- **The ladder's grain counts** (one run of the event log) are 171 at 10%, 946 at 30% and 22 at
  60%: at 60% Density has moved far enough to change the texture's grain, which is the point.

## Host checklist

For the close's sitting in Ableton Live 12 and Bitwig Studio:

- [ ] Install from the PKG and the EXE (and unpack the tarball); the browser's status line says
      "Factory content v2 in …".
- [ ] The 24 presets per product in the browser, in order, INIT first.
- [ ] The VST3 program list and its selector follow the browser, in Live and Bitwig.
- [ ] The AU factory presets list and load, in Live (and Logic, best effort).
- [ ] A `midi` FX preset on a track with a MIDI track routed to it.
- [ ] Save as…, then reopen the project: the preset's name comes back, with no edited dot until
      a value moves.
- [ ] A favorite survives a restart.
- [ ] Mutate recorded as automation (the lanes show the gestures).
- [ ] An M1 session reopens unchanged, with its name "Init".
- [ ] **The content's noise floor under granulation.** The quiet VSCO 2: CE sources were gained
      +29 to +42 dB to a -1 dBFS peak (the content's `PROVENANCE.md`), raising their recorded
      noise floor with them: `Brass/trombone-f3-sustain` +41.8 dB (instrument 04 Low Tide Brass,
      11 Gilded Section), `Strings/double-bass-e1-sustain` +35.6 dB (08 Earth Hum, 14 Low Bow
      Bass), `Brass/trumpet-d5-harmon-mute` +32.3 dB (18 Clockwork Brass),
      `Strings/viola-section-d4-tremolo` +31.4 dB (17 Tremolo Engine), `Brass/horn-ds4-muted`
      +29.7 dB (18 Clockwork Brass) and `Brass/tuba-f2-sustain` +29.7 dB (03 Ember Field). Listen
      for hiss under those presets, in the renders and in a host. v1's VCSL files have high gains
      too (`Textures/ocean-drum` +46.0 dB in FX 04 and instrument 07, `Textures/cymbal-roll`
      +38.5 dB in 23, `Mallets/marimba-c2` +35.0 dB in 10, `Plucked/harp-c3` +32.6 dB in 12,
      `Mallets/wine-glass-ds4-rubbed` +30.6 dB in 02 and 20, `Plucked/mbira-b2` +27.3 dB in 15,
      whose hiss Task 16 saw in its spectrogram even after a 3.5 kHz low-pass): hear 15 Kalimba
      Steps in particular.
