# M2 sub-project 6: Views — design

| | |
|---|---|
| **Status** | **Approved by the programme manager 2026-10-30** against the programme design and INTENT, with two notes: the FX view's Reverb section in PR 1 is accepted (§6.1), and the INTENT §23 row takes the next free number at merge, after sub-projects 3, 2, 4 and 5 (§13). **The director's decisions D1–D11, 2026-10-30,** relayed by the programme manager, are recorded in §8 and applied in place: every recommended option (D1 to D11, option 1 each). Sub-project 5 replaces the contents of the header label `header.preset` with its preset strip and browser button; this sub-project keeps that ID and bounds slot and moves nothing else in the header's left group (§4). **Built (PR 1), 2026-10-30** (the plan: `docs/superpowers/plans/2026-10-30-m2-views.md`): where the build changed what this document said, the text is marked **Amended** in place with its ruling's ID (`VW-T<task><letter>`), and §16 lists every ruling. **PR 2 pending:** it waits for the program manager's word that wave 2 and sub-project 5 have merged. |
| **Milestone** | M2 Giga (INTENT §20, §23 #64), sub-project 6 of 7; wave 3, merged second, after sub-project 5. Started with wave 2 (programme design §6, "Wave rules"): the work that needs only wave 1 is built now; the work that needs wave 2's parameters waits until wave 2 has merged and this branch is rebased. |
| **Spec authority** | `INTENT.md`, then `docs/superpowers/specs/2026-10-29-m2-programme-design.md` (its §6.6, §7 and §12 are this sub-project's charter: realized here, not reopened). This document records how this sub-project implements them. |
| **Builds on** | M2 sub-project 1 as built (§23 #65): four layers, the morph law (D1), the Source kind, Pitch and Size Spread, the 48-slot mod matrix and its sources, the 218 parameters, the patch at version 2, the per-layer grain events and live peaks; M1's Perform view (§23 #58): the canvas, `LaneModel`, `LaneState`, `SplatRenderer`, `MacroRow`, `LoaderStrip`, the UI → host gesture path and the audio → UI publish path; sst-jucegui at `22a89ca`. |
| **Feeds** | Sub-project 5 (the header's preset name, which its browser replaces; the undo stack's whole-state entry, which Mutate and preset recall push); sub-project 7 (the pack, the canvas figure, the GPU spike's decision). |
| **Starts from** | `main` at `ab9f4e2` (sub-project 1 merged, PR #15), and `docs/m2-carry-over.md` with **11 open rows** (51, 85, 87, 88, 144, 148, 184, 217, 218, 221, 232). |
| **Two PRs** | Allowed by programme design §6.6, taken here (§6.1): **PR 1**, everything that needs wave 1 alone; **PR 2**, after wave 2 and sub-project 5 merge, the panels and readouts for wave 2's parameters and operations. Both count as sub-project 6 for the carry-over. |

## 1. Why this sub-project

INTENT §17 describes an editor with four views: Perform (lanes, macros, a layer strip and an XY
pad), Edit (per-layer panels), Mod (drag a source onto a control) and FX (the global chain), with
undo/redo, keyboard navigation, light and dark themes and a colour-blind-safe layer palette. M1
built one lane and the macros (§23 #58). Sub-project 1 built four layers, the morph, the spreads
and the full mod matrix, headless by choice (§23 #65): today layers B to D, the pad, the spreads
and 36 of the 48 routes are reachable only through a host's generic editor, automation and the
remote-controls pages. Every M2 feature after wave 1 lands as more parameters that the UI must
show.

This sub-project gives those features their faces. It delivers the programme design's inventory
items I30 to I36, I29's undo/redo and I38's gestures (§6.6 there):

- **I30** the Perform view completed: one lane per layer, the layer strip, the XY pad, pitch
  spread on the canvas's y axis;
- **I31** the Edit view: per-layer panels, each parameter with its base value, its spread and its
  modulation depth;
- **I32** the Mod view: drag a source onto a control; depth shown as rings;
- **I33** the FX view: the global chain, with carry-over row 217 decided;
- **I34** the About box with the notices and licenses (row 221);
- **I35** §17's basics: keyboard navigation, light and dark themes, the layer palette, resizing
  and HiDPI kept;
- **I36** the timeboxed GPU-canvas spike;
- **I29** undo/redo; **I38** the Perform gestures (freeze now; record, capture, drop onto any
  lane and splice marks once sub-project 2's operations exist);
- **I11**'s readout: the memory cap shown (after sub-project 2).

It changes no audio. Every one of the 36 goldens keeps its WAV SHA-256 throughout, and the
plugins render bit-identically with the editor open or closed, as M1's do.

## 2. Goals

1. **Every parameter is reachable from the UI** (the programme design's exit): a machine check
   walks the 218 slots, and wave 2's additions after the rebase, and fails on any that no view
   shows.
2. **The canvas tells the truth for four layers** (INTENT §8.7): each lane draws its own layer's
   grains, in that layer's colour, over its own ring or sample, with its own scan window, freeze
   tint and feedback marker; a grain's height is its pitch, so Pitch Spread and the Pitch macro
   are visible.
3. **The frame budget holds with four lanes:** `gg-bench --canvas` at 8,192 grains stays at or
   under 3 ms idle on the reference core, and the host check at the close confirms Live and Bitwig
   (programme design §7.11).
4. **Direct manipulation grows** without losing M1's: Position and Spray drags as in M1, plus
   Pitch and Pitch Spread on the vertical axis, per-lane freeze, the pad, the strip, and a file
   dropped onto a lane.
5. **The mod matrix is editable** from the UI: every route, every source, every target, in both
   products, saved in the state as sub-project 1 defined it.
6. **Undo/redo** covers what the editor does, survives closing the editor, and plays back through
   the host-visible gesture path so hosts record it.
7. **The basics:** keyboard-navigable, two themes, a palette that four kinds of colour vision
   tell apart, resizable and HiDPI as before.
8. **The About box** shows the same notices and license texts the installers carry, closing row
   221.
9. **The GPU spike** ends in a document and a §23 decision, within its timebox, with the CPU
   renderer kept unless the spike beats it on the §16 budget with no new platform risk.
10. **Nothing moves a golden**, and the audio thread's cost with the editor closed is unchanged.

## 3. Non-goals and anti-goals

**Non-goals (this sub-project)**
- No new parameter (programme design §7.1: wave 3 adds none). The theme, the lane's axis mode
  and the undo stack are not parameters.
- No DSP change. Row 217's answer is chosen so that it needs none (§8, D10).
- No preset browser, tags, favourites, INIT or Mutate (sub-project 5's). The header keeps a
  preset-name slot for its browser to replace.
- No sample loading for layers B to D or for the FX until `MEDI` version 2 exists (sub-project 2):
  PR 1 drops files onto layer A's lane in the instrument alone; PR 2 extends it.
- No record, capture or splice gestures until the ring's operations exist (sub-project 2): PR 2.
- No memory readout until the cap exists (sub-project 2): PR 2.
- No per-grain filter panel (M5) and no Filter panel until sub-project 4's parameters exist: PR 2.
- No new stress panel in the editor: the stress workload stays a test seam and `gg-bench`'s.
- No accessibility audit beyond keyboard navigation and sst-jucegui's accessibility handlers
  (M6, INTENT §20).

**Anti-goals**
- The editor must never change the audio: no view may compute anything the audio thread reads
  except through the existing gesture path and the new route path (§6.6), both main thread →
  queue → audio thread.
- No publish structure may allocate, lock or grow on the audio thread; the additions to
  `UiSnapshot` are plain trivially copyable fields.
- No second UI framework. Visage is not evaluated (§6.11).
- No decoration: every pixel of the canvas means something the engine is doing (§8.7).

## 4. Constraints

- **CLAUDE.md's non-negotiables** and INTENT §14's determinism rules. The editor reads the engine's
  state through `GrainQueue`, `SnapshotBuffer`, `LivePeaks` and the published clock, and writes
  base values through the plugin's UI queue; both stay as M1 built them, extended by addition.
- **Ownership** (programme design §6.6 and the brief): this sub-project owns `ui/`,
  `plugin/src/editor/` and the spike under `docs/spikes/`, but not the browser's files
  (sub-project 5's). Shared files are edited **by addition only**: `core/include/gg/core/
  UiSnapshot.h` and the `Processor`'s publish (per-lane fields, §6.4), `plugin/src/
  GigaGrainsPlugin.h/.cpp` (the route path and the undo hooks, §6.6, §6.8), `core/include/gg/
  core/MachineSettings.h` (the theme attribute, §6.9), `cmake/products.cmake` (the license list
  factored into `cmake/licenses.cmake`, §6.10), `tests/CMakeLists.txt`, `tools/gg-bench`
  (`--canvas`'s resolve, §6.12) and INTENT. **Amended** (VW-T6a): `media/src/SettingsFile.cpp`
  too, by one addition: `saveSettings`' NUL guard covers the `theme` field this sub-project
  added. Also by addition, as built: the root `CMakeLists.txt` and `.github/workflows/ci.yml`
  (`gg-snap` and its artifact, §6.13).
- **The header's preset name** is coordinated with sub-project 5 through the programme manager:
  the label `header.preset` keeps its component ID and bounds slot; sub-project 5 replaces its
  contents with the browser's control. This sub-project moves nothing else in the header's left
  group.
- **Wave 2's parameters are unknown here by name.** The Edit view's panels are driven by a table
  keyed on `ParamIds.h`'s rows, so PR 2 adds rows, never redesigns (§6.5).
- **sst-jucegui at its pin** (`22a89ca`): its knobs, sliders, toggles, menus, `MultiSwitch`,
  `VSlider`, `NamedPanel`, `TabbedComponent`, `MarkdownComponent`, the light and dark built-in
  style sheets, and its keyboard traverser. No submodule edit; no bump unless sub-project 5's
  clap-wrapper bump forces one (programme design §8).
- **GUI tests run on macOS** (the existing `[plugin]` GUI tests are `#if defined(__APPLE__)`:
  Linux CI has no display for JUCE's GUI start-up, and Windows isn't tried). The new ones follow;
  everything framework-free (`gg-ui-core`) runs on every leg and under the sanitizers.
  **Amended** (VW-T3b): the premise was wrong. M1's GUI tests sit inside `#if GG_BUILD_EDITOR`
  with no `__APPLE__` gate, so they build and run on every leg that builds the editor; the new
  ones follow them there. Only `snap_cli` (§6.13) is macOS-only. The sanitizer presets build no
  editor, so no GUI test runs under them.
- **The frame budget** (INTENT §16): at most 3 ms of UI-thread time per frame with 8,000 visible
  grains; the idle `gg-bench --canvas` at 8,192 grains with four lanes is the machine check.
- **Licensing:** the About box reproduces `THIRD_PARTY_NOTICES.md` and the license texts verbatim;
  nothing is paraphrased. Only the words the notices use; never "Mutable Instruments", "Clouds"
  or "Beads".

## 5. Approaches

**The canvas for four layers.** Three were weighed:
1. **Four `GrainCanvas` components**, one per layer, each with its own image: simple reuse, but
   four resolves of four images, four VBlank attachments, and an overlay (the pad's ghost, the
   drop highlight) that spans lanes becomes awkward.
2. **One canvas, four lanes, one accumulator, a per-band tint in the resolve** (chosen, §6.3):
   the resolve cost stays proportional to the canvas's area, as M0 measured it; each lane's rows
   take its layer's colour; one component owns the drags and the drop.
3. **Four accumulators blended in one resolve:** per-layer colour even where lanes overlap, but
   lanes don't overlap, and it quadruples the resolve's reads.

**The Edit view's scope.** One layer at a time, chosen in the layer strip that Perform and Edit
share (§8, D5), rather than four columns of 48 controls, which don't fit 900 px, or one layer with
a "link" switch that edits all four, a hidden multiplier of every gesture.

**The Mod view.** INTENT §17 asks for drag-and-drop onto controls and rings around them. A route
list alone would satisfy "every route editable" but not §17; a grid of 24 sources by 218 targets
is unreadable. Chosen (§8, D6): the Mod view is the selected layer's Edit panels under a strip of
source chips, every control a drop target and a ring bearer, with the 48-slot route list beside
them for what a drop can't express (curve, source layer, target layer, clearing).

**Undo.** Three homes were weighed: the host's (most hosts don't undo a plugin's internal
gestures, and INTENT §17 promises undo/redo), an editor-lifetime stack (lost when the window
closes) and an instance-lifetime stack in the plugin's editor object (chosen, §6.8): it survives
the editor's close and open, and sub-project 5 can push whole-state entries into it.

**Themes.** sst-jucegui ships light and dark style sheets; the editor's own colours (`Theme.h`)
become a palette struct with a light and a dark instance. Where the choice is kept: a per-machine
setting (chosen; a theme is a property of the screen, not the session), not the state.

**The GPU spike.** Candidates ordered by platform risk (§6.11): JUCE's own `juce_opengl` first
(cross-platform, already in the JUCE tree we build), native Metal second (macOS only). Visage is
rejected without measuring: a second UI framework beside JUCE and the shim is a new platform
risk by definition, which the programme design's criterion excludes.

## 6. Design

### 6.1 The two PRs and what each needs

**PR 1 (needs wave 1 alone; built now):**
- the editor's frame: header with the four view buttons, undo/redo, the About button; the views
  as a `TabbedComponent`-style stack with one visible (§6.2);
- the Perform view: four lanes, the layer strip as the lanes' header column, the XY pad, the
  vertical axis as pitch, the rectangular scan window with its four drags, per-lane freeze, the
  instrument's file drop onto lane A (§6.3);
- the publish additions (§6.4);
- the Edit view for every parameter the tree has at the rebase (§6.5);
- the Mod view and the route path (§6.6);
- the FX view's frame with its Reverb section and row 217's behaviour (§6.7; the brief listed the
  FX view under wave 2's work, but its reverb needs only M1, and the delay and limiter sections
  slot in by the same table in PR 2: flagged for the programme manager);
- undo/redo (§6.8);
- themes, the palette, keyboard navigation (§6.9);
- the About box (§6.10), closing row 221;
- the GPU spike (§6.11);
- `gg-bench --canvas` with the four-lane resolve (§6.12);
- `gg-snap`, the screenshot tool, and the pack (§6.13, §11).

**PR 2 (after wave 2 and sub-project 5 merge, this branch rebased):**
- Edit panels for wave 2's parameters: Quality and anything else sub-project 3 appends; the ring
  length, the memory cap and the per-layer samples of sub-project 2 (the FX's loader strip, drops
  onto lanes B to D, `MEDI` version 2); sub-project 4's Filter and buses panel and Smear;
- the FX view's Delay and Limiter sections;
- the memory-cap readout;
- the record, capture and splice gestures on the lanes;
- the header's preset name as sub-project 5 built it, if anything of this sub-project's must move.

PR 2's table rows are the only design work left: each new parameter gets a (panel, order) row,
and each new ring operation a gesture mapped to sub-project 2's API.

### 6.2 The editor's frame

- **The window** opens at **960 × 600** logical pixels (D11; M1's was 900 × 500) and resizes,
  HiDPI-aware, down to **640 × 360** as before. The plugin's `guiAdjustSize` keeps the minimum.
- **The header** (40 px): the product name (click: the About box), the preset-name slot
  (`header.preset`, sub-project 5's), the four view buttons **Perform · Edit · Mod · FX**
  (`header.perform` … `header.fx`, a radio group), undo and redo glyph buttons (`header.undo`,
  `header.redo`, each with its tooltip naming what it would undo), a theme switch
  (`header.theme`: Dark or Light, D8), and the frame-time readout at the right as in M1.
  **As built:** the theme switch is a push button labeled with the current theme, and the header
  also holds `header.notice`, where notices show for 4 s in the frame time's place. **Amended**
  (VW-T15a): a component posts a notice through the component tree (`postNotice`, which finds the
  Editor, a `NoticeSink`); with no editor above it, as in a test that builds the component alone,
  it falls back to `context.notice`. The plugin no longer sets `context.notice`.
- **One view is visible at a time.** The views are children of a `ViewStack`; switching is a
  show/hide, so the canvas keeps its grains, and the view chosen is remembered per instance
  while it lives (not saved). Each view is a `juce::Component` under `ui/src/views/`.
  **Amended** (VW-T15b): "per instance" is the plugin instance. `EditorPlugin` keeps the last
  view chosen (neither saved nor a parameter), and an editor opens on it, so closing and
  reopening the editor keeps the view; a new instance opens on Perform.
- **The layer strip is shared:** Perform shows it as the lanes' header column (§6.3); Edit and
  Mod show the same strip, compact, at the top, and the layer it selects is the one their panels
  edit. Selection is UI state, per instance, not a parameter.
- **Files dropped on the editor** go where M1 sent them (the instrument's loader strip) unless a
  lane takes them (§6.3).

### 6.3 The Perform view

**Layout** (top to bottom): the instrument's loader strip (as M1; the FX's arrives in PR 2); the
canvas, four lanes stacked, equal height, each with a header cell at its left (D3); the bottom
row: the XY pad (a square of the row's height) and the macro row (the rest of the width, as M1).

**The lane header** (the layer strip's cell; 120 px wide, the lane's height): the layer's letter
in its colour, large (click: select the layer for Edit and Mod; the selected one is outlined);
the On toggle (`Layer A On` …); a Level slider (`Level`, `Level B` …), horizontal; the Source kind
as a menu button (`A Source` …); a Freeze toggle (`A Freeze` …). Every control is a gesture on
its parameter, as the macros are, with `HeldValue`. An off layer's lane and header are dimmed,
its peaks still drawn.

**The lane's body.** Each lane is M1's lane for its own layer, by that layer's **built kind**
(`builtKind` of the Source's base value, ruling FR-6's rule):
- a **Live** layer's lane is the FX lane of M1: the ring with the write head fixed at the right
  edge, history scrolling left, its own `LivePeaks` (`Processor::livePeaks(layer)`), the scroll
  stopped while that layer is frozen;
- a **Sample** layer's lane is M1's instrument lane: static, x the position across the layer's
  sample. In PR 1 only layer A can hold a sample; a Sample layer with none shows "Load a sample,
  or drop one here" in its own lane;
- an unbuilt kind draws as the kind it renders as (`builtKind`), so the lane is never blank.

So the FX and the instrument no longer have different canvases: a lane's kind is its layer's. The
instrument with layer B set to Live shows a scrolling ring in lane B under a static sample in
lane A, which is what the engine does.

**Grains.** The canvas drains the queue as M1 did and keeps every layer's grains
(`LiveGrains` per layer, 8,192 across the four, dropping the oldest), splatting each into its
lane's rows of the one accumulator. **Amended** (VW-T9a): each layer's list holds 8,192 (four
lists, about 1.3 MB), so a dense layer can't push another layer's grains out. **The vertical
axis is pitch** (D1): a grain at rate `r` lies
at `y = laneCentre − 12·log2(|r|) × (laneHalfHeight − radius) / 48`, so ±48 semitones span the
lane (Pitch ±24 plus Pitch Spread ±24), clamped to the lane; a grain with `r ≤ 0` or non-finite
draws centred. **Amended** (VW-T2c): the `|r|` above never applies, since a reverse grain
(`r < 0`) draws centered, as `r = 0` does; the formula holds for `r > 0`. Width is its length,
brightness its gain, as M1. Pan is no longer drawn (D1's
cost). `grainCapsule` takes a lane geometry with `top`, `height` and `pixelScale`; the maths lives
in `LaneModel` as before, tested without JUCE.

**Amended** (VW-T9b): the lanes are half-open row ranges, `[H·L/4, H·(L+1)/4)` of the canvas's
height `H`, so a row two lanes would share belongs to the lower lane and a press reaches one
lane. A press in a lane selects its layer, as a click on its letter does, even when it grabs
nothing (VW-T9c).

**The resolve** tints each lane's rows with its layer's colour (`SplatRenderer::resolveToArgb`
gains a banded overload: a list of `{rowBegin, rowEnd, rgbTint}`; the M1 overload is the one-band
case and stays). Lanes are separated by a 1 px rule in the outline colour; nothing else is drawn
between them.

**The scan window becomes a rectangle** (D2): x as M1 (Position and Spray, one-sided on a Live
lane, bipolar on a Sample lane), y from the layer's base Pitch ± Pitch Spread on the pitch axis.
The ghost window is the same rectangle at the effective values (Position, Spray, Pitch and Pitch
Spread from the snapshot, §6.4), dashed, when any of the four differs by more than
`kGhostThreshold` in its own 0–1 domain. Drags, each a host-visible gesture as M1's:
- the left and right edges set **Spray** (M1's rule, PU-T8a included);
- the top and bottom edges set **Pitch Spread** (symmetric: the dragged edge's distance from the
  centre, in semitones, clamped 0–24);
- the body sets **Position** horizontally or **Pitch** vertically: the first 4 logical px of
  movement pick the axis, and the drag stays on it until release (axis lock), so a diagonal press
  edits one parameter; **as built** (VW-T9c), the body's gesture begins when the axis is
  decided, so a press that never moves 4 px is no gesture;
- a corner (within the edge tolerance of both) takes the horizontal edge. **Amended**
  (VW-T2a): "the horizontal edge" is the edge dragged horizontally, the Spray edge (left or
  right), as M1's x rules give it. On a Live lane the right side is the body (PU-T8a), so there
  the right corners take the top and bottom edges.

**Amended** (VW-T2b): **the body keeps a height to grab.** At Pitch Spread 0, the default, the
rectangle is one row high, so a press would almost always land on the top or bottom edge. The
body's half-height is `max(tol, H/2 − tol)` about the rectangle's center, where `H` is the
rectangle's height and `tol` the edge tolerance. The top edge grabs from `tol` above the higher
of the rectangle's top and the body's down to the body, never inside it, and the bottom edge
mirrors it. A band thinner than `2·tol` so grows to a `2·tol` body,
and a rectangle `4·tol` high or more keeps the plain rule (each edge within the tolerance either
side of it), so nothing jumps as the spread grows. The rule is named in `LaneModel.h`, and the
test table has rows for spread 0 and 2 on a 50 px lane.

`hitTest` grows a second axis; its tests enumerate every region.

**Freeze.** The header's Freeze toggle, and a double-click on a lane's body, toggle that layer's
Freeze (one gesture, value 0 or 1). The frozen lane keeps M1's tint.

**The feedback marker** is per lane: drawn at a Live lane's head while that layer's Feedback is
above 0 (the snapshot's per-lane value).

**A file dropped onto lane A** in the instrument loads it as the strip's drop does; a file
dropped on any other lane, or on an FX lane, is refused with the strip's notice ("layer B takes a
sample after the session format's next version") until PR 2, where every lane takes one through
sub-project 2's per-layer loader. The lane under a drag is highlighted.

**The XY pad** (`pad`): a square; its corners are the morph's, **A bottom-left (X 0, Y 0), B
bottom-right, C top-left, D top-right**, Y up, each corner lettered in its layer's colour; the
handle at (Morph X, Morph Y); a drag is one gesture on both parameters (begin X, begin Y, values
in pairs, end both); arrow keys move it by 1/100, Shift 1/1000; double-click returns it to the
centre (one gesture each). **Amended** (VW-T10a): a drag is one undo entry, labeled "Morph".
A real double-click is two entries, since JUCE delivers its first press as an ordinary click:
that press moves the handle to the clicked point (one entry), and the double-click centers it
(a second). One undo returns the handle to the clicked point, a second to where it started,
which is noted for the director's checklist. Under the handle the pad shades each quadrant by
the layers' current
weights (D1's law, computed in the UI from the base values and the On states as the engine
would, for the picture only), so an off layer's corner is dark. A ghost handle shows the
effective Morph X/Y when modulated (from the snapshot, §6.4).

**The macro row** is M1's, unchanged. The macros are in the mod matrix; a route's depth on a
macro's target shows in Edit and Mod, not here.

### 6.4 Audio → UI, by addition

`UiSnapshot` keeps M1's fields (layer A's, which M1's tests pin) and gains, in the same struct:

```
struct LaneSnapshot {           // one per layer, A to D
    int64_t writeHead;          // that layer's ring head (ring time)
    bool    frozen;             // its engine's freeze
    bool    on;                 // its enable gate is open or ramping
    uint8_t builtKind;          // gg::SourceMode it renders as
    float   basePosition, baseSpray, basePitch, basePitchSpread;
    float   effectivePosition, effectiveSpray, effectivePitch, effectivePitchSpread;
    float   feedback;           // in effect
};
std::array<LaneSnapshot, kLayers> lanes;
float baseMorphX, baseMorphY, effectiveMorphX, effectiveMorphY;
```

The `Processor` fills them where it fills M1's, from values it already holds (`base_`,
`effective_`, each layer's engine); no new computation, no allocation, trivially copyable, so the
RTSan and tripwire tests that cover the publish path cover these. The grain queue and
`GrainVis` are unchanged (sub-project 1 added `layer`). `livePeaks(layer)` already exists. The
editor context gains `livePeaksFor(layer)` and, for the pad and the strip, nothing new: they read
base values through `baseValue(slot)`.

**As built,** the editor context gained, by task: `livePeaksFor`, `sampleWaveformFor`,
`loadSampleFor` and `notice` (the canvas); `undo` (the pad); `routes` and `setRoutes` (the Edit
view's rings and the Mod view); `formatName` (the About box); `theme`, `setTheme`, `view` and
`setView` (the frame). M1's `livePeaks` (layer A's alone) is removed. `LaneSnapshot`'s fields are
the struct's above, with `on` from the `Processor`'s `layerRendering` (the on ramp's target or
value is non-zero); the snapshot grows from about 48 bytes to about 230.

### 6.5 The Edit view

**Shape** (D5): the compact layer strip at the top (the letter buttons, with On, Level, Source
and Freeze as in the lane headers); below, the selected layer's panels, each an sst-jucegui
`NamedPanel`, laid out in a wrapping grid of uniform cells (a knob or a slider with its label and
value) so a panel with one more parameter grows by one cell. The panels and their rows, from
INTENT §17 with the modulators added (every parameter must be reachable):

| Panel | Parameters (layer scope), in order | Later rows (PR 2) |
|---|---|---|
| **Source** | Source; the layer's sample strip (name, status, Load/Relink) for a Sample layer: layer A's in PR 1 | ring length, the per-layer sample (sub-project 2) |
| **Scheduler** | Density, Distribution, Freeze | — |
| **Grain** | Size + Size Spread, Position + Spray, Texture | Smear (sub-project 4), Quality (sub-project 3) |
| **Pitch** | Pitch + Pitch Spread | — |
| **Space** | Stereo, Diffusion, Feedback, Level | — |
| **Filter** | (empty until PR 2; the panel is hidden while its row list is empty) | the filter and buses (sub-project 4) |
| **LFO 1 / LFO 2** | Rate, Shape | — |
| **Env 1 / Env 2** | Attack, Decay, Sustain, Release | — |
| **Random 1 / Random 2** | Rate, Smooth | — |
| **Step** | Rate, Length, Step 1 … Step 16 as 16 vertical sliders | — |

The globals: Morph X/Y, the four On switches and the four Levels are the strip's and the pad's
(Perform); Mix, Reverb and Dry to Reverb are the FX view's (§6.7); the macros are the macro
row's. **Amended** (VW-T3c): the table places each slot once, and a layer's Level is placed in
its Space panel, as the table above lists it; the On switches are the strip panel's. The strip
(the lane headers and the compact row) also shows On, Level, Source and Freeze, as second
controls with the same `param.<id>`. A parameter may have a control in more than one view, so a
test that wants one control searches below its view's ID. **A pinned table**
(`ui/include/gg/ui/EditLayout.h`): one row per `LayerParam` and per
global slot, naming its view, panel and order, with "paired spread" links (Size → Size Spread,
Position → Spray, Pitch → Pitch Spread). A `[ui]` test walks every `ParamSlot` below `Count` and
fails, naming the parameter, if it has no row, or a row no panel draws; a `[plugin]` GUI test
walks the built editor and fails if any slot's control (`param.<id>`) is absent from every view,
except the slots the product hides (Mix and Dry to Reverb in the instrument). After the PR 2
rebase, wave 2's rows fail that test until they are placed: that is the design's "slots in
without redesign".

**The control** (`ParamControl`, generalizing `MacroRow`'s adaptor): a knob for a continuous
parameter, a horizontal slider for a 0–1 amount in a wide cell, a `MultiSwitch` or menu button
for a stepped one (Source, the LFO shapes, Step Length), a toggle for on/off, a `VSlider` for a
step. It shows the base value as the host's text (`ParamMetaData`'s display, so a Live layer's
Position reads in seconds and a Sample layer's in percent, ruling FR-6); edits are gestures with
`HeldValue`, Up/Right and Down/Left by 1/100, Shift 1/1000, as the macros; double-click resets
to the default (one gesture). A **spread pair** draws the spread's knob smaller at its base's
right, and the base knob's arc shows the spread's extent as a faint band around the base value
(Size ± its octaves, Pitch ± its semitones, Position ± Spray on a Sample layer and Position …
Position + Spray on a Live one), so "base value and spread" are one picture. **Modulation
depth** draws as rings around the knob (§6.6): one arc per route targeting this parameter on
this layer (or every layer), its length the depth in the parameter's range, its colour the
source's family, bipolar arcs centred on the base value. A control with no route has no ring.
**Amended** (VW-T11a): rings are drawn on knobs only, the spread pair's smaller knob included.
No slider needs them: the Step sliders, like every modulator's control, are no mod target
(`isValidTarget` refuses them), and `ringsFor` skips any target a route can't reach. The spread
band takes Position and Spray as their 0–1 values, as the lane's rectangle does. **Amended**
(VW-T12a): a toggle or a menu (Freeze, Source, an On switch) shows one 6 px dot per route at its
top right, in the source family's color, in slot order; a switch or a menu step has no arc to
draw a depth on, so its depth is edited in the Mod view's route list.

### 6.6 The Mod view and the route path

**Shape** (D6): the layer strip; a **source strip** of 24 chips in five groups (Macros 1–8 ·
LFO 1, LFO 2, Env 1, Env 2, Random 1, Random 2, Step · Wheel, Aftertouch, Bend, Velocity, Key ·
Follower · Beat, Bar, Playing), each chip in its family's colour, the per-layer ones labelled
with the selected layer's letter (`sourceLayer` = same); the selected layer's **Edit panels**,
the same components in "mod mode"; and the **route list** at the right (240 px, scrollable): 48
rows, each slot's source, source layer (A–D or same), target with its layer (A–D or all), depth
(a bipolar slider in the target's range), curve (a menu of the six), and a clear button; empty
slots read "—".

**Drag a chip onto a control** (`juce::DragAndDropContainer` at the editor): the route goes into
the first empty slot with `source` the chip's, `sourceLayer` same, `target` the control's
parameter (layer A's slot for a per-layer kind), `targetLayer` the selected layer, `curve`
Bipolar (Unipolar for a stepped or on/off target), and `depth` **a quarter of the target's range**
(positive), so the drop is heard and then shaped; a drop on a control that already has a route
from that source selects that route in the list instead. **Amended** (VW-T12b): a route to all
layers counts as that route for every layer, so Macro 1 dropped on layer B's Size selects
factory slot 1 (Macro 1 to Size on all layers) rather than adding a second route that would
modulate B's Size twice. The factory routes (slots 1–12) are
ordinary rows: editable and clearable, as sub-project 1 said. A full matrix refuses the drop with
a notice.

**Rings** on the controls are the routes (§6.5). In mod mode a ring is draggable: a vertical drag
on a control's ring sets that route's depth (the control's own value is not edited in mod mode:
the knob is inert there, so a drag never edits the wrong thing). Clicking a ring selects its row.
**Amended** (VW-T11b): inert includes the accessibility interface: in mod mode a control's value
source ignores `setValueFromGUI` and opens no gesture, so a screen reader's set, or a jog, edits
nothing either. **As built:** a ring drag is one undo entry ("Undo route N"), closed at the
release, at a switch out of mod mode or of the layer, or when the panel is destroyed; the list's
rows use JUCE's stock widgets, not sst-jucegui's.
Macro knobs in the Perform view take drops too (a macro as a target is refused by
`isValidTarget`, and the control says so), so the strip's targets are Edit's and the lanes'
header controls; the pad's Morph X/Y and the strip's Levels and On switches are valid targets
and take drops.

**The route path** (plugin, by addition). Today routes change only by a state load. The editor
gets `uiRoutes()` and `uiSetRoutes(const ModRoutes &)` on `GigaGrainsPlugin`: main thread;
`routes_` is updated (so a save carries them), the host's `state.mark_dirty` is called, and the
routes go to the audio thread through the existing `loadedState_` channel with a routes-only
flag (no seed, no values), applied in `applyLoadedState` by `setRoutes`, which recomputes
`timeVarying()` and jumps the ramps it moves (ruling LS-T12a). No gesture events: routes aren't
parameters. Each edit is one undo entry (§6.8). The `[plugin]` tests check a route set from the
UI reaches the render and the saved state, under the tripwire.

**Amended** (VW-T4a): **the routes take a channel of their own,** not `loadedState_`. A
`LatestValue` keeps only its latest value, so a UI edit pushed into the one channel would
overwrite a pending load. `uiSetRoutes` sets `routes_`, calls `mark_dirty` when the host offers
the state extension and, while active, pushes the routes onto `uiRoutesPending_`, a
`LatestValue<ModRoutes>` beside `loadedState_`; `applyLoadedState` takes the loaded state first,
then that channel, and applies it with `setRoutes`. Two additions make "the later write wins"
true in both orders: a state load while active also pushes its routes onto `uiRoutesPending_`,
before `loadedState_`, so the channel always holds the main thread's newest `routes_`; and
`deactivate()` drops an edit the audio thread never took, so it can't apply stale after the next
`activate()` (which hands `routes_` over itself). Re-applying identical routes changes nothing.
`tests/plugin/routes_tests.cpp` checks both orders in one block, the render (its bytes equal a
state load's with the same routes), the saved state, `mark_dirty`, an inactive edit and the
tripwire.

### 6.7 The FX view and row 217

**Shape:** sections across the width, each a `NamedPanel` of `ParamControl`s from the same
table: **Reverb** (Reverb Amount, Decay, Size, Damping, Pre-delay, Modulation, Dry to Reverb in
the FX), **Output** (Mix in the FX; the master Level is layer A's `Level`, shown in its lane; the
FX view names no Level), and in PR 2 **Delay** and **Limiter** as sub-project 4 defines them, and
Space's panel for the per-layer diffuser stays in Edit (INTENT §14: the diffuser is per layer).
Decay's text reads "∞" from `kDecayInfinite` up, as the host's does.

**Row 217 (Reverb Size clicks when moved) is decided here** (D10, recommended): the Size control
**commits on release**. While the knob is dragged it shows the target value, but no value event
is sent until the gesture ends, when one `setValue` goes with the gesture's end; the tooltip says
"Sets on release: the room's size changes in one step." Automation and host gestures are
untouched, so `fx-space-automate` and every golden keep their hashes; the row closes as decided,
its DSP alternative (two crossfaded reverb states) recorded in the row as the path if a future
sub-project wants a sweepable Size.

**As built** (`0d8969c`): `ParamValue`'s commit-on-release mode, set on the Reverb Size control
alone; a gesture that sets nothing sends no value. The tooltip reads "Sets on release: the
room's size changes in one step". The view has no viewport: its two panels fit 640 × 360,
which the frame's tests check. Row 217 is **Closed** by `0d8969c` (§12).

### 6.8 Undo and redo

**Where:** `UndoStack`, in `plugin/src/editor/`, owned by `EditorPlugin`, one per instance, alive
from construction to destruction, so closing and reopening the editor keeps it. Main thread.
**Amended** (VW-T5a): the class lives in `gg-ui-core` (`ui/include/gg/ui/UndoStack.h`),
framework-free and so tested under the sanitizers, and `EditorPlugin` owns the instance. It is
declared before the editor context and the shim, so it outlives every editor, and an editor
destroyed mid-drag closes its open gestures and group as one entry. `gg-ui-core` now links
`gg::core` PUBLIC (for `ParamIds.h` and `ModMatrix.h`).

**Entries** (D7):
- a **parameter edit:** `{slot, before, after}`, one per editor gesture (the `before` captured at
  `beginGesture`, the `after` at `endGesture`; a gesture that ends at its start value makes no
  entry); the pad's two parameters make one entry; a mod-mode depth drag is a route entry;
- a **route edit:** `{routes before, routes after}` (48 routes, 1.5 KB);
- a **whole-state entry:** `{values before, after; routes before, after}`, pushed by a caller that
  changes everything at once: sub-project 5's Mutate and preset recall (the API is
  `pushSnapshot(label, before, after)`; this sub-project pushes none, and the test pushes one).

Depth 200, the oldest dropped. Host automation, state loads and the audio thread's own changes
push nothing; an undo applies its `before` through the gesture path (`uiBeginGesture`,
`uiSetValue`, `uiEndGesture`, so hosts record it as automation and `HeldValue` shows it) or
through `uiSetRoutes`, and moves the cursor; redo applies the `after`. A redo stack is cleared by
a new entry. The header's buttons show the entry's label as their tooltip ("Undo A Size",
"Undo route 13", "Undo Mutate"); keys: ⌘Z / Ctrl+Z and ⇧⌘Z / Ctrl+Y while the editor has focus
(a host that eats them still has the buttons).

**Amended** (VW-T15e): a parameter entry's `before` is the control's shown value when its
gesture begins, not `base`, which lags the audio thread just after a gesture; so consecutive
entries chain. **Amended** (VW-T15d): **no undo mid-gesture.** While any gesture or group is
open (`busy()`), undo and redo do nothing and the header's buttons are disabled: the open
gesture belongs to its control, which alone can end it consistently with its `HeldValue` and the
host. **Amended** (VW-T15f): while undo or redo applies, the stack ignores every begin, end,
group, push, undo and redo its callbacks cause (`applying()`). **As built,** the keys are the
command modifier (⌘ on macOS, Ctrl elsewhere) with Z for undo, and with Shift+Z or Y for redo; on
macOS Ctrl+Y does nothing. Buttons with nothing to apply are disabled, their tooltip "Nothing to
undo" or "Nothing to redo".

### 6.9 Themes, the palette, keyboard navigation, HiDPI

**Themes** (D8): the dark theme is M1's (sst-jucegui's dark sheet and `Theme.h`'s colours); the
light theme is sst-jucegui's light sheet and a light palette for the editor's own surfaces and
text. `Theme.h` becomes `Palette` with `dark()` and `light()`; every component reads the editor's
current palette through a `ThemeProvider` rather than constants, and a switch restyles the tree
in place (`setStyle` on the root, `repaint`). **The lanes stay dark in both themes** (D9): the
canvas is additive light on a dark ground, and the resolve, the grain tints and the peaks are the
same in both. **The choice is per machine:** `settings.xml` gains a `theme` attribute (`dark` or
`light`; missing means dark), read and written on the main thread through gg-core's
`MachineSettings` and gg-media's settings file, by both products (the FX reads the file for this
alone; the `GG_SETTINGS_FILE` test seam applies). Every open editor of every instance in the
process follows a change at its next timer tick (they poll the setting; no cross-instance
messaging).

**As built:** `Theme.h` is gone. `Palette` (`ui/src/Palette.h`) has `dark()` and `light()`, and a
component reads the current one through `paletteOf`, which walks up its parents to the
`PaletteOwner`, the Editor, and gives `dark()` when there is none. The lanes' overlays (the rules
and the text on the lanes) take `laneRule` and `laneText`, the same in both palettes (D9). The
setting: `MachineSettings::theme`, a string, empty while unset; the file carries `theme` only once
it is chosen, so a file that never had one reads exactly as M1 wrote it. `readTheme` gives Light
for `light` alone and Dark for anything else (missing, unreadable, `Light`); `writeTheme`
rewrites the whole file and returns false, never throwing, when it can't, and the header then
says so in its notice while the editor keeps the theme chosen. `plugin/src/editor/UiSettings.cpp`
holds both, and the UI's `ThemeChoice` and the plugin's `ThemeName` are mapped in `EditorPlugin`.
The editors poll every 30th tick (once a second) and follow only a change of the setting since
their last read or write, so a failed save isn't undone by the poll. **Amended** (VW-T6a): the
settings file's NUL guard (`saveSettings`) refuses a `theme` holding a NUL, as it does the
folders.

**The layer palette** (D4): Okabe–Ito's four most separable hues, which deuteranopes,
protanopes and tritanopes tell apart: **A orange `#E69F00`, B sky blue `#56B4E9`, C bluish green
`#009E73`, D reddish purple `#CC79A7`**. They are the grain tints, the lane letters, the pad's
corners and the source chips' layer tags. Colour is never the only cue: every lane, cell and
chip carries its letter. The light theme uses the same four (they are mid-luminance, legible on
both grounds). Source families in the Mod view use a second set (macros, modulators, MIDI,
follower, transport), also from Okabe–Ito's eight, distinct from the layer four. **Amended**
(VW-T3a): after the layers' four, Okabe–Ito leaves four hues, and one of them, black, can't be
seen on the dark ground. So macros are yellow `#F0E442`, modulators vermilion `#D55E00`, MIDI
blue `#0072B2`, the follower gray `#999999` and the transport the text color (near-white in
dark, black in light). The light palette darkens the macros' yellow to an ochre (`0xff8f7a00`,
about 4:1 on its ground; the yellow is about 1.2:1). The chips carry their names, so color is
never the only cue.

**Keyboard navigation:** every control takes focus (sst-jucegui's do; the canvas, the pad and
the lane headers get `setWantsKeyboardFocus`); Tab and Shift+Tab move in reading order through
sst-jucegui's `KeyboardTraverser`; arrows adjust the focused control (1/100, Shift 1/1000);
Space or Return toggles a toggle, opens a menu; Escape ends a drag or closes the About box.
View switching: ⌘1–⌘4 / Ctrl+1–4; layer selection: ⌘⇧A–D / Ctrl+Shift+A–D (letters the hosts
don't bind with those modifiers in Live or Bitwig; a host that eats them loses only the
shortcut). The focused control shows sst-jucegui's focus ring; the canvas's focused lane shows
its window outlined, and arrows then nudge Position (←→) and Pitch (↑↓), Shift the spreads.
**As built:** sst-jucegui's traverser numbers only its own parameter editors, per parent, so Tab
would never reach the canvas, the pad or the header's buttons. The Editor's traverser, a
subclass of it, numbers every shown focusable component in reading order before each move. The
canvas's keyboard lane is the selected layer's, so ⌘⇧A–D chooses it; each nudge is one gesture
and one undo entry.

**HiDPI and resizing** stay M1's: the canvas renders at physical-pixel resolution; every view
lays out at any size from the minimum.

### 6.10 The About box

Opened from the product name or ⌘I / Ctrl+I; an overlay panel over the editor (sst-jucegui's
`ModalBase` pattern; not a native dialog, so it works in every host), closed by its button,
Escape or a click outside. It shows:
- the product's name and **version** (`gg::kVersion`, so `0.2.0-dev` on `main` and the release's
  string on a tag), the formats this binary is (CLAP, VST3, AU or standalone; from the build's
  definitions), and the license line: "Giga Grains is free software under the GNU Affero General
  Public License, version 3 or later"; **Amended** (VW-T14a): the format is the one the host
  loaded, read at run time from clap-wrapper's public `clap-wrapper.host-information/0`
  extension (VST3, AU or Standalone; CLAP when the extension is absent), since every format wraps
  the same plugin code and no build definition tells them apart;
- the repository URL (`https://github.com/sethbc/giga-grains`) as a link, and a line saying each
  release attaches its complete corresponding source;
- a list at the left of the texts, and the chosen text at the right in a scrolling view:
  **Third-party notices** (`THIRD_PARTY_NOTICES.md`, rendered by sst-jucegui's
  `MarkdownComponent`), **License** (the AGPL), **GPL-3.0**, and the platform's SDK license
  (the AudioUnitSDK's Apache 2.0 on macOS, the ASIO SDK's on Windows), and the CMakeRC and
  flowbite-icons MIT texts: **exactly the installer's license set** (`cmake/products.cmake`'s
  list, factored into `cmake/licenses.cmake` so both the About box and `gg-products` read one
  list).

**Embedding:** a CMake script at build time (`cmake/embed-text.cmake`) turns each file of the
list into a `std::string_view` in a generated `Notices.cpp` under the build tree, compiled into
`gg-ui` (about 100 KB of text; no CMakeRC dependency added, no new submodule). A `[ui]` test
reads the generated texts and the source files and requires them byte-equal, so the box can't
drift from the installers, and the existing `installer_license_set` test keeps the installers
honest. This closes carry-over row 221.

**As built** (`2116004`, `2ad02b2`): `cmake/licenses.cmake`'s `gg_license_files` returns the list
and `gg_add_notices(gg-ui)` generates `generated/Notices.cpp` in the build tree, about 120 KB of
source for about 90 KB of text, with no literal over 16,000 bytes. The list at the left shows each
text's name, never its source path. A **Plain text** toggle shows any text as monospaced plain
text. **Amended** (VW-T15c): **the box is modal.** While it shows, Escape closes it and every
other key, ⌘I included, is its own, so nothing changes behind it; Tab stays among its own
controls, and the Editor's traverser hands focus to it; a click outside its panel closes it. Row
221 is **Closed** by `2116004` (§12).

### 6.11 The GPU spike

**Timebox:** one plan task, one agent session's day; whatever stands at its end is written up,
and the task cannot grow.

**Candidates, in order:**
1. **JUCE OpenGL:** `juce_opengl` compiled once in `libs/thirdparty-impl/` (as other
   implementations are, outside the shim and without `gg::compiler-options`), an
   `OpenGLContext` attached to the canvas, the grains as instanced quads with the splat's
   falloff in the fragment shader, additive blending into a half-float framebuffer, and a
   resolve pass computing `1 − exp(−a)` with the lane's tint. Cross-platform; OpenGL is
   deprecated on macOS since 10.14 (a risk the write-up names) and runs through ANGLE or a
   driver on Windows and Linux.
2. **Metal on macOS:** a `CAMetalLayer` under the canvas's `NSView`, the same two passes. macOS
   only, so it would be a second renderer beside the CPU one.

Visage is not tried (§5).

**Measurement:** the standalone app with `GG_STRESS_GRAINS=8192` (a test seam that makes the
canvas draw `stressGrains` instead of the queue, four lanes, 1600 × 600 logical at 2×), the
editor's UI-thread frame time (the header's readout) and, for a GPU path, the GPU frame time from
the API's timer queries, each the median of 240 frames, CPU path and candidate side by side on
the reference core, in the write-up's table with the conditions. The hosts' numbers are the
close's.

**The criterion** (programme design §6.6, verbatim): the CPU renderer stays unless the spike
beats it on the §16 UI budget with no new platform risk. The write-up (`docs/spikes/
m2-gpu-canvas.md`) states the numbers, the risks found (deprecation, driver variance, context
loss, Linux compositors, the x86_64 slice) and the decision; INTENT §17's renderer paragraph and
§22's row are amended and a §23 row records it. My expectation, stated now so the write-up can
contradict it: the CPU renderer stays, because the budget already holds and both candidates add
a platform risk.

**As built** (`2291278`, `docs/spikes/m2-gpu-canvas.md`; the spike code on `spike/gpu-canvas`
at `082a874`, unmerged). Both candidates were built and measured within the timebox, in under an
hour. **Amended** (VW-T17a): **the CPU renderer stays,** and the decision rests on the ratios
and the risk clause, not on absolute figures, so it is not provisional on the idle run. Measured
interleaved, five runs a path, under contention (load average 10.9 to 18.6, five agent sessions
on the machine), the canvas at 1600 × 600 logical at 2× with 8,192 stress grains: the CPU path's
readout 4.10 ms (round 2's median), JUCE OpenGL 0.20 ms on its GL thread plus 0.38 ms of GPU,
Metal 0.09 ms on the UI thread plus 0.45 ms of GPU. The candidates win by 6.6× to 45× depending
on the column (6.6× for GL and 12.5× for Metal on whole-process CPU). Neither clears "no new
platform risk": OpenGL is deprecated on macOS and was unmeasured in the hosts, under Rosetta, on
Linux and on Windows; Metal is macOS-only, so it would be a second renderer. If the budget ever
breaks, Metal is the macOS lever. The 4.10 ms is contended and is no budget figure; the live
readout idle is TBD (idle run, Task 18b).

**Amended** (VW-T17b): **an open finding, the repaint rate.** The CPU canvas repaints on every
VBlank it can make: on the spike's 240 Hz display it painted about 156 frames a second, about 65%
of the message thread at the contended 4.1 ms (about 45% at the headless idle 2.87 ms, an
estimate). §16's budget is per frame and is not affected. Whether to cap the repaint rate is an
engineering call within §16, carried into the final review and the idle measurement; a cap that
changes what a user sees (the motion's smoothness) goes to the director as a forced choice.

### 6.12 `gg-bench --canvas` with four lanes

The benchmark keeps its 3200 × 1200 canvas, its stress workload (already four lanes) and its
three grain counts, and times the shipped path: the banded resolve with four tints in place of
the one-tint resolve. Its JSON gains `"lanes": 4` and `"resolve": "banded"`, so the numbers'
provenance is clear beside M0's and M1's. The `[bench]` canvas test checks it runs `--quick`.
The figure is measured idle on the reference core at the sub-project's end (the exit's
"at or under 3 ms") and again at the close beside X (programme design §6.7).

**As built** (`c729f02`): the measurement moved from `gg-bench`'s `main.cpp` into `Bench.cpp`
(`gg::bench::runCanvas`), so the test reaches it; `gg-bench-lib` links `gg::ui-core`. The
resolve takes four `TintBand`s, the canvas's height in quarters, tinted with `kLayerTints`. The
`[bench]` case is "the canvas bench reports four banded lanes". The figures published before
it are the one-tint resolve's. **The idle figure at 8,192 grains:** TBD (idle run, Task 18b).

### 6.13 `gg-snap`, the screenshots

A tool target built with the editor (`tools/gg-snap`, macOS CI; it needs JUCE's GUI start-up
and no window): it creates each product's editor over a stub `EditorContext` (default values,
`stressGrains` as the grains, a generated sample for the instrument, a few routes), and writes
PNGs of every view in both themes at 960 × 600 and at 640 × 360 into a directory
(`createComponentSnapshot`). The pack (§11) commits one set, regenerated by the fix wave; CI's
mac leg uploads a set as the `gg-snapshots` artifact on every run, so the programme manager and
the director can see the views without a host, and so can the reviewer. The `[plugin]` GUI tests
run the same snapshot code (without writing files) to cover each view's open, draw and close.

**As built** (`0cc3cf7`): `gg-snap <out-dir> [--sizes 960x600,640x360]`, in `gg-all`, so every
leg that builds the editor builds it. Per product it activates the plugin, creates the editor,
loads a generated 2 s sine into the instrument, turns layers B and C on, adds one route (LFO 1
to layer A's Pitch), runs 200 blocks and writes `<product>-<view>-<theme>-<w>x<h>.png`: 32 files
at the default sizes. It sets `GG_STRESS_GRAINS=2048` and points `GG_SETTINGS_FILE` at a scratch
file. `snap_cli` (macOS, with the editor) runs it and checks the 32 names; CI's macOS leg uploads
the output as `gg-snapshots`. The GUI tests draw each view with `createComponentSnapshot`
themselves, not through the tool's code.

## 7. Determinism and real-time safety

- **The audio is bit-identical with the editor open, closed, on any view, and with any theme:**
  the editor changes audio only through the gesture queue and the route path, both of which
  existed or follow the existing `loadedState_` channel (**Amended**, VW-T4a: the route path is
  a channel of its own beside `loadedState_`, §6.6); the `[determinism]` tests' renders
  through the plugin with publishing on and off (sub-project 5's) stay, and a new `[plugin]`
  test renders with routes set from the UI and from a state load and compares the bytes.
- **The publish additions** are fields in a trivially copyable struct, filled from values the
  `Processor` holds; the `[rt]`/`[rtsan]` tests on `process` with publishing on cover them.
- **The canvas's maths** stays in `gg-ui-core`, libm allowed (UI only), tested under the
  sanitizers; nothing in it reaches the audio RNG.
- **The route path** allocates nothing on the audio thread (`ModRoutes` is a fixed array through
  `LatestValue`); `setRoutes` is the existing audio-thread call.
- **The settings file** is read and written on the main thread only (ruling R16), as the
  loader's writes are.

## 8. Decisions for the director

Each a forced choice; the recommended option is first, with one line on what each costs. Nothing
engineering-only is here (that is §9).

**The director's answers, 2026-10-30, relayed by the programme manager:** every recommended
option: D1 option 1 (the pitch axis), D2 option 1 (the rectangle with its four drags and the axis
lock), D3 option 1 (four lanes always), D4 option 1 (Okabe–Ito), D5 option 1 (one layer at a
time), D6 option 1 (the Edit panels under source chips with the route list), D7 option 1 (editor
edits plus whole-state entries), D8 option 1 (dark by default), D9 option 1 (the lanes stay
dark), D10 option 1 (Reverb Size sets on release), D11 option 1 (960 × 600). Each is marked
below and applied in §6.

**D1. The lane's vertical axis** (§6.3). **Decided: option 1.** INTENT §17 freed it from pan for M2's pitch spread.
1. **Pitch:** a grain's height is its pitch, ±48 semitones across the lane; pan is no longer
   drawn. *Cost:* Stereo's effect is heard, not seen.
2. *Pitch by default, pan on a per-lane switch* saved per machine. *Cost:* two mappings to learn,
   and a lane whose axis means something different from its neighbour's.
3. *Keep pan.* *Cost:* Pitch Spread and the Pitch macro stay invisible, against INTENT §17.

**D2. The scan window and its drags** (§6.3). **Decided: option 1.**
1. **A rectangle:** x is Position and Spray as M1, y is Pitch ± Pitch Spread; the left and right
   edges set Spray, the top and bottom edges set Pitch Spread, and the body sets Position
   horizontally or Pitch vertically, axis-locked after 4 px. *Cost:* a vertical body drag moves
   Pitch, not the spread; INTENT §17's "drag vertically for pitch spread" is amended to say the
   edges do.
2. *INTENT literal:* the body's vertical drag sets Pitch Spread; Pitch only by knob. *Cost:* no
   gesture for Pitch itself, and the window has no height to show the spread.
3. *Horizontal only, as M1:* Pitch and both spreads by knob. *Cost:* the lane's new axis shows
   pitch but can't be grabbed.

**D3. The lanes** (§6.3). **Decided: option 1.**
1. **Four lanes always,** equal height, an off layer's lane dimmed with its peaks still drawn;
   the window grows to 960 × 600 (D11). *Cost:* each lane is a quarter of the canvas, about
   80 px at the default size and 45 px at the minimum.
2. *Lanes for the on layers only,* the rest collapsed to a thin strip. *Cost:* the layout jumps
   when a layer switches on or off, under a performer's hand.
3. *One lane at a time,* the selected layer's. *Cost:* the canvas no longer shows the instrument.

**D4. The layer palette** (§6.9). **Decided: option 1.**
1. **Okabe–Ito:** A orange `#E69F00`, B sky blue `#56B4E9`, C bluish green `#009E73`, D
   reddish purple `#CC79A7`, with the letter on every lane, cell and chip. *Cost:* layer A's
   grain tint moves from M1's amber `#FFC46B` to this orange.
2. *M1's amber for A, Okabe–Ito for B to D.* *Cost:* amber and the sky blue are a fine pair, but
   the four were not designed as one set, and amber against the bluish green is the weakest pair
   for a deuteranope.
3. *One hue in four lightnesses.* *Cost:* lightness already means gain on the canvas.

**D5. The Edit view's layer scope** (§6.5). **Decided: option 1.**
1. **One layer at a time,** chosen in the shared strip; the panels fill the width. *Cost:*
   comparing two layers means switching.
2. *Four columns,* every layer at once. *Cost:* 192 controls in 960 px; tiny, and scrolling.
3. *One layer with a "link" switch* that applies an edit to all four. *Cost:* a hidden
   multiplier on every gesture; four entries per undo.

**D6. The Mod view's shape** (§6.6). **Decided: option 1.**
1. **The selected layer's Edit panels under a strip of source chips, with the route list
   beside them:** drop a chip on a control to route; rings on the controls, draggable for depth;
   the list for curve, layers and clearing. *Cost:* the view is dense; the list is narrow.
2. *The route list alone* (48 rows of menus and sliders), with drops only in the Edit view.
   *Cost:* INTENT §17's drag-onto-a-control lives in another view than the one named Mod.
3. *A source × target grid.* *Cost:* 24 by 218 cells; unreadable.

**D7. What undo covers** (§6.8). **Decided: option 1.**
1. **The editor's own edits** (every gesture from any view, the pad, the strip, the lanes, route
   edits), coalesced per gesture, 200 deep, kept while the instance lives, **plus a whole-state
   entry API** for sub-project 5's Mutate and preset recall. *Cost:* a host's own undo and ours
   are separate stacks; an undo overwrites a value the host moved since.
2. *The editor's edits only,* no whole-state entries. *Cost:* Mutate can't be undone from the
   plugin, and the director asked for it with Mutate (INTENT §17).
3. *No plugin undo;* the host's. *Cost:* most hosts don't undo a plugin's internal gestures, and
   §17 promises it.

**D8. The theme's default** (§6.9). **Decided: option 1.**
1. **Dark by default** (M1's look, what every host screenshot so far shows), light by the
   header's switch, saved per machine. *Cost:* a light-OS user picks it by hand once.
2. *Follow the OS's light or dark,* with the same switch to override. *Cost:* the FX and the
   instrument look different from the host around them on a light OS with a dark DAW, until
   overridden; the switch gains a third state, System.
3. *Follow the OS, no switch.* *Cost:* no way to disagree with the OS on purpose.

**D9. The canvas in the light theme** (§6.9). **Decided: option 1.**
1. **The lanes stay dark in both themes:** the canvas is a screen of additive light; the grain
   tints and peaks are the same in both. *Cost:* a dark rectangle in a light window.
2. *Light lanes,* the resolve inverted (grains darken a light ground). *Cost:* a second resolve
   and a second set of tints to keep legible; overlaps saturate to black instead of white, a
   different picture of density.

**D10. Row 217, Reverb Size in the FX view** (§6.7). **Decided: option 1.**
1. **Set on release:** the knob shows its target while dragged and sends one value at the
   gesture's end; the tooltip says so; automation is as it is. The row closes as decided, with
   the DSP route recorded. *Cost:* Size can't be swept from the UI; a host's automation still
   clicks.
2. *Crossfade two reverb states over 100 ms on a Size change.* *Cost:* a DSP change in
   sub-project 4's area, twice the reverb's cost during a fade, and `fx-space-automate`'s hash
   moves.
3. *Accept the click* and sweep the knob live. *Cost:* a click when Size moves, in a view built to
   invite moving it.

**D11. The default window size** (§6.2). **Decided: option 1.**
1. **960 × 600** (the minimum stays 640 × 360). *Cost:* 60 px wider and 100 px taller than M1;
   a host window that remembered 900 × 500 keeps it, since the editor lays out at any size.
2. *Keep 900 × 500.* *Cost:* lanes of 60 px at the default.
3. *1024 × 640.* *Cost:* large on a laptop screen beside a host's mixer.

## 9. My calls

Engineering choices, each with its reason.

- **One canvas with four lanes and one accumulator, tinted per band in the resolve** (§5, §6.3):
  the resolve dominates the frame (M0's 2.0 ms at 3200 × 1200) and is proportional to area, so
  four lanes cost what one did; four images would cost four resolves' overheads and split the
  drags and the drop.
- **A lane's kind is its layer's built kind, not the product's** (§6.3): the engine already
  draws a Live layer's grains from its ring and a Sample layer's from its sample in either
  product; drawing anything else would lie.
- **The layer strip is the lanes' header column** in Perform (§6.3) and a compact row in Edit and
  Mod: the controls sit beside what they control, and the strip costs no vertical space.
- **The pad's corners are A bottom-left, B bottom-right, C top-left, D top-right, Y up:** the
  morph law's `(x, y)` with the mathematical convention; the letters make it unambiguous.
- **A drop's default depth is a quarter of the target's range:** a route with depth 0 is silent
  and the user would think the drop failed; a quarter is audible and safe on every target.
- **A dropped route takes the first empty slot, factory routes included:** sub-project 1 made
  the twelve ordinary routes; privileging them would mean a second rule.
- **The knob is inert in mod mode** (§6.6): a view where the same drag sometimes edits the value
  and sometimes a depth can't be learned; the Edit view is one click away.
- **The route path reuses `loadedState_`** with a routes-only flag rather than a second channel:
  the audio thread already applies routes from it, with `setRoutes`'s ramp rule. **Amended**
  (VW-T4a): a second channel, since one `LatestValue` would let a UI edit overwrite a pending
  load (§6.6).
- **Undo entries are coalesced per gesture and applied through the gesture path:** a knob drag
  is one step, as in every editor, and replaying through gestures keeps hosts' automation
  recording and `HeldValue` consistent with a hand-made edit.
- **The theme is a per-machine setting, read by both products:** a theme belongs to the screen;
  the state is portable (INTENT §8.8) and must not carry one machine's preference.
- **The notices are embedded by a CMake script, not CMakeRC or JUCE binary data:** no new
  dependency, no juceaide, and the texts stay byte-equal to the files by a test.
- **The license list moves to `cmake/licenses.cmake`,** included by `products.cmake` and `ui/`:
  one list, so the About box and the installers can't disagree.
- **`gg-snap` is a tool, not a test flag:** the pack, CI's artifact and the reviewer all want the
  files; the tests share its code and write none.
- **GUI tests stay macOS-only, framework-free tests everywhere:** that is the existing split
  (`#if defined(__APPLE__)` on the gui-strip test), and Linux CI has no display at ctest.
  **Amended** (VW-T3b): there was no such split; the GUI tests run on every leg built with the
  editor (§4).
- **The FX view's Reverb section is PR 1's** (§6.1): it needs nothing from wave 2, row 217 can be
  closed early, and the Delay and Limiter sections are two table rows in PR 2.
- **The OpenGL candidate compiles `juce_opengl` in `libs/thirdparty-impl/`,** not in the shim:
  no submodule edit, no change to the shim's module list, removable with the spike.
- **View switching keeps the canvas alive** (show/hide, not create/destroy): the grains' list
  and the clock would otherwise restart at each switch, and the frame readout would lie.
- **Keyboard shortcuts use the command modifier with digits and Shift with letters:** the keys a
  plugin window sees are those the host passes; Live and Bitwig pass these.
- **No PNG is committed except the pack's one set:** the repository's rule is about generated
  media; a dozen small PNGs are the record the close reads, regenerated by `gg-snap`.

## 10. Verification

**Unit tests** (Catch2; labels in brackets; the framework-free ones under ASan, UBSan and RTSan):
- `[ui]` (`gg-ui-core`): per-lane geometry (a grain lands in its lane's rows and never outside);
  the pitch axis (rate 1 → the lane's centre, rate 2 → +12 semitones' y, rate ≤ 0 and NaN →
  centred, ±48 clamped); the rectangular window (its four edges from Position, Spray, Pitch and
  Pitch Spread, Live and Sample lanes); `hitTest` on both axes with every region and the corner
  rule; the axis lock; the pad's inverse mapping and its weight shading equal to the morph law's
  weights; the banded resolve (each band's tint, the one-band case equal to M1's resolve bit for
  bit); `EditLayout`'s table covers every `ParamSlot` exactly once; the embedded notices equal
  the files.
- `[core][publish]`: the per-lane snapshot fields follow each layer's base and effective values,
  freeze, head, kind and Feedback; the morph fields; trivially copyable; the tripwire on the
  publish path with four layers (`[rt]`, `[rtsan]`).
- `[plugin]`: `uiSetRoutes` reaches the render (the bytes equal a state load's with the same
  routes) and the saved state, under the tripwire; `UndoStack`: entries coalesce per gesture,
  undo and redo replay through the gesture path (the host sees begin, value, end), a whole-state
  entry round-trips, depth 200, closing and reopening the editor keeps the stack.
- `[plugin]` GUI tests (macOS; **Amended**, VW-T3b: every leg built with the editor; in
  `tests/plugin/factory_tests.cpp` and, for the frame, `tests/plugin/editor_frame_tests.cpp`):
  each view opens, draws (the snapshot code) and closes, in both
  products and both themes; every parameter slot has a control in some view (the "reachable"
  walk); the pad's drag sends one gesture pair; the strip's On, Level, Source and Freeze send
  gestures; a double-click on a lane toggles that layer's Freeze; a source chip dropped on a
  control creates the route, with the default depth and the selected layer; a ring drag sets a
  depth; the route list clears a slot; the About box lists exactly the license set; the theme
  switch restyles and writes the scratch settings file, and a second editor follows; Tab
  reaches every control (the traversal's count equals the controls'); the instrument's lane A
  takes a drop and lane B refuses it with the notice; Reverb Size sends its value on release
  alone.
- `[bench]`: `--canvas --quick` runs with the banded resolve and reports `"lanes": 4`.

**Goldens:** no new case. **Every one of the 36 cases keeps its WAV SHA-256** through every
commit (`scripts/update-goldens.sh` before and after each commit that touches `core/` or
`plugin/`; the hash list in that commit). The event logs and metrics don't move.

**Benchmarks:** `gg-bench --canvas` idle on the reference core at the sub-project's end: at or
under 3 ms at 8,192 grains with four lanes, the JSON kept for the close (**the figure:** TBD
(idle run, Task 18b)); `engine-sample` shows no
`regressed` ratio on any CI model (programme design §7.6); the spike's table (§6.11).

**Validators:** clap-validator, pluginval at strictness 10 (with GUI tests where the leg runs
them), auval on the three identities and Steinberg's validator, on `release`, on every CI leg: the
editor opens and closes under them.

**Gate:** `scripts/check.sh` over `dev,asan,rtsan` passes; CI green on the head commit through
the draft PR; `scripts/format.sh --check`; a fresh reviewer's final review and its fix wave.

**Exit criteria** (the programme design's §6.6, each with its proof): every parameter is reachable
from the UI (the `[ui]` table test and the `[plugin]` walk; after PR 2's rebase, wave 2's rows
included); the canvas's frame time stays within §16's budget with four lanes at 8,192 grains
(`gg-bench --canvas` idle, in the report; Live and Bitwig at the close, §11's checklist); the GUI
tests cover each view's open and close (the `[plugin]` GUI tests); the spike doc exists with its
decision (`docs/spikes/m2-gpu-canvas.md`, INTENT §23).

## 11. The listening pack: `docs/listening/m2-views/`

The views make no sound of their own, so this pack is mostly to look at and to do, with three
renders for the gestures that change the sound. Rendered and captured for the record now, heard
and seen at M2's close (sub-project 7):

- **`README.md`:** how to make the screenshots (`gg-snap`) and the renders (`gg-render`, as the
  other packs), the screenshot index, the three renders' table, and the host checklist.
- **`snapshots/`:** one committed set from `gg-snap` at the fix wave's end: each product × each
  view × both themes at 960 × 600, and Perform at 640 × 360 (about 20 PNGs); what the director
  looks at before opening a host. **As built** (`0cc3cf7`): 16 PNGs, each product × each view ×
  both themes at 960 × 600, from a Debug build; the 640 × 360 set is in CI's `gg-snapshots`
  artifact and in `snap_cli`'s output, not committed.
- **`specs/`** (gg-render, 48 kHz stereo, each 6 to 10 s):
  - `01-window-drag.json`: the FX over a sweep; layer A's Position, Spray, Pitch and Pitch
    Spread stepped along the path a rectangle drag would take (right, wider, up, taller, back),
    so what the rectangle shows is heard;
  - `02-lane-freeze.json`: the FX over the sweep, layers A and B on; B frozen alone from 2 s to
    4 s, then A alone from 5 s to 7 s: per-lane freeze, what the lane's toggle does;
  - `03-pad-path.json`: the instrument's four Sample layers of `inst-layers-morph`, the pad
    dragged from the centre to each corner and back, as the pad's handle moves, 1 s per leg.
- **The host checklist** (Ableton Live 12 and Bitwig Studio; CLAP in Bitwig, VST3 and AU in Live;
  each item in both products):
  - the four views open, switch and close; the window resizes from 640 × 360 up; HiDPI;
  - Perform: four lanes draw the four layers' grains in their colours, a Live lane scrolls and a
    Sample lane stands; the rectangle's four drags record automation; double-click freezes one
    lane; the pad's drag records Morph X and Y; the strip's controls; a file dropped on lane A;
  - the frame readout at the densest setting reachable (four layers at Density 400, Size 2 s): at
    or under 3 ms;
  - Edit: every panel for every layer; a spread pair; a stepped control's menu; double-click
    resets;
  - Mod: a chip dropped on a control is heard; a ring drag changes the depth; a factory route
    cleared and the macro goes quiet; the route survives save and reload;
  - FX: Reverb Size sets on release without a click; Decay's "∞";
  - undo and redo from the buttons and the keys, across an editor close and open; the host's
    automation lane shows the undo as a change;
  - the theme switch, and a second instance following it; the light theme's legibility;
  - Tab through a view; arrows on a knob, on the pad and on the canvas;
  - the About box: the version, the notices scroll, the license texts;
  - the memory readout, the FX's loader strip and lanes B to D's drops, record, capture and
    splice marks (PR 2's, added to the list when PR 2 lands).

## 12. The carry-over

**11 open rows at this sub-project's start** (`docs/m2-carry-over.md`: 51, 85, 87, 88, 144, 148,
184, 217, 218, 221, 232).

**Closed here:** **217** (Reverb Size clicks when moved: decided in the FX view by D10, §6.7) and
**221** (the About box shows the notices and licenses, §6.10), both in PR 1, where the programme
design's §8 placed them.

**Touched files with open rows:** `plugin/src/editor/EditorPlugin.h` (85: the `<string>` include
before the shim's header, waiting on an sst-clap-helpers bump) and `plugin/src/editor/
EditorPlugin.cpp` (232: the shim keeps an editor a host never showed, upstream). Both wait on
upstream, not on a touch: they stay open, noted, and this sub-project files nothing upstream
(the policy). No other open row names a file this sub-project edits.

**Expected at the end: 9 open rows,** no new rows. Every minor the final review raises is fixed in
the fix wave unless blocked outside our control or genuinely large (the policy). The count is
restated in PR 2's report.

**As built (PR 1):** row **217** is **Closed** by `0d8969c` (the FX view's Reverb Size sets on
release, §6.7) and row **221** by `2116004` (the About box, §6.10); `docs/m2-carry-over.md` marks
both. `THIRD_PARTY_NOTICES.md`'s sentence that the About box doesn't show the texts yet now says
it does. **Amended:** the director retired the carry-over's count rules on 2026-10-30, so the
count above is no longer a target; a minor deferred from a review is fixed or deferred as the
review cycle decides, and the sub-project's final report lists the deferred ones.

## 13. INTENT changes (Part II, with one §23 row in the same change)

- **§16:** the UI budget's figure with four lanes from `gg-bench --canvas` (the banded resolve),
  dated, beside M0's and M1's.
- **§17:** the Perform view as built in M2: four lanes by the layer's kind, the pitch axis, the
  rectangular window and its four drags (D2), per-lane freeze, the strip as the header column,
  the pad with its corners, the file drop; the Edit, Mod and FX views as built, with the table
  rule; undo/redo (D7); the themes and the per-machine setting (D8, D9); the palette (D4); the
  keyboard map; the About box; the GPU spike's result and the renderer paragraph amended to it;
  "drag vertically for pitch spread" amended per D2; the window's size (D11).
- **§18:** `settings.xml`'s `theme` attribute (read as dark when missing; the version stays 1); the
  route path noted beside the state's routes ("Routes have no other way to change yet" no
  longer holds).
- **§19:** the GUI tests' scope, `gg-snap` and CI's `gg-snapshots` artifact; the "every parameter
  reachable" tests.
- **§22:** the GPU-canvas question resolved, with the spike's decision.
- **§23:** one row (the next free number at merge): the director's D1–D11 with their dates, the
  rulings made during the build, and the §9 calls that amend Part II (the lane by kind, the
  theme as a setting, the route path, Reverb Size on release). PR 2 appends to the same row.

Part I is unchanged. The programme design's §6.6 is unchanged (the FX view's split across the
PRs is a plan matter, flagged in §6.1).

**As built (PR 1):** made in one change with the §23 row, which takes its number at merge.
Besides the sections above, two stale sentences in §14 change by the same row: the mod
matrix's slots are now edited from the Mod view too, and the canvas's y axis is pitch. The idle
canvas figure §16 gets is TBD (idle run, Task 18b).

## 14. Risks

| Risk | Mitigation |
|---|---|
| The sub-project exceeds M1's Perform UI by far (it will) | Two PRs; the plan's waves run independent tasks in parallel worktrees (the canvas, Edit, Mod, undo, About, themes, the spike are disjoint) |
| Four lanes of peaks cost more than one (four column loops per frame) | Measured in `gg-bench --canvas`'s successor numbers and the host readout; the peak strips are cached per column until the head moves a column |
| The Mod view's drag-and-drop fights sst-jucegui's own mouse handling | A drop target wrapper around each control, not a subclass; the ring layer is a sibling overlay |
| The route path races a state load (both write `routes_` and `loadedState_`) | Both are main thread; the later write wins and the audio thread takes the latest, as the channel is defined. **Amended** (VW-T4a): the routes have their own channel, which a load also feeds, so the later write wins in either order (§6.6) |
| Undo replays through gestures a host records: a long undo makes long automation | Each replay is one begin, one value, one end, as a click would be |
| `juce_opengl` compiled outside the shim mismatches its definitions | The impl target takes `gg::juce-headers`' definitions; a configure check, and the spike is removable |
| The light theme is unreadable somewhere (contrast) | `gg-snap`'s light set is reviewed by the fix wave; the palette's four are mid-luminance |
| The About box's Markdown rendering drops a notice line | The test compares the embedded text, not the rendering; the box also offers "plain text" |
| Wave 2 appends parameters the table can't place in a panel that exists | The table's "Filter" and "Source" rows are placeholders; an unplaced slot fails a test, naming it, at PR 2's rebase |
| Hosts eat the keyboard shortcuts | Every shortcut has a button or a focusable control |

## 15. Left to the builder

- The exact cell sizes, fonts and the knob/slider choice per parameter within the table's
  rules; how a panel wraps at 640 px.
- How `MacroRow`'s data-source adaptor generalizes into `ParamControl` without changing the
  macros' behaviour (their tests pin it).
- The ring overlay's geometry (arc widths per route, how many fit, the overflow count).
- The banded resolve's signature and whether a lane's rows are passed as a span or an array of
  four.
- The axis lock's state machine in `GrainCanvas` and the corner rule's tolerance in `hitTest`.
- The settings poll's cadence for the theme (the editor's 30 Hz timer, every 30th tick, is my
  guess).
- The generated `Notices.cpp`'s layout and the CMake script's escaping of the texts.
- The snapshot tool's stub context (which values, which routes, which stress seed), so the
  pictures are informative and stable across runs.
- The spike's shader and the framebuffer format; the GPU timer queries per API.

## 16. Rulings made during the build (PR 1)

Each is an engineering ruling Claude made in the build, during review or in a task's report, and
each changed what this document said: the text is marked **Amended** where it applies. IDs are
`VW-T<task><letter>`. Rulings about the process (models, waves, merges) are in the build's
ledger, not here. The director's decisions are D1–D11 (§8).

| ID | Section | The ruling |
|---|---|---|
| VW-T2a | §6.3 | A corner of the scan rectangle takes the Spray edge (left or right); on a Live lane the right corners take the top and bottom edges, since the right side is the body. |
| VW-T2b | §6.3 | The body keeps a height to grab: half-height `max(tol, H/2 − tol)`, the top and bottom edges only beyond it, so a spread-0 rectangle has a body. |
| VW-T2c | §6.3 | A reverse grain (`r < 0`) draws centered, as `r = 0` does. |
| VW-T3a | §6.9 | The source families: macros yellow, modulators vermilion, MIDI blue, the follower gray, the transport the text color; the light palette darkens the macros' yellow to an ochre. |
| VW-T3b | §4, §9, §10 | The GUI tests run on every leg built with the editor, as M1's do; only `snap_cli` is macOS-only. |
| VW-T3c | §6.5 | Level's one placement is the Space panel; the strip shows On, Level, Source and Freeze as second controls with the same IDs. |
| VW-T4a | §6.6, §7, §9, §14 | The routes take their own channel (`uiRoutesPending_`); a load also feeds it and `deactivate` drops an untaken edit, so the later write wins in either order. |
| VW-T5a | §6.8 | `UndoStack` lives in `gg-ui-core`; `EditorPlugin` owns the instance, declared so it outlives every editor. |
| VW-T6a | §4, §6.9 | The settings file's NUL guard covers `theme`: one addition to sub-project 2's `media/src/SettingsFile.cpp`. |
| VW-T9a | §6.3 | `LiveGrains` holds 8,192 grains per layer, not across the four. |
| VW-T9b | §6.3 | Lanes are half-open row ranges; a shared row goes to the lower lane. |
| VW-T9c | §6.3 | A press in a lane selects its layer; a body press that never moves 4 px is no gesture. |
| VW-T10a | §6.3 | The pad's drag is one undo entry, "Morph"; a real double-click is two entries (the first press, then the centering). |
| VW-T11a | §6.5 | Rings on knobs only; the Step sliders and modulator controls are no target; the spread band takes Position and Spray as 0–1 values. |
| VW-T11b | §6.6 | Mod mode's inertness covers the accessibility value interface. |
| VW-T12a | §6.5, §6.6 | Toggles and menus show one dot per route in its family's color; their depth is edited in the route list. |
| VW-T12b | §6.6 | A route to all layers counts as that source's route for every layer, so a drop selects it instead of doubling it. |
| VW-T14a | §6.10 | The About box's format comes from clap-wrapper's host-information extension at run time. |
| VW-T15a | §6.2 | Notices reach `header.notice` through the component tree (`NoticeSink`); the plugin no longer sets `context.notice`. |
| VW-T15b | §6.2 | The view is remembered per plugin instance, unsaved. |
| VW-T15c | §6.10 | The About box is modal: the keys and Tab are its own while it shows. |
| VW-T15d | §6.8 | No undo mid-gesture: undo and redo wait while a gesture or group is open. |
| VW-T15e | §6.8 | A parameter entry's `before` is the control's shown value at the gesture's begin. |
| VW-T15f | §6.8 | While undo or redo applies, the stack ignores what its callbacks ask of it. |
| VW-T17a | §6.11 | The GPU spike's decision: the CPU renderer stays, on the ratios and the risk clause. |
| VW-T17b | §6.11 | Open: the canvas repaints at the display's rate; a cap is the final review's question, the director's if it changes what a user sees. |
