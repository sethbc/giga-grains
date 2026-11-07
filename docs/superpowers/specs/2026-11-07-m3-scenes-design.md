# M3 sub-project 10: scenes, snapshots within an instance

| | |
|---|---|
| **Status** | Draft, 2026-11-07, written without the director's input at his request ("keep building all of the sub-projects as parallel as possible … progress as much as you can without input"). Every forced choice is **Claude's provisional call** (N1 to N15), and so is every builder ruling (NB1 to NB16); the director reviews them later |
| **Program** | `docs/superpowers/specs/2026-11-07-m3-program-design.md` (§3, §9). This sub-project was added to M3 by the director on 2026-11-07 ("no i think this all needs to happen in m3"): gap 8 of the competitor comparison (the Torso S-4's scenes and punch-ins, the GR-MEGA's multi-presets) |
| **Base** | M3 sub-project 1, the Voice slice (`docs/superpowers/specs/2026-11-07-m3-voice-design.md`; branch `m3-voice`, draft PR #37) |
| **Branch** | `m3-sp10-scenes` (from `m3-voice` at `18ac47d`) |
| **Prefix** | N (forced choices N1…, builder rulings NB1…) |
| **CLAP indices** | globals **0x51 to 0x57** (seven parameters); no layer parameters. See N14 on 0x50 |

## 1. Intent

A performer keeps up to eight sounds inside one instance and moves between them while playing: a
**scene** is a captured set of the sound's parameter values. A scene is recalled at once, over a
glide time, or on the next beat or bar of the host's transport. It can be recalled from a host
parameter (automatable), from a MIDI Program Change, or from the editor. Two scenes can be morphed
with one control, and a momentary **punch** jumps to a scene and returns on release. INTENT §6
borrows Portal's macro morphing, and §7 serves live performers; the S-4 and the GR-MEGA have this
and we don't.

**At rest nothing moves.** With the bank empty and no scene control ever touched, every one of the
116 goldens (68 cases and 48 preset goldens on `m3-voice`) keeps its WAV SHA-256.

## 2. Scope

**In:**
- The scene bank, eight scenes, saved with the session and carried by presets.
- Seven global parameters: Scene, Scene B, Scene Morph, Scene Punch, Scene Quantize, Scene Glide
  and Scene Store.
- Recall: immediate, glided, or quantized to the transport.
- The morph between two scenes, the punch, and the store.
- Program Change input.
- The editor's commands (store, clear, recall) as a plugin API, with no buttons.
- The plugin's report of recalled values to the host.
- A "Scenes" remote-controls page and a "Scenes" panel in the FX view (so the frame tests place
  every parameter).
- The `gg-render` keys, the goldens and the tests.

**Out:**
- Scene buttons and names in the editor, the editor recall's undo entry, and scene presets among
  the factory presets: all sub-project 5's.
- Chained or probabilistic launches (the S-4's next, walk, random and chance).
- More than eight scenes.
- Routes, samples or rings in a scene.
- Editing which parameters a scene holds (the format keeps a mask; no UI sets one).
- Macro punch-ins and the S-4's TEMP snap-back of any control (N8).
- Glide times in beats (sub-project 7, or M4).
- Scene Morph as a modulation target (N13).

## 3. Forced choices (Claude's provisional calls; the director may overrule each)

- **N1. Eight scenes.**
  - Eight fit a row of pads and Program Changes 0 to 7. A stepped parameter of nine values (None,
    1 to 8) keeps its host steps coarse, and sub-project 5 has eight buttons to draw.
  - *Declined:* 16 (more slots than one pad row, and twice the UI); 128, the S-4's (a bank
    browser, not performance slots).
  - Growing the count later changes Scene's and Scene B's ranges, which moves VST3 and AU
    normalized automation. So any growth must happen before parameter IDs freeze at
    `0.5.0-beta.1`, and after that only by a new parameter.
- **N2. A scene holds base parameter values and nothing else.**
  - A scene holds no routes, no samples, no ring audio and no seed.
  - **Which parameters:** every **automatable** parameter except **Budget** and the seven scene
    controls (`isSceneSlot`, §4.2). That excludes, by their flags:
    - Ring Length (not automatable: a recall must never resize a ring);
    - the MIDI settings (MPE, MPE Bend Range, Bend Range, and sub-project 2's zone settings);
    - sub-project 4's MTS-ESP.

    It includes every sound parameter: the macros, Layer On, Morph X and Y, Mode, Source,
    Freeze, Quality, and the four automatable ring parameters (Loop, Loop Length, Overdub,
    Splice). Those four play the instance's own material, which a scene stays inside. A preset's
    scenes drop them (N11), as a preset load does.
  - Budget is the instance's CPU setting, not the sound, so it is excluded by name.
  - The rule needs no coordination: each parallel sub-project's parameters fall in or out by
    their flags.
  - **A scene captures every scene slot** (a full snapshot). The saved format keeps a
    per-parameter mask, so a scene saved before parameter X existed leaves X alone when recalled.
  - Base values, not effective ones: modulation and routes stay live on top.
  - *Declined:* routes in a scene (a route is a structure, which can't morph and would switch
    whole matrices at the midpoint); the effective values (they would bake in modulation, which
    the routes then add again); partial scenes set from a UI (later, if wanted: the format allows
    them).
- **N3. A recall writes base values (the write model).**
  - A recall is every scene parameter's base value set at one grid boundary, as if the host had
    sent all of them at that sample. So:
    - the parameters' own smoothing applies: Mix, Level, Diffusion, Feedback, Reverb Amount,
      Morph and Layer On ramp over 10 ms, and grain parameters latch per grain;
    - the host and the editor show what plays, the session saves what plays, and the host can
      record it.
  - *Declined:* an overlay (`effective = clamp(lerp(base, scene, w) + mod + routes)`). It is
    non-destructive, and modulatable from an LFO. But a knob under an overlaid parameter would do
    nothing audible, and the host would show a value that isn't playing.
- **N4. What recalls.**
  - **A change of Scene's value:** its rounded value at a boundary differs from the one in
    effect. A host re-sending the same value never re-recalls.
  - **A Program Change:** always, the same scene included, so pressing it again reverts tweaks.
  - **The editor's Recall command:** always.
  - Scene 0 is "None": it recalls nothing and cancels a pending quantized recall.
  - A recall of an empty scene writes nothing.
  - A state, preset or undo restore (`setBases`) triggers nothing.
  - A `setBase` of a scene control (the host's `params.flush` outside processing) acts at once,
    with no glide or quantize.
- **N5. The morph runs from Scene (A) to Scene B, at Scene Morph m.**
  - **The law,** for a slot in both scenes' masks, in float with no FMA (`gg::fp-contract-off`
    is the build's default):
    - a continuous slot: `f = (m == 1) ? b : a + m × (b − a)`, exact at both ends;
    - a stepped slot (`CLAP_PARAM_IS_STEPPED`): `f = m < 0.5 ? a : b`;
    - then clamped to its range.
  - A slot in A's mask but not B's takes a. A slot in B's mask but not A's is left alone.
  - A morph needs both scenes filled (`A_eff`, NB2, and Scene B). Otherwise a morph change
    writes nothing.
  - **A recall applies f, not plain A.** With Scene B at None, the default, f is A exactly.
  - **A change of Scene Morph or Scene B writes only the slots whose f changed** (bit for bit).
    So the slots on which A and B agree keep the user's tweaks, and choosing Scene B at m = 0
    writes nothing.
  - *Declined:* morphing every slot on every move (it erases every tweak at the first nudge);
    a morph "from here" (current values to B), which leaves Scene's choice meaningless.
- **N6. Scene Glide (0 to 16 s, default 0) is a recall's time.**
  - The recalled slots move from their values at the recall's boundary r to their targets f,
    cell by cell: at each applied boundary c, `p = (c − r) / G` in double, with
    `G = llround(Glide × sampleRate)` samples.
    - A continuous slot is `start + float(p) × (target − start)`, and exactly the target once
      p ≥ 1.
    - A stepped slot switches at p ≥ 0.5.
  - Only recalls glide. A morph move, a punch and its release, and a store act at once (the
    morph's own automation is its time course).
  - **Any other write to a gliding slot takes it out of the glide:** a host or editor event, a
    morph move, a punch. The user's hand wins.
  - A new recall replaces the whole glide, starting from the values in effect.
  - *Declined:* re-timing the per-parameter 10 ms ramps (only eight kinds of parameter ramp, and
    the grain parameters would still jump).
- **N7. Scene Quantize: Off, Beat, Bar, 2 Bars, 4 Bars (default Off).**
  - Quantize defers a recall from a Scene change, a Program Change or the Recall command to the
    next line of its division on the host's transport (§4.4).
  - With the transport stopped, or with no beat timeline, a recall applies at once. Waiting with
    no line ahead would be worse.
  - Morph, punch and store never quantize.
  - A bar is the transport's `beatsPerBar` beats, as the Bar source reads it.
  - *Declined:* finer divisions (1/16 to 1/2 beat: little use for a sound change); triplets and
    dotted values (sub-project 7's synced vocabulary may add them, as appended values).
- **N8. Scene Punch (Off, On) is a momentary jump to Scene B.**
  - Off → On writes scene B's own values (f at m = 1) at once, to the slots of B's mask whose
    base differs, remembering each one's value before. It needs no Scene A.
  - On → Off writes those values back to the slots the punch still owns. Any other write during
    the punch (an event, a recall, a morph move) takes its slot out.
  - A pad mapped by the host to Scene Punch gives the S-4's punch-in. Scene Morph is the gradual
    form of the same move.
  - **Macro punch-ins and TEMP snap-back are not here.** A host's momentary mapping to a macro
    covers the common case (press 1, release 0). A true "release returns any control" is an
    editor gesture (a modifier-drag), offered to sub-project 5, with no parameter needed.
  - *Declined:* Scene Punch as a 0 to 8 selector (a pad would need a mapped range to reach one
    scene).
- **N9. Scene Store (Off, On) captures on an Off → On edge**, into the scene Scene names
  (nothing at None).
  - There is no snap-back: to store again, switch it off and on.
  - A sweep of the switch is only 0 and 1, so it never stores into a scene the user passed over.
    That would happen with a 0 to 8 "store into" dial swept in a host's generic UI.
  - A snap-back to Off was declined too: a host's automation re-sending On would store every
    time.
- **N10. Program Change, on any channel (omni).**
  - Programs 0 to 7 recall scenes 1 to 8; programs 8 to 127 and Bank Select are ignored.
  - A Program Change follows Quantize and Glide, and sets Scene's value, which the host is told.
  - Scenes are instance-wide, so they ignore sub-project 2's per-layer note channels.
  - *Declined:* a Scene Channel setting (no index left in the range, and a host routes MIDI per
    track).
- **N11. Saved in `PTCH`, and carried by presets.**
  - The bank is a `<scenes>` element in the patch XML, keyed by CLAP ID as `<p>` is (§4.7). There
    is no patch-version bump: older readers ignore it, and a state without it has an empty bank.
  - **A preset carries its bank.** A preset load replaces the instance's bank with the preset's,
    or an empty bank when it has none, which is every factory preset today. Each scene's mask
    drops the four ring parameters, as a preset load keeps them.
  - **The scene controls load as stored,** with Scene Punch and Scene Store forced Off at every
    load (their momentary state isn't saved).
  - **INIT** is an empty bank with the controls at their defaults.
  - *Declined:* a preset load keeping the instance's bank. That bank's scenes would be recalled
    over the new preset's routes and samples, as other sounds than the ones stored.
- **N12. How a recall meets host automation and undo.**
  - **The hand wins.** At one boundary a host or editor event on a slot overrides the scene's
    write. A slot with its own automation lane follows its next point.
  - Automate Scene (and Scene Morph), not the scene parameters' own lanes.
  - **The recall reaches the host** as `CLAP_EVENT_PARAM_VALUE` out-events with no gesture. A host
    then updates its display; whether it records them is the host's (a carry-over row).
  - **Undo:** scene actions push no undo entry in this sub-project. The audio thread and the host
    push nothing, as today. Sub-project 5 adds one whole-state entry for a recall made from the
    editor, through the report of §4.6.
  - **Whole-state undo entries never carry the seven scene controls.** That covers Mutate and a
    preset recall. Undoing a Mutate press therefore never sends a Scene change that recalls.
  - *Declined:* sending gestures around a recall's values. Hosts that record within gestures
    would write 283 automation lanes for one press, which then fight the Scene lane.
- **N13. The seven are automatable, not modulatable, and no route targets them** (as the MIDI
  settings and the macros aren't targets).
  - A morph's writes are base values that the host is told of, so a CLAP param-mod event on Scene
    Morph would turn modulation into base writes. The mod event is ignored instead.
  - *Declined:* a modulatable Scene Morph. It is possible only with N3's declined overlay, so
    note it for after M3.
- **N14. The indices.**
  - The dispatch gave this sub-project globals 0x50 to 0x57. **0x000050 is Budget** (M2
    sub-project 3, frozen in INTENT §14). So the seven take **0x51 to 0x57**, exactly the range's
    rest, and 0x50 stays Budget's. The other sub-projects' ranges (2: globals 0x25 up, layers
    0x32 to 0x37; 3: globals 0x30, layers 0x38; 3b: layers 0x68 to 0x6A, globals 0x40 to 0x47
    unused; 4: globals 0x38, layer 0x3C) don't meet it.
- **N15. The minimal plugin surface.** So the director can use scenes from a host before
  sub-project 5:
  - the seven parameters;
  - a 14th remote-controls page, "Scenes" (CLAP and Bitwig's controller pages);
  - a "Scenes" panel in the FX view, the last, after MIDI (so the frame tests' "every parameter
    is reachable" holds; sub-project 5 may move them into its buttons);
  - three editor commands on the existing `uiCommand` path (Store, Clear and Recall scene k);
  - a scene status in `UiSnapshot` (§4.6).

## 3a. The builder check's rulings (Claude's, binding on the plan)

- **NB1. The order inside one boundary's group.**
  1. The group's scene-control events, Program Changes and scene commands are committed.
  2. A running glide takes its step for b. Then a **recall**, a **morph** change and a **punch**
     edge compute their writes, in that order, each later write overriding an earlier one on its
     slot. A recall's glide starts now.
  3. The group's other explicit base events are committed, overriding the writes and leaving the
     glide and the punch's return set for their slots.
  4. A **Store** edge or command captures the bases as they now stand.
  5. Clear commands apply.

  Then the effective values, as today. Of a Scene event and a Program Change in one group, the
  later one in the call's merged order wins, as "the latest event per parameter wins" does today.
  With sub-project 2's event numbering (its E8), that is the host's order, so it is the same for
  the same input.
- **NB2. The scene in effect, `A_eff`,** is the scene of the last recall applied, not Scene's
  value. While a quantized recall waits, Scene already shows its target, and the morph and the
  punch still use `A_eff`. Store uses Scene's value (the user's selection). A state load sets
  `A_eff` to the loaded Scene.
- **NB3. A quantized recall waits** in one slot. The latest request replaces it, and Scene 0
  cancels it. `setBases`, `reset()` and a deactivate drop it, and drop the glide and the punch's
  return set; the bank survives `reset()`.
- **NB4. Every boundary is applied while scenes are busy** (a glide running, or a quantized recall
  waiting), as for time-varying routes and sounding voices. `applyCellsBelow`'s condition becomes
  `timeVarying_ || voicesSounding_ || scenesBusy_`. At rest `scenesBusy_` is false, so the pieces
  are today's.
- **NB5. The report.** After each `process()`, the `Processor` holds a bitset of the slots whose
  latest base value (pending events included) came from a scene action in this call, or from a
  Program Change setting Scene, and changed bit for bit. The plugin first stores the host's event
  values as today, then each reported slot's latest base into `values_`. It sends each as
  `CLAP_EVENT_PARAM_VALUE` at the call's last frame (frame 0 in a 0-frame call). What `try_push`
  refuses waits for the next
  call, so a pending bitset lives in the plugin. `params.flush` (active) does the same through its
  `out`.
- **NB6. Who owns the bank.**
  - The **audio thread's copy** is the `Processor`'s, and it is the only writer while the plugin
    is active (store, clear).
  - The **main thread's mirror** is `scenes_` plus the names, used for the save.
  - **Loads** go main → audio inside `loadedState_`, with the epoch it already carries
    (`setScenes(bank, epoch)`).
  - **Captures and clears** go audio → main through a `LatestValue<PublishedScenes>` the
    `Processor` pushes after each change, carrying the epoch of the bank it changed. The plugin
    takes it in `on_main_thread` (after a `requestCallback`), in `stateSave` before writing, and
    in `deactivate`. It ignores one older than its last load's epoch, and marks the state dirty.
  - **While inactive,** the main thread alone edits `scenes_`. `activate()` hands it over, as it
    hands the routes over.
- **NB7. The inactive `params.flush`** recalls (or stores) on `values_` and `scenes_` with the
  same pure functions the `Processor` uses (`Scenes.h`), at once, and sends the written values
  through the flush's `out`.
- **NB8. Names** are main-thread only: at most 64 bytes, cut to a whole UTF-8 character as
  `patchName` cuts, empty by default. A Clear command from the editor clears the name too (the
  main thread does that when it sends the command). Store doesn't rename.
- **NB9. The stepped rounding** of Scene, Scene B and Scene Quantize is `floor(clamp(v) + 0.5)`,
  the existing stepped helper (`ParamMap.cpp`). An on/off value is on at 0.5 and above, as
  Freeze is.
- **NB10. A recall that changes Mode, Source or Layer On** acts as automation of them does:
  - a Voice → Cloud switch stops that layer's voices (as a steal);
  - Source switches the material at the boundary;
  - Layer On ramps.
  - Freeze is armed by `syncFreeze` after the group, as today.
- **NB11. Voice layers:** a recall changes the layer's values, and each voice's `CloudParams`
  is recomputed by the existing change detection. Per-voice route offsets and sub-project 2's
  per-note modulation stay on top.
- **NB12. A glide's endpoints and length are latched at r:**
  - `start` is the base in effect after step 1 of r's group (so before its explicit events);
  - `target` is f at r;
  - G is from Scene Glide at r, so automating Scene Glide affects the next recall;
  - the progress p uses absolute samples, so the glide is block-size invariant.

  Scene Quantize is latched the same way, at the request: a change applies to the next request.
- **NB13. Punch with Scene B at None or empty** consumes the edge and writes nothing. Its release
  then writes nothing. A punch that starts while one is already on can't happen (it's on/off).
- **NB14. No `rng::Field` is added.** Scenes draw nothing, so no RNG key changes.
- **NB15. `presetEdited`** (the browser's edited mark) ignores the seven controls, as it ignores
  the ring parameters. A recall that changes the sound marks it edited.
- **NB16. The scene status for the editor** (`UiSnapshot`, by addition) contains:
  - the filled bits (8 bits);
  - `A_eff`;
  - the waiting recall's scene (0 for none) and its division;
  - the glide's progress (−1 when none);
  - Punch's state.

  Sub-project 5 draws its buttons from it.

## 4. Design

### 4.1 Parameters (append-only IDs)

All seven are global, in both products, with flags `CLAP_PARAM_IS_AUTOMATABLE` (plus
`CLAP_PARAM_IS_STEPPED` where stepped) and **not** `MODULATABLE`.

| Slot | CLAP ID | Display name | Range, default | Notes |
|---|---|---|---|---|
| `Scene` | `0x000051` | Scene | stepped 0 to 8, 0; "None", "1" to "8" | A: the scene recalled, and the one Store writes |
| `SceneB` | `0x000052` | Scene B | stepped 0 to 8, 0; "None", "1" to "8" | The morph's and the punch's other end |
| `SceneMorph` | `0x000053` | Scene Morph | 0 to 1 (0% to 100%), 0 | N5 |
| `ScenePunch` | `0x000054` | Scene Punch | Off/On, Off | N8 |
| `SceneQuantize` | `0x000055` | Scene Quantize | stepped 0 to 4, 0; "Off", "Beat", "Bar", "2 Bars", "4 Bars" | N7 |
| `SceneGlide` | `0x000056` | Scene Glide | 0 to 16 s, linear, 0; shown in seconds | N6 |
| `SceneStore` | `0x000057` | Scene Store | Off/On, Off | N9 |

- **The slots:** appended after `BendRange` in `ParamSlot` and `kParamIds`. On this branch that
  makes 297 slots (290 + 7). The parallel branches' slots are added at merge (slots aren't saved;
  IDs are). `kLayerParamCount`, `kNextFreeLayerIndex` and `kNextFreeGlobalIndex` are untouched:
  the next-free indices count below 0x40.
- **The reserved blocks:** `detail::isReservedIndexInUse` gains `scope == 0 && index >= 0x51 &&
  index <= 0x57`, which the reserved-block `static_assert` requires. Update its comment and
  `kReservedIndexBegin`'s.
- **New helpers in `ParamIds.h`:**
  - `isSceneControl(slot)`: the seven;
  - `isSceneSlot(slot)`: automatable in `paramMeta` (both products' flags agree), and not Budget,
    and not `isSceneControl`.
- **Not a route target:** `isValidTarget` refuses `isSceneControl`.
- **Mutate:** the seven join `structuralLocks` (Mutate keys by CLAP ID, so no press moves).
- **No remote-controls change** but the new page (§4.6).
- **The counts that follow:** every hard-coded count follows, as sub-project 1 listed
  (`tests/support/ParamTable.h`, `patch_tests`, `identity_tests`, `params_tests`, `rt_tests`,
  `EditLayout` and its tests, the `[sweep]` floors, each with its reason), and so do the comments
  in `ParamIds.h` that state counts.

### 4.2 The bank (`core/include/gg/core/Scenes.h`, `core/src/Scenes.cpp`; pure, allocation-free)

- **The types:**
  - `inline constexpr uint8_t kScenes = 8;`
  - `struct Scene { std::array<float, kParamCount> values; std::array<uint64_t, (kParamCount + 63) / 64> mask; }`:
    filled when any mask bit is set. Trivially copyable.
  - `struct SceneBank { std::array<Scene, kScenes> scenes; }`: about 9.8 KB at 297 slots.
- **The pure functions** (the `Processor` and the plugin's inactive path share them):
  - `capture(const ParamValues &bases, Scene &out)`: every `isSceneSlot`, mask set;
  - `morphValue(slot, a, b, m)`: N5's law;
  - `morphTarget(bank, A, B, m, slot) -> std::optional<float>` (nothing when A is empty or the slot
    isn't in A's mask): f, or a when B is None or empty, or the slot isn't in B's mask;
  - `recallInto(bank, A, B, m, ParamValues &values, bitset &written)`: the inactive path's recall;
  - `dropRingParams(SceneBank&)`: clears `isRingParam` bits, for a preset's bank;
  - `clearSceneMomentaries(ParamValues&)`: Scene Punch and Scene Store to 0, at every load.
- **`isSceneSlot`'s table** (pinned by a test, so a parallel sub-project's merge that adds an
  automatable parameter adds it to scenes on purpose):
  - **in:** all of layer A to D's automatable parameters but none of the seven, the four
    automatable ring parameters included; Mix, Level and Levels B to D; Space's seven; Texture,
    Feedback and the macros; Morph X and Y; Layer A to D On; the delay's and the limiter's eight;
  - **out:** each layer's Ring Length, Budget, MPE, MPE Bend Range, Bend Range, and the seven.

### 4.3 The actions, in the `Processor`

**Storage** (members, preallocated, so nothing allocates on the audio thread):
- `SceneBank bank_` with its `epoch_`;
- `A_eff_`;
- the waiting recall `{scene, division, k_r}` and a forced flag;
- the glide `{r, G, start[], target[], bitset owned}`;
- the punch `{before[], bitset owned}`;
- the report bitset;
- `scenesBusy_`.

That is about 12 KB beside the bank. `setScenes(const SceneBank&, uint32_t epoch)` copies the bank,
is real-time safe and is called at a call's start (`applyLoadedState`) or before activation. The
`LatestValue<PublishedScenes>` producer is the audio thread.

**Each boundary b with a scene event, or while `scenesBusy_`** (NB1's steps):

- **Recall request.** It comes from a Scene change (N4), a Program Change or a Recall command,
  with scene k.
  - k = 0 cancels a waiting recall.
  - Else, if Scene Quantize is Off, or the transport isn't playing or has no beats, the recall
    applies at b.
  - Else it waits (§4.4).
- **Applying a recall** of k at boundary c:
  - `A_eff = k`;
  - for every slot in k's mask the target is `morphTarget(bank_, k, B, m, slot)`;
  - with Scene Glide 0 the bases take their targets at c; otherwise the glide starts at r = c;
  - written slots leave the punch's return set.
- **Glide step** at each applied c > r: N6's values for the owned slots. At p ≥ 1 the targets are
  exact and the glide ends.
- **Morph change** (Scene Morph's or Scene B's value changed at b; `A_eff` unchanged at b). For
  each slot, f is computed with the old (B, m) and with the new; the slots where they differ bit
  for bit take the new f. Those slots leave the glide and the punch's set.
- **Punch edges.**
  - Off → On (with B filled) writes scene B's value to each slot of B's mask where it differs
    from the base, after saving each base in `before[]`, and sets `owned`.
  - On → Off writes `before[]` to the owned slots and clears the set. Written slots leave the
    glide.
- **Store edge** (Scene Store Off → On, or the Store command with k). It runs after the explicit
  events (NB1 step 4): `capture(base_, bank_[k])` for k = Scene's value (the command's k), at
  k ≥ 1. Then the bank is published.
- **Clear command** k empties scene k's mask and publishes. A recall of it then writes nothing.
- Every write goes to `base_` through the path a committed base event takes (`pendingBase_`
  semantics), so ramps retarget, Density and Distribution rescale the pending onset (M1-h), and
  Freeze arms. Each write sets the slot's report bit when the value changed.
- A scene action never changes a modulation offset (`mod_`, CLAP's param-mod events) or a
  route, and Mutate never changes the bank.

### 4.4 Quantize (the transport seam)

- **The position** `pos(c)` is the transport source's beat position at boundary c: the value the
  Beat and Bar sources take for the cell [c, c + 32). `TransportSource` gains const accessors
  `beatPosition()` (double), `beatsPerBar()` and `hasBeats()`; it holds `pos(c)` before it steps
  at c.
- **The division** `q` in beats is 1, `beatsPerBar`, 2 × `beatsPerBar` or 4 × `beatsPerBar`, and
  `k(c) = floor(pos(c) / q)` is computed in double.
- **A request at r** applies at r when `pos(r) / q` is an integer, or when the transport isn't
  playing or has no beats. Otherwise it records `k_r = k(r)` and waits.
- **It fires at the first applied boundary c > r where `k(c) ≠ k_r`:** the next line reached, or
  a host jump or loop wrap, which counts as a line. A line falls at most 31 samples before its
  boundary (0.65 ms at 48 kHz).
- **The transport stopping** while a recall waits fires it at that boundary. A recall that waits
  keeps the q it was requested with (NB12).
- **Sub-project 7's tempo seam.** If sub-project 7 lands a shared tempo clock first (beat
  position per boundary, division lines), Quantize uses it, and the rule above is what it must
  compute. If not, this sub-project adds the accessors, and sub-project 7 builds on them. Either
  way the division vocabulary only grows by appended values, so Scene Quantize's five keep their
  numbers.

### 4.5 Program Change and the input path

- **`ControlKind::ProgramChange`** is appended (after sub-project 2's kinds at merge; keep both).
  `key` holds the program (0 to 127) and `value` isn't read. `validControl` admits it with a key
  of 0 to 127 on channel −1 to 15.
- **The plugin:** `isControlEvent` admits status 0xC0 (the MIDI dialect: CLAP MIDI events, VST3
  through clap-wrapper's per-channel program-change parameters, which the wrapper turns into 0xC0
  events, and AU MIDI). `toControlEvent` maps 0xCn to it with channel n. It counts toward
  `kMaxControlEvents`, and `applyBetweenBlocks` queues it as other controls.
- **In the `Processor`:** at its boundary, programs 0 to 7 set Scene's base to program + 1 (a
  reported write) and make a forced recall request. Other programs are ignored.

### 4.6 The plugin

- **Threads and the bank:** NB6. `LoadedState` gains `SceneBank scenes`, which keeps it
  trivially copyable. `GigaGrainsPlugin` gains:
  - `scenes_` and `sceneNames_` (main thread);
  - the published-bank take in `on_main_thread`, `stateSave` and `deactivate`;
  - `uiSceneCommand(kind, k)` on `uiCommand`'s queue, with three new `CommandKind`s: `StoreScene`,
    `ClearScene` and `RecallScene`. The scene index rides in `value`, and `layer` is ignored.
    `validCommand` takes k of 1 to 8; a ring check doesn't apply to them.
- **The report:** NB5. It covers the slots that recalls, glides, morphs, punches and a Program
  Change wrote. The editor reads `values_` as it does for automation.
- **The remote-controls page** "Scenes" is appended as page 13: Scene, Scene B, Scene Morph,
  Scene Punch, Scene Store, Scene Quantize, Scene Glide, and an empty slot.
- **The FX view's "Scenes" panel** (`ui/src/EditLayout.cpp`) comes after MIDI, in the same
  order. Each control is the generic one for its kind.
- **`UiSnapshot`** gains `SceneStatus` (NB16) by addition.
- **The undo snapshot builder and Mutate** skip `isSceneControl` (N12).
- **The epoch:** a Store made from a host's automation while a state load is in flight is
  dropped with the old bank, as the old values are.

### 4.7 State and presets

- **The patch element.** After `<mod>`, before `<preset>`, `toStateXml` writes:

  ```
  <scenes><scene n="3" name="Verse"><p id="65538" v="…" bits="…" /> …</scene> …</scenes>
  ```

  - Only filled scenes are written, in index order. Each `<p>` is one mask bit, in slot order,
    as `<params>`' `<p>` (`v` with `bits`, read back bit for bit). `name` is escaped as the
    root's name is.
  - `<scenes>` is left out when the bank is empty, so a state with no scenes keeps today's bytes.
- **Reading it,** at any version. Each of the following skips only itself, never the load:
  - a `<scene>` whose `n` isn't 1 to 8 (a later duplicate wins);
  - a `<p>` with an unknown ID, a non-`isSceneSlot` ID (Budget, Ring Length, a scene control),
    or a NaN `v` (clamped to range otherwise);
  - unknown attributes and elements.

  A missing `<scenes>` is an empty bank.
- **`kPatchVersion` stays 2.**
- **`SessionState`** gains `SceneBank scenes` and `std::array<std::string, kScenes> sceneNames`,
  which `toStateXml` and `fromStateXml` pass through (new optional parameters, in the style of
  `preset` and `edited`).
- **Loads.**
  - A **session load** applies the bank and the names, and its values after
    `clearSceneMomentaries`.
  - A **preset load** (`readPreset`) applies its bank after `dropRingParams`, its names, and its
    values after `keepRingParams` and `clearSceneMomentaries`.
  - **INIT** (`initState`) has an empty bank.
  - `gg-render`'s `state` and `preset` paths call the same functions.
- **The packer** (`--pack-preset`): a source's `scenes` (§4.9's format) may not set a ring
  parameter (exit 2, naming it), as its `params` may not.
- **Older builds** that re-save drop `<scenes>`. That is accepted before anything is public.

### 4.8 Determinism and real-time safety

- **Everything happens at grid boundaries,** as a function of the absolute sample:
  - recalls, glide steps, morph writes, punches, stores and Program Changes;
  - the glide's p;
  - the quantize lines, from the transport source's own per-cell position.

  So the output is bit-identical at any block size, given the same transport. The morph is a pure
  function of the scene values and (A, B, m), and a recall a pure function of the bank and the
  triple.
- **No libm** but `floor` and `llround` (exact). The morph and glide formulas are plain float or
  double arithmetic with `gg::fp-contract-off`.
- **Real time:**
  - nothing allocates, locks or makes a syscall: the storage is members, and `setScenes` and the
    publish are `memcpy`s of a trivially copyable type;
  - loops are bounded by `kParamCount` and `kScenes`;
  - 0-frame and 1-frame calls go through the existing pending-boundary carry;
  - `params.flush` stays trivial: `setBase` on a scene control is one bounded recall.
- **The cost** of a full recall is one pass over the slots plus `updateEffective`. During a glide
  every cell is applied, as with a time-varying route. The builder records the time of one block
  with a full recall in the plan's evidence.

### 4.9 `gg-render`

- **`params`** takes the seven by display name, as base values with no action. For example,
  `"Scene": 1` so that a later Store writes scene 1.
- **`automation`** takes all seven, as events, so they act.
- **`scenes`** (FX and instrument modes; an unknown key in sample mode) is an object keyed `"1"`
  to `"8"`.
  - Each entry is `{"name"?, "params": {…}}`: a scene captured from the render's initial values
    (after `preset`, `state` and `params`), with its own `params` on top.
  - A name that isn't a scene slot is bad input (exit 2: "… is not a scene parameter").
  - It replaces that index of a state's or preset's bank.
- **`controls`** take `{"time", "kind": "programChange", "program": 0–127, "channel"?}`.
- **`operations`** take `storeScene`, `clearScene` and `recallScene`, with `"scene": 1–8` and no
  `layer`, sent as the editor's commands at their time.
- **`--save-state`** writes the bank and the names.
- **The preset source format** (`PresetSource.h`) takes `scenes` as above.
- **Documentation:** `RenderSpec.h` documents each key, and the `render` tests pin the refusals
  (bad index, bad program, a ring parameter in a preset source's scene, `scenes` in sample mode).

### 4.10 Goldens (new)

Each golden catches the 16 injected click steps, or joins the click-injection table with its
reason. No new event-log field: the recalls are seen through the grains' latched values.

- **`fx-scene-recall`** (6 s): a stereo 330 Hz sine.
  - Scene 2 differs from scene 1 in Size, Density, Pitch +7, Mix, Reverb Amount, Delay Amount,
    Layer B On and Bus 1 Filter.
  - Scene is automated to 2 at 1.0 s, to 1 at 2.5 s and to 2 at 4.0 s, with Quantize Off and
    Glide 0.
- **`inst-scene-glide`** (7 s): the instrument over a sweep, one held note.
  - Scene Glide is 2 s and scene 2 is recalled at 1.0 s: Position, Size, Pitch +12, Level, and
    Layer B On, which switches at the glide's midpoint.
  - At 4.0 s Glide is automated to 0.5 and scene 1 recalled.
- **`fx-scene-morph`** (7 s): Scene 1, Scene B 2.
  - Scene Morph steps 0, 0.25, 0.5, 0.75, 1 and 0.4 at 1 s intervals.
  - The scenes differ in continuous parameters and in three stepped ones (LFO 1 Shape on a routed
    LFO, Bus 1 Filter, Quality), which switch at 0.5.
- **`fx-scene-quantize`** (5 s): the transport at 120 BPM, playing, from beat 0.
  - With Quantize Bar, a `programChange` 1 at 0.7 s recalls scene 2 at 2.0 s.
  - With Quantize Beat, Scene is automated to 1 at 2.9 s and fires at 3.0 s.
  - With Quantize Off, scene 2 is recalled at 4.1 s and applies at once.
- **`fx-scene-punch-store`** (6 s): Scene 1 in `params`.
  - At 0.5 s, Size and Pitch are automated. At 1.0 s Scene Store turns On (it captures into
    scene 1), and at 1.5 s Size moves again.
  - Scene B is 2, from `scenes`. Scene Punch is On from 2.5 s to 3.5 s; the release returns the
    owned slots.
  - At 4.5 s, `programChange` 0 re-recalls scene 1, the stored values.

The 108 earlier cases and the 8 voice goldens keep their WAV SHA-256: `scripts/update-goldens.sh`
before and after, and the at-rest check (`check-wav-hashes.sh`) ends with
`baseline cases: 108, changed: 0, gone: 0`.

## 5. Tests

- **Core, `tests/core/scenes_tests.cpp`** (`[core][scenes]`):
  - `isSceneSlot`'s table (§4.2);
  - the morph law: exact ends, monotone, the stepped midpoint at exactly 0.5, the clamp;
  - `capture` and the masks;
  - `recallInto` and `morphTarget` with partial masks;
  - `dropRingParams` and `clearSceneMomentaries`;
  - the patch round trip of the bank and names, bit for bit;
  - an empty bank writes no `<scenes>`, so the bytes are today's;
  - each malformed-input case of §4.7;
  - a state without `<scenes>` loads an empty bank;
  - a preset load replaces the bank and drops ring bits; INIT is empty.
- **`Processor`, `tests/core/scenes_processor_tests.cpp`** (`[core][scenes]`, also
  `[determinism]` where marked):
  - an event at sample a recalls at `ceil(a / 32) × 32`;
  - the hand wins in one group;
  - a Scene re-sent at the same value doesn't recall, while a Program Change of the same scene
    does;
  - an empty scene and Scene 0;
  - the glide's value at each cell and its end; a slot leaving the glide on an event; a new
    recall replacing the glide;
  - the morph's write set: equal slots keep tweaks, and Scene B at m = 0 writes nothing;
  - the punch: apply, release, and the return set shrinking;
  - Store: in the group's order, at None, and published with the epoch;
  - Clear;
  - quantize: each division's firing boundary; an on-line request; a stopped transport; a
    transport stopping while waiting; a loop wrap; the latest request winning; Scene 0
    cancelling;
  - Program Change mapping: 0 to 7, 8 ignored, any channel;
  - `setBases` triggering nothing and clearing; `setBase` recalling at once; `reset()`;
  - the report contract (NB5), with a pending later event excluded;
  - Mode, Source and Layer On switched by a recall;
  - a Voice layer's voices picking up recalled values;
  - **block-size invariance** of all five goldens' behaviors at 1, 7, 32, 64 and 4096;
  - **at rest:** a render with the seven at their defaults and the bank empty is bit-identical to
    one without this code path (and the goldens' hashes).
- **Real time (`rt`):**
  - the allocation tripwire on `process` with a recall, a glide, a punch, a store and a Program
    Change, at block 1 and block 4096;
  - `params.flush` with a Scene change;
  - RTSan (CI's `sanitizer (rtsan)` job).
- **Plugin, `tests/plugin/scenes_tests.cpp`** (`[plugin][scenes]`):
  - a MIDI 0xC0 recall;
  - the out-events (the written slots' final values, none at rest), with `try_push` refusal
    carrying over;
  - `values_` after a recall;
  - a Store through the parameter reaching `stateSave`;
  - the session round trip of the bank and names;
  - a preset file load (the bank replaced, ring bits dropped, momentaries Off);
  - INIT;
  - the inactive flush's recall and store;
  - a stale published bank dropped after a load;
  - Mutate leaving the seven alone; whole-state undo skipping them;
  - `presetEdited` ignoring them;
  - the "Scenes" remote-controls page;
  - the three commands;
  - `clap_plugin_as_vst3` unaffected.
- **GUI:** the frame tests with the FX view's Scenes panel at 960 × 600 and 640 × 360, in both
  themes; "every parameter is reachable".
- **`render`:** each new key, and its refusals.
- **`sweep`:** the seven are swept as stepped and continuous parameters. They are inert with an
  empty bank, so the floors are updated only where the swept share moves, each with its reason.
- **The gate:** `scripts/check.sh` (dev, asan; RTSan in CI), the validators (clap-validator,
  pluginval, Steinberg's VST3 validator; auval on macOS in CI), and `gg-bench`'s headline ratios
  (no engine change is expected).

## 6. INTENT changes (Part II, each with a §23 row)

- **§14:** a new paragraph, **Scenes** (M3 sub-project 10), covering:
  - the bank and `isSceneSlot`;
  - the write model;
  - the actions and their order in a group;
  - the glide, quantize, morph, punch and store laws;
  - Program Change;
  - determinism (the control grid, no RNG).

  Also, **Two-rate modulation** adds that a glide applies every boundary.
- **§15:** the Program Change input path (CLAP MIDI, VST3 through the wrapper's per-channel
  program-change parameters, AU MIDI), and the host questions below.
- **§17:**
  - the FX view's Scenes panel;
  - the "Scenes" remote-controls page (the 14th);
  - undo: scene actions push nothing, and whole-state entries skip the seven;
  - `UiSnapshot`'s scene status.
- **§18:**
  - the `<scenes>` element in `PTCH` (no version bump);
  - what a session load, a preset load and INIT do with the bank and the controls;
  - the seven IDs `0x000051` to `0x000057` in the ID tables;
  - scope 0's 0x51 to 0x57 among the reserved indices in use.
- **§20:** M3's row names sub-project 10 (with the program's other additions, 3b and 7 to 11).
- **§22:** scene chaining and chance, and more than eight scenes, as later questions.
- **§23:** one row at the next free number (#97 on this branch; renumbered at merge) recording
  N1 to N15 and NB1 to NB16 as Claude's provisional calls.

**Carry-over rows** (`docs/m3-carry-over.md`, at the next free numbers):
1. Which first-class hosts deliver a MIDI Program Change to each format, and whether a VST3 host
   sends it instead to the wrapper's preset selector at `0xC00000`, which would load a factory
   preset.
2. How each host treats a recall's `PARAM_VALUE` out-events: display only, recorded in write,
   latch or touch, or an automated lane overridden.
3. The cost of a full recall, measured on the reference core.

## 7. Interactions with the parallel sub-projects

- **2 (expression):**
  - `ControlKind` appends on both branches, so a merge keeps both lists.
  - Its automatable additions (Pan, Note Channel, Key Low and High, the curves) are scene slots.
    A scene can change a split.
  - Its MIDI settings aren't scene slots.
  - Per-note modulation stays on top of a recall.
  - Program Change ignores its note channels.
- **3 (pitch-sync) and 3b (playhead):** Clock and Scan Speed, Edge and Track are scene slots. A
  recall leaves a playhead's running offset alone; only the parameters change.
- **4 (tuning):** MTS-ESP isn't a scene slot (not automatable); Pitch Quantize is. The tuning file
  isn't in a scene.
- **5 (views and presets):** it owns:
  - the scene buttons, names and status display (from `SceneStatus` and the main-thread mirror);
  - the editor recall's undo entry, from NB5's report and the values before it;
  - copy and paste;
  - a TEMP or punch editor gesture, if wanted;
  - any scene-carrying factory presets, appended.

  It may move the FX view's Scenes panel.
- **7 (tempo sync):** §4.4's seam. Whichever lands second adopts the other's accessors, and Scene
  Quantize's values stay as numbered.
- **All:** the `structuralLocks`, `isValidTarget`, `isReservedIndexInUse`, remote-pages and
  `EditLayout` lists are appended by several branches, so merges keep every entry. When a new
  parameter merges in, `isSceneSlot`'s pinned table is updated on purpose.

## 8. Left to the builder

- The exact function signatures (`Scenes.h`, `setScenes`, the report accessor, the
  `toStateXml`/`fromStateXml` parameters) and where in `commitPending` the steps sit, keeping
  NB1's order.
- The task split. A natural one:
  1. `ParamIds` and the bank's pure core;
  2. the patch format;
  3. the `Processor`'s actions;
  4. quantize and Program Change;
  5. the plugin (threads, report, commands, remote page, panel);
  6. `gg-render` and the goldens;
  7. INTENT and the records.

  Tasks 2 and 4 can run beside 3 once 1 is in.
- The values for each golden's scenes, set so each catches its injected steps.
- The recall's measured cost (§4.8).

## 9. The builder check (questions a builder would still ask, answered)

1. *Scene and an explicit value for a scene slot arrive in one group: which wins?* The explicit
   value (NB1 step 3; N12).
2. *Does a state, preset or undo restore recall the saved Scene?* No: `setBases` never acts (N4).
   It sets `A_eff` (NB2) and drops the waiting recall, the glide and the punch (NB3).
3. *What does Scene compare against to detect a change?* Its rounded base before the group
   (NB9), not `A_eff`.
4. *Can the morph and the punch work with no Scene A?* The morph can't (N5). The punch can: it
   writes B's own values (N8).
5. *Which slots does a morph move write?* Those whose f changed bit for bit between the old and
   new (B, m), computed on the bank as it stands at b. Slots in B's mask alone are never
   touched (N5).
6. *Where does a glide step sit relative to the group's events?* Before them, in step 2 (NB1), so
   an event at the same boundary wins and takes its slot out.
7. *Does automating Scene Glide or Scene Quantize affect a recall already under way or waiting?*
   No: both are latched at the request (NB12).
8. *What is "the transport playing" for Quantize?* The transport source's playing flag and
   `hasBeats` as of the boundary (§4.4). A host without a transport gives neither, so recalls
   apply at once.
9. *Is the host told of a value a later host event already superseded?* No: the report holds
   only slots whose latest base, pending events included, is a scene's write (NB5).
10. *What thread writes the bank, and how does the save see a Store?* The audio thread while
    active, published with an epoch; `stateSave` takes the published bank first (NB6).
11. *What happens to a Store made just before a state load lands?* It is dropped with the old
    bank (the epoch, §4.6).
12. *Do scene controls appear in Mutate, the undo snapshot, the edited mark or the routes?* No
    (§4.1, N12, NB15, N13).
13. *Does a preset's scene ever set a ring parameter?* No: `dropRingParams` at load, and the
    packer refuses one (N11, §4.7).
14. *Do any existing bytes change?* No. A state with an empty bank writes no `<scenes>`. No
    golden sends a scene event or a Program Change. The pieces are cut as today while
    `scenesBusy_` is false (NB4). The event log's format is unchanged.
15. *Does a Program Change with an empty bank do anything?* It sets Scene (reported to the host)
    and recalls nothing. That is inert for the sound: a group whose values are unchanged moves no
    ramp, scheduler or engine parameter.
16. *What does an AU or VST3 host need?* Nothing new beyond the wrapper's existing MIDI paths.
    Whether each host sends Program Change there is a carry-over host check (§6).
17. *What if two parallel branches both append to `ParamSlot`?* Slots aren't saved. Keep both,
    and pin the merged `isSceneSlot` table again (§7).
