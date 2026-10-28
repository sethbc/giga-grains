# gg-bench baselines

One file per CI leg, named after the leg's preset: `mac-universal.json`, `windows-clangcl.json`,
`linux-gcc12.json`. CI's benchmark step passes `--baseline tools/gg-bench/baselines/<preset>.json`
to `gg-bench` when that file exists, and reports a verdict for every engine ratio. Through M1 the
verdict is report-only (spec ruling 3): a regression is a warning, never a failed job.
Sub-project 6 makes it blocking, with the published §16 headline.

Each leg's baseline has one ratio per engine case per block size (32, 64 and 256). The engine
cases are `engine-sample` (X, the headline), `engine-sample-eco` (X at the Eco tier),
`engine-live` (X read from the live ring, the FX's figure) and `engine-full-pool` (admission when
the pool is saturated).

The baselines themselves land at the end of sub-project 2 (Task 14), from three CI runs of the
finished engine. Until then this directory holds only this file, and CI prints the ratios with no
verdict.

## Format

```json
{
  "schemaVersion": 1,
  "leg": "linux-gcc12",
  "commit": "<sha>",
  "ratios": [
    {"case": "engine-sample", "blockSize": 32, "ratio": 0.83, "tolerance": 0.05}
  ]
}
```

- `ratio`: an engine case's ns per grain-sample over the `reference` kernel's, measured in the
  same round and taken as the median over the rounds. Because the two run side by side in one
  process, the ratio cancels the runner's speed.
- `tolerance`: the spread the ratio is allowed. A run regresses when its median ratio is above
  `ratio × (1 + tolerance)`.
- A case or block size the baseline doesn't list is reported as "not in the baseline". It doesn't
  count as a regression.

## Making one

Download the `gg-bench-<preset>` artifact of each CI run (`gh run download`). Then run:

```sh
gg-bench --make-baseline <preset> tools/gg-bench/baselines/<preset>.json run1.json run2.json run3.json
```

For each (case, block size), this takes each run's median ratio. The baseline's `ratio` is the
median of those values. Its `tolerance` is `max(0.05, 2 × (max − min) / median)`. `commit` is
the runs' commit, or their distinct commits joined by commas.

**A baseline is valid only against the `reference` kernel** (`renderHermiteSimd` in
`tools/gg-bench/Kernels.h`, frozen). If that kernel changes, or anything it calls, every baseline
has to be made again.
