# T4 triage: sub-project 5's deferrals (rows 389, 390, 392 to 398, 400 to 402)

Worktree `m2-close` at `0189dc9`. Read-only: nothing was edited, built or run in the repository. The
content repository was read through `gh api` (its HEAD is `4fdfe879`, which is also tag `v2`): its README, `select.tsv`
and `make-samples.sh` were read at that commit.

## Summary

| Row | Class | One-line reason / plan | Size | Group |
|---|---|---|---|---|
| 389 | FIX | When a relink's file has the entry's fingerprint, carry the entry's `contentPath` across into the new entry; drop it only when the audio differs. | S | G-samples (with 401) |
| 390 | CLOSE-OBSOLETE | A decided rule recorded in INTENT §19 (`INTENT.md:2839`–`:2841`), spec §6.4 and `RenderSpec.h:93`, and pinned by `preset_source_tests.cpp:630`. Nothing is left to do. | – | – |
| 392 | CLOSE-OBSOLETE | At content HEAD (tag `v2`) the header already names VSCO paths and the stray check is an exact line match. The counts are a per-release changelog line and are correct for v2. No content release is needed. | – | – |
| 393 | DIRECTOR | Close as recorded: INTENT §15 already records VSCO's request, and `THIRD_PARTY_NOTICES.md:167` and `PROVENANCE.md` give the credit. The other choice is to keep the row until a decision to sell. | – | – |
| 394 | KEEP-BLOCKED | The director's ears at the close's gate. The pack already has the checklist item; the exact forced question is given below. | – | G-pack |
| 395 | KEEP-BLOCKED | The director's ears at the gate. Three items are in the pack's notes but not in its checklist; add them as checkboxes (questions below). | – | G-pack |
| 396 | DIRECTOR | Keep the names and tags (all accurate), or strike `live-input` from the FX presets and rename one "Horizon" before 1.0.0. | – | – |
| 397 | KEEP-BLOCKED | The director's eyes at the gate. The browser has no `gg-snap` picture, so it can only be checked in a host (question below). | – | G-pack |
| 398 | FIX | Re-render the pack on `release`, diff the 55 rows of the table, and change the heading at README:97 from `build/dev` to `build/release` (plus any number that moved). | S | G-pack (last in it) |
| 400 | FIX | Not a race. The test's instance draws a fresh random seed (`GigaGrainsPlugin.cpp:191`), and with Distribution 0.5 a rare onset gap lets the shrink land in the first call. Pin Distribution 0 and a fixed seed in the test. | S | alone (test hygiene) |
| 401 | FIX | INTENT §18 D5 step 5 and spec §6.4 put the same-name search after the content roots. `findSample` skips it for a content-only entry because it takes the name from `absolutePath` (empty). Take the name from the reference instead. | S | G-samples (with 389) |
| 402 | DIRECTOR | Keep "values and routes only" and record it as decided (recommended); or also restore the name (M); or restore the name and the samples (L, with a memory-cap policy). | – | – |

Counts: FIX 4, CLOSE-OBSOLETE 2, KEEP-BLOCKED 3, DIRECTOR 3.

---

## Row 389: a relink drops the sample's content reference

**Evidence.**
- `SampleLoader::requestRelink` (`plugin/src/SampleLoader.cpp:121`–`:131`) queues a job that carries the entry's SHA-256 alone.
- `run()` takes the whole new entry from `media::loadSample(job.file, …)` and sets `differentAudio` when the fingerprint differs (`:831`–`:833`).
- `apply()` then replaces the entry outright (`:895`, `entry_ = std::move(result.entry)`).
- `media::loadSample` never fills `contentPath`. The only places in `plugin/src` that set it are `findAndLoad`'s "moved" branch (`SampleLoader.cpp:412`, `loaded.entry.contentPath = entry_->contentPath`) and gg-render's own `fileEntry` (`tools/gg-render/Render.cpp:753`–`:771`).

So relinking a factory preset's missing sample, even to a byte-identical copy, leaves a session that names the file by absolute path alone. The automatic search keeps the reference when it finds the file elsewhere (`:409`–`:413`); the manual relink is the only path that loses it. The row's "arguably right" holds only when the user picks *other* audio.

**Plan.**
- Add `bool relink` to `SampleLoader::Result` (set in `run()` from `job.kind`).
- In `apply()`, before `entry_ = std::move(result.entry)`: when `result.relink && !result.differentAudio && entry_.has_value()`, set `result.entry.contentPath = entry_->contentPath`. This mirrors `findAndLoad`'s moved branch. Other audio drops the reference, as today.
- Optional, in the same task: give any load or relink whose file lies under one of `media::contentRoots(settings)` the root-relative reference, as gg-render's `fileEntry` does. Then a hand-loaded content file saved into a user preset stays portable, and gg-render can render it (see row 390). Treat this as optional, because it changes what a plain load records.
- **Test:** `tests/plugin/sample_tests.cpp`, a new case "a relink to the same audio keeps the content reference; to other audio drops it" `[plugin]`.
  - Set up a state whose layer-A entry has only `contentPath`, with `GG_SYSTEM_CONTENT_DIR` pointing at an empty scratch folder so the entry is relinkable.
  - Relink to a copy with the same bytes: the saved `MEDI` entry has both `contentPath` and the new absolute path.
  - Relink to another file: `contentPath` is empty.
- Not on the audio thread. **No golden can move:** gg-render never relinks.
- Size S. Group with 401.

## Row 390: gg-render refuses a preset sample that has only an absolute path

**CLOSE-OBSOLETE: a decided and recorded rule.**
- `tools/gg-render/Render.cpp:856`–`:861` throws `BadInput` ("has no content-relative reference (gg-render reads only those)"), and `tests/tools/preset_source_tests.cpp:630` pins the message.
- The rule is recorded in three places:
  - **INTENT §19** (`INTENT.md:2839`–`:2841`): "since group 2 a preset's samples are content-relative; an entry with only an absolute path is refused, because `gg-render` never searches".
  - **Spec §6.4** (the presets spec, lines 353–354): "`gg-render` never searches; it resolves content references against `--content-dir` alone and refuses a preset that names content it can't find (exit 2)".
  - **`RenderSpec.h:93`**, and `Render.h:144`.
- No factory preset can hit it: the packer writes only content references (`PresetSource.cpp:223`). A user preset is not gg-render's input.
- If row 389's optional part is built, a user preset of content files gains references and renders. That would be a bonus, not a reason to keep this row.

## Row 392: content v2's tooling and README nits (the separate repository)

**CLOSE-OBSOLETE**, verified at `sethbc/giga-grains-content` HEAD = `4fdfe879` = tag `v2` (`gh api …/git/refs/tags`). The repository has two commits, `1f1fc140` (v1) and `4fdfe879` (v2). The row's claims at HEAD:

1. **"`select.tsv`'s header says 'a VCSL path'": already fixed.**
   - Line 1 reads `source (a VCSL path for download, a VSCO 2: CE path for download-vsco, or a spec)`.
   - Line 39 also has a v2 section comment.
   - `make-samples.sh:10`–`:11` describes both kinds.
2. **"The stray check is a substring match": not so at HEAD.**
   - `make-samples.sh:149`–`:154` builds `listed` as newline-terminated lines.
   - `:275`–`:277` matches `"$file"$'\n'*` or `*$'\n'"$file"$'\n'*`, which is an exact whole-line match.
3. **"The keep match is an exact heading".** `recorded_section` (`:80`–`:86`) matches `$0 == "### Samples/<output>"` exactly. That is the intended behavior: a missed heading makes the row be re-made, and the SHA and stray checks catch any divergence. The row itself says "the stray check covers both".
4. **"The README's file and byte counts are hard-coded".**
   - The content README's counts are the `## Changes` log ("v2 … 51 files", line 85), a per-release record that is correct for v2. Its step list (lines 50–77) has every release add its own entry, so nothing is stale.
   - The byte count is in giga-grains' own `content/README.md:11` (48,200,259 bytes), and it matches the v2 asset (`gh api …/releases/tags/v2`: 48200259).
   - INTENT §15 (`:1566`) repeats it correctly.

**A release in this close is not worth it, and none is needed: nothing is left to change.** Had a README-only fix remained, a `v3` would have cost a new zip, a pin bump and a re-fetch on every CI cache, for no audible change. A v3 is also bound to keep every v2 file byte for byte (content README step 1; `content/README.md:8`–`:9`).

## Row 393: VSCO 2: CE's request for credit and not to sell the samples

**DIRECTOR.** The facts:
- INTENT §15 (`INTENT.md:1570`–`:1573`) already records the request "for the day the content is bundled in something sold".
- Credit is given in `THIRD_PARTY_NOTICES.md:167`–`:168` (carried by the installers) and in `PROVENANCE.md`.
- Nothing in INTENT plans a sale; the product is AGPL and the content is a free download.
- A content release cannot drop the VSCO files: a release is a strict superset (content README step 1), and instrument presets name them.

- **Option 1 (recommended): close the row as recorded.** INTENT §15 is the trigger the row asks for. Cost: none.
- **Option 2: keep the row open until a decision to sell.** Cost: a permanent row that no work can close.
- **Option 3: act now as if the request bound.** For example, a named credit line for VSCO 2: CE and VCSL in the About box beside the notices. Cost: S, but the About box text is checked byte for byte by `notices_tests.cpp`, so the license set and that test change too.

## Row 394: the gained-up sources' noise floors; 07 Ocean Archive's level

**KEEP-BLOCKED: the director's ears at this close's listening gate.**
- The pack already has the item (`docs/listening/m2-presets/README.md:211`–`:223`, "The content's noise floor under granulation"), with every gain listed.
- The pack's note for 15 Kalimba Steps is at `:183`.
- 07 Ocean Archive's quietness appears only in the table (`inst-07-ocean-archive`, -25.26 dBFS RMS) and in "Levels" (`:162`–`:170`). Its source has Level 0, and its only Level route is Env 1 → Level +12 dB (`content/presets/Giga Grains/07 Ocean Archive.json:8`, `:41`).

The exact checklist question, to replace the open-ended "listen for hiss":

> **The noise floor.** In instrument 04, 08, 11, 14, 15, 17, 18 and 03, FX 04 and instrument 07,
> 10, 12, 23 (the renders, then a host), is the hiss under the grains acceptable?
> (a) **Yes, as is**. Nothing changes.
> (b) **Fix it in the presets**: a lower Bus 1 Cutoff, or a quieter or gated layer, per preset you
> name. This moves those presets' goldens (expected files regenerated).
> (c) **Replace the files**: a content `v3` adds denoised or quieter takes under new names (v2's
> bytes can't change), and the named presets are repointed. Cost: a content release, a pin bump,
> and those presets' goldens moved.
> And: **is 07 Ocean Archive loud enough** at -25 dBFS RMS? (a) yes; (b) raise it in the preset
> (its layers' Levels or Density), which moves its golden.

Unblocked by the director's answer at the sitting. Any (b) or (c) becomes a FIX in the close's fix wave.

## Row 395: 22 Shard Storm ticks; 18 and 21 quiet; 17 and 18 need a transport

**KEEP-BLOCKED: the director's ears at the gate.** The pack's "Notes for the sitting" cover all three (`README.md:176`–`:181`, and "Levels" at `:162`–`:170`), but the host checklist (`:190`–`:223`) has no checkbox for them. Add these to the checklist (G-pack):

> - [ ] **22 Shard Storm on a drum loop** (in a host): its pitched-up short grains tick at the
>   transients (the golden runs over a sweep; on `loop-48k.wav` its click score swung 8.5 to 22.7
>   by seed). (a) Keep it, as its glitch character. (b) Soften it: a longer Size floor or a short
>   attack. This moves its golden.
> - [ ] **18 Dotted Echoes and 21 Event Horizon over sparse input**: quiet by RMS over
>   `loop-48k.wav`'s bursts. (a) Fine; (b) raise them, which moves their goldens.
> - [ ] **17 Bar Climber and 18 Dotted Echoes with the transport stopped**: they sound like plain
>   clouds (their descriptions say so). (a) Fine as described; (b) give them a free-running
>   fallback, a mod-routing change that moves their goldens.

## Row 396: preset naming and tags

**DIRECTOR (forced choice).** Checked at HEAD:
- 18 Dotted Echoes' delay is a fixed 375 ms, a dotted eighth only at 120 BPM, and its description says so.
- All 24 FX sources carry `live-input`. FX 04 Undertow is the only one that also reads a content sample, and it carries `sample` too (`04 Undertow.json` tags: `texture, dark, drone, sidechain, sample, live-input`).
- The two "Horizon"s are FX 21 Event Horizon and instrument 01 Bowed Horizon. They are in different products, and each full name is unique.

Spec §6.7 lets the director strike words at the gate. Factory preset **order** is frozen from 1.0.0 (CLAUDE.md), so a rename is cheapest now.

- **Option 1 (recommended): keep them all.** Every tag is accurate (each FX preset does granulate the live input), and every name is unique within its product. Cost: none; close the row.
- **Option 2: strike the redundant tag and de-duplicate.**
  - Remove `live-input` from the 23 FX presets that don't also read a sample: it filters nothing in the FX browser.
  - Rename one of the "Horizon"s.
  - Optionally rename 18 to something tempo-neutral ("Ping-pong Echoes").
  - Cost M: the preset sources change, `presets_built` repacks, and the renamed presets' golden cases rename (`tests/golden/presets/cases/*-NN-<slug>.json` and the expected files; the audio is unchanged, since names and tags aren't in the render). The catalog, discovery and AU preset-list tests that name presets need updating, and so do the listening pack's specs and table.
- **Option 3: retag only and keep the names.** Cost S to M; the 23 sources and any test that counts tags.

## Row 397: the dark browser's list ground changed

**KEEP-BLOCKED: the director's eyes at the gate.**
- Commit `e6e9b94` ("the preset strip, browser and Mutate panel take the theme (row 313)") moved the browser's list from the old `theme::kBackground` (the lanes' dark, `Palette::lane` `#0b0c0f`) to `palette.background`, the header's ground (`#15171c`). See `ui/src/Palette.cpp:29`, `:31` and the commit's `PresetBrowser.cpp` hunk (`g.fillAll(palette.background)`).
- The change was tested for 4.5:1 text contrast in both themes.
- `gg-snap` draws no browser (`tools/gg-snap` has no reference to the browser), and `docs/listening/m2-views/snapshots/` has no picture of it, so the gate must look at it in a host.

Checklist question (G-pack, a new checkbox in the presets pack's host checklist):

> - [ ] **The dark browser.** Open the preset browser in the dark theme: its list now sits on the
>   header's ground (#15171c), not the lanes' near-black (#0b0c0f). (a) Keep it; (b) return the list
>   to the lanes' dark. Option (b) is S: one fill and its contrast test re-checked, and the text
>   and favorite-star contrast must still pass on the darker ground.

## Row 398: the pack's measured table comes from `build/dev`

**FIX.**
- `docs/listening/m2-presets/README.md:97` is headed "Measured (the M5 Max, `build/dev` at this commit)".
- The README's render commands (`:20`–`:52`) use `build/release`.
- There's no reason not to re-measure: the renders are deterministic, and the goldens pass on both Debug (`dev`) and the Release CI legs, so the numbers should match exactly.

**Plan.**
- Run the README's block as written on `release`.
- Read each `$L/<name>.metrics.json` with a short script: peak = `peakDb`; RMS = the power mean of `rmsEnvelopeDb` (as README:99–100 defines it); click = `clickScore`.
- Diff the 55 table rows (README:102–157). Also re-check the ladder's grain counts (171, 946 and 22, at `:186`–`:187`) from the event logs.
- Change the heading to `build/release` and update any value that moved. A moved value is a finding: Debug and Release would disagree, and it should be reported, not just pasted in.
- Evidence: the diff output. Docs only; nothing on the audio thread; no golden.
- Size S. **Group G-pack, done last**, after any fix-wave change that moves a preset's audio (from rows 394, 395 or 396), so the table is measured once on the final presets.

## Row 400: the flake "a Ring Length shrink waits for the grains reading past it"

**FIX (test determinism). This is not a race.**

**The test is single-threaded.** `tests/plugin/persist_tests.cpp:758`–`:796`: the main thread calls `fx.process` (the `render` helper, `:65`–`:83`) and `fx.onMainThread()` in turn. `PluginUnderTest` has no host thread pool, and nothing else in `MediaSession::pollRingLengths` (`plugin/src/MediaSession.cpp:1025`–`:1135`) or the engine reads a clock. `MemoryBudget` is pure arithmetic (`MemoryBudget.h:14`). `-j8` can't change its outcome.

**But its seed is random on every run.**
- The test never loads a state, so the instance keeps the seed it drew at construction: `seed_(freshSeed())` (`plugin/src/GigaGrainsPlugin.cpp:191`), from `std::random_device` (`:168`–`:181`).
- This was introduced by `5ac8a86` (sub-project 5's spec §7) and predates the test (`dd9bfa0`).
- The harness documents the remedy: `loadDefaultsWithSeed` (`tests/plugin/PluginHarness.h:764`–`:780`), "a test that compares … loads this first".
- The test sets Density 64 Hz and Size 50 ms, but leaves **Distribution at its default 0.5** (`core/src/ParamIds.cpp:174`–`:176`). The onset intervals are therefore Δ = 750 × (0.5 + 0.5·E) samples, with E a unit exponential keyed by the seed (`CloudScheduler.h:124`–`:131`, `:432`–`:438`).

**How the run fails.**
1. If no grain reading 2 s back is still sounding (beyond the first ~32 samples of the call) when the Ring Length event arrives, `Engine::checkReadLimit` lands the read limit in the same call (`engine/src/Engine.cpp:318`–`:338`).
2. The overdub period's 10 ms crossfade (480 samples) then ends inside the 512-sample block (`kBlock`, `persist_tests.cpp:58`). After that call `framesInUse()` (`LiveSource.h:247`–`:251`) is already 48,000.
3. So the first `onMainThread` offers at once. `CHECK(media.status(0) == "resizing")` and `CHECK_FALSE(ringAdoptionPending())` fail, and `polls` comes out 1, which fails `CHECK(polls > 1)`.

A 50 ms gap (Δ > 2400 samples, so E > 5.4) covers an instant with probability of roughly E[(Δ−2400)+]/E[Δ] ≈ 0.2%. That fits one failure seen over the many runs of this case (every check.sh and CI leg, 2026-10-31 to 11-02) and a pass on rerun.

**Shared mechanism with the `persist_tests.cpp:1817` investigation: none in the failure.**
- "a save of a running ring keeps the window's length" is a real two-thread test. It drives a bare `Processor` with seed 7 and races the ring reader's lapping (`readWith`) against an audio thread.
- Row 400's test goes through `readWith` too, in the resize copy (`MediaSession.cpp:1087`–`:1099`). But it is single-threaded: the head can't lap the copy, so its failure can't come from there. The two share code, not cause.
- Tell the other investigation that row 400 is explained by the seed, so it isn't counted as evidence of a reader race.

**Plan.**
- In the test, add `REQUIRE(fx.loadDefaultsWithSeed(<seed>))` before the `setParam` calls, and `setParam(fx, idOf(LayerParam::Distribution, 0), 0.0)`.
  - Periodic onsets every 750 samples under 2,400-sample grains mean at least three grains are always reading 2 s back, whatever the seed.
  - Spray's 50 ms default keeps every read past 1 s. The fixed seed makes every run the same bytes.
- Keep the 40-poll bound. Optionally record the landing poll count in `INFO` (it already is).
- **Verify the diagnosis before changing the test:** with the old parameters, loop the case body over seeds 0 to 2000 via `loadDefaultsWithSeed` (a throwaway, uncommitted run). Expect a handful of failing seeds with `polls == 1`, and none after Distribution 0.
- Sweep for siblings: other `persist_tests.cpp` cases that assert grain timing on a fresh instance without `loadDefaultsWithSeed` (`grep -n 'PluginUnderTest fx(kFxId)'`, then check each for timing assertions). List any found; fix those that assert grain-dependent timing.
- Test-only; nothing on the audio thread; no golden can move. Size S. Label `plugin`.

## Row 401: no same-name search for a content reference outside the content roots

**FIX. The code departs from the spec here; this is not a decided rule.**
- Spec §6.4 (lines 349–354) says the content roots come "after the absolute path and **before the same-name search**". Its "`gg-render` never searches" is gg-render's rule, not the plugin loader's.
- INTENT §18 D5 (`INTENT.md:2310`–`:2325`) lists step 5, "a file of the same name … then anywhere under the sample folder", for every entry.

**At HEAD, `media::findSample` takes the name only from `absolutePath`** (`media/src/Search.cpp:419`–`:424`). A factory preset's entry has no absolute path (`core/include/gg/core/Preset.h:26`; `PresetSource.cpp:223`), so `name` is empty and step 5 (b, the last load folder; c, the sample-folder scan) is skipped. Step 3 needs a `relativePath`, which such an entry also lacks. The row's description of the behavior is accurate; its claim that the spec requires it is not.

**Plan.**
- In `findSample`, when `entry.absolutePath` is empty and `isContentRelativePath(entry.contentPath)`, take `name` as the reference's last component.
  - Step 5a has no saved folder and is skipped.
  - 5b and 5c run as for any entry, matched by size and then SHA-256 as every candidate is, so a same-named file with other content is never taken.
- Update the doc comment in `media/include/gg/media/Search.h:39`–`:60` (step 5's name source).
- `SearchCache` needs no change: misses are keyed by the entry and the settings' folders and expire after 30 s.
- Update INTENT §18's step 5 text. Part II changes need a dated §23 entry in the same change (CLAUDE.md), or fold it into the close's §23 entry.
- **Tests:**
  - `tests/media/search_tests.cpp`, a new case "findSample finds a content reference by its name in the last load folder and under the sample folder" `[media]`: a content-only entry not under any root is found in `lastLoadFolder`, then by the scan; a same-named file with other bytes is not.
  - Plugin: a case in `tests/plugin/preset_tests.cpp` (or the file holding the preset-load cases) loading a packed fixture preset whose sample is missing from `GG_SYSTEM_CONTENT_DIR` but present in the settings' sample folder (`useSettingsFile`): it loads `[plugin][preset]`.
- Not on the audio thread.
- **No golden can move.** gg-render's preset path never calls `findSample` (`Render.cpp:847`–`:893`). Its state path does (`:812`–`:813`), but with `sampleFolder` and `lastLoadFolder` empty (`Render.cpp:717`–`:718`) the new candidates are skipped, and the state goldens' samples are found at step 2 anyway.
- **Cost to note:** when a factory sample is missing and the user has set a sample folder, a preset load from the browser now scans it, up to `kMaxScanEntries`, synchronously on the main thread (D8). That is the same cost a session restore already pays. `SearchCache` makes a repeat within 30 s free.
- Size S. Group with 389 (both in the sample-reference path; one task, one review).

## Row 402: a preset recall's undo restores values and routes only

**DIRECTOR (a forced choice).**

**What is built.**
- `EditorPlugin::pushWholeState` (`plugin/src/editor/EditorPlugin.cpp:306`–`:318`) pushes `undo_->pushSnapshot(label, vBefore, rBefore, uiValues(), uiRoutes())` after a browser or strip recall (`:361`–`:371`) and after each Mutate press (`:432`–`:440`).
- `gg::ui::UndoStack` (`ui/include/gg/ui/UndoStack.h`) is framework-free and "knows nothing of JUCE or the plugin". A `Snapshot` holds only the changed `ParamChange`s and an optional `RoutesChange`. Undo replays them through `setValues`/`sendValues` and `setRoutes`, which is the gesture path INTENT §17 D7 requires (`INTENT.md:2094`–`:2098`, `:2138`), so hosts record it.
- The preset's identity lives in the plugin: `presetName_`, `presetKey_`, `presetKeyByName_`, `presetValues_` and `presetEditedMark_` (`GigaGrainsPlugin.cpp:1527`–`:1546`). Its samples live in the loaders: `applyState(…, LoadKind::Preset)` → `MediaSession::restorePreset` (`MediaSession.cpp:792`–`:832`), and each layer the preset names no sample for keeps its own (D5). So FX presets other than 04 never touch samples; all 24 instrument presets do.
- The test is "a preset load, INIT and a Mutate press are one undo entry each, the rings kept" (`tests/plugin/browser_tests.cpp:896`).

**Option 1 (recommended): keep it as built and record it as decided.**
- Undo restores the values and routes. The strip then shows the recalled preset's name with the edited dot, which is accurate: the samples the recall loaded are still loaded, so the instance is neither preset.
- Redo clears the dot.
- Write this into INTENT §17's undo paragraph and spec §6.8, with a §23 entry, and close the row.
- Cost: docs only, S.

**Option 2: also restore the name (the preset identity), not the samples.**
- The build:
  - give `UndoStack::Snapshot` an opaque extra, for example a `std::shared_ptr<const void>` payload applied through a new optional `UndoCallbacks::setIdentity(payload, undoing)`, so `ui/` stays plugin-free;
  - `EditorPlugin` captures `{presetName_, presetKey_, presetKeyByName_, presetValues_, presetEditedMark_}` before and after the recall;
  - add a plugin setter.
- Tests: in `tests/ui/` (`[ui]`, the stack) and the browser undo case (`[plugin]`).
- Size M.
- The catch: after undoing a recall that loaded samples, the strip would read the old preset's name, *unedited*, over the recalled preset's samples, because `presetEdited` compares values only. So the payload must also force the edited mark whenever the recall changed any layer's sample.

**Option 3: restore the name and the samples.** Size L. What it would take:
- **The entry must hold the replaced samples.** That is either:
  - (a) their `MediaEntry`s, which works only for file samples; a sample captured from a ring has no file, and its `AUDI` bytes are dropped by the recall, so (a) cannot bring it back; or
  - (b) a `shared_ptr` to the replaced `PreparedSource` plus its embedded bytes.
  Redo needs the recall's samples the same way.
- **A new `MediaSession` path applies a layer's sample on undo** without a state load: `restorePreset` with the old entries, plus ruling R19's keep when the source is held. With (a) this is a synchronous main-thread find-and-decode (D8), seconds for a large file, and the file may since be missing or changed. Undo is then partial, values restored and the layer silent, with a status.
- **The memory cap under each variant:**
  - (a) A re-decode is checked like any load, against the cap less the layer's own source (`SampleLoader::budgetNow(release, …)`, `SampleLoader.cpp:528`–`:539`). It fits whenever the old sample fitted before, unless the user has loaded more or lowered the cap since; then the undo is refused for that layer ("over the memory cap"). That's a partial undo again.
  - (b) Holding sources in up to 200 entries keeps memory alive outside `memoryUsedBytes()` (`MediaSession.cpp:361`–`:387` counts only the loaders, the rings and pending bytes). It must be added there, or the cap stops meaning anything. Once added, invisible undo history makes new loads fail "over the cap". That needs an eviction rule, for example dropping the samples from the oldest entries first so they degrade to values-only, with their labels saying so.
- **The sample swap is not a parameter**, so hosts don't record it, unlike the rest of D7's undo.
- **Tests:** the stack's payload (`[ui]`); undo and redo of a recall over file samples, a missing file, a capture, and the cap at its limit (`[plugin]`); and the tripwire on `process` while an undo's sources are offered (`[plugin][rt]`).

**Separate from all three options:** a recall over a layer holding a ring capture discards that capture with no way back, under any option except 3(b). Note this for the director when the question is put.
