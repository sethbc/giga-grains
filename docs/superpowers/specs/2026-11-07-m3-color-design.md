# M3 sub-project 11: Color effects (drive, bit-crush and a scale-tuned resonator)

| | |
|---|---|
| **Status** | Draft, written 2026-11-07. The director asked that every M3 sub-project proceed in parallel without his input, then moved every gap of the competitor comparison into M3 ("no i think this all needs to happen in m3", 2026-11-07). Every forced choice is **Claude's provisional call** (C1 to C15), each with what it declined; the director reviews them later. **C3 (the drive's level law), C8 (what the resonator's key follows) and C9 (the resonator's level law) are the ones most worth his look** |
| **Program** | `docs/superpowers/specs/2026-11-07-m3-program-design.md` (§3's anti-goals, §9's rules). Its §8 table predates the director's addition of sub-projects 3b and 7 to 11; this sub-project's row is §2 below (gap 9 of the comparison) |
| **Builds on** | Sub-project 1, the Voice slice (`docs/superpowers/specs/2026-11-07-m3-voice-design.md`; V7's headroom), M2 sub-project 4's chain (`docs/superpowers/specs/2026-10-30-m2-extremes-design.md`; INTENT §14 "Extremes and the FX chain"), and sub-project 4's tuning table (`m3-sp4-tuning`, its spec §4.2 and T16) |
| **Branch** | `m3-sp11-color` (from `m3-voice` at `18ac47d`), worktree `~/src/gg-m3-sp11-color` |
| **Parallel with** | Sub-projects 2, 3, 3b, 4, 5 and 7 to 10; §9 says where they meet. **CLAP indices: globals 0x58 to 0x5F, layer indices 0x78 to 0x7B** |
| **Research** | sst-waveshapers at `dd12f31` (read where used: its saturators, rectifiers, folders, LUTs, tables, ADAA and fuzzes); sst-filters' `HalfRateFilter` and `CytomicSVF` at our pin `87a171b`; the competitor notes (the S-4's Deform and Ring, the GR-MEGA's and GR-2's effects) |

## 1. Intent

Color for INTENT §3.4's extreme focus, and texture: **drive** and **bit-crush** on each layer, and
one **resonator bank** tuned to the instance's scale that turns noise and clouds into chords. The
S-4 (Deform; Ring), the GR-1, the GR-MEGA and the GR-2 all have them; we have none. At rest
nothing moves: with every new parameter at its default, all goldens keep their WAV SHA-256, and the
grain-event log never changes at all, since every new stage sits after the grains.

## 2. Scope

**Delivers (the row the program's §8 lacks):** per-layer Drive, Drive Shape, Crush and Downsample
in the layer's task; a global eight-band Resonator with Mix, Root, Follow, Chord, Decay, Damping,
Gain and Channel; sst-waveshapers pinned and its notices; the 2× oversampler; `gg-render`'s names;
eight goldens; `gg-bench --chain`'s two new stages, report-only; the minimal UI placement the frame
tests require; INTENT §14 to §19 and a §23 row. **Exit:** every earlier golden's WAV hash unchanged;
the new goldens pass with their click-injection entries; the resonator's measured frequencies and
decays match their targets (§7); determinism, block-size invariance, the tripwire, RTSan and both
sanitizers clean; `scripts/check.sh` and the validators pass.

**Out:** drive per voice (it needs a buffer per voice: sub-project 9's per-voice sum, or M5's
per-grain filters, could host it; a carry-over row); the rest of gap 9's list (a compressor, an EQ,
chorus, flanger, ring modulation, Deform's input-following noise: after 1.0, as the comparison
ranks them); a 48-band bank; new factory presets (sub-project 5's, §9); a drive after the limiter.

## 3. Forced choices (Claude's provisional calls; the director may overrule each)

- **C1. Where each sits.** Drive then Crush **per layer**, in the layer's task **after the buses'
  sum and before the diffuser**; one **global** resonator bank in the serial tail **after the
  layers' sum and before the delay**, on the wet side alone (the FX's dry path stays clean, ruling
  M1-q). So per layer: buses and filters → drive → crush → diffuser → Level → soft limiter → morph
  weight; then the sum → resonator → delay → (reverb) → limiter → mix. Consequences, all wanted:
  Level trims the drive's output; the soft limiter and then the global limiter catch what drive and
  resonance add; Feedback's history hears the colored layer (a crushed loop degrades each pass, as
  tape does; bounded by the soft limiter); the diffuser smears the distortion; the delay repeats
  the resonated chord and the reverb sustains it (the S-4's order: filter and Ring before Space).
  *Declined:* one global drive (no layer identity; after the limiter it would break the Ceiling);
  drive per bus (twice the cost; the buses already split timbre by filter); drive after Level or
  the soft limiter (Level would no longer trim it, and the limiter would sit inside the
  saturation); a resonator per layer (four times the cost, four chords at once a niche); the
  resonator after the delay or the reverb (it would retune the echoes and the room, and lose the
  chord's attack under the reverb).
- **C2. Drive from sst-waveshapers,** GPL-3.0-or-later, pinned at **`dd12f31`**
  (`dd12f31a5a9016c9895e52d1a00eee0e1eebe6ce`, upstream `main` on 2026-11-07 and the pin of both
  shortcircuit-xt and Surge; shortcircuit-xt pairs it with our sst-basic-blocks `9cee763`). **Eight curated
  shapes**, called directly from the headers that define them (never `GetQuadWaveshaper` or
  `QuadWaveshaper_Impl.h`, which reach every shape, and never `Fuzzes.h`):

  | Drive Shape | Value | sst function (header) | Notes |
  |---|---|---|---|
  | Soft (default) | 0 | `TANH` (`Saturators.h`) | rational tanh, clipped to ±1 |
  | Medium | 1 | `ZAMSAT` (`Saturators.h`) | |
  | Hard | 2 | `CLIP` (`Saturators.h`) | |
  | Asymmetric | 3 | `ASYM_SSE2` (`Saturators.h`) | LUT in `globalWaveshaperTables` |
  | OJD | 4 | `OJD` (`Saturators.h`) | asymmetric piecewise |
  | Rectify | 5 | `ADAA_FULL_WAVE` (`Rectifiers.h`) | antiderivative-antialiased, an octave up |
  | Fold | 6 | `LINFOLD_SSE2` (`Wavefolders.h`) | linear fold |
  | Sine Fold | 7 | `SINUS_SSE2<true>` (`Effects.h`) | LUT in `globalWaveshaperTables` |

  *Declined:* sst-effects (no drive unit at our pin `987b39d`; Surge's Distortion lives in Surge);
  in-house shapes (a fork of tested code); the library's whole list of 45: the five fuzzes draw
  from `std::uniform_real_distribution` (a non-negotiable forbids `std::` distributions), the
  ADAA folders and the trigonometric tables build function-local statics on their first call, and a
  45-item menu isn't musical by default.
- **C3. The drive's law** (most worth the director's look). Drive is 0 to 100%, **0 Off**. With d
  its ramped value, the shaper's input gain is `g = 10^(36d/20)` (0 to +36 dB) and its output is
  **level-matched at −12 dBFS**: `comp(shape, d) = a / max(p, a/4)`, `a = 0.25`,
  `p = max_{j = ±1..±8} |S(g · a · j/8)|` (S the shape, evaluated from a fresh state, both signs
  for the asymmetric shapes), so a −12 dBFS signal's peak leaves about where it entered and comp
  never exceeds +12 dB. Below 5% an **entry blend** `e = min(1, d / 0.05)` mixes the dry signal
  into the driven one at the base rate, `out = x + e·(D(x) − x)` (D the oversampled, shaped and
  DC-blocked path, §5.2), so Drive rising from 0 is continuous for every shape, Rectify and the
  folds included, and Drive at 0 is exactly the input; inside the bottom 5% the dry signal and D's
  IIR phase comb slightly at the top of the band, which is accepted. A Drive Shape change
  crossfades over 10 ms (BusFilter's rule, §5.2). A 5 Hz DC blocker follows the shaper, since
  Asymmetric, OJD and Rectify make DC. *Declined:* an output or trim parameter (the layer's Level
  is the trim); compensation `1/g` (unity small-signal gain, so high drive gets quieter, not
  denser); no compensation (the level would climb with the knob until the shape saturates); the
  blend in the oversampled domain (at Drive 0 the path would still be the half-bands' phase, and
  stopping it would step).
- **C4. 2× oversampling, always, while the drive runs; latency stays 0.** An in-house, sample-by-
  sample port of sst-filters' `HalfRateFilter` (polyphase allpass IIR, **order 8 steep**: 69 dB
  rejection, transition band 0.01; its four a and four b coefficients copied as literals from
  `HalfRateFilter.h` at `87a171b`), for the upsampler and the decimator. An IIR half-band reports
  no latency: its group delay is a filter's phase, as the bus filters' is. 2× moves the first
  folds of the shaper's harmonics above the band; the hard shapes' harmonics past 3·fs/2 still fold
  in, attenuated by their own roll-off, so the gain is measured and pinned (§7), not promised.
  *Declined:* no oversampling (Hard, the folds and Rectify alias audibly on pitched Voice layers);
  4× (two cascaded stages and twice the shaper calls; the alternative the director may prefer if
  the gate hears aliasing); the library's own block functions (their loops step by 4
  and 8 samples, and our pieces are any length down to 1 sample); M2's 55-tap FIR half-band (27
  samples of latency each way, which would have to be reported).
- **C5. Crush:** **Crush** (0 to 100%, 0 Off) is a bit depth, continuous from 16 bits to 1:
  `b = 16 − 15c`, step `q = 2^(1−b)`, mid-tread rounding `q · floor(x/q + 0.5)`, no dither.
  **Downsample** (0 to 100%, 0 Off) is a sample-and-hold at an **absolute** rate, log from 48 kHz
  down to 100 Hz, `r = 48000 · (100/48000)^c`, never above the host rate, with no anti-alias
  filter: the alias is the effect. Bits then hold (they commute: the hold only picks samples).
  *Declined:* dither (noise that defeats the character); a rate relative to the host's (the sound
  would change between 44.1 and 96 kHz); one combined Crush knob (the two are different sounds).
- **C6. The resonator bank is in-house:** 8 resonators per channel, each a **TPT state-variable
  band-pass** (Cytomic's equations, the ones `CytomicSVF` implements, without its resonance clamp
  at 0.98, which caps Q near 25 and so decays near 0.3 s at 200 Hz), its damping set from a T60.
  *Declined:* `CytomicSVF` itself (that clamp); sst-effects (no resonator at our pin); direct-form
  modal biquads (float coefficients near z = 1 at long decays and low frequencies); 48 bands (the
  S-4's; six times the cost, and a chord needs no more than 8 tones); Karplus-Strong lines (each
  pitch's tuning then rests on a fractional delay and an allpass, and the lines need far more state).
- **C7. Tuning.** A **Chord** shape is a list of 12-TET intervals, **snapped to the nearest degree
  of the effective tuning** (sub-project 4's T16 degree set: the sorted, distinct `keySemitones` of
  the mapped keys; in 12-TET the integers, so snapping is the identity); **Scale** and **Scale
  Thirds** walk the tuning's degrees from the root (every degree, every second); **Harmonic** is
  exact ratios 1 to 8 over the root, unsnapped (a string). 12-TET when no tuning is loaded.
  *Declined:* shapes in degrees alone (in 12-TET every degree is a semitone, so a "triad in
  degrees" is a cluster); unsnapped 12-TET chords under a Scala tuning (out of key).
- **C8. What the key follows** (most worth the director's look). **Resonator Follow**: **Fixed**
  (the default: **Resonator Root**, a key, default 48, C3), **Last Note** (the newest held note's
  tuned pitch, its slides and tuning expression included, latched when it is released) or **Held
  Notes** (one resonator per held key, ascending, the rest filled with those keys an octave up, and
  so on; Chord is ignored). **Resonator Channel** (All, 1 to 16, Lower zone, Upper zone) chooses
  which notes it follows, with sub-project 2's channel rule (E16). That is what lets the FX follow
  a keyboard without its Cloud layers freezing on the same notes (D4): layers on channel 1 (E16's
  Note Channel), the resonator on channel 2, or a LinnStrument's upper zone. *Declined:* always
  following the notes (the FX would freeze its Cloud layers whenever the resonator is played);
  following only a root, never a chord (Held Notes is how a keyboard plays a resonator).
- **C9. The resonator's level law** (most worth the director's look). Each resonator's output is
  its normalized band (peak gain 1) times `m_i = min(8, √(fs / (8π·B_i)))`, `B_i` its −3 dB
  bandwidth in Hz (`ln 1000 / (π·T60_i)`: 1.1 Hz at 2 s): **unity noise power for the bank**
  while that stays under +18 dB of peak gain, capped there (at 48 kHz the cap binds from a decay
  of about 74 ms). Then **Resonator Gain** (−12 to +24 dB, default 0), then M1's `softLimit` on
  the bank's sum, then **Resonator Mix**, a linear crossfade from the wet signal to the bank. The
  physics: whatever the law, a tone on a resonance gains `√(fs/(πB))` more than broadband noise
  does (41 dB at a 2 s decay). The cap keeps a tone at a chord tone, the common case in a resonator
  tuned to the music's key, from slamming the limiter; the price is that long decays on noise are
  quiet (about −14 dB at 2 s, −26 dB at 32 s), which Gain restores. *Declined:* peak gain 1 (a
  2 s bank on noise some 32 dB down); unity noise power uncapped (+32 dB on a tone at a resonance
  at 2 s, saturating hard); an automatic gain control (signal-adaptive state, harder to reason
  about at the gate).
- **C10. Off is the identity, by gating each stage.** Drive, Crush and Downsample each run only
  while their ramp's target or value isn't 0; the resonator only while Mix's is. A stage that
  returns to 0 stops at the sample its ramp reaches 0 and clears its state, so the next start is
  from rest. At rest no stage runs, no state is touched and no sample is rewritten: bit for bit,
  the sign of zero included, and no cost. What the resonator follows (C8) is updated at every
  cell whether the bank runs or not, so its tuning never depends on when Mix was raised.
  *Declined:* stages that always run (cost at rest, for warm state nobody hears).
- **C11. Mutate.** Locked by default (structural, as Delay Amount is): each layer's Drive, Crush and
  Downsample, and Resonator Mix, Follow, Root and Channel; a press never switches a stage on or
  moves the key. Drive Shape, Chord, Decay, Damping and Gain move. *Declined:* every one moving.
- **C12. Route targets.** Resonator Follow and Resonator Channel are no route target (they configure
  the note input, as Mode and E16's routing do); every other new parameter is, the stepped ones
  included (a Step sequence on Resonator Root plays a progression; one on Drive Shape morphs
  through its crossfades). None is a per-voice target: on a Voice layer a per-voice source routed
  to Drive reads the newest voice's value (B4), so pressure drives the layer, not a single note.
- **C13. Cross-machine last bits.** Soft and Asymmetric's paths use SSE's `rcp_ps` and libm-built
  tables (`globalWaveshaperTables`, built at static initialization with `std::tanh`, `std::exp`
  and `std::sin`), whose last bits differ between CPU vendors and between x86 and simde's NEON.
  Accepted, as Reverb 2's `powf` and pffft's flavors are (INTENT §14, Determinism): one machine
  renders identically every time and at every block size, the goldens' metric tolerances cover the
  rest, and the event log never sees audio. *Declined:* a patched copy using exact division (a
  fork for bits nobody hears; principle 8's "sounds identical" holds).
- **C14. The UI: the minimum the frame tests require.** A new **Color** panel in the Edit view (the
  layer's four) and a **Resonator** panel in the FX view (its eight), appended to `Panel`; three
  remote-controls pages appended, "Color A/B", "Color C/D" and "Resonator". Sub-project 5 polishes.
- **C15. CPU.** The color stages are fixed costs, outside the grain Budget, as the bus filters and
  the Spectral STFT are; nothing adapts to load. `gg-bench --chain` times them, report-only (§6).

## 4. Parameters (append-only CLAP IDs)

Eight globals in **scope 0, 0x58 to 0x5F** (inside M2 sub-project 3's reserved block), appended to
`kParamIds` after `BendRange`; four layer parameters at **0x78 to 0x7B** in every layer scope
(inside M2 sub-project 4's block), in a new list `GG_LAYER_PARAMS_M3_COLOR` appended after
`GG_LAYER_PARAMS_M2_EXTREMES` in both `GG_LAYER_PARAMS` and `GG_LAYER_PARAMS_SINCE_M1` (the index
column stays ascending). Values are in the CLAP domain `gg-render` takes.

| Slot | CLAP ID | Display | Range, default | Kind |
|---|---|---|---|---|
| `Drive`, `B_Drive` … | `0x010078` … `0x040078` | Drive | 0–1 (shown %), **0** | layer, automatable, ramped 10 ms, route target |
| `DriveShape` | `0x..79` | Drive Shape | 0–7 stepped (C2's table), **0 Soft** | layer, automatable, route target; changes crossfade 10 ms |
| `Crush` | `0x..7A` | Crush | 0–1, **0** (shown "Off", else bits: "16.0 bits" … "1.0 bits") | layer, automatable, ramped, route target |
| `Downsample` | `0x..7B` | Downsample | 0–1, **0** (shown "Off", else Hz: "48.0 kHz" … "100 Hz") | as Crush |
| `ResonatorMix` | `0x000058` | Resonator Mix | 0–1 (%), **0** | global, automatable, ramped 10 ms, route target |
| `ResonatorRoot` | `0x000059` | Resonator Root | 0–127 stepped (a key, 60 = C4), **48** | global, automatable, route target |
| `ResonatorFollow` | `0x00005A` | Resonator Follow | 0 Fixed, 1 Last Note, 2 Held Notes; **0** | global, automatable, **no route target** |
| `ResonatorChord` | `0x00005B` | Resonator Chord | 0–8 stepped (§5.3), **1 Fifths** | global, automatable, route target |
| `ResonatorDecay` | `0x00005C` | Resonator Decay | log2 s, −4 to 5 (62.5 ms to 32 s), **1.0 (2 s)** | global, automatable, route target |
| `ResonatorDamping` | `0x00005D` | Resonator Damping | 0–1 (%), **0.25** | global, automatable, route target |
| `ResonatorGain` | `0x00005E` | Resonator Gain | −12 to +24 dB, **0** | global, automatable, ramped, route target |
| `ResonatorChannel` | `0x00005F` | Resonator Channel | 0–18 stepped: 0 All, 1–16, 17 Lower zone, 18 Upper zone; **0** | global, automatable, **no route target** |

- **Counts on this branch:** 24 parameters, `kParamCount` 290 → 314, `kLayerParamCount` 63 → 67.
  `kNextFreeLayerIndex` and `kNextFreeGlobalIndex` don't change (every index is inside a reserved
  block). `isReservedIndexInUse` gains scope 0's 0x58–0x5F and the layer scopes' 0x78–0x7B, and its
  static_assert then requires all twelve indices used, as they are. A merge with another branch
  adds the counts and unions the reserved-index conditions.
- **Flags** follow their neighbours': the continuous ones as Smear and Delay Amount; the stepped ones
  `CLAP_PARAM_IS_STEPPED` as Bus 1 Filter. `isValidTarget` refuses `ResonatorFollow` and
  `ResonatorChannel` (C12).
- **No patch-version bump:** a state or preset without them reads their defaults (the voice spec's
  rule, §4.1). A preset saves all 24 (they are the sound, Resonator Channel included, as E16's
  Note Channel is); a preset load replaces them.
- **Mutate** (C11): `structuralLocks` gains each layer's Drive, Crush and Downsample, and Resonator
  Mix, Follow, Root and Channel. Mutate is keyed by CLAP ID since sub-project 1, so no other slot's
  draw moves; `--mutate-preset` outputs gain the moved new slots (Drive Shape, Chord, Decay,
  Damping, Gain), and the `render_mutate_preset*` expectations that pin bytes update in that
  commit, which says so. No audio golden moves (the moved slots' stages are off).
- Every hard-coded count follows, as sub-project 1 listed (`tests/support/ParamTable.h`,
  `patch_tests`, `identity_tests`, `params_tests`, `rt_tests`, `EditLayout` and its tests,
  Mutate's slot-count comment).

## 5. Design

### 5.1 The chain (INTENT §14's, new stages marked)

```
per layer L (A..D), one task:
   b1_L, b2_L = Σ grains_L by bus, each in spawn order
   w_L        = F1_L(b1_L) ⊕ F2_L(b2_L)
   w_L        = drive_L(w_L)       while Drive's ramp ≠ 0 (else untouched)                  new
   w_L        = crush_L(w_L)       bits while Crush's ramp ≠ 0, then hold while Downsample's new
   w'_L       = δ_L > 0 ? w_L + δ_L·(diffuse_L(w_L) − w_L) : w_L
   limited_L  = softLimit(w'_L · g_L)          (→ Feedback's history, as before)
   wet_L      = limited_L · weight_L(t)
instance, after the tasks join:
   wet   = wet_A ⊕ wet_B ⊕ wet_C ⊕ wet_D        (a mono render duplicated to both channels)
   wet   = wet + mix·(bank(wet) − wet)          only while Mix's ramp ≠ 0                    new
   wet'  = wet [+ Amount · delayRead]
   then the reverb, the limiter and the mix, as before
```

Both channels are processed even when one output channel renders (the layer's mono render is
already duplicated where the stage reads it).

### 5.2 Drive (`core/src/fx/Drive.h`, `Drive.cpp`; one per layer)

- **The only translation unit that includes sst-waveshapers** is `Drive.cpp` (as only
  `core/src/space/` includes sst-effects): `Saturators.h`, `Rectifiers.h`, `Wavefolders.h` and
  `Effects.h`, nothing else. `Processor.h` holds a `std::unique_ptr<fx::Drive>` per layer, never an
  sst header. Compiled in gg-core's sources, so `-ffp-contract=off` and `-Werror` apply to our
  code and the headers are SYSTEM.
- **Per sample, while active** (x the stereo sample, d Drive's ramped value):
  1. `u0, u1 = up2(x)` (the half-band's two outputs for this input sample, per channel);
  2. for each of u0 then u1 (in that order, so ADAA's history runs in time order): the shaper on
     lanes (L, R, 0, 0) with `drive = g(d)`, times `comp(shape, d)`: `v = S(u)·comp`; during a
     shape fade, `v = (1 − f)·v_old + f·v_new`, f the fade's linear value, each shape on its own
     `QuadWaveshaperState`;
  3. `D = DC(down2(v0, v1))`, the DC blocker `y_n = x_n − x_{n−1} + R·y_{n−1}`,
     `R = 1 − 2π·5 / fs` (computed once in `prepare`, plain arithmetic);
  4. `out = x + e·(D − x)`, `e = min(1, d / 0.05)` (C3's entry blend, at the base rate).

  `g(d) = exp2Det(d · 36 / 6.020599913279624)` per sample (double, then float). Every curated
  shape's output is bounded (the LUT shapes clamp their index; the others are within ±1, except
  Asymmetric, whose table runs to about −23 at its negative end), so the stage's output is finite,
  and the soft limiter after Level bounds the layer as before.
- **`comp`** is a table of 8 shapes × 129 values of d (0, 1/128, …, 1), built once in the
  `Processor`'s constructor by evaluating C3's formula with fresh states (lanes initialized), and
  read per sample by linear interpolation in d. Building it also **calls every curated shape once
  on the main thread**, which initializes `ADAA`'s function-local constants before the audio
  thread can (RTSan would otherwise see the guard on a first call).
- **The half-band** (`core/src/fx/HalfBand2x.h`): `HalfRateFilter`'s order-8 steep allpass
  cascades, ported to one sample in (up) or two samples in (down) per call, the same operations in
  the same order as the library's loops, stereo. A test checks it against the library bit for bit
  over blocks whose lengths are multiples of 8 (§7).
- **Shape changes** (Drive Shape's effective value, read at every grid boundary): as `BusFilter`
  does its type: the incoming shape starts from rest on the spare state and crossfades over the
  10 ms ramp; a change back during the fade turns it from its current value; a third shape waits
  for the fade to end. The first cell after a clear takes the shape at once.
- **Gating** (C10): active from the sample Drive's ramp leaves 0; stopped where it reaches 0 with
  target 0, at which point `e` is 0, so the last processed sample already equals x; the
  half-band, shaper and DC states are then zeroed. Cleared too by `reset()`, the enable gate and a
  layer's ramp to off (where the diffuser and Feedback history are cleared today).

### 5.3 Crush (`core/src/fx/Crush.h`; per layer, in-house, no library)

- **Bits**, while Crush's ramp c ≠ 0: `b = 16 − 15c`, `q = (float) exp2Det(1 − b)`,
  `x ← q · floor(x/q + 0.5)` per channel (`std::floor` is exact on every platform).
- **Hold**, while Downsample's ramp c ≠ 0: `ρ = min(1, 48000 · exp2Det(c · log2(100/48000)) / fs)`
  in double; per sample `φ += ρ`; if `φ ≥ 1`, `φ −= 1` and latch x; output the latch. Started with
  `φ = 1`, so the first sample latches. At c just above 0 and fs ≤ 48 kHz, ρ is 1 and every sample
  latches: continuous with Off.
- Gated, stopped and cleared as Drive is (§5.2).

### 5.4 The resonator (`core/src/fx/Resonator.h`, `Resonator.cpp`; one per instance)

- **Pitches, at every grid boundary**, in semitones from C4 (`keySemitones`' convention;
  `kC4Hz` = 261.6255653005986, sub-project 4's), from the root, the Chord and Follow:
  - `κ(k)` is key k's key term: the effective tuning's `keySemitones[k]` (12-TET: k − 60);
    `snap(s)` the nearest degree of T16's set (ties to the lower); `ε(n)` a note's expression part,
    `notePitch(n) − κ(n.key)` (bend and tuning expression; sub-project 4's helper, sub-project 1's
    expression until it merges).
  - **Fixed:** root key r = Resonator Root, `ε = 0`. **Last Note:** r and ε from the newest held
    (or pedal-held) note passing Resonator Channel; with none held, the last such note's, latched;
    none since `reset()`: Fixed's. Chord shapes (C7, in semitones over the root):

    | Chord | Value | Offsets |
    |---|---|---|
    | Harmonic | 0 | ratios 1–8, exact: `12·log2(j)`, j = 1..8, **unsnapped** |
    | Fifths (default) | 1 | 0, 7, 12, 19, 24, 31, 36, 43 |
    | Major | 2 | 0, 4, 7, 12, 16, 19, 24, 28 |
    | Minor | 3 | 0, 3, 7, 12, 15, 19, 24, 27 |
    | Major 7 | 4 | 0, 4, 7, 11, 12, 16, 19, 23 |
    | Minor 7 | 5 | 0, 3, 7, 10, 12, 15, 19, 22 |
    | Sus 4 | 6 | 0, 5, 7, 12, 17, 19, 24, 29 |
    | Scale | 7 | degrees j0 … j0 + 7 |
    | Scale Thirds | 8 | degrees j0, j0 + 2, … j0 + 14 |

    `s_i = snap(κ(r) + off_i) + ε` for the interval shapes, `κ(r) + 12·log2(i + 1) + ε` for
    Harmonic, `degree[j0 + step_i] + ε` for the scale shapes, j0 the index of `snap(κ(r))`; a step
    past the set's top makes that resonator silent. The Harmonic offsets are a constant table of
    doubles computed once (constexpr or at construction), not libm on the audio thread.
  - **Held Notes:** the distinct keys among the held (and pedal-held) notes passing Resonator
    Channel, ascending, the lowest 8, each with its newest note n: `s = κ(k) + ε(n)`; with m of
    them, resonator i ≥ m takes resonator `i mod m`'s pitch plus `12·floor(i/m)`. None held: the
    last set, latched (none since `reset()`: Fixed's).
  - The channel test is sub-project 2's E16 rule (All passes every channel; 1–16 that channel; a
    zone the channels the MPE layout gives it). Until sub-project 2 merges, the zones pass nothing,
    as E16's do with MPE Off. Unmapped keys (sub-project 4's T7) never enter the note table.
  - This tracking runs at every cell, active or not (C10).
- **Frequencies and coefficients**, from s_i: `f_i = kC4Hz · exp2Det(s_i/12)` (double). A resonator
  outside [20 Hz, 0.45·fs] is **silent**: its output gain is 0 and its coefficients clamp to the
  range. `T60 = exp2Det(Decay)`, `T60_i = T60 · min(1, f_0/f_i)^(2·Damping)` (resonator 0 is the
  root or the lowest held key; the power through `exp2Det`/`logDet`), `Q_i = max(0.5, π f_i T60_i /
  ln 1000)`, `k_i = 1/Q_i`, `B_i = f_i·k_i`, `m_i = min(8, √(fs/(8π·B_i)))`, `g_i =
  fasttan(π f_i / fs)` (sst-basic-blocks', as BusFilter's coefficients use), `a1 = 1/(1 + g(g + k))`,
  `a2 = g·a1`, `a3 = g·a2`, and the output weight `c_i = m_i·k_i`. All float after `f_i`.
- **Per sample, while active:** each of the 16 band-passes (8 per channel) runs Cytomic's update
  `v3 = x − ic2; v1 = a1·ic1 + a2·v3; v2 = ic2 + a2·ic1 + a3·v3; ic1 = 2v1 − ic1; ic2 = 2v2 −
  ic2`, with `(a1, a2, a3, c)` interpolated linearly across the 32-sample cell from the last cell's
  to this one's (BusFilter's `setCoeffForBlock<32>` rule), so a retune, a new chord or a new note
  glides over 0.67 ms. `bank = softLimit(G · Σ_{i=0..7} c_i·v1_i)` per channel, summed in i order,
  G Resonator Gain's ramped linear value. Then `wet ← wet + mix·(bank − wet)`.
- **Gating** (C10): active from the sample Mix's ramp leaves 0, stopped and zeroed where it reaches
  0 with target 0; zeroed by `reset()`. The first cell after a clear takes its coefficients at once
  (no glide from rest).
- SIMD is the builder's choice (4 resonators to a vector is natural); every operation is IEEE `+`,
  `−`, `×`, `÷` with contraction off, so the bank is expected bit-exact across platforms, as the
  bus filters are.

### 5.5 Headroom, V7 and the limiter

- The drive's input is the layer's grain sum after the buses: Density's normalization and V7's
  `1/√n` keep it near one stream's level, so −12 dBFS is a fair reference and **a chord drives
  like one note** (V7), with the intermodulation a summed drive gives (accepted: per-voice drive is
  out, §2).
- After the drive, the layer's Level trims, the soft limiter bounds each layer below 1, and the
  global limiter (if on) holds the Ceiling after the reverb. The resonator's sum has its own
  `softLimit`. So nothing new can exceed what the chain bounded before.
- **Feedback with Drive** can self-sustain (a quiet loop gains up to `g·comp`); the soft limiter
  bounds it below 1 and the sweep's runaway check (the last second no more than 6 dB over the
  loudest earlier one) holds, since saturation doesn't grow.
- **Latency** stays 0 and the instrument's tail stays 132.5 s: the longest Decay, 32 s, is inside
  it, and the delay at 100% feedback is already the tail's case.

### 5.6 Determinism and real time

- Every stage is a function of the sample clock, the effective values on the grid and per-sample
  state: no draw, no RNG field, no load-dependent behavior. Ramps retarget at grid boundaries, so
  each stage starts and stops at the same sample at any block size, with any thread pool (the
  drive and crush run in the layer's task; the resonator in the serial tail).
- 0- and 1-sample blocks: the half-band and every state run per sample.
- No allocation: every unit and the comp table are made in the constructor; `globalWaveshaperTables`
  is built at static initialization (load time); the prewarm (§5.2) leaves no first-call static.
  `params.flush` is unchanged.
- The event log is untouched by every stage, at rest or not.

### 5.7 `gg-render`

`params` (FX and instrument; sample mode rejects them as unknown keys, as today) takes `"A Drive"`
… `"D Downsample"` (generated from the layer list) and the eight globals by their display names.
Nothing else changes; `automation` takes them all (none is a ring parameter).

### 5.8 Goldens (new; 6 s, block 256 unless noted, seed 1; each catches the 16 injected steps or joins the click-injection table with its reason)

- `inst-drive-sweep`: one held note over a 220 Hz sine, Drive 0, 0.25, 0.5, 0.75, 1 at 1 s
  intervals, Soft (the entry blend and comp).
- `fx-drive-shapes`: the FX over a 50–700 Hz sweep, Mix 1, Drive 0.6, Drive Shape stepped 0 to 7
  every 0.7 s (the shape crossfade).
- `inst-crush`: one note over a 330 Hz sine: Crush 0, 0.4, 0.8 at 1 s intervals, then Crush 0.4
  with Downsample 0.3 and 0.6. If the holds' steps score above the click limit, the case lowers
  them (the sweep's exemption, §7, is for the sweep alone).
- `fx-resonator-chord`: stereo noise, Resonator Mix 1, Fixed, Root 48, Major, Decay 2 s, Damping
  0.25, Gain +12 dB; Chord to Minor 7 at 3 s (the retune glide).
- `inst-resonator-held`: a cloud over a noise sample, Resonator Mix 0.8, Held Notes: C-E-G at
  0.5 s, F-A-C at 3 s.
- `inst-resonator-slide`: layer A at Mode Voice over noise, Last Note, Harmonic, an MPE note whose
  bend slides a fifth (the root follows the slide).
- `inst-voice-drive`: a Voice layer, Drive 0.7, one note for 1.5 s, then a four-note chord at 2 s
  (V7 with drive).
- `fx-color-chain`: the FX over `loop-48k.wav`, four layers: A Drive 0.8 Hard with Crush 0.3, B
  Drive 0.5 Fold, Resonator Mix 0.5, Delay Amount 0.3, Limiter Peak at −3 dB (everything at once).
- **If sub-project 4 has merged when this builds:** `inst-resonator-scale`, a 5-note `.scl`
  through `gg-render`'s `tuning` key, Chord Scale, Held Notes off; otherwise whichever of the two
  merges second adds it.

## 6. CPU (`gg-bench --chain`, report-only)

Two stages join the chain's rounds beside `spectral`, each in ns per stereo sample over stereo
noise at 48 kHz, printed and written as `"drive"` and `"resonator"` in the chain's JSON (settings
listed in `tools/gg-bench/Bench.h`): **drive**, one layer's Drive 0.5 Soft with Crush 0.5 and
Downsample 0.5 (oversampler, shaper, DC, crush, hold); **resonator**, the bank at Fifths, Root 48,
Decay 2 s, Mix 1. No ratio, no baseline, no verdict; `bench_chain_cli` checks the two keys exist.
The figures go in INTENT §16 from an idle run. X (`engine-sample`) is untouched: nothing here is in
the engine.

## 7. Tests (labels `color` and `resonator`, with `determinism`, `rt`, `rtsan` where they apply)

- **At rest:** every existing golden's WAV SHA-256 unchanged (`scripts/update-goldens.sh` before and
  after; the 108 and every M3 golden merged under this branch); a unit test renders with each new
  parameter at its default and compares bytes with the parameter absent from the state.
- **Drive:** Drive 0 bit-exact (the sign of zero); Drive stepped from 0 to 1 by one automation
  point over a sine scores under the click limit (15), and so does every shape change; for a
  −12 dBFS sine at Drive 25, 50 and 100%, Soft's, Medium's, Hard's and OJD's output peak within
  ±0.5 dB of the input's, and every shape's RMS within ±9 dB of the input's (Rectify's octave loses
  its DC); the alias power of a 5 kHz sine at Drive 100%, Soft and Hard, in the bins below 5 kHz
  that aren't the input's, below the same stage's without oversampling by a margin the builder
  measures and pins; Rectify's mean within 1e-3 after 1 s; the half-band port equals
  `HalfRateFilter`'s `process_block_U2` and `process_block_D2` bit for bit over 1,024 samples in
  blocks of 8.
- **Crush:** 4 bits gives exactly 16 levels over [−1, 1]; Downsample at 12 kHz holds each value 4
  samples at 48 kHz; bits-then-hold equals hold-then-bits; Crush and Downsample at 0 are bit-exact.
- **Resonator:** each resonator's measured frequency (an impulse's response, a long FFT, the peak
  refined) within 1 cent of its target for every 12-TET chord at Root 24 to 96, and the worst error
  to 0.45·fs recorded; T60 within 10% of `T60_i` at Decay 0.25, 2 and 16 s and Damping 0, 0.5, 1;
  the noise law (white noise in at −24 dBFS RMS and 48 kHz, Decay 0.0625 s, where the cap doesn't
  bind: the bank
  within ±1.5 dB of its input power; at 2 s, the cap's −14 dB within ±1.5 dB);
  a sine at a resonance, Decay 32 s, Gain +24 dB: the output stays within ±1 and finite; Last Note
  latching and Held Notes' fill (two keys fill 8 by octaves); Resonator Channel against notes on
  two channels; a non-12 table (a `TuningTable` built in the test, no file) snaps Major to its
  nearest degrees; the bank's state is zero after Mix returns to 0; Mix 0 bit-exact.
- **Determinism:** one render with every stage active and Drive Shape, Chord, Root and Follow
  automated, bit-identical at blocks 1, 7, 32, 64, 256 and 1,000, and with no pool and pools of 1, 2
  and 8 workers.
- **Real time:** the allocation tripwire (`rt`) with every stage active and a shape fade running;
  RTSan over the same render (`rtsan`), on a freshly constructed `Processor` (so the prewarm is what
  keeps the first call clean).
- **The sweep** (`tests/sweep/`): four cases: every shape at Drive 100% with Feedback 0.95; Crush 1
  with Downsample 1; the resonator at Decay 32 s, Damping 0, Gain +24 dB over a sine at the root;
  an automation burst of all 24 at grid rate. Finite, the peak bound, no runaway, and the click
  score: if Crush or Downsample at their tops score as clicks (their steps are the effect), an
  `Exemption::Crush` looks away where Crush or Downsample's effective value is at or above the
  measured threshold, pinned like `kLimiterDrive` (provisional, for the director's look at the
  close).
- **Plugin:** the 24 parameters' flags, IDs and names; the three remote pages; the frame tests
  (every parameter reachable, labels unelided at 960 × 600 and 640 × 360, both products, both
  themes); a preset round-trip of all 24.
- `scripts/check.sh` (dev, asan, rtsan), the validators (clap-validator, pluginval, auval, VST3).

## 8. Dependencies, licensing and CI

- `libs/sst/sst-waveshapers` as a submodule at `dd12f31`, `add_subdirectory(... EXCLUDE_FROM_ALL)`
  after sst-filters (its headers include `sst/utilities/globals.h`, which sst-filters provides),
  with `SST_WAVESHAPERS_BUILD_TESTS` and `_EXAMPLES` forced OFF; linked `PRIVATE` into gg-core;
  added to `gg_mark_system_includes`. Header-only; nothing downloaded at configure.
- GPL-3.0-or-later, compatible with AGPL-3.0-or-later. `THIRD_PARTY_NOTICES.md` gains its entry
  (and so the editor's embedded notices, which `notices_tests.cpp` checks byte for byte).
- It is at upstream's head, so `docs/dependency-watch.md` gets no row.

## 9. Where the parallel sub-projects meet this one

- **IDs:** this one takes globals 0x58–0x5F and layer indices 0x78–0x7B; 2 takes 0x25–0x2F and
  0x32–0x37, 3 global 0x30 and layer 0x38 upward, 4 global 0x38 and layer 0x3C. Every branch that
  uses a reserved block edits `isReservedIndexInUse`, `kParamCount`, `kLayerParamCount` and the
  slot lists: the merge unions them. Remote pages and `Panel` values are appended in merge order
  (neither is a stability contract).
- **2 (expression):** Resonator Channel uses E16's channel rule and zones; until 2 merges, the zone
  values pass nothing. B4 decides what a per-voice source reads on Drive (C12). With E16, the FX's
  layers and resonator can listen on different channels (C8).
- **4 (tuning):** the resonator reads the effective `keySemitones`, T16's degree set and
  `notePitch`. Built before 4 merges, it reads a local 12-TET table, the integers −60…67 and sub-
  project 1's pitch expression through the same three accessors; whichever merges second points
  them at 4's (a line each) and adds `inst-resonator-scale`.
- **5 (views and presets):** the Color and Resonator panels are placed, not designed (C14); 5 may
  move them and add factory presets that use color (suggested: a resonated pad and a crushed
  texture), appended, with their preset goldens.
- **9 (voice filter):** its per-voice filter sits inside a layer's grain sum, upstream of w_L, so
  the drive follows it; no state is shared.
- **3, 3b, 7, 8, 10:** no shared code beyond the counts above.

## 10. The builder check (the questions a builder would still ask, answered)

1. *Where exactly in `Processor::renderLayer` does the stage go?* After the bus sum's `w[0] +=
   w2[0]` branch and before `l.diffuser.process`, on `w[0]`, `w[1]`.
2. *Which ramps?* `Processor::Ramp`s like Level's: Drive, Crush, Downsample per layer; Resonator Mix
   and Gain globally; all retargeted in `retargetRamps`, jumped where the others jump (`jumpRamps`,
   `setBase`'s jumps, `jumpMovedRamps`). Decay, Damping, Root, Chord, Follow and the notes reach the
   bank through its per-cell coefficients, not ramps.
3. *Does a loaded state glide in?* No: the ramps jump and the first cell after a clear snaps the
   shape and the bank's coefficients.
4. *Drive Shape is a route target: what does a fractional value mean?* The effective value rounds
   to the nearest step and clamps to 0–7, as Bus 1 Filter's does.
5. *Why is the DC blocker after the decimator, not in the oversampled domain?* It costs half, and DC
   is the same in both.
6. *What reference does comp use for a shape whose output isn't monotonic?* The largest magnitude
   over eight probes up to `g·a` (C3), so the folds are level-matched to their peaks.
7. *Root in Held Notes or Last Note mode?* Ignored once a note has been followed; it is the start.
8. *More than 8 keys held?* The lowest 8.
9. *What is "newest"?* The largest start ordinal in the note table (sub-project 1's).
10. *Does the resonator see layer notes filtered by E16?* No: it applies its own Channel to the
    whole note table, independently of every layer's routing.
11. *Mono instance (one output channel)?* The sum is duplicated before the bank; both channels run.
12. *Is 44.1 kHz different?* Downsample's top is clamped to the host rate; the half-band
    coefficients are rate-free; the DC blocker, the bank's coefficients and the silence range follow
    fs. A unit test runs the resonator's frequency check at 44.1 and 96 kHz too.
13. *Where do the shape names and the Chord names live?* `ParamMap`'s text functions, beside Bus 1
    Filter's.
14. *AU parameter order?* It is ascending CLAP ID, so the new globals sit between Budget (0x50) and
    the delay (0x60); not frozen until `v0.5.0-beta.1` (§18).
15. *What does the event log or the canvas show?* Nothing new: the stages are after the grains.
16. *A layer switched off with Drive on?* Its task stops rendering; the ramp to off clears the
    drive and crush states with the diffuser's, so the layer switched back on starts them from rest.
17. *Does the resonator's own `softLimit` clip unity-power noise?* At −12 dBFS RMS in, the knee
    (0.5) is two sigma, so a few percent of samples bend; the tests' noise law is measured at
    −24 dBFS RMS, where the knee is eight sigma away and the law is the linear bank's.
18. *Can a route push Resonator Decay past 32 s?* No: the effective value is clamped to the
    parameter's range, as every target's is.

## 11. INTENT changes (Part II; one §23 row, the next free number when this merges)

- **§14:** a paragraph **Color** after Extremes (Drive and its shapes, the law, the oversampler, Crush,
  the resonator, gating) and the chain's diagram as §5.1.
- **§15:** sst-waveshapers' row: "pinned at `dd12f31`; the drive's eight shapes (M3 sub-project 11)",
  replacing "drive later", with a bullet as sst-effects' (one translation unit, PRIVATE, no
  fuzzes or `QuadWaveshaper_Impl.h`, the prewarm).
- **§16:** the `--chain` stages' figures (report-only).
- **§17:** the Edit view's Color panel and the FX view's Resonator panel.
- **§18:** the 24 parameters and their IDs; Mutate's new locks; the counts.
- **§19:** the goldens, the sweep cases and any click exemption.
- **§20:** the M3 row names sub-project 11, if the director's addition hasn't already.
- **§23 row:** C1 to C15, dated 2026-11-07, marked as Claude's provisional calls.

**Carry-over rows to add:** drive per voice (after sub-project 9, or M5's per-grain filters); a
denser bank (the S-4's 48 bands) if ears ask; the level law of long decays on noise, for the
gate's listening.

## 12. What's left to the builder

The plan's task order: the submodule, its target and notices; the half-band port with its bit-exact
test; `Drive` and `Crush` with their unit tests; the parameters, counts, text and Mutate locks; the
layer stage in `Processor` (the at-rest proof: `update-goldens.sh` before and after); `Resonator`
and its tracking of notes, with the 12-TET stand-ins of §9; the serial-tail stage; `gg-render` and
the goldens; the sweep cases; the plugin's pages and the minimal panels with the frame tests; the
`--chain` stages; INTENT, CLAUDE.md (the labels, the counts, `--chain`) and the carry-over rows. The
exact golden specs (note times, sample files), the SIMD layout of the bank, and the listening pack
(`docs/listening/m3-color/`, A/B specs rendered for the record) are the builder's.
