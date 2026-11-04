# T2 triage: rows 340 to 356 and 383 to 388

Worktree `m2-close` at `0189dc9` (= `main`), read-only. Line numbers are at HEAD.

**Counts:** FIX 20 (one of them, row 388, a bundle of 19 items triaged one by one below: 18 FIX,
1 out of scope), KEEP-BLOCKED 2 (352, 356), DIRECTOR 1 (387), CLOSE-OBSOLETE 0. Two rows are
partly done already: row 351's hexfloat dump (`fbeec1c`) and row 349's preset-golden group
(`d526169`). Each keeps its remainder as a FIX. Row 345's `contentPath` hardening is done too
(`2a9746c`).

## Summary

| Row | Class | One-line reason / plan | Size | Group |
|---|---|---|---|---|
| 340 | FIX | Clamp `maxBytes` to `kDefaultDecodeBound` once in `decodeInput`. Today a `UINT64_MAX` bound lets a saturated `planarBytes` pass `checkLength` (`limit + 1` itself can't overflow) | S | G1 media |
| 341 | FIX | Remaining: `MemoryBudget.cpp:80`'s `free` shadows `::free` (rename it `available`). The include lists and the `MachineSettings.h` comment are fine at HEAD | S | G1 media |
| 342 | FIX | Tighten `encodeState`'s two asserts so a `*AudioMissing` flag excuses only an embedded entry whose payload is absent. No caller reuses a decoded state today: the bug is latent | S | G1 media |
| 343 | FIX | Drop `liveRun`'s unused `r0` parameter and its two callers (`GrainKernels.h:1227`, `:1287`, `:1406`, `:1414`) | S | G4 kernel |
| 344 | FIX | Add `LiveSource::windowAt(head)` → {end, oldest} with one `spliceAt`, used by `liveRun`'s `inside` and `fade`. Bit-identical refactor | M | G4 kernel |
| 345 | FIX | Remaining: `collectOne` returns {path, UTF-8 text}; add a test that collects into an unwritable folder. The `contentPath` hardening was done in `2a9746c` | S | G1 media |
| 346 | FIX | Still written twice (`Bench.cpp:517`, `:1334`): one `warmUpSecondsFor(setup, quick)` helper | S | G5 tools |
| 347 | FIX | `Render.cpp:1060-1091`: check the cap with the capture's cost before `ringBuffer`/`prepare`, as the plugin does | S | G5 tools |
| 348 | FIX | Move `spanOf`/`readWith`/`readRing` into `plugin/src/RingReader.{h,cpp}`. Structure only | S | G2 reader |
| 349 | FIX | Partly done: the presets group passes `--content-dir` (`d526169`). The 60 main cases still get none in `update-goldens.sh:80`, while ctest passes `--content-dir ${GG_GOLDEN_MEDIA_DIR}` (`tests/CMakeLists.txt:593`). Align the script | S | G5 tools |
| 350 | FIX | `sampleBytes` leaves out the guard frames that `PreparedSource::bytes()` counts. Add them, and pin `sampleBytes == source.bytes()` | S | G1 media |
| 351 | FIX | Hexfloat part done (`fbeec1c`). Remaining: a test hook in `Processor::adoptRing` between `publishStorage` and `retireRing`, which makes the use-after-free regression test deterministic | M | G2 reader |
| 352 | KEEP-BLOCKED | The director's ears: a Ring Length drag can only be heard in a host (gg-render can't resize mid-render). Goes in the gate checklist; fix only if heard | — | Gate |
| 353 | FIX | Confirmed at `MediaSession.cpp:242-247`. Apply the row's fix. A deterministic test is possible through the existing `ringReadHook` | S | G2 reader |
| 354 | FIX | Still present (`MediaSession.cpp:566-579`, `:976-984`). Drop the frames once bytes exist; activation restores from the bytes | M | G3 session |
| 355 | FIX | Still present (`MediaSession.cpp:700-775`). A newer load withdraws every layer's waiting restore offer where it offers none | M | G3 session |
| 356 | KEEP-BLOCKED | Rosetta translation, outside our control. Output is held bit for bit by the memcmp. No WARN in the 5 recent CI runs I grepped. Unblocked by a WARN in a CI log, or an Intel Mac | — | — |
| 383 | FIX | Not blocked: `formatOf(const clap_host*)` can be fed a fake `clap_host` with a stub `get_extension`. Expose it, test every flavor | S | G6 views |
| 384 | FIX | Make `MediaSession::capturesFolder` public (`MediaSession.h:282` is under `private:` at `:228`). Delete the editor's copy (`EditorPlugin.cpp:259-281`) | S | G3 session |
| 385 | FIX | Size each layer's `LivePeaks` for the longest ring, and clamp the lane's oldest frame to the ring's frames (`GrainCanvas.cpp:735`) | M | G6 views |
| 386 | FIX | Per the spec, the engine is right for the **Splice parameter** (SP-T1a/b: from the oldest mark still in storage, Loop ignored), and `spanOf` is right for **capture's own argument** (SP-T8a). The lane mixes them: outline, numbers and Capture follow `spanOf` (`GrainCanvas.cpp:818-822`, `LaneMenu.cpp:64`), while its Add-splice anchor follows the engine (`LaneModel.cpp:457`). Make the lane follow the engine | M | G2 reader |
| 387 | DIRECTOR | A text approval for the gate checklist, not a listening item. All 8 strings are present at HEAD | — | Gate |
| 388 | FIX | 18 items FIX, one out of scope (row 330's). See the per-item table | M overall | G6 views (+G3) |

**Groups (one task each):**
- G1 media: 340, 341, 342, 345, 350.
- G2 ring reader: 348, then 353, 386 and 351's hook. Also add the race test's diagnostic (last section).
- G3 session: 354, 355, 384, and 388's `collectSamples`/`capturesFolder` tests.
- G4 kernel: 343, 344. Run `update-goldens.sh` before and after; every hash must be unchanged.
- G5 tools: 346, 347, 349.
- G6 views: 383, 385, the rest of 388.
- Gate checklist: 352, 387.

---

## Row 340: `checkLength` at a `UINT64_MAX` bound (FIX, S, G1)

**Evidence.**
- `media/src/Decode.cpp:138-141`: `need = planarBytes(frames, kept)` saturates at `UINT64_MAX` (`:61-71`), and the check is `need > stream.maxBytes`. With `maxBytes == UINT64_MAX`, a header whose frames overflow passes, and the decode then tries to allocate.
- The row's second claim doesn't hold. `decodeCompressed`'s `limit = maxBytes / (kept * 4)` (`:224`) is at most 2^62, so `limit + 1` (`:271`) can't overflow.
- The public entries pass the bound through unchanged: `decodeAudioFile` (`:774-806`), `decodeAudioBytes` (`:808-816`), then `decodeInput` (`:754`).

**Plan.**
- In `decodeInput`, clamp `maxBytes = std::min(maxBytes, kDefaultDecodeBound)` before the tries.
- Update the comment in `Decode.h:23-26`, which says the bound is at most this.
- Test, `[media]` in `tests/media/decode_tests.cpp`: "a bound above the decoder's ceiling is the ceiling". It writes a WAV at run time (`AudioFixtures.h`) whose header claims more frames than 16 GiB holds, decodes it with `UINT64_MAX`, and requires the over-bound refusal. RED today: it allocates or throws.
- Off the audio thread. No golden can move: every caller already passes 16 GiB or less.

## Row 341: media hygiene (FIX, S, G1)

**Evidence.**
- The includes are fine at HEAD. `Sample.h` includes `<atomic> <cstdint> <filesystem> <memory> <optional> <string>`, `SamplePool.h`, `Sha256.h`, `SampleBuffer.h` and `PreparedSource.h` for what it names. `std::bad_alloc` and `std::invalid_argument` appear only in comments. `SamplePool.h` includes `<cstddef> <cstdint> <memory> <mutex> <string> <vector>` and `PreparedSource.h`.
- The `MachineSettings.h` comment, `:11-29`, was rewritten in merge `3cf1c66` and is wrapped at 100 columns.
- Still present: `media/src/MemoryBudget.cpp:80`, `const uint64_t free = …` in `refusal()`.

**Plan.**
- Rename the local `free` to `available`.
- Recheck the two headers' includes with the compiler, e.g. a one-line TU per header.
- No behavior change. The existing `[media]` budget tests cover it. No golden.

## Row 342: the `*AudioMissing` flag outlives its entry (FIX, S, G1)

**Evidence.**
- `core/include/gg/core/SessionState.h:162-163` declares the flags. `core/src/SessionState.cpp:682` and `:690` assert `writable || state.<x>AudioMissing[k]`. So a set flag excuses *any* unwritable entry in that slot, not only the one with the missing payload.
- No caller replaces an entry in a decoded state today. The writers at `MediaSession.cpp:588/605/641`, `GigaGrainsPlugin.cpp:2003` and `Render.cpp:604/622` each build a fresh `SessionState`. The bug is latent.

**Plan.**
- Tighten each assert to `writable || (flag && entry valid && layer == k && embed && !hasId(ids, audioId))`, so the flag excuses exactly the case it records.
- Optionally, reset the flag in a `setRing`/`setSample` helper.
- Test, `[core]`: "a decoded state whose missing-payload ring is replaced saves the new ring". Decode with a missing `AUDI` payload, replace `rings[k]` with a valid file entry, encode, decode back, and require the new entry. The assert runs under `dev` and `asan`.
- No audio thread, no golden.

## Row 343: `r0` unused in `liveRun` (FIX, S, G4)

**Evidence.**
- `engine/include/gg/grain/GrainKernels.h:1227-1228` takes `int64_t r0`.
- `:1287` is `static_cast<void>(r0);`.
- The callers at `:1406` and `:1414` pass `live.headAt(first)` and `live.headAt(kink)`.

**Plan.**
- Remove the parameter and the cast. Update both calls and the comment at `:1287`.
- No arithmetic changes, so no golden can move. Prove it: run `scripts/update-goldens.sh` before and after (all 108 hashes unchanged), plus `-L kernel` and `-L determinism`.
- It touches the audio-thread kernel's signature only. Group with 344, same function.

## Row 344: `spliceAt` evaluated four times per tested sample (FIX, M, G4)

**Evidence.**
- `liveRun`'s `inside` lambda (`GrainKernels.h:1252-1272`) calls `headAt`, then `anchorAt` (`windowEnd`, which calls `spliceAt`), then `windowOldestAt` (`windowOldest`, which calls `spliceAt`), then `Reader::fade(live, pos, head)`, which reads both again.
- Each `spliceAt` scans `firstInStorage` (`LiveSource.h:546-569`).

**Plan.**
- Add `struct WindowEnds { int64_t end, oldest; }; WindowEnds windowAt(int64_t head) const noexcept` to `LiveSource`, with one `spliceAt`. Make `windowEnd` and `windowOldest` its projections.
- Use it in `inside`. Give `fade` an overload that takes the ends.
- Bit-identical by construction: the same expressions in the same order. Same evidence as 343: hashes unchanged, `-L kernel`, `-L determinism`, RTSan.
- Optionally show the gain with `gg-bench --cases reference,engine-live`. The row asked for a profile first. With the default now FIX, measure before and after and report the numbers, whichever way they go.

## Row 345: collect and save's loose ends (FIX, S, G1)

**Evidence.**
- `contentPath` hardening: done. Commit `2a9746c` ("a content reference never leaves its root in the plugins' search") put `findSample`'s content-root step, which collect reaches, under one predicate (`media/src/Search.cpp:391`).
- Still open: `media/src/Collect.cpp:55` returns `optional<path>`, and the caller converts it to UTF-8 again (`:117-127`).
- Still open: none of `tests/media/collect_tests.cpp`'s six cases (`:96-252`) collects into an unwritable folder.

**Plan.**
- `collectOne` returns `optional<Collected{path, text}>`, the UTF-8 made once.
- New `[media]` test: "collectMedia lists every entry missing when the folder can't be written". A scratch folder made read-only (POSIX permissions; skip on Windows, or use a path under a file) gives `missing` for each entry, `copied` empty, and the state unchanged.
- Main thread only. No golden.

## Row 346: gg-bench's `--quick` cap written twice (FIX, S, G5)

**Evidence.** `tools/gg-bench/Bench.cpp:517` (`EngineConfiguration`) and `:1334` (`engineCaseSettings`) both apply `kQuickMaxWarmUpSeconds` (`:380`).

**Plan.**
- One `double warmUpSecondsFor(const EngineSetup &, bool quick)` next to the constant, used at both sites.
- Tests: the existing `[bench]` cases ("engine-live-long's at --quick's warm-up") plus `-L bench`.
- No engine code, no golden, no baseline change: the timing configuration is identical.

## Row 347: gg-render allocates its capture before the cap check (FIX, S, G5)

**Evidence.**
- `tools/gg-render/Render.cpp:1067` calls `ringBuffer(...)` and `:1088` calls `gg::media::prepare(buffer, …)`.
- Only after both does `setSource` (`:741-748`) `charge(cost)`.

**Plan.**
- In `capture`, when `op.into` is set, compute the cost before `ringBuffer`: `gg::media::sampleBytes(w.frames, 2, true, levelCountFor(w.frames))`, the same function `sourceBytes` uses.
- Check it with `budget.fits(cost − sourceCost[*op.into])` and throw `BadInput(budget.refusal(…))`, as the plugin's capture does.
- Test, `render` label: `render_capture_over_cap`. It needs a spec with `memoryCapMiB: 64`, a 60 s ring and a 60 s capture into B, and requires exit 2 with the refusal text. Only the order changes, so this also pins the message.
- No golden can move: no golden capture is near the cap.

## Row 348: a `RingReader.cpp` out of `MediaSession.cpp` (FIX, S, G2)

**Evidence.** `plugin/src/MediaSession.cpp:116-334` holds `Span`, `spanOf`, `ReadDone`, `readWith` (two overloads), `windowEntry` and `readRing`, and the ring-read hook at `:39-42`. `MediaSession.h:33-70` declares `readRing`.

**Plan.**
- Move these to `plugin/src/RingReader.{h,cpp}`: `readWith` as a template in the header's `detail` namespace, with `ReadDone`, `Span`, `spanOf` and the hook.
- `MediaSession.cpp` includes it.
- Do this first in G2, so 353 and 386 land in the new file.
- Structure only. The `[plugin][threads]` persist tests prove it. No golden.

## Row 349: `update-goldens.sh` passes no `--content-dir` (FIX, S, G5)

**Evidence.**
- Partly done: `d526169` made the preset group pass `--content-dir "$content_dir"` (`scripts/update-goldens.sh:81`, `:86-89`, `:105-108`).
- The first group (`:80`, `"tests/golden/cases|…|$renders_dir||"`) still passes none. Meanwhile ctest's `golden_${case}` passes `--content-dir ${GG_GOLDEN_MEDIA_DIR}` (`tests/CMakeLists.txt:593`).
- It works today only because `--media-dir` is also a content root for a state (`Render.cpp:709-715`).
- A main case with a `preset` that names a sample would pass in ctest and fail in the script. A preset's samples come from `--content-dir` alone.

**Plan.**
- Set the first group's fifth field to `$media_dir`, as ctest does.
- Evidence: the script's own SHA-256 list. Every one of the 60 hashes must be unchanged, since the same folder is the first root either way.
- No golden can move.

## Row 350: `sampleBytes` omits guard frames (FIX, S, G1)

**Evidence.**
- `media/src/MemoryBudget.cpp:26-44`: `sampleBytes` counts `n·channels` floats, plus the pairs, plus `ceil(n/2^l)·channels` per level, with no guard frames.
- `PreparedSource::bytes()` (`engine/include/gg/source/PreparedSource.h:87-89`) counts "the levels, guard frames included". Each level has `InterleavedStereo::kGuardFrames` frames before and after (`:67`).

**Plan.**
- Make `sourceBytes` return `source.bytes()`, which is exact.
- Make `sampleBytes`, the estimate before building, add `2·kGuardFrames·channels` floats per level, and the pairs' guards if any. `ringBytes` already adds `kRingGuard` (`:63-67`).
- Test, `[media]` `budget_tests.cpp`: "sampleBytes is what a prepared source holds", over mono and stereo, short (1 level) and long (several levels), requiring `sampleBytes(...) == prepare(...)->bytes()`.
- A boundary test at an exact cap may need its numbers moved by a few dozen bytes: say so in the commit.
- No golden. The cap's MiB readout won't change visibly.

## Row 351: two test aids (FIX, M, G2)

**Evidence.**
- The hexfloat dump is done. `fbeec1c` (2026-10-31 08:16, after the row was written at 05:34 in `977abec`) dumps `startPos`, `rate`, `onsetSample`, `onsetFrac` and `invDuration` in hexfloat: `tests/engine/kernel_tests.cpp:1406-1410`, `liveInteriorAtOne`.
- The `adoptRing` hook is not done. `core` has no hook. `plugin/src/PluginTestHooks.h` holds only `RingReadHook`.
- The regression test "a replaced ring stays published until its storage can be collected" (`tests/plugin/persist_tests.cpp:1537-1647`) is still statistical.
- The order it guards is at `core/src/Processor.cpp:1159` (`publishStorage`), then `:1163` (`retireRing(previous)`).

**Plan.**
- Add `gg::core::detail::setRingAdoptHook(void (*)(uint8_t layer))`: a `std::atomic` function pointer loaded relaxed in `adoptRing` between `publishStorage(l)` and `retireRing(previous)`. It is nullptr in production. It is RTSan-clean (one atomic load on the audio thread), and with the hook unset the cost is one relaxed load.
- New deterministic test, `[plugin][threads]`, single thread: "an adoption publishes the new ring before it retires the old one".
  1. Offer a ring, and in the hook record `ringRead(0).storage`, which must already be the new one.
  2. Call `p.sources(0).collect()` from the hook. Nothing is retired yet, so nothing is freed.
  3. Mutation check: swap the two lines and the test must fail, which it does deterministically, unlike the 50-to-64-run statistical form.
- Keep the statistical test as the stress companion.
- No golden: a null hook changes nothing.

## Row 352: a 120 Hz Ring Length drag clicks on the ring (KEEP-BLOCKED, gate)

**Why blocked.**
- The row stays until the director has listened to it at M2's listening gate. It is fixed only if he hears it.
- The click score is 12.7 against the limit of 15. It is deterministic, and the output scores 0.0.
- `gg-render` can't resize a ring mid-render: Ring Length is set in `params` only, and `automation` refuses it. `docs/listening/m2-sources/README.md:56` and `:74` say the resize is heard "only in a host". So there is no render to put in the pack.

**Gate checklist line:** "In a host, Giga Grains FX on a running loop with Overdub around 0.7 and Mix 1: drag A's Ring Length quickly back and forth across 8 s to 60 s for a few seconds. Listen at the ring (grains near the head) for a tick where the drag passes. Heard, yes or no?"
- Yes: it becomes a FIX in the close. The crossfades of the overdub periods meet at a slope kink; smooth the meeting.
- No: close the row as accepted.

## Row 353: a copy the head laps whole fails at its first try (FIX, S, G2)

**Evidence.** `plugin/src/MediaSession.cpp:237-247`.
- For a full-window span, `lapsEveryTry = span.to − span.from > n − (r1 − r0)` (`:242`) is true as soon as `r1 > r0`.
- When `r1 − r0 ≥ n`, `valid = r1 − n ≥ r0 = span.to`.
- So `:245-247` `break`s at try 0, `:257-261` sets `*replaced = true` and returns nullopt, with tries 1 to 7 unused.

**Plan.**
- The row's fix: `if (valid >= span.to) { if (last) break; continue; }`.
- Test, `[plugin][threads]`, single thread, deterministic: "a read the head laps whole tries again".
  - Set `detail::setRingReadHook`. At attempt 0, the hook runs `p.process` for N + 256 frames, which is legal: the test thread is the only one processing.
  - Require `readRing` to return a copy, and the hook to have seen attempt 1.
  - RED at HEAD: nullopt after one attempt.
  - Clear the hook.
- Not audio-thread code. No golden. **It cannot explain the race test's `bad == 1`:** see the last section.

## Row 354: an inactive save keeps a ring twice (FIX, M, G3)

**Evidence.**
- `MediaSession::deactivate` (`:943-990`) keeps `pendingWindow` (frames) and, when cached, `pendingBytes`.
- A save while inactive (`:566-579`) encodes the window into `pendingBytes` and keeps both.
- `memoryUsedBytes` (`:372-380`) counts both.
- `restoreHeld` (`:898-938`) prefers the window.

**Plan.**
- Once `pendingBytes` exists (at the save's encode, and in `deactivate`'s cached branch), drop `pendingWindow` and `refreshBudget()`.
- `restoreHeld` then restores through `restoreRing` from the bytes. Its path already exists (`:916-921`), with `decodeBound`.
- Cost: a float32 WAV parse and copy at activation, instead of a buffer move.
- Test, `[plugin]` `persist_tests.cpp`: "an inactive save keeps a ring's audio once". Deactivate a running 60 s ring and save while inactive. `memoryUsedBytes` must equal the WAV bytes plus the samples, not plus the frames. Activate, and the ring's frames must match the ones saved, bit for bit.
- Main thread only. No golden.

## Row 355: an older restore offer outlives a newer load (FIX, M, G3)

**Evidence.**
- `MediaSession::restore` (`:700-775`) offers a ring per layer that has one (`out.storages[l]`, `slot.offerPending = true`, `:764-772`).
- A layer the newer state has no ring for leaves its exchange's waiting ring offer alone.
- `SourceExchange` has no withdraw (`core/include/gg/core/SourceExchange.h:138-165`).

**Plan.**
- Add `SourceExchange::withdrawRing()` (main thread): supersede the pending ring offer with an empty one, which the audio side's `takeLayerOffers` settles as nothing to adopt. The storage it held goes back through the retire path, as a superseded offer's does.
- Call it from the plugin's state load for every layer the new load offers no ring for, and clear that slot's `offered*` fields.
- Test, `[plugin]` persist: "a newer load withdraws an older restore offer". Load state A (ring on B) without processing, then load state B (no rings), then process one block. B's storage is the one before A, the frames hold no A audio, and `restoreInFlight` is false.
- Touches the audio thread's offer take: run RTSan and `-L rt`. No golden (gg-render never loads twice).

## Row 356: a Rosetta-only run-end difference in `liveInterior` (KEEP-BLOCKED)

**Why blocked.**
- The difference is between `x · 1.0` and `x`, which IEEE makes identical. It appeared only in Rosetta's translation of the x86_64 slice: 3 times on CI, never in 89 local Rosetta runs, never on Windows' or Linux's real x86.
- Apple's translator is outside our control.
- The output is already held bit for bit: both paths are memcmp'd against the reference (`kernel_tests.cpp:1361-1440`), and the difference only WARNs.
- I grepped the logs of CI runs 41539299073, 41539339538, 41534798711, 41509850001 and 41505811559 for "bounds differ between decimation": 0 hits.

**Unblocks:**
- a WARN's diagnostic in any CI log: its hexfloat rows replay as a pinned kernel case;
- or an Intel Mac to run the case on natively.

**Optional for the director:** close it as an accepted record, since the output is bit-identical by test.

## Row 383: `formatOf` untested (FIX, S, G6)

**Evidence.** `plugin/src/editor/EditorPlugin.cpp:82-111`, in an anonymous namespace, needs only a `clap_host*` whose `get_extension(CLAP_WRAPPER_HOST_INFORMATION)` returns a `clap_wrapper_host_information_t` with `get_wrapper_flavor`.

**Not blocked.** No wrapper host is needed: a test builds a fake `clap_host` struct whose `get_extension` returns a static stub.

**Plan.**
- Move `formatOf` into a small header in `plugin/src/editor/` (e.g. `HostFormat.h`), or declare it in `EditorPlugin.h`.
- Test, `[plugin]`, editor builds only: "formatOf names each wrapper flavor": a null host gives "", no extension gives "CLAP", then VST3, AUv2, AUv3 (both "AU"), Standalone, and an unknown flavor gives "".
- This closes row 326's remainder too. No audio thread, no golden.

## Row 384: `capturesFolder` private, copied in the editor (FIX, S, G3)

**Evidence.**
- `plugin/src/MediaSession.h:282` sits under `private:` (`:228`). The editor's copy is at `plugin/src/editor/EditorPlugin.cpp:259-281`.
- They differ in one way: the copy reads `settingsFile()` per call, while the session uses `settingsFile_`, captured at construction. They diverge only when a test changes `GG_SETTINGS_FILE` after it has made the instance, which the harness rule forbids.

**Plan.**
- Move the declaration under `public:`.
- The editor's `context.capturesFolder` becomes `toUtf8(media().capturesFolder())` in a try/catch.
- Test, `[plugin]`: "the editor's captures folder is the session's": the settings file's folder, then the default when unset. This covers part of 388's "thinly tested".

## Row 385: `LivePeaks` isn't re-prepared on a resize (FIX, M, G6)

**Evidence.**
- `core/src/Processor.cpp:239-241` prepares each layer's strip at `kLiveSeconds` (16 s).
- `ui/src/GrainCanvas.cpp:735` takes `oldest = head − peaks->frames() + kBucket`.
- The strip is indexed by absolute ring time (`core/include/gg/core/LivePeaks.h:17-22`), so a longer strip is correct for any shorter ring.

**Plan (recommended).**
- Prepare each strip for the longest ring, `kMaxRingSeconds` (300 s).
- Memory: `ceil(300·sr/64)+1` atomics per layer, 0.9 MB at 48 kHz and 3.6 MB at 192 kHz per layer, allocated at construction on the main thread.
- Clamp the lane's oldest frame to `head − min(peaks->frames(), view.ringFrames)` (`EditorPlugin.cpp:166` already gives `ringFrames`).
- Cost to note: `LivePeaks::reset()` (`Processor::reset`, audio thread) walks every bucket, about 225k relaxed stores at 48 kHz, once per reset.
- Alternative if that is unwanted: move the strip into `RingStorage` and adopt it with the ring under its epoch. That is a size L change.
- Test, `[plugin]` canvas: "a 60 s ring's lane shows peaks over its 60 s". The UI-core `[ui]` geometry is unchanged.
- No audio output changes, so no golden can move (peaks aren't audio). Run `-L rt`: no allocation added in `process`.

## Row 386: splice numbering, the engine against `spanOf` (FIX, M, G2)

**Which side the spec backs.** `docs/superpowers/specs/2026-10-30-m2-sources-design.md` gives each its own job, and both are as specified:
- **The Splice parameter (what the engine plays) counts from the oldest mark still in storage, Loop ignored.** `:205-212`, ruling SP-T1a/b: "the Splice request … counts from the oldest mark **still in storage** (inside the ring's N frames), not inside a narrower loop window". The engine does this in `LiveSource::spliceAt` (`engine/include/gg/source/LiveSource.h:546-569`).
- **A capture's own `splice` argument counts inside the loop window.** `:355-363`, ruling SP-T8a: "The Splice parameter's selection doesn't narrow it". `MediaSession::spanOf` does this (`MediaSession.cpp:124-148`), and so does gg-render's `capture` op.

**The bug is the lane, which shows the Splice parameter's selection with capture's numbering:**
- `ui/src/GrainCanvas.cpp:817-822` outlines `spliceSpan(ring, params.loop, loopFrames, params.splice)`, which is `spanOf`'s ("Loop included").
- The mark numbers at `:827-` are "numbered from the oldest inside the window".
- `ui/src/LaneMenu.cpp:64` passes `params.splice` as the capture's argument, so Capture takes splice k inside the loop window, not the one playing.
- Meanwhile the lane's own Add-splice anchor already uses the engine's numbering (`ui/src/LaneModel.cpp:457-464`, `spliceAnchor`: "The engine numbers from the first mark in storage … and ignores Loop").
- The lane is inconsistent with itself as well as with the engine.

**Plan.**
- The outline and the mark numbers use `spliceSpan(ring, false, 0, k)`, the engine's numbering.
- The loop band stays as it is.
- "Capture the selected splice" captures the span the engine plays. Add a `RingSpanRequest::fromStorage` flag (in the new `RingReader`, row 348): `spanOf` then numbers from the marks in `[r0 − n, r0)` and ignores `loop`. The editor sets the flag when it passes the Splice parameter. gg-render's `capture` op and saves keep SP-T8a unchanged.
- Tests:
  - `[ui]` LaneModel: with Loop on and a mark older than the loop, the outlined span equals `spliceAnchor`'s span.
  - `[plugin]` persist: with Loop on, two marks (one older than the loop) and Splice 1, the lane's capture equals the frames the engine's window reads (`ringMeta` plus the engine's span).
- Main thread and UI only, so no golden can move. `fx-splice-capture` has Loop off and goes through gg-render's SP-T8a path, unchanged.
- **A director option, only if he wants it:** make the engine number inside the loop window instead. That reverses ruling SP-T1a/b (INTENT §23 #68), changes what Splice k plays with Loop on, and can move loop-and-splice renders. Not recommended: the build already chose storage, for block-size invariance.

## Row 387: new user-visible strings for the text pass (DIRECTOR, gate)

**Evidence: all eight strings are present at HEAD.**
- "Load a sample": `ui/src/LaneMenu.cpp:156`, and "Load a sample, or drop one here" at `ui/src/GrainCanvas.cpp:600`.
- "Save the capture as": `LaneMenu.cpp:172`.
- "can't save the capture: …": `plugin/src/editor/EditorPlugin.cpp:250`, `:255`.
- "Osc", "Resamp", "Spec": `ui/src/LayerStrip.cpp:127`.
- "<L>'s sample is loading: set <L> Source to Sample to play it": `GrainCanvas.cpp:1315`, `LaneMenu.cpp:134`.
- "Captured into <L>": `LaneMenu.cpp:135`.
- "can't capture the ring: not enough memory": `EditorPlugin.cpp:202`.

**This isn't a listening item.** Put a "strings" block in the gate's checklist with the eight strings and where each shows. The forced choice:
1. **(Recommended)** The director approves or edits them at the gate, and the close applies his edits. Cost: one checklist block and minutes of reading.
2. Hold them to a whole-product text pass before M5's first public beta. Cost: the row stays open past the close, and the strings ship in 0.2.0's draft as written.

## Row 388: PR 2's deferred minors, item by item (FIX bundle, G6 unless noted)

| # | Item | Class | Plan, test (label), size |
|---|---|---|---|
| a | The hidden-target dim test asserts only "differs"; the theme-switch path is untested (`tests/plugin/mod_view_tests.cpp:383-402`, `CHECK(... != ...)` at `:400`) | FIX | Assert `== palette.dimText` (the palette's dim text color) for the hidden route and `== targetText` for the other, in both themes. Switch the theme and `refresh()`, then assert again. `[plugin]`. S |
| b | `RouteList` refresh's redundant `setText`, and an invalidated draft, untested (`ui/src/views/RouteList.cpp:521`) | FIX | Drop the redundant `setText` if it repeats what `refresh` already set. Add a `[plugin]` test: an edit invalidates a half-built row's draft, and refresh restores "Choose a target". S |
| c | gg-bench's `resolveMs` throw untested (`tools/gg-bench/Bench.cpp:1990`) | FIX | Pull the check into a pure `checkCanvasReport(report)` and test it with a zero `resolveMs` in `[bench]`. S (G5) |
| d | The About box's "shows once" untested | FIX | `[plugin]` GUI test: open About twice, one box. Find the guard in `ui/src/AboutBox.cpp`/`Editor.cpp` at build time. S |
| e | The "never drains" test's name overclaims (`tests/plugin/mutate_plugin_tests.cpp:949`) | FIX | Rename it to say what it checks (one main-thread request while undrained), ASCII only. S |
| f | Held-arrow tests use the real clock | FIX | Add a clock seam (an injectable `std::function<double()>` or a tick count) to the held-arrow repeat, and step it in the tests. Removes a flake source before it shows. S to M |
| g | The instrument's Oscillator-to-Sample band fallback untested | FIX | `[ui]` LaneModel (or `[plugin]` canvas) case: an instrument lane whose Source switches from Oscillator to Sample takes the fallback band. Locate the branch in `GrainCanvas.cpp`/`LaneModel.cpp` at build time. S |
| h | The Budget test walks to the viewport (`tests/plugin/panels_tests.cpp:859`) | FIX | Search below `view.edit` by `param.<budget id>`, as CLAUDE.md's GUI-test rule says, not by walking to the Viewport. S |
| i | `LayerStrip` relies on `lookAndFeelChanged` re-running `resized` (`ui/src/LayerStrip.cpp:225-228`) | FIX | Pin it with a `[plugin]` test: a theme switch on a narrow strip re-measures the menus (the short forms appear or disappear as the new fonts need). S |
| j | `collectSamples` and `capturesFolder` thinly tested (`EditorPlugin.cpp:259-300`) | FIX (G3) | Covered by row 384's test, plus a `[plugin]` test of `collectSamples`' summary text ("Collected N files", the missing list) over a scratch session. S |
| k | `RingCommand`'s mapping is inline (`EditorPlugin.cpp:172-182`) | FIX | Extract `toCommandKind(gg::ui::RingCommand)`, unit-tested in `[plugin]`. S |
| l | The "..." menu's items untested (`ui/src/LoaderStrip.cpp:104`, `loader.more`) | FIX | `[plugin]` GUI test: below the fold width the menu holds Sample folder and Collect, and each item calls its context function. S |
| m | The lane menu's anchor uses the head as of the last frame (`LaneMenu.cpp:72`, `spliceValueFor`) | FIX | Add `CommandKind::AddSpliceAt` with an absolute ring frame as its value (exact in a double to 2^53). The Processor calls `live.addSplice(frame)` directly (`LiveSource::addSplice(int64_t ringTime)` exists). The lane sends the frame it clicked. Tests: `[core]` (the command marks that exact frame at any block size) and `[plugin]` (a click on a running lane marks the frame under the pointer). Touches the audio thread's command apply, so run RTSan and `-L determinism`. No golden: gg-render keeps `behind`. M |
| n | No `[ui]` row for a Live lane shorter than 16 s | FIX | `[ui]` LaneModel case: an 8 s ring's lane maps its 8 s and clamps older frames. S |
| o | `snap_cli` takes about 250 s under ASan against its 300 s timeout (`tests/CMakeLists.txt:838`), and timed out once beside seven others | FIX | Raise `TIMEOUT` to 900. Optionally give it `PROCESSORS 2` so ctest schedules room. The `asan` preset builds no editor, so this concerns a sanitizer build with the editor. S |
| p | The four-lanes canvas test's Sample lane ignores the head; the paint before the clock store only clears the queue | FIX | Store the test clock before `laneOf`, and drop or rename the early paint ("clears the queue"). S |
| q | A refused drop of a path ending in a separator names an empty file | FIX | Trim trailing separators before taking the filename (the loader strip's `filesDropped`, via `ui/src/Editor.cpp:508-512`). `[plugin]` test: "a refused drop of `dir/` names `dir`". S |
| r | The embed-text fixtures' CR LF check isn't aligned to bytes | FIX | Make the check compare byte offsets (`"\r\n"` pairs at even and odd positions alike), not code units. The fixture check is beside `notices_tests.cpp`. S |
| s | "row 330's remaining items stay in row 330" | — | Out of this triage's scope: a pointer to row 330. Nothing to do in row 388. |

---

## The race in "the ring reader's copies match what the audio thread wrote"

The test is `tests/plugin/persist_tests.cpp:1649-1818`. It failed under Rosetta with `CHECK(bad == 0)` at `:1817` showing 1: reads 1912, trimmed 15, swaps 18, adopted 18 (4 of a full ring), declined 0.

**`bad` has two branches:**
- (A) **contiguity** at `:1791-1795`: `copy->oldest + w.frames() != copy->head`;
- (B) **data** at `:1796-1803`: a frame other than the ramp `(oldest + f) % 1000000` (right channel negated).

**Branch A can't be reached.** The test reads with `RingSpanRequest{}` (splice 0, loop false) and `maxFrames` 0 (`readRing`, `MediaSession.cpp:299`).
- `spanOf` then returns `{lo, r0}` (`:140-142`), and `:204` doesn't apply, so `span.to == r0`.
- `readRing` sets `out.head = done->head = r0` (`:310`, `:252`) and `out.oldest = done->valid` (`:309`).
- Untrimmed, the window is `ringToBuffer(…, from, to − from)` (`:302`), exactly `to − from` frames (`media/src/RingAudio.cpp:30`), with `valid == from`.
- Trimmed, it is `to − valid` frames (`:325-326`).
- Either way `oldest + frames == to == head`. **So the 1 is branch B, a data mismatch.**

**Row 353 can't produce it.** Row 353's path is `break` at `:247`, then `:257-261`: nullopt with `replaced`. The test then `continue`s at `:1784-1787` before `++reads`, so the case costs a read and never counts a `bad`. Nothing else in `readWith` returns a copy on a path the row describes.

**I found no other `readWith` path that accepts a wrong frame.**
- **The bound.** A frame is kept when `f ≥ r1 − n` (`:237`). The capacity is `C = N + maxBlock` (`engine/include/gg/source/RingStorage.h:47-49`). The writer publishes the head after every piece with a release store and a release fence (`core/src/Processor.cpp:1211-1221`, called for every layer after each piece at `:2111-2114`), and a piece writes at most `maxBlock` frames.
- So any frame write the reader observed comes after a head publication `> g − maxBlock`. That holds under x86-TSO, which Rosetta runs in hardware, and under ARM with the acquire fence at `:228`, which orders the data loads before `r1`. A slot overwritten by frame `g = f + C` therefore implies `r1 ≥ f + N + 1`, and the frame is excluded.
- **The epoch is a correct seqlock.**
  - Writer, `publishStorage` (`:1237-1246`): odd (relaxed), release fence, storage and head (relaxed), even (release).
  - Reader: `e0` acquire (`MediaSession.cpp:186`), storage acquire (`Processor.cpp:1185`), data, fence, `e1` (`:228-235`).
  - A read that saw the new storage or a post-adoption head sees `e1 ≠ e0` and retries.
- **The adoption copy is exact.** `LiveSource::adoptStorage` copies `[max(copiedTo, head − N′, head − N, 0), head)` with each storage's own capacity (`engine/src/LiveSource.cpp:241-286`).
- **The test's own swap fill is exact.** It is taken with the audio thread held between pieces (`:1745-1779`; the copy must be untrimmed at `:1759`), and it fills `f % next->capacity()` for `[head − N, head)`.
- **Freeing is safe.** The reader thread frees retired storage only between reads (`collect` at `:1781`, and inside `offerRing`), and `adoptRing` retires only after publishing (`Processor.cpp:1159-1163`).

**What the counts say.** Only 4 of the 18 adoptions were of a full ring. While `head < N`, which was most of this run, `valid` is 0 and no slot is ever overwritten (`head < N < C`). The lap bound isn't exercised there at all. A bad frame in that phase could only be a gap in a swapped storage's fill, or a memory reuse, not a reader-bound failure.

**Recommendation, part of G2: make the test say which mechanism it was.** On the first mismatch, record:
- `oldest`, `head`, `f`, the value found and the value expected;
- the read's index and whether it was trimmed;
- `swaps`, `adopted` and `adoptedFull` at that read;
- `p.ringRead(0).head` now.

Then `WARN` it, or `INFO` it before the CHECK. The found value names the cause:
- **expected + C·k** (a newer lap's frame): `readWith` accepted an overwritten frame, so the bound or ordering argument above is wrong somewhere;
- **expected − C, or 0** (a stale or never-written slot): a fill gap in a swapped storage, either in `adoptStorage`'s copy range or in the test's hold copy;
- **anything else**: memory reuse (row 351's class), or a translation fault.

Pair it with row 351's deterministic adoption-order test and row 353's retry fix. Neither is the cause, but together they leave this test as the only open question in the reader, with a diagnostic that will name the cause the next time it fails.
