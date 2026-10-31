# Listening pack: M2 sub-project 4 (Extremes)

This pack is rendered for the record now and heard at M2's close (sub-project 7; the programme
design's decision 1). It has two parts:

- renders from `gg-render`, to hear offline, one per item of the spec's table (§11);
- a host checklist for Ableton Live 12 and Bitwig Studio, for the close's sitting.

The spec is `docs/superpowers/specs/2026-10-30-m2-extremes-design.md`: Smear is its §6.1 (the
strength is decision D8), the Spectral kind §6.2 (what it smears is D1, Freeze D2), the buses
and their filters §6.3 (D4), the delay §6.4 (the time glide is D6, the hold D7) and the limiter
§6.5.

## Rendering the files

The renders aren't committed. The specs are in [`specs/`](specs/). To make the files, from the
repository root:

```sh
cmake --preset release && cmake --build --preset release --target gg-render
r=build/release/tools/gg-render/gg-render
L=build/release/listening/m2-extremes
$r --write-media "$L/media"             # tone-44k1.wav and loop-48k.wav
for s in docs/listening/m2-extremes/specs/*.json; do
  n=$(basename "$s" .json)
  $r "$s" --media-dir "$L/media" -o "$L/$n.wav" --spectrogram "$L/$n.png" \
     --metrics "$L/$n.metrics.json" --events "$L/$n.events.jsonl"
done
```

Each file is a 48 kHz, stereo, 32-bit float WAV, with a spectrogram PNG, the audio metrics and
the grain-event log beside it (one line per grain, with its `layer`, 0 to 3 for A to D). Every
render is deterministic, so on one platform the same commit gives the same bytes.

## The files

Fifteen renders from fifteen specs: the spec's table has thirteen items, and `02` has three
(`02a`, `02b`, `02c`). Every layer runs at **Level −6 dB**, except `02` and `04` (+6 dB, since the loop is
quiet) and `12` (+6 dB on all four layers, which is its point). The FX files run at Mix 100%.
The instrument files hold one note over the whole file, at Position 0.25 to 0.5 of a generated
4 s to 10 s sample (a position is a fraction of the sample there).

- `loop-48k.wav` (`02`, `04`, `09`) is the generated drum-like loop: a 30 ms noise burst every
  250 ms for 4 s. The noise sample is white noise at amplitude 0.5. The sweep sample is a
  100 Hz–2 kHz sweep (10 s in `03`, 4 s in `05`).
- **Smear** (`01`, `02`): "A Smear" runs 0 to 1. Its strength is decision D8's option 1 (the
  overlap raised toward 32, the jitter ±half a grain's length, the detune ±50 cents at 1).
  **`02c` is not option 2.** Option 2 (toward 16, ±a quarter length, ±20 cents) is a build
  constant that wasn't built, so it can't be rendered. `02c` is Smear 0.5, whose jitter (±a
  quarter length) and detune (±25 cents) are close to option 2's, though its density is halfway to
  the target overlap rather than toward 16. Hear it as the middle of the ladder, not as option 2.
- **The Spectral kind** (`03`, `04`, `05`): "A Source" 3. Position addresses the material as a
  Sample layer or a Live layer would: a fraction of the sample in `03` and `05`, seconds behind
  the head in `04`, where the input ring is the material (no layer sample).
- **Buses and filters** (`06`, `07`, `08`): Bus 1 and Bus 2 are one layer's two grain buses,
  "A Bus Split" the share of its grains on bus 2. A filter's type is 0 Off, 1 Low-pass, 2
  High-pass, 3 Band-pass or 4 Notch; its cutoff is in log2 Hz.
- **The delay and the limiter** (`09` to `12`, `13`): the delay sits after the layers' sum, the
  limiter after the delay.

The peaks and RMS levels below were measured on the `release` renders, over both channels and the
whole file unless a span is given. No file peaks above −3.9 dBFS.

| File | Length | What it is | Listen for |
|---|---|---|---|
| `01-smear-ladder.wav` | 12.0 s | The instrument over noise at a static Position (0.4, so no motion: an infinite stretch), Size 200 ms, Density 20 Hz (overlap 4). "A Smear" 0, then 0.25, 0.5 and 1 at 3, 6 and 9 s. Peak −7.2 dBFS. RMS −21.7, −21.9, −21.9 and −22.8 dBFS over 1–3, 4–6, 7–9 and 10–12 s. | The comb's ringing pitch at 0 (grains re-reading the same noise every 50 ms) fades into an even wash. The level holds: 1.1 dB apart. |
| `02a-stretch-1000x-smear-0.wav` | 12.0 s | The instrument over `loop-48k.wav` at a 1000x stretch: LFO 1 (saw up, 0.01 Hz) to Position, 0.025 deep, so the grains read forward 1 s of source in 100 s. Size 300 ms, Density 12 Hz, Level +6 dB. Smear 0. Peak −5.7 dBFS. RMS −33.9 dBFS. | The static comb and buzz: the same few bursts re-read over and over, in pulses. |
| `02b-stretch-1000x-smear-1.wav` | 12.0 s | The same, Smear 1 (D8's option 1). Peak −5.8 dBFS. RMS −27.5 dBFS. | A/B with `02a`: a smooth, slowly moving texture instead of pulses, and the detune's chorus at 1. It is 6.4 dB louder than `02a`: the raised overlap spreads the loop's bursts over the whole cloud, where at 0 they sit in a few grains. |
| `02c-stretch-1000x-smear-mild.wav` | 12.0 s | The same, Smear 0.5, standing in for option 2 (see above). Peak −5.5 dBFS. RMS −30.2 dBFS. | Between the two: how much of the comb survives half of Smear, and how much of the chorus. |
| `03-spectral-scan.wav` | 15.0 s | The instrument, layer A Spectral over a 10 s, 100 Hz–2 kHz sweep sample (Size 200 ms, Density 32 Hz). LFO 1 (sine, 0.1 Hz) to Position, ±0.4 around 0.5, so the window scans back and forth over the sweep. Peak −5.7 dBFS. RMS −19.4 dBFS. | Paulstretch's glassy, transient-free smear of one pitch at a time, rising and falling with the scan, between about 200 Hz and 1.8 kHz. |
| `04-spectral-freeze-fx.wav` | 10.0 s | The FX, layer A Spectral over `loop-48k.wav` as the input (Position 0.28, 0.23 s behind the head; Size 200 ms, Density 32 Hz, Level +6 dB). Freeze on at 3 s, held to the end (the loop ends at 4 s). Peak −4.6 dBFS. RMS −27.5 dBFS over 1–3 s, −20.2 over 4–10 s. | Before 3 s the bursts' spectrum pulses with them; after it, a pad of the burst spectrum that holds still and keeps shimmering, as the smear evolves under the frozen ring (D2 option 1). |
| `05-spectral-vs-grains.wav` | 18.0 s | The instrument over a 4 s sweep sample (Position 0.3), layer A Spectral and layer B Sample over the same sample, both Density 32 Hz, Size 200 ms. Morph X from 0 (layer A) to 1 (layer B) over 2–8 s in 1/48 steps, held to 10 s, and back over 10–16 s. Peak −7.3 dBFS. RMS −18.9 dBFS over 0.5–2 s (A), −20.3 over 8.5–10 s (B), −19.5 over 16.5–18 s (A). | The smear against plain grains of the same material: a wash with no edges, and then a stream of grains of one tone, and back. The two are within 1.4 dB of each other. |
| `06-bus-split-filters.wav` | 16.0 s | The FX over stereo noise. Bus 1 Low-pass 300 Hz (Resonance 30%), bus 2 High-pass 3 kHz (30%). "A Bus Split" 0 (all grains on bus 1), then 0.5 over 3–5 s, 1 over 7–9 s and back to 0 over 11–13 s, in 1/16 steps. Peak −7.3 dBFS. RMS −39.1 dBFS over 1–3 s (bus 1 alone), −26.6 over 5–7 s (both), −22.6 over 9–11 s (bus 2 alone), −40.3 over 14–16 s. | One cloud becomes two spectral halves and back: a low rumble, then rumble and hiss together, then hiss alone. No click at any move. |
| `07-filter-type-switch.wav` | 12.0 s | The FX over a stereo 330 Hz sine, its grains scattered ±24 semitones (A Pitch Spread 24; Density 32 Hz, Size 100 ms) so there is something above and below the cutoff. Bus 1's filter, at 330 Hz and Resonance 40%, steps Off, Low-pass (2 s), Band-pass (4 s), High-pass (6 s), Notch (8 s), Off (10 s). Peak −8.0 dBFS. RMS over each step's span: −19.0 (Off), −23.3 (LP), −24.0 (BP), −21.4 (HP), −21.5 (Notch), −20.0 dBFS (Off). | Each switch crossfades (10 ms); nothing clicks. The cloud loses its top, then everything but a band around the cutoff, then its bottom, then a notch, then is whole again. |
| `08-filter-lfo.wav` | 14.0 s | The instrument over noise, Size 100 ms, Density 32 Hz. Bus 1 Low-pass at 1 kHz, Resonance 60%; LFO 1 (sine, 0.25 Hz) to "A Bus 1 Cutoff", ±2 octaves (250 Hz to 4 kHz). Peak −12.2 dBFS. RMS −29.8 dBFS. | A smooth sweep, a resonant wah every 4 s, with no zipper. |
| `09-delay-pingpong.wav` | 12.0 s | The FX over `loop-48k.wav`. Delay Amount 60%, Time 375 ms, Feedback 60%, Damping 60%, ping-pong on (Density 32 Hz, Size 100 ms). Peak −9.9 dBFS. RMS −42.5 dBFS (the loop is bursts and silence). | Echoes alternating sides, each darker than the last as Damping takes the highs. |
| `10-delay-time-glide.wav` | 10.0 s | The FX over a stereo 330 Hz sine (Density 32 Hz, Size 100 ms). Delay Amount 60%, Feedback 50%, Damping 30%. Time 100 ms, then 500 ms at 3 s and 50 ms at 6 s. Peak −6.6 dBFS. RMS −18.1 dBFS. | The tape bend on each move (D6): the echoes' pitch slides down as the delay lengthens, up as it shortens, then settles. No click. |
| `11-delay-hold.wav` | 22.0 s | The FX over a 150 ms burst of a 330 Hz sine. Delay Amount 100%, Time 200 ms, Feedback 100%, Damping 0 (Density 32 Hz, Size 100 ms). Peak −11.9 dBFS. RMS −19.6 dBFS over each second from 2 s to 22 s, within 0.1 dB. | The repeat holds for 20 s without growing or fading (D7): a loop of the burst, forever. (A burst of white noise in the same loop was tried first and faded by about 2 dB from 9 s to 22 s: probably the damper at 0, which is still a low-pass near the top of the band; a tone holds.) |
| `12-limiter-modes.wav` | 12.0 s | The FX over stereo noise, four layers on at Level +6 dB each, Limiter Ceiling −6 dB. Limiter Mode Off, then Soft at 4 s and Peak at 8 s. Peak −4.0 dBFS over 1–4 s (Off), −7.8 over 5–8 s (Soft), −6.0 over 9–12 s (Peak). RMS −16.3, −16.6 and −17.1 dBFS. | Off clips nothing and sums hot (no sample past −4 dBFS, the ceiling ignored); Soft rounds the peaks; Peak holds the −6 dBFS ceiling exactly, and the level dips 0.5 dB as it works. |
| `13-all-together.wav` | 16.0 s | The FX over a 110–1760 Hz sweep. Layer A Spectral (Position 0.28, Size 200 ms), its buses split 50%, bus 1 Low-pass 600 Hz and bus 2 High-pass 2 kHz; layer B (+7 semitones, Size 150 ms) at Smear 0.7; Level −6 dB each. The delay at 40% (375 ms, Feedback 50%, Damping 50%, ping-pong) and the Peak limiter at −3 dB. Peak −10.4 dBFS. RMS −23.4 dBFS. | The chain as a whole: does it still sound like one instrument? |

## Host checklist

Do each item in **Ableton Live 12** and in **Bitwig Studio**. Use CLAP in Bitwig, and VST3 and
AU in Live. Note the host and format of anything that fails.

- [ ] **The remote pages in Bitwig.** Step through the device's remote-control pages. The
  "FX" page holds the delay's and the limiter's controls, and "Filter A" holds layer A's bus
  split and both buses' filters. Each control moves what its name says.
- [ ] **The Spectral kind in Live, with the sidechain.** Put Giga Grains on an instrument
  track, route another track's audio into its sidechain, set a layer's Source to Live, and then
  to Spectral. Play notes: the layer smears the routed audio. Set the Source back to Live and
  the sound's tail ends, with no click.
- [ ] **Automating Delay Time and Bus 1 Cutoff in Live.** Draw automation lanes for both and
  play. Time bends the echoes (the tape bend, `10`) and the cutoff sweeps without zipper.
- [ ] **The Peak limiter on a hot four-layer patch.** Switch all four layers on at Level +6 dB
  over dense, long grains, set the limiter to Peak at −6 dB, and watch the host's meter: it
  never passes −6 dB, and the host reports no clipping.
- [ ] **A Smear 1 patch's CPU against Smear 0.** Load one patch at Smear 0, note the host's
  CPU meter for the plugin, then set Smear to 1: the overlap, and so the load, rises, and never
  past the 400 Hz grain-rate ceiling. Note the two figures.
