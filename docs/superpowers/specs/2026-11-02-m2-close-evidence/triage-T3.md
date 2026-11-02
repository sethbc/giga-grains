# T3 triage: carry-over rows 357–377, 381, 382 (the extremes' minors, D-specwindow, D-corners)

Read-only triage at `m2-close` = `main` `0189dc9` (2026-11-02). No commit since the extremes merge
touched any of these rows: `git log --grep` finds none of the row numbers, and the later commits that
touch these files (`40e1d5e`, `f938804`, `cc68e3d`, sub-project 5's and 6's) leave each row's claim
true at HEAD. Line numbers below are HEAD's. Several have drifted from the rows' numbers.

Counts: **FIX 22** (357–377, 381), **CLOSE-OBSOLETE 0**, **KEEP-BLOCKED 0**, **DIRECTOR 0**,
**KEEP (record) 1** (382).

## Summary

| Row | Class | One-line reason / plan | Size | Group |
|---|---|---|---|---|
| 357 | FIX | Rename `kCutoffMin/Max/Default` to `kBusCutoff*` in `tests/support/ParamTable.h:158`–`:167` (4 uses). Test-only. | S | G1 test hygiene |
| 358 | FIX | `assert(b.size() <= filters_.size() && n <= maxBlockSize())` in `GrainFilterBank::render`; add `<cassert>`. Bench-only. | S | G2 gg-bench |
| 359 | FIX | Fold `CHECK(fl[i] == fr[i])` (`bench_tests.cpp:951`) into one `CHECK(std::equal(...))` per block. | S | G1 |
| 360 | FIX | Factor `renderBuses`/`renderOneBus`/`renderInput`'s block loop into one helper (`engine_tests.cpp:1093`, `:2660`, `:2681`). | S | G1 |
| 361 | FIX | Keep the behavior and document it: a source removal keeps `bus2Used()` raised so bus 2's filter rings out. Fix the doc in `Engine.h:405`–`:410` (and setSource's), plus a `[engine][buses]` test. | S | G5 FX units |
| 362 | FIX | `parammap_tests.cpp:1221`–`:1222`: `>= D` and `<= 400.0`. | S | G1 |
| 363 | FIX | Limiter: a ramp-of-1 test, and make a stray mode's slot Off (the identity) so it agrees with `steady()`. Test that mapping through a public constexpr. | S | G5 |
| 364 | FIX | `computeFrame`: a sample that is present but has 0 frames is silent and never falls to the ring. Test `[engine][spectral]`. | S | G3 Spectral hygiene |
| 365 | FIX | Scale DC and Nyquist (`spectrum[0]`, `[1]`) by `kOverlapGain` before the inverse, so they reconstruct at unity. **5 Spectral goldens move.** | S (+ goldens) | G4 Spectral DSP |
| 366 | FIX | One helper for the window reads' zero-fill (`SpectralSource.cpp:230`–`:266`). Output bit-identical. | S | G3 |
| 367 | FIX | Throw `std::logic_error` (not `bad_alloc`) when `pffft_new_setup` rejects N, and fix both docs. Extend the fft-size test to the range's ends. | S | G3 |
| 368 | FIX | Tests: a block over `maxBlockSize` goes in pieces, bit-identical; the steady-RMS law at 96 and 192 kHz. | S | G3 |
| 369 | FIX | Processor-level test: with Delay Amount 1 against 0 (Reverb 0), the difference is the echo, and it is no rougher than the wet across a Time move. | S | G5 |
| 370 | FIX | The code waits; spec §6.3 says "a change inside a fade turns the fade from its current value". Build the turn for A→B→A, and amend the spec so that a third type waits. No golden moves. | M | G5 |
| 371 | FIX | Processor test: a routed target past Step 16 whose base changes in a block keeps its route's offset, and a modulator control takes base + mod. | S | G5 |
| 372 | FIX (measure) | Never measured (INTENT §16 gives only `engine-spectral`, which is on). Add a report-only STFT stage to `--chain`, time it at the close's idle run, publish it, and ask the director only if the figure is large. | M | G2 |
| 373 | FIX | Move `kRingSeconds` into a light header (`gg/spectral/SpectralConstants.h`); `CloudScheduler.cpp` includes that. | S | G3 |
| 374 | FIX | Add `bench_rejects_space_chain` (exit 2) and `bench_chain_unwritable_json` / `bench_space_unwritable_json` (exit 1) to `tests/CMakeLists.txt`. | S | G2 |
| 375 | FIX | Plugin test: the material changes while the kind is Sample, then the kind becomes Spectral, and Position's units follow. | S | G6 plugin Spectral tests |
| 376 | FIX | Plugin test: an FX Spectral layer shows seconds over its ring and % over a loaded sample. | S | G6 |
| 377 | FIX | Replace `peak > 1e-4f` (`process_tests.cpp:1737`) with an RMS over the settled blocks, pinned with margin, and silence before the first window. | S | G6 |
| 381 | FIX | Build D-specwindow: the ring branch starts each frame inside the window (`anchorAt`/`windowOldestAt`), with reads bounded by storage. `applyCommands` admits a Spectral layer that reads its ring. No golden moves (defaults are bit-identical); the sweep's Spectral corners move. | L | G4 (before 365) |
| 382 | KEEP (record) | The director's D-corners option 1 says "row 382 keeps the finding on record" (spec §8, INTENT §23 #69). The row stays until the boundaries are scored. | – | – |

**Groups (one task each):**
- **G1, test hygiene:** 357, 359, 360, 362.
- **G2, gg-bench:** 358, 374, 372 (372's figure lands at the idle run).
- **G3, Spectral hygiene, hashes unchanged:** 364, 366, 367, 368, 373.
- **G4, Spectral DSP:** 381 first (hashes unchanged, which proves the defaults' identity), then 365 (5 goldens move, regenerated once).
- **G5, FX units' edges:** 361, 363, 369, 370, 371.
- **G6, plugin Spectral tests:** 375, 376, 377.

---

## 357: the bus-cutoff constants' names
**Evidence:** `tests/support/ParamTable.h:158`
`inline constexpr float kCutoffMin = 4.321928094887363f, kCutoffMax = 14.287712379549449f;` and
`:159` `kCutoffDefault`, used only at `:164` and `:167` (`git grep` finds no other use).

**Plan:**
- Rename them to `kBusCutoffMin`, `kBusCutoffMax` and `kBusCutoffDefault`.
- Proof: the `parammap`/`plugin` tests that read `ParamTable.h` build and pass unchanged.
- No audio thread; no golden can move, because only a test header changes.
- S. G1.

## 358: the grain-filter kernel asserts nothing
**Evidence:** `tools/gg-bench/GrainFilterKernel.h:61`–`:86`. `render` indexes `filters_[g]` for
`g < b.size()` and fills `scratchL_` for `n` samples. Neither bound is checked, though the doc
(`:59`–`:60`, `:104`–`:106`) states both.

**Plan:**
- Add `#include <cassert>` and, at the top of `render`,
  `assert(b.size() <= filters_.size()); assert(n <= scratchL_.size());`.
- Proof: the existing `[bench]` cases ("hermite-simd-grainfilter low-passes each grain at 1 kHz",
  "…runs and costs more…") run with the asserts live in `dev` and `asan`.
- Bench-only; no golden moves; the frozen `Kernels.h` is untouched.
- S. G2.

## 359: a per-sample CHECK
**Evidence:** `tests/tools/bench_tests.cpp:951`
`CHECK(fl[i] == fr[i]); // the same source on both channels`, inside the sample loop. That is
5,120 assertions per call.

**Plan:**
- Replace it with `CHECK(std::equal(fl.begin(), fl.end(), fr.begin()));` once per block, before
  the accumulation loop.
- Test-only. S. G1.

## 360: three copies of one block loop
**Evidence:** `tests/engine/engine_tests.cpp:1093` (`renderInput`), `:2660` (`renderBuses`) and
`:2681` (`renderOneBus`) each repeat the same `for (done < frames) { n = min(...); …;
engine.process(...); }` loop.

**Plan:**
- Add one `forEachBlock(frames, sizes, fn(done, n))` helper. The three call it with their own
  `process` form, and `renderInput`'s fixed size becomes a one-entry list.
- Proof: the existing `[engine][buses]` and `[engine]` cases pass unchanged.
- Test-only. S. G1.

## 361: `setSource(nullptr)` keeps `bus2Used_`
**Evidence:**
- `engine/src/Engine.cpp:111`–`:120`: the `source == nullptr` branch calls `clearGrains()` and
  leaves `bus2Used_` alone.
- `bus2Used_` is lowered only at `:383` (`reset()`) and `:436` (the enable gate).
- The row says this is "documented", but HEAD's doc says otherwise. `Engine.h:405`–`:409` reads
  "until reset() or the enable gate turning off, where the pool is cleared". That implies every
  pool clear lowers it, and the setSource doc (`Engine.h:207`–`:210`) is silent on the flag.
- The flag matters in `Processor.cpp:1974`: while it is raised, bus 2's filter output is summed;
  once lowered, it is fed silence and discarded.
- The path is reachable: `adoptSource(nullptr)` runs when a session without a sample clears one,
  as in `sample_tests.cpp:1689`.

**Plan (no behavior change):**
- Keep the flag raised. A source removal is already a hard cut, and lowering the flag would also
  cut filter 1's ringing tail, a second discontinuity.
- Amend `bus2Used()`'s doc: "a source removal (setSource(nullptr)) clears the pool but keeps it
  raised, so bus 2's filter rings out". Add the same clause to setSource's doc.
- New test "a source removal keeps bus 2's flag raised" `[engine][buses]`: after bus-2 grains,
  `setSource(nullptr)` leaves `bus2Used()` true, and `reset()` lowers it.
- No audio-path code changes, so no golden can move. S. G5.

## 362: Smear's law test, a loose bound
**Evidence:** `tests/core/parammap_tests.cpp:1221`
`REQUIRE(p.densityHz >= std::min(D, 400.0));` and `:1222` `<= std::max(D, 400.0)`. D is at most
`exp2Det(d(kLog2Of400Hz))` (399.99996 Hz, per the row).

**Plan:**
- Change them to `REQUIRE(p.densityHz >= D); REQUIRE(p.densityHz <= 400.0);`.
- Test-only. S. G1.

## 363: limiter edges
**Evidence:** `core/include/gg/core/Limiter.h`.
- A stray mode is asserted out (`:81`). Its release-build fallbacks also disagree:
  - `index()` (`:133`–`:136`) clamps a stray mode to **Peak's** slot, so `settle()` (`:188`–`:196`)
    puts weight 1 on Peak and a fade toward a stray mode ramps Peak's law in.
  - `steady()` (`:157`–`:171`) treats a stray mode as the **identity**, so a fade's last call jumps
    from Peak's law to the identity.
- A ramp of 1 (`beginFade`, then `++fadePos_ >= ramp_` on the same call, `:91`) applies the change
  at once. No test pins it: `tests/core/limiter_tests.cpp` uses `kRamp` and 0 only (`:99`–`:355`).

**Plan:**
- Make `index()` map a stray mode to Off's slot (`m <= Peak ? m : Off`), so the fade and
  `steady()` agree on the identity.
- Expose that mapping as a public `static constexpr size_t lawSlot(LimiterMode)` and test it.
  A stray `process()` can't run under the assert in `dev`/`asan`.
- Add "a fade of one call is the new law at once" `[core][fx]`: `prepare(kSr, 1)` matches
  `prepare(kSr, 0)` bit for bit from the change.
- This touches the audio thread (`Limiter`).
- No golden can move: the Processor's mode is `roundedIn` 0–2, so no stray mode reaches it, and
  its ramp is 480.
- S. G5.

## 364: an empty sample falls through to the ring
**Evidence:**
- `engine/src/SpectralSource.cpp:189`: `if (m.sample[0] != nullptr && m.sampleFrames > 0) {…
  return; }`, then the ring branch at `:203`.
- The Engine (`Engine.cpp:526`–`:540`) sets `material.ring` only when no source is current, so the
  case is unreachable through it.

**Plan:**
- Split the condition: `if (m.sample[0] != nullptr) { if (m.sampleFrames <= 0) return; … }`.
  A sample, empty or not, is the material, as the Engine decides (D1).
- New test "an empty sample is silent, never the ring" `[engine][spectral]`: material with
  `sample[0]` set, `sampleFrames` 0 and a filled ring writes silence.
- This touches the audio thread.
- No golden can move: the Engine never passes both a sample and a ring.
- S. G3.

## 365: DC and Nyquist reconstruct +2.5 dB
**Evidence:** `SpectralSource.cpp:276`–`:289`. Bins 1 … N/2−1 get random phases; DC (`spectrum[0]`)
and Nyquist (`spectrum[1]`) keep their values. The synthesis divides everything by
`kOverlapGain` = 0.75 (`SpectralSource.h:78`–`:83`: frames "add in power").

The +2.5 dB, derived:
- A constant input c has bin 0 = c·N/2. Inverted, that is c/2 per frame.
- The frame is windowed by w_s/0.75, and Σ w_s = 2 at hop N/4, so the output is c/0.75 = 1.333c,
  which is +2.5 dB, as the row says. Nyquist behaves the same.

Is it recorded as decided? INTENT §23 #69's ruling records only the 0.75 "because frames with
redrawn phases add in power", which these two bins don't do. So neither the spec nor INTENT
decides it, and CLOSE-OBSOLETE doesn't apply. The change only corrects the ruling's own intent of
unity gain, so it isn't a sound choice for the director either.

**Plan:**
- In `smearInto`, before the inverse, `spectrum[0] *= kOverlapGain; spectrum[1] *= kOverlapGain;`.
  The coherent bins then sum to exactly unity, and the others to unity in power.
- New test "a constant material smears to its own mean" `[engine][spectral]`: sample material of
  0.25, a settled stretch whose mean is 0.25 within 1e-3 relative (today 0.333). Optionally also
  a Nyquist material held within 0.1 dB.
- This touches the audio thread.
- **Goldens that move:** `inst-spectral`, `fx-spectral-freeze`, `golden_preset_fx-06-spectral-mist`,
  `golden_preset_fx-20-melting-clock` and `golden_preset_inst-21-smoke-cathedral`. These are every
  case with `"[A-D] Source": 3` (`git grep`); no golden automates Source to 3. Their event logs are
  unchanged, since the grains are the same.
- The audible effect is a DC offset only: content under about half a bin, roughly 6 Hz, maps to
  bin 0. Nyquist is inaudible. Expect the metrics to barely move. Look at all five spectrograms.
- The sweep's Spectral corners' audio moves too (scores, not hashes).
- Docs: add one line to INTENT Part II's STFT text ("DC and Nyquist, which keep their phase, are
  scaled by 0.75 so they too reconstruct at unity") and a §23 decision-log entry.
- S, plus the golden regeneration. G4: build it after 381, so 381's run proves the hashes
  unchanged first.

## 366: a duplicated clear loop
**Evidence:** `SpectralSource.cpp:230`–`:244` (`readSample`: zero-fill `[0, lo)`, the windowed read,
zero-fill `[hi, n)`) and `:246`–`:266` (`readRing`: the same zero-fill `[0, lo)`, then a wrapping
read).

**Plan:**
- One private helper zero-fills outside `[lo, hi)`. Both readers call it around their own loops.
- Proof: the existing `[engine][spectral]` block-split `memcmp` tests, plus `update-goldens.sh`
  before and after with every hash unchanged.
- No golden can move: the arithmetic is the same, bit for bit. S. G3.

## 367: `bad_alloc` for an unsupported N
**Evidence:**
- `SpectralSource.cpp:72`–`:76` throws `std::bad_alloc` when `pffft_new_setup` returns null.
- pffft returns null only for a rejected N: N < 0, N > 2^26, or not a multiple of 32 for REAL
  (`libs/pffft/src/pffft_priv_impl.h:1304`–`:1316`). Its `malloc` at `:1317` is unchecked.
- N comes from `fftSizeFor`: a power of two in [128, 2^24] (`:24`–`:25`, `:45`–`:61`), so the
  throw is unreachable.

**Plan:**
- Throw `std::logic_error("SpectralSource: pffft rejects the FFT size")` there, and keep
  `bad_alloc` for `pffft_aligned_malloc` (`:79`–`:82`).
- Update `SpectralSource.h:89`–`:90` and `Engine.h:200`.
- Extend "the fft size follows the rate" (`spectral_tests.cpp:126`) with `fftSizeFor` at 1 Hz,
  1e9 Hz and NaN: a power of two in [128, 2^24], a multiple of 128.
- No audio thread (constructor only); no golden can move. S. G3.

## 368: Spectral coverage gaps
**Evidence:** `SpectralSource.cpp:149`–`:181` loops in pieces of at most `maxBlockSize_`.
`spectral_tests.cpp` always constructs with `kMaxBlock` = 4096 and never passes more, and its RMS
test (`:233`) runs at 48 kHz only. `:126`–`:139` checks only N and the hop at other rates.

**Plan:**
- New test "a block over maxBlockSize goes in pieces, the same bytes" `[engine][spectral]`:
  `SpectralSource(48000, 64)`, one `process(…, 4096)` against blocks of 64, by `memcmp` of
  `ringFrames`, for both materials.
- Run "a steady material keeps its rms within 1 dB" at 44.1, 96 and 192 kHz too, as a GENERATE
  over rates.
- Test-only. S. G3.

## 369: the delay's fractional read at the Processor level
**Evidence:** `tests/core/fx_processor_tests.cpp:204` tests `gg::core::Delay` directly. Golden
`fx-delay-automate` (Time 0.1 s to 0.5 s at 2 s; click score 3.70,
`tests/golden/expected/fx-delay-automate.metrics.json:5`) covers the path by metrics only. That
golden is the same sub-project's, not later work, so the row isn't obsolete.

**Plan:**
- New `[core][fx]` case "the delay's echo through the processor moves without a step". Render the
  FX twice at one seed, Delay Amount 1 and 0, with Reverb Amount 0 and Feedback 0. The difference
  is the delay's return alone.
- Step Delay Time mid-render. The echo's largest per-sample step after the move must be at most
  1.6 × the wet's own largest step (the glide's 0.5 × slope plus margin).
- Test-only; no golden moves. S. G5.

## 370: a type change during a fade waits
**Evidence:**
- `core/src/fx/BusFilter.cpp:80`: `if (!fading_ && type != type_)`. A request during a fade is
  ignored until the fade ends (`:123`–`:129`). The class comment (`BusFilter.h:24`–`:25`) says so.
- But the spec (`docs/superpowers/specs/2026-10-30-m2-extremes-design.md` §6.3, line 377) says:
  "A change inside a fade turns the fade from its current value, as the ramps do."
- So the code diverges from the spec. The wait isn't a decision recorded in INTENT or the spec,
  and CLOSE-OBSOLETE doesn't apply.

**Plan:**
- In `setCell`, when `fading_ && type == type_` (the row's A→B→A), turn the fade. Swap `active_`
  and the two types, and set `fadePos_ = rampSamples_ - fadePos_`.
  - Both instances are already running, so the output stays continuous.
  - A's instance was never reset, so once the turn completes the output is bit-identical to a
    render with no change.
- A change to a third type during a fade still waits. Two instances can't turn toward a third.
- Amend spec §6.3's sentence to say exactly that, with a §23 note.
- New test "a type change back during a fade turns it" `[core][buses]`. Change Low-pass to
  High-pass at cell 0 and back at cell 5 (160 samples into the 480-sample fade). Pure Low-pass
  returns 160 samples later, bit-identical to a no-change render from there, with no step larger
  than the steady sound's.
- This touches the audio thread.
- No golden can move:
  - `fx-bus-filters` changes one type once, at 4.0 s;
  - `inst-filter-lfo` and the presets set types statically, snapped by `jump`;
  - no golden changes a type twice within 10 ms.
  The sweep's automation bursts move (scores only).
- M. G5.

## 371: the modulator-control bound, pinned at the predicate alone
**Evidence:**
- `core/src/ParamMap.cpp:341`–`:345` (`isModulatorControlSlot`: `Lfo1Rate…Step16`) and
  `core/src/Processor.cpp:663`–`:668` (`commitPending` writes `effective_ = base + mod` for those
  slots alone).
- `parammap_tests.cpp:1736`–`:1763` checks only the predicate. Nothing checks the Processor.

**Plan:**
- New `[core][mod]` Processor case. Route LFO 1 to layer A's Bus 1 Cutoff, a routable slot past
  Step 16, and change that slot's base in the same block.
- After `commitPending` and the next boundary, `effective(Bus1Cutoff)` includes the route's offset.
  Changing `Lfo1Rate`'s base in a block makes its effective value base + mod at once.
- Test-only; no golden moves. S. G5.

## 372: the STFT runs while a Spectral layer is off
**Evidence:**
- `Processor.cpp:1924`–`:1931`: a layer that isn't rendering still calls `l.engine.process(…,
  nullptr, 0, n, …)`.
- `Engine.cpp:524`–`:542` then runs `spectral_->setActive(mode_ == Spectral)` and
  `spectral_->process` whatever the enable gate.
- INTENT §23 #69 records it as a build ruling, "CPU only; deferred". The row asks to "measure its
  cost at the idle-machine run".
- INTENT §16 (INTENT.md:1749–1758) publishes only `engine-spectral`, with the STFT running under
  2,000 grains. It has no figure for the STFT alone, so the measurement was never made.
- My estimate, not a measurement: about 2 forward and 2 inverse 4096-point FFTs plus about 4,094
  RNG/sineDet bins per 1024-frame hop. That is about 50–100 ns a sample per layer, roughly
  0.3–0.5% of a core, and four idle Spectral layers would be 1–2%.

**Plan:**
- Add a report-only `spectral` stage to `gg-bench --chain` (`tools/gg-bench/Bench.h`): one
  `SpectralSource::process` over noise ring material, active, in ns per stereo sample and % of one
  core at 48 kHz. Add it to the `--chain` JSON, and `bench_chain_cli` covers that it runs.
- Time it at the close's idle-machine run and publish it in INTENT §16 beside `--chain`'s
  0.057%. That figure closes the row.
- If the figure is above about 0.25% of a core per layer, bring the director one forced choice:
  - option 1 (cost: a re-enabled Spectral layer's smear fades in over about 85 ms after its own
    ramp): gate the STFT on the layer's enable, as a kind switch does: drain, then silence.
  - option 2 (cost: the CPU above): keep ruling 3.
  No golden toggles a Spectral layer's Layer On, so option 1 would move no hash.
- The measurement touches no audio code; no golden moves. M, because it needs the idle run.
  G2.

## 373: CloudScheduler includes SpectralSource.h for one constant
**Evidence:** `engine/src/CloudScheduler.cpp:7` `#include "gg/spectral/SpectralSource.h"`, used once
at `:59` (`maxSpray = SpectralSource::kRingSeconds;`). `SpectralSource.h` pulls in `LiveSource.h`
and pffft's declarations.

**Plan:**
- Move `kRingSeconds` (and `kWindowSeconds`) into a new `engine/include/gg/spectral/SpectralConstants.h`
  (`namespace gg::spectral`).
- `SpectralSource` keeps `static constexpr double kRingSeconds = spectral::kRingSeconds;` so no
  caller changes. `CloudScheduler.cpp` includes only the new header.
- No golden can move: the value is unchanged. S. G3.

## 374: `--chain`'s exit 1 and `--space --chain` untested
**Evidence:**
- `tests/CMakeLists.txt:253`–`:288` covers only `bench_space_cli`, `bench_chain_cli` and the
  `bench_rejects_*` cases. None writes an unwritable JSON, and none passes `--space --chain`.
- `tools/gg-bench/main.cpp:252`–`:254` rejects `--chain` beside `--space` with "--chain doesn't
  apply with --space", exit 2 (`:572`–`:581`).

**Plan:**
- Add `bench_rejects_space_chain` to the foreach list (args `--space --chain`, error
  "--chain doesn't apply with --space").
- Add `bench_chain_unwritable_json` and `bench_space_unwritable_json` through
  `tools/check-exit-code.cmake` with `EXPECTED_CODE=1`: args `--chain --seconds 0.001 --rounds 1
  --json <build>/no-such-dir/x.json`, and the same with `--space`. The stderr names the path.
- Test-only. S. G2.

## 375: a material change before the kind becomes Spectral
**Evidence:** `tests/plugin/sample_tests.cpp:1638`–`:1694` sets Spectral first, then loads the sample.
`params_tests.cpp:1439` ("a kind change asks for a text rescan") covers Live and Sample only. No
case changes the material while the kind is Sample and then switches to Spectral.

**Plan:**
- New `[plugin]` case "the material set before a spectral kind decides its units". In the
  instrument, load the sample with layer A at Sample, process, then set A Source 3: Position shows
  `%`, with one rescan for the kind.
- Clear the sample while the kind is Sample (load the empty state), then set Spectral: Position
  shows seconds.
- Test-only. S. G6.

## 376: no FX Spectral Position-units test
**Evidence:** `tests/plugin/process_tests.cpp:1681` only renders. `sample_tests.cpp:1638` is
instrument-only (`kInstrumentId`, `:1647`). `GigaGrainsPlugin.cpp:625`–`:632` decides the units
from `shownMaterials_` in both products.

**Plan:**
- Make the `:1638` case a GENERATE over both products, or add an FX twin. FX A Source 3 shows
  seconds over its ring; a sample loaded into FX layer A shows `%`, with one rescan per change.
- Test-only. S. G6.

## 377: a weak "it sounded" bound
**Evidence:** `tests/plugin/process_tests.cpp:1737` `CHECK(peak > 1e-4f);`, after 192 blocks of noise
into Spectral at Smear 1.

**Plan:**
- Check the RMS over blocks 64–191, after the STFT and grains settle, above a level pinned from a
  measured run with about 12 dB of margin (expected around −40 dBFS or louder). Also check the
  first block before one window (N + 256 frames) is silent in the FX.
- Test-only. S. G6.

## 381: Spectral over its ring ignores the window, and the ring commands skip it
**What was decided:**
- The extremes spec §8 (line 725): "D-specwindow… Decided: option 1 (2026-11-01). Defer, carry-over
  row 381: a Spectral smear reads outside the Loop/Splice window until it is built, and the ring
  commands skip it."
- INTENT §23 #69 repeats it. So the behavior is decided (follow the window); only the build was
  deferred. That makes it FIX, not DIRECTOR.

**Evidence at HEAD:**
- `SpectralSource.cpp:207`–`:227`: the ring branch uses `head = ring.headAt(hopTime)` and
  `oldestStart = head − ring.readFrames()` alone. It never calls `anchorAt` or `windowOldestAt`.
- `core/src/Processor.cpp:1257`: `if (l.kind != SourceMode::Live || !live.enabled()) continue;`
  in `applyCommands`.
- The window itself is already set for every kind (`Processor.cpp:859`–`:862`: `setLoop`,
  `selectSplice` on `l.engine.live()`).
- `MediaSession::captureRing` (`plugin/src/MediaSession.cpp:1138`) reads the window whatever the
  kind, so only the splice commands and the analysis are missing.

**Plan, the engine part** (`SpectralSource::computeFrame`, ring branch): follow the Live grains'
law (LiveSource.h:52–70): the window steers where a read starts, and reads are bounded by the
storage (SP-T12aa).

```
head   = ring.headAt(hopTime);
E      = ring.anchorAt(hopTime);                       // the window's end (head at the defaults)
newest = std::min(E, head - kNewestMargin) - n;        // a frame never within the head's margin
oldestStart = std::max(ring.windowOldestAt(hopTime), head - ring.readFrames());
start  = clamp(newest - behind, oldestStart, newest);  // the newest bound wins, as now
oldest = std::max<int64_t>(0, head - ring.readFrames());  // reads: the storage bound, unchanged
```

- At Loop off with no splice, `E = R(t)` and `windowOldestAt = R(t) − N` (LiveSource.h:58–62).
  This is today's code bit for bit, except during a Ring Length shrink, where the admission limit
  N′ now narrows the frame's start exactly as it narrows a Live grain's (SP-T12ad).
- A window shorter than N + 256 frames (a loop under about 90 ms, or a short splice) leaves the
  frame starting at `newest`. It reads up to N frames back into storage before the window's
  start, as a Live grain admitted at the window's oldest frame reads past it. The builder records
  this as a ruling in the spec's amendment. If the director wants cyclic loop reads instead, that
  is a later option, not a blocker.
- `computeFrame` runs at a hop inside the block the Engine has just written
  (`Engine.cpp:515`–`:542`), so `anchorAt` and `windowOldestAt` are valid there. Both are functions
  of R(t), so the analysis stays block-size invariant.

**Plan, the Processor part** (`applyCommands`):
- Admit a layer whose ring is read. Factor `ringSaved`'s predicate (`ParamMap.cpp:263`–`:265`) into
  `gg::core::readsRing(SourceMode kind, SourceMode material)`. Use it in `ringSaved` and in
  `applyCommands`, with `material = l.engine.hasSampleSource() ? Sample : Live`, the same rule as
  `Processor.cpp:878`.
- Update the comment at `:1256` and Processor.h's spec §6.4 note.

**Tests:**
- `[engine][spectral]`:
  - "ring material follows the loop window": old frames hold a 1 kHz sine and the newest L =
    0.5 s a 200 Hz sine, with Loop on and Position 3 s. Goertzel energy is at 200 Hz, 1 kHz is
    more than 40 dB down. Today it is the reverse.
  - "ring material follows a selected splice".
  - "a loop shorter than a frame reads the newest frames": the edge law.
  - the existing block-split `memcmp` with Loop on and a splice selected.
- `[core][source]`:
  - "ring commands reach a Spectral layer that smears its ring", plus a negative case for one
    with a sample;
  - a splice-k capture from a Spectral layer.
- `[determinism]`/`[threads]`: extend `threads_tests.cpp:183` ("four Spectral layers over their
  rings…") and the block-size invariance case with Loop on and a splice.
- `[rt]`: the spectral tripwire case (`spectral_tests.cpp:501`) with Loop on.

**Audio thread:** yes (SpectralSource, Processor). Run RTSan.

**Goldens:** none can move.
- No golden or preset golden sets Loop, Loop Length or Splice, or issues splice operations, on a
  Spectral layer.
- The Spectral cases are `inst-spectral`, `fx-spectral-freeze` and the presets `fx-06`, `fx-20`
  and `inst-21` (`git grep '"[A-D] Source": *3'`). Their ring parameters are at their defaults:
  preset sources may not set a ring parameter, and `fx-splice-capture`'s layers are Live and
  Sample.
- So `update-goldens.sh` before and after shows all 108 hashes unchanged, which proves the
  defaults' identity.

**What moves:** `tests/sweep/sweep_extremes_tests.cpp`'s "extremes: Resample and Spectral under the
delay and the limiter" (fx `:219`, instrument `:226`). Their Spectral corners read looping rings,
so the audio changes. Recheck their click scores and scored-share floors with no exemption widened.

**Docs:**
- Amend the extremes spec §6.2 (D-specwindow built) and INTENT Part II's Spectral material text,
  with a §23 entry (amend #69 or a new row).
- Close row 381 with the commits.

**Size and group:** L. G4, built before 365.

## 382: the corners' boundaries, a record row
**Evidence:**
- The extremes spec §8 (line 732): "D-corners… Decided: option 1 (2026-11-01). Leave the boundaries
  to the Reverb Size exemption… Cost: nothing now; row 382 keeps the finding on record."
- INTENT §23 #69: "carry-over row 382 keeps the finding on record."
- The row's own When: "Open as a record row: the finding stays until the boundaries are scored."

**Class: KEEP (record).** The director's decision names this row as the place the finding lives,
so closing it would discard what D-corners chose to keep. Nothing upstream or scheduled blocks it.

**What closes it:** scoring the extremes corners' boundaries. Either:
- row 217's DSP route (a sweepable Reverb Size, two crossfaded reverb states, which retires the
  Size exemption); or
- a later decision to hold Reverb Size in the corners and rule on the ramps' corners, fast
  envelopes and lone short grains (D-corners' option 2).

M2's close builds neither.
