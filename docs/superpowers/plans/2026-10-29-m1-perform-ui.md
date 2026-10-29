# M1 Perform UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give both plugins eight relative macros, the Texture envelope, Feedback into the live
ring (FX), a fresh seed per instance, and a Perform view whose canvas draws the grains the engine
is actually playing, with the scan window draggable as a host-visible gesture.

**Architecture:**
- **Parameters and macros** live in `gg-core`: ten new `ParamSlot`s (Texture, Feedback, Macros
  1–8), and `Processor::updateEffective` becomes `clamp(base + mod + Σ macro curves)`, the curves
  in `ParamMap`.
- **Texture** is one more value latched per grain: at spawn, `GrainPool` derives an attack and a
  release fraction from Texture and the duration; the three window-evaluation sites warp the
  phase through them before the Hann table, so Texture 0 is bit-identical to today.
- **Feedback** is the Processor's: it keeps the last 256 samples of the limited wet signal, cuts
  its FX render pieces to at most 256 samples, and hands the engine `in + fb × wet[t − 256]` as
  the ring input.
- **Audio → UI** is three JUCE-free structures in `gg-core` (a grain event queue, a triple-buffered
  per-block snapshot with an atomic clock, a peak strip of the live ring) that the Processor fills
  only while publishing; the plugin gets a UI → host parameter queue flushed with gestures.
- **The editor** is rebuilt on sst-jucegui: header, canvas (`GrainCanvas` over `SplatRenderer`,
  with a JUCE-free `LaneModel` doing the geometry), macro row.

**Tech Stack:** C++20, CMake/Ninja, Catch2, JUCE 9.0.4 via clap-juce-shim, sst-jucegui (MIT, new
submodule), sst-basic-blocks `ParamMetaData`, clap-helpers, `gg-render`, `gg-bench`.

**Spec:** `docs/superpowers/specs/2026-10-27-m1-perform-ui-design.md` (approved 2026-10-27).
Read it before your task; its sections are cited as "spec §n". Two corrections to it, applied
in Task 13 as amendments: sst-jucegui is **MIT**, not GPL-3.0-or-later (its `LICENSE.md`; INTENT
§15 already says MIT); and the Feedback chunking (spec §5, "D must equal M … Engine renders longer
than M are split into chunks") is done by the **Processor**, not the engine, since the Processor
already owns the render pieces.

## Global Constraints

**Non-negotiables** (CLAUDE.md):
- **Audio thread:** no allocation, locks, I/O or syscalls; RTSan-clean; 0- and 1-sample blocks;
  `params.flush` stays trivial (O(queued events)).
- **Determinism:** output bit-identical at any host block size and any `maxBlockSize`; every
  curve and envelope is plain IEEE arithmetic, `exp2Det`, `logDet` or `std::sqrt`: **no libm** in
  anything that reaches the grain-event log or the audio (spec §3.1, §4). `std::log2` and friends
  are allowed on the UI thread only (the canvas's coordinate mapping).
- **The default sound is unchanged:** with Texture 0, Feedback 0 and every macro at rest, every
  existing golden's WAV SHA-256 is unchanged (`scripts/update-goldens.sh` before and after; INTENT
  §19), **including the sign of zero**. The grain-event log gains one field (Task 3), so the
  `.events.jsonl` files are regenerated once; the `.metrics.json` files and WAVs must not move.
- **Stability contracts:** IDs append-only; slot order = AUv2 order; the two existing remote pages
  keep their contents and move to pages 2 and 3.
- **Carry-over policy:** rows 115, 123, 198 and 199 of `docs/m1-carry-over.md` are owed to this
  sub-project and must close. No net growth of open rows (count them at start: 18 rows lack
  "Closed", 1 is marked `**Open`).
- **Formatting:** `scripts/format.sh` before every commit. Test case names ASCII only.
- **Commits** end with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` (or the model
  that wrote them, as the session's attribution reminder says). Run `scripts/check.sh` before
  reporting a task done, and paste its summary.

**The parameters** (spec §3.3, §4, §5; appended after `DryToReverb` in this order):

| Slot | ID | Name (FX / instrument) | Domain (CLAP value) | Default | Flags |
|---|---|---|---|---|---|
| `Texture` | `0x010009` | Texture | 0–1, shown as % | 0 | A·M |
| `Feedback` | `0x01000A` | Feedback | 0–0.95, shown as % | 0 | A·M; **HIDDEN in the instrument** |
| `Macro1` | `0x000011` | Macro Size | 0–1, shown as % | 0.5 | A·M |
| `Macro2` | `0x000012` | Macro Density | 0–1 | 0.5 | A·M |
| `Macro3` | `0x000013` | Macro Pitch | 0–1 | 0.5 | A·M |
| `Macro4` | `0x000014` | Macro Spray | 0–1 | 0.5 | A·M |
| `Macro5` | `0x000015` | Macro Texture | 0–1 | 0 | A·M |
| `Macro6` | `0x000016` | Macro Freeze | off/on (0 or 1) | 0 | A·M·STEPPED |
| `Macro7` | `0x000017` | Macro Feedback / **Macro Stereo** | 0–1 | FX 0 / **instrument 0.5** | A·M |
| `Macro8` | `0x000018` | Macro Mix / **Macro Level** | 0–1 | 0.5 | A·M |

A·M = `CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE`. `kParamCount` becomes 28. The
instrument's hidden set is Mix, DryToReverb and Feedback. Macro names differ per product, so
`paramMeta(slot, mode)` differs for Macro7 and Macro8 (as Position's already does).

**The recipes** (spec §3.2; `v` = the macro's effective value in 0..1, `b = 2v − 1`,
`b⁺ = max(b, 0)`, `u = v`; every offset is in the target's CLAP value domain and is added before
the target's clamp):

| Macro | Offsets |
|---|---|
| 1 Size | Size `+3b`; Density `−1.5b` |
| 2 Density | Density `+4b`; Distribution `+0.25·b⁺` |
| 3 Pitch | Pitch `+24b` |
| 4 Spray | Spray `+0.5b`; Stereo `+0.3·b⁺` |
| 5 Texture | Texture `+min(u / 0.75, 1)`; Diffusion `+smoothstep(0.75, 1, u)` where `smoothstep(e0, e1, x) = s²(3 − 2s)`, `s = clamp((x − e0)/(e1 − e0), 0, 1)` |
| 6 Freeze | Freeze `+u` (effective Freeze is on when `base + mod + u > 0.5`) |
| 7 FX | Feedback `+0.9u²` |
| 7 instrument | Stereo `+0.5b` |
| 8 FX | Mix `+0.5b` |
| 8 instrument | Level `+12b` dB |

A bipolar macro at 0.5 and a unipolar one at 0 contribute exactly 0 (`2·0.5f − 1 = 0` exactly in
float), so a resting preset is bit-identical to its base values.

**The Texture envelope** (spec §4; the plan's fixed family):
- Per grain, from Texture `τ` (latched at spawn) and the duration `d` in samples:
  - `aMin = min(0.5, max(0.05, 16 / d))` (the gated anchor's attack and release, each at least 16
    samples and 5% of the grain, never more than half of it);
  - `τ ≤ 0.5`: attack `a = 0.5 + 0.4τ`, release `r = 0.5 − 0.4τ` (no sustain; at 0.5 the peak is at
    70%);
  - `τ > 0.5`: with `s = 2τ − 1`, `a = 0.7 + (aMin − 0.7)s`, `r = 0.3 + (aMin − 0.3)s`; the sustain
    is `1 − a − r`.
  - So `a + r ≤ 1` always, and at τ = 0: `a = r = 0.5` exactly.
- The window at phase `φ ∈ [0, 1]` is `hann(warp(φ))` with
  `warp(φ) = φ < a ? φ · (0.5 / a) : (φ > 1 − r ? 1 − (1 − φ) · (0.5 / r) : 0.5)`, where `hann` is
  today's `HannTable::at`. At `a = r = 0.5` the scales are exactly 1.0 and `warp(φ) == φ` bit for
  bit (`1 − (1 − φ)` is exact for `φ ∈ [0.5, 1]`, Sterbenz), so Texture 0 renders bit-identically.
- The pool latches four doubles per grain: `envAttackEnd = a`, `envReleaseStart = 1 − r`,
  `envAttackScale = 0.5 / a`, `envReleaseScale = 0.5 / r`. The warp is written once, in
  `gg/grain/Envelope.h`, and every window site (the two scalar references and the SIMD interior)
  calls it, so they stay bit-identical by construction.

**Feedback** (spec §5): `ring[t] = sanitize(in[t]) + fb(t) · limited[t − 256]`, where `limited` is
`softLimit(w · g)` (Level applied, before the dry/wet mix; `Processor.cpp:556` today), `fb(t)` is a
`Ramp` like `mix_` (10 ms, per sample, absolute time), and 256 is `LiveSource::kHeadMargin`. FX
render pieces are at most `kFeedbackDelay = 256` samples. The instrument has no Feedback.

**Audio → UI** (spec §6.4): the Processor publishes only while `setPublishing(true)`; the
structures are lock-free, allocation-free and in `gg-core`:
```cpp
struct GrainVis { int64_t onsetSample; double startPos; float rate; float durationSamples; float gain; float pan; };
struct UiSnapshot { int64_t clock; int64_t writeHead; bool frozen; uint32_t activeGrains;
                    float basePosition, baseSpray, effectivePosition, effectiveSpray, feedback; };
```
`startPos` is ring time (FX) or source frames (instrument), as `GrainSpec::startPos`.

**UI → host** (spec §6.3): the editor writes base values through the plugin, which pushes
`CLAP_EVENT_PARAM_GESTURE_BEGIN`, `CLAP_EVENT_PARAM_VALUE` and `CLAP_EVENT_PARAM_GESTURE_END` to
the host's output events from `process` or `params.flush`, after `paramsRequestFlush`.

**sst-jucegui:** `libs/sst/sst-jucegui`, URL `https://github.com/surge-synthesizer/sst-jucegui`,
pinned at `22a89ca4e54179cbda0b8164e54e06dae5f30490` (shortcircuit-xt's pin; the JUCE 9 spike
built it, `docs/spikes/m0-juce9.md:245-256`). MIT. Added after `add_clap_juce_shim` so it adopts
`clap_juce_shim_requirements`.

**Fallback (the director, 2026-10-29):** if sst-jucegui fails to build or behave on a CI leg,
spend one bounded fix round; if it still fails, build the knobs, the toggle and the dark theme
with JUCE's own components, keep the same `EditorContext` and tests, and record a proposed INTENT
§15/§23 amendment in the report for the director to confirm.

## Review Focus

The spec implies these inputs but no task's tests exercised them until this list was written;
each has a test pinned to the owning task:
1. **A macro and host modulation of the same target add up** (Size base 0, mod +1, Macro Size
   at 1 → Size offset 3 + 1, clamped to the max): `effective = clamp(base + mod + Σ)` in one
   clamp, never two. Pinned in Task 2 ("macro offsets add to base and modulation before one
   clamp").
2. **Texture on a 16-sample grain** (the minimum duration, where `16 / d = 1`): the envelope is a
   plain Hann and no division by zero or NaN reaches the table. Pinned in Task 3 ("texture on the
   shortest grain is a Hann").
3. **Feedback with a 1-sample `maxBlockSize` and 0-frame calls:** the 256-sample history indexes
   by absolute time, so output is bit-identical to a 4096 render. Pinned in Task 4 ("feedback is
   block-size invariant").
4. **The grain queue overflows** (8,192 grains in one block with the UI not draining): the audio
   thread drops, counts, never blocks or allocates; the UI resumes with the next events. Pinned in
   Task 6 ("the grain queue drops when full and counts it").
5. **A drag that starts on the canvas and ends outside the window**, or while the host changes
   the same parameter: the gesture ends exactly once and the last value sent is the last drag
   position. Pinned in Task 8 ("a drag emits begin, values, end once").

---

## Execution waves

Tasks in the same wave touch disjoint files and can run in parallel worktrees
(`scripts/worktree-setup.sh`), merging fast-forward in task order:

1. Tasks 1, 3, 6, 7, 8
2. Tasks 2, 5
3. Task 4
4. Task 9
5. Task 10
6. Task 11
7. Task 12
8. Tasks 13, 14

Task 2 waits for 1 and 3 (it wires `CloudParams::texture`). Task 5 waits for 1 and 3 (both edit
`plugin/src/GigaGrainsPlugin.cpp` and `engine/src/Engine.cpp`). Task 4 waits for 2 (both edit
`Processor.cpp`). Task 9 waits for 4 and 6. Task 10 waits for 5 and 9. Task 11 waits for 7, 8
and 10. Task 12 waits for everything that renders.

---

### Task 1: The ten parameters and the Perform page

**Files:**
- Modify: `core/include/gg/core/ParamIds.h` (slots, IDs), `core/src/ParamIds.cpp` (metadata),
  `plugin/src/GigaGrainsPlugin.h:194-197` and `plugin/src/GigaGrainsPlugin.cpp:34-48, :87-91,
  :912-932` (hidden set, remote pages), `tools/gg-render/RenderSpec.cpp:556-568` and
  `RenderSpec.h:249-253` (names of both products)
- Test: `tests/core/patch_tests.cpp` (`kTable`, the pin test, the reserved-ID test),
  `tests/plugin/params_tests.cpp` (`kExpected`, "plugin exposes every param", the two remote-page
  tests), `tests/tools/render_spec_tests.cpp`

**Interfaces:**
- Produces: `ParamSlot::Texture, Feedback, Macro1 … Macro8` appended before `Count`;
  `kParamCount == 28`; `kParamIds` extended with the table above;
  `inline constexpr float kFeedbackMax = 0.95f;` in `ParamIds.h`;
  `constexpr bool isMacro(ParamSlot) noexcept` and `inline constexpr std::array<ParamSlot, 8>
  kMacroSlots` in `ParamIds.h`; `paramMeta(slot, mode)` names per the table (Macro7 and Macro8
  differ by mode); remote pages `{"Perform", Macro1…Macro8}, {"Cloud", …}, {"Space", …}` with
  `remoteControlsPageCount()` returning `kRemotePages.size()`; `paramSlotNamed(name)` matching
  either product's name.

- [ ] **Step 1: Write the failing tests.**
  - `patch_tests.cpp`: extend `kTable` with the ten rows (slot, ID, name, min, max, default,
    flags); `STATIC_REQUIRE(kParamCount == 28)`; the reserved-ID test now asserts `0x010009`,
    `0x01000A` and `0x000011`–`0x000018` **are** recognised and `0x01000C`, `0x000019`,
    `0x000010` are not. Add `"a state saved before the Perform UI loads at the defaults"`: the
    PTCH XML of a default FX state captured on `main` at `c79e045` (paste the literal) decodes to
    the 18 old values plus Texture 0, Feedback 0, Macros {0.5, 0.5, 0.5, 0.5, 0, 0, 0, 0.5}.
  - `params_tests.cpp`: extend `kExpected`; in `"plugin exposes every param"` assert
    `Feedback` is HIDDEN in the instrument only, Macro6 is STEPPED, and `get_info` names Macro7
    "Macro Feedback" in the FX and "Macro Stereo" in the instrument, Macro8 "Macro Mix" and
    "Macro Level". Replace the two remote-page tests with one: 3 pages; page 0 "Perform" lists
    `0x000011`–`0x000018` in order in both products; page 1 is the old "Cloud" list; page 2 the
    old "Space" list; `get(3)` fails.
  - `render_spec_tests.cpp`: `"instrument specs name the instrument's macros"`: an instrument
    spec with `"params": {"Macro Level": 1.0}` parses to `Macro8`; an FX spec with
    `"Macro Mix"` too; `"Macro Level"` in an FX spec is rejected with the unknown-key message.
- [ ] **Step 2:** Run `ctest --preset dev -L 'core|plugin|render'`. Expected: FAIL.
- [ ] **Step 3: Implement.** Metadata: Texture `.asPercent().withDefault(0.0f)`; Feedback
  `.asFloat().withRange(0.f, kFeedbackMax).withDefault(0.f)` with a percent display; macros
  `.asPercent()` with the defaults above, Macro6 `.asOnOffBool().withDefault(0.0f)` plus STEPPED.
  `paramSlotNamed` loops both modes' `paramMeta`, Fx first, so a name unique to one product
  resolves; `parameterValue` keeps range-checking against the spec's mode, and an instrument-only
  name in an FX spec fails there with the existing unknown-key message (make `readParams` pass the
  mode, or check the mode's own table first; either way the test above pins it). The
  `Processor` ignores the new slots until Tasks 2–4.
- [ ] **Step 4:** Run the tests. Expected: PASS. Run `scripts/update-goldens.sh` and confirm
  "every case's WAV is unchanged". Run `scripts/check.sh`.
- [ ] **Step 5: Commit** `feat(core): Texture, Feedback and the eight macros (IDs, metadata,
  Perform page)`.

### Task 2: Macro curves and the effective value

**Files:**
- Modify: `core/include/gg/core/ParamMap.h`, `core/src/ParamMap.cpp:29-49` (also
  `p.texture = at(Texture)` in `toCloudParams`), `core/src/Processor.cpp:270-280`
  (`updateEffective`)
- Test: `tests/core/parammap_tests.cpp`, `tests/core/processor_tests.cpp`

**Interfaces:**
- Consumes: Task 1's slots; Task 3's `CloudParams::texture`.
- Produces:
  ```cpp
  /// Each parameter's total macro offset, in its CLAP value domain (spec §3.2), from the
  /// macros' effective values (each in 0..1). Every entry is exactly 0 when every macro rests.
  ParamValues macroOffsets(const ParamValues &effectiveMacros, Mode mode) noexcept;
  ```
  in `ParamMap.h`, with `constexpr float smoothstep(float e0, float e1, float x) noexcept` beside
  it. `Processor::updateEffective()` computes, for the macro slots, `m[s] = clamp(base + mod)`;
  then `offs = macroOffsets(m, mode)`; then for every slot
  `effective_[s] = clampToRange(s, base_[s] + mod_[s] + offs[s])` (a macro's own offset is 0).
  `Processor::effective(slot)` therefore returns the macro-inclusive value.

- [ ] **Step 1: Write the failing tests.**
  - `parammap_tests.cpp`, `"macro offsets follow the recipes"`: for each macro at 0, 0.25, 0.5,
    0.75, 1 in both modes, `macroOffsets` equals the table's formulas within 1e-6 (write the
    formulas in the test, not a call to the same code); `"resting macros offset nothing"`: with
    the defaults, every entry `== 0.0f` exactly; `"smoothstep"`: 0 below e0, 1 above e1, 0.5 at
    the middle.
  - `processor_tests.cpp`:
    - `"macro offsets add to base and modulation before one clamp"` (Review Focus 1): Size base at
      its max − 1.0, a modulation event of +1.0 and Macro1 at 1.0: `effective(Size)` is the max
      (one clamp), and `effective(Density)` is base − 1.5.
    - `"a resting preset renders bit-identically with the macros present"`: an FX render of 2 s
      noise at the defaults equals a render with Macro1 set to 0.5 by an event at 0.5 s
      (`sameBits`).
    - `"macro events apply on the grid"`: a Macro3 value event at offset 5 changes
      `effective(Pitch)` only from sample 32 (render 64 samples in 1-sample calls and watch
      `effective`).
    - `"macro freeze engages freeze"`: Macro6 = 1 with Freeze base 0 → the engine freezes
      (`engine().liveWriteHead()` stops advancing after the ramp), and `effective(Freeze)` reads
      1.
    - `"instrument macros 7 and 8 drive Stereo and Level"`: instrument Macro7 at 1 → Stereo +0.5;
      Macro8 at 1 → Level +12.
    - Extend `automation()` (`processor_tests.cpp:326`) so its `ranges` cover all 28 slots, and
      keep `"processor is block-size invariant with automation"` green.
- [ ] **Step 2:** Run `ctest --preset dev -L core`. Expected: FAIL.
- [ ] **Step 3: Implement** `macroOffsets`, `smoothstep` and the two-pass `updateEffective`.
  `toCloudParams` gets `p.texture = at(Texture)`.
- [ ] **Step 4:** Run `ctest --preset dev -L core`; then `scripts/update-goldens.sh`: every WAV
  unchanged (the event logs changed in Task 3 only). `scripts/check.sh`.
- [ ] **Step 5: Commit** `feat(core): macro curves; effective = clamp(base + mod + macros)`.

### Task 3: The Texture envelope in the engine

**Files:**
- Create: `engine/include/gg/grain/Envelope.h`
- Modify: `engine/include/gg/grain/CloudScheduler.h:26-52` (`CloudParams::texture`), `:56-80`
  and `:258-270` (`GrainDraw`), `engine/src/CloudScheduler.cpp:28-74` (sanitize/clamp texture to
  0..1), `engine/include/gg/grain/GrainPool.h:20-40, :76-92, :168-181`,
  `engine/src/GrainPool.cpp:13-36, :50-79, :101-117, :138-152`, `engine/src/Engine.cpp:329-340,
  :365-375`, `engine/include/gg/grain/GrainKernels.h:262-267, :278-283, :486-517, :596-613`,
  `tools/gg-render/Render.cpp:335-340` (the event line), `tests/golden/expected/*.events.jsonl`
  (regenerated)
- Test: `tests/engine/window_tests.cpp` (new envelope cases), `tests/engine/kernel_tests.cpp`,
  `tests/engine/engine_reference_tests.cpp`, `tests/tools/render_spec_tests.cpp`

**Interfaces:**
- Produces, in `Envelope.h` (namespace `gg`), every body starting with `GG_FP_CONTRACT_OFF`:
  ```cpp
  struct EnvelopeShape { double attackEnd; double releaseStart; double attackScale; double releaseScale; };
  /// Global Constraints' family: texture in [0, 1] (NaN or out of range reads as 0), durationSamples >= 16.
  constexpr EnvelopeShape envelopeShape(double texture, double durationSamples) noexcept;
  /// warp(phase) for phase in [0, 1]; the identity when attackEnd == releaseStart == 0.5.
  constexpr double warpPhase(double phase, const EnvelopeShape &e) noexcept;
  ```
  `GrainSpec` gains `double texture{0.0};` after `durationSamples` (designated initializers at
  `Engine.cpp:329, :365`, `CloudScheduler.h:258`, `GrainPool.cpp:140`, `Bench.cpp:272`,
  `kernel_tests.cpp:59, :714` keep compiling since it has a default). `GrainRows` gains
  `const double *envAttackEnd, *envReleaseStart, *envAttackScale, *envReleaseScale;`, latched in
  `spawn` from `envelopeShape(g.texture, g.durationSamples)`, compacted in `retireFinished`.
  `CloudParams::texture{0.0}`; `GrainDraw` carries it to `GrainSpec`. The event line gains
  `"tex":<texture>` after `"dur"`. `tools/gg-bench/Kernels.h` is not touched.

- [ ] **Step 1: Write the failing tests.**
  - `window_tests.cpp`: `"envelope anchors"`: `envelopeShape(0, 4800)` is {0.5, 0.5, 1.0, 1.0}
    exactly; `(0.5, 4800)` is {0.7, 0.7, 0.5/0.7, 0.5/0.3} within 1e-12 (no sustain:
    `releaseStart == attackEnd`); `(1.0, 4800)` has `attackEnd == 0.05` and
    `releaseStart == 0.95`; `(1.0, 160)` has `attackEnd == 0.1` (16 samples of 160).
    `"texture on the shortest grain is a Hann"` (Review Focus 2): `envelopeShape(1.0, 16)` is
    {0.5, 0.5, 1.0, 1.0}, and `warpPhase(φ, …) == φ` for 1,000 phases.
    `"warp is the identity at texture 0"`: for 10,000 phases in [0, 1], `warpPhase(φ, shape0)`
    is bit-identical to φ. `"the envelope is continuous and bounded"`: for textures {0.25, 0.5,
    0.75, 1} and durations {16, 100, 4800}, `hann.at(warpPhase(φ))` at φ = 0 and 1 is 0, is
    non-decreasing over the attack, 1.0 over the sustain, non-increasing over the release, and
    adjacent samples of a 4800-sample grain differ by less than 2/16 (the 16-sample minimum).
  - `kernel_tests.cpp`: in `randomGrains` and `liveGrains`, give each grain
    `.texture = u01(seed, 6, k)`, so every existing SIMD-vs-reference bit-exactness case now
    covers textured grains; add `"textured grains match the spec loop"`: `specSample`'s window
    becomes `hann.at(warpPhase(e × invDuration, envelopeShape(g.texture, g.durationSamples)))`,
    and the reference matches it bit for bit.
  - `engine_reference_tests.cpp`: the end-to-end case with `CloudParams::texture = 0.6`.
  - `render_spec_tests.cpp`: the event line carries `"tex":0` for a default stream and the
    value set by `"params": {"Texture": 0.5}` in FX mode.
- [ ] **Step 2:** Run `ctest --preset dev -L 'engine|kernel|render'`. Expected: FAIL.
- [ ] **Step 3: Implement.** In the three window sites replace `e * invDuration` by
  `warpPhase(e * invDuration, shape)` where the shape's four values are read from the rows once
  per grain (scalar sites) or once per `renderInterior` call (SIMD site, before the lane loop).
  `sampleSimdRun` and `liveRun` need no change: the warp maps [0, 1] onto [0, 1]. Keep
  `GG_FP_CONTRACT_OFF` at the top of every new body.
- [ ] **Step 4:** Run the tests. Expected: PASS. Then `scripts/update-goldens.sh` **twice**: once
  on `main` before your change (to seed `wav-sha256.txt`) and once after: it must print "every
  case's WAV is unchanged (19 compared)"; every `.events.jsonl` changes (the new field) and no
  `.metrics.json` does. Then `cmake --build --preset release --target gg-bench` and
  `gg-bench --quick --cases reference,engine-sample,engine-sample-eco` before and after: report
  the three ratios; a move under 5% is expected and accepted (sub-project 6 re-baselines).
  `scripts/check.sh`.
- [ ] **Step 5: Commit** `feat(engine): Texture envelope latched per grain; event log carries
  tex` with the hash statement and the ratios in the message.

### Task 4: Feedback into the live ring

**Files:**
- Modify: `core/include/gg/core/Processor.h` (constants, members, `render`), `core/src/Processor.cpp`
  (`reset`, `jumpRamps`, `applyPending`'s retargets, `render`), `core/include/gg/core/ParamMap.h`
  and `core/src/ParamMap.cpp:51-59` (`WetParams::feedback`)
- Test: `tests/core/processor_tests.cpp`, `tests/core/parammap_tests.cpp`

**Interfaces:**
- Consumes: Task 1's `Feedback` slot and `kFeedbackMax`; Task 2's `updateEffective`.
- Produces: `static constexpr uint32_t kFeedbackDelay = 256;` in `Processor.h` with
  `static_assert(kFeedbackDelay == gg::LiveSource::kHeadMargin)`; `WetParams::feedback`
  (0 in the instrument); members `Ramp feedback_;`, `std::array<std::array<float,
  kFeedbackDelay>, 2> wetHistory_{};` (indexed by `t % kFeedbackDelay`), `std::array<std::vector<
  float>, 2> ringIn_` (`maxBlockSize` floats each, allocated in the constructor);
  `float Processor::feedbackAt(int64_t t) const noexcept` (the ramp's value, for tests and the
  snapshot).
  `render()`'s piece size in FX mode is `min(to − from, maxBlockSize, kFeedbackDelay)`. A piece
  is **feedback-active** when `feedback_.to != 0 || feedback_.at(from) != 0`; then
  `ringIn_[c][i] = finiteOrZero(dry[c][i]) + feedbackAt(t) × wetHistory_[c][t % 256]` for a
  stereo-expanded dry (mono fed to both; null input is 0) and the engine gets `ringIn_` as its
  input; otherwise the engine gets `dry`, as today. After computing `limited` for sample `t`,
  `wetHistory_[c][t % 256] = limited` (read before write within the same sample, so the read is
  256 samples old). `reset()` zeroes the history and jumps the ramp.

- [ ] **Step 1: Write the failing tests.**
  - `parammap_tests.cpp`: `toWetParams(fx).feedback == at(Feedback)`; the instrument's is 0.
  - `processor_tests.cpp`:
    - `"feedback writes the limited wet signal 256 samples late into the ring"`: add the
      one-line getter `const LiveSource &Engine::live() const noexcept;`. FX, Mix 1, Level 0 dB,
      Diffusion 0, Feedback 0.5 set by `setBase` before processing, Density 6, Size −2: process
      1024 samples of noise, keeping the output. For t in 0..767, read ring frame `t + 256`
      through `live().read(pos = t + 256 + 1.0 (the Hermite needs frame t+256 at tap floor(pos)),
      head = engine().liveWriteHead(), …)` or, simpler, `readLinear(t + 256, head, …)`, and
      compare bit for bit with `sanitize(in[t + 256]) + 0.5f × out[t]` (Mix 1 makes `out ==
      limited`). Also assert frames 0..255 hold `sanitize(in[t])` alone.
    - `"feedback is block-size invariant"` (Review Focus 3): `renderAutomation` with Feedback
      automated (0 → 0.9 at 0.3 s, → 0.2 at 1.1 s, a modulation of +0.3 at 1.7 s) over 2.5 s of
      noise, Mix 1, Density 6; splits `{4096,{1}}, {4096,{7}}, {4096,{511}}, {1,{4096}},
      {64,{4096}}, {256,{4096}}, {300,{4096}}` and three random splits with 0-frame calls;
      `sameBits` both channels, same grain log, same write head.
    - `"feedback at zero leaves the ring input alone"`: Feedback 0 throughout: output
      `sameBits` with a render on which `feedback_` never existed? Not available; instead assert
      the input pointer path by rendering an input containing `-0.0f` samples and NaN samples at
      Feedback 0 and comparing bit for bit with Mix 0 output (`d × (1 − m)` with m = 0 reproduces
      the sanitized dry), and keep the golden hash check as the real pin.
    - `"feedback stays bounded for 60 s"` (`[slow]`): FX, Feedback 0.95, Density 7 (128 Hz),
      Size 0 (1 s), Mix 1, Level +12 dB, Stereo 1, stereo noise input at 0.5 for 2 s then
      silence; 60 s: all finite; `|limited|` never above 1 (assert on the output while Mix is 1:
      `|out| < 1`); wet RMS over the last 10 s ≤ RMS over seconds 16–26 + 1 dB (spec §5).
    - `"freeze stops feedback writes"`: Feedback 0.9, Freeze on from 0.5 s: the write head stops
      and the output's RMS after 1 s stays within 0.5 dB of a render with Feedback 0 and the same
      Freeze (the frozen buffer holds; no regeneration).
    - Extend `"processor does not allocate"` to set Feedback 0.5 and Mix 1.
- [ ] **Step 2:** Run `ctest --preset dev -L core -LE slow`, then the slow one alone. Expected: FAIL.
- [ ] **Step 3: Implement.** Retarget `feedback_` in `applyPending` beside `mix_`. The
  instrument's `render` keeps its piece size and never builds `ringIn_`.
- [ ] **Step 4:** Tests PASS; `scripts/update-goldens.sh`: every WAV unchanged; `scripts/check.sh`.
  Check `rtsan` is green on the new `[rt]` case.
- [ ] **Step 5: Commit** `feat(core): Feedback regenerates the limited wet signal into the live
  ring, 256 samples late`.

### Task 5: Fresh seeds, the seed hand-off and the value mirror (rows 115, 123)

**Files:**
- Modify: `engine/include/gg/engine/Engine.h` (`setSeed`), `engine/src/Engine.cpp`,
  `core/include/gg/core/Processor.h`, `core/src/Processor.cpp` (`setSeed`, applied in
  `applyLoadedState`'s caller), `plugin/src/GigaGrainsPlugin.h:258-263`,
  `plugin/src/GigaGrainsPlugin.cpp` (constructor, `stateLoad:868-903`,
  `applyLoadedState:640-652`, `onMainThread`), `plugin/src/LatestValue.h` → moved to
  `core/include/gg/core/LatestValue.h` (namespace `gg::core`)
- Test: `tests/plugin/params_tests.cpp`, `tests/plugin/PluginHarness.h`,
  `tests/engine/determinism_tests.cpp`

**Interfaces:**
- Produces: `void Engine::setSeed(uint64_t seed) noexcept;` (sets `config_.seed`; used by the
  next `schedule`); `void Processor::setSeed(uint64_t) noexcept;`
  `uint64_t Processor::seed() const noexcept;`. The plugin's loaded-state snapshot becomes
  `struct LoadedState { ParamValues values; uint64_t seed; }` in `LatestValue<LoadedState>`;
  the audio thread applies both and then sets `std::atomic<bool> valuesApplied_` and calls
  `requestCallback()`; `onMainThread` sees the flag and calls
  `_host.paramsRescan(CLAP_PARAM_RESCAN_VALUES)` (row 123). `stateLoad` no longer calls
  `requestRestart` for a new seed (row 115). The constructor draws `seed_` from
  `std::random_device` (two 32-bit draws), falling back to a splitmix64 of
  `std::chrono::steady_clock::now()` if it throws. `gg::test::PluginUnderTest::loadDefaultsWithSeed(
  uint64_t)` in `PluginHarness.h` loads an encoded default state with that seed, for tests that
  compare instances.

- [ ] **Step 1: Write the failing tests.**
  - `params_tests.cpp`: `"a new instance draws its own seed"`: two instances' saved states carry
    different seeds and neither is 0 (the chance of a collision is 2^-64); `"a loaded state's
    seed reaches the audio thread without a restart"`: activate, process, load a state with
    seed 7, process a block, save: the saved seed is 7, `host.restarts() == 0`, and the output
    of the next 2 s equals a fresh instance loaded with the same state and processed the same
    way (`sameBits`); `"the value mirror is rescanned after the audio thread applies a state"`:
    load while active, `valueRescans()` is 1 at once and 2 after `process` + `on_main_thread`.
  - Fix every plugin test that compares two instances or an instance with a `Processor` by
    loading `loadDefaultsWithSeed(0)` first (grep `sameBits` in `tests/plugin`).
  - `determinism_tests.cpp`: `"setSeed changes the next grains only"`: render 1 s, `setSeed(5)`,
    render 1 s: the second second equals a fresh engine seeded 5 whose scheduler was advanced
    the same way? Not equal (the grain index continues). Pin instead: the first second is
    unchanged and the second differs, and a repeated render is bit-identical.
- [ ] **Step 2:** Run `ctest --preset dev -L 'plugin|engine'`. Expected: FAIL.
- [ ] **Step 3: Implement.** Keep `stateSave` writing `seed_`; the processor's seed is the same
  value once applied, so a save during the gap writes the loaded seed, which is what the audio
  thread will use.
- [ ] **Step 4:** Tests PASS; `scripts/check.sh` (the `rt` tests cover the new apply path).
- [ ] **Step 5: Commit** `feat(plugin): a fresh seed per instance; the seed and a rescan follow a
  loaded state to the audio thread`.

### Task 6: The publish structures

**Files:**
- Create: `core/include/gg/core/GrainQueue.h`, `core/include/gg/core/UiSnapshot.h`,
  `core/include/gg/core/LivePeaks.h`, `core/src/LivePeaks.cpp`
- Test: `tests/core/publish_tests.cpp` (new; add to `tests/CMakeLists.txt:8-52`)

**Interfaces:**
- Produces (namespace `gg::core`, all JUCE-free; `GrainVis` and `UiSnapshot` as in Global
  Constraints):
  ```cpp
  /// Single-producer (audio) single-consumer (UI) ring of GrainVis; push never blocks or allocates.
  class GrainQueue { public:
    static constexpr uint32_t kCapacity = 4096;
    bool push(const GrainVis &) noexcept;          // false and ++dropped when full
    bool pop(GrainVis &) noexcept;                 // UI thread
    uint64_t dropped() const noexcept;             // relaxed
    void clear() noexcept;                          // UI thread, between frames
  };
  /// Peaks of the live ring in buckets of kBucket frames: max(|L|, |R|) per bucket, as std::atomic<float>.
  class LivePeaks { public:
    static constexpr uint32_t kBucket = 64;
    void prepare(int64_t ringFrames);               // main thread; allocates ceil(ringFrames / kBucket) + 1
    void write(const float *l, const float *r, int64_t t, uint32_t n) noexcept;  // audio: accumulates into bucket t / kBucket
    float at(int64_t frame) const noexcept;         // UI: the bucket holding ring frame `frame`
    int64_t frames() const noexcept;
  };
  ```
  `UiSnapshot.h` also holds `using SnapshotBuffer = LatestValue<UiSnapshot>;` (Task 5 moves
  `LatestValue` into core; if Task 6 lands first, put the moved header in this task and Task 5
  keeps it).

- [ ] **Step 1: Write the failing tests** (`publish_tests.cpp`, tags `[core][publish]`):
  `"the grain queue is FIFO"`; `"the grain queue drops when full and counts it"` (Review Focus
  4): push 4097 → the last returns false, `dropped() == 1`, popping yields the first 4096;
  `"the grain queue is allocation-free"` (push and pop under `AllocTripwire::Scope`);
  `"live peaks bucket by absolute frame"`: write 200 frames at t = 0 with a peak of 0.7 in frame
  70 → `at(64..127) == 0.7f`, `at(0..63)` is the max of the first 64; writing across a bucket
  boundary in two calls gives the same buckets as one call; the ring wraps at `frames()`.
- [ ] **Step 2:** Run `ctest --preset dev -L publish`. Expected: FAIL.
- [ ] **Step 3: Implement** (a classic SPSC ring with `std::atomic<uint32_t>` head and tail,
  acquire/release; `LivePeaks::write` resets a bucket when `t / kBucket` advances past it).
- [ ] **Step 4:** Tests PASS; `scripts/check.sh`.
- [ ] **Step 5: Commit** `feat(core): the grain event queue, the UI snapshot and the live peak
  strip`.

### Task 7: sst-jucegui as a pinned dependency

**Files:**
- Modify: `.gitmodules`, `libs/CMakeLists.txt:320-360` (after `add_clap_juce_shim`),
  `ui/CMakeLists.txt:31-35` (`gg-ui` links `sst-jucegui`), `THIRD_PARTY_NOTICES.md`
- Test: `tests/plugin/factory_tests.cpp` (a smoke test that a `sst::jucegui::components::Knob`
  constructs inside the existing headless editor test fixture)

**Interfaces:**
- Produces: the submodule at the pinned SHA; targets `sst-jucegui`, `sst-jucegui-resources`;
  `gg_mark_system_includes(sst-jucegui sst-jucegui-resources sst-jucegui-juce-requirements)`;
  `gg-ui` links `sst-jucegui` PRIVATE.

- [ ] **Step 1:** `git submodule add https://github.com/surge-synthesizer/sst-jucegui
  libs/sst/sst-jucegui && git -C libs/sst/sst-jucegui checkout 22a89ca4e54179cbda0b8164e54e06dae5f30490`.
  Confirm `LICENSE.md` is MIT and copy its notice (and md4c's) into `THIRD_PARTY_NOTICES.md`.
- [ ] **Step 2:** Add `set(SST_JUCEGUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)` and
  `add_subdirectory(sst/sst-jucegui EXCLUDE_FROM_ALL)` inside the `GG_BUILD_PLUGINS AND
  GG_BUILD_EDITOR` block, after the shim; mark its includes SYSTEM.
- [ ] **Step 3:** Write the smoke test in `factory_tests.cpp` (`#if GG_BUILD_EDITOR`): construct
  a `Knob`, set a `data::Continuous` source whose value is 0.25, `getValue()` reads back 0.25.
  Run `ctest --preset dev -L plugin`. Expected: PASS once it builds.
- [ ] **Step 4:** `scripts/check.sh` (the asan/rtsan presets build no editor, so also build
  `cmake --preset release && cmake --build --preset release --target gg-all` to see the shim and
  jucegui compile warning-free under `-Werror`; a warning inside jucegui means its include isn't
  SYSTEM). Push the branch as a draft PR so Windows and Linux compile it; if a leg fails, apply
  the fallback rule in Global Constraints.
- [ ] **Step 5: Commit** `build: sst-jucegui pinned at 22a89ca (MIT)`.

### Task 8: `LaneModel` and the capsule splat (JUCE-free canvas geometry)

**Files:**
- Create: `ui/include/gg/ui/LaneModel.h`, `ui/src/LaneModel.cpp`
- Modify: `ui/include/gg/ui/SplatRenderer.h`, `ui/src/SplatRenderer.cpp`
- Test: `tests/ui/splat_tests.cpp`, `tests/ui/lane_model_tests.cpp` (new; add to
  `tests/CMakeLists.txt`)

**Interfaces:**
- Produces:
  ```cpp
  // SplatRenderer
  /// A capsule: the paraboloid falloff of `add`, measured from the segment (x0, y) – (x1, y).
  void addCapsule(float x0, float x1, float y, float radius, float intensity) noexcept;

  // LaneModel (namespace gg::ui), pure functions of a Geometry and the UiSnapshot/GrainVis:
  struct LaneGeometry { float width, height, pixelScale; bool fxLane; double sampleRate; double ringSeconds /*16*/; int64_t sourceFrames /*instrument*/; };
  struct Capsule { float x0, x1, y, radius, intensity; };
  /// The grain at UI time `nowSample` (engine samples, extrapolated), given the head at that time; nullopt when it is not sounding.
  std::optional<Capsule> grainCapsule(const GrainVis &, int64_t nowSample, int64_t headAtNow, const LaneGeometry &) noexcept;
  struct Window { float x0, x1; };     // pixels
  Window scanWindow(float positionValue, float sprayValue, int64_t headAtNow, const LaneGeometry &) noexcept;   // CLAP values → pixels (FX taper: seconds = 16/255·(2^(8v) − 1), spray = 2/255·(2^(8v) − 1))
  float positionValueAtX(float x, const LaneGeometry &) noexcept;                       // inverse, for drags (std::log2 allowed here)
  float sprayValueForHalfWidth(float halfWidthPx, const LaneGeometry &) noexcept;
  enum class DragTarget { None, Body, LeftEdge, RightEdge };
  DragTarget hitTest(float x, const Window &, float edgeTolerancePx) noexcept;
  ```
  FX mapping: x = width × (1 − behind / ringSeconds) with behind = (head − pos) / sampleRate,
  the head at the right edge; instrument: x = width × pos / sourceFrames. y = height/2 ×
  (1 + pan) clamped so the capsule stays on the lane; radius = max(1.5, 3) × pixelScale;
  intensity = gain (clamped 0..1); x0/x1 span the grain's read extent `startPos … startPos +
  rate × durationSamples`, and the capsule is drawn only while `onset ≤ now < onset + dur`.

- [ ] **Step 1: Write the failing tests.**
  - `splat_tests.cpp`: `"a capsule equals a splat when its ends coincide"` (`addCapsule(10, 10,
    10, 3, 0.7)` matches `add(10, 10, 3, 0.7)` pixel for pixel); `"a capsule is flat along its
    axis"`: intensity at (x, y) for x0 < x < x1 equals the splat's centre value; clipping and
    NaN rules as for `add`.
  - `lane_model_tests.cpp` (`[ui]`): `"the fx lane puts the head at the right edge"`: a grain at
    `startPos = head − 8 s` is at x = width/2 (±0.5 px); `"a grain scrolls left with time"`;
    `"pan maps to y"`; `"the scan window follows the position and spray tapers"`: Position
    0.28967658 (0.25 s) and Spray 0.36033038 (50 ms) give a window centred 0.25 s behind the
    head, 0.1 s wide; `positionValueAtX` inverts `scanWindow` within 1e-5 across the lane;
    `"hit testing"` at the edges and body; `"a drag emits begin, values, end once"` (Review
    Focus 5) is Task 11's (it needs the component); here pin the model side:
    `positionValueAtX` clamps x outside the lane to the 0..1 value range.
- [ ] **Step 2:** Run `ctest --preset dev -L ui`. Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** Tests PASS; `scripts/check.sh` (these tests also run under asan/rtsan).
- [ ] **Step 5: Commit** `feat(ui): LaneModel geometry and the capsule splat`.

### Task 9: The Processor publishes

**Files:**
- Modify: `core/include/gg/core/Processor.h`, `core/src/Processor.cpp` (`process` end, `render`
  per piece, the event-sink trampoline)
- Test: `tests/core/publish_tests.cpp`, `tests/core/processor_tests.cpp` (`[rt]`)

**Interfaces:**
- Consumes: Task 6's structures; Task 4's `feedbackAt`.
- Produces:
  ```cpp
  void Processor::setPublishing(bool on) noexcept;          // main thread; atomic
  GrainQueue &Processor::grainQueue() noexcept;              // consumed by the UI thread
  SnapshotBuffer &Processor::snapshot() noexcept;            // likewise
  const LivePeaks &Processor::livePeaks() const noexcept;    // prepared in the constructor for kLiveSeconds (FX only)
  const std::atomic<int64_t> &Processor::publishedClock() const noexcept;  // sampleClock after the last process()
  ```
  The engine's sink is now the Processor's static trampoline, which calls the external sink set
  by `setEventSink` (gg-render's) and, when publishing, pushes a `GrainVis`. At the end of
  `process()`, when publishing: `snapshot().push({clock, engine_.liveWriteHead(),
  engine freeze state, engine_.activeGrains(), base_[Position], base_[Spray],
  effective_[Position], effective_[Spray], feedbackAt(clock)})` and `publishedClock_.store(clock,
  release)`. In `render`, per piece, when publishing and in FX mode: `livePeaks_.write(ringIn or
  dry per channel, from, n)`.

- [ ] **Step 1: Write the failing tests.** `"publishing off costs nothing"`: with publishing
  off, the queue stays empty and the snapshot has no fresh value after a process; `"the
  snapshot carries effective position and spray"`: Macro Spray at 1 → `effectiveSpray ==
  base + 0.5` within 1e-6 and `clock == frames processed`; `"grain events reach the queue"`:
  Density 6 for 1 s → the queue's events equal the `GrainLog`'s grains (onset, startPos, rate,
  duration, gain, pan); `"live peaks follow the input"`: a 0.8 burst at 1.0 s → `at(48000)` is
  0.8; `"publishing does not allocate"` (`[rt]`): the tripwire around `process` with publishing
  on and the queue never drained (so it overflows), 4096-frame calls, Density 8.
- [ ] **Step 2:** Run `ctest --preset dev -L 'publish|core'`. Expected: FAIL.
- [ ] **Step 3: Implement.** `publishing_` is read once per `process()` into a local.
- [ ] **Step 4:** Tests PASS; `scripts/update-goldens.sh`: every WAV unchanged; `scripts/check.sh`
  including `rtsan`.
- [ ] **Step 5: Commit** `feat(core): the Processor publishes grains, a per-block snapshot and
  live peaks while the editor is open`.

### Task 10: The plugin's UI bridge

**Files:**
- Modify: `plugin/src/GigaGrainsPlugin.h`, `plugin/src/GigaGrainsPlugin.cpp`
  (`processNonblocking:282-358`, `paramsFlush:660-690`), `plugin/src/editor/EditorPlugin.cpp`
  (`createEditor`, gui create/destroy → publishing), `ui/include/gg/ui/Editor.h`
  (`EditorContext`), `plugin/src/SampleLoader.h/.cpp` (a waveform view of the loaded sample)
- Test: `tests/plugin/params_tests.cpp`, `tests/plugin/rt_tests.cpp`,
  `tests/plugin/PluginHarness.h` (an output-events collector)

**Interfaces:**
- Consumes: Task 5's mirror, Task 9's accessors.
- Produces, on `GigaGrainsPlugin` (main thread):
  ```cpp
  void uiBeginGesture(gg::core::ParamSlot) noexcept;   // pushes to uiQueue_, then paramsRequestFlush if canUseParams
  void uiSetValue(gg::core::ParamSlot, double value) noexcept;
  void uiEndGesture(gg::core::ParamSlot) noexcept;
  float uiBaseValue(gg::core::ParamSlot) const noexcept;  // values_ mirror
  ```
  `struct UiParamChange { clap_id id; double value; uint8_t kind; }` in a `LatestValue`-style
  SPSC ring of 1024 (reuse `GrainQueue`'s template by lifting it to `SpscQueue<T, N>` in
  `GrainQueue.h`; `GrainQueue` becomes `SpscQueue<GrainVis, 4096>`). The audio thread drains it
  in `processNonblocking` (before the host's events; a value becomes a `ParamEvent{0, slot,
  false, v}` appended to the Processor's list, with `kMaxParamEvents` raised to 4096 + 1024, and
  the mirror updated) and in `paramsFlush` (values via `setBase` when inactive, the same path as
  active), and pushes the matching `clap_event_param_gesture` / `clap_event_param_value`
  (cookie null; note_id, port, channel, key −1) to `out` with `try_push`.
  `EditorContext` gains: `std::function<float(ParamSlot)> baseValue; std::function<void(ParamSlot)>
  beginGesture, endGesture; std::function<void(ParamSlot, double)> setValue; gg::core::GrainQueue
  *grains; gg::core::SnapshotBuffer *snapshot; const gg::core::LivePeaks *livePeaks; const
  std::atomic<int64_t> *clock; double sampleRate; std::function<SampleWaveform()> sampleWaveform;
  gg::core::Mode mode;` with `struct SampleWaveform { std::shared_ptr<const std::vector<float>>
  peaks; int64_t frames; double seconds; }` (peaks in buckets of 512 frames, computed on the main
  thread once per loaded source and cached in `SampleLoader`). `guiCreate` calls
  `processor_->setPublishing(true)` and `guiDestroy` `false` (through the shim's `onShow`/
  `guiDestroy` hooks or `EditorProvider`; if the shim gives no destroy hook, the editor's
  destructor calls a context callback `publishing(bool)`).

- [ ] **Step 1: Write the failing tests.**
  - `PluginHarness.h`: `class OutputEvents` collecting `clap_output_events` pushes.
  - `params_tests.cpp`: `"ui changes reach the host as a gesture with values"`: active, call
    `uiBeginGesture(Size)`, `uiSetValue(Size, -1.0)`, `uiEndGesture(Size)`, then `process(64)`
    with an `OutputEvents`: the output holds gesture-begin, value −1.0, gesture-end for
    `0x010002` in order, `paramsValue` reads −1.0, and `host.flushRequests() == 1` (extend
    `ParamsHost` to count `request_flush`); `"ui changes apply through flush when inactive"`:
    the same before `activate`, through `params->flush`; `"a drag emits begin, values, end
    once"` (Review Focus 5, the plugin half): begin, 100 values, end → exactly one begin and one
    end in the output across two process calls, the last value the 100th.
  - `rt_tests.cpp`: `"ui parameter changes do not allocate"`: 1024 queued changes, the tripwire
    around `process` and `flush`.
- [ ] **Step 2:** Run `ctest --preset dev -L plugin`. Expected: FAIL.
- [ ] **Step 3: Implement.** `paramsFlush` stays O(events + queued changes).
- [ ] **Step 4:** Tests PASS; `scripts/check.sh`.
- [ ] **Step 5: Commit** `feat(plugin): the editor's parameter gestures reach the host; the
  editor context carries the publish structures`.

### Task 11: The Perform view

**Files:**
- Create: `ui/src/GrainCanvas.h`, `ui/src/GrainCanvas.cpp`, `ui/src/MacroRow.h`,
  `ui/src/MacroRow.cpp`, `ui/src/Theme.h` (the dark style sheet and colours)
- Modify: `ui/src/Editor.cpp` (rewrite the layout: header, canvas, macro row, the instrument's
  loader strip), `ui/include/gg/ui/Editor.h` (`kEditorMinWidth = 640`, `kEditorMinHeight =
  360`), `plugin/src/editor/EditorPlugin.cpp:102-112` (the folder callback returns the
  loader's bool and a message), `ui/CMakeLists.txt`
- Test: `tests/plugin/factory_tests.cpp` (the GUI tests; headless as today),
  `tests/ui/lane_model_tests.cpp`

**Interfaces:**
- Consumes: Tasks 7, 8, 10.
- Produces (component IDs for the tests): `"header.product"`, `"header.preset"` ("Init"),
  `"header.perform"`, `"header.edit"` (disabled), `"header.frameMs"`, `"canvas"`,
  `"macro.1"` … `"macro.8"` (jucegui `Knob`s; `"macro.6"` a `ToggleButton`), the loader strip's
  five IDs unchanged. `GrainCanvas : juce::Component` owns a `SplatRenderer` sized in physical
  pixels, a `juce::Image`, the `LaneModel` geometry, the live grain list (drained from the
  queue each frame; a grain is dropped once `now ≥ onset + dur`), the last snapshot, an
  extrapolated clock (`publishedClock` + wall time since it last changed × sampleRate, frozen
  when the snapshot says so) and the drag state; it draws the capsules through the splat
  renderer, then the peak strip (FX: `LivePeaks` scrolled so the head is at the right; instrument:
  the sample waveform), the solid scan window at the base values and the ghost window at the
  effective values when they differ, a freeze tint and a feedback marker at the head. A drag on
  the body calls `beginGesture(Position)`, `setValue(Position, positionValueAtX(...))` per move,
  `endGesture` on release (also from `mouseUp` outside the bounds and from the destructor if a
  drag is live); an edge drag does the same for Spray. Each macro is a `data::Continuous`
  adaptor over `baseValue`/`setValue` with `onBeginEdit`/`onEndEdit` → the gestures; the
  macro row refreshes from `baseValue` on a 30 Hz timer. Arrow keys on a focused macro move it
  by 1/100 of its range, Shift by 1/1000 (a focusable wrapper's `keyPressed` if jucegui's knob
  lacks it). The frame-ms readout averages 60 frames as the M0 panel did. The M0 stress panel is
  removed from the editor (`StressGrains` stays for `gg-bench --canvas`).

- [ ] **Step 1: Write the failing tests** (`factory_tests.cpp`, `#if GG_BUILD_EDITOR`, headless):
  - `"the perform view has a header, a canvas and eight macros"`: the IDs exist in both
    products; `"header.edit"` is disabled; the instrument has the loader strip and the FX none.
  - `"a macro knob writes the base value with a gesture"`: drive `macro.3`'s source
    (`setValueFromGUI(0.75)` between `onBeginEdit`/`onEndEdit`) through a context whose
    callbacks record calls: begin(Macro3), setValue(Macro3, 0.75), end(Macro3).
  - `"arrow keys adjust a focused macro"`: `keyPressed(upKey)` on `macro.1` changes its value by
    +0.01; with Shift by +0.001.
  - `"a canvas drag emits begin, values, end once"` (Review Focus 5): synthesize `mouseDown` in
    the window body, three `mouseDrag`s, `mouseUp` outside the component: one begin(Position),
    three setValue(Position, …) with values decreasing as x moves left, one end.
  - `"the canvas draws the grains the queue holds"`: push two `GrainVis` into a `GrainQueue`
    and a snapshot, call the canvas's `renderFrame()` (a public method the paint path uses) and
    read the accumulator (`accumAt`) at the expected capsule centres > 0 and at an empty spot
    == 0.
  - `"the folder button reports a failed save"`: with `setSampleFolder` returning false, the
    status label shows the returned message (row 198).
  - Row 199: `"the loader strip shows every sample state"`: every `SampleStatusView::State`
    with and without a message, including the tooltip for a long status.
- [ ] **Step 2:** Run `ctest --preset dev -L plugin`. Expected: FAIL.
- [ ] **Step 3: Implement.** Theme: `StyleSheet::getBuiltInStyleSheet(DARK)` through a
  `LookAndFeelManager` on the editor. Row 198's remaining items: the choosers start at the last
  loaded folder (remembered in the editor), long status text gets a tooltip, a drop with an
  extension outside `kSampleFilePatterns` is refused with a status message.
- [ ] **Step 4:** Tests PASS; `scripts/check.sh`; build `release` and open both plugins in the
  standalone app (`build/release/gg_assets/…app`) to see the view; push the draft PR so Linux's
  headless GUI tests and Windows compile it.
- [ ] **Step 5: Commit** `feat(ui): the Perform view: canvas with live grains and a draggable
  scan window, eight macros, dark theme`.

### Task 12: Goldens and the cross-feature determinism tests

**Files:**
- Create: `tests/golden/cases/fx-macros.json`, `fx-feedback.json`, `inst-texture.json` and
  their `expected/` files (via `scripts/update-goldens.sh`)
- Modify: `tests/metrics/click_injection_tests.cpp:102-126` (the table, if a new case hides
  steps), `tests/core/processor_tests.cpp`, `tests/core/note_tests.cpp`,
  `tests/plugin/rt_tests.cpp`
- Test: the above

**Interfaces:** consumes everything rendered so far.

- [ ] **Step 1: Write the cases.**
  - `fx-macros`: stereo noise input 0.4 for 7 s, 8 s render, Mix 1, Density 5, Size −2.5;
    `automation` sweeps each macro in turn over its travel in 5 steps (0, 0.25, 0.5, 0.75, 1)
    at 0.8 s per macro, Macro6 last (0 → 1 at 6.6 s).
  - `fx-feedback`: stereo sweep input for 1.5 s, 8 s render, Mix 1, Density 6, Size −2;
    Feedback 0.5 from 0, 0.9 at 3 s; Freeze 1 at 5 s, 0 at 6.5 s.
  - `inst-texture`: sine sample, Density 4, Size −1 (0.5 s), one note 60 for 5 s; Texture
    stepped 0, 0.25, 0.5, 0.75, 1.0 at 1 s intervals; 6 s.
- [ ] **Step 2:** `scripts/update-goldens.sh`; look at the three spectrograms
  (`build/dev/golden/*.png`) and describe each in the commit; the 19 old WAVs unchanged. Run
  `ctest --preset dev -L golden` and `-L metrics`; add any new case the click table needs with
  its count and reason.
- [ ] **Step 3: Cross-feature determinism and rt tests.**
  - `processor_tests.cpp`: `"the perform parameters are block-size invariant together"`: FX,
    `automation()` now covering all 28 slots plus Texture, Feedback and every macro automated at
    non-grid times with modulation; splits `{4096,{1}}, {4096,{7}}, {4096,{64}}, {4096,{511}},
    {4096,{4096}}, {64,{4096}}, {1,{4096}}, {256,{4096}}` and three random splits.
  - `note_tests.cpp`: the instrument's invariance test gains Texture and the macros.
  - `rt_tests.cpp`: `"plugin process with the perform parameters automated does not allocate"`:
    events on all 28 slots, publishing on, 8 calls as the Space case.
- [ ] **Step 4:** `scripts/check.sh` all three presets (the slow Feedback case included once).
- [ ] **Step 5: Commit** `test: fx-macros, fx-feedback and inst-texture goldens; perform
  parameters block-size invariant and allocation-free`.

### Task 13: INTENT, the spec, CLAUDE.md and the carry-over

**Files:**
- Modify: `INTENT.md` (§14, §15, §17, §18, §23), `docs/superpowers/specs/2026-10-27-m1-perform-ui-design.md`
  (Status row and in-place amendments), `CLAUDE.md` (gg-render's parameter names), `docs/m1-carry-over.md`

- [ ] **Step 1: INTENT.**
  - §14: after "Two-rate modulation", a **Macros** paragraph: relative, fixed recipes (the
    table), `effective = clamp(base + mod + Σ)`, polarity, on the grid; a **Texture** paragraph
    with the envelope family and the bit-identity at 0; a **Feedback** paragraph with the ring
    write, `fb ≤ 0.95`, D = 256 = M, the Processor's 256-sample pieces, Freeze wins.
  - §15: "sst-jucegui is in use since M1 sub-project 5 (§23 #58), pinned at `22a89ca`" beside
    the sst-effects sentence.
  - §17: "The M1 Perform view as built" (fixed-head FX lane, pan on y, the macro row, base-value
    drags with ghosts, the instrument's slot 7/8 substitutions, three remote pages); the peak
    pyramid realised as one 64-frame level in M1.
  - §18: the ten IDs appended to the table with "Sub-project 5's ten"; "New instances start at
    seed 0" → "A new instance draws its seed from the OS; the state carries it"; the remote
    pages paragraph.
  - §19: the grain-event log's field list gains texture.
  - §23: #58 (the director's decisions 1–6 of the spec, the plan's envelope family, the MIT
    correction, the chunking in the Processor, the one-level peak strip, Decided 2026-10-29) and
    #59 (the fresh-instance seed and the seed hand-off, row 115).
- [ ] **Step 2: The spec:** Status row lists the amendments; blockquotes at §4 (the family),
  §5 (Processor pieces), §6.4 (one level), §6.5 (MIT).
- [ ] **Step 3: CLAUDE.md:** the gg-render FX-mode `params` names list gains Texture, Feedback
  and the macros (both products' names).
- [ ] **Step 4: Carry-over:** close rows 115, 123, 198, 199 with the commits; add a "From M1
  sub-project 5" section only for rows blocked outside our control, if any; count the rows
  lacking "Closed" before and after and state both in the commit (no net growth).
- [ ] **Step 5: Commit** `docs: INTENT #58/#59, spec amendments, carry-over rows 115/123/198/199
  closed`.

### Task 14: Validators, bench check and the listening pack

**Files:**
- Create: `docs/listening/m1-perform/README.md`, `docs/listening/m1-perform/specs/*.json`

- [ ] **Step 1:** `cmake --build --preset release --target gg-products`, then clap-validator,
  pluginval at strictness 10, Steinberg's validator and auval ×3 (CLAUDE.md commands). Paste the
  summaries. clap-validator's `param-conversions` must pass with the stepped Macro6.
- [ ] **Step 2:** `gg-bench --quick` on `release`: the three ratios beside Task 3's; the full
  5-minute run is CI's.
- [ ] **Step 3: The listening pack** (as `docs/listening/m1-space/README.md`): one render per
  macro sweep (8), Texture 0 → 1 over a held instrument note, Feedback 0.5 and 0.9 with and
  without Freeze, plus the host checklist: both products in Live 12 and Bitwig, each macro
  swept, depths tuned by ear (the values to commit in `ParamMap.cpp` and the spec's decision
  notes), Feedback and Freeze in a performance, the canvas judged against what is heard, the
  frame-ms readout at 8,192 grains. Add the specs so `"every committed render spec parses"`
  counts them.
- [ ] **Step 4:** `scripts/check.sh` once more on the final branch; `scripts/update-goldens.sh`
  prints the 22 hashes, the 19 old ones equal to `main`'s.
- [ ] **Step 5: Commit** `docs: the Perform listening pack; validator and bench results`.
