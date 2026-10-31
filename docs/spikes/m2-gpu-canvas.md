# M2 spike: the GPU canvas

*M2 sub-project 6 (views), Task 17, 2026-10-30. The timeboxed spike INTENT §17 and §22 carried
from M0 (§23 #26): whether a GPU backend should replace the CPU "splat" renderer behind
`GrainCanvas`. The spec is `docs/superpowers/specs/2026-10-30-m2-views-design.md` §6.11; the
M0 budget spike is `docs/spikes/m0-canvas-budget.md`. Spike code is on the branch
`spike/gpu-canvas` (head `082a874`), unmerged.*

**Decision: the CPU renderer stays.** Both candidates draw the 8,192-grain frame for a small
fraction of the CPU path's time (JUCE OpenGL: about 0.2 ms on JUCE's GL thread and 0.4 ms on
the GPU; Metal: about 0.1 ms on the UI thread and 0.4 ms on the GPU, against about 4 ms of
UI-thread time for the CPU path under this session's load), so each beats the CPU path on the
§16 budget by 6.6× to 45× depending on the column (6.6× for GL and 12.5× for Metal on
whole-process CPU, 20× and 45× on the rendering thread), measured at one load; but neither clears the
criterion's other half, *no new platform risk*: OpenGL is deprecated on macOS and its behaviour
in the hosts, under Rosetta, on Linux compositors and on Windows drivers could not be ruled in
within the timebox, and Metal is macOS-only, so it would be a second renderer beside the CPU one.
The CPU renderer is expected to hold the budget idle: the only idle figure, 2.87 ms (§23 #60),
is `gg-bench --canvas` headless at 3200 × 1200, without `drawImage` and the overlays, and the
live readout has not been read idle; Task 18's idle run is the record. The decision rests on
the ratios and the risk clause, not on the absolute figures, so it is not provisional on that
run; the absolute figures below must still be re-read idle.

## The criterion

Spec §6.11's words, which it quotes from programme design §6.6: **the CPU renderer stays unless
the spike beats it on the §16 UI budget with no new platform risk.** §16's budget: at most 3 ms of UI-thread time
per frame with 8,000 visible grains. A risk the spike cannot rule out counts against the
candidate.

## Conditions

Everything below was measured on one machine, in one afternoon, **under heavy contention:**
five agent sessions shared the machine's 18 cores, and the one-minute load average ran from
10.9 to 18.6 across the measured runs (each run's is in the tables; the smoke runs before them,
not in the tables, saw 8.2 and 25 to 35). **The absolute figures are therefore
inflated and must be re-read idle;** the CPU path and the candidates were measured interleaved
under the same conditions, alternating run by run, so their ratios are the figures to trust.

- **Timebox:** spec §6.11 gives the spike one plan task, one agent session's day, with
  candidate 1 allotted half of it (the brief). Wall clock from the worktree's setup: the CPU
  baseline hook read its first figure at 15:13, the GL candidate its first clean figure at
  15:18 (two one-build stumbles in between), Metal at 15:23, and the two measurement rounds
  ended at 15:30, under an hour in all, so neither candidate's half was reached.
- **Machine:** Apple M5 Max (INTENT §16 names it the ceiling, not the target), macOS 27.2,
  Apple clang 21.0.0, JUCE 9.0.4, clap-wrapper v0.17.0. Main display 2304 × 1296 logical at
  2× (4608 × 2592 physical), **240 Hz.**
- **Build:** the `release` preset of the spike branch, the FX standalone app
  (`Giga Grains FX.app`), configured and built in the task's worktree with `--parallel 6`.
- **Workload:** `GG_STRESS_GRAINS=8192`, the canvas's test seam (Task 9): the M0 stress
  workload (`gg::ui::stressGrains`, four lanes, radius 2 to 8 logical px, lifetimes of 20 to
  59 frames with fades), generated per frame inside each renderer's timed span, drawn instead of
  the grain queue. The plugin is inactive (no audio input), equally in every run.
- **Size:** the editor at **1624 × 762 logical** (`GG_EDITOR_SIZE`, a spike hook), which makes
  the canvas component **1600 × 600 logical** as §6.11 asks, and its lanes (the canvas minus the
  120 px header column the layer strip owns) **1480 × 600 logical = 2960 × 1200 physical.**
  M0's headless benchmark used 3200 × 1200; these figures are for 7.5% fewer pixels.
- **The readout:** the header's "frame ms (avg 60)", the mean of the last 60 frames, read 20 s
  after the editor was constructed; **five runs per path, interleaved** (CPU, then each
  candidate, five times over), the median reported with the range. The window cannot be read
  by hand from the measuring session, so the spike branch adds the smallest hook:
  `GG_FRAME_LOG_SECONDS=20` prints the readout to stderr at 20 s and exits (the task brief
  allows this). It also prints, over the same 20 s, the message thread's CPU time
  (`thread_info`) and the process's (`getrusage`), each divided by the frames rendered: a
  per-frame cross-check that doesn't depend on where a renderer put its work.
- **What each path's readout contains** (they are not identical quantities; the table says
  which column is which):
  - **CPU path:** `GrainCanvas::paint`'s wall time on the message thread: generating the
    stress grains, clearing, splatting, resolving to ARGB, `drawImage` and the overlays (rules,
    the lane outlines). As M0 recorded, on macOS the CoreGraphics drawing that `drawImage`
    records runs later on Core Animation's thread (`JUCE_COREGRAPHICS_DRAW_ASYNC=1`), outside
    this span; it shows up in the process CPU column.
  - **JUCE OpenGL:** the wall time of `renderOpenGL` on JUCE's GL thread (generating the grains,
    uploading the instance buffer, both passes' submission; not the swap, which JUCE does
    after the callback and which waits for vsync), and the GPU time of both passes from
    `GL_TIME_ELAPSED` queries, read two frames later. The message thread does nothing per frame
    for this canvas (`setComponentPaintingEnabled(false)`, so JUCE takes no message-thread
    lock), which the main-thread CPU column confirms.
  - **Metal:** the wall time of the render on the message thread (generating the grains, the
    copy into the instance buffer, encoding both passes, commit): UI-thread time by
    construction, since the spike renders from the canvas's `VBlankAttachment`; and the GPU
    time from each command buffer's `GPUStartTime` to `GPUEndTime`.

### Both candidates draw the CPU renderer's picture

Neither window can be seen from the measuring session, so each candidate reads its
accumulator back once, at frame 120, and logs per-lane sums, compared with the CPU renderer's
`SplatRenderer` on the same `stressGrains` frame at 2960 × 1200:

| Lane | CPU accumulator sum | lit px | GL sum | lit px | Metal sum | lit px |
|---|---:|---:|---:|---:|---:|---:|
| A | 86,023.2 | 437,081 | 85,789.6 | 437,882 | 85,978.8 | 437,072 |
| B | 93,923.4 | 462,832 | 94,161.0 | 460,246 | 93,874.1 | 462,824 |
| C | 88,585.4 | 445,374 | 88,436.8 | 444,519 | 88,538.9 | 445,365 |
| D | 92,391.5 | 450,630 | 92,582.4 | 452,338 | 92,343.0 | 450,635 |

Metal matches within 0.06% (its pixel-centre rule is the CPU renderer's); GL within 0.3% on the
sums and 0.6% on lit pixels (half-float accumulation and pixel-coverage rounding at the
splats' rims). Both resolve with `1 − exp(−a)` and the lane tints, as `resolveToArgb` does.

## Results

### Round 1: CPU path and JUCE OpenGL, interleaved, five pairs

Load averages (1 min) at each run's start: CPU 13.1, 11.5, 11.5, 15.1, 18.6; GL 10.9, 11.9,
11.9, 18.3, 16.6. The fourth CPU run printed no readout (the process ended at 16 s without a
line; not reproduced), so the CPU column has four readings.

| Path | Readout, ms/frame (median; range) | GPU ms | Frames in 20 s | Main-thread CPU ms/frame | Process CPU ms/frame |
|---|---:|---:|---:|---:|---:|
| CPU (`paint` on the UI thread) | **4.01** (3.69–4.03) | — | 3,163 (2,743–3,350) | 4.39 (4.05–4.59) | 5.68 (5.35–6.34) |
| JUCE OpenGL (GL thread) | **0.20** (0.18–0.24) | **0.38** (0.33–0.38) | 1,907 (1,878–2,112) | 0.10 (0.08–0.13) | 0.88 (0.71–1.05) |

### Round 2: CPU path, JUCE OpenGL and Metal, interleaved, five triples

Load averages (1 min) at each run's start: CPU 14.6, 13.0, 11.3, 14.8, 11.9; GL 13.8, 15.2,
16.3, 13.7, 13.7; Metal 13.7, 12.1, 16.5, 12.5, 13.4. All fifteen runs printed.

| Path | Readout, ms/frame (median; range) | GPU ms (median; range) | Whole frame, readout + GPU | Frames in 20 s | Main-thread CPU ms/frame | Process CPU ms/frame |
|---|---:|---:|---:|---:|---:|---:|
| CPU (`paint` on the UI thread) | **4.10** (3.92–4.47) | — | 4.10 + the asynchronous CoreGraphics draw, unmeasured | 3,123 (2,567–3,198) | 4.49 (4.33–5.25) | 5.77 (5.60–6.73) |
| JUCE OpenGL (GL thread) | **0.20** (0.17–0.24) | **0.38** (0.37–0.40) | 0.58 | 1,916 (1,846–2,030) | 0.11 (0.08–0.12) | 0.88 (0.69–0.98) |
| Metal (UI thread; wait inside it 0.012) | **0.09** (0.09–0.13) | **0.45** (0.36–0.70) | 0.54 | 2,958 (2,750–3,016), plus 1,786 (1,726–2,002) VBlanks skipped | 0.19 (0.19–0.25) | 0.46 (0.43–0.61) |

The whole-frame column adds a candidate's rendering-thread time to its GPU time. The two
overlap (the GPU runs a frame while the thread encodes the next), so the sum is an upper
bound on one frame's serial cost, not a measured latency; for the CPU path it is the readout
alone, since the CoreGraphics draw it records runs on Core Animation's thread outside the
span and is not timed separately. Round 1's table omits the column for the same reason; its
sums would be 4.01 and 0.58.

**Ratios, candidate over the CPU path, round 2 medians:** JUCE OpenGL 0.048 on the thread
that renders (0.20 / 4.10), 0.024 on the message thread (0.11 / 4.49), 0.15 of the process's
CPU (0.88 / 5.77); Metal 0.023 on the message thread (0.09 / 4.10 by readout, 0.19 / 4.49 by
CPU time, which also counts the skipped VBlanks' callbacks), 0.08 of the process's CPU
(0.46 / 5.77). Round 1's GL ratios agree (0.051, 0.023, 0.15).

### Reading the numbers

- **Ratios, measured at one load** (no run varied it; both rounds sat at a load average of
  11 to 19, so how the ratios move with load is not known): per frame the GL candidate costs
  about **1/20 of the CPU path on the thread that renders** (0.20 ms on the GL thread against
  4.1 ms on the UI thread) plus about **0.38 ms of GPU**; Metal about **1/45 on the UI thread**
  (0.09 ms) plus about **0.45 ms of GPU.** Whole-process CPU per frame, the fairest single CPU
  figure (it includes the CPU path's asynchronous CoreGraphics draw, the drivers' threads, and
  the idle audio thread equally), narrows the gap to **6.6×** for GL (5.77 against 0.88 ms) and
  **12.5×** for Metal (0.46 ms). So the candidates' advantage is 6.6× to 45× depending on which
  column one reads, never less than 6.6×.
- **The CPU path's 4.1 ms here against 2.87 ms** (§23 #60, `gg-bench --canvas`, headless at
  3200 × 1200, idle) is partly the contention (the load average was 7 to 12 times the design's
  bar of 1.5) and partly that this is the live readout, which adds `drawImage` and the overlays
  to the benchmark's clear, splat and resolve. How much is which is unknown until the live
  readout is read idle, which is Task 18's run.
- **Frame rates differ, and don't matter to the per-frame figures.** The display runs at
  240 Hz. The CPU canvas repaints on every VBlank and reaches about 160 frames a second (a 4 ms
  paint can't make 240), the Metal spike renders about 150 a second and skips the VBlanks
  that find all three drawables out, and JUCE's GL thread rendered about 95 a second. That
  last rate is read off the frame count (1,916 in 20 s). Its cause was not checked; my
  inference, from reading `juce_OpenGLContext.cpp`'s `renderFrame` and not from any
  measurement, is that JUCE's macOS GL path paces its renders from a display link of its own
  and a vsynced swap, and why that would land at 95 rather than 120 or 240 was not
  investigated. Each figure is per rendered frame.
- **A note the task didn't ask for:** on this display the CPU canvas spent about 65% of the
  message thread painting (about 156 frames a second × 4.1 ms, under this contention; at the
  idle headless figure of 2.87 ms and the same rate it would be about 45%, an estimate, since
  the rate itself would rise as the paint got faster). The cause is inferred from the frame
  count, not traced: in stress mode the canvas's `VBlankAttachment` is its only repaint
  source, and 3,123 frames in 20 s on a 240 Hz display is a paint on every VBlank it can make.
  §16's budget is per frame and is expected to hold idle; whether the canvas should cap its
  repaint rate on displays above 120 Hz is a question for the controller, not this spike.

## What each candidate reached

**Candidate 1, JUCE OpenGL (built, measured).** `juce_opengl` compiled once as
`gg::juce-opengl-impl` (`libs/thirdparty-impl/JuceOpenGLImpl.mm`, beside `stb_impl`, with
`gg::juce-headers`' definitions and no `gg::compiler-options`; macOS only on the spike branch,
linking the OpenGL framework), and `GlGrainCanvas` (`ui/src/GlGrainCanvas.{h,cpp}`): an
`OpenGLContext` attached to a component in the canvas's place, a 3.2 core request (Apple gives
4.1), component painting off, the grains as instanced quads (`glDrawArraysInstanced`, a unit
quad plus a per-instance `(x, y, r, i)` stream), the splat's `max(0, 1 − d²/r²)` in the
fragment shader, additive (`GL_ONE, GL_ONE`) into an RGBA16F framebuffer the lanes' size, then a
resolve over the lanes' rectangle sampling it, `1 − exp(−min(a, 30))`, tinted by the lane band
the row falls in, over the lane ground. `GL_TIME_ELAPSED` queries bracket both passes (four in
flight, the oldest read when available). Two stumbles, each one build: `half` is a reserved
word in GLSL (the first run drew nothing and timed 156 ms of GPU error), and
`setOpenGLVersionRequired` is deprecated in JUCE 9 (`setPreferredVersion`). Not reached:
drawing the real grain queue and the overlays (the peaks, the scan rectangles, the freeze tint
and the marker, which a GL design would have to draw either in GL or by JUCE over the GL view,
and JUCE painting over an `OpenGLContext` re-enables the message-thread lock the spike turned
off), resize, theme, the hosts, the x86_64 slice, Linux and Windows.

**Candidate 2, Metal (built, measured; time remained).** `MetalGrainCanvas`
(`ui/src/MetalGrainCanvas.{h,mm}`, compiled with ARC, linking Metal and QuartzCore): a
`CAMetalLayer` on an `NSView` the canvas hosts through `juce::NSViewComponent`, the shaders in
MSL compiled at run time, the same two passes (an RGBA16Float accumulator with additive
blending, then the resolve into the layer's BGRA8 drawable), the instance buffer
triple-buffered in shared memory, rendered from the canvas's `VBlankAttachment` on the message
thread. The first version blocked the message thread for 7.9 ms a frame in `nextDrawable`,
because the 240 Hz VBlank outran the drawables; the measured version skips a VBlank that finds
three drawables out (a presented handler returns them), which is what a production renderer
would do. GPU time from `GPUStartTime`/`GPUEndTime` in the completed handler. Not reached: the
same list as the GL candidate's, and a render thread of its own (the spike's message-thread
render was chosen so its cost is UI-thread time by construction).

**Not tried:** anything in a host, under Rosetta, on Linux or Windows; Visage, by the spec (§5).

## The risks found

**JUCE OpenGL.**
- **Deprecated on macOS since 10.14.** It still runs on macOS 27.2 (a 4.1 core context over
  Apple's GL-on-Metal), and JUCE 9 keeps its macOS backend behind
  `JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS` (`juce_OpenGL_mac.h`). Apple has given no removal
  date and has dropped it from visionOS; a renderer the platform owner has deprecated is a new
  platform risk by definition, and one we cannot retire.
- **The hosts.** Not measured. A JUCE `OpenGLContext` in a plugin is an `NSOpenGLView` subview
  inside the host's layer-backed view hierarchy on macOS, a WGL child window on Windows and an
  EGL surface on Linux; M0's host matrix (Live 12.4.7, Bitwig 6.1.5) was run with the CPU
  renderer only, and the strict signing Live needs (`docs/spikes/m0-host-matrix.md`) says
  nothing about GL views. Resize flicker, black views on reopen and compositing with the
  host's own GPU use are the known shapes of trouble; none was ruled out here.
- **Context loss.** A GL context can be lost on Windows (display reconfiguration, driver
  resets, remote desktop) and must be rebuilt, shaders and framebuffers included; the spike
  implements `newOpenGLContextCreated`/`openGLContextClosing` but never exercised a loss.
- **The x86_64 slice.** CI's mac-universal leg runs the x86_64 tests under Rosetta; a GL
  context under Rosetta was not run. The slice would be untested on exactly the path it differs
  on.
- **Linux compositors and CI.** JUCE 9's Linux backend is EGL: a new build dependency
  (`libegl-dev`/`libgl-dev`; the Linux leg installs only `ninja-build xvfb libxi-dev`), a new
  run-time dependency on a working EGL display, and under `xvfb-run` (pluginval in CI) at best
  Mesa's llvmpipe, which turns the GPU path back into a slower CPU path. X11 versus Wayland
  hosts, and the static-libstdc++ export map (INTENT §15) with a dynamically loaded GL
  stack, add variance we don't have today.
- **Windows drivers.** Driver variance (integrated GPUs in laptops, remote desktop without GL)
  is the classic source of GL plugin bug reports; nothing was run.
- **A second code path anyway.** Every one of the above needs a CPU fallback, so adopting GL
  keeps the CPU renderer and adds a renderer, plus the overlay question (JUCE painting over
  the GL view, or the overlays redone in GL).

**Metal.**
- **macOS only.** Windows and Linux keep the CPU renderer, so Metal is a second renderer by
  construction (spec §6.11 says so), with the overlay question above.
- **The hosts.** Not measured. A `CAMetalLayer` in a host-provided `NSView` is how Metal plugin
  UIs work, and Metal is Apple's supported API (no deprecation risk), but the spike ran in the
  standalone only.
- **A render thread.** The spike renders on the message thread to measure UI-thread time; a
  production renderer would want its own thread or `CADisplayLink`, and a non-blocking
  drawable strategy (the spike's first version blocked 7.9 ms a frame).
- **Shaders compiled at run time** (the spike) or a `.metallib` built at configure time (a new
  build step, Xcode's `metal` compiler, absent from CMake's Ninja flow today).

## The decision against the criterion

1. **Does a candidate beat the CPU renderer on the §16 budget?** Yes, both: 20× and 45× on
   the thread that renders (0.20 ms and 0.09 ms against 4.1 ms here), 6.6× and 12.5× on
   whole-process CPU, measured at one load, plus about 0.4 ms of GPU that the budget doesn't
   count.
2. **With no new platform risk?** No. JUCE OpenGL adds the deprecation on macOS and five
   platform risks the timebox could not rule out, each of which counts against it. Metal adds
   none on macOS that the spike found, but it is macOS-only, so it would be a second renderer
   beside the CPU one, with the hosts unmeasured.
3. **So the CPU renderer stays,** as spec §6.11 expected. It is expected to hold the budget
   idle (the headless 2.87 ms at 8,192 grains, §23 #60, leaves 0.13 ms for `drawImage` and the
   overlays; the live idle readout is Task 18's), and M0's levers (resolve only the rows grains
   touched; a narrower resolve) stand before any GPU path.

**What the spike adds for later** (not a decision): if the budget ever breaks, on a larger
canvas or floor hardware, **Metal is the macOS lever,** not OpenGL: it reproduced the CPU
renderer's picture to 0.06%, costs about 0.1 ms of UI thread and 0.4 ms of GPU at 2960 × 1200,
and stood up in under two hours on the spike branch. JUCE OpenGL is the only cross-platform
candidate, and carries the deprecation. The spike branch keeps both, unmerged, for that day.

**INTENT:** this spike resolves §22's "GPU backend spike" row and §17's "A GPU backend gets a
timeboxed spike in M2" sentence; the amendments and the §23 row are the sub-project's close
(spec §6.11), not this task's.

## Reproduce

On the spike branch (`spike/gpu-canvas`), macOS:

```sh
cmake --preset release
cmake --build --preset release --parallel 6 --target gg_clapfirst_standalone_fx
app="build/release/gg_assets/Giga Grains FX.app/Contents/MacOS/Giga Grains FX"
export GG_STRESS_GRAINS=8192 GG_EDITOR_SIZE=1624x762 GG_FRAME_LOG_SECONDS=20
"$app"                        # the CPU path: gg-frame renderer=cpu ui_ms=...
GG_GPU_CANVAS=gl "$app"       # JUCE OpenGL: gl_thread_ms, gpu_ms, and the frame-120 readback
GG_GPU_CANVAS=metal "$app"    # Metal: ui_ms, wait_ms, gpu_ms, skipped, and the readback
```

Each prints one `gg-frame` line to stderr at 20 s and exits. Run them interleaved, five times,
and read `uptime` beside each; on an idle machine (load average under 1.5) the absolute
figures are the ones to record.
