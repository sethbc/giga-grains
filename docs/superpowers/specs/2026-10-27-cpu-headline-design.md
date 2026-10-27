# The §16 CPU headline: measured on the M5 Max, published as measured

| | |
|---|---|
| **Status** | Approved by the director, 2026-10-27, including the two Part I wordings in §4 |
| **Applies to** | M1 sub-project 2 (Cloud engine) and sub-project 6 (Close); INTENT §5, §10, §16, §19, §20, §21, §23 |
| **Replaces** | Director ruling §23 #23 ("keep the headline as a provisional target; M1 closes the gap") |

## 1. Why this decision

INTENT §16's provisional headline says: at least 2,000 Normal grains per instance at 48 kHz, in less than 25% of one Apple M1
performance core.

- **M0 measured the SIMD Hermite kernel** at:
  - 20–22% of one core on the director's M5 Max;
  - 48–53% on GitHub's Apple M1 VM;
  - 55–69% on AMD EPYC x86 VMs.

  The floor machines are therefore 1.9–2.8× over the budget (`docs/spikes/m0-kernel-benchmark.md`).
- **#23 made closing that gap an M1 goal.** Its three levers (prefetch, an interleaved source, cache-aware scheduling) attack
  cache misses and per-grain overhead. The M5 Max shows almost no cache penalty, so nothing guarantees that they close 2×.
- **Without a decision now,** sub-project 2 would open with an acceptance bar it may be unable to meet, and the end of M1 would
  stall on a judgment nobody prepared.

## 2. The decision

1. **Reference hardware is one performance core of the director's Apple M5 Max, on bare metal.** It replaces §16's "GitHub's
   M1 CI runners plus a mid-range x86 AVX2 machine", and its line "the M5 Max is the ceiling, not the target".
2. **The headline is "2,000 Normal grains per instance at 48 kHz in X% of one Apple M5 Max performance core."**
   - X is measured at the end of M1, in sub-project 6, and published as measured. **There is no ceiling on X.**
   - The text always names the chip, so nobody reads it as an M1 or x86 figure.
   - The M2 budget manager keeps any grain count safe by degrading deterministically (INTENT §14). The promise is an honest,
     reproducible figure, not a threshold.
3. **X is measured on the real engine's render path,** not on M0's prototype kernels in `tools/gg-bench/Kernels.h`.
   - The case: 2,000 Normal-quality grains of 100 ms, at 48 kHz, through `Engine::process`, scheduler and pan included.
   - The method is gg-bench's: the median of interleaved runs, performance-core QoS, the `release` preset, an idle machine.
   - The published number records the commit SHA and the block sizes (32, 64, 256).
4. **The floor machines become informative.**
   - CI's legs (the M1 VM, Windows and Linux EPYC VMs) keep running the full-length benchmark.
   - Their figures go in the benchmark doc as informative, not promised.
   - CI's CPU gate stays a regression gate, measured as ratios within one process (carry-over B-I2). It never gates on an
     absolute headline.
5. **Sub-project 2's optimization work is a fixed list of four levers, each tried, kept if it wins, then stop.**

   | Lever | What it attacks |
   |---|---|
   | Per-grain software prefetch | the first, likely cache-missing taps of each grain in each block |
   | A stereo-interleaved source layout | two address streams per tap; it halves the per-tap address work |
   | Cache-aware grain scheduling | source locality across grains. It reorders memory fetches only, never the summation |
   | Contiguous vector loads with shuffles in place of the per-lane gather | 8 one-at-a-time tap loads per output sample (NEON `tbl`, SSE shuffles) |

   - **A lever is kept** when its in-process ratio against the current kernel improves beyond the run-to-run spread, on the
     M5 Max or on any CI floor leg, and no leg regresses beyond its spread.
   - **Judging on the M5 alone would undersell the cache levers,** which mainly help older machines.
   - **Every lever obeys the determinism rules unchanged** (INTENT §14):
     - Grains are summed in spawn order, so a reordering lever may change only fetch order.
     - The output stays bit-identical at any block size.
     - A lever that changes output bits must justify it in a golden-regeneration commit.
   - When all four are tried, optimization stops. Sub-project 6 measures X and publishes it.

## 3. What does not change

- The Normal tier (4-point Hermite) is the headline's tier. Eco and HQ are not in the headline.
- The overload ladder, deterministic degradation and every real-time-safety rule.
- Part I §5's commitment to thousands of concurrent grains per instance at bounded CPU. The budget manager bounds the CPU,
  and the published figure states its cost.
- The UI budget (3 ms of UI-thread time at 8,000 grains) is a separate target, unaffected by this decision.

## 4. INTENT amendments

Task 9 of the M1 FX slice applies these as part of its INTENT update. It is the slice's INTENT task, which keeps
INTENT.md's edits in one commit mid-slice. Each Part II change gets the same dated §23 row.

**Part I (signed off by the director, 2026-10-27):**
- §5, first bullet: "with a published target (§16)" → "with published figures (§16)".
- §10, criterion 3: "The published grain-count and CPU target (§16) is met on reference hardware." → "The grain-count and CPU
  figures published in §16 are measured on the reference hardware and reproducible."

**Part II:**
- **§16:**
  - Retitle the headline as "measured on the reference core, published as measured". State it as in §2.2 above, with X to be
    measured at the end of M1.
  - Replace "Reference hardware" with §2.1 above.
  - Turn the floor-hardware paragraph into "informative floor figures" (§2.4).
  - Replace "Milestone M1's renderer owns the gap" with the lever list and the stop rule (§2.5).
- **§19:** "CPU benchmark gate in CI. Milestone M1 starts it as a regression gate at the measured baseline. The §16 headline
  becomes the gate once M1 meets it." → "CPU benchmark gate in CI: a regression gate on in-process ratios, from M1's baseline.
  The §16 headline is a published measurement, not a CI gate."
- **§20, M1 row:**
  - Delivers: "closing the §16 CPU gap" → "the §16 lever list (§23, the new row below)".
  - Exit: "The §16 headline judged again (§23 #23)" → "The §16 headline measured on the reference core and published
    (§23, the new row below)".
- **§21:** the CPU risk row becomes "Older and x86 machines pay 2× to 3× the M5 Max's CPU share for the same grain count". Its
  mitigation: the four levers judged on every CI leg, the informative floor figures, the budget manager and the quality tiers.
- **§23:** a new row, "§16 headline measured on the M5 Max and published as measured, with no ceiling; floor figures
  informative; sub-project 2 tries four levers and keeps the winners". It supersedes #23 and gives the Part I sign-off. Decided
  2026-10-27 by the director, during M1.
