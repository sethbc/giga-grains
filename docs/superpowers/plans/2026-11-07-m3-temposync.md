# M3 Tempo sync of rates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The layer's modulators, the global delay, the live ring's loop and hold, and (with sub-project 3b) the playhead's scan lock to the host's tempo when the user asks, each synced value a closed form of one sync clock's beat line; with every switch Off nothing moves.

**Architecture:** A header-only `gg::core::SyncClock` owned by the `Processor` keeps a closed-form beat line `β(t) = beatA + (t − a) × k`, relocked only at a jump, a tempo change, a play-state change or a meter change, from the host transport each `setTransport` observes; it resolves at the first grid boundary at or after the call's start, inside `stepSourcesAt` before the modulators step. Constexpr division tables snap each rate knob to a note value at a 120 BPM reference. The modulators, `delayParams`, `toRingParams`, a per-layer freeze quantizer in `syncFreeze` and (last) the Engine's playhead read the line; `gg-render`'s `transport` gains a meter, a bar start and timed changes, cut into its call loop.

**Tech Stack:** C++23, CLAP 1.x, clap-wrapper, Catch2, CMake presets, `gg-render` goldens.

**Spec:** `docs/superpowers/specs/2026-11-07-m3-temposync-design.md` (R0 to R15, RB1 to RB20; parent `docs/superpowers/specs/2026-11-07-m3-program-design.md`). File:line references are at `9f15140` (this branch after merging `m3-voice` at `b8dd6ac`). Sub-project 3b's names (Task 14) are as built on `m3-sp3b-playhead` at `58cc3d4`; re-read them when 3b merges.

**Plan-level decisions (Claude's, the director asked for progress without his input):**
- **P1.** `Transport` moves from `ModSources.h` to a new `core/include/gg/core/Transport.h` (ModSources.h includes it, so every user is unchanged), so `SyncClock.h` doesn't pull in the modulators.
- **P2.** The Edit-view placements (spec §7 step 7) go with the parameters (Task 4): the frame tests' "every parameter is reachable from the UI" would fail in between otherwise.
- **P3.** A relocking observation adds an applied boundary (RB6) by `join(0)` at `process()`'s start, decided at observation time by `SyncClock::pendingChangesLine()` against the line as it stands (the resolution at the boundary sees the same line, so prediction and resolution agree). A steady observation never joins, so a host sending its transport every call adds no boundary.
- **P4.** While any layer's quantized action pends, every grid boundary applies (as for spawning voices, Voice spec B1), and the action fires at the boundary equal to its `at`. Pending starts and ends at boundaries fixed by events and the line, so this is block-size invariant; with Ring Quantize Off nothing ever pends.
- **P5.** Every relock, rebase, play-state change and meter change increments `epoch` (stop included: RB4's run-on is a rebase).
- **P6.** The quantizer's state is RB10's plus the epoch it was computed at: `{bool pending; bool target; int64_t at; uint32_t epoch;}`; a pending splice is `{bool pending; double value; int64_t at; int64_t lineSample; uint32_t epoch;}`. Ring Quantize switched to Off while an action pends applies it at that boundary.
- **P7.** Ring Quantize joins a new predicate `isInstanceSetting(ParamSlot)` (`isMidiSetting(s) || s == ParamSlot::RingQuantize`) that `keepRingParams` and `keepRingLengths` use; `isMidiSetting` itself is untouched (sub-project 2 edits it too).
- **P8.** `gg-render` gains a test seam, `RenderOptions::transportEveryCall` (no CLI flag): at every call start it sends the host's transport as a host would (the beat computed in double, then through CLAP's 2⁻³¹ fixed point), so the in-process determinism test compares a host sending every call with the set-at-changes form.
- **P9.** The rate functions keep their signatures and gain overloads with the clock's values (`delayParams(effective, tempoBpm)`, `toRingParams(effective, layer, tempoBpm, barBeats)`); the old forms call the new ones at 120 BPM and 4 beats a bar, and each Off branch is today's expression unchanged.
- **P10.** Task order puts the records and the gate (Tasks 12, 13) before Scan Sync (Task 14), so the branch can merge without Scan Sync if 3b hasn't merged (RB14); Task 14 then amends the records and runs its own gate, in this branch or its own pull request.

## Global Constraints

- Audio thread: no allocation, locks, I/O, syscalls or unbounded loops; RTSan-clean; 0- and 1-sample blocks; `params.flush` trivial.
- Determinism: randomness only from `gg::rng` (seed, layer, stream, field, index); no `std::` distributions; output bit-identical at any host block size; layers summed A to D.
- At rest nothing moves: with the new feature at rest every existing golden keeps its WAV SHA-256. The at-rest check: `/home/sethbc/src/giga-grains-m3-voice/.superpowers/sdd/2026-11-07-m3-voice/check-wav-hashes.sh <worktree>` compares the 108 M2-era cases (last line `baseline cases: 108, changed: 0, gone: 0`); also confirm the 8 voice goldens (tests/golden/cases/*voice*) are unchanged unless the task changes voices on purpose.
- Stage files by name; never `git add -A` or `git commit -a` (the at-rest check rewrites expected files while it runs). Never commit regenerated expected files of existing cases (this Linux machine's metrics differ in the last digit).
- CLAP IDs append-only; your sub-project's index ranges as the spec says.
- `-Wall -Wextra -Werror`; GCC 13's diagnostics (CLAUDE.md). Test names ASCII only; tests parallel-safe.
- Format: `CLANG_FORMAT=$HOME/.local/share/giga-grains/format-venv/bin/clang-format scripts/format.sh`.
- New build trees: `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Models: implementers opus for design-heavy tasks, sonnet otherwise; reviewers sonnet at least; never Fable.
- Branch hygiene: this branch merges `m3-voice` (sub-project 1) as it moves; parallel sub-projects 2, 3, 4 edit `core/include/gg/core/ParamIds.h` lists and `Processor::updateVoices` — keep such edits small and self-contained to ease merges.
- **This sub-project's indices** (spec §1, R14): layer **0x7C Mod Sync, 0x7D Loop Sync, 0x7E Scan Sync** (Scan Sync with Task 14 only), **0x7F unused**; global **0x48 Delay Sync, 0x49 Ring Quantize**, **0x4A to 0x4F unused**; `rng::Field` **`ModRandom1Synced = 0x48`, `ModRandom2Synced = 0x49`**. The new list `GG_LAYER_PARAMS_M3_TEMPOSYNC(X)` sits **last** in both `GG_LAYER_PARAMS` and `GG_LAYER_PARAMS_SINCE_M1`. `kNextFreeLayerIndex` and `kNextFreeGlobalIndex` are untouched.
- **The clock's arithmetic** (RB2) is in double under `GG_FP_CONTRACT_OFF` (`engine/include/gg/core/FpContract.h`); positions int64, beats double; libm limited to `std::floor`, `std::fabs` and `exp2Det` on the audio thread (RB17).
- **Transport Beat, Bar and Playing keep their own per-cell accumulator** (R1); `fx-mod-transport` keeps its WAV SHA-256. Transport Bar's new expression (R13) equals M2's in 4/4 with no bar offset, bit for bit.
- **No patch-version bump and no new event-log field** (R12, §4.9). The clock's state is never saved.
- **The tag `[temposync]`** (CTest label `temposync`) on every new test case, beside its area tag (`[core]`, `[engine]`, `[plugin]`, `[render]`, `[sweep]`, `[ui]`; `[determinism]` and `[rt]` where they apply). Never `sync` alone.
- **Task 14 (Scan Sync) needs sub-project 3b merged** (RB14). Before it, 0x7E is reserved and Scan Sync's row is absent.
- Models for this plan: opus implements Tasks 1, 3, 6, 8 and 14; sonnet the rest.

## Review Focus

1. **A host that omits parts of its transport mid-song** (a call with `process->transport` null, a playing call without `CLAP_TRANSPORT_HAS_BEATS_TIMELINE`, a call without `CLAP_TRANSPORT_HAS_TEMPO`): a null transport is no observation and the line runs on; a timeline-less observation never moves the position, only the tempo. (Task 1's `sync clock without a timeline keeps its position`, Task 3's `a call without a transport leaves the line`.)
2. **A host tempo ramp sent every call** (120 to 121 BPM over 2 s, a different tempo each call) under Delay Sync and Loop Sync: every call rebases, the line stays continuous (no beat jumps by more than one call's worth), nothing clicks and the output is finite. (Task 11's `sweep temposync tempo ramp every call`.)
3. **Song positions far from zero and negative** (a pre-roll at beat −4.25; bar 25,000, beat 100,000): synced values are right, Step's index is the Euclidean mod, the Random key is the two's complement of a negative period, `nextLineSample` finds lines at negative beats. (Task 1's `sync clock lines at negative beats`, Task 6's `synced modulators far from zero and negative`.)
4. **Tempo extremes, 1 and 1000 BPM,** with Delay Sync and Ring Quantize: the halving is bounded (at most `kMaxHalvings` = 12) and lands at or below 2 s; at 1000 BPM a beat line closer than the 20 ms ramp moves the hold to the next line; at 6 BPM a hold pends for 39 s and still lands; 1 BPM's 4-minute bar is exact arithmetic. (Task 7's `delay sync halves at 1 bpm`, Task 8's `quantize at 1000 bpm and 6 bpm`, Task 1's `sync clock tempo is sanitized`.)
5. **A pending hold meets Ring Quantize Off, a stop, a reset, a state load or a preset load:** Off and a stop apply it at that boundary; reset, a state load and a preset load's `setBase` of Freeze clear it and apply at once; nothing waits forever. (Task 8's `quantize off applies a pending hold`, `a stop applies a pending hold`, `reset and loads clear a pending hold`.)

---

### Task 1: The sync clock

**Files:**
- Create: `core/include/gg/core/Transport.h` (P1: `struct Transport` moved from `core/include/gg/core/ModSources.h:219-226`, gaining `int beatUnit{4};` and `double barStart{0.0};`), `core/include/gg/core/SyncClock.h`, `tests/core/sync_clock_tests.cpp` (tags `[core][temposync]`; add to `tests/CMakeLists.txt`)
- Modify: `core/include/gg/core/ModSources.h` (include `Transport.h`; the struct removed)

**Interfaces:**
- Produces (all `noexcept`, header-only, no allocation):
  - `enum class SyncLine : uint8_t { Beat, Bar };`
  - `class SyncClock` with `explicit SyncClock(double sampleRate)`; `void reset()` (RB4: `a = 0`, `beatA = 0`, T 120, internal, not locked, epoch 0, 4/4, offset 0, no pending); `void observe(const Transport &host, int64_t s)` (latest wins); `bool pending() const`; `bool pendingChangesLine() const` (P3: whether resolving the pending observation now would increment the epoch); `void resolveAt(int64_t b)` (resolves a pending observation, if any, whose `s <= b`); `double beatAt(int64_t t) const`; `int64_t nextLineSample(SyncLine line, int64_t from) const` (the first line whose `llround`ed sample is `>= from`); `double tempo() const`; `double beatsPerSample() const` (k); `double barBeats() const`; `double barOffset() const`; `bool locked() const`; `uint32_t epoch() const`; `int64_t anchorSample() const`; `double anchorBeat() const`.
  - The rules are RB2 to RB5 with P5: steady when locked, playing, `|h − beatAt(s)| <= 32 × k` and the host tempo equal to T bit for bit; else a jump relock (`a = b`, `beatA = h + (b − s) × kHost`); an equal position at another tempo a rebase (`beatA = beatAt(b)` with the old k, then the new T and k); stopped to playing always relocks; playing to stopped rebases at b and runs on; while stopped only the tempo (a rebase) and the meter change; the first observation after reset takes the host's position whatever its play state; `hasBeats` false keeps the clock internal at the host's tempo. Tempo sanitizing: non-finite or `<= 0` keeps T, else clamped to [1, 1000]. `barBeats = num × 4 / denom` (num `beatsPerBar` at least 1, denom `beatUnit` in {1, 2, 4, 8, 16, 32}, else 4/4), `barOffset = h_barStart − barBeats × floor(h_barStart / barBeats)`.

- [ ] **Step 1: Write the failing tests** in `sync_clock_tests.cpp`, each `[core][temposync]`:
  - `sync clock steady host never relocks`: 120 BPM at 48 kHz, 2,000 calls of block sizes drawn from {1, 7, 32, 64, 256, 333} by `gg::rng` (seed 7); before each call the host beat `h = llround(β_true(s) × 2^31) / 2^31` with `β_true(s) = s × 120 / (60 × 48000)` in double; `observe(host, s)`, then `resolveAt(c)` at each boundary c in the call. Assert `epoch() == 1` after the first resolution and stays 1, and `beatAt(c)` at every boundary equals, bit for bit, a second clock observed once at s = 0.
  - `sync clock jump relocks at the first boundary`: steady at 120 BPM, then an observation at s = 48,013 with `h = beatAt(s) + 0.5`; `pendingChangesLine()` is true; `resolveAt(48,032)`; `beatAt(48,032) == h + 19 × k` exactly and the epoch is one more.
  - `sync clock one sample of jitter is steady`: `h = beatAt(s) + k` → `pendingChangesLine()` false, no epoch change.
  - `sync clock tempo change is continuous`: at b = 96,000 a 90 BPM observation at an agreeing position; `beatAt(b)` before and after the rebase equal bit for bit; `beatAt(b + 48,000) − beatAt(b) == 48,000 × 90 / (60 × 48000)` within 1e-12.
  - `sync clock stop runs on and play relocks`: stopped at beat 8 from b0; observations while stopped give `h = 3.0`; `beatAt(b0 + 24,000) == beatAt(b0) + 1.0` (within 1e-12) and `locked()` false; a playing observation with `h = 0.0` relocks: `beatAt(b) == 0.0 + (b − s) × k`, `locked()` true.
  - `sync clock without a timeline keeps its position`: from reset, observations with `hasBeats = false`, tempo 100: `beatAt(t) == t × 100 / (60 × 48000)` (within 1e-12), `locked()` false; one with tempo 140 rebases; the position is never taken.
  - `sync clock latest observation wins`: two observations before any boundary (1-sample calls at s and s + 1, the second a jump); `resolveAt` applies the second only.
  - `sync clock reset`: after a relock and a meter change, `reset()` equals a fresh clock on every accessor.
  - `sync clock meter and lines`: 6/8 with `barStart = 1.5`: `barBeats() == 3.0`, `barOffset() == 1.5`; from beat 0 at 120 BPM `nextLineSample(Bar, 0) == 36,000` and `nextLineSample(Beat, 1) == 24,000`; 3/4 with no offset: bar lines every 72,000 samples; a meter change is `epoch + 1` with no position change.
  - `sync clock lines at negative beats`: anchored at beat −4.25 at sample 0: `nextLineSample(Beat, 0) == 6,000`, `nextLineSample(Bar, 0) == 6,000` (beat −4 is a 4/4 bar line), and a `from` landing one sample after a line's `llround` takes the next line.
  - `sync clock tempo is sanitized`: NaN, −5 and 0 keep T; 0.5 gives 1; 5,000 gives 1,000; at 1 BPM from beat 0, `nextLineSample(Bar, 48,000) == 11,520,000` (4 beats of 60 s).
- [ ] **Step 2:** Run `ctest --preset dev -L temposync`. Expected: FAIL (compile errors first).
- [ ] **Step 3:** Implement `Transport.h` and `SyncClock.h` per Interfaces (RB2's formulas exactly; `k` recomputed only when T changes).
- [ ] **Step 4:** Run `ctest --preset dev -L 'temposync|core' -LE slow`. Expected: PASS.
- [ ] **Step 5:** Commit `feat(core): the sync clock, a closed-form beat line relocked at a discontinuity (R1, R2)`.

### Task 2: The division tables, their text and parsing

**Files:**
- Create: `core/include/gg/core/SyncDivisions.h` (constexpr tables and snapping), `core/src/SyncDivisions.cpp` (text and parsing, main thread), `tests/core/sync_divisions_tests.cpp` (`[core][temposync]`); add the source to `core/CMakeLists.txt` and the test to `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `inline constexpr std::array<double, 36> kNoteValues` (RB8: for n = −4 … 7, `(2/3)·2^n`, `2^n`, `1.5·2^n`, sorted ascending); `inline constexpr size_t kDelayNoteCount` (the prefix up to 4 beats); `inline constexpr std::array<double, 32> kPassLengths` (`2^n`, `1.5·2^n`, n = −1 … 14, sorted); the constexpr neighbor products for each.
  - `size_t snapIndex(std::span<const double> table, std::span<const double> products, double dRef) noexcept` (RB8's rule: entry i when `e(i−1)·e(i) <= dRef² < e(i)·e(i+1)`, ends clamp).
  - `double modDivisionBeats(float rateLog2Hz) noexcept` (`D_ref = exp2Det(1 − v)`), `double delayDivisionBeats(float timeLog2Seconds) noexcept` (`2 × exp2Det(v)`, the delay prefix), `double loopSyncBeats(float loopLog2Seconds, uint8_t loopSync, double barBeats) noexcept` (§4.3: Beats → 0.25 below 0.354, 0.5 below 0.707 (thresholds `sqrt(0.125)`, `sqrt(0.5)` as constexpr literals), else `floor(b_ref + 0.5)`; Bars → `max(1, floor(b_ref / 4 + 0.5)) × barBeats`; Off → 0), `double passBeats(float scanSpeed) noexcept` (`1 / (2 |v|³)` snapped to `kPassLengths`, signed by v, 0 at v = 0).
  - `enum class ModSyncLevel : uint8_t { Off, Step, StepRandom, All };` and `bool modSyncs(ModSyncLevel, int modulator) noexcept` (modulator 0 LFO 1, 1 LFO 2, 2 Random 1, 3 Random 2, 4 Step; RB8's levels).
  - `std::string noteValueText(double beats)` (`1/64` … `1/1`, `2/1` … `32/1`, `T` triplet, `.` dotted), `std::string loopBeatsText(double beats, uint8_t loopSync, double barBeats)` (`1/4 beat`, `1/2 beat`, `1 beat`, `3 beats`, `1 bar`, `2 bars`), `std::string passText(double signedBeats)` (a `-` when backward); `std::optional<double> noteValueFromText(std::string_view)` and `std::optional<double> loopBeatsFromText(std::string_view)` (beats; nullopt for anything else), and the reference values `float modRateRef(double beats)` (`1 − log2 D`), `float delayTimeRef(double beats)` (`log2(D / 2)`), `float loopLengthRef(double beats)` (`log2(beats / 2)`; for bars `log2(2 n)`).

- [ ] **Step 1: Write the failing tests:**
  - `division tables are sorted and their products monotone` (all three tables, strictly).
  - `m2 defaults snap to straight values at 120 bpm`: `noteValueText(modDivisionBeats(0.0f)) == "1/2"` (LFO 1's 1 Hz), `log2 0.25 → "2/1"`, `1.0f → "1/4"`, `−1.0f → "1/1"`, `2.0f → "1/8"`; `noteValueText(delayDivisionBeats(std::log2(0.375f))) == "1/8."`; `noteValueText(modDivisionBeats(std::log2(6.0f))) == "1/8T"`.
  - `division snapping takes the ends and ties to the longer`: below 1/64T → 1/64T; above 32/1 → 32/1; the delay's above 4 beats → `1/1`; `dRef` exactly at `sqrt(e(i)·e(i+1))` → entry i + 1.
  - `division text round-trips every entry`: for every note value, `noteValueFromText(noteValueText(e)) == e` and `modDivisionBeats(modRateRef(e)) == e`; for the delay prefix `delayDivisionBeats(delayTimeRef(e)) == e`.
  - `loop sync counts at their thresholds and meters`: Beats: b_ref 0.35 → 0.25, 0.36 → 0.5, 0.70 → 0.5, 0.71 → 1, 2.49 → 2, 2.5 → 3; Bars at the default Loop Length (log2 2) → 4 beats in 4/4, 3 in 3/4 and 6/8; Bars b_ref 1.0 → 1 bar; the texts `"1/4 beat"`, `"3 beats"`, `"1 bar"`, `"2 bars"`, and `loopBeatsFromText` back.
  - `pass lengths snap and carry the sign`: `passBeats(0.5f) == 4.0` (`"1/1"`), `passBeats(-0.5f) == -4.0` (`"-1/1"`), `passBeats(1.0f) == 0.5` (`"1/8"`), `passBeats(0.0f) == 0.0`.
  - `mod sync levels pick their modulators`: Step only Step; StepRandom Step and both Randoms; All all five; Off none.
- [ ] **Step 2:** Run `ctest --preset dev -L temposync`. Expected: FAIL.
- [ ] **Step 3:** Implement. The products and thresholds are constexpr literals; the audio-thread functions use `exp2Det`, comparisons and `std::floor` only.
- [ ] **Step 4:** Run `ctest --preset dev -L temposync`. Expected: PASS.
- [ ] **Step 5:** Commit `feat(core): the division tables, snapped at a 120 BPM reference, with their text (R4, RB8, RB9)`.

### Task 3: The clock in the Processor, the plugin's transport, Transport Bar's meter

**Files:**
- Modify: `core/include/gg/core/Processor.h:633-636` (`setTransport`'s doc; a `SyncClock clock_` beside `transport_`; `const SyncClock &syncClock() const noexcept`), `core/src/Processor.cpp:644-651` (`setTransport` also calls `clock_.observe(transport, layers_[0]->engine.sampleClock())`), `:1245-1277` (`stepSourcesAt(c)` calls `clock_.resolveAt(c)` first), `:1459` (`reset()` resets the clock), `core/src/ModSources.cpp:318-326` (`barPhase()`: R13's `fraction((β − barOffset) / barBeats)`, the same `barBeats`/`barOffset` rules as SyncClock), `plugin/src/GigaGrainsPlugin.cpp:527-541` (fill `beatUnit` from `tsig_denom` under `CLAP_TRANSPORT_HAS_TIME_SIGNATURE`, `barStart = bar_start / CLAP_BEATTIME_FACTOR` under `CLAP_TRANSPORT_HAS_BEATS_TIMELINE`), `tests/plugin/PluginHarness.h:293` (`transportEvent` gains `denom` and `barStartBeats` defaulted to 4 and 0)
- Test: `tests/core/temposync_processor_tests.cpp` (new, `[core][temposync]`), `tests/core/mod_sources_tests.cpp`, `tests/plugin/process_tests.cpp`

**Interfaces:**
- Consumes: Task 1's `SyncClock`, `Transport` (with `beatUnit`, `barStart`).
- Produces: `Processor::syncClock()`; the clock resolved at every stepped boundary before the modulators step (RB1). No boundary is added here (Task 7 adds RB6's).

- [ ] **Step 1: Write the failing tests:**
  - `processor clock is block size invariant`: an FX `Processor`, 10 s, a transport sequence given through `setTransport` at fixed frames (120 BPM playing from beat 0; a locate to beat 2 at 3.0 s; 90 BPM from 5.0 s; stopped at 7.0 s; playing from beat 0 at 8.5 s), rendered at blocks 1, 7, 32, 64, 256 and 333 (calls cut at those frames); after each call record `(epoch(), anchorSample(), anchorBeat(), tempo())`, deduplicated: the sequences are equal bit for bit across block sizes; and, from a copy of the clock taken after the last call before each change, `beatAt(240,000)` (5.0 s) is 2 + 2 × 2 = 6.0 within 1e-12 on the line before the tempo change, `beatAt(336,000)` (7.0 s) is 6 + 3 = 9.0 within 1e-12 on the 90 BPM line, and while stopped the line runs on at 90 BPM until the play at 8.5 s relocks to 0.
  - `processor clock with a steady host every call equals set once`: the same 10 s with a steady 120 BPM host observed every call (beats through CLAP's fixed point) against observed once: `beatAt` at every boundary equal bit for bit, at blocks 7 and 333.
  - `a call without a transport leaves the line`: steady, then 20 calls with no `setTransport`: `beatAt` continues the line exactly and the epoch doesn't change.
  - `transport sources are m2s with the clock` (in `mod_sources_tests.cpp`): over 10,000 cells of 4/4 at 120 BPM from beat 0.3, `TransportSource`'s three values equal a copy of M2's `beatPhase`/`barPhase`/`playing` kept in the test, bit for bit; and `transport bar counts the hosts bar`: 6/8 (num 6, unit 8) at beat 4.5 → bar phase `0.5`; 3/4 with `barStart = 1.0` at beat 2.5 → `0.5`.
  - `the transports meter and bar start reach the clock` (`[plugin][temposync]`, in `process_tests.cpp`, both products): a 6/8 transport with `bar_start` 1.5 beats → `processor()->syncClock().barBeats() == 3.0`, `barOffset() == 1.5`; without `CLAP_TRANSPORT_HAS_TIME_SIGNATURE` → 4.0 and the offset alone.
- [ ] **Step 2:** Run `ctest --preset dev -L 'temposync|transport' -LE slow` and `ctest --preset dev -R "transport reaches the processor"`. Expected: the new cases FAIL; the existing transport test still passes.
- [ ] **Step 3:** Implement per Files.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS. Run the at-rest check. Expected: `baseline cases: 108, changed: 0, gone: 0`; `fx-mod-transport` among the unchanged; the 8 voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(core): the Processor's sync clock from the host transport; Transport Bar counts the host's bar (R13)`.

### Task 4: The parameters

**Files:**
- Modify: `core/include/gg/core/ParamIds.h` (`GG_LAYER_PARAMS_M3_TEMPOSYNC(X) X(ModSync, 0x7C, "Mod Sync") X(LoopSync, 0x7D, "Loop Sync")`, last in both macros at `:132-141`; `ParamSlot::DelaySync`, `ParamSlot::RingQuantize` after `BendRange` at `:308`; `kParamIds` `0x000048`, `0x000049` after `:498`; `isReservedIndexInUse` at `:521` gains `(scope == 0 && (index == 0x48u || index == 0x49u)) || (scope >= 1 && index >= 0x7Cu && index <= 0x7Du)`; `kLayerParamCount` 65; `isRingParam` gains `LoopSync`; `isInstanceSetting` (P7); the count comments), `core/src/ParamIds.cpp` (metadata: Mod Sync int 0..3 `{"Off", "Step", "Step + Random", "All"}`, Loop Sync 0..2 `{"Off", "Beats", "Bars"}`, Delay Sync 0..1 `{"Off", "On"}`, Ring Quantize 0..2 `{"Off", "Beat", "Bar"}`, all default 0, flags `CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED`, never modulatable), `core/include/gg/core/ModMatrix.h:139-163` (`isModulatorControl` admits `ModSync`; `isValidTarget` refuses `LoopSync`, `DelaySync`, `RingQuantize`), `core/src/ParamMap.cpp` (`isModulatorControlSlot` follows), `core/src/Preset.cpp:107-125` (`isInstanceSetting` in place of `isMidiSetting`), `core/src/Mutate.cpp:24-62` (`buildStructuralLocks` locks each layer's `ModSync`, `DelaySync`, `RingQuantize`; Loop Sync through `isRingParam`), `tools/gg-render/PresetSource.cpp:118-135` (Ring Quantize refused), `ui/src/EditLayout.cpp` (RB16: Mod Sync first in `Panel::Lfo1`, Loop Sync after Loop Length in `Panel::Ring` at `:57`, Delay Sync after Delay Time in `Panel::Delay` at `:138`, Ring Quantize last in `Panel::Midi` at `:148`; renumber the later cells), `rng::Field` in `engine/include/gg/core/Rng.h:29-43` (`ModRandom1Synced = 0x48`, `ModRandom2Synced = 0x49`)
- Test: `tests/support/ParamTable.h`, `tests/core/patch_tests.cpp`, `tests/core/preset_tests.cpp`, `tests/core/mutate_tests.cpp`, `tests/core/mod_matrix_tests.cpp`, `tests/plugin/identity_tests.cpp`, `tests/plugin/params_tests.cpp`, `tests/plugin/rt_tests.cpp`, `tests/plugin/preset_tests.cpp`, `tests/ui/edit_layout_tests.cpp`, the sweep floors

**Interfaces:**
- Produces: `LayerParam::ModSync`, `LayerParam::LoopSync`; `ParamSlot::ModSync` … `D_ModSync`, `LoopSync` … `D_LoopSync`, `DelaySync`, `RingQuantize`; `kLayerParamCount` 65, `kParamCount` 300; CLAP IDs `0x0L007C`, `0x0L007D` (L = 1..4), `0x000048`, `0x000049`; `constexpr bool isInstanceSetting(ParamSlot) noexcept`; `ModSyncLevel modSyncOf(const ParamValues &, uint8_t layer) noexcept`, `uint8_t loopSyncOf(const ParamValues &, uint8_t layer) noexcept`, `bool delaySyncOf(const ParamValues &) noexcept`, `uint8_t ringQuantizeOf(const ParamValues &) noexcept` in `ParamMap.h` (each rounded half up and clamped); gg-render names `"A Mod Sync"` … `"D Loop Sync"`, `"Delay Sync"`, `"Ring Quantize"` (automatic from the macros).

- [ ] **Step 1: Update and write the tests:**
  - `ParamTable.h` and every count: 300 parameters, 65 a layer, the eight layer IDs and two globals with their ranges, defaults and flags (no `CLAP_PARAM_IS_MODULATABLE`).
  - `temposync switches are no route targets` (`mod_matrix_tests.cpp`): `isValidTarget` false for `ModSync`, `LoopSync`, `DelaySync`, `RingQuantize`; `isModulatorControl(LayerParam::ModSync)` true.
  - `a preset load keeps loop sync and ring quantize and sets the others` (`preset_tests.cpp`, core): current A Loop Sync 2 and Ring Quantize 1, loaded A Loop Sync 0, Ring Quantize 0, A Mod Sync 3, Delay Sync 1 → after `keepRingParams` Loop Sync 2, Ring Quantize 1, Mod Sync 3, Delay Sync 1; `keepRingLengths` (INIT) keeps Ring Quantize and resets Loop Sync.
  - `an m3 voice state loads with the switches off` (`patch_tests.cpp`): a state encoded without the ten slots' values (strip them from an encoded `PatchState` of this branch, or build it as `m3-voice` writes it) reads all ten at 0.
  - `mutate leaves the five switches` (`mutate_tests.cpp`): 50 presses at amount 1 of a state with every switch at its maximum: all ten slots unchanged.
  - `pack preset refuses loop sync and ring quantize` (`tests/tools/`, `[render][temposync]`): a source setting `"A Loop Sync"` exits 2 naming it (the ring-parameter rule, through `isRingParam`); one setting `"Ring Quantize"` exits 2 naming it (`tools/gg-render/PresetSource.cpp:128`'s refusal gains `slot == ParamSlot::RingQuantize`; the MIDI settings' treatment unchanged).
  - `edit_layout_tests.cpp`'s "every parameter has exactly one placement" with the new slots.
- [ ] **Step 2:** Run `ctest --preset dev -L 'core|plugin|ui|render' -LE slow`. Expected: the updated cases FAIL.
- [ ] **Step 3:** Implement per Files. Short labels ("Sync", "Quantize") only if the elision check needs them.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS, the frame tests included ("every parameter is reachable from the UI", the elision checks, `editor_labels_dejavu`). Check `-L sweep`; adjust a floor only with its reason. Run the at-rest check. Expected: `baseline cases: 108, changed: 0, gone: 0`, voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(params): Mod Sync and Loop Sync per layer, Delay Sync and Ring Quantize (0x7C, 0x7D; 0x48, 0x49)`.

### Task 5: Synced text, parsing and the rescan

**Files:**
- Modify: `plugin/src/GigaGrainsPlugin.cpp:1452-1500` (`paramsValueToText`, `paramsTextToValue`), `:641-671` (`checkKinds` also compares each layer's Mod Sync and Loop Sync and the Delay Sync shown, and requests the same `CLAP_PARAM_RESCAN_TEXT`), `plugin/src/GigaGrainsPlugin.h` (the shown switches), `ui/src/ParamControl.cpp` (the editor's text through the same helper), `core/include/gg/core/SyncDivisions.h` and `core/src/SyncDivisions.cpp` (the helper below)
- Test: `tests/plugin/temposync_plugin_tests.cpp` (new, `[plugin][temposync]`), `tests/core/sync_divisions_tests.cpp`

**Interfaces:**
- Consumes: Task 2's text functions; Task 4's `modSyncOf`, `loopSyncOf`, `delaySyncOf`.
- Produces: `std::optional<std::string> syncedText(ParamSlot slot, float value, const ParamValues &values)` (the synced text when `slot` is a rate its layer's or the global switch syncs: the five rates by `modSyncs`, Loop Length by Loop Sync at 4/4 (the main thread has no meter; RB12), Delay Time by Delay Sync; else nullopt) and `std::optional<float> syncedFromText(ParamSlot, std::string_view, const ParamValues &)` (the entry's reference value; nullopt when unsynced or unparsed, so the caller falls back to `paramMeta`).

- [ ] **Step 1: Write the failing tests:**
  - `synced rates show divisions` (core): A Mod Sync All, A LFO 1 Rate log2 6 → `"1/8T"`; at Mod Sync Step, LFO 1 Rate → nullopt and Step Rate 2.0 → `"1/8"`; Delay Sync On, Delay Time log2 0.375 → `"1/8."`; A Loop Sync Bars, Loop Length log2 4 → `"2 bars"`.
  - `plugin rate text follows the switches`: through `params.value_to_text` and `text_to_value` in both products: free `LFO 1 Rate` shows Hz; after a param event setting A Mod Sync 3 and a process call, it shows `"1/2"` at 0; `text_to_value("1/4.")` gives `modRateRef(1.5)`; `text_to_value("2 Hz")` while synced falls back to the free parse.
  - `plugin rescans text when a switch changes`: a host stub counting `request_callback`, then `on_main_thread` calling `params.rescan` with `CLAP_PARAM_RESCAN_TEXT`, once per change of A Mod Sync, A Loop Sync and Delay Sync; none for Ring Quantize.
- [ ] **Step 2:** Run `ctest --preset dev -L temposync -LE slow`. Expected: FAIL.
- [ ] **Step 3:** Implement. The helpers run on the main thread only.
- [ ] **Step 4:** Run `ctest --preset dev -L 'plugin|temposync|ui' -LE slow`. Expected: PASS.
- [ ] **Step 5:** Commit `feat(plugin): synced rates show and parse note values; text rescans on a switch (RB9)`.

### Task 6: Mod Sync

**Files:**
- Modify: `core/include/gg/core/ModSources.h:34-160` (`LfoParams`, `RandomParams`, `StepParams` gain `double divisionBeats{0.0};`; `Lfo::step`, `RandomSource::step`, `StepSequencer::step` gain a last parameter `double beat = 0.0`), `core/src/ModSources.cpp` (the synced forms), `core/src/ParamMap.cpp` (`lfoParams`, `randomParams`, `stepParams` set `divisionBeats = modDivisionBeats(rate)` when `modSyncs(modSyncOf(effective, layer), m)`, else 0), `core/src/Processor.cpp:1250-1263` (`stepSourcesAt` reads `const double beat = clock_.beatAt(c)` once and passes it; the synced Randoms' fields `ModRandom1Synced`, `ModRandom2Synced`)
- Test: `tests/core/mod_sources_tests.cpp`, `tests/core/temposync_processor_tests.cpp`

**Interfaces:**
- Consumes: Task 2's `modDivisionBeats`, `modSyncs`; Task 3's `clock_`; Task 4's `modSyncOf` and the two `rng::Field` values.
- Produces: `stepSourcesAt` passes `rng::Field::ModRandom1Synced` / `ModRandom2Synced` to a Random whose `divisionBeats > 0`, else today's field. With `divisionBeats > 0`, R5's closed forms (φ = `frac(beat / D)`, `k = floor(beat / D)` as int64): LFO the shape at φ; Random target `uniform01(hash({seed, layer, 0, field, static_cast<uint64_t>(k)}))` with field the synced one, output `d(k−1) + (d(k) − d(k−1)) × g(min(1, φ / s))`, `g(x) = x²(3 − 2x)`, s = 0 → `d(k)`; Step `steps[((k % len) + len) % len]`. Each synced step then writes RB7's free state (LFO `phase_ = φ`; Random `phase_ = φ`, `target_ = d(k)`, `out_` the output, `started_ = true`, its own `k_` untouched; Step `phase_ = φ`, `k_` the Euclidean index). With `divisionBeats == 0`, today's code path unchanged.

- [ ] **Step 1: Write the failing tests:**
  - `synced lfo random and step are closed forms of the beat`: 10,000 cells at a beat line `β_c = 1.37 + c × 32 × k` (k at 120 BPM, 48 kHz), D = 1/3 (LFO Square), 1/4 (Random, Smooth 0.5), 1/2 (Step, length 6): each output equals the closed form computed in the test, bit for bit.
  - `a looped transport replays synced values`: the same beats visited twice (cells for beats 2.0 to 6.0, then again from 2.0): the second pass's values equal the first's bit for bit, random draws included.
  - `synced random is continuous at period boundaries`: Smooth 0.25, 0.5, 1.0: the value at `φ → 1⁻` of period k − 1 and at φ = 0 of period k differ by under 1e-6 (both `d(k−1)` in the limit); Smooth 0: a step.
  - `synced modulators far from zero and negative`: beat −4.25 with D 1/2 → Step index `length − ...` by the Euclidean mod (length 6: k = −9 → index 3), the Random key `static_cast<uint64_t>(int64_t{−9})`; beat 100,000.375 → φ and k exact.
  - `leaving sync never jumps`: Sine LFO 4 Hz synced at 1/8 for 1,000 cells then free: `|v_free − v_last_synced| <= 0.01`; Random and Step continue from their synced target and step.
  - `mod sync off is m2 bit for bit`: 10,000 cells with `divisionBeats = 0` and any beat: outputs equal the existing `step(params, cellSeconds)` path's.
  - `mod sync levels sync their modulators` (`temposync_processor_tests.cpp`): an instrument `Processor`, one held note, routes from each modulator to a target; at A Mod Sync Step only the Step source's values follow the clock (compare two renders whose transports differ by a locate: only synced sources differ), at Step + Random the Randoms too, at All the LFOs too.
  - `mod sync is block size invariant` (`[determinism]` too): A Mod Sync All, routes from LFO 1, Random 1 and Step to Pitch, Size and Pan, a transport with a locate and a tempo change; blocks 1, 7, 32, 64, 256 and 333 give the same output bits.
- [ ] **Step 2:** Run `ctest --preset dev -L 'temposync|mod'`. Expected: FAIL.
- [ ] **Step 3:** Implement. No new allocation, one `std::floor` and one division per synced modulator per cell.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS. Run the at-rest check. Expected: `baseline cases: 108, changed: 0, gone: 0`, voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(core): Mod Sync, the LFOs, Randoms and Step as closed forms of the song's beat (R5, RB7)`.

### Task 7: Delay Sync and Loop Sync

**Files:**
- Modify: `core/include/gg/core/ParamMap.h:98,278` and `core/src/ParamMap.cpp` (P9: `DelayParams delayParams(const ParamValues &, double tempoBpm) noexcept`, R6: Delay Sync On → `D × 60 / T` halved while above `Delay::kMaxSeconds`, at most `kMaxHalvings = 12`; `RingParams toRingParams(const ParamValues &, uint8_t layer, double tempoBpm, double barBeats) noexcept`, R7: Loop Sync on → `loopSyncBeats(...) × 60 / T`, then the existing clamps), `core/include/gg/core/Processor.h:145-156` (`RingMeta` gains `int64_t loopFrames{0}`, the loop frames set at the last applied boundary), `core/src/Processor.cpp:1384-1396` (both call sites pass `clock_.tempo()`, `clock_.barBeats()`; `publishRing` fills `loopFrames`), `process()` at `:2231` (P3: `if (clock_.pendingChangesLine() && tempoFeaturesEngaged()) join(0);` beside the `transportChanged_` join), `plugin/src/MediaSession.cpp:213-224` (`windowOf` takes the published `loopFrames` for a Loop-Synced layer; RB12), `tools/gg-render/Render.cpp:496-501` (the same for a capture)
- Test: `tests/core/delay_tests.cpp`, `tests/core/parammap_tests.cpp`, `tests/core/temposync_processor_tests.cpp`, `tests/plugin/persist_tests.cpp`

**Interfaces:**
- Consumes: Task 2's `delayDivisionBeats`, `loopSyncBeats`; Task 3's clock; Task 4's `delaySyncOf`, `loopSyncOf`.
- Produces: `bool Processor::tempoFeaturesEngaged() const noexcept` (any layer's Loop Sync not Off, or Delay Sync On, in `effective_`; Task 14 adds Scan Sync); `RingMeta::loopFrames`.

- [ ] **Step 1: Write the failing tests:**
  - `delay sync targets the note value` (`parammap_tests.cpp`): Delay Sync On, Time log2 0.375 → `timeSeconds == 0.375f` at 120 BPM and `0.3f` at 150; Off → `exp2Det` of the slot at any tempo, equal to the one-argument form bit for bit.
  - `delay sync halves at 1 bpm`: Time at 1/1 (4 beats): at 60 BPM 4 s halves once to `2.0f` (not above 2, kept); at 1 BPM 240 s halves seven times to `1.875f`; at 1000 BPM `0.24f`, no halving.
  - `delay sync glides on a tempo change` (`temposync_processor_tests.cpp`): FX, Delay Sync On, 120 BPM then 150 at 1.0 s: the delay's read target (`Delay` test hook or the impulse response's echo time, the implementer picks one and states it) moves from 18,000 to 14,400 samples and the read reaches it by the glide, no step in the output (click score under 15).
  - `loop sync frames at tempos and meters` (`parammap_tests.cpp`): Loop Sync Bars, Loop Length log2 2: 2.0 s at 120 in 4/4, 1.5 s in 3/4, 2.4 s at 100 BPM; Beats at log2 0.25 (b_ref 0.5) → 0.25 s at 120; Off equals the two-argument form bit for bit.
  - `a tempo change is a loop window change at its boundary`: FX, A Live, Loop On, Loop Sync Bars, 120 then 90 BPM at 2.0 s: `ringMeta(0).loopFrames` is 96,000 before and 128,000 from the boundary after 2.0 s, at blocks 7 and 256 at the same sample.
  - `a steady host adds no boundary under loop sync`: with Loop Sync on, a steady host observed every call renders the same bits as observed once (blocks 7, 333).
  - `capture uses the published loop frames` (`persist_tests.cpp`): a Loop-Synced frozen ring at 100 BPM, one bar: the saved span is 115,200 frames (2.4 s at 48 kHz), not Loop Length's base value.
- [ ] **Step 2:** Run `ctest --preset dev -L 'temposync|fx|source|media'`. Expected: FAIL.
- [ ] **Step 3:** Implement per Files.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS. Run the at-rest check. Expected: `baseline cases: 108, changed: 0, gone: 0`, voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(core): Delay Sync and Loop Sync, on the clock's tempo and bar (R6, R7, RB6, RB12)`.

### Task 8: Ring Quantize

**Files:**
- Modify: `core/include/gg/core/Processor.h` (per layer `FreezeQuantizer` and `PendingSplice`, P6; `bool anyQuantizedPending() const noexcept`), `core/src/Processor.cpp:1440-1457` (`syncFreeze` gains `int64_t b` and `bool atBoundary`: at a boundary the quantizer takes `request = on && armed` and produces `quantized`; `engineFrozen = quantized || (noteHeld && Cloud)`; at a call's start it passes the last `quantized` through), `:1813-1846` (`applyCommands`: AddSplice pends per RB11; AddSpliceAt and ClearSplices unquantized; ClearSplices cancels a pending one), `:2160-2206` (`applyCellsBelow` also runs while `anyQuantizedPending()`, P4; a pending action fires at the boundary equal to its `at`), `reset()`, the state load and `setBase(Freeze)` (clear and apply at once, RB10)
- Test: `tests/core/temposync_ring_tests.cpp` (new, `[core][temposync]`; add to `tests/CMakeLists.txt`), `tests/plugin/persist_tests.cpp`

**Interfaces:**
- Consumes: Task 1's `nextLineSample`, `locked()`, `epoch()`; Task 4's `ringQuantizeOf`; `LiveSource::freezeRampSamples(sampleRate)` (R).
- Produces: R8's rules with P6: a hold's `t_q = nextLineSample(line, b0 + R)`, `at = 32 × floor((t_q − R) / 32)`; a resume's and a splice's `t_q = nextLineSample(line, b0)`, `at = 32 × ceil(t_q / 32)`; a splice at `at` marks `behind = value + (at − t_q) / sr` seconds behind the head; an epoch change while pending recomputes from the current boundary; `!clock_.locked()` or Ring Quantize Off at a boundary applies what pends there.

- [ ] **Step 1: Write the failing tests** (FX `Processor`, layer A Live, Loop On, Ring Quantize Bar unless stated, 120 BPM playing from beat 0; the head read from `ringMeta(0).head` after each call, calls of 1 sample around the line):
  - `a quantized hold stops the head on the line`: Freeze on at 2.7 s: the head's last advancing sample is in `[192,000 − 31, 192,000]` (4.0 s); at Ring Quantize Beat from 2.7 s, in `[144,000 − 31, 144,000]`.
  - `a quantized resume starts on the line`: Freeze off at 5.1 s: the head first advances in `[288,000, 288,031]` (6.0 s).
  - `a reversed request cancels`: on at 2.7 s, off at 3.0 s: the head never stops.
  - `a relock recomputes a pending hold`: Freeze on at 2.7 s, a locate to beat 0 at 3.0 s (sample 144,000, a boundary): the next bar line is beat 4 of the new line, sample 240,000, and the head's last advance is in `[240,000 − 31, 240,000]`.
  - `a stop applies a pending hold`: Freeze on at 2.7 s, stopped at 3.0 s: the hold applies at the boundary of 3.0 s.
  - `quantize off applies a pending hold`: Freeze on at 2.7 s, Ring Quantize 0 at 3.0 s: the hold applies at that boundary.
  - `reset and loads clear a pending hold`: `reset()`, a state load and a preset load's `setBase` of Freeze each leave nothing pending, Freeze applied at once.
  - `unquantized ring actions apply at once`: the FX's note freeze, AddSpliceAt and ClearSplices at 2.7 s apply at 2.7 s's boundary.
  - `a pending splice marks the line`: AddSplice value 0 at 1.3 s → the same mark as an unquantized AddSplice value 0 sent at sample 96,000 (2.0 s, a boundary) in a reference render; a second AddSplice before the line replaces the first (its value kept); ClearSplices cancels it.
  - `quantize at 1000 bpm and 6 bpm`: at 1000 BPM, Beat, a request 100 samples before a line (closer than R = 960) holds on the next line (`t_q` 2,880 samples later); at 6 BPM, Bar, a request at 1.0 s holds at 40.0 s within 31 samples (a 41 s render at block 4096). 1 BPM's lines are Task 1's arithmetic.
  - `quantize off applies at the boundary`: at Ring Quantize Off a Freeze on at 2.7 s starts its ramp at that boundary and the head's last advance is exactly `boundary + R − 1` (M2's rule), and AddSplice marks at once; the at-rest check covers the M2 goldens with Freeze and splices (`fx-note-freeze`, `fx-loop-overdub`, `fx-splice-capture`).
  - `quantized ring actions are block size invariant` (`[determinism]`): `fx-sync-loop`'s events in process at blocks 1, 7, 32, 64, 256 and 333: equal output.
  - `a save while a hold pends saves the ring as it is` (`persist_tests.cpp`): save between the request and `at`; the restored state holds the ring (Freeze on) and its frames equal the ring at the save.
- [ ] **Step 2:** Run `ctest --preset dev -L temposync`. Expected: FAIL.
- [ ] **Step 3:** Implement per Files. The quantizer holds fixed state per layer; nothing loops beyond the four layers.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS. Run the at-rest check. Expected: `baseline cases: 108, changed: 0, gone: 0`, voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(core): Ring Quantize, the hold, resume and splice on the next beat or bar line (R8, RB10, RB11)`.

### Task 9: gg-render's transport changes

**Files:**
- Modify: `tools/gg-render/RenderSpec.h:384-395` (`TransportSpec` gains `std::array<int, 2> timeSignature{4, 4}`, `double barStart{0.0}`, `std::vector<TransportChangeSpec> changes`; `struct TransportChangeSpec { double time; std::optional<double> beat; std::optional<double> tempoBpm; std::optional<bool> playing; std::optional<std::array<int, 2>> timeSignature; }`; the doc block lists the keys), `tools/gg-render/RenderSpec.cpp:595-615` (parsing and refusals, §4.10), `tools/gg-render/Render.cpp:935-941` (the first `setTransport` with the meter and bar start), `:1132-1180` (the call loop cut at each change's frame `llround(time × sr)`, then `setTransport` with the host state there: the given beat, or the previous beat plus `(f − f_prev) × tempo_prev / (60 × sr)` while it played, held while stopped; the given or previous tempo, play state and meter), `tools/gg-render/Render.h` (`RenderOptions::transportEveryCall`, P8)
- Test: `tests/tools/render_spec_tests.cpp`, `tests/tools/temposync_render_tests.cpp` (new, `[render][temposync]`)

**Interfaces:**
- Consumes: Task 1's `Transport` fields; Tasks 4 to 8's parameters (by name).
- Produces: the spec keys `transport.timeSignature`, `transport.barStart`, `transport.changes[]` with `time`, `beat`, `tempoBpm`, `playing`, `timeSignature`; `RenderOptions::transportEveryCall` (`bool`, default false).

- [ ] **Step 1: Write the failing tests:**
  - `render spec reads the transports meter bar start and changes`: a spec with all keys parses to the expected `TransportSpec`; an M2 spec's transport parses as before (defaults [4, 4], 0, no changes).
  - Refusals, each exit 2 with its message: a `changes` time not strictly ascending, negative, or at or past `durationSeconds`; a change with none of the four keys; an unknown key; `timeSignature` num 0 or 33, denom 3; a non-finite `beat` or `barStart`; `tempoBpm` 0.
  - `render cuts calls at transport changes`: a 2 s FX spec, A Mod Sync Step, Step to Pitch ±12, a locate to beat 1.0 at 0.7 s (frame 33,600, not a multiple of 256): audio and event lines equal at blocks 1, 256 and 333, and differ from the same spec without the change.
  - `render params take the temposync names`: `"A Mod Sync": 3`, `"Delay Sync": 1`, `"Ring Quantize": 2`, `"B Loop Sync": 2` set their slots; `"A Loop Sync"` in `automation` is accepted.
  - `transport every call is a host`: the same spec without its change, with and without `transportEveryCall`, at block 333: audio and event lines equal bit for bit.
- [ ] **Step 2:** Run `ctest --preset dev -L render`. Expected: FAIL.
- [ ] **Step 3:** Implement per Files.
- [ ] **Step 4:** Run `ctest --preset dev -L 'render|golden'`. Expected: PASS; `fx-mod-transport` passes unchanged.
- [ ] **Step 5:** Commit `feat(render): the transport's meter, bar start and timed changes (R15)`.

### Task 10: The five goldens and their determinism

**Files:**
- Create: `tests/golden/cases/{fx-sync-mod,inst-sync-step,fx-sync-delay,fx-sync-loop,fx-sync-stop}.json` (spec §4.11, seed 1, block 256, 6 to 8 s) and their expected files (from `scripts/update-goldens.sh build/dev`)
- Modify: `tests/metrics/click_injection_tests.cpp` (an entry with its reason for any case that hides steps), `tests/tools/temposync_render_tests.cpp`

**Interfaces:**
- Consumes: Task 9's spec keys and `RenderOptions::transportEveryCall`.

- [ ] **Step 1:** Write the five specs as §4.11 describes them (the times and depths within §4.11 are the builder's, §9).
- [ ] **Step 2:** Add the tests in `temposync_render_tests.cpp` (`[render][temposync][determinism]`):
  - `temposync goldens render the same at six block sizes`: each of the five specs at blocks 1, 7, 32, 64, 256 and 333: audio bits equal, and each layer's event lines equal.
  - `a host transport sent every call equals set at changes`: each of the five with `transportEveryCall` against without, at blocks 64 and 333: audio and event lines equal bit for bit (none of the five routes a Transport source).
  The goldens' timing claims are pinned in process by Task 3 (stop, run-on, play) and Task 8 (`fx-sync-loop`'s hold, resume and splice), not by the goldens.
- [ ] **Step 3:** Run `scripts/update-goldens.sh build/dev`. Expected: the five are new and the script's changed list names only them. Then run the at-rest check. Expected: `baseline cases: 108, changed: 0, gone: 0`; the 8 voice goldens unchanged.
- [ ] **Step 4:** Look at every new spectrogram (`build/dev/golden/<case>.png`). Run `ctest --preset dev -L 'golden|temposync'`. Expected: PASS, the click injection included (16/16 or a table entry with its reason).
- [ ] **Step 5:** Commit `test(golden): five tempo-sync cases; every earlier WAV hash unchanged`, staging only the five cases and their expected files by name and listing their hashes.

### Task 11: The sweep cases and the real-time checks

**Files:**
- Modify: `tests/sweep/SweepDriver.h` and `tests/sweep/SweepDriver.cpp` (`void transport(const gg::core::Transport &t)`: given to the Processor before the next call; `const gg::core::Processor &processor() const` if absent)
- Create: `tests/sweep/sweep_temposync_tests.cpp` (`[sweep][temposync]`; add to `tests/CMakeLists.txt`)
- Modify: `tests/plugin/rt_tests.cpp`, `tests/plugin/temposync_plugin_tests.cpp`

- [ ] **Step 1: Write the tests:**
  - `sweep temposync every switch on, fx` and `..., instrument`: every layer Mod Sync All and Loop Sync Bars, Delay Sync On, Ring Quantize Beat, routes from every modulator; the tempo stepped through 1, 10, 60, 120, 300, 1000 BPM, a host loop (a locate back one bar) every 100 ms, Freeze toggled every 70 ms; the harness's checks (finiteness, the peak bound, no runaway, the click score outside named exemptions) and a floor under the scored share.
  - `sweep temposync tempo ramp every call` (Review Focus 2): Delay Sync On, A Loop Sync Bars, A Live with Loop On, the tempo 120 to 121 BPM linearly over 2 s, a transport every call of 256 (the host's beat integrated in double): the harness's checks; and, through the driver's `processor()`, `syncClock().beatAt(s)` at each call start within one cell of beats of the host's.
  - `plugin process allocates nothing with tempo sync` (`[plugin][rt][temposync]`, both products): every switch on, a relocking transport every tenth call, a pending quantized hold across calls; the tripwire on `process` and `params.flush`.
  - `plugin mutate press leaves the temposync switches` (`[plugin][temposync]`): the plugin's default Mutate press leaves all ten slots.
- [ ] **Step 2:** Run `ctest --preset dev -L 'sweep|rt|temposync'`. Expected: the new cases run; fix what fails in the owning code (not the harness's limits) or name an exemption with its reason.
- [ ] **Step 3:** Run `ctest --preset dev -LE slow` and `ctest --preset asan -L 'temposync|rt|sweep' -LE slow` (configure `asan` with the ccache launchers if absent). Expected: PASS.
- [ ] **Step 4:** Commit `test(sweep): tempo sync under every switch, tempo extremes and per-call ramps; the RT tripwire`.

### Task 12: Records

**Files:**
- Modify: `INTENT.md` (spec §6: §14 a "Tempo sync" paragraph, the counts and the two `rng::Field` values; §15 the monotonic-clock bullet as built; §17 the placements and the synced display; §18 the four parameters and what a preset load keeps; §19 the transport keys and the five goldens; §20 M3's row names sub-project 7, M4's keeps the grid scheduler and gains R9's looper remainder, 3b's H13 superseded once Task 14 lands; §22's stop note; §23 the next free row with R0 to R15 provisional, and P1 to P10), `docs/superpowers/specs/2026-11-07-m3-program-design.md` (§8 row 7, §10), `docs/m3-carry-over.md` (RB20's rows (a), (b), (c); and "Scan Sync waits for 3b" while Task 14 is open), `CLAUDE.md` (the `temposync` label in Tests; the counts, 65 a layer and 300 in all on this branch; the `gg-render` names and transport keys; the five goldens, 121 in all with the voice cases; `RenderOptions::transportEveryCall` under gg-render)

- [ ] **Step 1:** Write them, each Part II change with its §23 entry in the same change (CLAUDE.md's rule).
- [ ] **Step 2:** Commit `docs(m3): tempo sync's records; INTENT §14, §15, §17 to §20, §22, §23; carry-over rows`.

### Task 13: The gate

- [ ] **Step 1:** Merge `m3-voice` if it has moved; resolve the count and list merges by addition.
- [ ] **Step 2:** Run `scripts/check.sh --presets dev,asan` (rtsan in CI; one gate on the machine at a time). Expected: `PASS`; paste the summary.
- [ ] **Step 3:** Run the at-rest check. Expected: `baseline cases: 108, changed: 0, gone: 0`; the 8 voice goldens unchanged.
- [ ] **Step 4:** Run the validators (`scripts/fetch-validators.sh tools-bin`, then clap-validator and pluginval at strictness 10 on the release products). Expected: PASS.
- [ ] **Step 5:** Push and open the draft PR (without Scan Sync if 3b hasn't merged, RB14). Fix CI's GCC 13 diagnostics if any appear.
- [ ] **Step 6:** Render the listening files (`docs/listening/m3-temposync/`: the five cases' WAVs and a README: the trill's period across the tempo change, the loop replay at beat 2, the delay's glide, the bar-exact hold and resume, the vibrato through the stop).

### Task 14: Scan Sync (after sub-project 3b merges)

**Files:**
- Modify: `core/include/gg/core/ParamIds.h` (`X(ScanSync, 0x7E, "Scan Sync")` appended to `GG_LAYER_PARAMS_M3_TEMPOSYNC`; `isReservedIndexInUse` to `0x7Eu`; 66 a layer, 304 in all, plus 3b's), `core/src/ParamIds.cpp` (Off, On; automatable, stepped), `core/include/gg/core/ModMatrix.h` (`isValidTarget` refuses it), `core/src/Mutate.cpp` (locked), `ui/src/EditLayout.cpp` (after 3b's Scan cells in `Panel::Grain`), `engine/include/gg/grain/CloudScheduler.h` (`CloudParams::scanSync`, RB13's `ScanSync` struct), `engine/include/gg/grain/Playhead.h` (`void lock(int64_t t, double x0, double r, ScanEdge edge) noexcept`), `engine/src/Engine.cpp` (the per-piece lock path for streams with `scanSync.on`: the lock key, U latched, Cloud phase-locked, voices rate-locked; else 3b's `sync` untouched), `core/src/ParamMap.cpp` (`toCloudParams` fills `scanSync` from the clock values passed in), `core/src/Processor.cpp` (`tempoFeaturesEngaged` adds Scan Sync; `updateVoices` fills each voice's `scanSync` with its own Scan Speed; the voice cache key gains Scan Sync, its sign and pass length beside 3b's slots), `core/src/SyncDivisions.cpp` and `plugin/src/GigaGrainsPlugin.cpp` (Scan Speed's synced text, `passText`, and the rescan on Scan Sync)
- Create: `tests/engine/temposync_scan_tests.cpp` (`[engine][temposync]`), `tests/golden/cases/{inst-scan-sync,inst-scan-sync-voice}.json` and their expected files
- Test: `tests/core/temposync_processor_tests.cpp`, `tests/support/ParamTable.h` and the counts, `tests/tools/temposync_render_tests.cpp`

**Interfaces:**
- Consumes: 3b's `Playhead`, `ScanEdge`, `ScanGeometry`, `scanFold`, `CloudParams::scanSpeed`/`scanEdge`, `kVoiceKeySlots`; Task 2's `passBeats`; Task 1's `anchorSample()`, `anchorBeat()`, `beatsPerSample()`, `epoch()`.
- Produces: `struct ScanSync { bool on{false}; int64_t anchorSample{0}; double anchorBeat{0.0}; double beatsPerSample{0.0}; double passBeats{0.0}; uint32_t epoch{0}; };` in `CloudScheduler.h`; `Playhead::lock` (sets `x0` reduced by 3b's canon for Wrap and Bounce, `tau0 = t`, `r`, the edge; a pending restart cleared); a source counter in the Engine bumped on a sample load or replacement, a source-mode change and a change of Loop, Loop Length, Loop Sync or Splice; RB13's Cloud lock `lock(t, g × U × β / D, g × U × beatsPerSample / D, edge)` and voice lock `lock(t, 0, g × U × beatsPerSample / D × track, edge)` (a later key change continuous: `lock(t, x(t), r', edge)`).

- [ ] **Step 1:** Merge `m3-sp3b-playhead`'s merged base (or `m3-voice` once it carries 3b); re-read 3b's names and adjust this task's to them.
- [ ] **Step 2: Write the failing tests:**
  - `scan sync cloud point is phase locked`: Sample layer, Scan Sync On, Scan Speed 0.375 (2/1), Wrap, 100 BPM: each grain's fraction equals `scanFold(P + U × β(onset) / 8, 0, 1, Wrap)` within 1e-9, across a locate to beat 0 and a tempo change to 80 BPM; Bounce and Stop likewise.
  - `scan sync ignores cloud restarts`: a held-set restart (3b H4) doesn't move the Cloud point off the song.
  - `scan sync voice is rate locked`: Mode Voice, Scan 0.5 (1/1): a voice's fraction at onset o is `scanFold(P + (o − start_v) × U × k / 4, 0, 1, Wrap)`; a tempo change bends it without a jump; Scan Track On halves the pass an octave up.
  - `scan sync off is 3bs path bit for bit` and `scan sync at zero speed holds`.
  - `scan sync on a ring uses the window`: Live layer, g = −1, U the window at the lock: the point moves toward the head.
  - The two goldens' specs (§4.11) at six block sizes (in `temposync_render_tests.cpp`); `inst-scan-sync`'s point returns to Position at the locate.
- [ ] **Step 3:** Run `ctest --preset dev -L 'temposync|playhead'`. Expected: FAIL.
- [ ] **Step 4:** Implement per Files.
- [ ] **Step 5:** Run `ctest --preset dev -LE slow`; `scripts/update-goldens.sh build/dev` (the two new cases only; then the at-rest check, `baseline cases: 108, changed: 0, gone: 0`, and 3b's and the voice goldens unchanged); look at both spectrograms; `gg-bench --cases reference,engine-sample --quick` on `release` before and after, the ratio within its noise (RB15).
- [ ] **Step 6:** Amend the records (INTENT §14, §17, §19, §20's H13 note, §23; CLAUDE.md's counts, 304 plus 3b's and the seven goldens; carry-over's Scan row closed), run `scripts/check.sh --presets dev,asan`, and commit `feat(temposync): Scan Sync, one pass per note division, phase-locked on the Cloud stream (R10, RB13)` (and the goldens and records as their own commits). If 3b merged after Task 13's PR, open this as its own pull request.
