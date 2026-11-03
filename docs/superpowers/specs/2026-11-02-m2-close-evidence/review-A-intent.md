# Review A: INTENT.md, internally

Read: all of `INTENT.md` (3,283 lines) at `0189dc9` on `m2-close`: Part I §1–§13, Part II §14–§22 and every §23 row #1–#76. I spot-checked against code or specs only where a finding needed it: `ModMatrix.h` `isValidTarget`, `ui/include/gg/ui/PadModel.h`, the engine spec §6.5 and D4, the presets spec §6.2 and D8, the `tests/sweep/` and golden case counts, and `tests/ui/edit_layout_tests.cpp`. Line numbers are `INTENT.md`'s unless another file is named.

Totals: 3 CONFLICT, 20 DRIFT, 3 UNSURE.

---

## Conflicts

### A-1. The overload ladder's order: Part I says drop, then steal; as built, the steal is tried first
- **Class:** CONFLICT
- **Sources:**
  - Part I §8.4, l.118–120: "(§14: a lower interpolation tier first, then dropped new grains, then stolen grains with fades)". Signed off in #61 ("§8 ... with the director's sign-off: overload order").
  - §14 hard rules, l.664–666: "1. lower the interpolation tier for new grains; 2. drop new grains; 3. steal existing grains with pre-emptive fades." CLAUDE.md's non-negotiables give the same order.
  - §14 "As built", l.685–688: "else lower the tier ...; else, if no tier fits or the pool is full, **steal** the oldest sounding grain if it has sounded at least half its window (D4) ..., and otherwise **drop** the new grain".
  - l.695–696: "It mostly steals and re-admits at the draw's tier, so lowering is rare". #66 ES-T10a says the same.
  - The engine spec §6.5 (l.365, 389–393) reads the order "by the threshold": new grains are dropped while the oldest grain is under half its window, and stolen once it is past. It claims "the order is §14's". D4 (#66) chose the threshold.
- **The disagreement:** for each grain, the code tests the steal before the drop. The two texts agree only if Part I's "then" is read as escalating with load, not as the order of the tests. Nothing in §14 or §23 states that reading. A reader of §8.4 or CLAUDE.md expects drop to come before steal. A reader of l.685–688 sees the reverse.
- **Resolution (forced choice):**
  1. **(Recommended)** Keep Part I unchanged. Rewrite §14's numbered list and l.685–688 to give the escalation by the oldest grain's age: "lower the tier; while the oldest sounding grain is under half its window, drop the new grain; once it is past, steal it with a fade". Add a §23 row saying this is how §8.4's order is read (D4). *Cost:* one Part II edit and a row; no code change, no Part I edit.
  2. Amend Part I §8.4 and CLAUDE.md to the as-built test order: "lower the tier, then steal the oldest grain past half its window, with a fade, else drop". *Cost:* a Part I change needing the director's sign-off, and a non-negotiable's wording changes.

### A-2. INIT keeps Ring Length: a controller's ruling bends the director's D8 ("a fresh instance, bit for bit")
- **Class:** CONFLICT
- **Sources:**
  - §18 l.2371–2374: "INIT (D8) is the product's defaults ... It is a fresh instance but for each layer's Ring Length, which it keeps ... (amended 2026-11-02 ...)".
  - #70, group 2 ruling (2): "INIT keeps each layer's Ring Length alone (amended 2026-11-02 by the final review's fix wave: first ruled to keep all five ring parameters as a preset load does, which bent D8, 'INIT is a fresh instance, bit for bit' ...)". The row's "Decided by" names the program manager for "the ring rulings".
  - Presets spec D8, l.793–795: "1. **A fresh instance, bit for bit:** every default, the factory routes, nothing else. **Decided: option 1.**"
  - The same spec, l.240: "**Amended 2026-11-02 (the final review's fix wave; the controller's ruling, D8)**".
- **The disagreement:** the director decided that INIT is bit for bit a fresh instance. The controller's ruling keeps a non-default Ring Length. No director sign-off is recorded.
- **Resolution (forced choice):**
  1. **(Recommended)** The director confirms the Ring Length exception as an amendment of D8, recorded in §23 #70 and in §18. *Cost:* INIT and a fresh instance differ whenever a ring isn't 16 s, so "INIT means the defaults" has one asterisk.
  2. INIT resets Ring Length to 16 s as well, as D8 says. *Cost:* loading INIT resizes every ring on the main thread. It can be refused by the memory cap. A 300 s held ring shrinks to 16 s and loses its older audio.

### A-3. Part I §5 promises live buffers "saved with the session"; D1 saves only held rings
- **Class:** CONFLICT (low severity)
- **Sources:**
  - Part I §5, l.79–80: "minutes-long *live* buffers inside a plugin, saved with the session and managed with splices".
  - §18 l.2236–2238: "A ring is saved when it is held ... A plain live ring refills from the input and isn't saved". l.2238–2244 adds that only a layer whose base Source builds to Live (Live or Resample) saves its ring, and a Spectral layer only when it has no sample.
  - #68 D1, the director's, made on Part II alone.
- **The disagreement:** Part I's sentence, read plainly, covers every live buffer. The director's D1 narrowed it to held rings, and Part I was never touched.
- **Resolution (forced choice):**
  1. **(Recommended)** Amend Part I §5 with the director's sign-off: "saved with the session when held (frozen or looped)". *Cost:* a one-clause Part I edit.
  2. Leave Part I as it is, and add to §18 a sentence that reads §5 as the held case, citing D1. *Cost:* Part I keeps over-promising to anyone who reads only Part I.

---

## Drift

### A-4. The header's status line is stale
- **Class:** DRIFT
- **Source:** l.7. Sub-projects 1, 3, 2 and 4 are each "(PR #NN, in review ...)". Sub-projects 5 and 6 are missing.
- **Facts:**
  - §20 l.3124 has 1–4 merged.
  - CLAUDE.md has 5 merged (PRs #17, #25) and 6's PR 2 merged (PR #24).
- **Fix:** rewrite l.7's M2 clause along these lines: "M2 (Giga) in progress since 2026-10-29 ...; sub-projects 1–6 built and merged (§23 #65, #66, #68, #69, #70, #71; PRs #15, #16 and #23, #19, #20, #17 and #25, and the views' PRs ending with #24); sub-project 7 (Close) under way". This matches CLAUDE.md's "Where we are".

### A-5. §20's M2 row lags sub-projects 5 and 6
- **Class:** DRIFT
- **Source:** l.3124: "**5 Presets and content** (group 1 built 2026-10-31, PR #17; group 2 ... built 2026-11-01, PR #25; §23 #70), **6 Views** (PR 1 built; §23 #71)".
- **Facts:** §23 #71 records PR 2, built 2026-11-01. Both of 5's PRs are merged.
- **Fix:** "5 Presets and content (group 1, PR #17, and group 2, PR #25, merged; §23 #70), 6 Views (PRs 1 and 2 built and merged; §23 #71)".

### A-6. The morph pad's corners: §14 has them upside down
- **Class:** DRIFT
- **Sources:**
  - §14 l.326: "(A top left, B top right, C bottom left, D bottom right)".
  - §17 l.2067: "**The XY pad:** A bottom-left, B bottom-right, C top-left, D top-right, Y up".
  - The views spec l.331–332 and l.924 agree with §17, as does `ui/include/gg/ui/PadModel.h:17–19`: "A bottom-left (X 0, Y 0) ... C top-left (0, 1)".
- **Fix:** at l.326, "(with Y up: A bottom left, B bottom right, C top left, D top right; the views spec's pad, §17)". The weights are unchanged, so no formula moves.

### A-7. Ring Length as a mod target: four places still say it is admitted, or that this is unsettled
- **Class:** DRIFT
- **Sources:**
  - l.474–476: "`isValidTarget` admits every layer parameter but the modulators' controls".
  - l.871–872: "the current mod-target rule still admits it as a route's target, until sub-project 3's rule".
  - l.1277–1279: "the matrix refuses a layer parameter only if it is a modulator's control".
  - §18 l.2655: "the current mod-target rule still admits it until sub-project 3's rule".
  - §18 l.2661–2664: "whether Ring Length is refused as a target follows sub-project 3's target rule ..., checked after the rebase onto it".
- **Decided:** #68 SP-T12c: "Ring Length is not a mod-matrix target (`isValidTarget` refuses it ...), sub-project 3's test that admitted it corrected". The code agrees: `core/include/gg/core/ModMatrix.h:143–144` has `ref->param != LayerParam::RingLength`.
- **Fix:** at all five places, say "every layer parameter but the modulators' controls and Ring Length (ruling SP-T12c, §23 #68)". At l.871–872 and l.2655, replace the parenthesis with "and `isValidTarget` refuses it as a route's target (SP-T12c)". Delete l.2661–2664's "whether ... checked after the rebase" clause.

### A-8. §18 says eight remote-controls pages; there are thirteen
- **Class:** DRIFT
- **Sources:**
  - §18 l.2764: "**Remote controls:** eight 8-slot pages since M2 sub-project 1 (three in M1)". The page list ends at "Cloud D".
  - §17 l.2000, §18 l.2802 ("thirteen pages, above") and #69 ("thirteen remote pages") say thirteen.
- **Fix:** at l.2764, "thirteen 8-slot pages since M2 sub-project 4 (eight from sub-project 1, three in M1)". Add the "FX" page (the delay and the limiter) and "Filter A" to "Filter D" (a layer's Smear, Bus Split and two bus filters) to the list, with their page IDs (8 to 12, if that is what was built; verify against the code). Note that l.2802 says "above", which then holds.

### A-9. §18 contradicts itself on Quality and Budget and the patch version; one forward-looking clause dangles
- **Class:** DRIFT
- **Sources:**
  - l.2640–2641: "The patch stays at version 2: a state without them loads the defaults".
  - The next sub-bullet, l.2643–2644: "They joined with the patch's version bump (The patch, below)".
  - l.2642: "Neither joins a remote-controls page before sub-project 6." No row records sub-project 6 adding them, and the thirteen pages (l.2000–2003) don't include them.
- **Fix:** delete the l.2643–2644 sub-bullet, or change it to "They joined after sub-project 1's version bump, with none of their own". Replace l.2642's sentence with "Neither is on a remote-controls page (§17's thirteen)". That second part is UNSURE: verify that no page carries them.

### A-10. §19's running golden counts disagree with the merge order and with the 60 cases
- **Class:** DRIFT
- **Sources:**
  - l.2939: "M2 sub-project 4 added nine cases (§23 #69), so there are 45".
  - l.2956: "Every one of the 36 earlier cases kept its WAV SHA-256".
  - l.2960: "M2 sub-project 2 adds seven cases ..., so there are 51" (present tense, "through every task so far").
  - l.2921: "(the follow-up's `inst-sample-end` is the 44th, below)". Nothing below describes it; it appears only in #66.
  - l.2854: "the 36 existing goldens' WAV hashes stayed unchanged through group 1". #70 says "36 before main's merge, all 43 equal to main's after".
- **Facts:** the merge order was 3, 2, 4 (#64). 22 + 14 = 36; + 7 + 1 = 44; + 7 = 51; + 9 = **60** (l.2855 and #69's merges: "Of main's 51 golden cases ...").
- **Fix:**
  - Move the sub-project 2 paragraph before sub-project 4's and change "adds" to "added".
  - At l.2939, "so there are 60". At l.2956, "every one of the 51 earlier cases (main's at the merge, §23 #69) kept its WAV SHA-256 but `fx-layer-switch` and `fx-overload`, which row 301's morph fix moved".
  - At l.2921, add a sentence describing `inst-sample-end` (the 2 s 660 Hz sine at Position 0.97; the edge fade, 1.94 against 36.03; #66), or drop "below".
  - At l.2854, "(36 before main's merge, 43 after; §23 #70)".

### A-11. The sweep harness: 20 cases in §19; there are 30
- **Class:** DRIFT
- **Source:** l.3039–3040: "20 `[sweep]` cases in `tests/sweep/`".
- **Facts:**
  - `tests/sweep/` holds 30 `TEST_CASE`s. CLAUDE.md says "30 cases".
  - Sub-project 4 added the limiter and delay exemption cases (#69's `kLimiterDrive`, `kDelayGlideHold`), among others.
  - #66's "family of 20 cases" is history and stays.
- **Fix:** "30 `[sweep]` cases in `tests/sweep/` (20 from sub-project 3; sub-projects 2 and 4 added the rest)". Confirm the split before writing it.

### A-12. "Every parameter is reachable": §19 says 223 slots; the test walks 283
- **Class:** DRIFT
- **Sources:**
  - l.3070–3071: "a `[ui]` test walks the 223 slots (with sub-project 3's Quality and Budget)".
  - `tests/ui/edit_layout_tests.cpp:49` loops over `kParamCount`, which is 283 (§18 l.2674).
  - PR 2 placed wave 2's parameters (l.2041–2042, l.2126 "Every parameter is still placed once").
- **Fix:** "walks all 283 slots (`kParamCount`)".

### A-13. The per-instance memory figure (about 51 MB) predates the ring levels and sub-project 4
- **Class:** DRIFT
- **Sources:**
  - l.226: "An instance holds about 51 MB".
  - l.294–295: "about 51 MB per instance: four rings about 25 MB ...".
  - l.837–838: "about 51 MB at 48 kHz since sub-project 3".
  - Against them, l.854: "a 16 s stereo ring is 6 MB, 11.5 MB with its three levels". So four default rings are about 46 MB, not 25. l.299–301: "Sub-project 4 adds about 8.4 MB of fixed cost".
- **Fix:** re-measure or recompute. By the document's own figures it is about 46 + 5 + 14.5 + 6.3 + 8.4 ≈ 80 MB, but I am UNSURE of the exact total. State one figure at l.294–295, with its parts (rings with levels, pools, reverb, event buffers, sub-project 4's Spectral and delay buffers). Have l.226 and l.837 point to it instead of repeating "51 MB". Note at l.837 that the ring share is counted against the cap and the rest is fixed.

### A-14. §16 contradicts itself on the four-layer cases' baseline entries
- **Class:** DRIFT
- **Sources:**
  - l.1699–1700: "their ratios 'not in the baseline'; their figures are measured at sub-project 7's idle run, which adds their baseline entries".
  - l.1735–1736 and #66: "the six report-only cases now have entries beside the four gated ones on those legs (not on `mac-universal`)". The six include `engine-layers` and `engine-layers-full` (CLAUDE.md "Benchmarks").
- **Fix:** at l.1699–1700, "their ratios have baseline entries on the x86 legs since sub-project 3's AVX2 task (below), none on `mac-universal`; their M5 Max figures are measured at sub-project 7's idle run".

### A-15. §16: "engine-live's figure with the ring pyramid's levels is measured when they land"; they have landed
- **Class:** DRIFT
- **Source:** l.1747. The levels landed in sub-project 2's Task 12 (2026-10-30; l.721 onward, #68). No later figure is recorded.
- **Fix:** "is measured at sub-project 7's idle run", or give the figure if one exists.

### A-16. §16's SIMD line still says 4-wide only, with dispatch only "if profiling demands it"
- **Class:** DRIFT
- **Source:** l.1863–1864: "**SIMD:** 4-wide via sst and simde, behind an internal batch API. Add xsimd or Highway runtime dispatch only if profiling demands it." The AVX2 work kept 8-wide kernels chosen at load from CPUID (l.247–254, l.1731–1738, #66 "Kept").
- **Fix:** "4-wide via sst and simde, plus 8-wide AVX2 sample and live kernels chosen once at load by CPUID, rendering the same bits (§14, §23 #66); no third-party dispatch library".

### A-17. §21 still names JUCE 8.0.16-security as the fallback
- **Class:** DRIFT
- **Sources:** §21 l.3148: "8.0.16-security is the fallback". Against it, §15 l.1465–1466: "8.0.16-security, Shortcircuit XT's version, is no longer a fallback". M1's editor is built on JUCE 9 (#58).
- **Fix:** at l.3148, "M1 and M2 run on 9.0.4; 8.0.16-security is no longer a fallback (§15)".

### A-18. §15's dependency table credits sst with what we built in-house
- **Class:** DRIFT
- **Sources:**
  - l.1489, sst-basic-blocks: "SIMD wrappers, sinc tables, LFOs, envelopes, mod matrix". The mod matrix and its sources are `gg/core/ModMatrix.h` and `ModSources.h`, in-house and libm-free (l.448–469). First-party code includes only `sst/basic-blocks/params/ParamMetadata.h` and `simd/setup.h`.
  - l.1490, sst-filters, sst-waveshapers, sst-effects: "Filters, drive, reverb and delay". The global delay is "in-house and libm-free" (l.1280–1281).
  - l.1474–1476: the unpinned rows "are planned, for M2 and M3". M2 added none of them.
  - l.1607: "Written in-house: the scheduler, grain pool, kernels and buffers".
- **Fix:**
  - At l.1489, "SIMD setup and `ParamMetaData` (the parameter metadata); LFOs and the mod matrix are in-house (§14)".
  - At l.1490, "Filters (`CytomicSVF`), reverb (Reverb 2); drive later (sst-waveshapers)".
  - At l.1474–1476, "planned, for M3 and later".
  - At l.1607, add "the mod matrix and its sources, the global delay and limiter, the Spectral Smear".

### A-19. Seed (a): the carry-over count rules
- **Class:** DRIFT (confirmed; located)
- **Sources:**
  - Part I and Part II's body hold no count rule. I grepped "net growth", "when touched", "burns down", "open rows" and "count rule" before l.3175.
  - Three log rows still state the rules, with no forward link to #67:
    - #50 (l.3234): "**The carry-over policy** (the director, 2026-10-28; CLAUDE.md, "Working style"): fix, don't carry, every minor ...; a sub-project that edits a file closes every open "when touched" row for it; no net growth of open rows over a sub-project; sub-project 6 burns down every open row".
    - #63 (l.3247): "that file repeats the policy's five rules unchanged ... The no-net-growth count is that file's open rows, 13 at the split".
    - #64 (l.3248): "The carry-over should end M2 at 8 open rows".
  - #67 (l.3251) retires them but names only "#60's and #64's carry-over targets are history". It doesn't name #50's policy or #63's rules, and #50 still cites CLAUDE.md "Working style" as their home.
- **Fix:**
  - Append "(retired by #67, 2026-10-30)" after #50's policy clause and after #63's "five rules ... no-net-growth count" sentence. Append "(history since #67)" to #64's carry-over sentence.
  - Extend #67 to "#50's policy (but its no-upstream-filing rule, which stands), #63's five rules and no-net-growth count, and #60's and #64's targets are history".

### A-20. Space's Size click still points at the carry-over; row 217 is closed
- **Class:** DRIFT
- **Sources:** l.1074–1076: "Accepted: Size is "set, don't sweep" (`docs/m2-carry-over.md`)." Against it, §17 l.2092–2093: "(row 217, closed as decided)", and #71 D10: "rows 217 (`0d8969c`) ... closed".
- **Fix:** "Accepted: Size is "set, don't sweep"; the FX view's knob sets on release (D10, §17, §23 #71; row 217 closed). Automation still clicks."

### A-21. Clauses that wait for work that is now built
- **Class:** DRIFT (each low)
- **Sources:**
  - l.821–822: "shown in the UI by sub-project 6". Built (PR 2's memory readout, l.2135).
  - l.1196–1198: "headless like wave 1's ... until sub-project 6's views draw them". Drawn (PR 2, l.2122–2125).
  - l.1914: "Undo and redo stay with sub-project 6." Built (l.2094–2098).
  - l.1998–1999: Macro 7 "is inert on a Sample layer, the default, until the resample source". Resample is built (l.400, #68).
  - l.2003–2005: "The Edit view's Filter panel and the FX view draw these parameters in sub-project 6; until then they are headless." Drawn.
  - §21 l.3144: "shown in the UI by sub-project 6". Built.
- **Fix:** change each "until/by sub-project 6" to the past tense ("since M2 sub-project 6's PR 2"). At l.1998–1999, "...and acts on a Live or Resample layer (§23 #68)".

### A-22. §18's first-ten table gives Position as "FX: 0–16 s"; it reaches 300 s
- **Class:** DRIFT
- **Sources:** l.2478: "`0x010001` | Position | FX: 0–16 s behind the write head; instrument: 0–100% of the loaded file". Since sub-project 1 it is per layer kind (l.413). Since sub-project 2 the Live range reaches 300 s at `kPositionMax` (l.2665–2671).
- **Fix:** "Live layer: 0–16 s at M1's maximum, 0–300 s since M2 sub-project 2 (`kPositionMax`, below); Sample layer: 0–100% of the loaded file". The defaults go per product, as l.2496 says for the default.

### A-23. Log hygiene: stale "pending" tails, missing links and labels
- **Class:** DRIFT (log rows; each low)
- **Sources:**
  - #48 (l.3232) ends "R18 pending the director's confirmation", but earlier says "the director confirmed it on 2026-10-28".
  - #49 (l.3233) ends "R19 pending the director's confirmation", but opens "confirmed by the director with R18 on 2026-10-28".
  - #44 (l.3228) and #47 (l.3231) say "sub-project 6 makes the gate blocking", and #57 (1) says "the blocking gate that sub-project 6 adds". #60 amends #44 by name, and #61/#62 move the gate to 1.0.0, but #47 and #57 have no forward pointer.
  - #57 (2) (l.3241): "Windows' Visual Studio generator has no compiler launcher". #76 reverses it but cites only #75.
  - #73 (l.3257): "the caches are still saved by pushes to `main` (§19)". §19 says nothing about caches; the cache rules are in CLAUDE.md.
  - #69 (l.3253) has no closing "Decided ... by ...; written by ..." line, though the preamble (l.3180–3181) says each row "says when, and by whom".
  - #70's title (l.3254) says "group 1", but the row holds group 2 too.
  - §16 l.1796–1797 omits #75's `--rounds 1` for the quick smoke.
  - §19 l.3099–3100 lists the installers' licenses without CC0, which §15 l.1585–1586 adds (`CC0-1.0.txt`).
- **Fix:**
  - Strike the "pending" tails in #48 and #49.
  - Add "(amended by #60, #61, #62)" to #47's and #57 (1)'s "sub-project 6" clauses, and "(reversed by #76)" to #57 (2).
  - Point #73's "(§19)" at CLAUDE.md's "CI caches", or drop it.
  - Give #69 its "Decided 2026-10-30 to 2026-11-01 by the director (D1–D8, D7a, D-bound, D5b, D-glide, D1-rest, D-specsave, D-specwindow, D-corners), the program manager and Claude; written by ..." line.
  - Retitle #70 to "groups 1 and 2".
  - Add "`--rounds 1`, #75" at §16 l.1797, and "the CC0 text (§15)" at §19 l.3099.

---

## Unsure

### A-24. Seed (b): model-tier text is not in INTENT.md
- **Class:** UNSURE (not found)
- **Sources:** a grep of INTENT.md for Fable, Opus, Sonnet, Haiku, "implementer", "model tier" and "subagent" finds no tier rule. The only model mention is #66 l.3250: "The fix's review (opus) approved it", which agrees with the rule (NO FABLE; opus at the top).
- **Resolution:** nothing to fix in INTENT.md. If the seed's text exists, it is in another file (the program design, plans, CLAUDE.md or the memory files), which belong to the other reviewers' families.

### A-25. M3's working process isn't recorded; §19's M2 process reads as standing
- **Class:** UNSURE
- **Sources:**
  - §19 l.3081–3085: "**From M2 the sub-projects are run by agent sessions** (§23 #64): a program-manager session launches one worker session per sub-project ...".
  - The director's 2026-10-31 decision that M3 returns to M1's way (one session, serial sub-projects, parallel tasks inside each; no workers, waves or program manager) is in the session memory (`memory/m3-process.md`), not in any repository document. That note says to record it in §23 when M3's plan is approved.
- **Resolution:** UNSURE whether it should be recorded now. At minimum, change l.3081's "From M2" to "In M2", so the text doesn't contradict a decision the director has already made. Alternatively, add a §23 row now: "M3 is built as M1 was (the director, 2026-10-31)".

### A-26. #68's SP-M8 "sign-off pending"
- **Class:** UNSURE
- **Sources:** #68, l.3252: "The presets owner's sign-off on SP-M8 is pending." #70 group 2's ruling (4), and §17 l.1916–1922 ("Ring Length, which no press moves, dimmed and with no toggle"), act on SP-M8 as settled.
- **Resolution:** if group 2's ruling (4) is the sign-off, append "(signed off by group 2's ruling (4), §23 #70)" to SP-M8. If not, it is an open item for sub-project 7.

---

## Checked and consistent

- **Parameter totals:** 218 → 223 → 243 → 283, and 48, 49, 54 and 62 per layer (§18 l.2563–2675, #65, #66, #68, #69's 258 → 263 → 283 progression). The arithmetic holds: 4 × 62 + 35 globals = 283. "Forty" (§14 l.1195) agrees.
- **Fixed counts:**
  - Mod slots: 48 (§14 l.450, §17 l.2077, §18 l.2740).
  - Pool ceiling: 16,384 (§14 l.221, Budget's top l.676 and l.2638).
  - Budget: 256–16,384, default 4,096 (§14, §18, #66).
  - Ring: 1–300 s, Loop Length 50 ms–300 s (§14, §18, §21, #68).
  - Memory cap: 1 GiB, 64 MiB to 16 GiB (§14, §18, §21).
  - Embed cap: 32 MiB (§18, §21).
  - State cap: 64 MiB.
- **Content and goldens:**
  - Presets: 24 per product, 48 in all, 8 MIDI-driven.
  - Content v2: 51 files, 46.0 MiB, under Part I §12's 20–50 MB.
  - Goldens: 60 + 48 = 108 (§19 l.2855–2856).
  - Mutate's 62 structural locks: 33 from group 1 plus 29 from group 2.
- **The CPU gate and X:**
  - The gate is report-only until 1.0.0 in §16 l.1808–1811, §19 l.3043–3046, §20 and #60–#62.
  - X = 19.5%: §16, §20 and the later idle-run checks (#66 +1.4/+0.8/+1.3%, #69 ±0.4% A/B, both inside R3's 3%).
- **Releases and freezes:** 0.2.0, 0.3.0 and 0.4.0 are unpublished drafts; the 0.5.0 betas are the first public releases; v1.0.0 follows M5. This is consistent across §18 l.2403–2411, §19 l.3108–3116, §20, #61 and #62. The freeze points (IDs, AU order and identities at `v0.5.0-beta.1`; preset order at 1.0.0) agree.
- **§22:** every question a §23 row answered is struck through and marked resolved (identities, JUCE, Ableton MIDI, the canvas and GPU, buses vs per-grain filters, C++23, AVX2, Windows signing, ASIO). The open rows (the thread-pool survey, the DC blocker, Spectral feedback, Logic MPE, the transport-stop policy, Nimbus, grain chunking, the spread model) have no answering row.
- **Part I:** §2 (one state format), §8.6 (determinism), §8.8, §10.5 and §12 (license list, CC0 content as a release asset) agree with Part II and the log, apart from A-1 and A-3. #39 and #61 record the director's Part I sign-offs.
- **The log's supersession links:** #23→#39, #18→#24, #36→#45, #49→#53, #44→#60, #61→#62, #57→#73 and #40→#68 are each stated in the later row (gaps in A-23).
- **Later decisions reflected in Part II:**
  - #72 (themes in M5): §17 l.1933–1934 and §20 M5.
  - #73–#76 (CI): §15 l.1393–1395 for Ninja, §16 l.1795–1797 for quick on `main`, except A-23's `--rounds 1`.
  - #71's D1 and D2: §17 l.1874–1877.
  - #69's D-bound: §14 l.1025.
  - #66's D1–D5: §14.
