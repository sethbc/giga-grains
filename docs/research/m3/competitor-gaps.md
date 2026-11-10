# Giga Grains against the Torso S-4, Tasty Chips GR-1, GR-MEGA and GR-2: a gap analysis

Prepared 2026-11-07 for the director. Read-only research: no repository file was changed.

**What was compared.** The products' own pages and manuals (Torso's online user guide for the S-4;
Tasty Chips' product pages), plus reviews and press where the manufacturer is thin. Our side:
INTENT.md (the `m3-voice` worktree, at `45a4084`), the M2 specs in `docs/superpowers/specs/`, the
M3 program design and the four M3 sub-project specs (voice, expression, pitch-sync, tuning).

**"GR-1 Mega" does not exist.** Tasty Chips makes three granular instruments: the **GR-1** (2017,
now in its last units on sale), the **GR-MEGA** (a larger four-layer "granular workstation",
shown at Superbooth 2023 and shipping since 2024), and the **GR-2** (Kickstarter May 2026, now
shipping; the GR-1's successor, running the GR-MEGA's firmware with fewer controls and voices)
([shop](https://www.tastychips.nl/shop/); [GR-2 announcement](https://www.tastychips.nl/early-bird-gr-2s-now-on-kickstarter/)).
The request most likely meant the GR-MEGA, so it gets its own table, after the GR-1's.

**Status key.**
- **Built:** in M1 or M2. The citation is the INTENT section or the M2 spec.
- **Built (M3):** built in M3 sub-project 1, or specified in an M3 sub-project spec now in build.
- **Planned:** on the roadmap. The citation names the milestone and section.
- **Partial:** part of the capability exists, or it can be had by routing; the note says what's missing.
- **Gap:** neither the intent nor the roadmap covers it.
- **Out of scope:** excluded by design (the INTENT line is cited), or a hardware-only feature with
  no software analog that matters.

Our citations are short: "§14 Voice mode" means INTENT §14's paragraph headed **Voice mode**,
"M2 sources" means `docs/superpowers/specs/2026-10-30-m2-sources-design.md`, and so on.

---

## 1. Torso Electronics S-4 ("sculpting sampler")

**In outline.** Four parallel stereo tracks. Each track runs five devices in a chain: Material,
then Granular, Filter, Color and Space
([product page](https://torsoelectronics.com/pages/s-4);
[specs](https://torsoelectronics.com/s4/technical-specifications)).
- **Material** is one of DISC (streams from disk), TAPE (a looper) or POLY (an 8-voice sampler).
- **Granular** is MOSAIC, **Filter** is RING (a 48-band resonator), **Color** is DEFORM and
  **Space** is VAST ([device reference](https://docs.torsoelectronics.com/s4/devices/device-reference/)).

Figures below are from the OS 2.x user guide unless noted.

| S-4 feature | Detail (source) | Giga Grains status |
|---|---|---|
| **Grain engine: Mosaic** | | |
| Grain count | Up to 64 grains per track (the current guide's [device reference](https://docs.torsoelectronics.com/s4/devices/device-reference/)); the launch review gave 128 ([SOS](https://www.soundonsound.com/reviews/torso-electronics-s-4)) | **Built**, far beyond it: 4,096 a layer, with a ceiling of 16,384 an engine (§14 Grain pool; M2 engine) |
| Grain buffer | A 4 s rolling buffer, which LOCK freezes and saves with the project ([Mosaic](https://docs.torsoelectronics.com/s4/devices/device-reference/mosaic/)) | **Built**, beyond it: rings of 1 to 300 s, Freeze, saved when held (§5; M2 sources) |
| RATE | Tempo divisions from 4/1 to 1/64, or FREE ([Mosaic](https://docs.torsoelectronics.com/s4/devices/device-reference/mosaic/)) | Free rate **Built** (Density, 0.01 to 400 Hz; §14 Scheduler). Tempo divisions are **Planned** (M4's Sync scheduler; §14 Scheduler, §20) |
| SIZE | In beats, relative to the global tempo ([Mosaic](https://docs.torsoelectronics.com/s4/devices/device-reference/mosaic/)) | In seconds, **Built** (5 ms to 2 s; §14 Per-grain spreads). In beats, **Planned** (M4; §20) |
| CONTOUR | The grain envelope: a sine at the center, skewing to a downward ramp below it and an upward ramp above it ([Mosaic](https://docs.torsoelectronics.com/s4/devices/device-reference/mosaic/)) | **Partial.** Texture runs from Hann to a late peak to gated (§14 Texture). Nothing gives an early peak or a downward ramp (percussive grains). The parameter model already carries a skew field (§14 Parameter model). **Gap** for the downward skew |
| SCAN | STRETCH (the playhead drifts through the audio), POSITION (a fixed point), and DELAY SYNC or DELAY FREE (a fixed delay behind the input) ([Mosaic](https://docs.torsoelectronics.com/s4/devices/device-reference/mosaic/)) | POSITION and the delay forms are **Built** (Position: seconds behind the head on a ring, a fraction of a sample; §14 The Source kind). DELAY SYNC is **Planned** (M4). **STRETCH, a moving playhead, is a Gap:** Position is static unless an LFO, automation or a transport source moves it (§14 The mod matrix) |
| SPRAY | RANDOM: per-grain jitter of position and stereo. WARP: smooth random drift of the playhead. Each has a MONO form ([Mosaic](https://docs.torsoelectronics.com/s4/devices/device-reference/mosaic/)) | **Built.** Spray and Stereo give the random form (§14). Random 1 or 2 with Smooth, routed to Position, gives the warp form (§14 The mod matrix) |
| PATTERN | A fixed pseudo-random pattern applied to the grains' pitch, unique to each instance ([Mosaic](https://docs.torsoelectronics.com/s4/devices/device-reference/mosaic/)) | **Planned:** M4's "patterns" (§20). Today the Step source (16 steps, in time rather than per grain) routed to Pitch comes close (§14 The mod matrix) |
| PITCH, quantized to a scale | Scale-based ([Mosaic](https://docs.torsoelectronics.com/s4/devices/device-reference/mosaic/); [scales](https://docs.torsoelectronics.com/s4/appendix/scales/)) | **Gap.** Pitch Spread is continuous and uniform (§14 Per-grain spreads). Scale quantize is named in §8.1 and in §14's parameter model, but §22 leaves it as "after M2" with no milestone |
| Per-grain random pitch, amplitude and reverse | Added by OS 2.2, with Feedback (which replaced SOS) and feedback of the output into the buffer ([Gearnews](https://www.gearnews.com/torso-electronics-s-4-synth/)) | Random pitch is **Built** (Pitch Spread). Feedback is **Built** (§14 Feedback). Random amplitude per grain is a **Gap** (small: the parameter model's random drop and gain, §14). **Random reverse is a Gap:** no grain plays backwards |
| WET as MIX or SEND | ([Mosaic](https://docs.torsoelectronics.com/s4/devices/device-reference/mosaic/)) | **Built** (Mix, Dry to Reverb; §14 Extremes) |
| Runs while stopped | A choice: always, or only while playing ([Mosaic](https://docs.torsoelectronics.com/s4/devices/device-reference/mosaic/)) | **Built:** a plugin runs whenever the host processes. The transport-stop policy is **Planned** for M4 (§20, §22) |
| **Material devices** | | |
| DISC: stream from disk with no length limit; TEMPO SYNC, STRETCH (pitch kept) or FREE; START and LENGTH on a grid; FREEZE; REVERSE; RECORD to a file ([Disc](https://docs.torsoelectronics.com/s4/devices/device-reference/disc/)) | | Streaming from disk is **Out of scope:** "Sources live in RAM. Disk streaming doesn't suit random-access grain reads" (§14 Buffers and memory; M2 sources' non-goals). But samples of any length within the 1 GiB to 16 GiB cap are **Built** (M2 sources). Tempo-locked STRETCH playback is the playhead **Gap** above, with the tempo lock for M4. Reverse playback is a **Gap** |
| TAPE: a 30 s looper buffer; LENGTH in bars or FREE; SOS overdub; quantized record start; varispeed and reverse recording; REWIND and REVERSE ([Tape](https://docs.torsoelectronics.com/s4/devices/device-reference/tape/); [CDM on OS 2.0](https://cdm.link/torso-s-4-os-2-0/)) | | Looping, overdub and record into the ring are **Built**, at up to 300 s (Loop, Loop Length, Overdub, Freeze, capture; M2 sources). Loop length in bars and a quantized record start are a **Gap**: M2 sources deferred "a looper transport (record arm, punch in, quantized loop start)" to M4, but §20's M4 row doesn't list it. Varispeed recording is a **Gap**, and a minor one |
| POLY: an 8-voice sampler; loop modes off, forward and ping-pong; an ADSR with a curve; velocity; a per-voice filter amount and shape; one MIDI channel per track ([Poly](https://docs.torsoelectronics.com/s4/devices/device-reference/poly/); [MIDI CCs](https://midi.guide/d/torso-electronics/s-4/)) | | Polyphonic voices are **Built (M3)**: 16 a layer, with an envelope per voice and velocity (§14 Voice mode). Plain sample playback is **Out of scope** ("not a sampler", §9). **Gaps:** an envelope curve (ours is linear); a filter per voice (our filters are per layer, after the voices are summed; §14 Extremes); a MIDI channel per layer (below) |
| **Filter: RING** | A 48-band tuned resonator bank: Cutoff, Resonance, Decay (Sustain or Choke), Pitch ±24, Slope morphing low-pass to band-pass to high-pass, Tone, Scale quantize ([Ring](https://docs.torsoelectronics.com/s4/devices/device-reference/ring/)) | A 12 dB state-variable filter, two per layer (low-pass, high-pass, band-pass, notch), is **Built** (§14 Extremes). **The tuned resonator bank is a Gap** |
| **Color: DEFORM** | Drive, a one-knob compressor, Crush (bit depth or sample rate), Tilt (two bands), noise that follows the input envelope ([Deform](https://docs.torsoelectronics.com/s4/devices/device-reference/deform/)) | **Gap.** Drive is "later" in §15 (sst-waveshapers is planned, with no milestone). There is no crusher, compressor or input-following noise |
| **Space: VAST** | A delay (FREE, STRAIGHT, TRIPLET or DOTTED) and a reverb; Spread as phase shift, ping-pong or diffusion; Tone in the loop; Freeze and Clear ([Vast](https://docs.torsoelectronics.com/s4/devices/device-reference/vast/)) | The delay with ping-pong, the reverb with Decay ∞, a diffuser and a damper in the loop are **Built** (§14 Extremes; M1 Space). **A tempo-synced delay time is a Gap** (Time is 1 ms to 2 s) |
| **Modulation** | Four modulators per track: WAVE (skew, fold, curve, phase, stereo spread), RANDOM (variation, smoothing), FOLLOW and ADSR (each stage with a curve). Each can map to any number of destinations, and Perform, Scene and Mix have groups of their own. Each runs always or only while playing ([modulation](https://docs.torsoelectronics.com/s4/modulation/modulation-overview/); [MIDI CCs](https://midi.guide/d/torso-electronics/s-4/)) | **Built, and richer:** 2 LFOs, 2 envelopes, 2 Randoms and a 16-step Step source per layer, plus the follower, MIDI and transport, in a 48-slot matrix (§14 The mod matrix). **Gaps:** LFO skew, fold and phase; envelope curves; per-channel spread of a modulator; a follower on a chosen track or layer (ours hears the input alone); rates synced to tempo (only the Beat and Bar phase sources exist) |
| **Perform** | 12 macros: 8 sliders and 4 punch-in "action" buttons. They are relative and project-wide, with four modulators of their own ([Perform](https://docs.torsoelectronics.com/s4/perform-and-scene/perform-overview/); [specs](https://torsoelectronics.com/s4/technical-specifications)) | **Built:** 8 relative macros (§14 Macros; §17). **Gap:** momentary punch-in actions and a TEMP control that snaps back on release ([temporary control](https://docs.torsoelectronics.com/s4/miscellaneous/temporary-control/)) |
| **Scenes** | 128 scenes per project, recalling every parameter and sample. A track can keep its parameters or its sample. Launch is quantized (straight, triplet, dotted, free, or sample end). Launches can chain by rule (next, previous, walk, random) with a chance percentage, and Program Change launches a scene ([scenes](https://docs.torsoelectronics.com/s4/perform-and-scene/scene-overview/); [launching](https://docs.torsoelectronics.com/s4/perform-and-scene/scene-overview/launching-scenes/); [chance, rule, chain](https://docs.torsoelectronics.com/s4/perform-and-scene/scene-overview/chance-rule-and-chain/)) | **Gap.** We have presets with a browser, Mutate and undo (§17), the XY morph across layers (§14 Layers), and the host's automation. Snapshots inside an instance, recalled quantized or by Program Change, are not in the intent |
| **Tracks and routing** | Four sends route each track into any other track, or into itself ([SOS](https://www.soundonsound.com/reviews/torso-electronics-s-4); [track overview](https://docs.torsoelectronics.com/s4/projects-and-tracks/track-overview/)) | **Partial.** A Resample layer re-grains the instance's whole output, and Feedback re-grains a layer into its own ring (§14 Mix and global FX, Feedback). **Gap:** feeding one chosen layer into another |
| **Mixer** | Level, pan and a DJ filter per track; a master compressor ([specs](https://torsoelectronics.com/s4/technical-specifications)) | Levels and filters per layer, and the limiter, are **Built** (§14). A per-layer Pan is **Built (M3)** (the expression spec §2). A master compressor is a **Gap** (minor) |
| **Copy and paste** | Scenes, tracks and devices ([CDM](https://cdm.link/torso-s-4-os-2-0/)) | **Gap** (minor, UI only): copying a layer's settings to another layer |
| **MIDI and sync** | A MIDI channel per track; CC control; MIDI clock in and out; analog sync; Ableton Link; Program Change to scenes ([MIDI config](https://docs.torsoelectronics.com/s4/midi-and-analog-sync/midi-config/); [Gearnews](https://www.gearnews.com/torso-electronics-s-4-synth/)) | CC and automation are **Built**, through the host. **Gap: a note channel per layer** (all four layers hear every note; §14 Layers says "per-layer or per-channel note routing is M3's", but the M3 program defers it, §4.1). MIDI clock, analog sync and Link are **Out of scope** for the plugins, since the host owns the tempo (the standalone could use them: post-1.0) |
| **Hardware** | Stereo 1/4" in and out, a built-in microphone, a 10-in, 10-out USB audio interface, 4 GB of storage, an LCD, 9 encoders, 21 buttons ([specs](https://torsoelectronics.com/s4/technical-specifications)) | **Out of scope:** hardware. The standalone app covers audio I/O (§2) |

## 2. Tasty Chips GR-1 (granular synthesizer, 2017; firmware 2.x and 3.x)

| GR-1 feature | Detail (source) | Giga Grains status |
|---|---|---|
| Voices and grains | 128 grains a voice. Sources disagree on polyphony: a regular polyphony of 11 notes ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)), 16 voices (retailer), "1,000+ grains" in all ([Tasty Chips](https://www.tastychips.nl/product/gr-1-granular-synthesizer/)) | **Built (M3)** and larger: 16 voices a layer across 4 layers, sharing 4,096 grains a layer (§14 Voice mode, Grain pool) |
| Grain size | 5 to 5,000 ms ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | **Built** from 5 ms to 2 s (§14 Per-grain spreads). Grains of 2 to 5 s are a **Gap** (minor) |
| Density | 0.1 to 1,000 grains per second ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | **Built** from 0.01 to 400 Hz (§14 Scheduler). Above 400 Hz is **Built (M3)** at Clock Pitch (the pitch-sync spec §1) |
| Grain shape | Sides, Tilt and Curve, drawn on screen ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | **Partial:** Texture is one family of shapes (§14 Texture). An early-peak tilt and a curve are a **Gap** |
| Scan | A playhead speed of up to ±2×, overridden by the slider; scan loops ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1); [loopop](https://loopopmusic.com/review-tasty-chips-gr-1-granular-synthesis-explained-full-workflow-tutorial)) | **Gap:** no playhead speed and no scan loop (see the S-4's SCAN). Position and its drag on the canvas are **Built** (§17) |
| Spray, Pan Spray | ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | **Built:** Spray and Stereo (§14) |
| Direction | The probability that a grain plays forwards or backwards ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | **Gap:** no reverse grains |
| Pitch and glide | Pitch ±1 octave on the knob, wider through MIDI; Glide ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | Pitch is **Built**, clamped to ±48 semitones (§14 Per-grain spreads). **Glide is out of scope for M3** ("Mono, legato and glide… fit later", M3 program §3). It is a Gap for after M3 |
| Grain sync | Grains synced to the note or to MIDI clock, with density as the division ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | Sync to the note is **Built (M3)** (Clock Pitch, the pitch-sync spec). Sync to the clock is **Planned** (M4) |
| Reuse mode | Recycles old grains ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | **Gap** (minor; it is a character effect) |
| Sources | Mono samples only, up to about 2 min; live sampling through a USB interface, up to 112 s; loop recording ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | **Built**, beyond it: stereo samples bounded only by the memory cap, rings to 300 s, loop and overdub (M2 sources) |
| Audition and Tape modes | Plain playback; tape playback, where pitch moves speed ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | Plain playback is **Out of scope** (§9). Tape mode is the playhead **Gap** with speed tied to pitch |
| LFOs | Two, from 27.5 hours to 100 Hz, in sine, saw, square, random and Aux (a hand-drawn, recorded shape of about 70 s) ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | **Built:** 2 LFOs a layer at 0.01 to 50 Hz, with Random and Step (§14 The mod matrix). A drawn or recorded LFO is a **Gap** (minor) |
| Mod matrix | 9 × 11; MIDI, CV, velocity, aftertouch and MPE as sources; no poly aftertouch ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | **Built**, larger: 48 slots (§14 The mod matrix). Poly aftertouch and MPE are **Built (M3)** (§14 Voice mode) |
| Envelopes | A voice ADSR ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | **Built (M3):** Env 1 is the voice's amplitude (§14 Voice mode) |
| Filter and FX | A 12 dB multimode filter and delay, distortion, bit reduction and reverb, all on the whole patch; a sine sub-oscillator ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | The filter, delay and reverb are **Built** (§14 Extremes, Space). Distortion and bit reduction are a **Gap**. A sub-oscillator is **Planned** as an Oscillator layer in M5 (§4, §20) |
| Multitimbral | Since 2.0: up to 4 patches on their own MIDI channels; a keyboard split; recording into one patch while the others play ([Synthtopia, v2.0](https://www.synthtopia.com/?p=106558); [SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | Four layers are **Built** (§14 Layers), but every note reaches every layer. **Gap: a channel and a key range per layer** (INTENT §14 promises M3; the M3 program defers it) |
| Chords and drones without a keyboard | Buttons play chords of up to 4 notes, or drones ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | **Partial:** in the FX notes freeze, and Cloud layers drone for as long as a note is held. A Hold (latch) control is a **Gap** (small) |
| Performances | 32 patches in 4 banks; a performance list (v3) ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1); [Synthtopia, v3](https://www.synthtopia.com/?p=146931)) | **Built:** presets, the browser and the host's project (§17, §18) |
| MIDI, CV and I/O | DIN and USB MIDI, NRPN; two CV inputs and a gate out; a 7" screen ([SOS](https://www.soundonsound.com/reviews/tasty-chips-gr-1)) | MIDI is **Built**. CV is **Out of scope** (hardware); the host's modulation and automation are its software analog |

## 3. Tasty Chips GR-MEGA (granular workstation; firmware 1.7)

| GR-MEGA feature | Detail (source) | Giga Grains status |
|---|---|---|
| Layers, voices, grains | 4 multitimbral layers, 20 voices a layer, 128 grains a voice, up to 10,000 grains in all; chords of up to 12 notes ([Tasty Chips](https://www.tastychips.nl/product/gr-mega-black/)) | 4 layers are **Built**, and 16 voices a layer are **Built (M3)**. Our grain counts are larger: 4,096 a layer, 16,384 an engine (§14) |
| Five engines | Sampler (with loop and slice); granular (with tape, granular-slice and sampler-slice modes); spectral (a phase vocoder of 3 voices, FFT 256 to 8,192, up to 12 notes a voice in chord mode) ([Tasty Chips](https://www.tastychips.nl/product/gr-mega-black/); [Synth Anatomy](https://synthanatomy.com/2026/07/superbooth-23-tasty-chips-gr-mega-multi-timbral-granular-synthesizer.html)) | Granular is **Built**. Spectral is **Built** as a paulstretch-style smear, which voices can play pitched (§14 Extremes; Voice mode). A Spectral window-size control is a **Gap** (ours is fixed near 85 ms). The sampler engine is **Out of scope** (§9). **Slice modes are a Gap for samples:** splices exist on rings only (M2 sources) |
| Rate modes | Free, Sync-size, and Scan & rate ([Tasty Chips](https://www.tastychips.nl/product/gr-mega-black/)) | Free is **Built**. Sync is **Planned** (M4). **Scan & rate is the playhead Gap** |
| Scan loop modes | Bounce, wrap, kill ([Tasty Chips](https://www.tastychips.nl/product/gr-mega-black/)) | **Gap**, with the playhead |
| Spawn modes | Direct, gradual, recycle ([Tasty Chips](https://www.tastychips.nl/product/gr-mega-black/)) | Direct is **Built**: a note forces an onset (§14 Layers). Gradual and recycle are a **Gap** (minor) |
| Arpeggiator | Up, down, random, shuffle, play order ([Tasty Chips](https://www.tastychips.nl/product/gr-mega-black/)) | **Gap**, low value: the first-class hosts have arpeggiators. Not a §9 non-goal (that is song sequencing) |
| Step sequencer | 64 steps, chords of up to 7 notes, one per patch ([Tasty Chips](https://www.tastychips.nl/product/gr-mega-black/)) | Note sequencing is **Out of scope** ("Song or arrangement sequencing", §9; the host sequences). A 16-step modulation sequencer is **Built** (Step; §14 The mod matrix) |
| Spray types (1.7) | Pan spray, pitch spray, and "patch spray", which spreads grains across layers ([Synth Anatomy](https://synthanatomy.com/2026/07/superbooth-23-tasty-chips-gr-mega-multi-timbral-granular-synthesizer.html); [Gearnews](https://www.gearnews.com/tasty-chips-gr-mega-synth/)) | Pan and pitch spray are **Built** (Stereo, Pitch Spread). Spreading grains across layers is a **Gap** (Bus Split spreads them across a layer's two buses only; §14 Extremes) |
| Pitch quantize (1.7) | Western and equal-tempered scales, and a user scale ([Synth Anatomy](https://synthanatomy.com/2026/07/superbooth-23-tasty-chips-gr-mega-multi-timbral-granular-synthesizer.html)) | **Gap** for grain pitch (see the S-4). Keys mapped through a scale are **Built (M3)** (Scala and MTS-ESP, the tuning spec) |
| Granular filter (1.7) | Controls what frequency content the grains spawn with ([Synth Anatomy](https://synthanatomy.com/2026/07/superbooth-23-tasty-chips-gr-mega-multi-timbral-granular-synthesizer.html)) | **Partial:** our bus filters act after the grains (§14 Extremes). A filter on the source is a **Gap** (minor) |
| Akai-style time-stretch; snap scan; grain clock retrigger at note-on ([Synth Anatomy](https://synthanatomy.com/2026/07/superbooth-23-tasty-chips-gr-mega-multi-timbral-granular-synthesizer.html)) | | Retrigger is **Built (M3)**: a voice forces an onset at its start (§14 Voice mode). Time-stretch is the playhead **Gap**. Snap scan is a **Gap** (it fits slices) |
| Modulation | 4 LFOs a layer, 4 step modulators, 20 sources and 100 destinations; envelopes from 0.1 ms to 45 s with log, linear and exponential curves, to pitch, filter, amp and aux; synced rates (normal, dotted, triplet) ([Tasty Chips](https://www.tastychips.nl/product/gr-mega-black/); [Synth Anatomy](https://synthanatomy.com/2026/07/superbooth-23-tasty-chips-gr-mega-multi-timbral-granular-synthesizer.html)) | **Built**, comparable: 7 modulators a layer and every parameter a target (§14 The mod matrix). **Gaps:** envelope curves; rates synced to tempo |
| Filters | 12 dB low-pass and high-pass **per voice** ([Tasty Chips](https://www.tastychips.nl/product/gr-mega-black/)) | Per layer, **Built** (two per layer; §14 Extremes). **Per voice, a Gap.** Per-grain filters are **Planned** for M5 (§20; §22 resolved buses for v1) |
| Effects | 4 at once: delays (plain and ping-pong), reverbs, overdrive and reducer, a compressor; in 1.7 a 48-band multi-band resonator (with scales, detune and physical models), saturation, effect presets, a master EQ ([Tasty Chips](https://www.tastychips.nl/product/gr-mega-black/); [Synth Anatomy](https://synthanatomy.com/2026/07/superbooth-23-tasty-chips-gr-mega-multi-timbral-granular-synthesizer.html)) | The delay, reverb and limiter are **Built** (§14). **Gaps:** drive and saturation ("drive later", §15), the reducer, the compressor, the resonator bank and the EQ |
| Multi-presets, templates, patch randomizer, macros ([Synth Anatomy](https://synthanatomy.com/2026/07/superbooth-23-tasty-chips-gr-mega-multi-timbral-granular-synthesizer.html)) | | Presets, INIT, Mutate (randomize with locks) and macros are **Built** (§17) |
| SFZ import into slice mode ([Synth Anatomy](https://synthanatomy.com/2026/07/superbooth-23-tasty-chips-gr-mega-multi-timbral-granular-synthesizer.html)) | | **Out of scope** (multisample mapping; §9) |
| Sampling | Stereo input; up to 60 min, or 5 min 50 s in granular mode ([Tasty Chips](https://www.tastychips.nl/product/gr-mega-black/)) | **Built:** a 300 s live ring a layer, and samples bounded only by the memory cap (M2 sources) |
| I/O | Balanced stereo in and out, 2 CV in and a gate out, DIN and USB MIDI, USB audio, HDMI, LAN, 512 GB of storage ([Tasty Chips](https://www.tastychips.nl/product/gr-mega-black/)) | **Out of scope:** hardware |

## 4. Tasty Chips GR-2 (2026; the GR-MEGA's firmware on smaller hardware)

The GR-2 runs the GR-MEGA's patches and shares its firmware's root ([Tasty Chips](https://www.tastychips.nl/early-bird-gr-2s-now-on-kickstarter/)).
So every GR-MEGA row above applies too: rate, loop and spawn modes, the arpeggiator, the per-voice
filters and the envelope curves. The table lists only what differs, or what the GR-2's page states.

| GR-2 feature | Detail (source) | Giga Grains status |
|---|---|---|
| Layers, voices, grains | 4 layers of 16 voices, 128 grains a voice, 4,000 grains in all, chords of up to 12 notes ([Tasty Chips](https://www.tastychips.nl/product/gr-2/)) | **Built (M3)**: our counts match or exceed them (§14) |
| Engines | Granular, sampler, tape (with tape-style scratching), spectral (a phase vocoder, plus paulstretch) ([Tasty Chips](https://www.tastychips.nl/product/gr-2/); [Synth Anatomy](https://synthanatomy.com/2026/05/tasty-chips-gr-2-the-second-generation-of-the-hardware-granular-sampler.html)) | Granular and paulstretch are **Built** (§14 Extremes). Scratching is the playhead **Gap** (a playhead under the hand). The sampler is **Out of scope** (§9) |
| Granular details | Two window types; slice mode; pitch per grain or per voice; clock retrigger ([Synth Anatomy](https://synthanatomy.com/2026/05/tasty-chips-gr-2-the-second-generation-of-the-hardware-granular-sampler.html)) | Per-grain and per-voice pitch and retrigger are **Built (M3)**. The window family is **Partial** (Texture). Slices on samples are a **Gap** |
| Modulation | 4 LFOs and 4 random generators a layer; ADSRs with curves and an Aux envelope; an envelope follower; key and position tracking; MPE in five dimensions; macros; **recordable X/Y touch gestures with playback**; 120+ destinations; unipolar and bipolar; curves ([Tasty Chips](https://www.tastychips.nl/product/gr-2/); [Sonicstate](https://www.sonicstate.com/news/2026/05/07/tasty-chips-electronics-gr-2-granular-synth-superbooth-2026/)) | MPE, the follower, key tracking, macros, curves and polarity are **Built** or **Built (M3)** (§14 The mod matrix; Voice mode). **Gaps:** envelope curves; a "position" tracking source (where a voice is reading); a gesture recorder inside the plugin (low: a host records the XY pad as automation, §17) |
| Filters | Low-pass and high-pass per voice, 12 dB ([Tasty Chips](https://www.tastychips.nl/product/gr-2/)) | **Gap** per voice (as for the GR-MEGA) |
| Effects (4 at once) | Chorus, compressor, delay, ping-pong delay, cubic distortion, tanh saturation, flanger, a 5-band EQ, bit reduction, reverb, large reverb, resonator, ring modulator, vibrato ([Tasty Chips](https://www.tastychips.nl/product/gr-2/)) | Delay, ping-pong and reverb are **Built**. **Gaps:** chorus, flanger, the distortions, bit reduction, the EQ, the resonator, ring modulation, the compressor |
| Arpeggiator | Up, down, random, shuffle, play order ([Tasty Chips](https://www.tastychips.nl/product/gr-2/)) | **Gap**, low (the host has one) |
| Sampling | Stereo input; 60 min, or 5 min 50 s in granular mode ([Tasty Chips](https://www.tastychips.nl/product/gr-2/)) | **Built** (M2 sources) |
| I/O | Stereo 1/4" in and out; USB-C as a multichannel audio interface; DIN MIDI; 2 CV in and a gate out ([Tasty Chips](https://www.tastychips.nl/product/gr-2/); [Synthtopia](https://www.synthtopia.com/?p=153199)) | **Out of scope:** hardware. Note that sources disagree on balanced I/O: the product page says balanced, while Tasty Chips' announcement says the GR-2 "lacks HDMI and balanced audio" |

---

## 5. Where we already lead

None of these products has any of the following, so they are not gaps:
- grains in the thousands per layer at a published CPU cost (§5, §16);
- live rings up to 300 s, saved with the session when held, with Morphagene-style splices and
  capture (§5; M2 sources);
- a Resample layer granulating its own output (§4);
- HQ sinc interpolation with mipmaps (§14 Kernels);
- Smear against combing at 1000× stretch (§14 Extremes);
- the XY morph across four layers;
- a 48-slot matrix with every parameter a target;
- per-note MPE through CLAP note expressions;
- pitch-sync formant grains (M3, specified);
- Scala and MTS-ESP microtuning (M3, specified);
- deterministic, bit-exact rendering (§8.6);
- a canvas that draws the real grains (§8.7);
- CLAP, VST3 and AU in every DAW, and the standalone app.

---

## 6. The real gaps, ranked

The ranking follows INTENT Part I:
- §3's four focuses: texture is the anchor, then playable, rhythmic and extreme;
- §8.1, "randomness is shaped… scale quantize, tempo grids";
- §6's borrowed ideas: Granulator III's Classic and Loop modes, Lucid and Fragments' scale-locked
  grains, Portal's macro morphing and transient-synced capture;
- §10.1, "the director reaches for it".

Each gap's smallest cover keeps the at-rest rule: a new control at its default renders every
golden bit-identically.

1. **A moving playhead, with speed, direction and loop modes, per layer and per voice.**
   - **What it is.** Position advances on its own at a set speed, the classic granular
     time-stretch, and can run backwards. At a loop's edges it wraps, bounces or stops. On a Voice
     layer it starts from Position at each note-on, so a voice plays through the sample at its own
     stretch.
   - **Who has it.** The S-4 (Mosaic SCAN STRETCH; Disc STRETCH and Tape), the GR-1 (scan
     speed, scan loops), and the GR-MEGA and GR-2 (Scan & rate; bounce, wrap and kill).
   - **Why it matters to us.** §5 promises "100× to 1000× stretch", and §6 borrows "Classic and
     Loop-style modes". Today a stretch needs an LFO routed to Position, the LFOs are global and
     never restart on a note, and every voice reads the same static Position (only Timbre →
     Position moves one voice). This is the largest hole for M3's "playable".
   - **Smallest cover.** Two layer parameters:
     - **Scan:** a speed, bipolar, in × real time, default 0 (today's static read);
     - **Scan Loop:** wrap, bounce or one-shot, within the sample, or within the ring's loop or
       splice window.

     A 64-bit playhead offset per stream adds to Position, latched per grain as Position already
     is. It is reset at each voice's start, and Cloud's playhead is reset at transport start.
     Smaller still, but weaker: a per-voice "Note Time" ramp source for the matrix.
   - **Fits:** M3 (a new small sub-project, or folded into sub-project 2). Locking Scan to the
     host's tempo comes in M4.

2. **Note routing per layer: a MIDI channel and a key range.**
   - **What it is.** Each layer hears only its own channel (All, or 1 to 16) and its own key
     range, so one instance is four instruments, or a keyboard split.
   - **Who has it.** All four: the S-4 (a MIDI channel per track), the GR-1 (4 patches on
     channels, a keyboard split), the GR-MEGA and the GR-2 (4 multitimbral layers).
   - **Why it matters to us.** INTENT §14 (Layers and the chain) says "per-layer or per-channel
     note routing is M3's, with the Voice scheduler". But the M3 program §4.1 says it "stays
     later", and no M3 spec builds it: a silent divergence.
   - **Smallest cover.** Three layer parameters, Channel (default All), Key Low (0) and Key High
     (127), applied when a note enters the note table's view for the layer. MPE member channels
     map to a layer's zone.
   - **Fits:** M3. Or amend §14 on purpose and move it to M4.

3. **Grain pitch quantized to a scale.**
   - **What it is.** Pitch Spread, Smear's detune and the routes' pitch offsets snap to a scale,
     so random pitch is always in key.
   - **Who has it.** The S-4 (Mosaic PITCH and PATTERN by scale; Ring's Scale) and the GR-MEGA
     and GR-2 (pitch quantize, 1.7).
   - **Why it matters to us.** §8.1 names "scale quantize" as how randomness is shaped, and §6
     borrows "scale-locked grains". But §22 has left it at "after M2", with no milestone.
   - **Smallest cover.** A layer parameter, Pitch Quantize (Off, Chromatic, or the instance's
     Tuning), that rounds each grain's semitone offset to the nearest degree of the scale. M3
     sub-project 4's tuning table already holds the degrees.
   - **Fits:** M3 sub-project 4 (or 5).

4. **Envelope curves.**
   - **What it is.** Exponential or logarithmic stages for Env 1 and Env 2.
   - **Who has it.** The S-4 (ADSR attack, decay and release curves), and the GR-MEGA and GR-2
     (curves from log through linear to exp).
   - **Why it matters to us.** Since M3 sub-project 1, Env 1 *is* the voice's amplitude, and a
     linear release sounds abrupt at its tail on long, pedaled chords.
   - **Smallest cover.** A Curve parameter per envelope, where 0 is linear (bit-identical) and the
     two directions bend the curve. A closed form keeps it deterministic.
   - **Fits:** M3 sub-project 2, which already makes the envelope controls per-voice targets.

5. **A filter per voice** (brightness from velocity, pressure or timbre, note by note).
   - **Who has it.** The GR-MEGA and GR-2 (low-pass and high-pass per voice, with a filter
     envelope) and the S-4's Poly (filter amount and shape per voice).
   - **Why it matters to us.** Our filters are per layer, after the voices are summed (§14
     Extremes), so MPE timbre can't brighten a single note.
   - **Smallest cover.** One state-variable filter per voice on that voice's grain sum (16 per
     layer), counted against the budget. Or wait for M5's per-grain filters (§20), which
     subsume it.
   - **Fits:** M5, unless the director wants MPE brightness at M3's gate.

6. **Tempo sync beyond grain onsets.**
   - **What it is.** LFO, Random and Step rates in note divisions; the delay's Time in straight,
     triplet or dotted divisions; loop length in bars; a quantized record start.
   - **Who has it.** The S-4 (Tape LENGTH in bars, quantized record, Vast's TIME, Mosaic in
     beats) and the GR-MEGA 1.7 (synced rates).
   - **Why it matters to us.** §3.3 is the rhythmic focus. §20's M4 row lists the grain grid
     only, and M2 sources deferred the looper transport to M4 without §20 saying so.
   - **Smallest cover.** A Sync choice on each rate parameter (Hz or a division), read from the
     transport that is already a source.
   - **Fits:** M4. Add these to M4's row.

7. **Reverse grains.**
   - **What it is.** A per-grain probability of playing backwards.
   - **Who has it.** The GR-1 (direction), the S-4 (Mosaic's random reverse in OS 2.2; Tape and
     Disc reverse), and the GR-2's scratching.
   - **Smallest cover.** A layer parameter, Reverse (0 to 100%), with a counter-keyed draw per
     grain (a new `rng::Field`; nothing drawn at 0) and a negative read rate in the kernels. The
     views already draw a reverse rate (views spec VW-T2c).
   - **Fits:** M4, or M3 alongside the playhead's direction.

8. **Snapshots inside an instance, with morph and quantized recall.**
   - **What it is.** Some 8 to 16 full-state snapshots per instance: recalled by a stepped
     parameter or a Program Change, optionally quantized to the beat, with a morph amount between
     two of them. Momentary "punch-in" and snap-back (temporary) controls come with it.
   - **Who has it.** The S-4 (128 scenes with chance and chain; punch-in and TEMP) and the
     GR-MEGA's multi-presets.
   - **Why it matters to us.** §6 borrows Portal's "macro morphing", and §7 serves live
     performers.
   - **Smallest cover.** Snapshot slots saved in the state; recall interpolates the continuous
     parameters on the grid and switches the stepped ones at the end.
   - **Fits:** M4 (quantized recall), or after 1.0.

9. **Color and resonance: drive and saturation, a crusher, and a tuned resonator bank.**
   - **Who has it.** The S-4 (Deform; Ring's 48-band resonator, quantized to a scale), the
     GR-MEGA and GR-2 (overdrive and tanh, reducer, a 48-band resonator, chorus, flanger, ring
     modulation, EQ, compressor) and the GR-1 (distortion, bit reduction).
   - **Why it matters to us.** §3.4's extreme focus, and texture: a resonator tuned to the
     scale turns noise and clouds into chords.
   - **Smallest cover.** A per-layer drive and crush stage in the layer's task (sst-waveshapers,
     already planned in §15 as "drive later"), and one global resonator bank tuned by M3's tuning
     table.
   - **Fits:** M5.

10. **Grain envelope skew both ways, and window choices.**
    - **What it is.** Early-peak (percussive) grains as well as late-peak ones.
    - **Who has it.** The S-4 (CONTOUR's downward ramp), the GR-1 (Tilt and Curve), and the
      GR-MEGA and GR-2 (two window types).
    - **Why it matters to us.** Rhythmic and percussive grain streams (§3.3).
    - **Smallest cover.** Make Texture's low half bipolar, or add a Skew parameter that drives
      the parameter model's existing skew field (§14 Parameter model). At 0 it is the Hann.
    - **Fits:** M4.

**Lower-ranked gaps**, each with its natural home:
- **Layer-to-layer resampling** (the S-4's track sends): a "Resample from: Out, or layer A to D"
  choice. M5, or after 1.0.
- **Slices on samples**, with a key or Splice choosing a slice, and snapping slices to transients
  (the GR-MEGA's granular slice mode): extend the ring's splices to Sample layers. M4.
- **Hold (latch)** for drones without a keyboard (the GR-1's chord and drone buttons): one global
  toggle. M3 sub-project 5, small.
- **Glide, mono and legato** (the GR-1): after M3, as the M3 program's non-goals say.
- **The follower on a chosen layer or the output** (the S-4's Follow); **LFO skew, fold and
  phase**; **a drawn or recorded LFO** (the GR-1's Aux; the GR-2's gestures): M5, or after 1.0.
- **A Spectral window size and a Spectral chord mode** (the GR-MEGA and GR-2, FFT 256 to
  8,192): M5.
- **Grains longer than 2 s** (up to 5 s, as on the GR-1): trivial; any milestone.
- **A master compressor, an EQ, chorus and flanger** (the GR-2, the S-4's mixer): after 1.0.
- **An arpeggiator** (the GR-MEGA and GR-2): after 1.0. The hosts have arpeggiators.
- **MIDI clock and Ableton Link in the standalone** (the S-4): M6, or after 1.0.

**Out of scope, confirmed:**
- **Disk streaming**, the S-4's Disc: §14 Buffers and memory; M2 sources' non-goals.
- **A plain or multisample sampler and SFZ import**, as in the S-4's Poly and the GR sampler
  engines: §9, "Multisample zone mapping. This is not a sampler."
- **Note step sequencing**, as in the GR-MEGA: §9, "Song or arrangement sequencing".
- **All the hardware:** CV and gate, built-in audio interfaces, the microphone, the screens and
  encoders, analog sync, and onboard storage.

---

## 7. What in M3's specs should change now

1. **Settle per-layer note routing (gap 2).** INTENT §14 (Layers and the chain) assigns it to M3.
   The M3 program (§4.1, "per-layer note routing stays later") and all four sub-project specs
   leave it out. Either add it to M3, which is small and natural beside sub-project 2's MPE zones
   (an upper zone could feed chosen layers), or amend §14 with a §23 row moving it to M4. As it
   stands, the work and INTENT disagree.
2. **Add a per-voice playhead (gap 1) to M3's scope.** Without it a Voice layer can't play
   through a sample: every voice reads one static Position. This is the most common way a
   granular instrument is played, and the reason §6 cites Granulator III's Classic mode. The
   smallest form is two layer parameters (Scan, Scan Loop), or at least a per-voice ramp
   source. This needs a program-design amendment and a director's call.
3. **Give Env 1 and Env 2 a curve (gap 4) in sub-project 2.** That spec already makes the eight
   envelope controls per-voice targets, so the curve sits naturally there. At Linear it is
   bit-identical.
4. **Use sub-project 4's tuning table for grain pitch quantize (gap 3).** It is a small,
   high-value addition to sub-project 4 or 5, and it closes §8.1's "scale quantize", which has
   waited since M2 (§22).
5. **Make M4's row explicit (gap 6).** §20's M4 row names the grain grid alone. It should also
   carry synced modulator and delay rates, and the looper transport that M2 sources deferred to
   M4 (record arm, punch-in, quantized loop start).

## Sources

**Torso S-4**
- [Product page](https://torsoelectronics.com/pages/s-4)
- [Technical specifications](https://torsoelectronics.com/s4/technical-specifications)
- The user guide:
  [device reference](https://docs.torsoelectronics.com/s4/devices/device-reference/),
  [Mosaic](https://docs.torsoelectronics.com/s4/devices/device-reference/mosaic/),
  [Tape](https://docs.torsoelectronics.com/s4/devices/device-reference/tape/),
  [Disc](https://docs.torsoelectronics.com/s4/devices/device-reference/disc/),
  [Poly](https://docs.torsoelectronics.com/s4/devices/device-reference/poly/),
  [Ring](https://docs.torsoelectronics.com/s4/devices/device-reference/ring/),
  [Deform](https://docs.torsoelectronics.com/s4/devices/device-reference/deform/),
  [Vast](https://docs.torsoelectronics.com/s4/devices/device-reference/vast/),
  [modulation](https://docs.torsoelectronics.com/s4/modulation/modulation-overview/),
  [modulator reference](https://docs.torsoelectronics.com/s4/modulation/modulator-reference/),
  [Perform](https://docs.torsoelectronics.com/s4/perform-and-scene/perform-overview/),
  [scenes](https://docs.torsoelectronics.com/s4/perform-and-scene/scene-overview/),
  [launching scenes](https://docs.torsoelectronics.com/s4/perform-and-scene/scene-overview/launching-scenes/),
  [chance, rule and chain](https://docs.torsoelectronics.com/s4/perform-and-scene/scene-overview/chance-rule-and-chain/),
  [MIDI config](https://docs.torsoelectronics.com/s4/midi-and-analog-sync/midi-config/),
  [global tempo](https://docs.torsoelectronics.com/s4/miscellaneous/global-tempo/),
  [track overview](https://docs.torsoelectronics.com/s4/projects-and-tracks/track-overview/)
- [MIDI CC map (midi.guide)](https://midi.guide/d/torso-electronics/s-4/)
- [Sound On Sound review](https://www.soundonsound.com/reviews/torso-electronics-s-4)
- [Gearnews (OS 2.2)](https://www.gearnews.com/torso-electronics-s-4-synth/)
- [CDM (OS 2.0)](https://cdm.link/torso-s-4-os-2-0/)
- [Sonicstate (Superbooth 2025)](https://www.sonicstate.com/news/2025/05/09/superbooth-2025-torso-electronics-s4-20-sculpting-sampler/)

**Tasty Chips GR-1**
- [Product page](https://www.tastychips.nl/product/gr-1-granular-synthesizer/)
- [Sound On Sound review](https://www.soundonsound.com/reviews/tasty-chips-gr-1)
- [Synthtopia (v2.0)](https://www.synthtopia.com/?p=106558)
- [Synthtopia (v3)](https://www.synthtopia.com/?p=146931)
- [loopop review](https://loopopmusic.com/review-tasty-chips-gr-1-granular-synthesis-explained-full-workflow-tutorial)

**Tasty Chips GR-MEGA**
- [Product page](https://www.tastychips.nl/product/gr-mega-black/)
- [Synth Anatomy (1.7)](https://synthanatomy.com/2026/07/superbooth-23-tasty-chips-gr-mega-multi-timbral-granular-synthesizer.html)
- [Gearnews](https://www.gearnews.com/tasty-chips-gr-mega-synth/)
- [Sonicstate](https://sonicstate.com/news/2024/03/27/gr-mega-granular-workstation/)

**Tasty Chips GR-2**
- [Product page](https://www.tastychips.nl/product/gr-2/)
- [Tasty Chips' announcement](https://www.tastychips.nl/early-bird-gr-2s-now-on-kickstarter/)
- [Shop](https://www.tastychips.nl/shop/)
- [Sound On Sound news](https://www.soundonsound.com/news/tasty-chips-gr-2-granular-synthesizer)
- [Synth Anatomy](https://synthanatomy.com/2026/05/tasty-chips-gr-2-the-second-generation-of-the-hardware-granular-sampler.html)
- [Synthtopia](https://www.synthtopia.com/?p=153199)
- [Sonicstate (Superbooth 2026)](https://www.sonicstate.com/news/2026/05/07/tasty-chips-electronics-gr-2-granular-synth-superbooth-2026/)
