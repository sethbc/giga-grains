# Listening pack: M2 sub-project 3 (Engine scale and quality)

This pack is rendered for the record now and heard at M2's close (sub-project 7; the programme
design's decision 1). It has two parts:

- renders from `gg-render`, to hear offline, one per item of the spec's table (§11);
- a host checklist for Ableton Live 12 and Bitwig Studio, for the close's sitting.

The spec is `docs/superpowers/specs/2026-10-30-m2-engine-design.md`: the tiers and the Quality
parameter are its §6.1, the HQ sinc §6.2, the file pyramid §6.3, the weights, Budget and the
quotas §6.4, and the overload ladder §6.5 (the steal threshold is decision D4).

## Rendering the files

The renders aren't committed. The specs are in [`specs/`](specs/). To make the files, from the
repository root:

```sh
cmake --preset release && cmake --build --preset release --target gg-render
r=build/release/tools/gg-render/gg-render
L=build/release/listening/m2-engine
$r --write-media "$L/media"             # tone-44k1.wav and loop-48k.wav
for s in docs/listening/m2-engine/specs/*.json; do
  n=$(basename "$s" .json)
  $r "$s" --media-dir "$L/media" -o "$L/$n.wav" --spectrogram "$L/$n.png" \
     --metrics "$L/$n.metrics.json" --events "$L/$n.events.jsonl"
done
```

Each file is a 48 kHz, stereo, 32-bit float WAV, with a spectrogram PNG, the audio metrics and
the grain-event log beside it. Each grain's line carries its `tier` (0 Eco, 1 Normal, 2 HQ, after
the ladder) and `lvl` (the mip level it reads); a line `{"i":k,"layer":L,"steal":o}` records that
grain `k` of layer `L` was stolen for the grain whose onset is `o`. Every render is deterministic,
so on one platform the same commit gives the same bytes.

## The files

Every layer runs at **Level −6 dB**, and the FX files at Mix 100%. The HQ weight is 27 units per
stereo grain and 20 per mono one, against Normal's 4 and 3 (`kGrainWeights`, set from the
measured HQ/Normal cost ratio of 6.79), so a Budget of N grains holds N × 4 / 27 HQ grains.

The peaks and RMS levels below were measured on the `release` renders: the peak over both
channels and the whole file, the RMS over the mono mix's 10 ms envelope (the metrics JSON's
`rmsEnvelopeDb`), power-averaged over the span given. No file peaks above −5 dBFS. The grain
rates and counts come from the event logs; "sounding" is the mean number of a layer's grains
sounding at once over the span, a stolen grain counted until its fade ends.

The figures in *Listen for* marked "on a tone" were measured apart from these files, with
sample-mode renders of a single sine at 48 kHz (100 ms grains, 20 a second), to give each claim
a number the sweeps and loops can't isolate.

| File | Length | What it is | Listen for |
|---|---|---|---|
| `01-three-tiers.wav` | 9.5 s | The FX: layer A Live over a stereo 2 kHz–16 kHz sweep (amplitude 0.5, the file's length), layer B Sample over `loop-48k.wav`'s bursts (Position 0.25, Spray 100%), both at Pitch +7, Size 80 ms, Density 48 Hz; both layers' Quality Eco for 3 s, Normal for 3 s, HQ for 3.5 s. Peak −13.2 dBFS. RMS −25.3 dBFS in Eco (0.5–3.0 s), −25.5 in Normal (3.5–6.0 s), −25.9 in HQ (6.5–9.5 s). About 47 grains a second per layer throughout. | The tiers as characters. Eco's dull top and grit: on a tone at +7, a 16.3 kHz output sits 1.5 dB under HQ's and its first image only 19 dB under it. Normal, M1's sound: 0.4 dB under at the top, the image 25 dB under. HQ's clean top: the image 50 dB under. On the spectrogram the image is the faint line falling from 21 kHz as the sweep rises. |
| `02-hq-transposition-ladder.wav` | 20.5 s | The instrument, layers A (HQ) and B (Normal) reading the same 4 s sweep, run from 20 kHz down to 50 Hz so that grains reading forward at rate 8 never run off its end (Position 0.4, Spray ±1.6 s, Size 50 ms, Density 32 Hz). The pad alternates every 2 s: A alone (Morph X 0), then B alone (X 1), A first at each step. Pitch 0, +12, +19, +24 at 0, 4, 8 and 12 s; from 16 s the held key moves from 60 to 72 over Pitch +24, for +36. Peak −8.9 dBFS. RMS, HQ then Normal: 0: −19.1 and −19.1 dBFS; +12: −19.7 and −19.8; +19: −20.1 and −20.0; +24: −20.6 and −19.4; +36: −21.2 and −19.5. | At each step the Normal half folds the sweep's top back down into the band and the HQ half doesn't; the Normal half is louder by the folded energy from +24 up (1.2 dB at +24, 1.7 at +36). On a tone transposed to 30 kHz, HQ leaves 82 to 83 dB less than Normal at +12, +24 and +36 (exact octaves: a level of the pyramid at rate 1), 47 dB less at +19, and only 39 and 41 dB less at +11 and +23 (third question, below). |
| `03-slow-stretch.wav` | 12.5 s | The instrument, one note (key 36 over Pitch −24, rate 1/16), 200 ms grains at Density 32 Hz over `tone-44k1.wav` at Position 0.9 (its left channel's sweep near 4.8 kHz, its right a 220 Hz sine), held 0.25–12.25 s; Normal until 6.25 s, then HQ. Peak −8.1 dBFS. RMS −22.0 dBFS in Normal (0.5–6.25 s), −22.3 in HQ (6.5–12.25 s). | The Hermite's slow-stretch image tones against the sinc's: the sweep read 16 times slower sits near 300 Hz, and the Hermite puts an image near 2.7 kHz (3 kHz less it), a faint whistle at −67 dB on the spectrogram; with HQ it is down at −108 dB. The low line itself doesn't change (0.3 dB). |
| `04-overload-ladder.wav` | 12.5 s | The FX over a stereo 330 Hz sine (amplitude 0.4, the right channel a fifth up): four HQ layers at Density 400 Hz and Size 500 ms. Budget 4,096 for 3 s, 1,024 for 3 s, 256 for 3 s, 4,096 for 3.5 s. Peak −15.9 dBFS. RMS −28.6 dBFS at 4,096 (0.5–3.0 s), −35.8 at 1,024 (3.5–6.0 s), −41.9 at 256 (6.5–9.0 s), −31.2 back at 4,096 (9.5–12.5 s). | The spec expected the texture to soften (tiers lowered), thin (drops), churn (steals) and return. With the measured HQ weight, Budget 4,096 already overloads four HQ layers (200 grains each want 5,400 units against a quota of 4,096), so the file churns from the start: each layer admits its 400 grains a second, about 390 at HQ and 12 lowered, and steals as many, about 158 sounding. At 1,024 the first 0.5 s mostly drops (about 80 grains a second admitted per layer, of 400), then 167 a second admitted (143 HQ, 24 Normal) and as many stolen, 44 sounding. At 256: 46 a second (34 HQ, 12 Normal), 12 sounding. Lowering stays rare at every Budget: once the oldest grain is half-way, the ladder steals and admits the new grain at HQ. No click anywhere (clickScore 0.00). |
| `05-steal-tails.wav` | 10.0 s | The instrument, one HQ layer of 1 s grains at Density 200 Hz over `tone-44k1.wav` (Position 0.3), Budget 256 (37 stereo HQ grains); keys 60 (0.25–5.25 s) and 67 (3.0–8.5 s). Peak −9.6 dBFS. RMS −31.8 dBFS over the first 0.75 s, −29.2 over 1.0–5.25 s, −28.2 over 5.5–8.5 s, −31.1 in the tail. About 82 grains a second admitted (73 HQ, 9 lowered to Normal), 82 stolen, the rest of the 200 dropped; 43 sounding. | The stolen tails as a continuous, refreshing texture rather than a stuck chord: every grain is stolen at half its window, so the cloud renews about twice a second, in uneven waves: runs of 10 to 30 admissions, with drops between them while the new grains are young. Does D4's threshold sound right (second question)? |
| `06-quotas.wav` | 9.0 s | The FX over a stereo 110 Hz–1760 Hz sweep: Budget 512 (2,048 units); layer A at Density 400 Hz and Size 1 s (Normal); layer B, the same at Pitch +7, on at 3 s and off at 6 s. Peak −8.9 dBFS. RMS −16.7 dBFS with A alone (1.0–3.0 s), −25.8 with both (4.0–6.0 s), −20.3 after B goes off (6.5–9.0 s); the dense cloud of a sweep phases, so the half-second RMS swings by up to 6 dB within each span. | A thins when B takes its half, and recovers: A alone holds its 400 grains (407 sounding, nothing stolen); with B each layer's quota is 1,024 units, 256 grains, and A's grains are stolen at half their life (258 sounding, about 405 stolen a second, the same for B); after B goes off, 396 sounding, nothing stolen. About 3 dB of the drop is the pad's sharing between two layers; the rest is the thinning. |
| `07-live-hq.wav` | 8.5 s | The FX, layer A Live at HQ, Pitch +12, over a stereo 50 Hz–20 kHz sweep (amplitude 0.4, 8 s). Peak −10.1 dBFS. RMS −21.1 dBFS over 0.5–7.5 s. Every grain's line reads `"lvl":0`: the ring has one level. | The ring has one level until sub-project 2: HQ reads it stretched at rate 2. Once the doubled sweep passes 24 kHz (about 7.6 s) all that remains is fold-back: over 7.7–8.2 s it measures −76.6 dB on the spectrogram, against −47.7 dB for the same file at Normal, so 29 dB less, but not `02`'s 82 dB at an exact octave from the file's pyramid. |

## Three questions for the sitting

1. **Does HQ earn its weight on real material?** An HQ grain costs 6.8 Normal grains
   (`engine-sample-hq` over `engine-sample` at block 256), so a Budget holds about a seventh as
   many. Listen to `01`, `02` and `03` for whether the cleaner top and the missing image tones
   are worth that on loops and transpositions you'd use.
2. **Does D4's threshold sound right under sustained overload?** A grain may be stolen once it
   has sounded half its window. `04` and `05` churn in waves at that rate; a later threshold
   would hold grains longer and drop more, an earlier one would renew faster.
3. **Is HQ's weak spot acceptable?** Its anti-aliasing is weakest just under an octave or two
   octaves up, where the residual rate is near 2 and the stretched kernel's stopband is about
   −37 dB, and strongest at exactly an octave, where a pyramid level is read at rate 1. On a
   tone: 39 dB less fold-back than Normal at +11, 41 at +23, 47 at +19, 82 to 83 at +12, +24
   and +36. Compare `02`'s +19 step with its +12 and +24.

## Host checklist

Do each item in **Ableton Live 12** and in **Bitwig Studio**. Use CLAP in Bitwig, and VST3 and
AU in Live. Note the host and format of anything that fails.

- [ ] **Quality and Budget in the generic editor.** In each product, open the host's generic
  parameter view. `A Quality` … `D Quality` are there, each showing Eco, Normal and HQ (Normal
  by default), and `Budget`, 256 to 16,384 grains (4,096 by default). Automate one of each and
  see the automation lane follow.
- [ ] **HQ audible.** Set layer A's Pitch to +12 or +24 over bright material (a cymbal loop, a
  sweep) and switch its Quality between Normal and HQ: with HQ the fold-back under the top
  should go, and nothing else should change.
- [ ] **Budget 256 under four HQ layers never clicks.** Switch all four layers on at HQ, Density
  400 Hz, Size 500 ms or longer, and set Budget to 256. Play for a minute, then automate Budget
  between 256 and 16,384: the texture thins and churns, but there is no click, no dropout and no
  silent layer.
