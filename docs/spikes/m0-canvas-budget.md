# M0 spike: canvas stress budget

*M0 Task 9, 2026-10-26. Measures whether the CPU additive "splat" renderer (INTENT §17) can
draw thousands of grains within the INTENT §16 UI budget of **at most 3 ms of UI-thread time
per frame with 8,000 visible grains**. The headless numbers come from one machine. Task 12 read
the editor's own "frame ms" in Ableton and Bitwig; see "In the hosts (Task 12)".*

## What was measured

`gg-bench --canvas` times `gg-ui-core` with no JUCE and no window:

- a **3200 × 1200** canvas, i.e. a 1600 × 600 logical canvas on a 2× display;
- the editor's **stress workload** (`gg::ui::stressGrains`) at 4,096, 8,192 and 16,384 grains:
  - four lanes, a random x per grain;
  - radius 2 to 8 logical px (4 to 16 physical);
  - lifetimes of 20 to 59 frames, with a fade in and out;
- per frame:
  - **clear** the float accumulator;
  - **splat** every grain (`SplatRenderer::add`);
  - **resolve** to ARGB (`resolveToArgb`, v = 1 − exp(−a), tinted).

Generating the synthetic grains is timed separately and left out of the total, because the real
canvas gets its grains from the engine. Each result comes from 240 frames after a 20-frame
warm-up.

It does **not** include drawing the image (`Graphics::drawImage`) or the rest of JUCE's paint.
The editor's "frame ms (avg 60)" readout adds the `drawImage` call, timed on the UI thread. On
macOS most of the image draw then runs later, off the UI thread, so the readout doesn't include
it; see "In the editor" below.

**Build:** release preset, AppleClang 21, arm64.

**Machine:** Apple M5 Max. INTENT §16 names the M5 Max as the ceiling, not the target.

**Indicative only.** Other builds may have been running at the same time (load average 1.9 to
3.6 around the recorded run). Three back-to-back runs agreed within 1%: 2.76 to 2.78 ms at
8,192 grains.

## Results

Recorded run, `gg-bench --canvas`:

```json
{
  "benchmark": "canvas",
  "machine": "Apple M5 Max (arm64)",
  "compiler": "AppleClang 21.0.0.21000334",
  "width": 3200,
  "height": 1200,
  "pixelScale": 2.0,
  "frames": 240,
  "budgetMs": 3.0,
  "results": [
    {"grains": 4096, "generateMs": 0.026, "clearMs": 0.058, "splatMs": 0.346, "resolveMs": 2.000, "totalMs": 2.406, "totalMsMean": 2.400, "totalMsP95": 2.450},
    {"grains": 8192, "generateMs": 0.052, "clearMs": 0.058, "splatMs": 0.709, "resolveMs": 2.002, "totalMs": 2.767, "totalMsMean": 2.763, "totalMsP95": 2.824},
    {"grains": 16384, "generateMs": 0.106, "clearMs": 0.058, "splatMs": 1.444, "resolveMs": 2.018, "totalMs": 3.518, "totalMsMean": 3.511, "totalMsP95": 3.579}
  ]
}
```

| Grains | Clear | Splat | Resolve | **Total (median)** | p95 | vs 3 ms budget |
|---:|---:|---:|---:|---:|---:|---|
| 4,096 | 0.06 | 0.35 | 2.00 | **2.41 ms** | 2.45 ms | within |
| 8,192 | 0.06 | 0.71 | 2.00 | **2.77 ms** | 2.82 ms | within, 0.23 ms (8%) to spare |
| 16,384 | 0.06 | 1.44 | 2.02 | **3.52 ms** | 3.58 ms | over |

## Reading the numbers

- **At the budget point (8,192 grains), the CPU renderer fits, but only just,** and before the
  image is drawn. Drawing a 3200 × 1200 image costs extra, so at this canvas size the whole frame
  probably goes over 3 ms in a host. Task 12 read the editor at its own, much smaller canvas size
  (see the last bullet and "In the hosts (Task 12)"), so this size remains unmeasured in a host.
- **The resolve dominates and is a fixed cost per pixel:**
  - about 2.0 ms at 3.84 M pixels (0.52 ns/pixel), whatever the grain count;
  - it scales with canvas area, not with grains.
- **The splat is cheap and linear in grains:** about 85 ns per grain at a mean radius of about
  10 physical px.
- **Clearing is negligible** (0.06 ms).
- **The editor's default canvas is much smaller.** At its startup size the canvas is 868 × 360
  logical, or 1736 × 720 physical pixels at 2× (1.25 M pixels). Scaling the numbers above to
  that area, a frame should take about 0.65 ms for the resolve and about 1.4 ms in total at
  8,192 grains.

**If a frame goes over budget** (a larger canvas, or slower hardware), the levers, in order of
payoff, are:

1. Resolve only rows or tiles that grains touched this frame. Grains sit in lanes, so much of the
   canvas is empty.
2. A cheaper or narrower resolve: an 8-bit lookup, or 16-bit lanes on NEON.
3. The GPU backend spike from INTENT §17.

## Implementation notes

These set the numbers above. They are in `ui/src/SplatRenderer.cpp`.

- **Both inner loops are written for the compiler to vectorize** (4-wide on arm64 and x86_64,
  checked with `-Rpass=loop-vectorize`).
  - The resolve was first 6.0 ms. It didn't vectorize because a `uint32_t` store could alias the
    `width_` member, so the trip count was unknown. Copying the bounds to locals fixed it.
- **The resolve uses no libm.** exp(−a) is computed as 2^t: the exponent bits of 2^⌊t⌋ plus a
  cubic for the fraction. It is about 1e-4 accurate, far below one 8-bit step, and monotonic.
  The tests check it stays within one level of the exact curve for a from 0 to 30.
  - A packed-integer channel variant was 6% faster but less accurate (0.97 levels worst case),
    so it was not adopted.
- **The splat walks the splat's bounding box.** A per-row circle chord was tried first. Every
  row of the box is processed and the weight clamp drops the corners. That is twice as fast as
  fitting each row to its chord, because rows are short and a per-row square root, rounding
  and changing trip count cost more than the corner pixels. The output is bit-identical.

## In the editor

The plugin editor (`gg::ui::Editor`, 900 × 500) has a canvas stress panel:

- a combo box for 0, 4,096, 8,192 (the default) or 16,384 grains;
- a readout, "frame ms (avg 60)", plus the canvas size in physical pixels.

The canvas repaints on every display refresh (`juce::VBlankAttachment`).

**What the readout measures.** It is the mean, over the last 60 frames, of the UI-thread time the
canvas's `paint` spends on:

- clearing the accumulator;
- splatting every grain;
- resolving into the image;
- the `Graphics::drawImage` call.

It leaves out generating the synthetic grains and the rest of JUCE's paint.

**On macOS it counts UI-thread time only.** JUCE 9 sets `drawsAsynchronously` on the editor view's
layer (`juce_NSViewComponentPeer_mac.mm`). So the CoreGraphics drawing that `drawImage` records
runs later, on Core Animation's thread, mostly outside the timed span. That makes the readout the
quantity the §16 budget names (UI-thread time per frame), and a lower bound on the frame's whole
cost.

In Task 9 the editor was not captured from a live window: screen recording and accessibility
access weren't granted to the agent session. An offscreen render (`createComponentSnapshot` at
2×, JUCE's software renderer, 8,192 grains) read **2.50 and 3.56 ms at 1736 × 720 px** in two
runs. Each run averaged only 10 frames, the first of them touching a freshly allocated image. The
gap from the 1.4 ms estimated above is that first frame plus drawing the image, which the
headless benchmark leaves out. The software renderer draws synchronously, so these readings do
include the image draw. Treat them as a sanity check only: in a host the paint goes through
CoreGraphics, not the software renderer.

The JUCE 9 spike later read the live standalone windows at 1736 × 720 px: 2.15 to 2.52 ms
(`docs/spikes/m0-juce9.md`).

## In the hosts (Task 12)

**Result: under 3 ms in both hosts.**

- **The reading.** The director read the editor's "frame ms (avg 60)" at 8,192 grains, at the
  display's native scale, in Ableton Live 12.4.7 and Bitwig Studio 6.1.5. It was "ALWAYS below
  3ms" in both. No exact numbers were recorded.
- **Canvas size.** It wasn't recorded either. The editor opens at 900 × 500, which makes the canvas
  1736 × 720 physical pixels on a 2× display, unless the window was resized.
- **Build:** the `release` build at `c7e3a1a` (JUCE 9.0.4, arm64), the same build as the host
  matrix (`docs/spikes/m0-host-matrix.md`).
- **Machine:** the Apple M5 Max, which INTENT §16 names as the ceiling.

**Caveat.** On macOS the readout is UI-thread time only (see "In the editor"), so it's a lower
bound on what a frame costs in total. It does meet the §16 budget as worded: at most 3 ms of
UI-thread time per frame with 8,000 visible grains.

**Not measured:**

- floor hardware: an M1, or a mid-range x86 machine;
- Windows and Linux hosts;
- canvases larger than the editor's default.

**Director decision: the GPU backend spike stays in M2** (INTENT §17, §22; §23 #26). The CPU splat
renderer stays the v1 renderer. If a frame goes over budget before then, the levers listed under
"Reading the numbers" apply.

## Reproduce

Headless:

```sh
cmake --preset release
cmake --build --preset release --target gg-bench
build/release/tools/gg-bench/gg-bench --canvas [--quick] [--json out.json]
```

In a host: open either plugin's editor, leave the stress panel at 8192 grains, and read
"frame ms (avg 60)" once it has run for a few seconds.
