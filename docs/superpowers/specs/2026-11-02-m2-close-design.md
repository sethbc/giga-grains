# M2 sub-project 7: Close — design

| | |
|---|---|
| **Milestone** | M2 Giga (INTENT §20), sub-project 7 of 7, wave 4 |
| **Status** | **Draft for approval** (2026-11-02). Phase A of the close's brief: the consistency review (`docs/m2-consistency-review.md`) and the carry-over triage (§10) are written; §8's decisions wait on the director. Nothing is planned or built yet |
| **Spec authority** | `INTENT.md`; the program design (`docs/superpowers/specs/2026-10-29-m2-programme-design.md`, renamed to `…-program-design.md` by §6.6), whose §6.7 this document **amends** (§1) and realizes |
| **Builds on** | Sub-projects 1 to 6, all merged (PRs #15 to #20, #23 to #25; INTENT §23 #65, #66, #68 to #71); the CI changes (#28, #29; §23 #73 to #76) |
| **Starts from** | `main` at `0189dc9` on branch `m2-close`; `docs/m2-carry-over.md` with **80 open rows** (79 open plus row 330, closed in part) |
| **Waits on** | The director's answers to §8. The program manager's own fix of the Windows link race on dispatch run 41543261855 (two format targets writing one `.exp`/`.lib`, from #76) is outside this sub-project: it is not triaged here and nothing here touches it |

## 1. Why this sub-project, and how it amends design §6.7

The close turns M2's six merged sub-projects into the `v0.2.0` draft: one listening sitting over
six packs, the idle benchmark run, the release commit. The director added three deliverables on
2026-11-02, which come first, and the close's brief adds items handed over by the sub-projects and
the program manager.

**Design §6.7's Delivers, as amended here** (the director, 2026-11-02, relayed by the program
manager; items 1 to 3 are new):

1. **A full document consistency pass** ("make sure that we do a full consistency pass on the
   documents and raise any internal inconsistencies in the decisions that have been made thus
   far"): `docs/m2-consistency-review.md`, with every drift and defect fixed here and every
   conflict put to the director (§6.1, §8.1).
2. **The carry-over burned down as far as possible** ("clean down as many of the carry-over items
   as possible"): every open row triaged FIX, CLOSE-OBSOLETE, KEEP-BLOCKED or DIRECTOR (§10), and
   every FIX built (§6.2). This replaces §6.7's "the carry-over burned down (§8)".
3. **A from-scratch build setup** ("is there a one liner we can put in the readme to install the
   software required to build this program"): a README "Building from source" section with one
   line per OS, backed by checked-in scripts that CI runs (§6.3).
4. The possible ring-reader race found on PR #29's run 41534801566 (§6.4; new row 403).
5. The items handed over: the editor header split (row 331), the debug assert for the Rosetta
   fault (row 234), the local auval registrar fault, row 402, the layers pack's gate questions, the
   `linux-gcc13` baseline remake, the full benchmark's run length (§6.5).
6. The US-spelling sweep, PR #22's work taken over (§6.6).
7. §6.7 as written: listening gate #2 on the six packs, one README and one checklist, with the
   host checks for Live and Bitwig (§6.7); the idle run of every `gg-bench` mode, published, X
   re-measured (§6.8); CI's report-only gate read for every model (§6.8); INTENT §20's M2 row
   closed and §16's figures updated; `GG_VERSION_PRERELEASE` emptied and `docs/releases/v0.2.0.md`
   written (§6.9). **The program manager tags `v0.2.0`**, never this sub-project.

**Design §6.7's Exit, as amended:** the director passes the gate; no regressed ratio stands
unexplained; every conflict of the consistency review has the director's answer recorded as a
dated §23 row; every FIX row of §10 is built or, if the build showed it can't be, moved to another
class with the reason; the carry-over's open count before and after is in the final report;
`main` returns to `0.3.0-dev`. "The carry-over count is at or below its §8 target" is struck: the
count rules were retired (§23 #67; the consistency review's R-50).

## 2. Goals

- The documents say what was decided: every drift fixed, every conflict decided by the director.
- The carry-over holds only what is outside our control or a record the director asked to keep.
- A newcomer builds and tests the project on macOS, Windows or Linux from one pasted line per OS,
  and CI keeps that line honest.
- The ring reader's possible race explained and, if real, fixed with a test that fails without the
  fix.
- One sitting gives the director everything M2 needs from ears and hosts.
- `v0.2.0`'s release commit, with the published figures, ready for the program manager's tag.

## 3. Non-goals and anti-goals

- **Non-goals:** new features; a clap-wrapper, sst-clap-helpers or clap-validator bump (none has a
  fix for our rows, §10); making the CPU gate blocking (1.0.0, §23 #60, #61); calibrating the
  budget's weights (M5, §23 #66; row 307); the Windows link race (the program manager's).
- **Anti-goals:** resolving a conflict silently; closing a row as obsolete without verifying it at
  HEAD; moving a golden WAV hash without naming the case and the reason; editing Part I without the
  director's sign-off; renaming a third-party identifier (JUCE's `Colour`, `getCentre`,
  `ScopedJuceInitialiser_GUI`) or anything in a submodule in the spelling sweep.

## 4. Constraints

CLAUDE.md's non-negotiables bind every change. The ones that bite here:
- Every DSP or engine change (rows 302, 365, 370, 381, 363's stray mode, 306's scrub, 234's
  counter) ships with unit tests, `scripts/update-goldens.sh` before and after with the hash list,
  and the determinism tests; the audio thread stays allocation-, lock- and syscall-free and
  RTSan-clean.
- Part II changes carry a dated §23 row in the same change; Part I changes (K-4 option 2, K-6
  option 1) only with the director's sign-off, which an answer to §8 is.
- Models: no Fable, ever; `model` named on every dispatch; opus where superpowers' Model Selection
  says "the most capable model" (the whole-branch final review included), otherwise haiku for
  mechanical work and sonnet for integration and as the reviewer floor.
- One `check.sh` gate per machine; the AU lock (`/tmp/gg-au.lock`) around every AU install and
  validator run; published timings only in a window the program manager grants.
- Merges of `origin/main` by merge commit; no rebase, no force-push; CI through the draft PR.

## 5. Approaches

**The consistency pass.** (a) One reviewer reads everything; (b) four reviewers by document
family, one reconciler that verifies and dedupes. Chosen: (b), on opus throughout, as the brief
asks: INTENT internally; the build, CI and release docs against the files they describe; the
program design, plan and specs 1 to 3; specs 4 to 6 and the six listening packs. The reconciler
verified every conflict and every unsure item at its source and spot-checked 28 of 71 drifts; 84
raw findings became 8 conflicts, 2 defects and 69 drifts.

**The triage.** Four opus triagers by row family, each verifying a row's claims at HEAD (line
numbers, later commits, upstream histories through `gh api`) before classing it. The results are
§10's table.

**The spelling sweep.** (a) Merge PR #22's branch and finish the sweep on top; (b) redo it on the
close's head and close #22 as superseded. Chosen: (b) (§9): #22's base is 369 commits behind `main`,
four sub-projects have rewritten the files it touches, and its 93 fixes are a scripted rename the
full sweep repeats anyway.

**The build setup.** (a) README text alone; (b) scripts the README calls, which nothing runs;
(c) scripts that CI's own install steps run, so the one-liner and CI install the same things. Chosen:
(c) (§6.3, §9).

**The gate.** One README across six packs or six READMEs read in turn: the design (§7.11) already
says one list; the shape of that list is the director's choice (§8.3).

## 6. The how, as decided

### 6.1 The consistency review and its fixes

`docs/m2-consistency-review.md` lists every finding with its sources, class and resolution, and
keeps each reviewer's own ID beside the reconciled one. Its 69 drifts (R-1 to R-69) and 2 defects
(X-1: CI's Test step skipped when the libstdc++ check fails; X-2: the Rosetta golden loop leaves
out the 48 preset goldens and passes no `--content-dir`) are built in the close's plan, grouped by
the file they fix. Its 8 conflicts (K-1 to K-8) are §8.1's questions. Once the director answers,
each answer becomes a dated §23 row (numbered at the build), the document's "Resolution" fields
read the answer, and the text each answer touches is fixed in the same change. Two drifts the
director's rules make concrete:
- **R-38, the model rule,** recorded in no repository file: CLAUDE.md's "Working style" and the
  program plan's Global Constraints and brief template say it (no Fable; opus for "the most
  capable model"; haiku mechanical; sonnet integration and reviewer floor; `model` always named),
  citing the director's 2026-10-30 call as the close's brief relays it.
- **R-50, the count rules:** design §6.7's exit (amended in §1 here), plan Task 5 Step 5, and every
  other count-rule sentence the review lists, struck or marked retired with §23 #67.

The drift fixes in the listening packs that need new renders (the engine pack's `04`, `05` and `07`
predate the raised-cosine steal fade and the ring pyramids; D-15 of the review) are made in §6.7's
gate preparation, so the director hears `main` as it is.

### 6.2 The carry-over burn-down

§10 classes the 80 open rows: **58 FIX, 3 CLOSE-OBSOLETE, 13 KEEP-BLOCKED, 1 kept record, 5
DIRECTOR**, and adds row 403 (FIX). Each FIX is built in one of these task groups (the plan sets their order and splits;
groups with no file in common run in parallel task worktrees):

| Group | Rows | Touches the audio path? | Goldens |
|---|---|---|---|
| A. Media and the loader | 306, 340, 341, 342, 345, 350, 389, 401 | 306's scrub is in the decoder, off the audio thread | none can move (no golden file holds a non-finite sample) |
| B. The ring reader and the session | 403 (§6.4), 348, 351, 353, 354, 355, 384, 400 | no (main-thread reader, the exchange's offers) | none |
| C. The sweep and the reverb onset | 302, 303, 309 | 302: the reverb's input fades in over 10 ms when it turns active after the first sample | none (no case raises Amount from 0 after its start; the sweep's `kReverbOnset` exemption is deleted) |
| D. The kernels' live path | 343, 344 | bit-identical refactors | none (hashes checked) |
| E. Spectral hygiene, then the window, then DC | 364, 366, 367, 368, 373; then 381; then 365 | 381: a Spectral layer over its ring follows the loop window and the splice commands (D-specwindow option 1); 365: DC and Nyquist reconstruct at unity | 381: none (no golden sets Loop or Splice on a Spectral layer); **365 moves five WAVs** (`inst-spectral`, `fx-spectral-freeze`, `golden_preset_fx-06-spectral-mist`, `golden_preset_fx-20-melting-clock`, `golden_preset_inst-21-smoke-cathedral`), DC only, event logs unchanged |
| F. The FX units' edges | 361, 363, 369, 370, 371 | 363: a stray limiter mode is the identity, agreeing with `steady()`; 370: a filter type change inside a fade turns the fade (A to B to A) | none (no golden changes a filter type twice within 10 ms or sets a stray mode) |
| G. The tools and the benchmarks | 346, 347, 349, 358, 372, 374, 148, 304, 305 | no | none |
| H. Test hygiene | 357, 359, 360, 362, 375, 376, 377 | no | none |
| I. The editor | 232, 330 (a), 331, 383, 385, 386, 388's 18 items; 333 after §8.2 | no (385 sizes `LivePeaks` at prepare) | none |
| J. The Rosetta fault | 234 | a debug assert and an always-on fault counter in `renderLayerPiece` (§6.5) | none |
| K. The listening packs | 308, 398; the gate's questions for 352, 387, 394, 395, 397 | no | none |

The CLOSE-OBSOLETE rows (310, 390, 392) are marked **Closed** with their evidence in the first
docs task. A KEEP-BLOCKED row's "When" column is rewritten to name what unblocks it, as §10 says.
After the gate, rows 352, 394, 395 and 397 close (heard and accepted, or fixed in the fix wave),
and each DIRECTOR row is built or closed as its answer says. Expected open after the close: the
thirteen KEEP-BLOCKED rows less the four the gate settles, plus row 382 (the record), row 234 (a
watch row, §6.5) and row 330 (its M5 remainder, §9): **about 12**, plus any DIRECTOR row an answer
keeps open; the final report gives the exact count.

### 6.3 Building from source

Three scripts, each idempotent, each printing what it installs and what it found already present:
- `scripts/bootstrap-macos.sh`: checks the Xcode command-line tools (asks for
  `xcode-select --install` and exits if missing: it opens a dialog that can't be scripted), then
  installs with Homebrew (installing Homebrew itself if missing) `cmake`, `ninja`, `git` and `llvm`.
  Homebrew's `llvm` is 23.1.4 today, so one formula gives `format.sh` its clang-format 23 and the
  `rtsan` preset its LLVM 20 or newer.
- `scripts/bootstrap-linux.sh`: Ubuntu 22.04 and 24.04 (and Debian-family systems with the same
  packages). gcc-13 (from the `ubuntu-toolchain-r/test` PPA on 22.04, the archive on 24.04), CMake,
  `ninja-build`, git, curl, unzip, `file`, `binutils`, the twelve JUCE development packages that
  CI's pinned `prepare-for-juce` action installs (`libfontconfig1-dev libasound2-dev libx11-dev
  libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev
  libfreetype6-dev libglu1-mesa-dev libjack-jackd2-dev`), `libxi-dev`, and `xvfb` for headless GUI
  tests. `--with-format` adds clang-format 23 the way the Format workflow gets it (a venv and the
  hash-pinned wheel). Run as root or through `sudo`.
- `scripts/bootstrap-windows.ps1`: installs with winget `Kitware.CMake`, `Ninja-build.Ninja`,
  `Git.Git` and Visual Studio 2022 Build Tools with the C++ x64 tools, a Windows SDK, and the C++
  Clang (ClangCL) components; `-Verify` installs nothing and checks each with `vswhere` and the
  PATH. A companion, `scripts/dev-shell-windows.ps1`, enters Visual Studio's x64 developer shell
  (`Enter-VsDevShell -Arch amd64`) and puts `VC\Tools\Llvm\x64\bin` first on the PATH, as
  `windows-clangcl` needs (§23 #76).

Each script takes `--clone DIR` (`-Clone DIR`): it then clones the repository with
`--recurse-submodules` into DIR and runs `cmake --preset dev`, `cmake --build --preset dev` and
`ctest --preset dev` (on Windows, inside the developer shell). The README's "Building from source"
gives one line per OS that fetches the script from `main` and runs it with `--clone`, a second form
for a reader who wants to read the script first (clone, then run it), and the preset table's
pointer to CLAUDE.md.

**CI keeps them honest** (§9): the Linux leg's install steps become `scripts/bootstrap-linux.sh`
(replacing `prepare-for-juce`'s step and the two `apt-get` lines; ccache stays a CI-only line); the
macOS leg's become `scripts/bootstrap-macos.sh --ci` (which skips `llvm`, the leg doesn't use it);
the Windows leg runs `scripts/bootstrap-windows.ps1 -Verify` on the hosted image, whose Visual
Studio is preinstalled (a winget install of the Build Tools takes longer than the leg's budget). A
nightly-only job runs `bootstrap-linux.sh --clone` in a fresh `ubuntu:22.04` container, so a
package the hosted image happens to carry can't hide a missing one. A `scripts` test checks that
the README's three lines name the scripts and options that exist.

### 6.4 The ring reader's possible race (new row 403)

Run 41534801566 failed "the ring reader's copies match what the audio thread wrote"
(`tests/plugin/persist_tests.cpp:1817`, `bad == 1`; reads 1912, trimmed 15, swaps 18, adopted 18,
4 of a full ring) under Rosetta with three parallel shards. The triage (T2) found that the count
can only be the data check (`:1796`–`:1803`): the contiguity check can't fire for this request, and
row 353's path returns nothing. Phase B treats it as a bug, by superpowers:systematic-debugging:
1. Make the test say what it saw: on the first mismatch, record the frame, the value found and the
   value expected, the swap and adoption counts, and whether the ring was full. A value one lap
   newer means an overwritten frame was accepted; older or zero means a gap in a swapped storage;
   anything else means memory reuse.
2. Reproduce: loop the test natively and under `arch -x86_64` (a local `mac-universal` tree) with
   the machine loaded, and run the `[threads]` tests in a ThreadSanitizer tree (a local
   configuration, `-fsanitize=thread`, editor off).
3. Fix the cause with a test that fails without the fix (row 351's `adoptRing` hook, built in group
   B, may be what makes it deterministic). If no run in the budget the plan sets reproduces it,
   the diagnostics stay in the test, row 403 stays open as a watch row naming them, and the final
   report says so.

### 6.5 The items handed over

- **Row 331, the header split:** `ui/src/Editor.cpp`'s header into `ui/src/views/Header.{h,cpp}`,
  every `header.*` component ID kept; the GUI tests and `gg-snap`'s 32 PNGs show it unchanged.
- **Row 234, the Rosetta fault:** in `Processor::renderLayerPiece`, where `renders !=
  l.enabledGiven`, two `assert`s: that `renders` agrees with the on ramp, and that the flip is on
  the 32-frame grid, at a ramp's end, or after a jump. `assert` is compiled out of the Release
  `mac-universal` slice where every fault occurred, so an always-on counter of the same two
  conditions (a relaxed atomic increment on a branch that is never taken in a correct run) is read
  by the tests that flaked and fails them with its diagnostic. The exit-case test
  (`engine_tests.cpp:2153`) compares its stolen and dropped counts before its samples, so a
  recurrence says which diverged. Run 41499519821's fault is added to the row. The row stays open
  as a watch row until a recurrence names the fault.
- **The local auval fault** (`LSRegisterURL` error -10811 for the installed dev component): under
  the AU lock, unregister the stale component (`lsregister -u`), remove it, reinstall from
  `release`, restart the registrar, and run the three `auval` identities; `docs/releasing.md`'s
  troubleshooting gains the recipe.
- **Row 402:** §8.2.
- **The layers pack's two questions** (the layers' sum sounds quieter; Env's release barely
  audible): in the gate's checklist (§6.7).
- **The `linux-gcc13` baseline:** remade from three full-length GCC 13 runs (dispatches on the
  close's branch, after its last engine change) with `gg-bench --make-baseline … --merge`, each
  `(replaced)` entry compared with the old one. The same pass handles rows 304 and 305 for Windows
  once the program manager's link fix lands; models drawn fewer than three times keep the 0.10
  floor and stay with row 144.
- **The full benchmark's run length:** Linux's full step took 26 minutes on run 41525070644; the
  mac and Windows legs reached the old 30-minute limit there. If dispatch 41543261855 or a later
  nightly shows any leg's step above 45 minutes (three quarters of #73's 60), `engine-live-long`
  runs at block 256 alone (one 300 s warm-up in place of three). It is report-only with no baseline
  entry, so no gated ratio changes; INTENT §16 and CLAUDE.md say so. Otherwise nothing changes.

### 6.6 The US-spelling sweep

On the close's head, after the drift fixes: the program design and plan renamed to
`2026-10-29-m2-program-design.md` and `2026-10-29-m2-program.md` with every reference updated, and
every British spelling in first-party prose, comments, user-visible strings, test names and our
own identifiers and file names made American (program, behavior, color, center, favorite,
neighbor, -ize, canceled, labeled, artifact, gray, license as a noun). Never: a submodule; a
third-party API name; quoted text from outside the project (license texts, upstream messages); a
key or tag a saved format reads, unless no fixture or golden holds it (the builder lists each kept
one); `docs/m1-carry-over.md`, M1's frozen record. PR #22 is closed as superseded with a comment
naming the close's commit. Every golden WAV hash is unchanged (checked before and after).

### 6.7 Listening gate #2

Prepared after the fixes, before the idle run, and reported with `GATE READY`:
- **The renders refreshed on the close's head:** each pack's renders whose source moved since the
  pack was made are rendered again (the engine pack's `04`, `05` and `07`; the Spectral renders
  after group E; the presets pack's table on `release`, row 398), and the pack says so.
- **One README,** `docs/listening/m2-gate/README.md`, in the shape §8.3 decides, holding every open
  question from the six packs (the review's appendix lists them per pack, with lines), the
  carry-over's gate rows (352, 387, 394 to 397, and 402 if the director wants to hear it first), the
  views' 16 snapshots, and the host checks (§8.4), each item answerable in a word.
- The director's verdicts become the fix wave's tasks before anything else; an accepted item closes
  its row.

### 6.8 The idle benchmark run and the CI gate read

In the program manager's idle window (`NEED IDLE MACHINE`): every `gg-bench` mode from `release`
on the reference core (the full default run, `--canvas`, `--space`, `--chain` with row 372's new
Spectral stage), published to `docs/benchmarks/2026-11-…-m2-*.json` with the README's table beside
M1's; X re-measured at blocks 32, 64 and 256 and compared with M1's 19.5% (block 256) and the
sub-projects' idle figures; HQ's ratio to Normal reported for row 307 without moving the weights.
INTENT §16's figures follow. **The CI gate read:** the latest full-length run of every leg and
model read against its baseline; each `regressed` ratio is explained or fixed. Known now: run
41525070644 (Linux, EPYC 9V45) shows `engine-layers-full` at blocks 32 and 64 (1.534 against
1.481, 1.845 against 1.326) and `engine-sample-sse` at block 64 (1.2565 against 1.2556) regressed
against single-run entries from `630d740`, before sub-projects 2 and 4 merged; the close measures
`engine-layers-full` at `630d740` and at its head locally to tell code from noise.

### 6.9 The release commit

Last, after the final review: INTENT §20's M2 row marked closed, §16's figures, CLAUDE.md's "Where
we are", `GG_VERSION_PRERELEASE` emptied in `CMakeLists.txt`, `docs/releases/v0.2.0.md` written as
`docs/releasing.md` says, and `scripts/release/check-version.sh v0.2.0` passing. The program
manager tags; a follow-up PR from this branch's successor sets 0.3.0 and the `dev` suffix.

### 6.10 Order of work

1. The spelling sweep's renames (so later edits land on the final file names), then the drift and
   defect fixes and the director's answers, then the rest of the sweep.
2. Groups A to J (§6.2) and §6.3, §6.4, §6.5, in parallel where files allow.
3. The gate's preparation (group K, §6.7): `GATE READY`. The director listens; the fix wave.
4. The idle run (`NEED IDLE MACHINE`), the baselines, the CI gate read.
5. `check.sh` over `dev,asan,rtsan`, CI green, the validators locally, the opus final review and
   its fix wave, the release commit: `BUILD READY`.

## 7. Determinism and real-time safety

- **302** fades the reverb's input with a raised cosine over 480 samples (10 ms at 48 kHz) from
  the first active chunk after an idle one, on the sample clock: block-size invariant, no
  allocation.
- **365** scales two bins by a constant before the inverse: deterministic, allocation-free.
- **381** changes where a Spectral frame starts reading (inside the loop window) and which layers
  take splice commands: the window's law is Live's, already block-size invariant.
- **370** turns a fade from its current value: per-sample state the filter already holds.
- **363** maps a stray mode to the identity: a constant table.
- **234**'s counter is a relaxed atomic increment on a branch never taken in a correct run; its
  asserts are debug-only.
- **385** sizes `LivePeaks` at prepare, off the audio thread.
- Every other FIX is off the audio thread, test-only or a refactor whose hashes are checked.
- The determinism tests and `[rt]` tripwire run on every group that touches `core/` or `engine/`.

## 8. Decisions for the director

Each is a forced choice; option 1 is the recommendation. The consistency review gives each
conflict's sources in full.

### 8.1 The consistency review's conflicts

- **K-1, the restore criterion.** Must a restored session match the saved session's continued
  render within the goldens' tolerances (§23 #64), or only render the same on every restore (ruling
  SP-T7e, as built)?
  1. Amend the criterion to what was built: every restore renders identically and restores the
     saved frames bit for bit; the clock and grain index restart. *Cost:* a reopened frozen pad
     replays the same material with new grain draws, never the saved session's continuation.
  2. Keep it and add the check. *Cost:* a sparse cloud may fail it; meeting it exactly needs the
     clock and grain index in the state, a format change.
- **K-2, a click entering Peak mid-sound** (24.4, outside D5b's "nothing wider"; closed row 378).
  1. Accept it as part of Peak's zero-attack law (D5b); the sweep exempts the mode fade by samples,
     not by where calls fall. *Cost:* the extremes anti-goal "no click when the limiter's mode
     changes" is relaxed for Peak.
  2. A defect: inside a mode crossfade Peak's gain falls no faster than the fade. *Cost:* a limiter
     DSP change in the close; `fx-limiter` and the presets using Peak re-checked by ear.
- **K-3, INIT keeps Ring Length** (the controller's 2026-11-02 ruling) against D8's "a fresh
  instance, bit for bit".
  1. Ratify it as D8a: INIT keeps each layer's Ring Length (memory, not sound). *Cost:* INIT differs
     from a fresh instance when a ring isn't 16 s.
  2. INIT resets Ring Length to 16 s. *Cost:* INIT resizes rings on the main thread, can be refused
     by the memory cap, and cuts a held ring's audio past 16 s.
- **K-4, the overload ladder's order.** Part I §8.4 says "lower the tier, then drop, then steal";
  the code, by D4, steals the oldest grain past half its window before dropping.
  1. Confirm the escalation reading (§8.4 orders the rungs by load, D4 decides between drop and
     steal): §14's list and CLAUDE.md's non-negotiable gain the clause; no Part I edit. *Cost:* a
     non-negotiable's wording grows.
  2. Amend Part I §8.4 to the test order. *Cost:* a Part I edit.
- **K-5, which held rings a session saves.** D1: "a ring is saved when it is held"; the controller
  narrowed it to layers whose base Source builds to Live (§23 #68).
  1. Ratify the narrowing. *Cost:* a frozen Sample layer's ring is lost on reopen.
  2. Save every held ring, Sample layers included. *Cost:* up to 115 MB a layer at 300 s of audio no
     grain reads unless the kind changes; the 32 MiB embed cap fills sooner.
- **K-6, Part I §5's "live buffers … saved with the session"** against D1's held rings only.
  1. Amend Part I §5: "saved with the session when held (frozen or looped)". *Cost:* a one-clause
     Part I edit.
  2. Keep Part I; §18 says §5 means the held case. *Cost:* Part I over-promises to a reader of Part
     I alone.
- **K-7, "a merge, not a rebase"** (the program manager's ruling, #68, #69) against #64's "each
  rebased and re-verified".
  1. Ratify: the design and plan say "rebases on, or merges, the merged result, then re-verifies".
     *Cost:* none; M2's history stays non-linear.
  2. Keep "rebase" as the rule and record M2's merges as departures. *Cost:* the rule keeps stating
     what wasn't done.
- **K-8, rows 118 and 119 moved on the program manager's word** (plan A3 wants the director's too).
  1. Ratify; record it in #65. *Cost:* none.
  2. Amend plan A3 so the program manager alone may move an item that shrinks a later sub-project.
     *Cost:* the director loses sight of scope moves.

### 8.2 The carry-over's DIRECTOR rows

- **Row 402, what undo of a preset recall restores** (today: the values and routes; the samples the
  preset loaded and its name stay).
  1. Keep "values and routes", recorded as decided (INTENT §17, the presets spec). *Cost:* after an
     undo the strip shows the recalled name, edited, over the recalled samples (accurate: they are
     still loaded); a recall over a captured ring sample can't be undone.
  2. Also restore the name. *Cost:* M; the edited mark must still be forced when the recall changed
     a sample.
  3. Restore the name and the samples. *Cost:* L; an undo re-decodes files (and can fail on a
     missing file or the memory cap) or holds old samples in up to 200 undo entries against the
     cap, with a rule for dropping them.
- **Row 333, the route list auto-selects a route an undo or a load added.**
  1. Auto-select only when exactly one route appeared (an undo, a redo, a drop), never on a load.
     *Cost:* S.
  2. Keep it as built (any added route is selected). *Cost:* a preset or state load selects a route
     the user didn't touch.
  3. Never auto-select an added route. *Cost:* an undo of a route's removal leaves it unselected.
- **Row 393, VSCO 2: CE's request for credit and not to sell the samples directly** (not binding
  under CC0; credited in `PROVENANCE.md` and `THIRD_PARTY_NOTICES.md`; INTENT §15 records it).
  1. Close the row as recorded. *Cost:* the question returns only if the project is ever sold with
     the content, which INTENT doesn't plan.
  2. Keep it open until a decision about selling. *Cost:* a row that may never close.
- **Row 396, preset names and tags** (all 24 FX presets tagged `live-input`; "Horizon" in both
  products' names; the factory order freezes at 1.0.0).
  1. Keep the names and tags as they are; the director may still strike words at the gate.
     *Cost:* none now.
  2. Strike `live-input` from the FX presets and rename one "Horizon" now. *Cost:* the affected
     preset goldens move and the browser's tag list changes.
- **Row 387, the views' new user-visible strings** (eight, listed in the row).
  1. The director approves or edits them at the gate, as checklist items. *Cost:* a few minutes of
     the sitting.
  2. Hold them for a full text pass before M5. *Cost:* the row stays open through M3 and M4.

### 8.3 The gate's checklist shape

1. **One README, one numbered checklist** in sections by pack (layers, engine, sources, extremes,
   presets, views), then the carry-over's gate rows, then the host checks; each item answerable
   in a word (yes, no, or a pick), an unanswered item standing as "accept". *Cost:* one long sitting
   of about two hours, nothing split.
2. Six per-pack checklists read in order, plus a host-checks file. *Cost:* the same listening, with
   the cross-pack questions (the morph's level, Spectral over a ring) asked twice or in the wrong
   pack.
3. Two sittings: ears first (the packs), hosts second. *Cost:* a second round-trip through the
   program manager before the idle run.

### 8.4 The host checks for Live and Bitwig (design §7.11)

1. **A focused matrix:** Live 12 with the VST3 and the AU, Bitwig with the CLAP and the VST3, both
   products in each (eight cells), each cell one short script: the scan and load; the factory
   presets in the host's list and one loaded; the editor opened in both themes and resized 960 × 600
   to 640 × 360; one parameter automated; the project saved and reopened with a frozen ring; notes
   into the instrument and audio through the FX; the frame-time readout at `GG_STRESS_GRAINS=8192`
   (views' exit); the host's thread pool seen or not (the thread-pool survey). About 45 minutes.
   *Cost:* the standalone apps are checked only by the agents' tests.
2. Every format in both hosts plus both standalone apps (twelve cells). *Cost:* about 90 minutes.
3. One smoke per host (load, one preset, save and reopen). *Cost:* about 20 minutes, and the
   presets-in-four-formats exit (presets spec §1) is left unmet.

## 9. My calls

- **The spelling sweep is redone, not merged** (§5): #22 is 369 commits behind and its files have
  been rewritten since; the sweep's scripted renames repeat its 93 fixes. #22 is closed as
  superseded.
- **CI runs the bootstrap scripts** in its own install steps (Linux, macOS), verifies the Windows
  prerequisites with `-Verify`, and runs a nightly fresh-container Linux bootstrap: the only way the
  README's lines can't rot. The Windows Build Tools install itself is not run in CI (it outlasts the
  leg's budget on a hosted image that already has Visual Studio).
- **Row 148 is built now,** ahead of #60's 1.0.0 date: requiring a `verdict: N of` line beside exit
  3 hardens the report-only step and doesn't make the gate blocking.
- **Row 234 gets an always-on fault counter beside the debug-only asserts:** the asserts alone can't
  fire in the Release slice where every fault occurred.
- **Row 365's fix moves five golden WAVs** (DC and Nyquist reconstructed at unity, as the Spectral
  kind's law says; a change below about 6 Hz, inaudible); each moved hash is named in its commit.
- **Row 302's remedy fades the reverb's input in,** rather than keeping the reverb running at Amount
  0: it costs nothing while the reverb is idle and moves no golden.
- **Row 370 builds the turn the extremes spec describes** for A to B to A, and amends the spec so a
  change to a third type still waits.
- **Row 386: the lane follows the engine** for the Splice parameter's selection (SP-T1a/b), and the
  lane's Capture takes the span the engine plays; a capture's own `splice` argument keeps SP-T8a.
  Both rulings stand; the lane mixed them.
- **Row 330's remainder:** (a) a loaded out-of-range depth widens the slider instead of being
  clamped on first touch (FIX); (b) drag-and-drop is tested headlessly already (closed); (c) the
  stock JUCE widgets go with M5's theme work (§23 #72), so the row stays KEEP-BLOCKED on M5 for (c)
  alone.
- **Row 372 is measured, not changed:** a report-only Spectral stage in `--chain`, timed at the idle
  run; a large figure goes to the director then.
- **Row 307's weights don't move** in the close: M5 calibrates them (§23 #66); the idle run reports
  HQ's ratio.
- **The `engine-live-long` shortening** happens only if a leg's step passes 45 minutes (§6.5).
- **The ThreadSanitizer tree is local** (§6.4); a TSan CI job is proposed to the director only if it
  finds a real race.
- **Each conflict's answer is its own dated §23 row**, beside the close's own row.

## 10. The carry-over triage

The four triage reports (the evidence for each row: file:line, commits and upstream checks) are
in `docs/superpowers/specs/2026-11-02-m2-close-evidence/triage-T1.md` to `-T4.md`, beside the four
consistency reviewers' raw reports (`review-*.md`; their appendices hold CI's per-OS prerequisites,
which §6.3 uses, and each pack's open gate questions, which §6.7 uses). They are summarized here. Before: **80 open**. Classes: **FIX 58, CLOSE-OBSOLETE 3, KEEP-BLOCKED
13, kept record 1, DIRECTOR 5**; new row 403 is a FIX.

| Row | Class | Reason or plan | Group |
|---|---|---|---|
| 85 | KEEP-BLOCKED | Our sst-clap-helpers pin is its HEAD; `clap_juce_shim.h` still lacks `<string>`. Unblocked by an upstream commit that adds it | – |
| 87 | KEEP-BLOCKED | clap-wrapper's `next` (15 commits past our pin) and v0.17.0 don't touch `_current_program_name`, `SaveState` or `RestoreState`; pinned by the AU tests. Unblocked by a clap-wrapper fix | – |
| 88 | KEEP-BLOCKED | `SaveState` still doesn't drain `SetParameter`'s queue; the value never reaches our code. Unblocked by a clap-wrapper fix | – |
| 144 | KEEP-BLOCKED | The blocking gate, scheduled for 1.0.0 (§23 #60, #61) | – |
| 148 | FIX | Built now (§9): CI's warning branch also requires a `verdict: N of` line | G |
| 184 | KEEP-BLOCKED | `held_` is one bit per (channel, key); M3's Voice scheduler (§20) | – |
| 218 | KEEP-BLOCKED | clap-wrapper's AUv2 `SupportsTail()` still returns false; we can't override the wrapper's class | – |
| 232 | FIX | Our workaround: `EditorPlugin::guiDestroy` replaces the shim when it kept an editor it never showed | I |
| 233 | KEEP-BLOCKED | clap-validator 0.4.1 is still its latest release and still demands a leading `/`. Unblocked by a release that accepts `C:\...` | – |
| 234 | FIX | The asserts and the always-on counter (§6.5); run 41499519821 recorded; the row then stays as a watch row | J |
| 302 | FIX | Still clicks (`kReverbOnset` still exempts it): fade the reverb's input in over 10 ms; delete `kReverbOnset` | C |
| 303 | FIX | `kGuardFrames` now has three copies of 15 and `unit` two: one public constant, one `unit`, the copies deleted | C |
| 304 | FIX | Delete the stale SSE-era EPYC 9V74 entry; re-added only from three runs (or with row 144) | G |
| 305 | FIX | The baseline pass: three full-length runs per x86 leg on the close's engine code (Windows after the program manager's link fix) | G |
| 306 | FIX | Zero any non-finite decoded sample in `decodeAudioFile` and the ring audio reader | A |
| 307 | KEEP-BLOCKED | HQ's weights stay provisional until M5's calibration (§23 #66, §20's M5 exit); the idle run reports the ratio | – |
| 308 | FIX | The engine pack's README items (the method of its spectral figures, `02`'s RMS spans, HQ's floor at 0.67); its question goes into the gate | K |
| 309 | FIX | The sweep driver's latent gaps, now that 302 changes the driver (`sizeBase_`, the `LivePeaks` plant, the inline lines in five TUs) | C |
| 310 | CLOSE-OBSOLETE | Confirmed by CI with `a9e82d3` (three tries) after `5f1f9b5`; green on mac-universal since | – |
| 330 | FIX (a) | (a) a loaded out-of-range depth widens the slider; (b) closed, tested headlessly; (c) stock widgets: M5's theme work (§23 #72), so the row stays KEEP-BLOCKED for (c) | I |
| 331 | FIX | The header split (§6.5) | I |
| 333 | DIRECTOR | §8.2 | I |
| 340 | FIX | Clamp the decode bound to `kDefaultDecodeBound` once in `decodeInput` | A |
| 341 | FIX | `MemoryBudget.cpp`'s local `free` renamed; the rest is fine at HEAD | A |
| 342 | FIX | `encodeState`'s asserts excuse a missing-audio flag only for an embedded entry whose payload is absent | A |
| 343 | FIX | Drop `liveRun`'s unused `r0` | D |
| 344 | FIX | `LiveSource::windowAt(head)`, one `spliceAt` per test; bit-identical | D |
| 345 | FIX | `collectOne` returns the path and its text; a test collects into an unwritable folder (the `contentPath` hardening is in `2a9746c`) | A |
| 346 | FIX | One `warmUpSecondsFor` helper owns `--quick`'s cap | G |
| 347 | FIX | `gg-render` checks the cap before it allocates a capture | G |
| 348 | FIX | `plugin/src/RingReader.{h,cpp}` split out of `MediaSession.cpp` | B |
| 349 | FIX | `update-goldens.sh` passes the 60 main cases the `--content-dir` the CTest passes (the presets got theirs in `d526169`) | G |
| 350 | FIX | `sampleBytes` counts the guard frames; pinned against `PreparedSource::bytes()` | A |
| 351 | FIX | The `adoptRing` hook between publication and retirement (the hexfloat dump is in `fbeec1c`) | B |
| 352 | KEEP-BLOCKED | The director's ears: a Ring Length drag can only be heard in a host; a gate item, fixed if heard | K |
| 353 | FIX | `readWith`'s first-try break when the head laps a whole copy; a deterministic test through `ringReadHook` | B |
| 354 | FIX | An inactive save drops the frames once the bytes exist; activation restores from the bytes | B |
| 355 | FIX | A newer load withdraws every layer's waiting restore offer where it offers none | B |
| 356 | KEEP-BLOCKED | A Rosetta translation artifact; the output is held bit for bit. Unblocked by a WARN in a CI log or an x86 Mac | – |
| 357 | FIX | `kBusCutoff*` names in the test table | H |
| 358 | FIX | `GrainFilterBank::render` asserts its sizes | G |
| 359 | FIX | One check over the buffer | H |
| 360 | FIX | One block-loop helper for the engine tests | H |
| 361 | FIX | Document that a source removal keeps `bus2Used()` raised, with a test (the row's "documented" was untrue) | F |
| 362 | FIX | Smear's law test bound `>= D` | H |
| 363 | FIX | A stray limiter mode is the identity, agreeing with `steady()`; the ramp-of-1 test | F |
| 364 | FIX | An empty present sample is silent and never falls to the ring | E |
| 365 | FIX | DC and Nyquist at unity; five WAVs move (§6.2, §9) | E |
| 366 | FIX | One zero-fill helper for the window reads; bit-identical | E |
| 367 | FIX | `std::logic_error` for a rejected FFT size, docs fixed, the range's ends tested | E |
| 368 | FIX | The piece loop and 96/192 kHz tested | E |
| 369 | FIX | A Processor-level delay test | F |
| 370 | FIX | The fade's turn for A to B to A; the spec amended for a third type (§9) | F |
| 371 | FIX | A Processor test of the modulator-control bound | F |
| 372 | FIX | Measured: a report-only Spectral stage in `--chain`, timed at the idle run | G |
| 373 | FIX | `kRingSeconds` in a light header | E |
| 374 | FIX | `--chain`'s exit 1 and `--space --chain`'s exit 2 tested | G |
| 375 | FIX | A plugin test: the material changes before the kind becomes Spectral | H |
| 376 | FIX | An FX Spectral Position-units test | H |
| 377 | FIX | An RMS bound in place of `peak > 1e-4` | H |
| 381 | FIX | D-specwindow option 1 built: a Spectral layer over its ring follows the window and takes splices | E |
| 382 | KEEP (record) | The director's D-corners keeps the finding on record until the corners' boundaries are scored (§23 #69) | – |
| 383 | FIX | `formatOf` tested with a fake `clap_host`; no wrapper host is needed | I |
| 384 | FIX | `MediaSession::capturesFolder` public; the editor's copy deleted | B |
| 385 | FIX | `LivePeaks` sized for the longest ring; the lane's oldest frame clamped | I |
| 386 | FIX | The lane follows the engine's splice numbering (§9) | I |
| 387 | DIRECTOR | §8.2 (a gate item) | K |
| 388 | FIX | Its 18 items, one line each in the plan; its pointer to row 330 goes there | I |
| 389 | FIX | A relink to a file with the same fingerprint keeps the content reference | A |
| 390 | CLOSE-OBSOLETE | A decided rule (INTENT §19, the presets spec §6.4, `RenderSpec.h`), pinned by a test | – |
| 392 | CLOSE-OBSOLETE | The content repository's `v2` already names VSCO paths and matches exact lines; its counts are v2's | – |
| 393 | DIRECTOR | §8.2 | – |
| 394 | KEEP-BLOCKED | The director's ears at the gate: the gained sources' noise floor (15 Kalimba Steps) | K |
| 395 | KEEP-BLOCKED | The director's ears at the gate: 22 Shard Storm's ticks, 17 and 18 without a transport | K |
| 396 | DIRECTOR | §8.2 | – |
| 397 | KEEP-BLOCKED | The director's eyes at the gate: the dark browser's list ground (in a host; `gg-snap` doesn't draw the browser) | K |
| 398 | FIX | The presets pack's table re-measured on `release` | K |
| 400 | FIX | Not a race: the test's instance draws a fresh seed and Distribution 0.5 leaves a rare gap; a fixed seed and Distribution 0 (confirmed first by a seed loop) | B |
| 401 | FIX | The same-name search after the content roots (INTENT §18, the presets spec §6.4) takes the name from the content reference | A |
| 402 | DIRECTOR | §8.2 | – |
| **403** (new) | FIX | The ring reader's possible race (§6.4) | B |

## 11. Verification

- Every group: its unit tests (labels as CLAUDE.md lists), `scripts/update-goldens.sh` before and
  after with the hash list in the commit (every hash equal except §6.2's five for row 365), the
  determinism tests; group B and row 403 also the `[threads]` tests looped and in the TSan tree.
- The bootstrap scripts: CI's three legs install through them; the nightly's fresh-container job;
  the `scripts` test of the README's lines; `shellcheck` on the two shell scripts.
- The spelling sweep: a grep for the British forms over first-party files lists only the kept
  exceptions; every golden hash unchanged.
- The gate: the director's verdicts recorded in the gate README, each fault through the fix wave.
- The idle run's JSON committed with its README; the CI gate read in the final report.
- Before `BUILD READY`: `scripts/check.sh` over `dev,asan,rtsan` passing (its summary in the PR),
  CI green on the head, clap-validator, pluginval, auval (under the AU lock), the VST3 validator and
  `ctest -L au` run locally on `release`, the opus final review and its fix wave, this spec marked
  Built, the §23 rows in, and the open count before and after.

## 12. INTENT changes (Part II, with §23 rows in the same change)

- **§23, the close's row** (next free number at merge): this sub-project's deliverables, the triage's
  counts, the gate's verdicts, the idle figures, the run-length decision if taken.
- **§23, one dated row per conflict answer** (K-1 to K-8), and per DIRECTOR row answered.
- §14 (K-2, K-4 per the answers; 302's onset fade; 381's window; 365's unity), §16 (the idle
  figures, X, row 372's figure), §17 (row 402's answer), §18 (K-3, K-5), §19 (the bootstrap
  scripts and their CI checks; the Rosetta golden coverage), §20 (M2 closed), the header status
  line, and every drift the review lists in INTENT.
- **Part I:** only as the director's answers to K-4 (option 2) or K-6 (option 1) say.

## 13. Risks

| Risk | Mitigation |
|---|---|
| The burn-down's 58 FIXes outgrow the close | Groups run in parallel task worktrees; a FIX the build shows to be larger than its row moves class with the reason in the final report, never silently |
| Row 403 doesn't reproduce | The diagnostics stay in the test and the row stays a watch row (§6.4) |
| The spelling sweep collides with every other group | Its renames land first; its text pass last, on the merged head |
| A gate fault is large | The fix wave comes before the idle run; a fault too large for M2 becomes a row with the director's word |
| The Windows link race delays the Windows baselines | Rows 304 and 305's Windows half waits for the program manager's fix; the Linux remake doesn't |

## 14. Left to the builder

The plan's task split and order inside §6.10; each group's test names; the bootstrap scripts' exact
flags and messages; the TSan configuration's details; the gate README's wording; which of row 388's
items share a task.
