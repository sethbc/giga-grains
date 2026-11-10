# M3 sub-project 6: the close

| | |
|---|---|
| **Status** | Design from the director's brainstorming of 2026-11-09 (`.superpowers/sdd/m3-call-review.md`, sub-project 6). The director's decisions are marked **(the director)**; everything else is Claude's call |
| **Program** | `docs/superpowers/specs/2026-11-07-m3-program-design.md` §8 row 6 and §20's M3 exit |
| **Builds** | Last, after sub-project 5 is on `main`, on a branch `m3-close` |
| **Model** | M2's close (`docs/superpowers/specs/2026-11-02-m2-close-design.md`), which the builder reads first |

## 1. Intent

M3 ends with its exit met and recorded, its records consistent, its open items triaged, and a
`0.3.0` release commit for the program manager to tag as an unpublished draft (INTENT §23 #62).
M3 grew from six sub-projects to twelve while running in parallel, so the records drifted more than
M2's: this pass makes them one story again.

## 2. Goals, non-goals, anti-goals

**Goals:** §20's M3 exit met (the gate passed, the first-class hosts pass); the records consistent;
every carry-over row triaged; the idle figures measured; the release commit.

**Non-goals:** new features; fixing every row (the director chose triage); making Logic Pro a
first-class host.

**Anti-goals:** a gate run on an unidle machine and called final; a record that contradicts the
code; a release commit with CI red.

## 3. Approach

One session, one branch, the M2 close's form (Claude's call; a close is a series of checks over
everything, so splitting it across parallel branches would fight over the same records). It runs
after sub-project 5 lands.

## 4. The design

### 4.1 Listening (the director)

- **Each sub-project's pack is heard as it lands** (`docs/listening/m3-<name>/`, made by its lead);
  the director's notes go into that pack's README, and anything to fix becomes a task or a
  carry-over row then, not at the close.
- **The final gate is short:** one pack, `docs/listening/m3-gate/`, of cross-feature renders (a
  Voice patch with pitch-sync, scan, the voice filter, a tuning and a scene recall together; the
  color stages on a dense cloud; the new presets' goldens) plus the host checklist (4.2). It
  confirms the parts still sound right together and that nothing regressed since each pack.

### 4.2 Hosts

- **Bitwig and Live are first-class and must pass** (§20's exit): a checklist the director runs in
  each, covering per-note expression from a LinnStrument (Bitwig CLAP and VST3, Live VST3 and AU),
  MPE zones announced by the controller, Program Change scene recall, MTS-ESP following a master,
  tempo sync with the transport running, stopped and looping, the FX on a MIDI track, and state
  save and restore with scenes and a tuning.
- **Logic Pro is checked and recorded (the director):** the director tries the wrapped AU once for
  MPE and tails; what works and what doesn't goes into INTENT §22 (resolving its M3 question) and
  the release notes. It doesn't block 0.3.0.

### 4.3 The carry-over (the director: triage every row)

Every row of `docs/m3-carry-over.md` gets one of: fixed in this close (cheap and real), kept with a
named reason and unblocker, or closed as obsolete. Sub-project 1's parked minors and row 234's
status are among them. The close's report counts each kind.

### 4.4 Consistency pass

Across INTENT (Parts I and II), `AGENTS.md`, the program design, every M3 spec and the release
docs, as M2's close did:
- INTENT §20's M3 row names all twelve sub-projects (0, 1 to 6, 3b, 7 to 11) and what each built;
- the program design's §8 gains rows 3b and 7 to 11;
- §23's rows from the parallel merges are numbered in merge order, with no duplicates or gaps,
  and each cross-reference matches;
- the competitor research (`.superpowers/sdd/m3-briefs/research/competitor-gaps.md`) is copied
  into `docs/research/` and cited;
- every count in `AGENTS.md` (parameters, goldens, tests, labels, remote pages, presets) matches the
  tree.

### 4.5 Idle figures

On an idle machine, not beside other builds: `gg-bench`'s full run (X, the published headline's
case, beside M2's 19.4%), `--chain` with the color stages, `--canvas` at 8,192 grains with voice
shades, and "a crawl of the 48 packed factory presets takes under 50 ms" with the 66 presets M3
leaves. That last test read 53 to 57 ms on several branches on a loaded box during M3; the close
decides, from an idle measurement, whether it is load or a real slowdown to fix. Figures go to
`docs/benchmarks/`.

### 4.6 The release commit

`GG_VERSION_PRERELEASE` emptied, `docs/releases/v0.3.0.md` written, `scripts/release/check-version.sh
v0.3.0` passing, CI green, as `docs/releasing.md` says. The program manager tags it; nothing is
published before M5.

### 4.7 The builder check's answers

A reviewer read this as a builder would (2026-11-09). Answered, or settled as Claude's calls:
- **Start:** every other M3 sub-project (0, 1 to 5, 3b, 7 to 11) is on `main`; sub-project 5 is
  last by design. The close's first task confirms it and freezes the counts it checks.
- **Sources:** the director's decisions are recorded in each sub-project's spec and its §23 row.
  The competitor research is copied from the main checkout's ignored
  `.superpowers/sdd/m3-briefs/research/competitor-gaps.md` into `docs/research/`, which this
  close creates.
- **The crawl test:** if the idle measurement is over 50 ms, it is a real slowdown: find the cause
  (systematic debugging) and fix it. Relaxing the bound needs the director's ruling. The `gg-bench`
  gate stays report-only (INTENT §23 #60); baselines are remade from three full dispatches only
  where engine ratios moved, as M2's close did.
- **The gate and the hosts block the release commit:** everything else is prepared first; the
  release commit and INTENT's "M3 met" wording follow the director's results. A failure found
  there is fixed in the close when cheap and real, or carried with the director's ruling.
- **The carry-over** is `docs/m3-carry-over.md`, which already holds the rows M2 left open (row 234
  among them); M2's file is a frozen record and isn't triaged again.
- **Hosts and platforms:** as M2's close: Live and Bitwig on macOS, where the director plays; the
  CI legs and validators cover Windows and Linux.
- **The version:** `CMakeLists.txt` is already at 0.3.0 with the `dev` suffix (since `c4e9245`);
  the release commit empties the suffix.

## 5. Exit

§20's M3 exit: listening gate #3 passed and the first-class hosts pass; plus the triaged
carry-over, the consistent records, the idle figures and the release commit, with `scripts/check.sh`
and every CI job green.

## 6. Left to the builder

The gate pack's exact renders; the checklist's wording; the order of the passes; how each row's
triage is worded. Each recorded as a ruling.
