# gg-bench baselines

One file per CI leg, named after the leg's preset: `mac-universal.json`, `windows-clangcl.json`,
`linux-gcc13.json`. CI's benchmark step passes `--baseline tools/gg-bench/baselines/<preset>.json`
to `gg-bench` when that file exists, and reports a verdict for every engine ratio. Through M1 the
verdict is report-only (spec ruling 3): a regression is a warning, never a failed job.
Sub-project 6 makes it blocking, with the published §16 headline.

**Baselines are per CPU model.** GitHub runs a leg on whichever CPU model it assigns. So far
Linux has landed on AMD EPYC 7763, 9V45 and 9V74 and Intel Xeon 8370C, and Windows on EPYC 7763,
9V45 and 9V74. The files have entries for the mac leg's Apple M1 VM, Windows' EPYC 7763 and 9V74,
and Linux's EPYC 7763 and 9V45; the other models get no verdict yet. The ratio
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

`engine-layers` (four layers of 500 grains, X's count), `engine-layers-full` (four of 2,000) and
`engine-live-long` (`engine-live`'s stream over a 300 s ring, its reads spanning it; M2
sub-project 2) are timed and print ratios, but have no entries yet: they are report-only until
three full runs are merged in (`--merge`, below), and a ratio a file lacks is only `not in the
baseline`. A `--quick` run caps `engine-live-long`'s 300 s warm-up at 16 s, so its quick ratios
read a partly filled ring; only full-length runs make entries.

## Format

```json
{
  "schemaVersion": 2,
  "leg": "linux-gcc13",
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
- Every ratio of the model's entry must be in the run: one the run lacks is reported as "missing
  from this run" and fails like a regression (exit 3). A run with no ratios at all, against a
  model the file has, is an error (exit 1), never "0 of 0".
- Each entry has at least one ratio, and each (case, block size) at most once. The `[bench]` case
  "the committed baselines are well formed" checks the three files.
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
   - It checks the result as `--baseline` would. A median that isn't positive and finite, or a
     tolerance that isn't finite, is an error (exit 1), and no file is written.
4. **To add a new model** after GitHub starts assigning it, or to remake one, merge the new runs
   into the committed file. The old runs aren't needed:

   ```sh
   gg-bench --make-baseline <preset> tools/gg-bench/baselines/<preset>.json \
       --merge tools/gg-bench/baselines/<preset>.json run4.json run5.json run6.json
   ```

   `--merge <old.json>` goes right after the output file, and may be the same file. Every entry
   of the old file stays where it is, except those for the new runs' models, which are made from
   the new runs only. Models the old file lacks are added at the end. gg-bench prints each entry
   as `(kept)`, `(replaced)` or `(new)`. The old file's leg must be `<preset>`. The kept entries
   are still ratios of the engine code they were made from, so merge only runs of engine code
   whose ratios haven't moved since.

   A `(replaced)` entry is rebaselined to its new runs whatever they show, a regression
   included: before committing, compare each replaced entry with the old one (`git diff`) and
   explain any ratio that moved.

**A baseline is valid only against the `reference` kernel** (`renderHermiteSimd` in
`tools/gg-bench/Kernels.h`, frozen). If that kernel changes, or anything it calls, every baseline
has to be made again. `Kernels.h` includes no engine header, so no engine change can move it: it
has its own frozen copies of the Hann table and the readers. The `[bench]` case "the reference
kernel's output is pinned" hashes its output, so a change to it fails there first.
