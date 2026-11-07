# M3 sub-project 9: a filter per voice

| | |
|---|---|
| **Status** | Draft, 2026-11-07. Written without the director, at his request ("keep building all of the sub-projects as parallel as possible and merging them as able to. progress as much as you can without input"). Every forced choice is **Claude's provisional call** (F1 to F14, §3), each with the alternatives it declined. The builder check's answers are in §3a. The director reviews the calls later. **F2 (lanes, not slots) and F4 (Off never reroutes) carry the architecture; F5 (the fourth parameter is Key Track) is the one he is most likely to overrule** |
| **Origin** | The director's decision of 2026-11-07 ("no i think this all needs to happen in m3"): every gap the competitor comparison found moves into M3. This is **gap 5**, a filter per voice. The GR-MEGA and GR-2 have low-pass and high-pass filters per voice with a filter envelope, and the S-4's Poly has a per-voice filter amount and shape. The comparison is in the session's research notes (`competitor-gaps.md` §6.5) |
| **Program** | `docs/superpowers/specs/2026-11-07-m3-program-design.md`: §4 (the voice model), §9 (the rules), and its §2a P1 to P8. Sub-project 9 is not yet in its §8 table; the top controller adds the row (§6 below) |
| **Builds on** | sub-project 1, `docs/superpowers/specs/2026-11-07-m3-voice-design.md`: V1 to V7 and B1 to B10, as built on `m3-voice` (draft PR #37) with V7's headroom. The per-voice target list is coordinated with sub-project 2, `2026-11-07-m3-expression-design.md` on `m3-sp2-expression` (E6, E7, EB5, EB6) |
| **Branch** | `m3-sp9-voicefilter`, worktree `~/src/gg-m3-sp9-voicefilter` (from `m3-voice` at `18ac47d`) |
| **Parameter indices** | layer indices **0x74 to 0x77** in each layer scope; no global |

## 1. Intent

On a layer at Mode Voice, each voice's grains pass through **that voice's own state-variable
filter** before they meet the layer's two bus filters. The filter's type is the layer's. Its
cutoff and resonance are the layer's values plus the voice's own modulation, so a strike's
velocity, a LinnStrument's pressure or Y-axis timbre, the note's key and that voice's own Env 2
brighten or darken **one note** while its neighbours keep theirs. A pedaled chord can open note by
note, and an MPE slide up the pad can brighten the note being slid.

This is not INTENT §22's per-grain filter. That one stays M5's, measured at 2.75× to 3.59× X per
grain-sample in M2. A per-voice filter runs once per voice per sample, whatever the voice's grain
count: at most 32 four-wide filter steps a sample per layer (§4.7).

**At rest nothing moves.** With Voice Filter Off, the default, a layer renders exactly as
sub-project 1 left it: the same rows, summed into the same buffers, in the same spawn order. Every
existing golden keeps its WAV SHA-256, sub-project 1's eight Voice goldens included.

## 2. Scope

**In:**
- four layer parameters: Voice Filter (the type), Voice Cutoff, Voice Resonance and Voice Key
  Track;
- the engine's **voice lanes**: each voice's grains rendered into its own buffer, filtered there,
  then summed in a fixed order;
- the lane's life: assignment when a voice starts, ring-out after it ends, merging when lanes run
  short, clearing on a reset;
- declicked type changes, with the return to the direct render;
- Voice Cutoff and Voice Resonance as **per-voice targets**, and with sub-project 2 as per-note
  slots;
- the cut-off and resonance law per voice, and Key Track;
- `gg-render` keys; seven goldens; the tests; a `gg-bench` case and a `--chain` stage;
- the Edit view's placement of the four parameters, which the frame tests require.

**Out:**
- per-grain filters (M5; §22 stays resolved as it is);
- a per-voice filter type;
- a filter on Cloud layers' grains: the bus filters serve those;
- a dedicated filter envelope (Env 2 is already per voice);
- drive or saturation inside the filter (sub-project 11, Color, if anywhere);
- a remote-controls page for the voice filter (sub-project 5's, if any: the "Filter A" to
  "Filter D" pages are full);
- Voice presets that use the filter (sub-project 5's);
- new factory routes (F7).

## 3. Forced choices (Claude's provisional calls; the director may overrule each)

- **F1. The filter lives in the engine, not the Processor.** `Engine` renders and filters each
  voice's lane inside `processBlock` and hands the `Processor` the same two buses it does today
  (`out` and `bus2`), so the bus filters, the diffuser and everything after them are unchanged.
  Only the engine knows a grain's voice and its end, and the lane's life follows those. Keeping the
  filter there also lets `gg-bench` time it on the engine alone. *Declined:* the engine exporting
  per-voice buffers that the `Processor` filters with `BusFilter`. That splits a lane's life
  between two components with a handshake, copies up to 32 stereo buffers out per piece, and
  leaves the cost unmeasurable on the engine.
- **F2. Lanes, not slots: 32 lanes a layer, each owned by one voice from its start until its last
  grain and its filter's ring-out end.** Sub-project 1's allocator takes the lowest free slot and
  reuses a stolen voice's own slot (`VoiceAllocator.h`). A finished voice's slot is free at once,
  while its grains still play out (V4). A filter tied to the slot would push the old voice's
  grains through the new note's filter the moment the slot was reused, and with a cap steal that
  would happen every time. So `kVoiceLanes = 2 × kMaxVoices = 32` lanes stand apart from the
  slots:
  - a new voice takes the lowest-index Free lane;
  - with none Free, it takes the Draining lane whose owner has the lowest ordinal, the oldest, and
    **merges** with it: the old voice's remaining grains stay on the lane, its filter state
    carries on, and its coefficients glide to the new voice's over the next cell (no click).

  At most 16 lanes are Owned (one per spawning voice), so a merge always finds a Draining lane.
  *Declined:*
  - one filter per slot, 16 in all (above);
  - discarding a draining lane's state at exhaustion, which clicks;
  - more than 32 lanes (memory and cost for a case that merges cleanly anyway).
- **F3. The order: the voice filter, then the layer's bus filters.** A grain's path is:
  1. its voice's lane, on its own bus;
  2. the voice filter;
  3. the layer's bus filter for that bus (Bus 1 or Bus 2, as today);
  4. the diffuser, Level and the rest, as today.

  Each lane holds both buses' stereo, four channels (bus 0 L, R, bus 1 L, R), through **one
  four-wide `CytomicSVF` step**: `processBlockStep(A, B, C, D)` runs the same coefficients on all
  four SIMD lanes. The filter is linear, so filtering a voice's two bus shares separately is
  filtering the voice, and the bus split costs nothing extra. *Declined:*
  - per-voice bus filters (twice the cost, and Bus 1 and Bus 2 would stop meaning the layer's);
  - the voice filter replacing the bus filters on Voice layers (it would lose a feature);
  - the voice filter after the buses (the buses are layer-wide sums, and nothing per voice is left
    to filter by then).
- **F4. Off never reroutes.** The lanes are rendered only while the layer's voice filter is
  **engaged**: the type in force isn't Off, or a type fade is in progress, and at least one lane is
  busy. While it isn't engaged, `renderPool` writes every grain into `out` and `bus2` in spawn
  order, exactly sub-project 1's code path. The lane column is written and nothing reads it.
  - **Off to a type:** engagement begins at a grid sample. From there, every grain with a lane
    renders into its lane, and each lane crossfades from its dry sum to the filter over the
    10 ms ramp, the incoming filter starting from silence. That is `BusFilter`'s Off-to-type law.
  - **A type to Off:** the lanes crossfade from the filter to dry. At the first grid sample at or
    after the fade's end, the engine disengages and returns to the direct render.

  Summing the lanes' dry signals differs from the direct spawn-order sum only by rounding (about
  an ulp), so neither switch is audible. *Declined:* always rendering Voice layers by lane. That
  changes the summation order, so every one of sub-project 1's Voice goldens would move, and the
  cost would be paid with the filter Off.
- **F5. Four parameters: Voice Filter, Voice Cutoff, Voice Resonance, Voice Key Track.**
  - The first three mirror a bus's: the same type list, the same log2 Hz cutoff range and default,
    and the same resonance law.
  - The fourth is **Key Track**, because the matrix can't express it exactly. A Key or Note Pitch
    route is unipolar from key 0, or bipolar about 63.5, so the base cutoff would stop meaning
    "the cutoff at C4".

  *Declined:*
  - an Env Amount parameter: a route does it exactly, Env 2 → Voice Cutoff, since Env 2 is per
    voice;
  - a velocity-sensitivity parameter: Velocity → Voice Cutoff is a route;
  - a per-voice type: a type is structure, and a crossfade per voice would double the lane state.
- **F6. Key Track's law.** The cutoff moves by `keyTrack × pitchIn / 12` octaves, where `pitchIn`
  is the voice's note semitones above C4. That is the term sub-project 1 adds to the voice's pitch
  (`(key − 60) + bend + tuning`; sub-project 2 adds the channel tuning, EB11, and sub-project 4
  owns the key term through the tuning). So 100% is one octave per octave, centred on C4, and it
  follows slides and the tuning. It does not follow the layer's Pitch, which transposes the
  material (the formant with sub-project 3's Clock = Pitch), not the note. Range 0% to 100%,
  default 0%. *Declined:* a bipolar range (negative tracking is rare and has no route-free use);
  tracking the layer's Pitch too.
- **F7. Per-voice targets: Voice Cutoff and Voice Resonance; layer-wide only: Voice Filter and
  Voice Key Track.**
  - The two join `isVoiceTarget`, so a per-voice source routed to them acts per voice on a Voice
    layer (B2's no-double-count rule holds as built). With sub-project 2's E7, they also join the
    per-note slots and get the three CLAP per-note flags.
  - All four are valid route targets layer-wide, as Bus 1 Filter and its cutoff are, so an LFO on
    Voice Cutoff moves every voice.
  - **No new factory routes:** the 48 slots stay as sub-project 1 left them. The expression comes
    from routes a patch adds (Velocity, Pressure, Timbre or Env 2 → Voice Cutoff) and from
    sub-project 5's Voice presets.

  *Declined:* factory routes to Voice Cutoff. They would take user slots, and Timbre is already
  routed to Position (factory route 46).
- **F8. The filter isn't counted against the budget.** The budget's ledger weighs grains. A lane
  costs about one four-wide filter step per sample whatever its grain count, and its number is
  bounded (32 a layer, 128 an instance). `gg-bench` reports its cost (§4.11). *Declined:* a ledger
  weight per busy lane. It would make a filter starve grains under load, and the overload ladder's
  three rungs are about grains (§14's order).
- **F9. A lane rings out, then frees, and the free is checked only at grid samples.** A Draining
  lane is freed at the first grid sample t (t a multiple of 32, `Processor::kControlGrid`) that
  meets every condition:
  - `t ≥ lastEnd`, the latest end its grains latched;
  - the engine isn't engaged, or no type fade is in progress and every state value of the lane's
    active filter has `|s| < 2⁻²⁰` (about −120 dB).

  It is freed in any case at `lastEnd + 4 s`. At 20 Hz and resonance 100% (Q = 10, the filter's
  slowest decay), the state falls about 120 dB in 2.2 s, so the cap never cuts anything audible.
  A freed lane's filters are reset. *Declined:*
  - freeing at the last grain's end, which cuts a resonant tail and clicks;
  - a fixed fade-out, a second rule that would still need a length;
  - a block-boundary check, which would depend on the host's block size.
- **F10. Mutate locks all four out of its defaults** (`structuralLocks`), as sub-project 2 locked
  Pan (E6) and its routing and curve parameters (E18). A default press then moves what it moved
  before, and the Voice Filter type, like Source and Mode, is structure. *Declined:* moving Voice
  Cutoff and Resonance by default. It is worth revisiting once sub-project 5's Voice presets exist,
  as a carry-over row.
- **F11. The voice filter acts on voices only.** On a Cloud layer, and on a Voice layer's orphan
  Cloud grains (those still sounding after a switch, B10), grains render direct, unfiltered by it.
  A switch from Voice to Cloud therefore hands the sound from filtered voices to unfiltered Cloud
  grains, and the voices' lanes drain through their filters. *Declined:* running the voice filter
  on a Cloud layer as one voice, which would be a third layer filter beside the two the buses
  already give.
- **F12. No resonance compensation, and V7 unchanged.** V7's `1 / sqrt(n)` is latched into each
  grain's gain before the filter, and the filter is linear, so it applies to the filtered voice
  unchanged. The resonance peak (Q up to 10, +20 dB at the cutoff) is the user's, as on the bus
  filters, and the layer's soft limiter after the chain bounds it. *Declined:* gain compensation
  by resonance, which would make Voice and bus resonance behave differently.
- **F13. The Edit view:** a line of its own at the top of the Filter panel, since the voice filter
  comes first in the signal order: Voice Filter (the line's first cell, an `editLine`), Voice
  Cutoff, Voice Resonance, Voice Key Track. Bus Split and the two bus lines follow, and the existing
  rows renumber. If the 640 × 360 frame tests' elision or reachability checks fail there, the
  builder gives it a panel of its own (`Panel::VoiceFilter`, appended to `Panel`) beside Filter,
  and records the ruling. Short labels are allowed where elision needs them; the host always sees
  the full names. Graying the controls on a Cloud layer is sub-project 5's.
- **F14. The event log is unchanged.** The lane is a rendering detail, so no `"lane"` field is
  written. The voice filter acts after the grains, as Space does, so its goldens are checked
  across platforms by their metrics alone (CLAUDE.md's rule for Space). Within one platform the
  WAV hashes and the block-size tests pin the bits.

## 3a. The builder check's answers (Claude's, binding on the plan)

- **FB1. Where the filter's law lives.** A new engine class, `gg::VoiceFilterBank`
  (`engine/include/gg/grain/VoiceFilter.h`, `engine/src/VoiceFilter.cpp`). It copies
  `BusFilter`'s law exactly:
  - the cutoff clamp `[20, min(20000, 0.45 × sr)]` Hz and its NaN rule;
  - `res = 0.95 × clamp(resonance, 0, 1)`;
  - `setCoeffForBlock<32>` glides the coefficients across each cell;
  - a type change starts the incoming type on the spare filter from silence and crossfades over
    the ramp;
  - a change back during a fade turns the fade from its current weight, with BusFilter's
    `fadePos_` arithmetic;
  - a third type waits until the fade ends.

  It runs on four channels. `core/src/fx/BusFilter.*` is **not touched or moved**, so no bus
  golden's bits can move. A test pins the bank's channels 0 and 1 bit for bit (`memcmp`) to
  `BusFilter` over the same cells, types and fades. `gg-engine` gains `sst-filters` as a PRIVATE
  link (header-only, already pinned and used by `gg-core`).
- **FB2. The type and the fade are the engine's, one controller for every lane.** All lanes switch
  together: one `type_`, `nextType_`, `fading_`, `fadePos_` and `active_`, the values
  `BusFilter` holds per instance. Each lane holds only its two `CytomicSVF`s (active and spare) and
  its cell.
  - A lane assigned during a fade initializes both filters and joins the fade.
  - A lane assigned with no fade in progress initializes its active filter and snaps its
    coefficients (`firstBlock`), as `BusFilter::jump` does.
- **FB3. Grid alignment.** While the engine is engaged, or a type request differs from the type
  in force, `Engine::process` splits its blocks so that each block starts on a grid sample (clock
  ≡ 0 mod 32), using the same splitting the read limit uses (`readLimitCheckAt_`). At most 32
  frames then lie between grid samples. At every grid block start:
  1. the requested type is applied, as `BusFilter::setCell` applies it, which may start or turn a
     fade;
  2. engagement is recomputed (F4);
  3. the free rule runs (F9);
  4. every busy lane gets `setCell` with its cell.

  The `Processor` already cuts its ranges at every cell while a voice spawns (B1), so splitting
  costs nothing extra then. While lanes only drain, the split is the engine's own. The output is
  block-size invariant either way: every decision is made at grid samples of the absolute clock,
  and each fade advances per sample. **Lane scratch is therefore 32 frames:** 32 lanes × 4
  channels × 32 floats, 16 KiB an engine.
- **FB4. How `renderPool` partitions rows.**
  - `GrainSpec` gains `uint8_t lane{kNoLane}` (`kNoLane = 0xFF`), and `GrainRows` stores it as a
    new column beside `bus`. A voice grain takes its voice's lane at admission (`spawnAs` passes
    the lane with the ordinal). A Cloud grain keeps `kNoLane`.
  - `renderPool` makes **one pass over the rows in spawn order**, as today. Each row's output is
    chosen by `(engaged && rows.lane[i] != kNoLane) ? laneOut[lane][bus] : busOutputs[bus]`. So
    each lane buffer, and the direct buffers, sum their grains in spawn order.
  - No sort, no second pass, and no change to the kernels: a lane buffer is a `kernels::Output`
    like `out`, with the same `blockStart` origin.
  - With `bus2` null (the five-argument form), a lane's bus-1 grains render into its bus-0
    channels, mirroring `BlockOutputs`' aliasing.
  - With one output channel, a lane carries source channel 0 unpanned, as `out` does.
- **FB5. The sum and its order.** After `renderPool` reaches the block's end, the busy lanes are
  taken in **ascending owner ordinal** (an insertion sort of at most 32 at each block start;
  ordinals are unique among busy lanes). Each lane is filtered sample by sample, and its four
  outputs are added: `out[c][t] += y_c`, `bus2[c][t] += y_{2+c}`.
  - Each sample's sum is therefore: the direct grains in spawn order, then each lane in ordinal
    order. That is fixed per sample and depends on neither the block split nor lane indices.
  - A merged lane sorts by its new owner's ordinal from the merge onward.
  - With `bus2` null, only `y0` and `y1` are added.
  - With `out` null or `out[0]` null, nothing is rendered, but the lanes still step on silence, so
    their ring-out and free timing stay exact.
- **FB6. A lane's state.** A lane is in one of three states:
  - **Free**;
  - **Owned**, by a spawning voice in a slot;
  - **Draining**, its voice finished or replaced, its grains or its ring-out remaining.

  It also records its owner's ordinal, `lastEnd` (raised at each admission to the new row's
  `endSample`, never lowered by a steal's shorter fade: conservative), its cell (cutoff Hz and
  resonance, kept from its owner's last `VoiceParams`), and whether a cell has been given.
- **FB7. The lane's events.**
  - **`startVoice(slot, ordinal, params)`** first runs the free rule at the engine's clock. It then
    moves the slot's previous voice, if still Owned, to Draining, and assigns a lane by F2. Last,
    it sets the lane's cell from `params`.
  - **`finishVoice` and `clearVoices`** move Owned lanes to Draining; their cells freeze.
  - **`setVoiceParams`** updates the owned lane's cell.
  - **`clearGrains`** (the enable gate, `reset()`, a source removal) sets every busy lane's
    `lastEnd` to the clear's frame, so the lanes ring out the state they hold, as bus 2's filter
    does.
  - **The enable gate turning off and `reset()`** also free every lane, reset its filters and
    disengage, at the frame the grains end.
- **FB8. The cell's values reach the engine through `VoiceParams`.** It gains `float
  filterCutoffHz{20000.0f}` and `float filterResonance{0.0f}`. `setVoiceParams` already runs at
  every applied cell for every spawning voice (B1), and `CloudScheduler` sees only `params.cloud`,
  so no onset rescale fires. `Engine::setVoiceFilterType(VoiceFilterType)` is the layer's type
  request. `gg::VoiceFilterType` (Off, LowPass, HighPass, BandPass, Notch) has `BusFilterType`'s
  values, and core pins that with a `static_assert`. `EngineConfig` gains `uint32_t
  filterRampSamples{0}`: 0 means `max(1, llround(0.01 × sampleRate))`, and the `Processor` passes
  its `rampSamples_`, so both filters fade over the same ramp.
- **FB9. The Processor's per-voice cell** (`Processor::updateVoices`, after `applyVoiceRoutes`):
  - `cutoffSlot = clampToRange(VoiceCutoff, base + mod + layer offset + voice offset + per-note
    modulation)`, NaN falling back to `base + mod` as the other voice slots do;
  - `resonance = clampToRange(VoiceResonance, …)` the same way;
  - `hz = (float) exp2Det((double) cutoffSlot + (double) keyTrack × pitchIn / 12.0)`, where
    `keyTrack` is the layer's effective Voice Key Track and `pitchIn` is the voice's (§4.5 of the
    voice spec).

  The two offsets are read and zeroed from `voiceOffsets_` as Level's is. **They are not added to
  `kVoiceTargets`** (the `CloudParams` list) **nor to the voice cache's key.** So a moving Voice
  Cutoff route never moves `voiceRecomputes()`, and a test pins that. The cell is computed for
  every spawning voice at every applied cell, whatever the type, so a lane always holds its
  voice's latest values when the type turns on (about 20 flops and one `exp2Det` per voice per
  cell).
- **FB10. B5's short tap and B10's switches.**
  - A tap's voice starts with its cell at its boundary, like its envelopes.
  - A layer switched to Voice starts no voice for notes already held (B10), so no lane.
  - A switch to Cloud finishes its voices, and their lanes drain (F11).
- **FB11. What the free rule reads:** `ic1eq` and `ic2eq` of the lane's active `CytomicSVF`,
  stored to two `float[4]` arrays, and their largest magnitude. That happens only at grid block
  starts, only for Draining lanes past `lastEnd`. `Engine::process` runs with denormals flushed
  (`ScopedDenormalsOff`), so the state reaches exact zero on its own too.
- **FB12. Sub-project 2's per-row slot** (EB6) and this sub-project's per-row lane are **two
  columns**: E9's steal order reads the slot, the render reads the lane, and neither replaces the
  other. A voice grain stolen by the ladder fades in its lane like any grain.
- **FB13. Thread pool render tasks** (the `threads` label): each layer's lanes belong to its own
  engine, so a task touches no other layer's state, and the existing "same bytes with pools of 0,
  1, 2 and 8 workers" tests extend to a render with the voice filter on.
- **FB14. "Busy" and "engaged", exactly.** A lane is busy while Owned or Draining. The engine is
  engaged at a grid block start when two things hold:
  - at least one lane is busy;
  - the type in force isn't Off, or a fade is in progress.

  Engagement changes only at grid block starts, never inside a block.
- **FB15. A type request with no busy lane takes effect at once, with no fade**, since there is
  nothing to fade. It applies at the call's first frame or at a `startVoice`, whichever comes
  first. So a Cloud layer with its type set never splits blocks, and a voice starting on a layer
  whose type is already Low-pass gets a lane that jumps to Low-pass from silence (FB2). When the
  last busy lane frees during a fade, the controller jumps to the requested type and the fade
  ends. `startVoice` applies a pending request before it assigns the lane, so the order of the
  `Processor`'s two calls at one boundary doesn't matter.
- **FB16. A lane's `kernels::Output`s:**
  - bus 0 is `{buf[lane][0], numChannels > 1 ? buf[lane][1] : nullptr}`;
  - bus 1 is `{buf[lane][2], …[3]}`, or bus 0's when `bus2` is null;
  - each has the block's `blockStart` as origin, as `out`'s has.

  The kernels then carry source channel 0 unpanned into a mono lane, as they do into a mono `out`.
- **FB17. The 4 s cap** is `llround(4 × sampleRate)` samples past `lastEnd`, checked at grid block
  starts like the rest of F9.

## 4. Design

### 4.1 Parameters (append-only IDs)

The indices agreed for the parallel M3 sub-projects:
- sub-project 2: globals 0x25 up, layer 0x32 to 0x37;
- sub-project 3: globals 0x30 up, layer 0x38 up;
- sub-project 4: globals 0x38 up, layer 0x3C up;
- sub-project 3b: layer 0x68 to 0x6F;
- **this one: layer 0x74 to 0x77, no global.**

| Row | CLAP ID (A … D) | Display name | Range, default | Flags and roles |
|---|---|---|---|---|
| `VoiceFilter` | `0x010074` … `0x040074` | Voice Filter (A Voice Filter …) | stepped 0 to 4: Off, Low-pass, High-pass, Band-pass, Notch (`busFilterType()`); **Off** | automatable, modulatable, stepped; a route target layer-wide (as Bus 1 Filter is), not a voice target; Mutate-locked |
| `VoiceCutoff` | `0x010075` … | Voice Cutoff | log2 Hz, 20 Hz to 20 kHz, 1 kHz (`busCutoff()`) | automatable, modulatable; a route target and a **voice target**; a per-note slot (with sub-project 2); Mutate-locked by default |
| `VoiceResonance` | `0x010076` … | Voice Resonance | 0 to 1 (percent), 0 | as Voice Cutoff |
| `VoiceKeyTrack` | `0x010077` … | Voice Key Track | 0 to 1 (percent), 0 | automatable, modulatable; a route target layer-wide, not a voice target; Mutate-locked by default |

- **The list.** A new list, `GG_LAYER_PARAMS_M3_VOICEFILTER(X)`:
  `X(VoiceFilter, 0x74, "Voice Filter") X(VoiceCutoff, 0x75, "Voice Cutoff")
  X(VoiceResonance, 0x76, "Voice Resonance") X(VoiceKeyTrack, 0x77, "Voice Key Track")`. It goes
  **after `GG_LAYER_PARAMS_M2_EXTREMES`**, and after sub-project 3b's 0x68 list and any 0x70 list,
  in both `GG_LAYER_PARAMS` and `GG_LAYER_PARAMS_SINCE_M1`, keeping the index column ascending
  (`validIndexColumn`).
- **The reserved block.** `detail::isReservedIndexInUse` gains `(scope >= 1 && index >= 0x74 &&
  index <= 0x77)`. `kNextFreeLayerIndex` and `kNextFreeGlobalIndex` are untouched (they count
  indices below 0x40).
- **The counts.** `kLayerParamCount` 63 → 67 and `kParamCount` 290 → 306 on this branch. Merges
  add the other branches' counts. Every hard-coded count follows: `tests/support/ParamTable.h`,
  `patch_tests`, `identity_tests`, `params_tests`, `rt_tests`, `EditLayout` and its tests, the
  sweep floors (with their reasons), and the `ParamSlot` comment's tallies.
- **No patch-version bump.** A state without the four values reads their defaults, and Off
  renders sub-project 1's bits. The builder confirms in `PatchState` and records it. A preset
  saves the four values: they are the sound, not the instance's.
- **AU parameter order** is ascending ID, unfrozen until M5 (§18), so the four take their place
  after each layer's 0x67 to 0x6x rows.

### 4.2 The engine: lanes

`Engine` gains:
- `VoiceFilterBank lanes_` (FB1, FB2): 32 lanes, each with two `CytomicSVF`s, its state, owner
  ordinal, `lastEnd`, cell and given flag; the global type controller; and the scratch
  `float laneBuf_[32][4][32]`;
- per voice, `Voice::lane`;
- `requestedFilterType_` and `engaged_`.

Every array is fixed and allocated at construction, and `process()` allocates nothing.

- `processBlock`, engaged:
  1. zero the busy lanes' scratch for the block's frames;
  2. spawn as today, each voice grain taking its lane;
  3. `renderPool` with the lane outputs (FB4);
  4. the lane pass (FB5);
  5. `clock_ = blockEnd`;
  6. retire, as today.
- `processBlock`, not engaged: today's code exactly, with the lane column written at admission.
- **Grid blocks** (FB3): `process()` caps each block at the next grid sample while
  `engaged_ || requestedFilterType_ != the type in force` (the type in force counts a pending
  fade).
- **The free rule and the merge** (F2, F9) run at grid block starts and at `startVoice`.

### 4.3 The Processor

- `Processor::updateVoices` computes each spawning voice's cell (FB9) into its `VoiceParams`.
- At each applied boundary the effective Voice Filter, rounded as `busParams` rounds a bus type,
  is passed to `l.engine.setVoiceFilterType` (FB8). Cloud layers pass it too; their engines have
  no busy lane, so it is never engaged there.
- The `Processor`'s bus filters, diffuser, Level and chain are untouched.
- The rest of the core:
  - `ModMatrix.h`: `isVoiceTarget` gains Voice Cutoff and Voice Resonance (F7); `isValidTarget`
    admits all four.
  - `ParamMap`: a `voiceFilterParams(effective, layer)` beside `busParams`, giving the type and
    key track.
  - `Mutate.cpp`: `structuralLocks` gains the four, in every layer (F10).

### 4.4 Determinism

- **Fixed decision points.** Every decision is a function of the schedule, made at grid samples
  of the engine's absolute clock: the type in force and its fades, engagement, frees, merges and
  cells. Each fade advances per sample. The coefficients glide per cell by `setCoeffForBlock<32>`,
  as the bus filters do (M2 sub-project 4).
- **Fixed sum order.** The order is fixed per sample (FB5), so the output is bit-identical at any
  host block size: 1, 32, 64, 255, 256 and 4096 are tested.
- **No new randomness.** Lanes are assigned by index and ordinal alone.
- **Cross-platform.** The filter's arithmetic is `CytomicSVF`'s (`fasttan`, a polynomial; SIMD
  adds and multiplies), as on the bus filters, and the cutoff's `exp2Det` is the project's
  deterministic one. No libm call is added to anything the event log sees.

### 4.5 Real-time safety

- Fixed arrays only: the lanes, the scratch and the sort buffer (32).
- The lane pass is bounded: 32 lanes × 32 frames a block.
- The free rule reads eight floats a lane.
- `process()` and `params.flush` stay allocation-free, lock-free and RTSan-clean. The tripwire
  (`rt`) covers a render with the voice filter engaged, with type fades, merges and frees.
- 0- and 1-sample blocks are handled: a 1-sample block on a grid sample does the grid work; one
  off-grid continues the cell.

### 4.6 The at-rest rule

- **No golden moves.** With every layer's Voice Filter at Off (the default, and every existing
  spec, preset and state), the engine is never engaged. It renders the direct path, the code
  sub-project 1 shipped, with one new column written. Every existing golden keeps its WAV SHA-256:
  the 108 M2 goldens, sub-project 1's eight Voice goldens, and whatever the parallel branches have
  merged.
- **The check.** `scripts/update-goldens.sh` runs before and after every task, with sub-project
  1's `check-wav-hashes.sh` against this branch's starting baseline. The final report says every
  hash matches.
- **Off with cells given.** A test renders sub-project 1's voice schedules (chords, steals,
  dups, Bus Split 50%, mono, the five-argument form) twice: once with the new APIs never called,
  and once with random cells given every cell but the type kept Off. It requires the two outputs
  `memcmp`-equal.
- **Cloud layers.** The type set to Low-pass on a Cloud layer also renders Cloud's bits.

### 4.7 Memory and cost

- **Memory.** About 40 KiB per engine: 32 lanes × two `CytomicSVF`s (about 0.7 KiB a lane), plus
  16 KiB of scratch. That is about 160 KiB an instance.
- **Cost per busy lane.** Per sample: one four-wide SVF step (two during a fade), four loads and
  four adds into the buses. Per block: zeroing its 4 × 32 scratch. Per grid sample: the cell's
  `setCoeffForBlock` (a `fasttan`).
- **The estimate:** about 3 to 5 ns per lane-sample, so 16 busy lanes cost roughly 0.3% of a core
  per layer at 48 kHz. Against X's 2,000-grain workload that is under 2%. `gg-bench` measures it
  (§4.11).
- **Cloud mode, and Off, cost nothing new** but the lane column's byte, so X's ratio is
  unchanged: the gate's baselines are re-read, not remade.

### 4.8 `gg-render`

- **`params`** take `"A Voice Filter"` to `"D Voice Filter"` (0 to 4), `"A Voice Cutoff"` (log2
  Hz), `"A Voice Resonance"` and `"A Voice Key Track"` (0 to 1), and B to D's; they are automatable
  in `automation` too.
- **`mod`** routes take the targets `"Voice Filter"`, `"Voice Cutoff"`, `"Voice Resonance"` and
  `"Voice Key Track"` by layer A's name, with `targetLayer`.
- **Note `paramMods`** (sub-project 2's key) accept `"A Voice Cutoff"` and `"A Voice Resonance"`
  once both branches have merged.
- **Preset sources** (`--pack-preset`) accept the four as sound parameters.
- **Nothing else changes:** no key is added to the event log.

### 4.9 Goldens

Seven new goldens, 6 to 8 s each, at block 256 and seed 1 unless noted. Each catches the 16
injected click steps or joins the click-injection table with its reason.

- **`inst-voice-filter-velocity`:** layer A Voice over noise (a sweep at a fixed Position is one
  tone, as `inst-filter-lfo` found). Low-pass at 200 Hz, Resonance 0.5, and Velocity → Voice
  Cutoff +5 octaves. Three notes at velocities 20, 64 and 127 in turn, then the three together as
  a chord: each voice keeps its own brightness.
- **`inst-voice-filter-env`:** Env 2 → Voice Cutoff +6 octaves, with Env 2 at Attack 5 ms,
  Decay 400 ms, Sustain 0.2. Low-pass, Resonance 0.7. Four notes staggered 0.5 s apart and held:
  each note sweeps on its own.
- **`inst-voice-filter-mpe`:** MPE On; two notes held on channels 2 and 3. CC 74 sweeps 0 → 1 → 0
  on channel 2 alone, through a Timbre → Voice Cutoff route, and channel 3's pressure drives
  Voice Resonance. The gap's headline: one note brightens while the other holds.
- **`inst-voice-filter-keytrack`:** Key Track 100%, Band-pass at 1 kHz, Resonance 0.8. A chord on
  C2, C4 and C6, then a +12 bend on the C4 voice: each band follows its own note.
- **`inst-voice-filter-switch`:** four held voices. Voice Filter is automated Off → Low-pass (1 s)
  → High-pass (2 s) → Notch (3 s) → Off (4 s), with Bus Split 50%, Bus 1 Low-pass and Bus 2
  High-pass: the declick fades, the composition with the buses, and the return to the direct
  render.
- **`inst-voice-filter-steal`:** 40 taps 60 ms apart, Size 1.5 s, Env 1 Release 50 ms,
  Resonance 0.9, velocities alternating with Velocity → Voice Cutoff. More than 16 lanes drain at
  once, so lanes merge (F2) and voices are stolen, and none of it clicks.
- **`fx-voice-filter`:** the FX, layer A Voice over a live stereo sweep. Three notes with
  Velocity → Voice Cutoff and Key Track 50%.

Every spectrogram is looked at before committing (CLAUDE.md). The event logs are byte-identical
to the same specs' logs with the filter Off. A test reads that from each case: the voice filter
never touches a grain.

### 4.10 Tests

Tests are labelled `voicefilter`, with `engine` or `core` as the area.

- **Engine (`tests/engine/voice_filter_tests.cpp`):**
  - **Off never reroutes:** §4.6's `memcmp` test, and a Cloud layer with the type set.
  - **The partition is exact.** With the voice filter engaged, the engine's output equals a
    reference built from its own grain events. The test renders each voice's grains with the
    scalar reference kernels into its own buffer in spawn order, filters each with a
    `VoiceFilterBank` lane given the same cells, then sums the direct grains and the lanes in
    ordinal order. The result must be `memcmp`-equal to the engine's. So one voice's cutoff
    gesture changes that voice's lane alone.
  - **A lane's life:** assigned lowest-index first; a slot reused by a cap steal gets a different
    lane; Draining frees at the first grid sample past `lastEnd` with its state below 2⁻²⁰, and at
    `lastEnd + 4 s` at the latest; the merge takes the oldest draining lane and glides without a
    step (max sample delta bounded); `clearGrains`, the gate, `reset()` and `clearVoices` behave as
    FB7 says.
  - **Fades:** Off → type, type → type, the turn-back, a third type waiting, type → Off and the
    return to the direct render, each with no step above the click bound. During the
    disengagement the direct and lane sums differ by at most a few ulps.
  - **`VoiceFilterBank` against `BusFilter`:** channels 0 and 1 bit-identical over random cells,
    types and fades.
  - **Block-size invariance** (1, 32, 64, 255, 256, 4096) with engagement, fades, merges, frees,
    Bus Split, mono and the five-argument form.
- **Core (`tests/core/voice_filter_processor_tests.cpp`):**
  - the per-voice cell from Velocity, Key Track (exactly `keyTrack × pitchIn / 12` octaves), Env 2,
    Pressure and Timbre;
  - B2's no double count;
  - a layer-wide LFO → Voice Cutoff moving every voice;
  - `voiceRecomputes()` unmoved by a Voice Cutoff route (FB9);
  - Cloud layers untouched;
  - the type request at its boundary;
  - Mutate's defaults leaving the four alone;
  - the thread-pool invariance (FB13);
  - with sub-project 2 merged, per-note modulation of Voice Cutoff reaching one voice.
- **Parameters:** the four rows, IDs, ranges, defaults and flags; the reserved-index pin;
  `isValidTarget` and `isVoiceTarget`; a state without the values loading the defaults.
- **Plugin and `rt`:** the tripwire with the voice filter engaged; the four in param info;
  `params.flush` with them.
- **UI:** the Edit view's placement (F13). The frame tests (every parameter reachable, no label
  elided or colliding at 960 × 600 and 640 × 360, both themes, the keyboard) cover the four, and
  `gg-snap`'s set is regenerated and looked at.
- **Sweep harness:** one new case, `voice-filter-extremes`:
  - 16 voices at Resonance 100%;
  - Voice Cutoff swept over its whole range at the cell rate through Env 2 and an LFO;
  - the type stepped every 100 ms;
  - Key Track 100% over keys 0 to 127.

  It checks finiteness, the peak bound and the click score, with its scored-share floor pinned and
  its reason recorded.
- **`gg-render`:** each key of §4.8, and an unknown voice-filter name refused (exit 2).
- **The gates:**
  - click injection for the seven goldens;
  - `scripts/check.sh` (dev, asan, rtsan; CI's RTSan where the machine has none);
  - the validators;
  - the at-rest hashes in the final report.

### 4.11 Benchmarks (report-only)

- **`engine-voices-filter`:** sub-project 1's `engine-voices` setup (X's stream split across 16
  voices) with every voice's lane engaged: Low-pass, 1 kHz, Resonance 0.5, a fixed cell. It is
  reported beside `engine-voices` with its ratio to X, and has no baseline entry yet. The added
  cost is that ratio minus `engine-voices`'.
- **`--chain` gains a `voice-lane` stage:** one lane's four-wide step with its cell glide, in ns
  per lane-sample, the figure §4.7 estimates.
- **The full run grows by three configurations,** about 30 s. CI's step limits are unchanged.
- **The idle figures** are measured at the close, as M2's were.

## 5. Interactions with the parallel sub-projects

- **Sub-project 1 (Voice, finishing):** this branch takes `m3-voice` as built, V7 included. If
  sub-project 1's last tasks change `scheduleVoices`, `startVoice` or `renderPool`, the merge
  re-applies FB4 and FB7 to them. Its Voice goldens are the at-rest set.
- **Sub-project 2 (expression):**
  - Its E7 per-note slots (23 a layer, "exactly the 92 slots" in its plugin test) gain Voice Cutoff
    and Voice Resonance: 25 a layer, 100 in all. Its `isVoiceTarget` additions (Pan, Bus Split, the
    eight envelope controls) and these two merge as one list. Whichever branch lands second
    updates the count and the test.
  - Its EB6 per-row slot and this branch's per-row lane are separate `GrainRows` columns (FB12).
  - Its Pan acts per grain before the lane, so it composes.
  - Its E9 and E10 steals shorten grains or finish voices, and the lanes follow.
- **Sub-project 3 (pitch-sync):** Clock = Pitch voices render into lanes like any others. Key
  Track follows `pitchIn`, not the layer's Pitch (the formant), so a formant sweep doesn't move
  the cutoff (F6). No index overlap.
- **Sub-project 3b (playhead):** its 0x68 to 0x6A list precedes this one's. Both lists follow
  `GG_LAYER_PARAMS_M2_EXTREMES`, and the merge keeps them in index order. Both touch
  `isReservedIndexInUse`: the conditions OR together.
- **Sub-project 4 (tuning):** owns the key term in `pitchIn`. Key Track reads `pitchIn` as built,
  so a Scala scale's step moves the cutoff by its own interval at 100%.
- **Sub-project 5 (views and presets):** may move F13's placement, gray the controls on Cloud
  layers, add a remote page, and use the filter in its Voice presets (velocity and timbre to
  cutoff are the obvious routes). Revisiting F10's locks once those presets exist is a carry-over
  row.
- **Sub-projects 7, 8, 10 and 11:**
  - 8 (grain shape) may add `GrainRows` columns too; they merge side by side.
  - 10 (scenes) recalls these as sound parameters; a recalled type change takes FB2's fade.
  - 11 (color) places its stages relative to the bus filters, not inside the lanes. If it wants
    drive per voice, it reads this spec's lane pass as the place.
  - 7 (tempo sync) doesn't interact.
  - None of their index ranges overlaps 0x74 to 0x77.
- **All:** the counts in `ParamIds.h` and CLAUDE.md, and the §23 row numbers, are settled at
  merge.

## 6. INTENT changes (Part II, each with a §23 row numbered at merge)

- **§14, Voice mode:** a paragraph on the voice filter:
  - the four parameters;
  - the lanes and their life (F2, F9);
  - the composition with the buses (F3);
  - the Off rule (F4);
  - the cell law and Key Track (F6, FB9);
  - the per-voice targets (F7);
  - not counted against the budget (F8);
  - voices only (F11).
- **§14, Grain buses:** "True per-grain filters come later" gains "(a per-voice filter, once per
  voice per sample, is built in M3; per-grain filters stay M5's)".
- **§15:** `sst-filters` is linked by `gg-engine` too, PRIVATE (FB1).
- **§16:** `engine-voices-filter` and the `--chain` `voice-lane` stage, report-only, with their
  idle figures at the close.
- **§17:** the Edit view's Filter panel and its voice line (F13).
- **§18:** the four layer parameters and their IDs; no patch-version change; a preset saves them;
  Mutate's default locks (F10).
- **§20, the M3 row:** sub-project 9, a filter per voice. The top controller adds it with the
  other gap sub-projects (3b, 7 to 11), per the director's decision of 2026-11-07.
- **§22:** the per-grain filter row stays resolved; a cross-reference says why a per-voice filter
  doesn't reopen it (cost per voice, not per grain).
- **§23:** one row: F1 to F14 and FB1 to FB17, marked as Claude's provisional calls, and the
  director's decision that brought the gap into M3.
- **Carry-over** (`docs/m3-carry-over.md`):
  - F10's Mutate locks, to revisit with sub-project 5's Voice presets;
  - a remote-controls page for the voice filter (sub-project 5).

## 7. Left to the builder

- The goldens' exact gestures within §4.9: each catches the 16 steps or is pinned with its reason,
  and every spectrogram is looked at.
- F13's placement, or the fallback panel if the frame tests demand it, with short labels if
  elision needs them.
- The `voice-filter-extremes` sweep case's floor, and any other sweep floor that moves, each with
  its reason.
- `engine-voices-filter`'s and `voice-lane`'s first figures, in the final report beside X (on an
  idle machine where possible; the close's idle run publishes them).
- Confirming in `PatchState` that no version bump is needed, and recording it.
- Any engine change sub-project 1's remaining tasks make to `scheduleVoices`, `startVoice` or
  `renderPool`, followed as §5 says.
