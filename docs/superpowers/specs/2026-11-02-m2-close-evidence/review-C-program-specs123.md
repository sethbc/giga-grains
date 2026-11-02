# Consistency review C: the M2 program documents and sub-projects 1, 2 and 3, against INTENT

**Read:** `docs/superpowers/specs/2026-10-29-m2-programme-design.md` (all 640 lines);
`docs/superpowers/plans/2026-10-29-m2-programme.md` (all 293); the layers spec
(`2026-10-29-m2-layers-design.md`, all 1038), the engine spec (`2026-10-30-m2-engine-design.md`,
all 947) and the sources spec (`2026-10-30-m2-sources-design.md`: status, §1–§6.11, §7–§15);
INTENT §13, §14 (units, budget, mipmaps, the chain, the morph, the Source kind, the matrix's
targets, the tap), §18 (the ring parameters, the restore), §19 (process notes), §20, §21, §22 and
§23 #60–#76 in full; `docs/m2-carry-over.md`'s header. Spot-checked against the code where a
claim is checkable (`PatchState.h`, `ModMatrix.h`, `ContentPaths.h`, `smoke_tests.cpp`,
`Budget.h`) and against `git log --merges` on `main`. Director decisions that live only in the
program manager's memory are cited from `~/.claude/projects/-Users-sethbc-src-giga-grains/memory/`.
Worktree at `main` `0189dc9`. Read-only: nothing in the repository was edited.

**Counts:** 4 CONFLICT, 20 DRIFT, 1 UNSURE (25 findings). The three known seeds are C-2 (merge
order), C-4 (count rules) and C-5 (model tiers).

---

## Conflicts

### C-1. The restore criterion: "matches the saved session" (#64) against SP-T7e's "deterministic, not continuous"

**Class:** CONFLICT (a Claude ruling against a criterion recorded in the director's #64)

**Sources:**
- Program design §7.5, l.484–487: the codec is sub-project 2's choice under "the programme's
  criterion ... every machine that restores a session renders it identically, **and the restore
  matches the saved session's own render within the goldens' metric tolerances**".
- INTENT §23 #64 (l.3248), in the director's decisions: "under a criterion that every restore
  renders identically and matches the saved session within the goldens' tolerances".
- INTENT §23 #68 (l.3252), the codec call: float32 "restores bit for bit, which **exceeds** the
  program design's criterion".
- Ruling SP-T7e, sources spec §6.6 l.571–575 and INTENT §18 l.2288–2290: a restored session renders
  the same on every restore "but **not identically to the saved session continuing** ... The
  `[plugin]` tests gate the restore's determinism (two restores identical) and the ring's frames
  bit for bit, **not identity with a continued render**."
- Still unamended in the sources spec: goal 1, l.48–50 ("renders bit-identically to the saved
  session (every grain and every sample of audio the same)"), and §10's `[plugin]` bullet,
  l.913–914 ("restore then render equals the saved session's render from the save point").

The ruling drops the second half of #64's criterion, and nothing tests "within the goldens'
tolerances" either. #68's "exceeds" holds for the ring's frames, not for the render.

**Resolution (forced choice):**
1. **(Recommended)** Amend the criterion to what was built. Change design §7.5 to: every restore
   renders identically and restores the saved frames bit for bit, while the clock and grain index
   restart (SP-T7e). Add a dated note to #64, and make sources goal 1 and §10 l.913 say the same.
   *Cost:* the director accepts that a reopened frozen pad replays the same material with new
   grain draws. It is never the saved session's continuation.
2. Keep the criterion and add the missing check: a `[plugin]` case comparing a restore's render
   with the saved session continuing, by the goldens' metrics. *Cost:* a new test that a sparse
   cloud may fail. Meeting it exactly would need the clock and grain index saved in the state,
   which is a format change.

### C-3. The PM ruling "a merge, not a rebase" against #64's "each rebased and re-verified"

**Class:** CONFLICT (a program-manager ruling against the wording of director decision 4)

**Sources:**
- INTENT §23 #64 (l.3248), decision (4): "wave 2 ... in parallel (merge order 3, 2, 4, **each
  rebased and re-verified**)".
- Design §4 l.126–127 ("each rebased on the one before and re-verified"); §6 wave rules
  l.208–209 ("they merge after it, rebased") and l.213–215 ("Each later one rebases on the merged
  result"); plan C1 l.93–95 ("Rebase m2-NAME onto origin/main ... force-push").
- INTENT §23 #68 (l.3252): "merged into this branch from `main` on 2026-10-30 (**a merge, not a
  rebase, the program manager's ruling**)". The sources spec §13 l.1044–1045 says the same.
- INTENT §23 #69 (l.3253), twice: "main's sub-project 3 and its follow-up came in as one merge,
  not a rebase"; "The merge of sub-project 2 ... one merge, not a rebase".

The re-verification the decision cares about was done each time (#68 and #69 restate the hash
claims). The mechanism the director's decision names was not used.

**Resolution (forced choice):**
1. **(Recommended)** Ratify it. Amend design §4 l.126, §6 l.208 and l.213–215 and plan C1 to "rebases
   on, or merges, the merged result, then re-verifies". Add a note to #64 citing the ruling.
   *Cost:* none for M2's history, which is already merge commits. The history stays non-linear.
2. Keep "rebase" as the rule and record the M2 merges as a breach. *Cost:* nothing can be
   redone, since everything is merged. The rule goes on stating what wasn't done. (Moot for M3 if
   the director's M3 process, one serial session, is adopted.)

### C-10. Rows 118 and 119 moved on the program manager's word alone

**Class:** CONFLICT (process: the plan's own rule against what #65 records)

**Sources:**
- Plan A3, l.65: a move of an inventory item "is a design amendment, which **needs the PM's and
  the director's word**".
- INTENT §23 #65 (l.3249): "**The programme manager's acceptance** (2026-10-29): carry-over rows 118
  and 119 move from sub-project 2 to this one". Its closing line: "Decided ... by the director
  (D1–D5) and the programme manager (rows 118 and 119)".
- Layers spec §12 l.966–967: "the programme manager accepted the move on 2026-10-29".

No record shows the director's word on the move. (The text drift that follows is C-9.)

**Resolution (forced choice):**
1. **(Recommended)** Put it to the director in the close's forced choices as a ratification, and
   record the answer in #65. *Cost:* one line for the director; the work is built and was reviewed.
2. Amend plan A3 so the PM alone may move an item between sub-projects when the move shrinks a
   later one's scope. *Cost:* the director loses sight of scope moves, which the plan meant to
   keep.

### C-11. The controller narrowed the director's sources D1 ("which rings a session saves")

**Class:** CONFLICT (a controller ruling changes behavior the director decided)

**Sources:**
- Sources spec §8 D1 option 1, l.750: "**A ring is saved when it is held: frozen, or Loop on.**"
- Sources spec §8 l.755–758, amended by the final review: "A held ring is saved **only on a layer
  whose base Source builds to Live**".
- INTENT §23 #68's final fix wave (l.3252): "(the controller's ruling; the cost, **a Sample layer
  switched back to Live after a reopen starts with an empty ring**)".
- Design §12 l.617: a worker "escalates behaviour and sound questions as forced choices". Brief
  template l.157: "the director decides behaviour and sound by forced choice".
- The director's later D-specsave (extremes spec l.718–723; #69) extends saving to Spectral layers
  with no sample. It does not rule on Sample layers, so it doesn't ratify the narrowing.

**Resolution (forced choice):**
1. **(Recommended)** Ratify it as a close forced choice and record it in #68. *Cost:* a frozen
  Sample layer's ring (SP-T5c keeps it recording) is lost on reopen. A user who switches that
  layer to Live afterwards hears an empty ring.
2. Revert to D1 as worded: save every held ring, Sample layers included. *Cost:* each such ring
  goes into the state: 6.1 MB at 16 s, up to 115 MB at 300 s. No grain reads that audio unless
  the kind changes, and it fills the 32 MiB embed cap sooner.

---

## Drift

### C-2. Merge order: the design and plan state an order the director retired on 2026-10-30 (seed c)

**Class:** DRIFT (the records lag a director decision that isn't recorded in the repository)

**Sources:**
- Design §6 wave rules, l.201–209 ("A wave starts when every sub-project of the one before is
  merged ... [5 and 6] merge after it, rebased") and l.213 ("**Merge order within a wave is
  fixed** (3, then 2, then 4; 5, then 6)"). Per-entry lines: l.271 "Wave 2, merged second", l.295
  "merged first", l.317 "merged third", l.339 "Wave 3, merged first", l.363 "merged second".
  Risk row l.590 ("fixed merge order with rebase").
- Plan l.18 ("merge order is 3, 2, 4, then 5, 6"), Task 3 Step 5 l.264, Task 4 Step 4 l.276
  ("Merge N = 5 first, then N = 6").
- INTENT §23 #64 (4), l.3248: "merge order 3, 2, 4".
- The director's call: PM memory `m2-program.md` l.25, "**MERGE ORDER CHANGED (director,
  2026-10-30 ~23:45): finished branches merge as ready.**" No INTENT row, spec or plan records it.
- What happened (`git log --merges --first-parent main`): #15 layers 10-30; #16 engine 10-30 19:46;
  #23 engine follow-up 10-31 00:51; **#17 presets group 1 10-31 04:59; #18 views PR 1 10-31
  06:29** (wave 3, before wave 2's 2 and 4); #19 sources 11-01 11:23; #20 extremes 11-01 16:43;
  **#24 views PR 2 11-02 12:20, before #25 presets group 2 11-02 15:05** (6 before 5). Wave 2 itself
  did merge 3, 2, 4, as INTENT §20 l.3124 says.
- Also not foreseen by the design: presets split into two PRs (groups 1 and 2, #70). Design §6.5
  (l.326–346) and the §10 risks (l.589, l.591) allow a two-PR split for sub-projects 6 and 2 only.

**Resolution:**
- Add a §23 row (the next free one at the close) recording the director's 2026-10-30 call:
  finished branches merge as ready, re-verified against `main` at the merge. Cite it from #64.
- Amend design l.201–215 to say so ("A wave's sub-projects merge as each is ready, the later one
  merging `main` and re-verifying; amended 2026-10-30, §23 #NN"), and give l.271, l.295, l.317,
  l.339 and l.363 the order that actually happened.
- Amend plan l.18, l.264 and l.276 the same way.
- Note presets' two-PR split in §6.5, citing #70.

### C-4. Carry-over count rules still stated after #67 retired them (seed a)

**Class:** DRIFT

**Sources:** INTENT §23 #67 (l.3251): "with no 'no net growth', 'touch it, close it' or count rule
... #60's and #64's carry-over targets are history. CLAUDE.md's 'Working style', the M2 carry-over
file's header and the programme design's §8 say so." (Done: design §2 l.64–65, §8 l.549–565; the
carry-over header l.3–8.) Still stating the old rules:
- Design §6.7 exit, l.381–382: "**the carry-over count is at or below its §8 target**". §6.7
  l.377: "the carry-over burned down (§8)". I37, l.178: "the carry-over burn-down".
- Design §3, l.100–101: "the carry-over counted at each sub-project's start and end".
- Design §6, l.186–187: "Each spec records its carry-over count at start and end".
- Design §6.6, l.359–360: "Both PRs belong to sub-project 6 for the carry-over count".
- Design §12, l.611: the PM "keeps the carry-over count".
- Design §10's last risk, l.593: "carried only if genuinely large" (the old policy's wording).
- Plan l.11: "§8 the carry-over targets". Review Focus 1, l.28: "the carry-over counts". B2,
  l.82: "the carry-over count at start and end". **B9, l.89**: "every minor fixed or carried with a
  stated external blocker (the carry-over policy)", which contradicts B6 at l.86, already updated.
  C6, l.117: "carry-over count".
- **Plan Task 5 Step 5, l.285: "the carry-over at or below 8 open rows".** Task 6 Step 2,
  l.292: "the eight open carry-over rows".
- Sub-project specs, as written before #67 but unmarked: the layers spec §4 l.115 and §12
  l.974–975 ("Expected at the end: 11 open rows ... (the policy)"); the sources spec §4 l.117, and
  §12 l.1004–1005, whose sentence after the strikethrough still states the old policy ("Every
  minor ... is fixed in the fix wave unless blocked outside our control or genuinely large (the
  policy)").

**Resolution:**
- Design l.381–382 becomes "the carry-over swept: every row closed or carrying the reason it
  waits, and the deferred items listed in the close's report (§23 #67)", with l.377 and I37 to
  match.
- Drop the count clauses at design l.100–101, l.186–187, l.359–360 and l.611.
- Plan l.285 becomes "the carry-over swept (§23 #67)" and l.292 "the open carry-over rows". B9
  becomes B6's wording. l.11, l.28, l.82 and l.117 lose "count".
- In the sources spec, strike l.1004–1005's last sentence as l.1004's first was struck. Mark the
  layers spec l.974–975 "(the policy then in force; retired by §23 #67)".

### C-5. Model tiers: "never haiku or sonnet" against the director's NO FABLE rule (seed b)

**Class:** DRIFT (the plan lags a director rule that the repository records nowhere)

**Sources:**
- Plan Global Constraints, l.22: "workers and reviewers on the top tier (the default model), task
  implementers on Opus". When written, "the default model" was Fable.
- Plan brief template, l.151–152: "reviewers and decision-makers on the default model,
  implementers on Opus (`model: "opus"`); **never haiku or sonnet**".
- Design §12, l.605–606: "reviews and decisions on the top tier; implementation on the tier below".
- The director's rule, PM memory `subagent-model-tiers.md` l.3 and l.11–15: "NO FABLE ... Opus is
  the top tier; otherwise superpowers' default Model Selection; always name the model explicitly"
  (Haiku for mechanical tasks, Sonnet for integration and the reviewer floor). No repository file
  has it: `grep -rni 'haiku\|sonnet'` finds only the plan's l.152 and the views PR 2 plan's
  `**Model:** sonnet` lines (`docs/superpowers/plans/2026-11-01-m2-views-pr2.md` l.139, l.291,
  l.366, …). Those follow the new rule and contradict the program plan.
- Related, outside this family: the sub-project plans tell implementers to commit with
  `Co-Authored-By: Claude Fable 5.1` (layers plan l.59, engine plan l.65, sources plan l.64,
  extremes plan l.62, presets plan l.70, views plan l.68). The presets plan l.72 says "reviewers
  inherit the parent model", which the "always name the model" rule forbids.

**Resolution:**
- Replace plan l.22 and l.151–152 with: "Subagents: always name the model; never Fable. Opus where
  superpowers' Model Selection asks for the most capable model (design tasks, the whole-branch
  final review, high-risk reviews, fix-loop escalation); otherwise its defaults, Haiku for
  mechanical tasks and Sonnet for integration and as the reviewer floor (the director,
  2026-10-30)."
- Change design §12 l.605–606 to point at that line.
- Record the rule where every session reads it: CLAUDE.md "Working style" (or INTENT §19's process
  note, l.3081–3085), so it stops living in memory alone.
- The Fable co-author lines in the frozen sub-project plans can stay as history. A plan written
  from here on uses the session's own model's line.

### C-6. The resample tap "costs nothing while no layer is Resample", amended by SP-T12ac

**Class:** DRIFT

**Sources:**
- Ruling SP-T12ac (#68, l.3252; sources spec §6.2's amendment, l.269–273): "the tap's history is
  written whatever the kinds (two stores a sample) ... 'while no layer is Resample the tap costs
  nothing' no longer hold[s]".
- Unamended: design §7.2 l.424–425 ("The tap costs nothing, and adds no latency, while no
  layer's source is Resample"). INTENT §14 l.269–271, whose parent bullet still says "it costs
  nothing, and adds no latency, while no layer's source is Resample (M2 programme design §7.2;
  §23 #64)" while its own sub-bullet (l.276–278) says the opposite. Sources spec §10 l.894: the tap
  is "written only while a Resample layer exists".

**Resolution:**
- Design l.424–425: "It adds no latency; its history is two stores a sample whatever the kinds
  (amended 2026-10-30, SP-T12ac, §23 #68)".
- INTENT l.270–271: cut "it costs nothing, and" and point at the as-built sub-bullet.
- Sources §10 l.894: "written whatever the kinds (SP-T12ac)".

### C-7. Ring Length as a mod-matrix target: INTENT still says it's pending; SP-T12c refused it

**Class:** DRIFT

**Sources:**
- Ruling SP-T12c (#68, l.3252): "Ring Length is not a mod-matrix target (`isValidTarget` refuses it
  ...), sub-project 3's test that admitted it corrected". The code agrees,
  `core/include/gg/core/ModMatrix.h` l.128–129 ("the modulators' own controls ... and Ring Length,
  which is neither automatable nor modulatable").
- INTENT §18 l.2655 ("the current mod-target rule still admits it until sub-project 3's rule
  (below)") and l.2661–2664 ("whether Ring Length is refused as a target follows sub-project 3's
  target rule ..., checked after the rebase onto it").
- INTENT §14 l.473–476: "`isValidTarget` admits every layer parameter but the modulators'
  controls". Ring Length isn't named.
- Sources spec §6.3 l.309–313 ("or named in the report if 3's rule admits it"): not amended, and
  the status line (l.5) lists no SP-T12c.

**Resolution:**
- INTENT l.2655: "neither automatable nor modulatable by its flags, and refused as a route target
  (ruling SP-T12c)". Replace l.2661–2664 with the as-built rule.
- INTENT l.474–475: "admits every layer parameter but the modulators' controls and Ring Length
  (PM-1, SP-T12c)".
- Add an SP-T12c amendment under sources §6.3 l.313 and list it in the status line.

### C-8. libFLAC and FLAC still in the design after the codec call

**Class:** DRIFT

**Sources:**
- INTENT §23 #68: "`AUDI` payloads are float32 WAV bytes, not FLAC ... §15's planned libFLAC row
  removed; the program design's exit 'libFLAC is pinned' is met by not needing it". INTENT §15
  l.1476–1478 and §18 l.2183 already say so.
- Design I12, l.152: "`AUDI` (FLAC embed, `embed=1`)". Design §6.2 Delivers, l.261–262: "the
  `AUDI` chunk (FLAC through libFLAC, `embed=1`)". §6.2 exit, l.276: "libFLAC is pinned". §7.5
  l.483–487 is conditional and fine.

**Resolution:** I12 and l.262 become "`AUDI` (float32 WAV payloads, `embed=1`; §7.5's codec call,
§23 #68)". Replace l.276's "libFLAC is pinned" with "no new codec dependency (float32 chosen, §23
#68)".

### C-9. Rows 118 and 119 still placed in sub-project 2

**Class:** DRIFT (the text half of C-10)

**Sources:** #65 (l.3249), with rows 118 and 119 closed by `263696d` (layers spec §12 l.977–982).
The sources spec l.38–39: "I9 ... was closed by sub-project 1". Unamended: design I9, l.149 ("|
2 |"); §6.2 Delivers, l.255–257 ("I8 to I13 ... the source-mode switch made reachable, fading
sounding grains instead of clearing the pool (rows 118, 119)"); §8's table, l.559 ("118, 119 |
Sub-project 2 | Closed").

**Resolution:** I9's sub-project becomes "1 (moved, §23 #65)". §6.2 drops the clause and reads
"I8, I10 to I13". §8 l.559 becomes "Sub-project 1 (moved 2026-10-29) | Closed (`263696d`)".

### C-12. "No regression against M1's baselines" after the x86 baselines were remade

**Class:** DRIFT

**Sources:**
- ES-T12d (#66, l.3250; engine spec l.648–657): "the x86 baselines were remade from the same three
  runs ... `linux-gcc13` has EPYC 9V45 and 7763 replaced; `windows-clangcl` has EPYC 7763
  replaced, Xeon 8370C and EPYC 9V45 new". Design §7.10 l.535 foresaw this: "Rebuilding the x86
  baselines is part of keeping it".
- Still saying "M1's": design §2 l.62–63 ("`engine-sample`'s ratios **against M1's baselines**
  show no `regressed` verdict on any CI model at the close"); I37 l.178 ("benchmarks against M1's
  baselines"); INTENT §20's M2 exit, l.3124 ("the report-only gate shows no regressed ratio
  against M1's baselines (§23 #41)").

**Resolution:** Say "against the committed baselines (M1's on mac-universal; on x86, the entries
remade at the AVX2 task, §23 #66), and X within 3% of M1's published headline on the reference
core (§23 #41)". The second clause keeps #41's promise checkable, since the AVX2 speedup is now
inside the x86 entries.

### C-13. The program design's status says "Not yet built"

**Class:** DRIFT

**Source:** Design l.5: "Not yet built: wave 1 starts with the programme plan". Sub-projects 1 to
6 are built and merged (INTENT §20 l.3124; #65, #66, #68 to #71).

**Resolution:** "In progress: sub-projects 1 to 6 built and merged (§23 #65, #66, #68 to #71);
sub-project 7 (the close) running. Amended 2026-10-30 (#67, §8) and as marked."

### C-14. Design §7.7: the limiter's default is "M1's soft-limiter law"; as built it is Off

**Class:** DRIFT (a §7 rule changed without the design's amendment, which §7's preamble l.386–387
requires)

**Sources:**
- Design §7.7 l.502–503: "The limiter keeps M1's soft-limiter law as its default so no golden
  moves; its stronger mode is opt-in."
- INTENT §23 #69 (l.3253): "The §9 calls that amend Part II: ... **the limiter's Off default**".
  INTENT §14 l.1300–1301: "Off is the identity, by default, and M1's per-layer soft limiters stay.
  **Soft** is M1's `softLimit` scaled to the Ceiling."
- Design §7 preamble, l.386–387: "a spec that needs to break one amends this document and INTENT
  together".

The intent (no golden moves) holds, but the rule's text describes a different default. (It belongs
to sub-project 4; listed here because the §7 compliance check is this review's.)

**Resolution:** §7.7 l.502–503 becomes: "The global limiter is Off by default, the identity, with
M1's per-layer soft limiters kept, so no golden moves; Soft (M1's law at the Ceiling) and Peak
are opt-in (amended, §23 #69)."

### C-15. Design §7.8: the content search order and fetch path as first planned

**Class:** DRIFT

**Sources:**
- Design §7.8 l.516–518: "a local `scripts/fetch-content.sh` does the same into `build/content`.
  The plugins search the installed Content folder, **then** a user content folder in the settings
  file."
- As built: `media/include/gg/media/ContentPaths.h` l.106–116 (1 `$GG_CONTENT_DIR`, **2 the user's
  `contentFolder`**, 3 the per-user install, 4 the system install). The presets spec l.321 and
  l.870 ("`GG_CONTENT_DIR` first; the settings' `contentFolder` second"). CLAUDE.md "Setup":
  `gg-content` fetches into `build/<preset>/content-cache/content/`, and `fetch-content.sh <dir>`
  takes a directory.

**Resolution:** l.516–518 becomes: "`scripts/fetch-content.sh <dir>` (CI) and the `gg-content`
target (`build/<preset>/content-cache/content/`) fetch it. A reference resolves against
`GG_CONTENT_DIR`, then the user's content folder, then the per-user and system installs (as built,
the presets spec §6.4; §23 #70)."

### C-16. Design §6.1: the morph and the mod matrix "in the engine"

**Class:** DRIFT (minor)

**Sources:**
- Design §6.1 l.223–224 ("XY morph as a modulation-free mixer law in the engine") and l.232
  ("**Owns:** `engine/` (Engine, the layer and task structure, the mod matrix and its sources)").
- The layers spec §5 option 1, l.121–128: "`Engine` stays one layer; the `Processor` owns four
  (chosen) ... the `Processor` ... is where the tasks, the morph sum and the matrix live". The code
  agrees: `core/include/gg/core/ModMatrix.h` and `ModSources.h`, and the morph in `Processor.h`.
  `engine/include/gg/engine/` holds only `Engine.h` and `TaskRunner.h`.

**Resolution:** l.223–224 becomes "a mixer law in the `Processor` (`gg-core`)". l.232 becomes
"`engine/` (the per-layer Engine, the task runner), `gg-core`'s mod matrix and sources, ...", citing
the layers spec §5.

### C-17. Layers spec §10's C++23 checks, as superseded by LS-T1a

**Class:** DRIFT

**Sources:** Layers spec §6.1 amendment, l.182–187 (LS-T1a): the smoke test checks
"`__cplusplus > 202002L`, not `>= 202302L`" and "The configure's floor message has no
scratch-project test". `tests/smoke_tests.cpp:35`: `STATIC_REQUIRE(__cplusplus > 202002L)`.
Unamended: §10 `[build]`, l.885–886: "a `static_assert(__cplusplus >= 202302L)` in the tests; the
configure's floor message checked by a scratch-project test".

**Resolution:** Amend l.885–886 to the LS-T1a form, and add §10 to the status line's list (l.5).

### C-18. Engine spec: D3's stated cost and §5's AVX2 approach, as superseded

**Class:** DRIFT

**Sources:**
- D3, l.705–708: "16 taps ... at 16 taps **the first image sits at least 70 dB down** and the
  passband holds to 0.4 of Nyquist". ES-T1a (§6.2 l.199–202, l.208–213; #66): at stretch 0.5
  "the stopband is −37 dB ... the 70 dB pin there is withdrawn". It is a question for the
  director's sitting (#66's "Questions for the director's sitting"). Only D3's weight was amended
  (l.707).
- §5 AVX2, l.147–150: "compiled with `-mavx2` (and no FMA), chosen once at load ... through a
  function pointer". §14 risks, l.925: "`-mno-fma`". ES-T12b (l.635–638): per-function
  `target("avx2")`, no file-level flag. ES-T12a (l.630–634): the table is chosen per engine.

**Resolution:**
- Add to D3 l.708: "(**Amended**, ES-T1a: at s = 1; for a residual rate near 2 the stretched
  kernel's stopband is about −37 dB, the sitting's question)".
- Mark §5 l.147–150 and l.925 "Amended (ES-T12a, ES-T12b)", pointing at §6.12.

### C-19. The steal overshoot's "8% of the default budget" doesn't follow from its own figures

**Class:** DRIFT (an arithmetic error copied into INTENT)

**Sources:** Engine spec §6.5 l.408–410 and INTENT §14 l.697–700: "at most `kStealFade` × the onset
rate grains, **8% of the default budget** at 16,384 onsets a second". `kStealFade` = 256
(`engine/include/gg/grain/Budget.h:39`). At 48 kHz, 256 / 48,000 s × 16,384 /s ≈ 87.4 grains in
flight: **2.1% of the default Budget of 4,096**, and 8.5% of the exit case's quota of 1,024 grains
(engine spec l.788–789; INTENT l.704).

**Resolution:** Both places read "about 87 grains, 2% of the default budget (8.5% of the exit
case's 1,024-grain quota) at 16,384 onsets a second".

### C-20. The output bound "2 + the input's peak" after #69's D-bound

**Class:** DRIFT

**Sources:** #69 (l.3253), "The output bound with the delay (the director, 2026-10-31, D-bound
option 1): §14's bound stays 2 + the input's peak, Space's; with the global delay engaged ... it
is 3 + the input's peak". INTENT §14 l.1025 already carries it. Unamended: the engine spec §6.11
l.558 ("the peak at most 2 + the input's peak (the chain's bound, INTENT §14)") and the sources
spec §6.2 l.256 ("the tap is bounded (`|out| ≤ 2 + |d|`)").

**Resolution:** Both gain "(3 + |d| with the global delay engaged, §23 #69 D-bound)". In the
sources spec the tap's stability argument should state the larger bound.

### C-21. Sources spec: §10 bullets not marked for SP-T12aa

**Class:** DRIFT

**Sources:** Under SP-T12aa (§6.1's amendment, l.225–235) "the window steers admission alone",
and the fade and reads keep the storage's bounds. Sources §10 `[engine][live]`, l.885–886, still
says "the admission clamp and the fade use it; a grain past a narrowed window fades to 0 with no
read outside it". (The tap bullet, l.894, is C-6; goal 1 and l.913 are C-1.)

**Resolution:** l.885–886 becomes "the window steers admission; a grain admitted under a wider
window finishes in its old span (SP-T12aa)". List §10 in the status line.

### C-22. Layers spec LS-T13a: the instrument's `sample` "stays required"

**Class:** DRIFT (minor)

**Sources:** Layers spec §6.11 amendment, l.692–693 (LS-T13a): "The instrument's `"sample"` stays
required". SP-T8c (#68; sources spec l.687–688): "An instrument spec may leave out `sample` beside
`state`". CLAUDE.md's `gg-render` section adds the same beside a `preset` (#70).

**Resolution:** Append to l.693: "(Later relaxed: optional beside `state`, SP-T8c, §23 #68, and
beside a `preset` that names layer A's, §23 #70.)"

### C-23. INTENT §14 Units: "The four kinds are Sample, Live, Oscillator and Resample"

**Class:** DRIFT (minor)

**Sources:** INTENT §14 l.204–206 ("The four kinds are Sample, Live, Oscillator and Resample
(§4)"). INTENT §14 l.392–394, the Source kind's enumeration, frozen: "0 Sample, 1 Live, 2
Resample, 3 Spectral, 4 Oscillator". Spectral has been built since #69 (l.406). (§4, Part I, lists
four kinds and stays unchanged: Spectral is a derived source.)

**Resolution:** l.205 becomes "The kinds are §4's four, Sample, Live, Oscillator and Resample, plus
Spectral, a smear of a layer's sample or ring (M2 sub-project 4); each layer picks its kind with
its Source parameter".

### C-24. INTENT §20's M2 row: "6 Views (PR 1 built; §23 #71)"

**Class:** DRIFT (minor)

**Sources:** INTENT §20 l.3124 vs #71's title (l.3255), "M2 sub-project 6 (Views), PRs 1 and 2",
and PR #24 merged 2026-11-02 (`ba56c5f`).

**Resolution:** "**6 Views** (PR 1 built 2026-10-31, PR #18; PR 2 built 2026-11-02, PR #24; §23
#71)". The close marks the row closed anyway (design §6.7).

---

## Unsure

### C-25. Sources spec §6.5: "fixed costs ... about 65 MB"

**Class:** UNSURE

**Source:** Sources spec l.385–386: "The fixed costs (the pools, the reverb, the event buffers,
about 65 MB at 48 kHz with sub-project 1's and 3's figures)". Those figures (engine spec
l.461–464; INTENT §14 l.293–295) are pools about 5 MB, the reverb 14.5 MB and the event buffers
6.3 MB, about 26 MB. The 51 MB instance total includes the four 16 s rings (25 MB), which the cap
counts as variable. Sub-project 4 adds 8.4 MB fixed (INTENT l.299–301), which wasn't known when
this was written.

**Why unsure:** the 65 MB may count something not itemized (the tap, the per-layer buffers, the
sample slots). If not, it should read "about 26 MB (34 MB with sub-project 4's 8.4)".

---

## Checked and found consistent

- **The morph law.** INTENT §14 l.322–363 states the layers spec §6.3 as amended: D1, LS-T10a,
  and row 301's floor ε = 0.35 with the multilinear ramp (the director's D1-rest). Its figures and
  tables match.
- **The tap's latency.** It is 256 samples (`kFeedbackDelay`) in design §7.2, INTENT §14 l.269,
  the sources spec §6.2 and #64. INTENT's "one block" was amended at approval, as design §13 l.633
  says.
- **§7.1's ID reservations**, kept by every sub-project:
  - sub-project 1 ends at next-free `0x31` (layer) and `0x22` (global);
  - sub-project 2 uses `0x40`–`0x44` per layer and nothing in scope 0;
  - sub-project 3 uses `0x50` per layer and `0x000050`;
  - sub-project 4's forty fall in the `0x60` blocks (delay `0x60`–`0x64`, limiter `0x65`–`0x67`);
  - wave 3 adds none: 218 + 5 + 20 + 40 = 283 (#66, #68, #69);
  - ruling FR-1 makes "an index left unused stays unused" testable.
- **One owner per state change (§7.5).** The patch is at version 2 (`PatchState.h:78`), bumped
  only by sub-project 1. The container is still version 1. Only sub-project 2 added chunks
  (`MEDI` v2, `AUDI`, `INST`); sub-project 4 says "the patch still version 2". The 64 MiB read cap
  and the 32 MiB embed cap stand. A preset is a `GGST` with no `AUDI` (SP-M3's `INST` is ignored
  on load).
- **The pool.** 4,096 per layer in the plugins and a 16,384 engine ceiling: INTENT §14 l.214–222,
  the engine spec §6.6 and #65/#66 agree. Design §6.3's "configurable to 16,384" is met.
- **Quality tiers.** Eco, Normal and HQ, default Normal, lowered only by the ladder. HQ is a
  16-tap sinc (D3). Only HQ reads the pyramid (D5), so every M1 hash holds. INTENT §14, the
  engine spec and #66 agree.
- **The mod matrix.** 48 slots and twelve factory routes (INTENT l.450, l.485; layers spec
  §6.8). The design left the count to the spec (§7.3 "32 is my expectation", §11). The RNG key
  for Random, Field 7/8 with the step index, matches §7.3. The one-clamp rule is kept.
- **Feedback** is 0–0.95 everywhere and follows the kind (#65). The instrument's Macro 7 became
  Feedback in wave 1, with the fold (§7.3, #65, LS-T9a).
- **Rings and memory.** Rings run 1–300 s (D3), and Loop Length's top and `kPositionMax` =
  1.5279342 agree across INTENT §18, the sources spec and #68. The memory cap is 1 GiB by default,
  64 MiB to 16 GiB (D2), in INTENT §21 and #68. The cap is a per-instance setting saved in the
  state, per design §7.5.
- **Render tasks** are the layers (§7.4), with grain chunking in §22. The thread-pool invariance
  test exists (#65).
- **C++23 (§7.9)** went green within its timebox, with no revert, and §22 is resolved. **AVX2
  (§7.10)** is kept under its keep rule, with the CPU-feature ruling in #66 and the x86 baselines
  rebuilt.
- **X's definition is unchanged.** Every new case is report-only (§7.6). ES-F2 fixed
  `engine-sample`'s regression inside sub-project 3, as §7.6 requires.
- **The other program and director rulings** were checked against the director's D-decisions
  (D1–D5 of 1 and of 3, D1–D7 of 2), and none contradicts them beyond C-1, C-3, C-10 and C-11.
  The program rulings are PM-1, EF-1/2/6/8 (the director chose "fade the tail" for row 300),
  SP-T7a, and the 60 to 90 minute sanitizer timeout (superseded by #74). LS-T10a is an engineering
  form of D1's stated 10 ms fade, and row 301's change went to the director (D1-rest). ES-T10b
  became row 300 for the director.
- **Status tables.** Each sub-project spec's status table lists its amendments, except those
  named above (C-1, C-7, C-17, C-18, C-21).
- **The carry-over header** says the retirement in #67's terms.
