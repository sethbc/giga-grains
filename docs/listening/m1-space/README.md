# Listening pack: M1 sub-project 4 (Space)

This pack feeds listening gate #1 (sub-project 6). It has two parts:

- renders from `gg-render`, to hear offline, one per item of the spec's checklist (§9);
- a host checklist for Ableton Live 12 and Bitwig Studio.

The spec is `docs/superpowers/specs/2026-10-28-m1-space-design.md`.

## Rendering the files

The renders aren't committed. The specs are in [`specs/`](specs/). To make the files, from the
repository root:

```sh
cmake --preset release && cmake --build --preset release --target gg-render
r=build/release/tools/gg-render/gg-render
L=build/release/listening/m1-space
$r --write-media "$L/media"             # loop-48k.wav, for file 05
for s in docs/listening/m1-space/specs/*.json; do
  n=$(basename "$s" .json)
  $r "$s" --media-dir "$L/media" -o "$L/$n.wav" --spectrogram "$L/$n.png"
done
```

Each file is a 48 kHz, stereo, 32-bit float WAV, with a spectrogram PNG beside it. Every render
is deterministic, so on one platform the same commit gives the same bytes.

## The files

Every file runs at **Level −6 dB**, except `08`, which uses the default **Level 0 dB**. No file
peaks above −1 dBFS; the peaks below are measured on the renders.

The instrument files (`01`, `02`, `07`, `08`) share one sample: a 4 s sine sweep, 50 Hz to
8 kHz, at amplitude 0.5 (0.45 in `08`), read at Position 0.5. Any Space parameter a file doesn't
name is at its default: Diffusion 0, Amount 0, Decay 2 s, Size 50%, Damping 30%, Pre-delay 0,
Modulation 25%, Dry to Reverb 0.

Two things hold in every file with reverb:

- **The reverb is first heard about 60 ms after its input**, even at Pre-delay 0: those are
  Reverb 2's own early taps at Size 50%, not a delay we add.
- **Decay's label reads short of the decay you hear** (ruling R-T5c). Measured T60s: label 1 s
  → 1.72 s, 2 s → 2.89 s, 4 s → 5.09 s. The renders agree: `02b` (label 2 s) loses about 19 dB
  a second, a T60 of about 3 s; `02c` (label 8 s, Size 90%) about 6 dB a second, about 10 s.

| File | Length | What it is | Listen for |
|---|---|---|---|
| `01-diffusion-sweep.wav` | 10.5 s | Item 1. A sparse cloud: Density 2.0 (4 Hz), Size 62.5 ms, key 60 held 0.25–10 s. Diffusion steps from 0 at 1.0 s to 100% at 8.75 s, 1/32 every 0.25 s. No reverb. Peak −9.7 dBFS. | Separate grains at the start blur into a texture by the end, and the image widens (the left–right correlation falls from 0.96 to 0.00). It should never sound like a reverb (no tail of its own) or a chorus (no pitch movement). |
| `02a-room.wav` | 6.0 s | Item 2, **small room**: Amount 50%, Decay 0.5 s, Size 20%. Three short stabs (keys 60, 67, 72, 0.15 s each, from 0.25 s), then key 60 held 1.5–2.5 s. Grains 62.5 ms. Peak −8.3 dBFS. | A short, close room. The tail is down 60 dB within about a second of the last note. |
| `02b-medium.wav` | 8.0 s | Item 2, **medium**: the same at Decay 2 s, Size 50% (the defaults). Peak −9.4 dBFS. | A mid-size room; the tail lasts about 3 s. |
| `02c-hall.wav` | 14.0 s | Item 2, **long hall**: the same at Decay 8 s, Size 90%. Peak −9.1 dBFS. | A long hall; the tail rings to the end of the file. In all three, the tail is smooth: no metallic ringing, no flutter, no grain. The stabs show it most. |
| `03a-modulation-0.wav` | 10.0 s | Item 3, **Modulation 0**. A chord (keys 48, 55 and 64 over a 220 Hz sine, Density 6.0, 64 Hz) held 0.25–1.75 s into Amount 60%, Decay **∞**, Size 70%. After the chord the frozen tail holds for 8 s. Peak −6.8 dBFS. | The tail holds level (about −22 dBFS RMS from 2.5 s to the end). Does it ring, with fixed resonances that buzz or whistle? |
| `03b-modulation-25.wav` | 10.0 s | Item 3, **Modulation 25%** (the default). Peak −6.8 dBFS. | Less ringing than `03a`, with no audible pitch wobble. |
| `03c-modulation-100.wav` | 10.0 s | Item 3, **Modulation 100%**. Peak −8.8 dBFS. | The ringing gone. Is the pitch now seasick, the chord's tones wavering? |
| `04-fx-infinite-freeze.wav` | 12.0 s | Item 4. The FX over a stereo 110 Hz–1760 Hz sweep, 3 s long, Mix 1, Density 6.0, Amount 50%. Key 60 held 0.5–4.0 s freezes the input at 0.5 s. Decay 2 s, then **∞ from 1.5 s**, then **2 s again at 8.0 s**. Peak −7.2 dBFS. | The pad builds while the note is held, then hangs on its own from 4 s to 8 s (about −17.5 dBFS RMS) and stays clean. From 8 s it fades out within about 2 s. **Listen at 1.5 s and 8.0 s** for the switch into and out of ∞ (see the checklist). |
| `05-fx-dry-send.wav` | 8.0 s | Item 5. The FX over `loop-48k.wav` (noise bursts every 250 ms, 4 s), **Mix 0**, Amount 80%. Dry to Reverb 0 until 1.0 s, then 100%. Peak −2.9 dBFS (a dry burst). | Before 1.0 s, the dry input alone, silent between bursts. After it, the same bursts in a room: the gaps fill at about −36 dBFS RMS, and the room rings on after the input stops at 4 s. No grains anywhere. |
| `06-fx-automate-predelay-size.wav` | 11.0 s | Item 6. The FX over a 330 Hz sine (amplitude 0.4, 10 s), Mix 0, Dry to Reverb 100%, Amount 70%: the sine plus its reverb, the worst case for both artefacts. **Pre-delay** moves each second from 1 s: 100, 300, 50, 500, 0 ms. **Size** moves each second from 6 s: 80, 20, 60, 40%. Peak −3.2 dBFS. | 1–6 s: each Pre-delay move glides like tape, with no click. A glide bends the reverb's pitch: down an octave while the delay grows, up a fifth while it shrinks, for as long as it moves (up to 1 s, for 0 to 500 ms). 6–10 s: each Size move glitches. **How bad is it?** (see the checklist). |
| `07-defaults.wav` | 10.0 s | Item 7. Key 60 held 0.25–2.25 s dry, then the same note held 4.0–6.0 s with Amount at 50% (set at 3.5 s). Every other Space parameter at its default. Peak −8.9 dBFS. | The second note in the room a user gets on first turning Amount up: Decay 2 s, Size 50%, Damping 30%, Pre-delay 0, Modulation 25%. |
| `08-loudness.wav` | 10.0 s | Item 8. **Level 0 dB** (the default) and a dense cloud, Density 7.0 (128 Hz), on the sweep at amplitude 0.45. Key 60 held 0.25–2.25 s dry, then held 4.0–6.0 s with **Amount 100%** (set at 3.5 s). Peak −1.7 dBFS. | The first note is the default loudness without reverb (peak −3.7 dBFS, RMS −14.6 dBFS over 0.5–2.25 s), already past the wet soft limiter's knee as in sub-project 3. The second adds the full return (peak −1.7 dBFS, RMS −13.3 dBFS over 4.5–6 s). Does the sum sound squashed, or the jump too loud? |

`08`'s sample is at 0.45, not sub-project 3's 0.5, so the file stays under −1 dBFS: at 0.5 its
peak was −0.95 dBFS.

## Checklist (spec §9)

- [ ] **1. Diffusion (`01`).** Swept 0 → 100% on a sparse cloud, the grains blur into a texture
  without sounding like a reverb or a chorus.
- [ ] **2. Rooms (`02a`–`02c`).** Amount at three Decay/Size settings runs from a small room to a
  long hall. The tail sounds smooth, not metallic or grainy.
- [ ] **3. Modulation (`03a`–`03c`).** On a long frozen tail, does 25% stop the ringing without a
  seasick pitch wobble? Is 100% too much? Is 25% the right default?
- [ ] **4. ∞ (`04`).** With a frozen FX note, the pad hangs and stays clean; bringing Decay down
  fades it. At ∞ the highs still fade, by Damping (30% here); the lows hold, and the mids fade
  far more slowly than the highs (ruling P2). Is ∞ too dark?
  - **Entering ∞ may make a small step.** The render shows a faint broadband transient about
    30 ms after Decay reaches ∞ (1.5 s), and another about 70 ms after it leaves (8.0 s): its
    energy above 2 kHz peaks near −71 dBFS (−65 dBFS on leaving), about 45 dB below the pad and
    30 dB above the hiss floor before it. At ∞ that energy keeps circulating, a hiss near
    −90 dBFS above 2 kHz whose highs fade over a few seconds (the spectrogram's grey wedge from
    1.5 s). Can you hear either switch?
- [ ] **5. Dry to Reverb (`05`).** With Mix 0 the input sits in the room, and the grains stay out
  of it.
- [ ] **6. Automating Pre-delay and Size (`06`).** Pre-delay glides like tape, with no click. It
  leaves a faint broadband haze while it moves (about −90 dBFS); is it audible? Size clicks or
  glitches: that is accepted (decision S8, "set, don't sweep"). On the golden case's click
  detector a Size move scores 31–228 against a click limit of 30, and in `06` each move leaves
  about 0.75 s of broadband hash as the reverb rings the glitch out. **Judge how bad it is,** for
  the carry-over row: tolerable for a parameter you set, or does Size need smoothing (or a
  crossfade) after all?
- [ ] **7. Defaults (`07`).** The five defaults behind Amount (Decay 2 s, Size 50%, Damping
  30%, Pre-delay 0, Modulation 25%) sound right when Amount is first turned up. They affect
  nothing while Amount is 0, so this is what a user hears first. Change any? **Your call.**
- [ ] **8. Loudness (`08`).** Amount at 100% with a dense cloud at the default Level: the
  grains go through the wet soft limiter, and the return through a soft limiter of its own,
  added after. Loud but not squashed? And the instrument's default-loudness row in
  `docs/m1-carry-over.md` (closed: the default Level stays), heard again with reverb added:
  still right? It reopens only if you hear a problem.
- [ ] **Decay's scale.** Throughout, does the label match what you hear well enough? It reads
  short of the measured decay: labels 1, 2 and 4 s ring for T60s of 1.72, 2.89 and 5.09 s.
  The label is Surge's own; recalibrating would be a constant offset in the mapping. **Your
  call:** keep Surge's scale, or recalibrate.

## Host checklist

Do each item in **Ableton Live 12** and in **Bitwig Studio**. Use CLAP in Bitwig, and VST3 and
AU in Live. Note the host and format of anything that fails.

**First, install the build under review.** The AU in `~/Library/Audio/Plug-Ins/Components` is a
branch build, installed during validation. Replace it, and the CLAP and VST3, with the release
build of the commit you are judging (CLAUDE.md, "Validators", shows the AU's install).

- [ ] **The "Space" page in Bitwig.** Both products show a second remote-controls page,
  "Space", after "Cloud": Diffusion, Reverb Amount, Reverb Decay, Reverb Size, Reverb Damping,
  Reverb Pre-delay, Reverb Modulation and Dry to Reverb. In the instrument the last slot is
  empty, as Mix's is on the Cloud page. Each knob moves its parameter, and Decay's top shows
  "∞".
- [ ] **Automating Amount and Decay in Live.** Draw automation for Reverb Amount and Reverb
  Decay, Decay running into ∞ and back. Amount moves smoothly, with no zipper. Decay changes the
  tail's length without clicks; listen for the small step when it enters ∞.
- [ ] **The tail after the clip stops.** With a long Decay (or ∞), stop a clip, or play a short
  one, and let the reverb ring. In both hosts the tail goes on after the clip ends, and nothing
  cuts it off (ruling R-T7a):
  - **the FX** reports an infinite tail, so a host never suspends it on silence;
  - **the instrument** reports a constant 122.5 s, so a host that suspends silent plugins does so
    at most 122.5 s after the last note, even at ∞;
  - **the AU reports no tail,** because clap-wrapper's AUv2 doesn't support it. Note whether
    Live cuts off the AU's reverb when the clip stops.
