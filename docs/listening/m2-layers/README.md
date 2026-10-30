# Listening pack: M2 sub-project 1 (Layers and structure)

This pack is rendered for the record now and heard at M2's close (sub-project 7; the programme
design's decision 1). It has two parts:

- renders from `gg-render`, to hear offline, one per item of the spec's table (§11);
- a host checklist for Ableton Live 12 and Bitwig Studio, for the close's sitting.

The spec is `docs/superpowers/specs/2026-10-29-m2-layers-design.md`: the layers and the morph pad
are its §6.2–§6.4 (the pad's law is decision D1), the Source kind §6.4, the spreads D4 and D5,
the mod matrix and its sources §6.8, and the macros as factory routes decision 7.

## Rendering the files

The renders aren't committed. The specs are in [`specs/`](specs/). To make the files, from the
repository root:

```sh
cmake --preset release && cmake --build --preset release --target gg-render
r=build/release/tools/gg-render/gg-render
L=build/release/listening/m2-layers
$r --write-media "$L/media"             # tone-44k1.wav and loop-48k.wav
for s in docs/listening/m2-layers/specs/*.json; do
  n=$(basename "$s" .json)
  $r "$s" --media-dir "$L/media" -o "$L/$n.wav" --spectrogram "$L/$n.png" \
     --metrics "$L/$n.metrics.json" --events "$L/$n.events.jsonl"
done
```

Each file is a 48 kHz, stereo, 32-bit float WAV, with a spectrogram PNG, the audio metrics and
the grain-event log beside it (one line per grain, with its `layer`, 0 to 3 for A to D). Every
render is deterministic, so on one platform the same commit gives the same bytes.

## The files

Every layer runs at **Level −6 dB** (`Level`, and `Level B` to `Level D` where those layers
play), except `08`, whose Level is −21 dB under Env 1's +15 dB. The FX files run at Mix 100%. Most
FX files read a stereo sine sweep at amplitude 0.5, 110 Hz to 1760 Hz, as long as the file, so a
grain from further back sits lower on the spectrogram. `loop-48k.wav` (`01`, `06`, `12`) is the
generated drum-like loop: a 30 ms noise burst every 250 ms for 4 s; `tone-44k1.wav` (`06`) is a
50 Hz–8 kHz sweep on the left and a 220 Hz sine on the right. The routes in `07` to `13` are added
in slots 13 and up, on top of the factory routes (the macros' twelve, at rest).

The peaks and RMS levels below were measured on the `release` renders, over both channels and the
whole file unless a span is given. No file peaks above −5 dBFS.

**Layers sum under one pad (D1).** With the pad at its centre, the weights of the layers that are
on share 1 between them: two layers play at half amplitude each, four at a quarter. Layers that
don't correlate (different pitches, sizes or sources) therefore add to *less* than one layer
alone: about 3 dB less for two and 6 dB less for four. `01`, `05` and `14` measure it.

| File | Length | What it is | Listen for |
|---|---|---|---|
| `01-four-layers.wav` | 4.5 s | The FX over `loop-48k.wav`, layer A alone (Density 32 Hz, Size 150 ms), then from 1.5 s layers B (+7, 60 ms), C (−12, 300 ms) and D (+12, 30 ms) too, every layer at 32 Hz; the pad at the centre. Peak −12.8 dBFS. RMS −38.2 dBFS over 0.5–1.5 s (A alone), −45.5 over 2–4 s (all four). The loop is short bursts and silence, so the file is quiet. | Four textures as one instrument: a fifth, an octave down and an octave up around the loop's bursts. Nothing is louder than one layer was in M1 (D1): the four together measure about 7 dB *below* layer A alone. Is that drop acceptable, or should the pad keep power rather than amplitude? |
| `02-morph-square.wav` | 10.0 s | The instrument, four Sample layers (A a 100 Hz–4 kHz sweep, B a 330 Hz sine, C noise, D a 50 Hz–2 kHz sweep, each read at Position 0.25), one note held 0.25–9.75 s. The pad steps from the centre to layer A's corner, Morph X 0 and Y 0 (0.5–1.0 s), round the square's four edges, 2 s each in 1/32 steps every 1/16 s (1.0–9.0 s), and back to the centre. Peak −8.6 dBFS. RMS −23.1 dBFS at the centre (0.5–1.0 s), −21.0 over the square. | Smooth crossfades from one layer to the next, with no step at any corner or edge: the noise (layer C, whose corner is X 0, Y 1) fades in along the Y = 1 edge (5.0–7.0 s) and out along X = 0 (7.0–9.0 s), and is all but silent along the first two edges. Each pad move ramps over 10 ms. |
| `03-pitch-spread-ladder.wav` | 10.5 s | One note over a 220 Hz sine (Position 0.25, Density 32 Hz), held 0.25–10.25 s. A Pitch Spread 0, then 2, 7, 12 and 24 semitones at 2.25, 4.25, 6.25 and 8.25 s. Peak −7.4 dBFS. RMS −19.4 dBFS. | From one line to a cluster: each grain's pitch is drawn uniformly within ±2, ±7, ±12 and ±24 semitones, so a narrow wobble, a band, two octaves' scatter and four octaves'. No clicks between the steps (the spread applies to each new grain). |
| `04-size-spread-ladder.wav` | 8.5 s | One note over noise (Position 0.25, Size 100 ms, Density 32 Hz), held 0.25–8.25 s. A Size Spread 0, then 0.5, 1 and 2 octaves at 2.25, 4.25 and 6.25 s. Peak −5.3 dBFS. RMS −21.6 dBFS at 0 (0.5–2.25 s), −21.0 at 2 octaves (6.5–8.25 s). | Grains from one length to a scatter of lengths, drawn uniformly within ±2 octaves at the top (25 ms to 400 ms): the texture turns from an even hiss to a mix of ticks and swells. The level holds: 0.6 dB apart. |
| `05-layer-on-off.wav` | 8.0 s | The FX over the sweep. Layer A at its defaults; layer B (+7, 60 ms, 32 Hz) on at 1.5 s, off at 3.0 s, on at 4.5 s, off at 6.0 s. Peak −9.4 dBFS. RMS −18.3 dBFS with A alone (0.5–1.5 s), −22.2 with both (2.0–3.0 s), −19.9 after B goes off (3.2–4.5 s). | The layer speaks at once when switched on (its forced onset), and no click either way: each switch ramps over 10 ms. A dips about 4 dB while B plays, D1's sharing (above). |
| `06-source-switch.wav` | 4.0 s | The FX's layer A over `loop-48k.wav`, Live, switched to Sample (`tone-44k1.wav`, read at Position 0.29 of its 4 s) at 1.5 s and back to Live at 3.0 s. Density 32 Hz, Size 150 ms. Peak −9.9 dBFS. RMS −23.3 dBFS. | The switch itself is inaudible: the grains already sounding finish on the source they started on (about 150 ms of overlap at each switch), and the new source's grains come in under them. No click, no gap. |
| `07-lfo-position.wav` | 10.0 s | The FX over the sweep. LFO 1 (sine, 0.25 Hz) to Position, ±0.15 around 0.45 (0.27 s to 1.69 s behind the input); LFO 2 (square, 1 Hz) to Pitch, ±12 semitones. Density 32 Hz. Peak −8.4 dBFS. RMS −20.0 dBFS. | The scan moves smoothly: the cloud slides below and back up to the sweep every 4 s. The pitch steps an octave up and down on the square, every half second. |
| `08-envelope-notes.wav` | 12.0 s | The instrument over a 50 Hz–8 kHz sweep (Position 0.5, Density 32 Hz, Size 31 ms, Level −21 dB, Reverb Amount 25%). Env 1 (500 ms, 1 s, 30%, 1 s) to Size (+3 octaves) and Level (+15 dB); Env 2 (3 s, 2 s, 70%, 3 s) to Diffusion (+100%). Keys 60, 64 and 67 at 0.5, 4.0 and 7.5 s, 2.5 s, 2.5 s and 3 s long. Peak −9.6 dBFS. RMS −28.8 dBFS. | Each note swells over 500 ms, longer grains and louder, then settles to the sustain. The room opens slowly over each note as Env 2 raises Diffusion. The instrument spawns no grain after a note-off, so Env 1's release is heard only in the last grains and the reverb's tail. |
| `09-random-pitch.wav` | 10.0 s | The FX over a stereo 330 Hz sine (Density 32 Hz, Size 100 ms). Random 1 (4 Hz, Smooth 0) to Pitch, ±7 semitones; Random 2 (1 Hz, Smooth 50%) to Stereo, ±50% around 50%. Peak −7.7 dBFS. RMS −19.5 dBFS. | Stepped pitches, a new one every 250 ms; the width drifts, slewed rather than stepped. The same seed gives the same notes every time: render it twice and compare. |
| `10-step-sequence.wav` | 10.0 s | The FX over a stereo 220 Hz sine (Density 64 Hz, Size 60 ms, Position 46 ms, no spray). Step at 4 Hz, Length 12, to Pitch ±12: the figure 0, 3, 7, 12, 10, 7, 3, 0, −5, −2, 0, 5 semitones, 3 s round. Peak −6.4 dBFS. RMS −18.2 dBFS. | The sequence, three times and a bit, in time with itself; steps 13 to 16 never play. |
| `11-wheel-and-bend.wav` | 12.0 s | The instrument, keys 60, 64 and 67 held 0.25–11.75 s over a 220 Hz sine (Position 0.25, Density 32 Hz, Size 20 ms). The mod wheel (to Size, +4 octaves) rises over 0.5–4.5 s in 1/32 steps and falls over 4.5–6.0 s. Pitch bend (to Pitch, ±2 semitones) glides up over 6.5–7.5 s, back by 8.0 s, down over 8.5–9.5 s and back by 10.5 s. Peak −8.1 dBFS. RMS −20.5 dBFS. | The wheel widens the grains, from a buzz at 20 ms to a smooth chord at 320 ms, and back. The bend bends the whole chord up a tone and down a tone. |
| `12-follower-density.wav` | 5.0 s | The FX over `loop-48k.wav`'s bursts; the Env Follower to Density (+30, clamped at 400 Hz), from a base of 0.5 Hz. Size 60 ms, Position 20 ms behind, no spray. Peak −8.3 dBFS. RMS −35.5 dBFS over 0–4 s; silent after the input ends at 4 s. | Denser grains on each burst, thinning after: the follower peaks near 0.3 on a burst and falls to about 0.1 by the next (5 ms attack, 200 ms release). The grain-event log shows the onsets bunching on each burst. |
| `13-transport-beat.wav` | 10.0 s | The FX over the sweep, a 120 BPM transport playing from beat 0. The beat phase to Position, +0.2 from 0.35 (0.37 s to 1.26 s behind the input, every beat). Density 64 Hz, Size 60 ms, no spray. Peak −7.5 dBFS. RMS −19.1 dBFS. | The scan locks to the beat: every 0.5 s the cloud jumps back to the input's present and slides further into the past, a sawtooth in time with the tempo. |
| `14-macro-size-all-layers.wav` | 13.0 s | The FX over the sweep. Macro Size swept down to 0, up to 100% and back to rest in 1/16 steps every 0.125 s, first with layer A alone (0.5–4.5 s), then, from 6.0 s, with layers B (+7), C (−12) and D (+12) on too (7.5–11.5 s). Peak −9.8 dBFS. RMS −19.4 and −20.4 dBFS over the first sweep's bottom (1.5–2.5 s) and top (3.0–4.0 s); −25.7 and −27.1 over the second's (8.5–9.5 s, 10.0–11.0 s). | The macro moves every layer as one (decision 7): the four lines shorten and lengthen together. The four layers sit about 6 dB below layer A alone, D1's sharing (above). |

## Host checklist

Do each item in **Ableton Live 12** and in **Bitwig Studio**. Use CLAP in Bitwig, and VST3 and
AU in Live. Note the host and format of anything that fails.

### Layers B to D

- [ ] **From the generic editor.** In each product, open the host's generic parameter view.
  Layers B to D's parameters are there as `B Position` … `D Step 16`, with `Layer B On` …
  `Layer D On`, `Level B` … `Level D`, `Morph X` and `Morph Y`. Switch layer B on, set its
  Pitch and Size, and hear it join layer A.
- [ ] **From the remote pages.** In Bitwig, step through the device's eight remote-control pages
  (spec §6.9): "Perform", "Cloud" and "Space" as in M1, then "Layers" (the four Layer On
  switches and the four Levels), "Morph" (Morph X and Y) and "Cloud B" to "Cloud D" (that
  layer's Position, Size, Density, Pitch, Spray, Stereo, Freeze and Level). Each control moves
  what its name says, on the right layer.

### Threads (Bitwig)

- [ ] **Four layers under load, on the host's pool.** In a Bitwig project, switch all four layers
  on in several instances, with dense, long grains, until the CPU meter climbs. The plugin asks
  the host to run its layers (`request_exec`, spec I4): watch the per-plugin CPU, or the thread
  pool's activity, and note whether the load spreads. The sound is the same with the pool as
  without (the `[determinism]` tests prove the bytes; this checks the host side).

### The instrument's sidechain (D2)

- [ ] **Routed from a track.** Put Giga Grains on an instrument track, route another track's audio
  into its sidechain input, and switch a layer's Source to Live. Play notes: that layer granulates
  the routed audio, and the others keep their samples.

### Sessions from M1

- [ ] **A version-1 session sounds as it did.** Open a project saved with an M1 build (version-1
  state) of each product. It loads with layers B to D off and sounds as it did in M1: the factory
  routes reproduce the macros exactly.
- [ ] **The migrated instrument's Macro 7.** In that instrument project, Macro 7 was M1's Stereo
  macro; it is now Macro Feedback. The saved Stereo amount has been folded into Stereo's base, so
  the width sounds as it did, and Macro 7 now drives Feedback. Check both by ear.
