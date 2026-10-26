# CLAUDE.md

Guidance for Claude Code sessions working in this repository.

## Start here

giga-grains builds two plugins from one shared C++20 grain engine:

- **Giga Grains**, a granular synthesizer;
- **Giga Grains FX**, a granular effect.

They ship as CLAP, VST3, AU v2 and standalone, for macOS, Windows and Linux. The license is
AGPL-3.0-or-later.

**Read [`INTENT.md`](INTENT.md) before doing anything.** It is the north star.

- **Part I (Intent)** changes only with the director's explicit sign-off.
- **Part II (Design direction)** may evolve. Every change to it adds a dated entry to the decision
  log (§23) in the same change.
- **When a task conflicts with INTENT.md, stop.** Propose an INTENT.md amendment; don't diverge
  silently.
- **The principles in §8 are the tie-breakers** when goals conflict.

Development is Claude-led. The director steers, listens and reviews. Ears are the scarce
resource, so machines must catch everything they can catch.

## Non-negotiables

- **Audio thread:**
  - no allocation, locks, I/O, syscalls or unbounded loops;
  - must be RealtimeSanitizer-clean;
  - must handle 0- and 1-sample blocks;
  - `params.flush` stays trivial.
- **Determinism:**
  - Randomness only from the engine's counter-based RNG, keyed by (seed, layer, stream, field,
    grain index).
  - No `std::` random distributions.
  - No behavior that adapts to measured CPU.
  - No render-mode-dependent behavior unless the user opts in with an explicit parameter.
  - Layers are summed in a fixed order.
  - Output must be bit-identical regardless of host block size.
- **Positions and phases are 64-bit** (int64 + fraction, or double). float32 is allowed only for
  offsets within a block.
- **Overload degrades deterministically:**
  1. lower the interpolation tier;
  2. then drop new grains;
  3. then steal grains with fades.

  It never clicks and never drops out.
- **Stability contracts** (frozen from the first public release):
  - CLAP parameter IDs, which are append-only;
  - AUv2 parameter order;
  - factory preset order;
  - plugin identities and AU codes.
- **Licensing:**
  - Only AGPLv3-compatible dependencies.
  - No GPLv2-only code and no unlicensed code.
  - Never use the names "Mutable Instruments", "Clouds" or "Beads" in the product.
  - Pin every dependency to an exact SHA or tag.

## Working style

- Every DSP change ships with:
  - unit tests;
  - updated `gg-render` golden renders, compared by metrics;
  - the determinism tests.
- Run the sanitizer builds (ASan/UBSan, RTSan) and the format validators before claiming work is
  done.
- When you report a result, include its evidence: test output, benchmark numbers, render
  statistics or spectrograms.
- Each milestone ends with a listening gate: render A/B files plus a short checklist for the
  director.
- Reference templates for the CLAP-first build, CI and installers:
  - `~/src/shortcircuit-xt`
  - also `~/src/surge` and `~/src/OB-Xf`.

## Build and test

There's no code yet. Build, test, render and benchmark commands get established in milestone M0
(INTENT.md §20) and documented here at that point.
