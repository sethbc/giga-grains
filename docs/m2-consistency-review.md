# M2 consistency review

**Status:** 2026-11-02, at `main` `0189dc9` (branch `m2-close`). M2 sub-project 7's item 1 (the
director: "make sure that we do a full consistency pass on the documents and raise any internal
inconsistencies in the decisions that have been made thus far"). Method: four opus reviewers, one
per document family (A: INTENT internally; B: CLAUDE.md, CI, build and release docs; C: the M2
program design and plan, specs 1 to 3; D: specs 4 to 6 and the six listening packs), then one
reconciling reviewer, who checked every conflict and unsure item at its source, spot-checked 28 of
the 71 drift findings, and merged duplicates. The reviewers' IDs (A-3, C-11 …) stand beside each
new ID.

## How to read this

- **CONFLICT (`K-n`):** two decisions disagree, or a ruling bends a director decision with no
  recorded director sign-off. Never resolved silently: each is a forced choice for the director,
  option 1 the recommendation. Each answer becomes a dated INTENT §23 row.
- **DEFECT (`X-n`):** a bug in code or CI configuration, not text. Fixed in the close as a task.
- **DRIFT (`R-n`):** text lags a decision already made, or a document disagrees with the code or
  configuration. Fixed in the close; the fix names the deciding source.
- Part II edits made by the DRIFT fixes are covered by one dated §23 row for this pass (CLAUDE.md:
  every Part II change adds one). §23 rows are history: a drift fix there is a pointer annotation
  ("amended by #NN"), never a rewrite. No DRIFT fix touches Part I; only K-4 and K-6 can.
- Item 6 of the close renames the design and plan to `…-program-design.md` and `…-program.md`;
  the paths below are the current ones.

## Summary

Counted in each family that raised a finding; the last column counts each merged finding once.

| Class | A (INTENT) | B (build, CI) | C (program, specs 1–3) | D (specs 4–6, packs) | After merging |
|---|---|---|---|---|---|
| CONFLICT | 3 | 0 | 4 | 2 | **8** |
| DEFECT | 0 | 2 | 0 | 0 | **2** |
| DRIFT | 26 | 17 | 21 | 20 | **69** (R-48 from the brief's seeds alone) |

The reviewers raised 9 conflicts, 71 drifts and 4 unsure items (84). Reconciled: 8 conflicts (A-2
and D-1 are one), 2 defects (B-7, B-8, raised as drift), 69 drifts, no unsure item left.

---

## Conflicts

### K-1. The restore criterion (C-1)
**Question:** must a restored session match the saved session's own continued render within the
goldens' tolerances, or only render the same on every restore?
- §23 #64 (the director, 2026-10-29): "the `AUDI` codec is sub-project 2's choice, under a
  criterion that every restore renders identically and matches the saved session within the
  goldens' tolerances". Program design §7.5 l.484–487 says the same.
- Ruling SP-T7e (Claude, 2026-10-30; sources spec §6.6 l.571–575; INTENT §18 l.2288–2290): "a
  restored session renders deterministically, the same on every restore, but not identically to
  the saved session continuing … The `[plugin]` tests gate the restore's determinism … not identity
  with a continued render."
- §23 #68: float32 "restores bit for bit, which exceeds the program design's criterion". True of the
  ring's frames, not of the render: nothing tests "within the goldens' tolerances".
- Unamended: sources spec goal 1 l.48–50 ("renders bit-identically to the saved session") and §10
  l.913–914 ("restore then render equals the saved session's render from the save point").

**Options:**
1. **(Recommended)** Amend the criterion to what was built: every restore renders identically and
   restores the saved frames bit for bit; the clock and grain index restart (SP-T7e). Amend design
   §7.5, sources goal 1 and §10, and #68's "exceeds"; note it on #64. *Cost:* a reopened frozen pad
   replays the same material with new grain draws, never the saved session's continuation.
2. Keep the criterion and add the check: a `[plugin]` case comparing a restore's render with the
   saved session continuing, by the goldens' metrics. *Cost:* a sparse cloud may fail it; meeting
   it exactly needs the clock and grain index in the state, a format change.

**Resolution:** Director, 2026-11-02: option 1 (§23 #78).

### K-2. A click entering Peak mid-sound, outside D5b's scope (D-2)
**Question:** is the click on a mode change into Peak over hot material part of Peak's accepted
law, or a defect?
- Extremes spec §3 l.83–84, anti-goal: "A click or a zipper when … the limiter's mode changes". §6.5
  l.459–460: "A mode change crossfades … so switching never clicks".
- D5b (the director, 2026-10-31; spec l.458; §23 #69): `kLimiterDrive` "looks away over the calls in
  which Peak's gain falls below −6 dB or Soft's input passes twice the ceiling, plus 10 ms after,
  and nothing wider".
- Row 378 (closed): "Peak entered mid-sound at −18 dB still scores 24.4 at 461 samples into the mode
  fade … that click is outside the stated scope, so it is exempt only when the draw's calls fall so
  that the dip lands in an earlier call".

**Options:**
1. **(Recommended)** Accept it as part of Peak's zero-attack law (D5b). Spec §3, §6.5 and INTENT §14
   l.1305–1306 say so; `kLimiterDrive` exempts the mode fade by samples, not by where calls fall.
   *Cost:* one anti-goal relaxed; a small test change.
2. A defect: inside a mode crossfade Peak's gain falls no faster than the fade; a carry-over row.
   *Cost:* a limiter DSP change, `fx-limiter` may move, the presets that use Peak re-checked by ear.

**Resolution:** Director, 2026-11-02: option 1 (§23 #79).

### K-3. INIT keeps Ring Length against D8 (A-2, D-1)
**Question:** does INIT keep each layer's Ring Length, as the controller ruled, or reset it, as D8
says?
- D8 (the director, 2026-10-30; presets spec l.793–795): "**A fresh instance, bit for bit:** every
  default, the factory routes, nothing else. **Decided: option 1.**"
- The controller's ruling (presets spec l.240–243): "**Amended 2026-11-02 (the final review's fix
  wave; the controller's ruling, D8):** INIT keeps each layer's **Ring Length** alone". INTENT §18
  l.2371–2374 and #70 ruling (2) the same.
- Row 391 (closed) credits "the director's D8: a fresh instance; only Ring Length is memory"; D8's
  text has no such clause. #70's "Decided by" names the program manager for "the ring rulings" and
  no one for the 2026-11-02 amendment.

**Options:**
1. **(Recommended)** Ratify as D8a, "INIT keeps each layer's Ring Length (memory, not sound)"; the
   spec's D8, §18, #70 and row 391 name the decider. *Cost:* INIT differs from a fresh instance
   whenever a ring isn't 16 s.
2. INIT resets Ring Length to 16 s. *Cost:* INIT resizes every ring on the main thread, can be
   refused by the memory cap, and cuts a held ring's audio past 16 s; code, tests, §18 and #70
   change.

**Resolution:** Director, 2026-11-02: option 1 (§23 #80).

### K-4. The overload ladder's order (A-1)
**Question:** does Part I's "lower the tier, then drop, then steal" mean an escalation with load,
as the engine spec reads D4, or the order of the per-grain tests, which the code reverses?
- Part I §8.4 l.118–120: "(§14: a lower interpolation tier first, then dropped new grains, then
  stolen grains with fades)"; signed off by the director in #61 ("Part I, with the director's
  sign-off: overload order"). CLAUDE.md's non-negotiables and §14 l.664–666 give the same list.
- D4 (the director, 2026-10-30; engine spec l.711–713): "the oldest sounding grain is stolen … only
  once it has sounded for this share of its window; before that, new grains are dropped".
- §14 as built l.685–688: "else, if no tier fits or the pool is full, **steal** the oldest sounding
  grain if it has sounded at least half its window (D4) … and otherwise **drop** the new grain";
  l.695–696: "It mostly steals". Engine spec l.389–393 calls this "§14's" order, read as escalation.

**Options:**
1. **(Recommended)** Confirm the escalation reading. Reword §14's list, l.685–688 and CLAUDE.md's
   non-negotiable to "lower the tier; while the oldest sounding grain is under half its window, drop
   the new grain; past it, steal it with a fade"; a §23 row says this is how §8.4 reads (D4). *Cost:*
   no Part I edit; a non-negotiable gains a clause.
2. Amend Part I §8.4 and CLAUDE.md to the test order ("lower the tier, then steal the oldest grain
   past half its window, else drop"). *Cost:* a Part I edit; the non-negotiable's wording changes.

**Resolution:** Director, 2026-11-02: option 1 (§23 #81).

### K-5. Which held rings a session saves: D1 narrowed (C-11)
**Question:** is a held ring saved only on a layer whose base Source builds to Live (the
controller's ruling), or on every held layer, as D1 reads?
- D1 (the director, 2026-10-30; sources spec l.750): "**A ring is saved when it is held: frozen, or
  Loop on.**"
- The amendment (2026-10-31, the final review; sources spec l.755–758): "A held ring is saved only on
  a layer whose base Source builds to Live". §23 #68: "(the controller's ruling; the cost, a Sample
  layer switched back to Live after a reopen starts with an empty ring)".
- The director's later D-specsave (2026-11-01; extremes spec l.718–723) adds Spectral layers with no
  sample; it does not rule on Sample layers.

**Options:**
1. **(Recommended)** Ratify the narrowing; record it in #68. *Cost:* a frozen Sample layer's ring
   (still recording, SP-T5c) is lost on reopen; switching that layer to Live then gives an empty
   ring.
2. Save every held ring, Sample layers included. *Cost:* 6.1 MB per layer at 16 s, up to 115 MB at
   300 s, of audio no grain reads unless the kind changes; the 32 MiB embed cap fills sooner.

**Resolution:** Director, 2026-11-02: option 1 (§23 #82).

### K-6. Part I §5 promises live buffers "saved with the session" (A-3)
**Question:** amend Part I to the held case D1 decided, or keep Part I and read it there?
- Part I §5 l.79–80: "minutes-long *live* buffers inside a plugin, saved with the session and
  managed with splices".
- D1 (the director, 2026-10-30; INTENT §18 l.2235–2237): "A plain live ring refills from the input
  and isn't saved". Made on Part II; Part I was not touched.

**Options:**
1. **(Recommended)** Amend Part I §5: "saved with the session when held (frozen or looped)". *Cost:*
   a one-clause Part I edit.
2. Keep Part I; §18 says §5 means the held case (D1). *Cost:* Part I over-promises to a reader of
   Part I alone.

**Resolution:** Director, 2026-11-02: option 1 (§23 #83).

### K-7. "A merge, not a rebase" against "each rebased and re-verified" (C-3)
**Question:** ratify the program manager's merge mechanism, which replaced the rebase the
director's decision names?
- §23 #64 (4) (the director, 2026-10-29): "merge order 3, 2, 4, each rebased and re-verified".
  Design §4 l.126–127, §6 l.208–209 and l.213–215, plan l.18 and C1 l.93–95 ("Rebase … force-push")
  the same.
- §23 #68: "merged into this branch from `main` on 2026-10-30 (a merge, not a rebase, the program
  manager's ruling)"; #69 twice ("one merge, not a rebase"). The close's brief keeps it ("never
  rebase or force-push"). Re-verification was done each time.

**Options:**
1. **(Recommended)** Ratify: design and plan say "rebases on, or merges, the merged result, then
   re-verifies"; a note on #64. *Cost:* none for M2's history, which stays non-linear.
2. Keep "rebase" as the rule; record M2's merges as departures. *Cost:* nothing can be redone; the
   rule keeps stating what wasn't done.

**Resolution:** Director, 2026-11-02: option 1 (§23 #84).

### K-8. Carry-over rows 118 and 119 moved on the program manager's word (C-10)
**Question:** ratify moving rows 118 and 119 from sub-project 2 to sub-project 1?
- Plan A3 l.65: a move "is a design amendment, which needs the PM's and the director's word".
- §23 #65: "**The programme manager's acceptance** (2026-10-29): carry-over rows 118 and 119 move
  from sub-project 2 to this one"; "Decided … by the director (D1–D5) and the programme manager (rows
  118 and 119)". No director word is recorded.

**Options:**
1. **(Recommended)** Ratify; record it in #65. *Cost:* none; the work is built and reviewed.
2. Amend plan A3 so the program manager alone may move an item that shrinks a later sub-project.
   *Cost:* the director loses sight of scope moves.

**Resolution:** Director, 2026-11-02: option 1 (§23 #85). (The text fix, R-53, holds either way.)

---

## Defects

### X-1. CI's Test step is skipped when the libstdc++ check fails (B-7)
- **What:** `ci.yml` l.22–23 promises "Once the build succeeds, every later check runs even if an
  earlier one failed". The *Test (unit, plugin and golden)* step (l.612–618) has no `if:`, so it
  runs only on `success()`; on Linux a failed "Linux products need no libstdc++ newer than Ubuntu
  22.04's" (l.555–591) skips every ctest, the event-log gate included.
- **Fix:** give the Test step `if: ${{ !cancelled() && steps.build.outcome == 'success' }}`, as the
  later steps have. The installer step keeps its plain `success()`.
- **Resolution:** fixed in the close (plan task TBD).

### X-2. The Rosetta golden loop skips the 48 preset goldens (B-8)
- **What:** `ci.yml` l.637–638 says the x86_64 slice runs "every golden case with the same commands
  as the golden CTests", but l.697 globs `tests/golden/cases/*.json` (60) alone: the 48
  `golden_preset_*` cases never run under Rosetta, and its command passes no `--content-dir` (the
  CTest passes one, `tests/CMakeLists.txt` l.593). CLAUDE.md ("the golden cases three `gg-render`
  processes at a time") and §23 #75 ("no case is dropped") assume full coverage.
- **Fix:** extend the loop to `tests/golden/presets/cases/*.json`, reading the presets that
  `presets_built` packed into `build/mac-universal/golden-media/presets/` and passing
  `--content-dir "$CONTENT_CACHE_DIR/content"` to every case. Measure the step against its 20-minute
  limit; if it doesn't fit, say in `ci.yml`, CLAUDE.md and a carry-over row which cases the slice
  leaves out.
- **Resolution:** fixed in the close (plan task TBD).

---

## Drift

### INTENT.md

**R-1** (A-4, D-18). Header, l.7: sub-projects 1–4 "in review", 5 and 6 absent.
Fix: "sub-projects 1–6 built and merged (§23 #65, #66, #68 to #71; PRs #15, #16 and #23, #19, #20,
#17 and #25, #18 and #24); sub-project 7 (the close) under way". Resolution: Fixed in `47c6eac`.

**R-2** (A-5, B-1, C-24, D-18). §20's M2 row, l.3124: "**6 Views** (PR 1 built; §23 #71)"; 5's PRs
not marked merged. #71 covers PRs 1 and 2; PR #24 merged 2026-11-02.
Fix: "5 Presets and content (PRs #17 and #25, merged; §23 #70), 6 Views (PRs #18 and #24, merged;
§23 #71)". Resolution: Fixed in `47c6eac`.

**R-3** (C-12). §20 l.3124's M2 exit, and design §2 l.62–63 and I37 l.178: "against M1's
baselines". The x86 baselines were remade at the AVX2 task (ES-T12d, §23 #66).
Fix: "against the committed baselines (M1's on `mac-universal`; on x86 the entries remade at the
AVX2 task, §23 #66), and X within 3% of M1's published headline on the reference core (§23 #41)".
Resolution: Fixed in `47c6eac` (design §2 and I37, outside INTENT: fixed in `32b9d79`).

**R-4** (A-6). §14 l.326: "(A top left, B top right, C bottom left, D bottom right)". §17 l.2067,
the views spec l.331–332 and `ui/include/gg/ui/PadModel.h` l.17–19: A bottom-left, Y up.
Fix: "(with Y up: A bottom left, B bottom right, C top left, D top right; §17)". No formula moves.
Resolution: Fixed in `47c6eac`.

**R-5** (C-23). §14 l.204–206: "The four kinds are Sample, Live, Oscillator and Resample". l.392–394
enumerates five; Spectral is built (#69).
Fix: "§4's four kinds plus Spectral, a smear of a layer's sample or ring (M2 sub-project 4)".
Resolution: Fixed in `47c6eac`.

**R-6** (C-6). §14 l.269–271 ("it costs nothing … while no layer's source is Resample"), contradicted
by its own sub-bullet l.276–278; design §7.2 l.424–425; sources spec §10 l.894 ("written only while
a Resample layer exists"). Ruling SP-T12ac (#68): the tap's history is written whatever the kinds.
Fix: "It adds no latency; its history is two stores a sample whatever the kinds (SP-T12ac, §23
#68)" in all three. Resolution: Fixed in `47c6eac` (design §7.2 and the sources spec §10, outside INTENT: fixed in `32b9d79` (design) and `6bdf1b1` (sources)).

**R-7** (A-13, C-25). Memory figures: §14 l.226 and l.294–295 ("about 51 MB … four rings about 25
MB"), l.837–838 (calls 51 MB the fixed costs); sources spec l.385–386 ("about 65 MB"). By INTENT's
own figures a 16 s ring with its levels is 11.5 MB (l.854), so four are about 46 MB, and sub-project
4 adds 8.4 MB fixed (l.299–301).
Fix: compute the figure from the code (or measure it), state it once at l.294–295 split into rings
with levels (counted against the cap) and fixed costs (pools, reverb, event buffers, sub-project 4's
Spectral and delay buffers); l.226, l.837 and the sources spec point at it. Resolution: Fixed in `47c6eac` (the figure computed from its components' sizes, not measured; the sources spec's pointer, outside INTENT: fixed in `6bdf1b1`).

**R-8** (A-7, C-7). Ring Length as a mod target: §14 l.474–476 and l.1277–1279, §18 l.871–872,
l.2655 and l.2661–2664 say it is admitted or unsettled; sources spec §6.3 l.309–313 unamended. Ruling
SP-T12c (#68): refused; `core/include/gg/core/ModMatrix.h` l.144 has `ref->param !=
LayerParam::RingLength`.
Fix: "every layer parameter but the modulators' controls and Ring Length (SP-T12c, §23 #68)" at
each; delete l.2661–2664's "checked after the rebase" clause; add an SP-T12c amendment under sources
§6.3 and to its status line. Resolution: Fixed in `47c6eac` (the sources spec §6.3 amendment, outside INTENT: fixed in `6bdf1b1`).

**R-9** (C-19). §14 l.697–700 and engine spec §6.5 (l.405–410): the steal overshoot is "8% of the
default budget at 16,384 onsets a second". 256 / 48,000 s × 16,384/s ≈ 87 grains: 2.1% of the
default Budget (4,096), 8.5% of the exit case's 1,024-grain quota.
Fix: "about 87 grains, 2% of the default budget (8.5% of the exit case's 1,024-grain quota)".
Resolution: Fixed in `47c6eac` (engine spec §6.5, outside INTENT: fixed in `6bdf1b1`).

**R-10** (D-3). D-glide (the director, 2026-10-31; row 379) accepted a crackle, but §14 l.1287–1290,
extremes spec §3 l.84, §6.4 l.425 and §8 D6 l.688–689 still say "never clicks".
Fix: add "except inside a Feedback loop at 95% or more over a near-silent tail, where the re-read
passes crackle: an accepted law (D-glide, row 379)" at each, marked **Amended (D-glide)** in the
spec. Resolution: Fixed in `47c6eac` (the extremes spec's amendments, outside INTENT: fixed in `6bdf1b1`).

**R-11** (D-4). §14 l.1305–1307 and the extremes pack l.86–88: "Peak's own click score on hot
material is 15 to 18 … which a listener should judge". D5b (2026-10-31) kept the law; row 378
measures 16.24 to 42.05 for Peak and up to 19.77 for Soft at the bottom ceilings; no document says
where "15 to 18" was measured.
Fix: cite D5b and row 378's measured range with its material; drop "15 to 18"; the pack's question 1
becomes whether to revisit the law after M2. (Text depends on K-2's answer for the mode change.)
Resolution: Fixed in `47c6eac` (the extremes pack's question 1, outside INTENT: fixed in `13199cd`).

**R-12** (D-5). D-specwindow (the director, 2026-11-01, option 1: defer, row 381) is in no body text:
§14's Spectral bullet l.1245–1251 and extremes §6.2 (ends l.302).
Fix: "**Amended (D-specwindow, 2026-11-01):** until row 381 is built, a Spectral layer's analysis
reads Position over the whole ring whatever Loop, Loop Length and Splice say, and the ring commands
skip it". Resolution: Fixed in `47c6eac` (extremes §6.2, outside INTENT: fixed in `6bdf1b1`).

**R-13** (A-20). §14 l.1074–1076: Size "set, don't sweep" points at the carry-over. Row 217 is closed
(D10, §17 l.2092–2093, #71).
Fix: "the FX view's knob sets on release (D10, §23 #71; row 217 closed). Automation still clicks."
Resolution: Fixed in `47c6eac`.

**R-14** (A-21, D-8). Clauses waiting on built work: l.821–822 and §21 l.3144 ("shown in the UI by
sub-project 6"), l.1196–1198 ("headless … until sub-project 6's views draw them"), l.1914 ("Undo and
redo stay with sub-project 6"), l.2003–2005 ("headless" until sub-project 6), l.1998–1999 (Macro 7
"inert … until the resample source").
Fix: past tense, "since M2 sub-project 6's PR 2" (l.1914 points at l.2094); l.1998–1999 "acts on a
Live or Resample layer (§23 #68)". Resolution: Fixed in `47c6eac`.

**R-15** (A-18). §15's dependency table: l.1489 credits sst-basic-blocks with "LFOs, envelopes, mod
matrix" (in-house, `ModMatrix.h`, `ModSources.h`); l.1490 credits sst with the delay (in-house,
l.1280–1281); l.1474–1476 plans unpinned rows "for M2 and M3" (M2 added none); l.1607's in-house list
is short.
Fix: l.1489 "SIMD setup, `ParamMetaData` and the patched Reverb 2's helpers (lag, block interpolators, quadrature oscillator, block ops); the LFOs, envelopes and mod matrix are in-house (§14)" (checked against `core/src/space/Reverb2Patched.h` l.43–48); l.1490
"Filters (`CytomicSVF`), reverb (Reverb 2); drive later"; l.1474 "for M3 and later"; l.1607 adds the
mod matrix and sources, the delay and limiter, the Spectral Smear. Resolution: Fixed in `47c6eac`.

**R-16** (A-14, B-15). §16 l.1694–1700: the four-layer cases' ratios "not in the baseline … sub-project
7's idle run, which adds their baseline entries". l.1734–1736, #66 and the committed x86 baselines
(`630d740`, 30 ratios an entry) already hold them; `mac-universal.json` doesn't.
Fix: "their ratios have entries on the x86 legs since the AVX2 task, none on `mac-universal`; their
reference-core figures come from sub-project 7's idle run". Resolution: Fixed in `47c6eac`.

**R-17** (A-15). §16 l.1747: `engine-live`'s figure with the ring levels "is measured when they land".
They landed (sub-project 2's Task 12, #68).
Fix: "is measured at sub-project 7's idle run" (or the figure, once measured). Resolution: Fixed in `47c6eac`.

**R-18** (A-16). §16 l.1863–1864: "4-wide via sst and simde … Add xsimd or Highway runtime dispatch
only if profiling demands it". The 8-wide AVX2 kernels are kept, chosen at load (#66).
Fix: "4-wide via sst and simde, plus 8-wide AVX2 sample and live kernels chosen once at load by
CPUID, the same bits (§14, §23 #66); no third-party dispatch library". Resolution: Fixed in `47c6eac`.

**R-19** (A-23). §16 l.1796–1797 omits #75's `--rounds 1` for the quick smoke.
Fix: add "`--quick --rounds 1` (§23 #75)". Resolution: Fixed in `47c6eac`.

**R-20** (D-8). Undo of a preset load and Mutate: §17 l.2094–2098 lacks row 402's limits; presets
spec §6.9 l.582–583 and §9 l.835–837 say "sub-project 6's undo sees ordinary gestures"; views spec
§6.8 l.557–559 "this sub-project pushes none". As built, `EditorPlugin.cpp` l.369 and l.439 push one
whole-state entry per load and press (row 402); PR 2's batch replay and key-repeat merging (rows 323,
337) are unmarked in views §6.8.
Fix: §17 l.2096 adds "(values and routes; a recall's samples and name aren't restored, row 402)";
presets §6.9 and §9 "each press or load is one whole-state undo entry (views D7; row 402)"; views §6.8
gains **Amended** notes for `pushWholeState` and PR 2's replay and merging. Resolution: Fixed in `47c6eac` (the presets and views specs, outside INTENT: fixed in `6bdf1b1`).

**R-21** (D-9). The edited mark: §17 l.1896–1897 and presets §6.2 l.251–253 say any differing base
value marks a preset edited "from the 218 values"; presets §6.9 l.565 "a bitset over the 218 slots".
`GigaGrainsPlugin.cpp` l.1795–1801 skips the ring parameters; there are 283 slots.
Fix: "from the 283 values, the ring parameters aside (a load keeps them)" in §17 and presets §6.2;
"283 slots" at §6.9. Resolution: Fixed in `47c6eac` (the presets spec, outside INTENT: fixed in `6bdf1b1`).

**R-22** (A-22). §18's first-ten table l.2478: Position "FX: 0–16 s". Per layer kind since sub-project
1; Live reaches 300 s (`kPositionMax`, l.2665–2671).
Fix: "Live layer: 0–16 s in M1, 0–300 s since M2 sub-project 2 (`kPositionMax`); Sample layer: 0–100%
of the loaded file". Resolution: Fixed in `47c6eac`.

**R-23** (A-9). §18 l.2640–2644: "The patch stays at version 2" beside "They joined with the patch's
version bump"; l.2642 "Neither joins a remote-controls page before sub-project 6". Checked:
`kRemotePages` (`GigaGrainsPlugin.cpp` l.86–113) holds neither Quality nor Budget.
Fix: delete l.2643–2644's sub-bullet; l.2642 "Neither is on a remote-controls page (§17's
thirteen)". Resolution: Fixed in `47c6eac`.

**R-24** (A-8). §18 l.2764: "eight 8-slot pages". §17 l.2000, l.2802 and #69 say thirteen; checked
in `kRemotePages`: "FX" is page 8, "Filter A" to "Filter D" pages 9 to 12.
Fix: "thirteen 8-slot pages since M2 sub-project 4 (eight from sub-project 1, three in M1)", the list
extended with FX (8) and Filter A–D (9–12). Resolution: Fixed in `47c6eac`.

**R-25** (A-10). §19's golden counts: l.2939 "so there are 45"; l.2956 "the 36 earlier cases"; l.2960
"adds seven … so there are 51" (present tense, after sub-project 4's paragraph); l.2921 "is the 44th,
below" (nothing below); l.2854 "the 36 existing goldens". Merge order was 3, 2, 4: 36 + 7 + 1 = 44,
+ 7 = 51, + 9 = 60.
Fix: sub-project 2's paragraph before 4's, past tense; l.2939 "so there are 60"; l.2956 "the 51
earlier cases kept their WAV SHA-256 but `fx-layer-switch` and `fx-overload` (row 301)"; l.2921
describes `inst-sample-end` (#66) or drops "below"; l.2854 "(36 before main's merge, 43 after; §23
#70)". Resolution: Fixed in `47c6eac`.

**R-26** (A-11, B-12). §19 l.3039–3040: "20 `[sweep]` cases … about 26.5 s in `dev` and 194 s under
ASan". `tests/sweep/` holds 30 (`TEST_CASE` counts 19 + 5 + 3 + 2 + 1); CLAUDE.md says 30 but keeps
the 20-case timings (engine spec l.568).
Fix: "30 `[sweep]` cases (20 from sub-project 3, the rest from 2 and 4)"; re-time `ctest -L sweep` in
`dev` and `asan` and update §19 and CLAUDE.md. Resolution: Fixed in `47c6eac` (the 26.5 s and 194 s timings are labeled as timed over 20 cases; re-timing `ctest -L sweep` in `dev` and `asan` waits for a quiet machine, owed to the close's Task 19; CLAUDE.md, outside INTENT: fixed in `7178176`).

**R-27** (A-12, B-12). §19 l.3070–3071: "walks the 223 slots". `tests/ui/edit_layout_tests.cpp` l.49
walks `kParamCount`, 283.
Fix: "walks all 283 slots (`kParamCount`)". Resolution: Fixed in `47c6eac`.

**R-28** (B-12, A-23). §19 l.3091: "a PKG with one component per format (CLAP, VST3, AU, the two
apps)"; l.3099 lists the installers' licenses without CC0. §15 l.1582–1586, `docs/releasing.md` l.34
and #70 add the Content component and `CC0-1.0.txt`.
Fix: "(CLAP, VST3, AU, the two apps and, since M2 sub-project 5, the Content)"; add the CC0 text at
l.3099. Resolution: Fixed in `47c6eac`.

**R-29** (A-25). §19 l.3081: "**From M2 the sub-projects are run by agent sessions**". The director
decided on 2026-10-31 that M3 runs as M1 did (one session, sub-projects in series, parallel tasks
inside each; no workers, waves or program manager), recorded only in the program manager's notes.
Fix: "In M2"; a dated §23 row records the director's 2026-10-31 decision. Resolution: Fixed in `47c6eac` (§19; the row is §23 #87).

**R-30** (A-17). §21 l.3148: "8.0.16-security is the fallback". §15 l.1465–1466: "no longer a
fallback"; JUCE 9.0.4 since #58.
Fix: "M1 and M2 run on 9.0.4; 8.0.16-security is no longer a fallback (§15)". Resolution: Fixed in `47c6eac`.

**R-31** (A-19). The retired count rules still stand in §23 #50 (l.3234: "no net growth of open rows
over a sub-project; sub-project 6 burns down every open row"), #63 (l.3247: "the policy's five rules
… The no-net-growth count") and #64 (l.3248: "should end M2 at 8 open rows"). #67 retires them but
names only #60's and #64's targets.
Fix: annotate #50 and #63 "(retired by #67, 2026-10-30)" and #64 "(history since #67)"; #67 names
#50's policy (its no-upstream-filing rule stands), #63's five rules and count, #60's and #64's targets.
Resolution: Fixed in `47c6eac`.

**R-32** (A-26). §23 #68: "The presets owner's sign-off on SP-M8 is pending", and SP-M8's "Ring
Length stays unlocked". Superseded: #70 ruling (4) locks "the five ring parameters per layer"
(`core/include/gg/core/Mutate.h` l.41).
Fix: annotate SP-M8 "(signed off and extended by #70 ruling (4): the five ring parameters are
structural locks)". Resolution: Fixed in `47c6eac`.

**R-33** (D-7). §23 #69: "carry-over rows 378 and 379, closed as defects". Both rows: "**Closed: an
accepted law**".
Fix: "closed as accepted laws, not defects". Resolution: Fixed in `47c6eac`.

**R-34** (D-13, A-23). §23 #70: "v1 has no voices or strings" (v1's `Samples/Strings/` holds VCSL
folk strings, presets spec l.396); "rows 389 to 400" (presets §12 l.981–983: 389 to 401, then 402 at
the merge); its title says "group 1" though it holds group 2. Presets spec §11 l.942 still names a
"voice-and-strings phrase" (the pack uses `phrase-strings.json`).
Fix: "v1 has no voices and no orchestral strings (its Strings are folk instruments)"; "rows 389 to
401, and 402 at the merge with views' PR 2"; title "groups 1 and 2"; presets §11 **Amended:** a
strings phrase, since no content release has a voice. Resolution: Fixed in `47c6eac` (presets §11, outside INTENT: fixed in `6bdf1b1`).

**R-35** (D-11). §23 #71 ("333 and 338 open, and rows 383 to 387 added") and views spec §16
l.1212–1217. Row 338 is closed at `293cbb5`; row 388 exists; the carry-over header says 383 to 388.
Fix: "334 to 338 closed (338 at `293cbb5`); rows 383 to 388 added". Resolution: Fixed in `47c6eac` (views spec §16, outside INTENT: fixed in `6bdf1b1`).

**R-36** (A-23, B-15). Log hygiene: #48 and #49 end "pending the director's confirmation" after
recording it; #47 and #57 (1) say "sub-project 6" makes the gate blocking (amended by #60, #61, #62);
#57 (2)'s "Visual Studio generator has no compiler launcher" (reversed by #76); #73's "(§19)" for the
caches (they are in #57 (3) and CLAUDE.md "CI caches"); #69 has no "Decided … by …" line.
Fix: strike the two "pending" tails; annotate #47, #57 (1) "(amended by #60, #61, #62)" and #57 (2)
"(reversed by #76)"; #73 "(#57; CLAUDE.md, CI caches)"; give #69 its decided-by line (the director's
D1–D8, D7a, D-bound, D5b, D-glide, D1-rest, D-specsave, D-specwindow, D-corners; the program manager;
Claude). Resolution: Fixed in `47c6eac`.

### CLAUDE.md

**R-37** (B-1, D-18; seed). "Where we are" (l.23–44) covers sub-projects 1 to 5, not 6.
Fix: add "Sub-project 6, Views, is built (INTENT §23 #71; PRs #18 and #24): the Perform, Edit, Mod
and FX views, four lanes with a pitch axis, the layer strip and XY pad, the Mod view's route path,
undo, two themes, the About box, `gg-snap` and the GPU spike"; add #71 to l.23's citations. The close
updates it again at its end. Resolution: fixed in `7178176`.

**R-38** (C-5, A-24; seed). The model rule is recorded nowhere in the repository. Plan l.22 ("workers
and reviewers on the top tier (the default model), task implementers on Opus") and the brief
template l.151–152 ("never haiku or sonnet") contradict it; design §12 l.605–606 is vague; the presets
plan l.72 says "reviewers inherit the parent model". The rule: the director, 2026-10-30, as relayed
in the close's brief (`.superpowers/brief.md`, "Setup"; not in the repository): "NO FABLE, ever.
Name `model` on every dispatch … 'The most capable model' … means opus. Otherwise … haiku for
mechanical work, sonnet for integration work and as the reviewer floor."
Fix: CLAUDE.md "Working style" gains that rule; plan l.22 and l.151–152 state it; design §12 points
at it; presets plan l.72 is marked superseded. The sub-project plans' `Co-Authored-By: Claude Fable
5.1` lines stay as history. Resolution: fixed in `7178176` (CLAUDE.md), `32b9d79` (plan and design), `0115c8a` (the presets plan).

**R-39** (B-6). "Benchmarks": "seen so far … Windows on EPYC 7763, 9V45 and 9V74" omits the Xeon
8370C that the next lines and `windows-clangcl.json` hold.
Fix: "Windows on EPYC 7763, 9V45 and 9V74 and Intel Xeon 8370C". Resolution: fixed in `7178176`.

**R-40** (B-10). "Tests run in parallel" shows two `catch_discover_tests` calls; `tests/CMakeLists.txt`
l.198–205 has three (`~[serial]`, `[serial]~[crawl48]`, `[crawl48]` with `FIXTURES_REQUIRED
presets_built`).
Fix: show the three. Resolution: fixed in `7178176`.

### README.md

**R-41** (B-2). l.8–9: "**Status (2026-10-29):** … next comes M2."
Fix: re-date; M2 in progress, sub-projects 1 to 6 built, the close under way; the M5 sentence kept.
Resolution: fixed in `7178176` (the status paragraph).

**R-42** (B-3). l.19–21: `git submodule update --init` (CLAUDE.md and CI: `--recursive`); "Visual
Studio 2022 with ClangCL" (Ninja + clang-cl from an x64 developer shell since #76); "downloads the
VST3 and AudioUnit SDKs" (also RtAudio where needed; the first `ctest` fetches the content).
Fix: as CLAUDE.md "Setup" says; item 3's "Building from source" section replaces it. Resolution:
fixed in the close.

### Workflows, build files and other docs

**R-43** (B-4). `ci.yml` l.715: "sub-project 6 makes it blocking". #60, #61 and the file's own l.14–17:
at 1.0.0.
Fix: "it turns blocking at 1.0.0, after M5 (INTENT §23 #60, #61)". Resolution: fixed in `13199cd`.

**R-44** (B-9). `format.yml` l.5–6 lists "engine core plugin ui tools tests clients"; `scripts/format.sh`
l.17 adds `media` and `libs/thirdparty-impl`.
Fix: "the folders `scripts/format.sh` lists". Resolution: fixed in `13199cd`.

**R-45** (B-14). `CMakeLists.txt` l.54–55: "'-' signs ad hoc, locally and on every CI run but a tag's or
the nightly's". `ci.yml` l.455, l.539–541: a dry run with the secrets signs too; a nightly without them
signs ad hoc.
Fix: "'-' signs ad hoc: locally, and on every CI run that builds no installer or has no signing
secrets". Resolution: fixed in `13199cd`.

**R-46** (B-5). `tools/gg-bench/baselines/README.md` l.5–7: "Through M1 the verdict is report-only …
Sub-project 6 makes it blocking".
Fix: "report-only until 1.0.0 (INTENT §23 #60, #61) … turns blocking at 1.0.0, after M5".
Resolution: fixed in `13199cd`.

**R-47** (B-11). `docs/releasing.md` l.278: "Check the five choices"; l.34 and
`installer/macos/distribution.xml.in` have six (the Content).
Fix: "six". Resolution: fixed in `13199cd`.

### The program design and plan

**R-48** (seed). "programme" in the design and plan (file names and text) and elsewhere in
first-party text, against US spelling (the director, 2026-10-30).
Fix: the close's item 6, the US-spelling sweep (PR #22 taken over), renames the two files and fixes
every use. Resolution: fixed in the close.

**R-49** (C-13). Design l.5: "Not yet built: wave 1 starts with the programme plan".
Fix: "In progress: sub-projects 1 to 6 built and merged (§23 #65, #66, #68 to #71); sub-project 7 (the
close) running; amended as marked". Resolution: fixed in `32b9d79`.

**R-50** (C-4, D-6; seed). Count rules retired by #67 still stated: **design §6.7 exit l.381–382 "the
carry-over count is at or below its §8 target"**, l.377, I37 l.178, §3 l.100–101, §6 l.186–187,
§6.6 l.359–360, §12 l.611, §10 l.593; **plan Task 5 Step 5 l.285 "the carry-over at or below 8 open
rows"**, Task 6 Step 2 l.292, l.11, l.28, l.82, B9 l.89 (contradicts B6 l.86), l.117; layers spec §4
l.115 and §12 l.974–975; sources spec §4 l.117 and §12 l.1004–1005; extremes spec §12 l.944
("Expected at the end: 11 open rows").
Fix: design exit "the carry-over swept: every row closed or carrying the reason it waits, the
deferred items listed in the close's report (§23 #67)", l.377 and I37 to match; drop the count
clauses; plan l.285 "the carry-over swept (§23 #67)", l.292 "the open carry-over rows", B9 as B6;
the sub-project specs' forecasts marked "(the policy then in force; retired by §23 #67)", extremes
§12 with the rows it added (357 to 382). The close's spec replaces §6.7's wording. Resolution: fixed
in the close.

**R-51** (C-2). Merge order: design l.201–215 ("**Merge order within a wave is fixed** (3, then 2,
then 4; 5, then 6)"), l.271, l.295, l.317, l.339, l.363, l.590; plan l.18, l.264, l.276; #64 (4). The
director, 2026-10-30 (~23:45, in the program manager's notes only): "finished branches merge as
ready". On `main`: #17 and #18 (wave 3) before #19 and #20; #24 (6) before #25 (5). Presets' two-PR
split (#70) is unforeseen in design §6.5.
Fix: a dated §23 row records the decision, cited from #64; design and plan say "a wave's
sub-projects merge as each is ready, the later one merging `main` and re-verifying" (mechanism per
K-7) and give the order that happened; §6.5 notes presets' two PRs (#70). Resolution: the INTENT half (the dated row, §23 #88, cited from #64; §19) fixed in `47c6eac`; the design and plan: fixed in `32b9d79`.

**R-52** (C-8). Design I12 l.152 "(FLAC embed, `embed=1`)", §6.2 l.261–262 "FLAC through libFLAC", exit
l.276 "libFLAC is pinned". #68: float32 WAV payloads; libFLAC removed (§15 l.1476–1478, §18 l.2183).
Fix: "`AUDI` (float32 WAV payloads, `embed=1`; §23 #68)"; l.276 "no new codec dependency (float32,
§23 #68)". Resolution: fixed in `32b9d79`.

**R-53** (C-9). Rows 118 and 119 still placed in sub-project 2: design I9 l.149, §6.2 l.255–257, §8
l.559. Closed in sub-project 1 by `263696d` (#65).
Fix: I9 "1 (moved, §23 #65)"; §6.2 "I8, I10 to I13"; §8 "Sub-project 1 (moved 2026-10-29) | Closed
(`263696d`)". Resolution: fixed in `32b9d79`.

**R-54** (C-14). Design §7.7 l.502–503: "The limiter keeps M1's soft-limiter law as its default". As
built and in §14 l.1300–1301 and #69: Off by default.
Fix: "The global limiter is Off by default, the identity, with M1's per-layer soft limiters kept, so
no golden moves; Soft and Peak are opt-in (§23 #69)". Resolution: fixed in `32b9d79`.

**R-55** (C-15). Design §7.8 l.516–518: fetch "into `build/content`"; the installed Content "then" the
user folder. As built (`media/include/gg/media/ContentPaths.h` l.106–116): `GG_CONTENT_DIR`, the
user's `contentFolder`, the per-user install, the system install; `gg-content` fetches into
`build/<preset>/content-cache/content/`.
Fix: state the four roots in that order and the fetch paths (presets spec §6.4, §23 #70).
Resolution: fixed in `32b9d79`.

**R-56** (C-16). Design §6.1 l.223–224 ("XY morph … in the engine") and l.232 (`engine/` owns "the mod
matrix and its sources"). Layers spec §5 option 1: the `Processor` (`gg-core`) owns them.
Fix: "a mixer law in the `Processor` (`gg-core`)"; Owns "`engine/` (the per-layer Engine, the task
runner), `gg-core`'s mod matrix and sources" (layers spec §5). Resolution: fixed in `32b9d79`.

### Sub-project specs 1 to 3

**R-57** (C-17). Layers spec §10 l.885–886: "`static_assert(__cplusplus >= 202302L)` … checked by a
scratch-project test". LS-T1a (§6.1 l.182–187) and `tests/smoke_tests.cpp` l.35: `> 202002L`, no
scratch test.
Fix: the LS-T1a form; §10 in the status line. Resolution: fixed in `6bdf1b1`.

**R-58** (C-22). Layers spec LS-T13a l.692–693: "The instrument's `"sample"` stays required". SP-T8c
(#68) and #70 relax it beside `state` and `preset`.
Fix: append "(later optional beside `state`, SP-T8c, §23 #68, and beside a `preset` that names layer
A's, §23 #70)". Resolution: fixed in `6bdf1b1`.

**R-59** (C-18). Engine spec D3 l.705–708 ("the first image sits at least 70 dB down"), withdrawn at
stretch 0.5 by ES-T1a (§6.2 l.199–213; −37 dB, the sitting's question); §5 l.147–150 and §14 l.925
(`-mavx2`, `-mno-fma`, one pointer), superseded by ES-T12a and ES-T12b.
Fix: D3 gains "**Amended** (ES-T1a): at s = 1; near residual rate 2 about −37 dB, the sitting's
question"; §5 and l.925 marked "Amended (ES-T12a, ES-T12b)", pointing at §6.12. Resolution: fixed in
the close.

**R-60** (C-20). The output bound: engine spec §6.11 l.558 ("2 + the input's peak") and sources spec
§6.2 l.256 (`|out| ≤ 2 + |d|`). D-bound (the director, 2026-10-31; §14 l.1025): 3 + the input's peak
with the global delay engaged.
Fix: both add "(3 + |d| with the global delay engaged, D-bound, §23 #69)"; the tap's stability
argument states the larger bound. Resolution: fixed in `6bdf1b1`.

**R-61** (C-21). Sources spec §10 l.885–886: "the admission clamp and the fade use it; a grain past a
narrowed window fades to 0". SP-T12aa (§6.1 l.225–235): the window steers admission alone.
Fix: "the window steers admission; a grain admitted under a wider window finishes in its old span
(SP-T12aa)"; §10 in the status line. Resolution: fixed in `6bdf1b1`.

### Sub-project specs 4 to 6

**R-62** (D-6). Extremes spec §10 l.848: "(9 new, 45 in all …)"; §11 l.923: "Fourteen renders from
thirteen specs" (the pack and `specs/` have fifteen).
Fix: "60 cases on `main` after the merges (51 earlier, §23 #69)"; "Fifteen renders from fifteen
specs". (§12's forecast is R-50.) Resolution: fixed in `6bdf1b1`.

**R-63** (D-10). Views spec status l.5 and §6.9 l.608–611 wait on row 313 ("`ui/src/Theme.h` is back
as a shim … until PR 2's palette pass"). Row 313 is closed in full; `Theme.h` is gone; row 331 falls
to the close. §8 D8 l.862–868 doesn't point at #72; the carry-over header l.36 says "closed 313 and
330 in part".
Fix: status "row 313 closed by sub-project 5 group 2; row 331 is the close's"; §6.9 **Amended:**
`Theme.h` is gone; D8 adds "M5 adds System, high contrast and user themes (§23 #72)"; the header
l.36 says 313 closed. Resolution: fixed in `6bdf1b1`.

**R-64** (D-12). Views spec §6.7 l.538–540: "The view has no viewport". VW-T19b (l.1185) and VW-P2e
(l.525): the FX view scrolls in the shared panel grid.
Fix: **Amended (VW-T19b, VW-P2e)** at l.539 and after §6.5 l.411. Resolution: fixed in `6bdf1b1`.

### The listening packs

**R-65** (D-14). `docs/listening/m2-layers/README.md` l.76–80 ("layers B to D are silent unless
their Source is Live … until … MEDI v2") and l.87 ("the device's eight remote-control pages"). Per-layer
samples since #68 and views PR 2; thirteen pages since #69. Rendered before row 301's morph fix
(`5693fe9`), which touches `01`, `05` and `14`.
Fix: per-layer samples from the strip or a lane drop; "the first eight of the thirteen pages";
re-render on `main` before the sitting. Resolution: fixed in `13199cd` (text; the re-render before the sitting is Task 16's).

**R-66** (D-15). `m2-engine` l.60 (`07`: "every grain's line reads `"lvl":0` … −76.6 dB"), stale
since the ring pyramids (`188c436`); `04`, `05` figures predate the raised-cosine steal fade
(`65292dc`, EF-8); `06` predates row 301; row 308's items open.
Fix: re-render `04`–`07` on `main`, re-measure, rewrite `07` for level 1, fold in row 308, name the
commit the figures come from. Resolution: Task 16's (the re-renders and re-measuring; text not yet changed).

**R-67** (D-16). `m2-sources` l.75–76 ("through sub-project 6's readout once it lands"): built in
views PR 2. l.73–74 sets Ring Length "from the generic editor", which some hosts may hide for a
non-automatable parameter. Rows 385 and 386 bear on these checks and aren't named.
Fix: "(the sample strip's memory readout, views PR 2)"; "from the Edit view's Ring panel (or the
generic editor, where the host shows it)"; name rows 385 and 386. Resolution: fixed in `13199cd`.

**R-68** (D-17). Items sent to "the director's checklist" are in no pack: the pad double-click's two
undo entries (views spec l.336–339, #71), row 333, row 402, row 387, rows 396 and 397.
Fix: into the close's merged gate checklist (the views items there; 396 and 397 with the presets;
402 cross-referenced). Resolution: Task 16's (the merged gate checklist).

### The carry-over

**R-69** (B-13). `docs/m2-carry-over.md` l.24–25: rows 233 and 234 were "numbered before the rule
above (no M1 row has either number)". M1 had a row 233, the AUv2 order (§23 #60, "carry-over row
233 … (185 and 233 closed)"); M1's file numbers rows by line, so the token is absent.
Fix: "(M1's row 233, the AUv2 order, closed at M1's close, §23 #60; these name the M2 rows)";
CLAUDE.md's validator note says "M2 carry-over row 233". Resolution: fixed in `0fd8ae9` (the carry-over) and `7178176` (CLAUDE.md).

---

## Dropped or reclassified

- **B-7, B-8** (raised as DRIFT): code and CI configuration, so **DEFECT** (X-1, X-2). B-8's text-only
  option is kept as X-2's fallback.
- **A-26** (UNSURE): #70 ruling (4) locks the five ring parameters, which settles and extends SP-M8;
  **DRIFT** (R-32).
- **A-25** (UNSURE): the M3 process is a director decision of 2026-10-31; **DRIFT** (R-29).
- **A-24** (UNSURE, "not in INTENT"): merged into R-38, the model rule.
- **C-25** (UNSURE): merged into R-7; the 65 MB has no itemized source, so the figure is recomputed.
- **A-9's unsure part:** checked; no remote-controls page carries Quality or Budget.
- **B-12's timings, D-4's "15 to 18", D-16's generic editor** (UNSURE points): kept as DRIFT with a
  re-measure or a hedged wording (R-26, R-11, R-67).
- **B-13** offered renumbering rows 233 and 234: not needed, and it would rewrite §23 rows; R-69
  keeps the numbers.
- **A-1** kept as CONFLICT (K-4), though both sides are the director's: D4 didn't amend Part I, and
  only the director can say how Part I reads.
- **A-3** kept as CONFLICT (K-6), not drift: D1 was made on Part II, and any fix touches Part I.
- **C-2** kept as DRIFT, not CONFLICT: the director's own later call retired the fixed order; only its
  record is missing. Its mechanism half is K-7.
- Nothing was dropped outright.

## Checked and consistent

- **Counts:** 283 parameters (4 × 62 + 35; 218 → 223 → 243 → 283 across #65, #66, #68, #69); 48 mod
  slots, twelve factory routes; pool 4,096 a layer, 16,384 ceiling; Budget 256–16,384, default 4,096;
  rings 1–300 s; memory cap 1 GiB (64 MiB–16 GiB); embed cap 32 MiB; state cap 64 MiB; 24 presets a
  product, 8 `midi`; content v2 51 files; goldens 60 + 48 = 108; Mutate's 62 locks; 30 sweep cases;
  63 bench configurations; 11 `slow` tests.
- **Decisions carried through:** the CPU gate report-only until 1.0.0 (§16, §19, §20, #60–#62, CLAUDE.md;
  except R-43, R-46); 0.2.0–0.4.0 unpublished drafts, 0.5.0 betas first public, freezes at
  `v0.5.0-beta.1` and preset order at 1.0.0; X = 19.5% and later idle runs within 3%; §7.1's ID
  blocks; one owner per state change (patch v2, container v1, chunks from sub-project 2 alone); the
  tap's 256-sample latency; Quality tiers and D5's HQ-only pyramid; the morph law with D1-rest; Feedback
  0–0.95; extremes D1–D8, D7a, D-bound, D-specsave, D-corners; presets D1, D-Q2, D-Q3, `brass`,
  content v2 a superset of v1; views D1–D12 and #72; #73–#76 in §15, §16 and CLAUDE.md (but R-19).
- **Records:** §22's resolved questions struck and its open ones unanswered by any row; the log's
  supersession links (#23→#39, #18→#24, #36→#45, #49→#53, #44→#60, #61→#62, #57→#73, #40→#68), gaps
  in R-36; each spec's status table lists its amendments but those named above; the carry-over
  header states #67's retirement; the `header.preset` slot agrees across presets, views and §17;
  the extremes pack's `11` matches D7a; the sources pack's figures postdate the engine follow-up.
