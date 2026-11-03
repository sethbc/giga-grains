# Review D: specs 4–6 and the six M2 listening packs

**Read** (worktree `m2-close` = `main` at `0189dc9`, 2026-11-02): `docs/superpowers/specs/2026-10-30-m2-extremes-design.md` (all 1,010 lines), `2026-10-30-m2-presets-design.md` (all), `2026-10-30-m2-views-design.md` (all), `docs/superpowers/plans/2026-11-01-m2-views-pr2.md` (the rulings table and Tasks 3 and 9); `docs/listening/m2-{layers,engine,sources,extremes,presets,views}/README.md` (all) and `docs/listening/m1-space/README.md` (its shape); INTENT §14 (the Extremes block, 1194–1340, and the bound at 1025), §17 (1866–2141), §18 (the presets block 2355–2380, what a session saves 2234–2243, the ring rows 2650–2665), §19 (3036–3060), §20 (3124), §23 #60–#76 (3244–3260, #69–#72 in full); `docs/m2-carry-over.md` (the header and rows 313, 331, 333, 338, 378–402, plus 301, 308, 323, 330, 352). I checked the code at three points where the documents disagreed: `plugin/src/GigaGrainsPlugin.cpp:1789–1809`, `plugin/src/editor/EditorPlugin.cpp:306, 369, 439`, and `ui/src/Theme.h` (absent).

Totals: **2 CONFLICT, 16 DRIFT**, with 3 UNSURE points noted inside findings.

---

## Conflicts

### D-1. INIT keeps Ring Length, against the director's D8 "nothing else"
**Class:** CONFLICT (minor; a controller's ruling narrowing a director decision, not ratified)
**Sources:**
- Director's D8, presets spec `:793–795`: "**A fresh instance, bit for bit:** every default, the factory routes, nothing else." The §8 entry carries no amendment note.
- Controller's ruling, presets spec `:240–243`: "**Amended 2026-11-02 (the final review's fix wave; the controller's ruling, D8):** INIT keeps each layer's **Ring Length** alone … so it is a fresh instance in everything that is heard."
- INTENT §18 `:2372`: "It is a fresh instance but for each layer's Ring Length". §23 #70 (`:3254`), ruling (2): "first ruled to keep all five ring parameters as a preset load does, which bent D8 … only Ring Length sizes a ring, so INIT keeps it".
- Row 391 (`docs/m2-carry-over.md:146`) is closed as "the director's D8: a fresh instance; only Ring Length is memory". That credits the director with a nuance D8's text doesn't contain. #70's "Decided by" names the program manager for "the ring rulings" but not the 2026-11-02 fix-wave amendment.
- Row 391 settled the known seed (all five kept). The remaining exception, Ring Length, has never been put to the director. D5 (the sample) and D6 (the seed) already bend "bit for bit", but the director chose both at the same sitting as D8.
**Resolution (forced choice):**
1. **(Recommended)** The director ratifies "D8a: INIT keeps each layer's Ring Length (memory, not sound)". The presets spec §8 D8 then gains an "Amended by D8a" note, and #70 and row 391 name the decider. *Cost:* one line for the director to approve at the close; nothing changes in code.
2. INIT resets Ring Length to 16 s as well. *Cost:* INIT resizes rings, which reallocates on the main thread and drops held audio beyond 16 s. It reverses ruling (3)'s "no preset load resizes a ring" for INIT, and the code, tests, INTENT §18 and #70 all change.

### D-2. A click at a mode change into Peak that D5b's exemption doesn't cover
**Class:** CONFLICT (a sub-project anti-goal and spec claim against a closed row that records a click outside the director's stated scope)
**Sources:**
- Extremes spec §3 anti-goals `:83–84`: "A click or a zipper when … the limiter's mode changes". §6.5 `:459–460`: "A mode change crossfades … so switching never clicks".
- D5b, spec `:458` and §23 #69: the exemption covers "the calls in which Peak's gain falls below −6 dB or Soft's input passes twice the ceiling, plus 10 ms after, and nothing wider".
- Row 378 (`docs/m2-carry-over.md:133`), last sentence: "Peak entered mid-sound at −18 dB still scores 24.4 at 461 samples into the mode fade … that click is outside the stated scope, so it is exempt only when the draw's calls fall so that the dip lands in an earlier call". The row is **Closed**.
- So a mode-change click is known and recorded. It sits outside the director's "nothing wider", and the sweep passes only because of where the call boundaries fall.
**Resolution (forced choice):**
1. **(Recommended)** The director confirms that a mode change into Peak over hot material may click, as part of Peak's zero-attack law (D5b). Spec §3 and §6.5, and INTENT §14 `:1305–1306`, then say so. The sweep's exemption is defined over the mode fade by samples rather than by call alignment, so the pass no longer depends on block layout. *Cost:* one anti-goal relaxed, and a small test change.
2. Treat it as a defect: inside a mode crossfade, Peak's gain can fall no faster than the fade. Reopen row 378's last clause as a row for M3. *Cost:* a DSP change to the limiter, `fx-limiter` may move, and a re-voice check of the presets that use Peak.

---

## Drift

### D-3. D-glide's accepted crackle still says "never clicks"
**Class:** DRIFT
**Sources:** Under D-glide (2026-10-31; row 379, `docs/m2-carry-over.md:134`), the glide inside a Feedback ≥ 95% loop over a near-silent tail "crackles (… an accepted law, not a defect)". The text still says otherwise in four places:
- extremes §3 `:83–84` ("when Delay Time moves");
- §6.4 `:425` ("bends the pitch like tape and never clicks");
- §8 D6 `:688–689` ("never clicks");
- INTENT §14 `:1287–1290` ("so a move bends pitch like tape and never clicks").

D-glide appears only in spec §6.4 `:418` and §23 #69, not in INTENT §14's body.
**Resolution:** Add a clause to INTENT §14 `:1290`, after "never clicks": "except inside a Feedback loop at 95% or more over a near-silent tail, where the re-read passes crackle: an accepted law (the director's D-glide, 2026-10-31; row 379)". Add the same qualifier to extremes §3 `:84` and §8 D6 `:689`, marked **Amended (D-glide)**.

### D-4. INTENT §14 and the extremes pack quote Peak's click score from before D5b
**Class:** DRIFT (one UNSURE point)
**Sources:**
- INTENT §14 `:1305–1307`: "Peak's own click score on hot material is 15 to 18 … which a listener should judge". The extremes pack `:86–88` gives the same figure and asks "is it clean enough?".
- D5b (2026-10-31, after the pack's `ec85c6a` of 2026-10-30) accepted the laws. Row 378 records higher scores at the bottom ceilings: 19.59, 42.05 and 21.05 for Peak, and up to 19.77 for Soft at −24 dB.
- INTENT §14's limiter bullet doesn't mention D5b or `kLimiterDrive`.
- **UNSURE:** extremes §10 `:870–871` says "Peak did not click in `fx-limiter` (clean score 4.31)". `tests/golden/expected/fx-limiter.metrics.json` confirms 4.31. That case is the same shape as the pack's `12`, so "15 to 18 on hot material" may come from a different measurement. Its source isn't stated.
**Resolution:** At INTENT §14 `:1306`, replace "15 to 18 … which a listener should judge" with the D5b law and row 378's measured range, saying which material. In the pack's question 1 (`:86–88`), say that D5b has already kept the law, so the sitting's question is whether to revisit it after M2. State where "15 to 18" was measured, or drop the figure.

### D-5. D-specwindow is missing from the Spectral source's design text
**Class:** DRIFT
**Sources:** The director's D-specwindow (extremes §8 `:725–730`; #69; row 381, `:136`) says a Spectral layer's analysis ignores Loop, Loop Length and Splice, and the ring commands skip it. No body text says so:
- extremes §6.2 (`:249–302`), which has no Amended mark for it;
- INTENT §14's Spectral "material" bullet (`:1245–1251`). By contrast, D-specsave is in §18 `:2240–2243` and in spec §6.2 `:304–310`.
**Resolution:** Add to INTENT §14 `:1251` and after extremes §6.2 `:302`: "**Amended (D-specwindow, 2026-11-01):** the analysis reads Position over the whole ring, whatever Loop, Loop Length and Splice say, and the ring commands skip a Spectral layer (row 381)".

### D-6. The extremes spec's counts and carry-over forecast are stale
**Class:** DRIFT
**Sources:**
- §12 `:944`: "**Expected at the end: 11 open rows,** none added". The sub-project added rows 357–382 (carry-over header `:29–36`), and the count rules are retired (#67). Unlike presets §12 `:981–985` and views §12 `:1083–1086`, it has no amendment.
- §10 `:848`: "(9 new, 45 in all; every WAV of the 36 existing cases …)". As merged, the cases are 51 earlier plus 9 (#69, `:3253`).
- §11 `:923`: "Fourteen renders from thirteen specs". The pack (`README.md:37`) and `specs/` (15 files) have fifteen renders from fifteen specs.
**Resolution:** Add an **Amended** note at `:944` matching presets §12 (rows 357 to 382 added, plain log, #67). At `:848`, say "60 cases on main after the merges". At `:923`, say "Fifteen renders from fifteen specs (`02a`–`02c`)".

### D-7. §23 #69 says rows 378 and 379 were "closed as defects"
**Class:** DRIFT (wording that inverts the decision)
**Sources:** #69 (`:3253`): "The sweep's two limiter and delay exemptions (the director, 2026-10-31; carry-over rows 378 and 379, closed as defects)". Rows 378 and 379 (`:133–134`) say: "**Closed: an accepted law (D5b)**" and "An accepted law, not a defect".
**Resolution:** In #69, replace "closed as defects" with "closed as accepted laws, not defects".

### D-8. Undo of Mutate and preset loads, described three different ways
**Class:** DRIFT
**Sources:**
- As built, the editor pushes one whole-state entry per Mutate press and per browser or strip preset load, INIT included: `EditorPlugin.cpp:369` `pushWholeState("Preset", …)`, `:439` `pushWholeState("Mutate", …)`; row 402 (`:157`), whose test is "a preset load, INIT and a Mutate press are one undo entry each".
- The presets spec still says the gestures are undo's unit: §6.9 `:582–583` "so hosts record it as automation and sub-project 6's undo will see ordinary gestures"; §9 `:835–837` "sub-project 6's undo sees ordinary gestures".
- The views spec §6.8 `:557–559` says "this sub-project pushes none". The pushes now live in its own `EditorPlugin`, added at the merge (row 402). §6.8 also has no Amended text for PR 2's undo work: the whole-state replay in batches through `sendValues` and the merging of key repeats (plan Task 3; rows 337, 323). INTENT §17 `:2138–2139` records that work. The spec's status line (`:5`) claims every PR 2 change is marked in place.
- INTENT §17 `:2094–2098` names "whole-state entries for sub-project 5's Mutate and preset recall" without row 402's limits: the samples and the preset's name aren't restored, and a host's preset load pushes nothing. INTENT §17 `:1914` "Undo and redo stay with sub-project 6" is stale.
**Resolution:**
- In presets §6.9 `:582–583` and §9 `:836`, change the text to "each press is one whole-state undo entry (views D7; row 402)".
- In views §6.8 `:559`, add "**Amended (the merge with sub-project 5's group 2):** `EditorPlugin::pushWholeState` pushes one per browser/strip preset load, INIT and Mutate press; the entry holds values and routes alone (row 402)". Also add a PR 2 paragraph on the `sendValues` batch replay and on key repeats merging within 1 s.
- In INTENT §17 `:2096`, append "(values and routes; a recall's samples and name aren't restored, row 402)". At `:1914`, replace the text with a pointer to `:2094`.

### D-9. The edited mark ignores the ring parameters, but the docs don't say so
**Class:** DRIFT
**Sources:** `GigaGrainsPlugin.cpp:1795–1801` skips `isRingParam` slots: "A preset load ignores the ring parameters, so reloading wouldn't undo a change". The docs say otherwise:
- presets §6.2 `:251–253`: "A base value that differs from the loaded preset's marks it edited … from the 218 values";
- INTENT §17 `:1896–1897`: "when a base value differs from the loaded preset's".
- Presets §6.9 `:565` also says "a bitset over the 218 slots", and the parameter count is 283 (#69).
**Resolution:** At presets `:252–253`, write "from the 283 values, the ring parameters aside (a load keeps them)". Add the same clause to INTENT §17 `:1897`. At `:565`, write "the 283 slots".

### D-10. Theme handling and the header in the views spec, after row 313 closed
**Class:** DRIFT
**Sources:**
- Views status `:5`: "The header's split into its own component (row 331) and the preset strip's palette (row 313's rest) wait on sub-project 5's group 2."
- Views §6.9 `:608–611`: "`ui/src/Theme.h` is back as a shim … until PR 2's palette pass (carry-over row 313)."
- What changed: row 313 (`:68`) is **Closed** in full; "`ui/src/Theme.h` is gone, replaced by `PresetColors.h`" (checked: the file is absent). Row 331 (`:86`) now falls to the M2 close, since PR 2 finished first. The carry-over header `:36` still says "closed 313 and 330 in part".
- #72 (`:3256`) brings back D8's declined option 2 (a System choice) for M5. Views §8 D8 (`:862–868`) doesn't point to it.
**Resolution:**
- Status `:5`: row 313 closed by presets group 2; row 331 is the close's.
- §6.9 `:611`: add "**Amended:** `Theme.h` is gone; the three panels read the palette (row 313, closed by sub-project 5 group 2)".
- Carry-over header `:36`: 313 closed.
- §8 D8: add "M5 adds System, high contrast and user themes (§23 #72)".

### D-11. The views spec and #71 give PR 2's carry-over numbers wrongly
**Class:** DRIFT
**Sources:**
- Views §16 `:1212–1217`: "Closed here: … 334 to 337 … Open: … 338 (the final verification). New rows 383 to 387 hold the minors deferred." #71 (`:3255`): "333 and 338 open, and rows 383 to 387 added".
- Row 338 (`:93`) is "**Closed** by PR 2's final verification at `293cbb5` (2026-11-02)". Row 388 exists ("PR 2's deferred minors … the final review's triage"). The carry-over header `:36` says "added rows 383 to 388", and presets §12 `:983` says "views' PR 2, whose rows took 383 to 388".
**Resolution:** In views §16 `:1212–1217` and in #71, write "334 to 338 closed (338 at `293cbb5`); rows 383 to 388 added".

### D-12. The views spec still says the FX view has no viewport
**Class:** DRIFT
**Sources:**
- Views §6.7 `:538–540`: "The view has no viewport: its two panels fit 640 × 360, which the frame's tests check."
- VW-T19b (`:1185`): "the FX view's Output panel … no longer fits a 640 × 360 view, so the FX view scrolls vertically as the Edit view does". VW-P2e (`:525`): "the view wraps in the Edit view's panel grid".
- §16's preface (`:1150–1152`) says every ruling is marked Amended where it applies, but no Amended (VW-T19b) mark appears in §6.5 or §6.7.
**Resolution:** At `:539`, add "**Amended** (VW-T19b, VW-P2e): the FX view scrolls in the shared panel grid at 640 × 360". Add a VW-T19b mark in §6.5 after `:411`.

### D-13. §23 #70 on content v1 and its row range
**Class:** DRIFT
**Sources:**
- #70 (`:3254`): "content v2 … adds strings and brass … and v1 has no voices or strings until then". The presets spec `:396` says v1's "37 files are Strings, Winds, Mallets, Plucked, Textures and the nine generated". The fetched `PROVENANCE.md` has v1 VCSL entries under `Samples/Strings/` (psaltery-c4-bowed, dan-tranh-b2-tremolo).
- #70 gives the group's rows as "rows 389 to 400". Presets §12 `:981–983` gives 389 to 401, and row 402 came from the merge.
- Presets §11 `:942` still names "the content's voice-and-strings phrase". The pack (`:59–68`) explains the strings-only phrase, but §11 isn't marked Amended.
**Resolution:**
- #70: write "v1 has no voices and no orchestral strings (its Strings are folk instruments)", and "rows 389 to 401, and 402 at the merge with views' PR 2".
- Presets §11 `:942`: add "**Amended:** a strings phrase (`phrase-strings.json`), since no content release has a voice".

### D-14. The layers pack describes the instrument before per-layer samples
**Class:** DRIFT
**Sources:**
- Layers pack `:76–80`: "**In the instrument, layers B to D are silent unless their Source is Live.** The sample loader feeds layer A alone until the session format's next version (MEDI v2…)". That changed:
  - sub-project 2 built `MEDI` v2 and per-layer samples (#68);
  - views PR 2 added the sample strip for every layer and drops on any lane (INTENT §17 `:2131–2137`; VW-P2c, VW-P2m).
- Layers pack `:87–91`: "step through the device's eight remote-control pages". There are thirteen since extremes (spec §6.8 `:552–555`; #69).
- The pack was rendered at `75358ef`/`f89fc6d`, before row 301's morph fix (`5693fe9`), which changed the Layer On crossfade at the center. That affects `01`, `05` and `14`, which automate Layer On. The stated spans mostly avoid the ramps.
**Resolution:**
- At `:76–80`, write: "A layer B to D left at Sample has no sample until one is loaded: load one from the sample strip or drop a file on its lane (since M2 sub-project 2 and views PR 2), or set its Source to Live and feed the sidechain."
- At `:87`, write "the first eight of the thirteen pages".
- Re-render the pack on main before the sitting.

### D-15. The engine pack was rendered before the steal fade and the ring pyramids
**Class:** DRIFT
**Sources:**
- Pack `:60` (`07-live-hq`): "Every grain's line reads `"lvl":0`: the ring has one level … until sub-project 2 … over 7.7–8.2 s it measures −76.6 dB … against −47.7 dB". Sub-project 2 built the ring pyramids (`188c436`; CLAUDE.md: `fx-hq-live` "every line reads `"lvl":1`"), so the file on main reads level 1 and these figures are stale.
- The pack's commit `315d77c` comes before the raised-cosine steal fade (`65292dc`, EF-8; engine spec `:380`, `:399`). `04` and `05`'s RMS and grain figures (`:57–58`) therefore come from the linear fade.
- `06` (`:59`) automates Layer B On and predates row 301's ramp fix.
- Row 308 (`:63`) lists more open items for this README: the figures read off the spectrogram, `02`'s spans, and HQ's floor at stretch 0.67.
**Resolution:** Re-render `04`–`07` on main and re-measure them. Rewrite `07`'s row for level 1 ("the ring's pyramid serves +12 at rate 1"). Fold in row 308's items. Note at the top which commit the figures come from.

### D-16. The sources pack still waits on a readout that has landed
**Class:** DRIFT (one UNSURE point)
**Sources:**
- Pack `:75–76`: "the status says why (through sub-project 6's readout once it lands)". Views PR 2 built the memory readout and cap (INTENT §17 `:2133–2137`; views pack `:111–113`).
- **UNSURE:** `:73–74` says to set Ring Length "from the generic editor, while playing". Ring Length is "neither automatable nor modulatable" (INTENT §18 `:2655`), and some hosts' generic editors may not show a non-automatable parameter. Not verified in a host.
- Rows 385 (a long ring's lane draws peaks for its newest 16 s only) and 386 (the splice outline disagrees with the engine when Loop is on) are visible in exactly these checks, and no pack mentions them.
**Resolution:**
- At `:76`, write "(the sample strip's memory readout, views PR 2)".
- At `:73`, write "from the Edit view's Ring panel (or the generic editor, where the host shows it)".
- Add a note naming rows 385 and 386 beside the long-ring and splice items.

### D-17. Items sent to "the director's checklist" are missing from the packs
**Class:** DRIFT
**Sources:**
- Views spec `:336–339` and #71 ("a real double-click two, its first press an ordinary click (a checklist item)"): the pad double-click makes two undo entries. It isn't in the views pack's checklist (`:92–123`).
- Row 333 (`:88`, "The director's checklist": the route list auto-selects a route an undo or a load added) is absent.
- Row 402 ("The director's checklist": undo of a preset recall restores neither the samples nor the name) is in neither the views pack nor the presets pack.
- Row 387 ("The M2 close's listening gate": the new strings for the text pass) is absent.
- Rows 396 (naming and tags; spec §6.7 `:470–472`, "The director may strike or add words … at the gate") and 397 ("A note for the director's gate": the browser's list ground) are absent from the presets pack (`:161–223`).
**Resolution:** Add these as checklist items. The pad double-click, row 333, row 402 and row 387 go in the views pack. Rows 396 and 397 go in the presets pack, with row 402 cross-referenced there. Alternatively, put all six straight into the close's merged checklist (appendix below).

### D-18. INTENT's status lines lag sub-projects 4 to 6
**Class:** DRIFT (this may also fall to the reviewer of INTENT's header and roadmap)
**Sources:**
- INTENT `:7` gives sub-project 4 as "(PR #20, in review)" and doesn't mention sub-projects 5 and 6.
- §20 `:3124` says "**6 Views** (PR 1 built; §23 #71)". PR 2 was built on 2026-11-01 (#71) and merged (PR #24, main `ba56c5f`).
- CLAUDE.md's "Where we are" covers sub-projects 1 to 5 and doesn't mention sub-project 6.
**Resolution:**
- `:7`: sub-projects 4 to 6 built and merged, with their PRs.
- `:3124`: "6 Views (PRs 1 and 2 built and merged; §23 #71)".
- CLAUDE.md: add a sub-project 6 sentence.

---

## Appendix: the gate questions per pack, for the close's merged checklist

The packs differ in shape. M1's `m1-space` puts its ear questions in a numbered "Checklist (spec §9)", with a host checklist after it. Among the M2 packs, the engine pack has "Three questions", the extremes pack "For the director's ears" and the presets pack "Notes for the sitting". The layers pack embeds its questions in its table, and the sources and views packs have host checklists only. The close's merged checklist should pull all of these out.

**m2-layers** (`docs/listening/m2-layers/README.md`)
- OPEN, `:54` (and #65): "Is that drop acceptable, or should the pad keep power rather than amplitude?" No later decision touches this. D1-rest (2026-10-31) changed the floor near the empty corners only.
- OPEN, `:61` (and #65): "Env 1's release is heard only in the last grains and the reverb's tail". No later change spawns grains after a note-off.
- OPEN, the host checklist `:82–114`. The intro at `:76–80` and the "eight pages" at `:87` need D-14's fix before use.

**m2-engine** (`docs/listening/m2-engine/README.md`)
- OPEN, `:64–67`, Q1: does HQ earn its weight?
- OPEN, `:68–70`, Q2: D4's steal threshold. The figures it cites (`04`, `05`) predate EF-8 (D-15).
- OPEN, `:71–75`, Q3: HQ's weak spot just under each octave. Row 308 adds "HQ's broadband floor at stretch 0.67".
- OVERTAKEN, `:60`: `07`'s "one level until sub-project 2" (D-15).
- OPEN, the host checklist `:82–92`.

**m2-sources** (`docs/listening/m2-sources/README.md`)
- The pack asks no ear questions; `08a`/`08b` is a hash check (`:55`).
- OPEN, the host checklist `:66–83`. `:75–76` is overtaken by views PR 2 (D-16). Add rows 385 and 386 to look at.
- OPEN elsewhere, for the same sitting: row 352 ("A 120 Hz Ring Length drag clicks on the ring … The director's ears at M2's listening gate").

**m2-extremes** (`docs/listening/m2-extremes/README.md`)
- PARTLY OVERTAKEN, `:86–88`, Q1 (Peak's click score). D5b (2026-10-31) already kept the law, so the ear check is now only whether to revisit it later (D-4).
- OPEN, `:89–91`, Q2: the Spectral kind's width.
- OPEN, `:92–94`, Q3: the gap on a switch to Spectral.
- OPEN, `:95–96`, Q4: the squash of a loud repeat at Feedback 100% (D7).
- OPEN, `:97–101`, Q5: the bend from Reverb Modulation's glide (host only).
- OPEN, the host checklist `:108–123`.
- Not yet listed: D-glide's crackle (row 379) could be a confirm-by-ear item. Row 381 (D-specwindow) is a known gap the director deferred, not an ear question.

**m2-presets** (`docs/listening/m2-presets/README.md`)
- OPEN, `:168–170`: should INIT start the instrument further into its sample (D8)?
- OPEN, `:176–178`: 17 and 18 need a playing transport (row 395).
- OPEN, `:179–180`: 22 Shard Storm's ticks on drums (row 395).
- OPEN, `:181–182`: 19 stays bounded.
- OPEN, `:183–184` and `:211–223`: the noise floor of the gained sources, 15 Kalimba Steps in particular (row 394).
- OPEN, `:204–209`: lock the bus filter types against Mutate? About 73% of 15% presses flip one.
- OPEN, the host checklist `:194–210`.
- Not yet listed: row 396 (strike or add tag words, `brass` among them; spec §6.7 `:470–472`), row 397 (the browser's list ground), row 402 (what an undo of a recall restores), and row 393 (VSCO's credit request; it waits on the director, not on the ears).

**m2-views** (`docs/listening/m2-views/README.md`)
- OPEN, the host checklist `:92–123`, which covers PR 1 and PR 2.
- OPEN, the 16 snapshots to look at (`:37–50`).
- Not yet listed: the pad's double-click as two undo entries (VW-T10a), row 333, row 402, row 387, and rows 385 and 386 (what the lanes show).
- OPEN elsewhere: row 330's remainder ("PR 2 or the director's checklist": a loaded out-of-range depth is clamped on first touch).

**The exit checks the specs leave to the sitting:** presets §1 `:31–35`, "every factory preset loads in all four formats in Live and Bitwig"; views §10 `:1011–1016`, the frame time in Live and Bitwig. Both are in the packs' checklists.

---

## What I checked and found consistent

- **Extremes D1–D8 and D7a:** spec §8 against #69 and INTENT §14 `:1221–1297`, word for word in substance.
- **D-bound:** spec §6.4 `:413` and §6.6 `:492`, INTENT §14 `:1025` and row 380, all "3 + the input's peak" with the delay engaged.
- **D-specsave:** spec §6.2 `:304–310` and INTENT §18 `:2240–2243`.
- **D-corners:** spec `:732–737`, row 382 and #69.
- **The morph fix:** D1-rest leaves the four-layer and adjacent-pair weights unchanged at rest. The packs' center and corner claims still hold (layers `:47–50`, views `03`, extremes `05`, `12` and `13`).
- **Presets D1:** the content has 24 sources per product, 8 tagged `midi`, and focus tags 16 / 12 / 8 / 12 (counted from `content/presets/`).
- **Presets D-Q2 and D-Q3, the 62 structural locks, the `brass` tag, content v2 as a superset of v1, and the preset strip, browser and Mutate panel taking the theme (row 313):** consistent across the spec, INTENT §17 `:1915–1928`, INTENT §18, #70 and the carry-over.
- **INIT keeping Ring Length alone:** the same in presets §6.2 and §6.9, INTENT §18 `:2371–2374`, #70 and row 391 (only D-1's annotation is missing).
- **The `header.preset` slot (90 px):** the same in presets §6.8, views §4 and §6.2, and INTENT §17 `:1895` and `:2047`. Views moved nothing else in the header.
- **Views D1–D12:** spec §8 against #71 and INTENT §17. D12 is recorded in INTENT §16 `:1857`.
- **#72 (full themes in M5):** consistent with INTENT §17 `:1933–1934` and with D8 and D9 as M2's scope.
- **Undo:** views D7 matches INTENT §17 `:2094–2098`, and a host's preset load pushing nothing (row 402) matches "state loads push nothing". Only the limits in D-8 are missing.
- **The extremes pack's `11-delay-hold`:** matches D7a (a sine burst holds within 0.1 dB, noise darkens).
- **The presets pack's Mutate ladder:** matches spec §11 and `--mutate-preset`.
- **The sources pack's figures:** measured at `489d85e`, after the engine follow-up merged into the branch.
