# gg-bench baselines

One file per CI leg, named after the leg's preset: `mac-universal.json`, `windows-clangcl.json`,
`linux-gcc12.json`. CI's benchmark step passes `--baseline tools/gg-bench/baselines/<preset>.json`
to `gg-bench` when that file exists, and reports a verdict for every engine ratio. Through M1 the
verdict is report-only (spec ruling 3): a regression is a warning, never a failed job.
Sub-project 6 makes it blocking, with the published §16 headline.

**Baselines are per CPU model.** GitHub runs a leg on whichever CPU model it assigns. Linux has
landed on EPYC 7763, EPYC 9V45 and Xeon 8370C, and Windows on EPYC 7763 and 9V45. The ratio
cancels a machine's speed but not its microarchitecture: for unchanged code, ratios differ by up
to ~20% between models and by up to ~6% on one model. So each leg's file has one entry per CPU
model, keyed by gg-bench's `machine` string (for example `AMD EPYC 7763 64-Core Processor
(x86_64)`). A run is checked only against the entry for its own model. When the file has no entry
for the model, gg-bench prints `no baseline for <machine>; no verdict` and exits 0. A model gets
an entry once later runs on it are added to the baseline (see "Making one").

Each entry has one ratio per engine case per block size (32, 64 and 256). The engine cases are:
- `engine-sample`: X, the headline;
- `engine-sample-eco`: X at the Eco tier;
- `engine-live`: X read from the live ring, the FX's figure;
- `engine-full-pool`: admission when the pool is saturated.

## Format

```json
{
  "schemaVersion": 2,
  "leg": "linux-gcc12",
  "machines": [
    {
      "machine": "AMD EPYC 7763 64-Core Processor (x86_64)",
      "commit": "<sha>",
      "runs": 3,
      "ratios": [
        {"case": "engine-sample", "blockSize": 32, "ratio": 0.83, "tolerance": 0.10}
      ]
    }
  ]
}
```

- `machine`: the `machine` string of the gg-bench runs the entry was made from. It is matched
  exactly, and each model appears at most once.
- `commit`: those runs' commit, or their distinct commits joined by commas. `runs`: how many
  runs there were.
- `ratio`: an engine case's ns per grain-sample over the `reference` kernel's, measured in the
  same round and taken as the median over the rounds, then the median over the entry's runs.
- `tolerance`: the spread the ratio is allowed. A run regresses when its median ratio is above
  `ratio × (1 + tolerance)`.
- A case or block size missing from the model's entry is reported as "not in the baseline". It
  doesn't count as a regression.
- Schema 1, the earlier format with one set of ratios per leg, is no longer accepted.

## Making one

1. Download the `gg-bench-<preset>` artifact of each CI run (`gh run download`). The runs must
   all be of the same engine code.
2. Run:

   ```sh
   gg-bench --make-baseline <preset> tools/gg-bench/baselines/<preset>.json run1.json run2.json run3.json
   ```

3. **How it groups the runs:** by their `machine`, and it writes one entry per model.
   - For each (case, block size), it takes each run's median ratio.
   - The entry's `ratio` is the median of those values.
   - Its `tolerance` is `max(0.10, 2 × (max − min) / median)`. A model seen in only one run gets
     the 0.10 floor.
4. **To add a new model** after GitHub starts assigning it, make the baseline again from the old
   runs' artifacts together with runs on the new model. The model's entry comes from its own runs
   only.

**A baseline is valid only against the `reference` kernel** (`renderHermiteSimd` in
`tools/gg-bench/Kernels.h`, frozen). If that kernel changes, or anything it calls, every baseline
has to be made again.
