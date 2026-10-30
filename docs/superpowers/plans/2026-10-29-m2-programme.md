# M2 Programme Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run M2's seven sub-projects in four waves through one worker session each, from the approved programme design to the unpublished 0.2.0 draft.

**Architecture:** This plan is the programme manager's (PM). It writes no product code: each sub-project's worker session writes its own spec and plan and builds with task subagents in its own worktree. The PM launches and drives the workers with the `claude-session-driver` skill's `csd`, approves specs against the programme design, relays the director's forced choices, verifies evidence on disk and in CI, and merges each wave's PRs in the fixed order.

**Tech Stack:** `csd` (tmux-hosted Claude Code workers), git worktrees under `.claude/worktrees/`, `gh`, the project's `scripts/check.sh`, `scripts/update-goldens.sh`, `scripts/worktree-setup.sh`.

**Spec:** `docs/superpowers/specs/2026-10-29-m2-programme-design.md` (the design). Its §6 gives each sub-project's scope, "Owns" and exit; §7 the cross-cutting rules; §8 the carry-over targets; §12 the hierarchy.

## Global Constraints

- CLAUDE.md's non-negotiables and the design's §3 bind every worker; the PM checks, it does not re-derive.
- INTENT Part I changes only with the director's explicit sign-off; Part II changes carry a dated §23 row in the same change.
- Nothing is published: no beta, no nightly flag, drafts stay drafts (INTENT §23 #62).
- Wave rules (design §6): a wave starts when the one before is fully merged and `main` is green; merge order within a wave is 3, 2, 4 and 5, 6; each later PR rebases on the merged result and re-verifies.
- One owner per state change (design §7.5): sub-project 1 alone bumps the patch version; sub-project 2 alone adds chunks. Wave-2 parameters append inside their reserved index blocks (§7.1): `0x..40`–`0x..4F` for 2, `0x..50`–`0x..5F` for 3, `0x..60`–`0x..7F` for 4.
- The 22 M1 goldens keep their WAV SHA-256s unless a commit names the case and the reason (§7.6).
- The carry-over: no sub-project ends with more open rows than it started with; M2 ends at or below 8 (§8).
- Model tiers (the project's practice): workers and reviewers on the top tier (the default model), task implementers on Opus.
- The worker's memory directory is not the main checkout's (Claude Code keys memory by path), so every brief carries the practice the main checkout's memory holds; the brief template below does.
- CI runs through a draft PR, never a bare branch with a dispatch.

## Review Focus

1. **A worker reports green without evidence.** Expected: the PM merges only on artifacts: `gh pr checks` green, `<build>/check/*.log` in the worktree with a `PASS` summary, the golden hash list, the carry-over counts. Pinned in the per-sub-project procedure, steps B3 to B6.
2. **A wave-2 worker edits outside its ownership or its ID block.** Expected: the PM's pre-merge diff check on `ParamIds.h`, `SessionState.h` and the patch version refuses the PR until fixed. Pinned in step C2.
3. **A worker session dies or compacts mid-build.** Expected: relaunch in the same worktree with the resume brief; the SDD ledger and the plan's checkboxes are the truth. Pinned in "If a worker stops".
4. **A later-in-wave rebase moves an M1 golden hash.** Expected: the worker re-runs `update-goldens.sh` after the rebase and re-states the hash claim; a moved hash is explained or fixed before merge. Pinned in step C3.
5. **A spec's forced choice is decided by the worker instead of the director.** Expected: the PM's spec checklist requires behaviour and sound choices to appear in the spec's "Decisions for the director" section with a recommended option; a spec that hides one is sent back. Pinned in step A3.

---

## The per-sub-project procedure

Every wave task below runs this procedure for each of its sub-projects, with N the sub-project number and NAME its slug (`layers`, `sources`, `engine`, `extremes`, `presets`, `views`, `close`). `$CSD` is the skill's CLI: `<claude-session-driver skill base directory>/scripts/csd`, found by invoking the `claude-session-driver:driving-claude-code-sessions` skill, whose header prints the base directory. The worker's shim is `/tmp/csd-workers/bin/m2-NAME`.

### Phase 0: worktree and launch

- [ ] **0.1** From the main checkout on `main`, up to date (`git pull --ff-only`), create the worktree and branch:
  ```bash
  git worktree add .claude/worktrees/m2-NAME -b m2-NAME main
  ```
- [ ] **0.2** Write the brief to `.claude/worktrees/m2-NAME/.superpowers/brief.md` from the template below (the `.superpowers/` directory is git-ignored), filling N, NAME, the design's §6.N heading and the wave's merge position.
- [ ] **0.3** Launch the worker in the worktree:
  ```bash
  $CSD launch m2-NAME /Users/sethbc/src/giga-grains/.claude/worktrees/m2-NAME
  ```
  Expected: stdout `/tmp/csd-workers/bin/m2-NAME`. Then send the opening prompt:
  ```bash
  /tmp/csd-workers/bin/m2-NAME send "Read .superpowers/brief.md in this worktree and follow it. Start with Phase A."
  ```

### Phase A: the spec

- [ ] **A1** Wait for the worker's turn to end, polling (a spec takes tens of minutes):
  ```bash
  /tmp/csd-workers/bin/m2-NAME wait-for-turn 3600; /tmp/csd-workers/bin/m2-NAME status
  ```
  Repeat while `working`. Then `read-turn`. Expected: the text ends with `SPEC READY` and names the spec's path, `docs/superpowers/specs/<date>-m2-NAME-design.md`.
- [ ] **A2** Read the spec file itself, not the worker's summary.
- [ ] **A3** Check it against this list; on any failure, `converse` the failures back and return to A1:
  - every §5 inventory item the design assigns to N appears with its own section, or a stated reason it moved (a move is a design amendment, which needs the PM's and the director's word);
  - everything it builds is inside §6.N's "Owns", and every shared-file edit is an addition;
  - its parameters use only N's reserved block; it bumps the patch version only if N = 1; it adds a chunk only if N = 2;
  - its exit criteria are §6.N's, each with the test, golden or benchmark that proves it;
  - its INTENT changes are listed, each with its §23 row, none in Part I;
  - it has a **Decisions for the director** section: every behaviour or sound choice as a forced choice with a recommended option, nothing engineering-only in it; and a **My calls** section for the rest;
  - it records the carry-over's open-row count at its start, and names the rows it closes;
  - it names its listening pack's contents under `docs/listening/m2-NAME/`.
- [ ] **A4** Put the spec's forced choices to the director in one message (`AskUserQuestion`, up to four per call, the recommended option first), together with any other spec that is at A4 in the same wave so the director answers once per wave where possible. Relay the answers verbatim:
  ```bash
  /tmp/csd-workers/bin/m2-NAME converse "The director's answers: <answers>. Record them in the spec as director decisions with today's date, mark the spec Approved by the programme manager, commit it, and go on to Phase B." 600
  ```
- [ ] **A5** Verify the spec file now carries the answers and the Approved status (`grep -n 'Approved' <spec>`).

### Phase B: the build

- [ ] **B1** Wait as in A1, with `wait-for-turn 3600` in a loop; a build runs for hours. Every few loops, sample `read-events --last 20` for anything a worker must not do (a push to `main`, a `gh release`, an edit under `libs/`, `gh pr merge`); on sight, `stop` the worker, fix the brief, relaunch with the resume brief.
- [ ] **B2** Expected end: the turn's text ends with `BUILD READY` and lists: the PR number, the `check.sh` summary (PASS), the golden hash list with its claim, the carry-over count at start and end, the pack's path, the spec's final status line.
- [ ] **B3** Verify on disk: `ls .claude/worktrees/m2-NAME/build/*/check/` shows `configure.log build.log test.log` for `dev`, `asan` and `rtsan`, and `build/check/format.log`; the last line of the worker's pasted summary is `PASS`.
- [ ] **B4** Verify in CI: `gh pr checks <PR>` shows every check passed on the PR's head commit (`gh pr view <PR> --json headRefOid`).
- [ ] **B5** Verify the goldens: `cat .claude/worktrees/m2-NAME/build/dev/golden/wav-sha256.txt` against the main checkout's last list (`build/dev/golden/wav-sha256.txt` after `scripts/update-goldens.sh` on `main`); every M1 case's hash equal, or the commit that moved it found with `git log -S<hash>`.
- [ ] **B6** Verify the carry-over, on the branch and on `main`; the branch's open count is not larger:
  ```bash
  f=docs/m2-carry-over.md; echo "open: $(( $(grep -c '^| \*\*[0-9]' $f) - $(grep -c '^| \*\*[0-9].*\*\*Closed\*\*' $f) ))"
  ```
  Every row the spec said it closes is marked **Closed** with its commits.
- [ ] **B7** Verify the spec's status table reads Built, with amendments marked, and the INTENT §23 row exists (`grep -n '^| 6[5-9] \|^| 7[0-9] ' INTENT.md` on the branch).
- [ ] **B8** Verify the pack: `ls docs/listening/m2-NAME/` holds a README and WAVs or the spec files that render them.
- [ ] **B9** Read the final review's findings and the fix wave in the PR's description; every minor fixed or carried with a stated external blocker (the carry-over policy).

### Phase C: the merge

- [ ] **C1** If N is not the first to merge in its wave and `main` has moved since the branch's base: tell the worker to rebase and re-verify:
  ```bash
  /tmp/csd-workers/bin/m2-NAME converse "main has moved (<merged PR>). Rebase m2-NAME onto origin/main, resolve conflicts within your own additions, run scripts/check.sh, scripts/update-goldens.sh and your benchmark cases again, re-state the golden hash claim, force-push with --force-with-lease, and report BUILD READY again with the new evidence." 600
  ```
  then repeat B1 to B9.
- [ ] **C2** Ownership and ID check on the final diff, from the main checkout:
  ```bash
  git fetch origin m2-NAME
  git diff origin/main...origin/m2-NAME --stat
  git diff origin/main...origin/m2-NAME -- core/include/gg/core/ParamIds.h core/include/gg/core/SessionState.h core/include/gg/core/PatchState.h
  ```
  Expected: files outside §6.N's "Owns" appear only with additions; new IDs are inside N's block; the patch version changes only for N = 1; chunk IDs change only for N = 2.
- [ ] **C3** For N not first in its wave: the hash list after the rebase equals the list before it for every case the rebase didn't intend to move (B5 again).
- [ ] **C4** Mark the PR ready and merge with a merge commit, as M1's PRs were:
  ```bash
  gh pr ready <PR> && gh pr merge <PR> --merge --delete-branch
  ```
- [ ] **C5** Stop the worker and remove the worktree:
  ```bash
  /tmp/csd-workers/bin/m2-NAME stop
  git worktree remove --force .claude/worktrees/m2-NAME
  git branch -D m2-NAME 2>/dev/null; git pull --ff-only
  ```
  Then `git status --porcelain -uall --ignore-submodules=none` on the main checkout is empty.
- [ ] **C6** Record the merge in the PM's memory file (sub-project, PR, merge SHA, carry-over count, hash claim).

### If a worker stops

A worker that is `gone` (crash, reboot, `/tmp` cleared) or has lost its thread after compaction is relaunched in the same worktree with the resume brief: `csd launch m2-NAME <worktree>` (or `csd adopt` with its session id from `~/.claude/projects/<encoded worktree path>/`), then:
```bash
/tmp/csd-workers/bin/m2-NAME send "Read .superpowers/brief.md. You are resuming: the spec, the plan under docs/superpowers/plans/, the ledger under .superpowers/sdd/ and the branch's commits are the truth. Report which phase and task you are in, then continue."
```

### The brief template

Write this to `.superpowers/brief.md` in the worktree, filled in. It carries what the worker's own memory would not.

````markdown
# M2 sub-project N: NAME — the worker's brief

You are the worker session for M2 sub-project N of Giga Grains. You are in the git worktree
`/Users/sethbc/src/giga-grains/.claude/worktrees/m2-NAME` on branch `m2-NAME`, branched from
`main`. The programme manager (another Claude session) drives you; the director (the human) is
reached only through it. Nothing you do is public.

## Read first, in this order
1. `CLAUDE.md` (the working rules; its "Build and test" section is how this tree builds).
2. `INTENT.md`: §5, §8, §14–§20, §22, §23 #64.
3. `docs/superpowers/specs/2026-10-29-m2-programme-design.md`: §6.N (your sub-project: what it
   delivers, what it owns, its exit), §7 (the rules every sub-project builds on), §8 (the
   carry-over), §12 (how you are run).
4. `docs/m2-carry-over.md`.
5. The M1 specs of the areas you touch, in `docs/superpowers/specs/2026-10-2*-m1-*.md`, for the
   form and for what exists.

## Setup
- `scripts/worktree-setup.sh --configure dev,asan,rtsan` first; it checks out the submodules.
- The project's practice, which your memory directory does not hold:
  - subagents: reviewers and decision-makers on the default model, implementers on Opus (`model:
    "opus"`); never haiku or sonnet;
  - CI runs through a draft PR (`gh pr create --draft`), never a bare branch;
  - the carry-over policy: fix minors, don't carry; touch a file, close its "when touched" rows;
    no net growth; count open rows at your start and your end;
  - the director decides behaviour and sound by forced choice; you decide engineering;
  - a golden WAV SHA-256 claim ("every M1 hash unchanged") is evidence; make it with
    `scripts/update-goldens.sh` before and after each DSP change.

## Phase A: the spec (stop at its end)
Write `docs/superpowers/specs/<today>-m2-NAME-design.md` in the form of the M1 specs: the
status table, intent, goals/non-goals/anti-goals, constraints, approaches, the how as decided,
INTENT changes (with each §23 row), verification, left to the builder. The programme design's
§6.N and §7 are the conversation you would otherwise have had; realize them, do not reopen them.
Two sections are mandatory:
- **Decisions for the director:** every behaviour or sound choice, as a forced choice with the
  option you recommend first and one line on what each costs. Nothing engineering-only here.
- **My calls:** the engineering choices you made, each with its reason.
Record the carry-over's open-row count at your start and the rows you will close. Name your
goldens, benchmark cases and the contents of `docs/listening/m2-NAME/`.
Commit the spec. End your turn with the line `SPEC READY` and the spec's path. Do not plan or
build until the programme manager tells you the director's answers.

## Phase B: the build
On the programme manager's go: record the director's answers in the spec, then invoke
`superpowers:writing-plans` on your spec (plan to `docs/superpowers/plans/<today>-m2-NAME.md`),
then `superpowers:subagent-driven-development` with its ledger under `.superpowers/sdd/`. Commit
often. Open the draft PR early (`gh pr create --draft --base main`), with the spec's path in its
body, and keep its description current: the final review's findings and the fix wave go there.
Before you report:
- `scripts/check.sh` over `dev,asan,rtsan` passes; paste its summary in the PR;
- CI is green on the head commit;
- `scripts/update-goldens.sh` run, the hash list in the commit that changed anything, every M1
  case's hash unchanged unless a commit says why;
- your benchmark cases run from `release`; `engine-sample` has no regressed ratio;
- the pack rendered into `docs/listening/m2-NAME/` with a README;
- the spec's status reads Built, amendments marked; INTENT's §23 row is in;
- the carry-over count at your end is at or below your start, closed rows marked with commits;
- a final review by a fresh reviewer subagent and its fix wave are done.
End your turn with `BUILD READY`, the PR number, and each item above as evidence.

## Phase C: the merge
You never merge, never push to `main`, never tag, never publish. If the programme manager asks
you to rebase onto `main`, do it with `--force-with-lease`, resolve conflicts within your own
additions, run everything above again and report `BUILD READY` again.

## Rules of the wave
- Edit only what §6.N says you own; a shared file (`Processor`, `ParamIds.h`,
  `tests/CMakeLists.txt`, INTENT) only by addition.
- Your parameters go in your reserved index block (design §7.1); patch version and chunks per
  §7.5. If you need something another sub-project is building, stop at the boundary and say so in
  your report; do not copy its code.
- Never edit a submodule. Never add a dependency that isn't pinned and AGPLv3-compatible.
- INTENT Part I is never yours to change; Part II changes carry a dated §23 row.
````

---

## Task 0: Pre-flight

**Files:**
- Create: the PM's memory file `m2-programme.md` in the main checkout's memory directory (`/Users/sethbc/.claude/projects/-Users-sethbc-src-giga-grains/memory/`), with a `MEMORY.md` line.

- [ ] **Step 1:** Prerequisites present: `which tmux claude gh node` prints four paths; `gh auth status` is logged in.
- [ ] **Step 2:** Consent for bypass-permission workers: if `~/.claude/.claude-session-driver-consent` is missing, ask the director to run `! <csd> grant-consent` (their consent, not the PM's). Expected afterwards: the file exists.
- [ ] **Step 3:** `$CSD prune` and `$CSD list` show no stale `m2-*` workers.
- [ ] **Step 4:** Create the PM memory file: the design's path, this plan's path, the wave state (`wave 1 not started`), and a table of sub-project → worker name, branch, PR, status. Add the `MEMORY.md` line.

## Task 1: Merge the programme PR

**Files:**
- Modify: nothing in the tree; the commit `451e9c7` on `m2-programme` already holds the design and the INTENT amendments. This plan joins it.

- [ ] **Step 1:** Commit this plan on `m2-programme`:
  ```bash
  git add docs/superpowers/plans/2026-10-29-m2-programme.md
  git commit -m "docs(plan): the M2 programme plan: the programme manager's procedure and the worker brief"
  ```
- [ ] **Step 2:** Push and open the draft PR:
  ```bash
  git push -u origin m2-programme
  gh pr create --draft --base main --title "M2 programme design and plan" --body "<the design's §1 summary; the INTENT amendments; this plan's path>"
  ```
- [ ] **Step 3:** Wait for CI (`gh pr checks --watch`). Expected: every leg green (docs-only; the goldens and tests are unchanged).
- [ ] **Step 4:** `gh pr ready` and `gh pr merge --merge --delete-branch`; `git checkout main && git pull --ff-only`.
- [ ] **Step 5:** Update the PM memory: `wave 1 ready to start`.

## Task 2: Wave 1, sub-project 1 (Layers and structure)

**Interfaces:**
- Produces, for wave 2: `ParamIds.h` with the next free index per scope recorded and the three reserved blocks as comments and a pinned test; the source interface with its mip-level accessor (one level); the Source-kind parameter with its whole enumeration; the patch version bump and migration; the layer chain of design §7.2; `gg-render` specs that address layers.

- [ ] **Step 1:** Run the procedure for N = 1, NAME = `layers`, first and only in its wave (C1 and C3 do not apply).
- [ ] **Step 2:** At A3, also check: C++23 is the spec's first task with its revert path (design §7.9); the thread-pool determinism test (§7.4) is named; the Macro 7 switch and its migration are in (§7.3); the three reserved blocks are in the spec's ID table (§7.1).
- [ ] **Step 3:** At B2, also require in the evidence: `gg-bench`'s new `engine-layers` and `engine-layers-full` cases run from `release` with their figures; the four-layer block-size and thread-pool invariance tests listed in the test output.
- [ ] **Step 4:** After C5, run `scripts/update-goldens.sh` on `main` once and keep its hash list as the wave-2 baseline the B5 checks compare against. Update the PM memory: `wave 2 ready`.

## Task 3: Wave 2, sub-projects 3, 2 and 4 in parallel

**Interfaces:**
- Consumes: Task 2's products.
- Produces, for wave 3: the complete parameter list (every M2 parameter exists after this wave; wave 3 adds none); `MEDI` v2 with the content-relative reference (sub-project 2); the preset-independent engine every preset will voice.

- [ ] **Step 1:** Run Phase 0 for N = 3 `engine`, N = 2 `sources`, N = 4 `extremes`, all branched from the same `main` (record its SHA in the PM memory).
- [ ] **Step 2:** Run Phase A for all three. Collect their forced choices and ask the director in as few messages as possible (A4): wait for all three `SPEC READY` lines before asking, unless one lags more than a day.
- [ ] **Step 3:** At A3 for N = 3, also check: the HQ kernel's scalar reference and bit-match test, the budget's weight table as a named constant, the overload tests at block sizes 1, 32, 64, 511 and 4096, the AVX2 task's timebox and keep rule (§7.10), the mipmap accessor's use. For N = 2: the `AUDI` codec choice with its exactness criterion (§7.5), the ring-length parameter (§7.2), the incremental pyramids as the last tasks (after 3 merges), I38's four operations. For N = 4: the Spectral Smear kernels instantiated for every tier after rebasing on 3, the limiter's default law equal to M1's soft limiter, delay and filters at rest.
- [ ] **Step 4:** Run Phase B for all three concurrently; poll each worker in turn.
- [ ] **Step 5:** Merge in order. N = 3 first: Phase C without C1/C3. Then N = 2: C1 (rebase onto the merged 3), B1–B9 again, C2–C6. Then N = 4 the same onto the merged 2.
- [ ] **Step 6:** After the last merge, `scripts/update-goldens.sh` on `main`; every M1 hash unchanged against Task 2's list, or the explaining commits found. Run `gg-bench --cases reference,engine-sample --quick` from `release` as a smoke check. Update the PM memory: `wave 3 ready`.

## Task 4: Wave 3, sub-projects 5 and 6 in parallel

**Interfaces:**
- Consumes: Task 3's parameter list and `MEDI` v2.
- Produces, for the close: the factory presets with their goldens; the content pipeline; every view; the packs of 1 to 6.

- [ ] **Step 1:** Before launching N = 5: ask the director to create the public `giga-grains-content` repository under their account (design decision 8) and to tell the PM its URL; record it in the PM memory. Sub-project 5's spec names it.
- [ ] **Step 2:** Run Phase 0 and Phase A for N = 5 `presets` and N = 6 `views`; one director message for both specs' forced choices where timing allows.
- [ ] **Step 3:** At A3 for N = 5, also check: the preset is a `GGST` container (§7.5), the floor of 40 presets and 8 MIDI-driven FX presets, the provenance record per sample (§7.8), the installers' Content component filled and tested (`ctest -L installer`), `gg-render --preset`, the identity and AU tests run on any clap-wrapper bump with rows 87, 88, 218 re-checked. For N = 6: the two-PR split named if the plan exceeds M1's Perform UI, the GPU spike timeboxed to one task ending in `docs/spikes/m2-gpu-canvas.md` and a §23 row, `gg-bench --canvas` at 8,192 grains with four lanes under 3 ms, rows 217 and 221 closed, I38's gestures.
- [ ] **Step 4:** Run Phase B for both; merge N = 5 first, then N = 6 with C1 and C3.
- [ ] **Step 5:** Update the PM memory: `wave 4 ready`, with the list of the six packs' paths.

## Task 5: Wave 4, sub-project 7 (Close)

- [ ] **Step 1:** Run Phase 0 and Phase A for N = 7 `close`. Its spec's forced choices include the gate's checklist shape and the host checks (design §7.11); its "Decisions for the director" also lists any `regressed` CI ratio found since M1 and the carry-over rows that would stay open.
- [ ] **Step 2:** Phase B, with these differences: the worker prepares the gate (one README across the six packs with the checklist, the host-check list for Live and Bitwig) and the idle benchmark run's instructions, then reports `GATE READY` instead of building further.
- [ ] **Step 3:** The director's sitting: the PM hands the director the gate README's path and the host-check list, and relays the verdicts and any faults to the worker. Faults go through the close's fix wave (design §10's last row) before anything else.
- [ ] **Step 4:** The idle benchmark run on the reference core: the PM asks the director for an idle machine window (nothing building, load average under 1.5), the worker runs every `gg-bench` mode from `release` and publishes to `docs/benchmarks/` with a README, as M1's close did.
- [ ] **Step 5:** The worker finishes: INTENT §20 marked, §16 figures, the carry-over at or below 8 open rows, `GG_VERSION_PRERELEASE` emptied, `docs/releases/v0.2.0.md`, then `BUILD READY`. Phase C merges it.
- [ ] **Step 6:** The release commit's tag, by the PM, following `docs/releasing.md` exactly: `scripts/release/check-version.sh v0.2.0` passes on `main`'s head, then the tag is pushed and CI's release job makes the draft, which stays unpublished. Verify: `gh release view v0.2.0 --json isDraft` is `true`.
- [ ] **Step 7:** A follow-up commit on a branch sets `GG_VERSION_PRERELEASE` back to `dev` and the version to 0.3.0; PR, CI, merge.

## Task 6: Programme close-out

- [ ] **Step 1:** `$CSD list --all` shows no live `m2-*` worker; `git worktree list` shows only the main checkout; `origin` has only `main`.
- [ ] **Step 2:** Update the PM memory and the main checkout's `m1-progress`-style memory: M2 closed, the PRs and merge SHAs, the eight open carry-over rows, what M3's brainstorm starts from.
- [ ] **Step 3:** Write a short lessons section into the PM memory: what the wave model cost in merges, which checks caught something, what the next milestone's programme should change.
