# Listening pack: M1 sub-project 5 (the Perform UI)

This pack feeds listening gate #1 (sub-project 6). It has two parts:

- renders from `gg-render`, to hear offline, one per item of the spec's checklist (§9);
- a host checklist for Ableton Live 12 and Bitwig Studio.

The spec is `docs/superpowers/specs/2026-10-27-m1-perform-ui-design.md`. The macro recipes, and
so the depths you tune, are `macroOffsets` in `core/src/ParamMap.cpp` (spec §3.2).

## Rendering the files

The renders aren't committed. The specs are in [`specs/`](specs/). To make the files, from the
repository root:

```sh
cmake --preset release && cmake --build --preset release --target gg-render
r=build/release/tools/gg-render/gg-render
L=build/release/listening/m1-perform
$r --write-media "$L/media"             # loop-48k.wav, for file 08
for s in docs/listening/m1-perform/specs/*.json; do
  n=$(basename "$s" .json)
  $r "$s" --media-dir "$L/media" -o "$L/$n.wav" --spectrogram "$L/$n.png" \
     --metrics "$L/$n.metrics.json" --events "$L/$n.events.jsonl"
done
```

Each file is a 48 kHz, stereo, 32-bit float WAV, with a spectrogram PNG, the audio metrics and
the grain-event log (one line per grain: its onset, read position, duration and so on) beside
it. Every render
is deterministic, so on one platform the same commit gives the same bytes.

## The files

Every file runs at **Level −6 dB** except `11b`, at 0 dB. No file peaks above −1 dBFS. The
peaks and RMS levels below were measured on the renders; RMS is over both channels.

The FX files (all but `07b`, `08b` and `09`) run at Mix 100%, except `08`, and at the FX's
defaults otherwise: Position 0.25 s, Size 150 ms, Density 16 Hz, Distribution 50%, Spray 50 ms,
Stereo 50%. Most read a stereo sine sweep at amplitude 0.5, 110 Hz to 1760 Hz, as long as the
file. A rising sweep shows on the spectrogram where in time each grain reads: a grain from
further back sits lower.

The sweeps run in steps, the way an automation lane would step them. A **bipolar** macro (Size,
Density, Pitch, Spray, Stereo, Level; Mix's `08` is the exception, below) does this:

- it rests at 50% until 0.5 s;
- it steps down to 0 by 1.5 s, in 1/16 steps every 0.125 s, and **holds the bottom for 2 s**
  (4 s in `02`, so that 1 Hz gives several onsets);
- it climbs to 100% over 2 s, in the same steps, and **holds the top for 2 s** (2.5 s in `01`,
  two of its 1.2 s grains);
- it returns to rest over 1 s and rests for 1.5 s more.

So in `03`, `04`, `07b` and `08b` the bottom is held over 1.5–3.5 s and the top over 5.5–7.5 s;
in `01` the top is held over 5.5–8.0 s; in `02` the bottom over 1.5–5.5 s and the top over
7.5–9.5 s. The "bottom" and "top" levels in the table are measured over the last 1.5 s of each
hold, once the cloud is steady, except the last 1.3 s of `01`'s top and the whole of `02`'s
bottom hold (its pulses and the silence between them).
`08` (Mix) runs over a 4 s loop, too short for the pattern: it climbs once, from 0 to 100%.

A **unipolar** macro (Texture, Feedback) climbs from 0 at 0.5 s to 100% at 4.5 s, in 1/32
steps every 0.125 s. It holds there until 5.5 s, then steps back to 0 by 6.5 s.

**Holes in the cloud.** At the FX's defaults, the onsets' spacing at Distribution 50% is random
enough to leave short silences. A 30 s render at the defaults has 13 spacings over 150 ms, the
longest 240 ms, which leaves up to 90 ms with no grain sounding. Files with the same seed and
Density history share the same onsets, so the same holes recur (near 5.45 s in `04` and `06`,
for example). They are Distribution's behaviour, not a macro's.

| File | Length | What it is | Listen for |
|---|---|---|---|
| `01-macro-size.wav` | 10.5 s | **Macro Size**, bipolar, over the sweep. Size goes from 150 ms down to 19 ms and up to 1.2 s. Density moves the other way, from 16 Hz up to 45 Hz and down to 5.7 Hz. The top is held 2.5 s (13 onsets). Peak −6.0 dBFS at 8.82 s, on the way back down. RMS −20.4 dBFS at the bottom, −21.4 at the top, −20.7 at rest. | The bottom should be a buzzy, grainy smear (broadband haze over 1.5–3.5 s) and the top a slow, smooth wash. Density moves only half as far as Size (1.5 octaves to its 3), so the overlap still follows Size: about 0.85 at the bottom, where the cloud has gaps, and 6.8 at the top. The level holds within about 1 dB from bottom to top. **On the way back down (8.0–9.0 s)** RMS rises to −16.6 dBFS: the last long grains overlap the denser short ones that follow (observation (f)). Can you hear the swell? |
| `02-macro-density.wav` | 12.0 s | **Macro Density**, bipolar, over the sweep. Density goes from 16 Hz down to 1 Hz and up to 256 Hz. Distribution rises from 50% to 75% in the top half. The bottom is held 4 s (six onsets, at 1.89–4.93 s). Peak −9.0 dBFS. RMS −25.8 dBFS over the whole bottom hold (pulses and silence), −19.8 at the top, −18.7 at rest. | Separate pulses at the bottom (isolated blobs on the spectrogram), a dense cloud at the top. The grains' gain normalization holds the level within about 7 dB across the travel. Do the sparse pulses "pulse", and does the dense end "cloud"? |
| `03-macro-pitch.wav` | 10.0 s | **Macro Pitch**, bipolar, over a 220 Hz sine (its right channel a fifth up, 330 Hz). ±24 semitones: 55 Hz to 880 Hz on the left. Each 1/16 step is 3 semitones, so you hear a staircase. A host's knob glides. Peak −9.3 dBFS. RMS −18.4 dBFS at the bottom, −19.0 at the top, −19.0 at rest. | Both extremes still sound like the input, two octaves down and two up, each held for 2 s. Does ±24 feel like the right range for one knob, or too wide to play? |
| `04-macro-spray.wav` | 10.0 s | **Macro Spray**, bipolar, over the sweep. Spray goes from 50 ms down to 0 and up to 0.92 s. Stereo rises from 50% to 80% in the top half. Peak −8.3 dBFS. RMS −16.0 dBFS at the bottom, −19.5 at the top, −19.6 at rest. The left–right correlation is 0.98 at the bottom, 0.86 at the top and 0.96 at rest. | At the bottom the grains lock onto one point, and the cloud follows the sweep as one line (a little louder, most likely because the grains then overlap in phase). At the top the line breaks into several pitches, all **below** the line: the FX's spray reaches only further back in time (see observation (d)). The cloud widens a little. |
| `05-macro-texture.wav` | 7.5 s | **Macro Texture**, unipolar, over a 220 Hz sine, Density 3.0 (8 Hz) so the grains barely overlap. Texture runs from 0 to 1 over the first three quarters of the travel (0.5–3.5 s), then Diffusion from 0 to 100% over the last quarter (3.5–4.5 s). Peak −4.75 dBFS at 4.87 s. Click score 0.00. | Smooth swells become gated tones, then the diffuser blurs them. **The level climbs with the travel:** RMS −22.6 dBFS at rest and −13.8 at the top, about 9 dB louder. A gated grain carries more energy than a Hann one, and the gain normalization doesn't compensate. Should the macro compensate the level? Faint vertical streaks up to about 7 kHz from 4.3 s are most likely the gated edges, diffused. No click. |
| `06-macro-freeze.wav` | 7.5 s | **Macro Freeze** (stepped) over the sweep: on at 1.5 s, off at 3.5 s, on at 5.0 s, off at 6.5 s. Peak −9.7 dBFS. RMS −18 to −20 dBFS throughout. | While frozen, the pitch holds (flat lines on the spectrogram) as the input sweeps on underneath. On release the cloud jumps to the sweep's current pitch, through a dip of about 20 ms. The spectrogram shows a streak at each release, but there is no click (click score 1.86). |
| `07-macro-feedback.wav` | 8.0 s | **Macro Feedback**, unipolar, over the sweep, which stops at 4.5 s. Feedback is 0.9·u², up to 0.9. Peak −9.4 dBFS. RMS −19.1 dBFS over 1.5–2.5 s and −19.4 over 3.5–4.5 s. After the input stops: −21.9 over 4.5–5.0 s, −38.6 over 5.0–5.5 s. | As the macro rises, echoes stack up below the line, one every 0.26 s (Position plus the 256-sample feedback delay). After the input stops at full Feedback, about six repeats ring out, and the tail is gone by about 6 s. At Level −6 dB the regenerated tail falls 35–40 dB a second. Is the top of the travel regenerative enough? The fine control in the low half comes from the u² curve. |
| `07b-macro-stereo.wav` | 10.0 s | The instrument's **Macro Stereo**, bipolar. The sample is 4 s of noise, read at Position 0.5, and key 60 is held from 0.25 s to 9.75 s. Stereo goes from 50% down to 0 and up to 100%. Peak −8.3 dBFS. RMS −21.9 dBFS at the bottom, −22.2 at the top, −22.5 at rest. Since M2 sub-project 1 the instrument's Macro 7 is Feedback, so `specs/07b-macro-stereo.json` no longer parses; the file stays unchanged as the gate's record (INTENT §23 #65, ruling LS-T5a). | Use headphones. The correlation is 0.95 at rest, 1.00 at the bottom (mono) and 0.71 at the top. Even at the top the image stays fairly narrow. Is that wide enough? |
| `08-macro-mix.wav` | 5.0 s | **Macro Mix** over `loop-48k.wav` (noise bursts every 250 ms, 4 s long), with Mix's base at 50%. The macro starts at 0, so Mix starts at 0 (dry only). It climbs to 100% (wet only) in 1/32 steps every 0.1 s, from 0.4 s to 3.5 s. Peak −3.7 dBFS (a dry burst). RMS −28.4 dBFS over 0–0.3 s (dry), −37.7 over 3.5–4.0 s (wet). | The dry bursts, separated by silence, turn into the grains' smear of them, with no zipper (Mix ramps over 10 ms). **The wet is about 9 dB quieter than the dry** at Level −6 dB, or about 3 dB at the default 0 dB. Is the crossfade's loudness acceptable? |
| `08b-macro-level.wav` | 10.0 s | The instrument's **Macro Level**, bipolar. The sample is a sine sweep, 50 Hz to 8 kHz over 4 s at amplitude 0.5, read at Position 0.5, and key 60 is held from 0.25 s to 9.75 s. Level goes from −6 dB down to −18 dB and up to +6 dB. Peak −1.7 dBFS at 6.83 s. RMS −31.3 dBFS at the bottom, −8.0 at the top, −19.9 at rest. | The 24 dB of travel measures 23.3 dB, the top end in the wet soft limiter. From about 5.0 s to 8.2 s the spectrogram fills with harmonics up to Nyquist. Is the top's distortion audible? From the default Level (0 dB) the macro's top is +12 dB, deeper still. Is ±12 dB the right depth? |
| `09-texture-steps.wav` | 8.0 s | **Texture** over a held instrument note: a 220 Hz sine sample at Position 25%, Density 2.0 (4 Hz), Size 177 ms, Distribution 0 (regular onsets). The grains are separate, at a 71% duty cycle, so each envelope's shape shows. Texture is 0, 0.25, 0.5, 0.75 and 1, for 1.5 s each from 0 s. Peak −10.3 dBFS. RMS −20.8, −20.8, −20.8, −18.4 and −16.8 dBFS per step. Click score 0.00. | **Hann** (0); a **swell** peaking at 60% of the grain (0.25), then at 70% (0.5); a short attack with a 45% **sustain** (0.75); **gated**, with 8.8 ms raised-cosine edges and a 90% sustain (1). From 6.0 s the spectrogram widens the gated grains' skirts to about 2 kHz. Does the travel go from smooth to gated with no click, and does 0.5 sound like a swell? |
| `10-feedback-50.wav` | 8.0 s | **Feedback 0.5**, unfrozen, over the sweep, which stops at 3.0 s. Peak −7.2 dBFS. RMS −18.8 dBFS while the input plays. After it stops: −21.5, −45.5 and −77 dBFS over 3.0–3.5, 3.5–4.0 and 4.0–5.0 s. | About four echoes after the input stops, each 0.26 s apart and lower on the sweep. They should fade cleanly. |
| `11-feedback-90.wav` | 8.0 s | **Feedback 0.9**, as `10`. Peak −5.9 dBFS. RMS −18.5 dBFS while the input plays. After it stops: −21.1, −38.7 and −60.8 dBFS. | About eight echoes, gone by about 5.5 s. At Level −6 dB, 0.9 is still far from self-oscillation. |
| `11b-feedback-90-level-0.wav` | 8.0 s | **Feedback 0.9 at Level 0 dB** (the default Level), as `11`. Peak −1.8 dBFS. RMS −11.4 dBFS while the input plays. After it stops: −13.6, −21.9 and −33.3 dBFS, and still −96 dBFS at 7.5–8.0 s. | A long tail, which is what the macro's top is for. **From 0.5 s to 4.5 s, broadband streaks reach Nyquist.** In the top six bands they measure −76 to −101 dB, against −144 to −158 dB in `11`. This is observation (b) at its loudest: the wet soft limiter, driven by the regenerated signal. Is it audible as hash or grit? |
| `12-feedback-90-freeze.wav` | 8.0 s | **Feedback 0.9, then Freeze** on at 2.0 s and off at 4.5 s, over the sweep, which stops at 3.0 s. Peak −7.8 dBFS. RMS −18.8 dBFS while frozen (2.0–4.5 s). After the release: −20.3 over 4.5–5.0 s and −38.3 over 5.0–5.5 s. | Freeze wins: the frozen ring holds steady, with no build-up, while Feedback stays at 0.9. On release the echoes ring out as in `11`. The streak at 4.5 s is the 20 ms jump out of the frozen pitch, not a click (click score 1.84). |
| `13-fx-scan-window.wav` | 9.0 s | **The FX scan window.** The sweep runs 9 s at a fixed Position 0.5, which is 0.94 s behind the head. Spray climbs from 0 to 100% in 1/32 steps every 0.2 s, from 1.2 s to 7.4 s. Peak −7.4 dBFS. RMS −18.6 dBFS over 4.0–4.5 s, −19.5 over 7.4–9.0 s. Silent for the first 0.94 s, while the grains read from before the input began. | The grain-event log shows every grain reading at least 0.941 s behind the head. That is Position, never less. At full Spray the grains read 1.04 s to 2.89 s behind. On the rising sweep the cloud spreads only **downward** in pitch, up to about 0.9 octaves below the Spray-0 line, never above it. Spray's exponential taper keeps the spread small for the first 60% of the parameter's travel: about 0.2 s at Spray 60%. Compare with the drawn window: see observation (d). |

## Checklist (spec §9)

- [ ] **1. Each macro across its travel** (`01`–`08`, `07b`, `08b`). Every macro sounds good
  from one end to the other, with no dead zone and no cliff. The depths are starting values
  (spec §3.2). **Tune them by ear.** The values live in `core/src/ParamMap.cpp`'s
  `macroOffsets`. When you settle them, the tuned values are committed there and recorded in a
  new "Decision notes" section at the end of the spec (in the style of the spec's in-place
  amendments) and
  in INTENT §23's #58. These come up in the renders:
  - **Size:** Density compensates only half of Size's octaves, so the overlap still follows
    Size (`01`).
  - **Pitch:** is ±24 semitones the right range (`03`)?
  - **Spray:** with the exponential taper, the first 60% of the Spray parameter's travel
    barely spreads (`13`); Macro Spray's top reaches Spray 86%, 0.92 s (`04`).
  - **Texture:** the macro gets about 9 dB louder toward its top (`05`).
  - **Feedback:** is 0.9·u² regenerative enough at Level −6 dB (`07`, `11`, `11b`)?
  - **Stereo:** the top reaches a correlation of only 0.71 (`07b`).
  - **Mix:** the wet is about 9 dB below the dry at Level −6 dB (`08`).
  - **Level:** the top drives the soft limiter (`08b`).
- [ ] **2. Texture from smooth to gated** (`09`, `05`). The five anchors sound like Hann, a
  swell and a gate, and no step clicks. Spec §4's gated anchor has edges of at least 16 samples
  and 5% of the grain: 8.8 ms in `09`.
- [ ] **3. Feedback** (`10`, `11`, `11b`). 0.5 gives a few clean echoes and 0.9 a long tail.
  The echoes come 0.26 s apart: Position, plus the fixed 256-sample delay.
- [ ] **4. Feedback and Freeze** (`12`, then in a performance; see the host checklist).
  Freezing holds the ring with no build-up, and releasing it lets the tail ring out.
- [ ] **5. The scan window** (`13`, then in the Perform view). The FX's window is one-sided:
  it runs from Position back to Position + Spray (ruling PU-T8a). Does the drawn window match
  where you hear the grains read?

**Observations for the director.** Found during the build; listen for each.

- **(a) Feedback 0.95 at Level +12 dB** settles into self-oscillation just under the limiter,
  at −0.18 dBFS, after the input stops. That is the 60 s stability test's setting. The output
  stays bounded, but it sings. `11b` (0.9 at 0 dB) is the nearest render here; turn Level up in
  a host to hear the rest.
- **(b) The golden `fx-feedback`** (`tests/golden/cases/fx-feedback.json`) has broadband streaks
  up to Nyquist from about 1.5 s, at Feedback 0.5. Its top bands are about −62 dB, against −79
  and −86 dB in `fx-automate`, and its click score is 4.29. Re-windowing and the limiter raise
  the noise floor there. `scripts/update-goldens.sh` leaves its WAV in `build/dev/golden/`;
  `11b` here shows the same effect at its strongest.
- **(c) The golden `inst-texture`'s RMS swings about 6 dB within one Texture step.** The cause
  is the sprayed sine's phases where the grains overlap, so only the step to Texture 1 is
  clearly louder. In `09` the grains don't overlap: the level holds within each step, and the
  steps to 0.75 and 1 are 2.4 and 4.0 dB louder.
- **(d) The FX scan window is one-sided** (ruling PU-T8a), because the engine's live spray reads
  only further back from Position. The instrument's window stays ±Spray around Position, as its
  sample spray is. Judge whether the drawn window matches what is heard (`13`; in a host, sweep
  Spray on a sustained input).
- **(e) The macOS FX standalone asks for microphone permission on first launch.** Allow it to
  hear the FX standalone.
- **(f) A move down from Macro Size's top swells** (`01`, 8.0–9.0 s). With the top held, the
  level is steady (−21.4 dBFS RMS). Bringing the macro back to rest over 1 s lets the last long
  grains (up to 1.2 s) overlap the denser, shorter ones that follow: RMS rises about 4 dB, to
  −16.6 dBFS, and peaks at −6.0 dBFS, at the soft limiter's knee.
- **(g) Holes at the defaults.** Distribution 50% leaves brief silences, up to about 90 ms (see
  "Holes in the cloud" above). Is that the right default character?

## Host checklist

Do each item in **Ableton Live 12** and in **Bitwig Studio**. Use CLAP in Bitwig, and VST3 and
AU in Live. Note the host and format of anything that fails.

**First, install the build under review.** The AU in `~/Library/Audio/Plug-Ins/Components` is a
branch build, installed during validation. Replace it, and the CLAP and VST3, with the release
build of the commit you are judging (CLAUDE.md, "Validators", shows the AU's install).

- [ ] **Both products load in both hosts,** and each opens the Perform view. It has a header
  (product, preset "Init", Perform, Edit greyed out, "frame ms (avg 60)"), the canvas and the
  row of eight macros. The instrument also has the loader strip.
- [ ] **The three remote-controls pages in Bitwig.** Both products show three pages:
  - "Perform": the eight macros, Size, Density, Pitch, Spray, Texture, Freeze, then Feedback
    and Mix (FX) or Stereo and Level (instrument);
  - "Cloud": Position, Size, Density, Pitch, Spray, Stereo, Freeze, Mix, with the instrument's
    Mix slot empty;
  - "Space", unchanged.

  Each knob moves its parameter, and the editor's macro row follows. Texture and Feedback are on
  no page; reach them through the host's parameter list or the macros. Feedback is hidden in the
  instrument.
- [ ] **Each macro swept across its travel,** on a sustained input (FX) or a held note
  (instrument), and automated in Live. Both the knob and the automation sound smooth, with no
  zipper; Macro Freeze switches.
- [ ] **Depths tuned by ear.** For each macro, note the depth you would change and to what (see
  item 1 above). The tuned values get committed in `ParamMap.cpp` and recorded in a new
  "Decision notes" section at the end of the spec and in INTENT §23's #58.
- [ ] **Feedback and Freeze in a performance.** On the FX, play a loop, ride Macro Feedback up
  and down, and punch Macro Freeze on and off. The tail regenerates without running away. Freeze
  holds and releases without a click. The canvas shows the freeze tint and the feedback marker
  at the head.
- [ ] **The canvas, judged for truthfulness.** Do the grains drawn match what is heard?
  - Their x position should be where they read, their length their duration, their brightness
    their gain, their height their pan.
  - A dashed ghost window should appear where the macros move the effective Position or Spray.
  - The FX's window should be one-sided (observation (d)).
  - Drag the window body (Position) and an edge (Spray). Each drag should record as automation
    in the host.
- [ ] **The frame-ms readout,** in Live 12 and in Bitwig: at most 3 ms (INTENT §16) at the
  densest setting a host can reach (decision D3, below). The 8,192 grains spec §9 first asked
  for can't be reached in a host: the M0 stress panel is gone, so the real view draws only the
  grains that are playing. The engine's pool holds 4,096 at most, and its densest setting
  (Density 400 Hz, Size 2 s) sounds about 800 at a time. Read the frame ms at that densest
  setting, in a full-screen editor too.

**How the frame budget is judged: decided** (decision D3, the director, 2026-10-29; spec §9 as
amended, INTENT §16, §23 #58). The densest setting a host can reach stays at or under 3 ms in
Live and Bitwig, and an idle `gg-bench --canvas` run at 8,192 grains stays at or under 3 ms,
taken with sub-project 6's measurement of X. Offline, `gg-bench --canvas` measured this build at
3.66 ms per frame at 8,192 grains during a build on the same machine, over the budget, and at
2.80 ms on a second run with nothing building (M0's, 2.77 ms; see Verification). Sub-project 6's
idle run (2026-10-29) measured 2.87 ms (p95 2.92 ms; `docs/benchmarks/2026-10-29-m1-canvas.json`),
inside the budget.

## Verification at fe4de01

These were run on the release build of `fe4de01` (the sub-project's code). This pack adds only
documents. Machine: Apple M5 Max, macOS. Other builds were sharing the machine (load average
about 3).

**Validators** (CLAUDE.md, "Validators"), on `build/release/gg-products`:

- **clap-validator 0.4.1, `Giga Grains.clap`** (both plugins): `81 tests run, 67 passed, 0
  failed, 0 warnings, 14 skipped`. `param-conversions` PASSED for both plugins, with Macro
  Freeze stepped. The 14 skips are the layout, preset-discovery and double-precision tests and
  `process-audio-denormals`, which don't apply.
- **pluginval v1.0.4, strictness 10, `Giga Grains.vst3`**: `Num plugins found: 2`, both
  tested, `SUCCESS`.
- **Steinberg's VST3 validator** (built from the CPM VST3 SDK checkout): `Result: 94 tests
  passed, 0 tests failed`.
- **auval**, with the `.component` installed for the user:
  - `aumu GGin Sbcx`: `AU VALIDATION SUCCEEDED.`
  - `aufx GGfx Sbcx`: `AU VALIDATION SUCCEEDED.`
  - `aumf GGfx Sbcx`: `AU VALIDATION SUCCEEDED.`

  Warnings: "Recommended Property is not supported" (×4), "Preset name is not retained in
  retrieved class data" (×3), and, for `aufx` only, "AU implements MusicDeviceMIDIEvent but is
  of type 'aufx'". auval lists all 28 parameters with the new names, in ascending ID order.

**Bench** (`gg-bench --quick --cases reference,engine-sample,engine-sample-eco`, release; a
smoke check: the full 5-minute run is CI's). Each ratio is the case's ns per grain-sample over
the reference kernel's, median over 5 rounds. Task 3 measured its ratios under heavier load
(load average 5–8).

| case | block | ratio (IQR) | Task 3 (after) |
|---|---|---|---|
| engine-sample | 32 | 1.1731 (0.0335) | 1.193 / 1.180 |
| engine-sample | 64 | 1.0727 (0.0403) | 1.112 / 1.111 |
| engine-sample | 256 | 1.0349 (0.0051) | 0.997 / 1.046 |
| engine-sample-eco | 32 | 0.8071 (0.0239) | 0.824 / 0.820 |
| engine-sample-eco | 64 | 0.7603 (0.0021) | 0.754 / 0.753 |
| engine-sample-eco | 256 | 0.6780 (0.0205) | 0.657 / 0.679 |

`engine-sample` (X) at block 256 is 18.98% of one core at 48 kHz in this run.

**The canvas** (`gg-bench --canvas`: the splat renderer, headless, at 3200×1200 physical pixels,
a 1600×600-point canvas at 2×, larger than the editor's default 900×500 window; the median of
240 frames). **The first run exceeds INTENT §16's 3 ms budget**, at both 8,192 and 4,096 grains.
It ran while `scripts/check.sh` was building on the same machine. A second run with nothing
building (load average about 4) came in under the budget, matching M0's 2.77 ms (idle,
same canvas; `docs/spikes/m0-canvas-budget.md`).

| grains | run 1, during a build (mean, p95) | run 2, load ~4 (mean, p95) | M0, idle |
|---|---|---|---|
| 4,096 | **3.05 ms** (3.26, 4.23) | 2.51 ms (2.54, 2.77) | 2.41 ms |
| 8,192 | **3.66 ms** (3.94, 5.01) | 2.80 ms (2.82, 3.02) | 2.77 ms |
| 16,384 | 4.79 ms (5.15, 6.67) | 3.52 ms (3.52, 3.60) | 3.52 ms |

Neither run was on an idle machine. The idle run that decision D3 (above) judges the budget by
was taken with sub-project 6's measurement of X, on 2026-10-29
(`docs/benchmarks/2026-10-29-m1-canvas.json`): 2.50 / **2.87** / 3.66 ms at 4,096 / 8,192 /
16,384 grains (p95 2.57 / 2.92 / 3.71). The 8,192 figure is inside the 3 ms budget.
