# Listening pack: M2 sub-project 6 (the views)

The views make no sound of their own, so this pack is mostly to look at and to do. It is made for
the record now and used at M2's close (sub-project 7). It has three parts:

- **screenshots** of the four views, from `gg-snap`: one set committed in [`snapshots/`](snapshots/),
  and CI's `gg-snapshots` artifact, made on every run of the macOS leg;
- **three renders** from `gg-render`, for the moves a user makes in the views that change the sound
  (the views add no DSP, so each move is rendered as the automation it records);
- a **host checklist** for Ableton Live 12 and Bitwig Studio.

The spec is `docs/superpowers/specs/2026-10-30-m2-views-design.md`: the frame and Perform view are
its §6.2 and §6.3, the Edit, Mod and FX views §6.4 to §6.7, the screenshots §6.13 and this pack §11.

## The screenshots

`gg-snap` draws each product's four views in both themes, headless, with no host and no window:

```sh
cmake --preset release && cmake --build --preset release --target gg-snap
build/release/tools/gg-snap/gg-snap build/release/snapshots [--sizes 960x600,640x360]
```

For each product (`fx`, `inst`), view (`perform`, `edit`, `mod`, `fx`), theme (`dark`, `light`) and
size it writes `<product>-<view>-<theme>-<w>x<h>.png`: 32 files with the default two sizes. What
the pictures show: the instrument has a generated 2 s sine loaded; layers B and C are on in both
products; one route (LFO 1 to layer A's Pitch) is added to the factory routes; 200 blocks have
run, so the canvas has grains, and it draws the synthetic 2,048-grain workload (`GG_STRESS_GRAINS`)
rather than the product's own grains. The per-machine settings are a scratch file, so it never
touches yours. It exits 0, 1 with a message, or 2 for bad usage.

[`snapshots/`](snapshots/) holds the **960 x 600** set (16 files) from a Debug build on macOS; the
frame readout in the header shows that build's time, not the product's. CI's `gg-snapshots` has
both sizes, from the universal Release build.

| Product | Perform | Edit | Mod | FX |
|---|---|---|---|---|
| Giga Grains FX, dark | [fx-perform-dark](snapshots/fx-perform-dark-960x600.png) | [fx-edit-dark](snapshots/fx-edit-dark-960x600.png) | [fx-mod-dark](snapshots/fx-mod-dark-960x600.png) | [fx-fx-dark](snapshots/fx-fx-dark-960x600.png) |
| Giga Grains FX, light | [fx-perform-light](snapshots/fx-perform-light-960x600.png) | [fx-edit-light](snapshots/fx-edit-light-960x600.png) | [fx-mod-light](snapshots/fx-mod-light-960x600.png) | [fx-fx-light](snapshots/fx-fx-light-960x600.png) |
| Giga Grains, dark | [inst-perform-dark](snapshots/inst-perform-dark-960x600.png) | [inst-edit-dark](snapshots/inst-edit-dark-960x600.png) | [inst-mod-dark](snapshots/inst-mod-dark-960x600.png) | [inst-fx-dark](snapshots/inst-fx-dark-960x600.png) |
| Giga Grains, light | [inst-perform-light](snapshots/inst-perform-light-960x600.png) | [inst-edit-light](snapshots/inst-edit-light-960x600.png) | [inst-mod-light](snapshots/inst-mod-light-960x600.png) | [inst-fx-light](snapshots/inst-fx-light-960x600.png) |

Look for: the four lanes in the layer colors (Okabe-Ito: A orange, B sky blue, C green, D pink),
the pad's corners (A, B, C, D) and handle, the macro row, undo and redo and the theme switch in the
header; and in the light theme, which parts stay dark by design (the lanes, D9) and whether
everything else reads. The instrument's lanes B
to D say "Load a sample, or drop one here" (they take a sample only after the session format's next
version).

## Rendering the files

The renders aren't committed. The specs are in [`specs/`](specs/). From the repository root:

```sh
cmake --preset release && cmake --build --preset release --target gg-render
r=build/release/tools/gg-render/gg-render
L=build/release/listening/m2-views
$r --write-media "$L/media"
for s in docs/listening/m2-views/specs/*.json; do
  n=$(basename "$s" .json)
  $r "$s" --media-dir "$L/media" -o "$L/$n.wav" --spectrogram "$L/$n.png" \
     --metrics "$L/$n.metrics.json" --events "$L/$n.events.jsonl"
done
```

Each file is a 48 kHz, stereo, 32-bit float WAV, with a spectrogram PNG, the audio metrics and the
grain-event log beside it. Every render is deterministic. The figures below were measured on the
`dev` build's renders over both channels (RMS as the power mean of the metrics' 10 ms envelope,
over the span given).

## The renders

Layer A runs at **Level -6 dB** in every file, and file 02's layer B does too; every other layer
stays at its default, 0 dB. The FX files at Mix 100% over a stereo sine sweep at amplitude
0.5, 110 Hz to 1760 Hz, as long as the file. The FX's Position is seconds behind the input, set by
`16/255 * (2^(8p) - 1)`: 0.3 is 0.27 s, 0.45 is 0.70 s. Spray is `2/255 * (2^(8s) - 1)`: 0.3 is
0.03 s, 0.6 is 0.21 s.

| File | Length | What it is | Listen for |
|---|---|---|---|
| `01-window-drag.wav` | 9.0 s | The FX, layer A at Density 32 Hz, Size 100 ms. The rectangle drag's path on the lane, in 4 steps each 0.125 s apart: right (Position 0.3 to 0.45, 1.0 s), wider (Spray 0.3 to 0.6, 2.0 s), up (Pitch 0 to +7, 3.0 s), taller (Pitch Spread 0 to 7, 4.0 s), then all four back together (5.5 s). Peak -8.1 dBFS. RMS -20.7 dBFS at 0.3-1.0 s, -21.6 at 2.5-3.0 s (Spray wide), -19.6 at 4.5-5.4 s (spread 7), -19.0 at 8-9 s. | What the rectangle shows is what you hear: the cloud reaching further back (a lag on the sweep), then a wider scatter in time, a fifth up, then a fifth up scattered over +-7 semitones, then everything returns in one move. No click at any step. The spectrogram shows the sweep's line rising with a small step at 1.0-1.4 s, a short rise at 3.0 s, and a scatter of short blips around the line from 4.0 to 5.5 s, which closes at 5.5 s. |
| `02-lane-freeze.wav` | 8.0 s | The FX, layers A and B on (B at +7, Size 60 ms, Density 32 Hz, both at Position 0.45). B frozen alone from 2 to 4 s, then A alone from 5 to 7 s: the lane toggles. Peak -10.4 dBFS. RMS -22.9 dBFS at 0.5-2 s, -22.5 at 2.5-4 s (B frozen), -22.8 at 5.5-7 s (A frozen). | A frozen lane stops following the input and holds its moment, a steady texture on one pitch region, while the other lane goes on following the sweep. Freezing B holds the fifth-up layer; freezing A holds the main one. The spectrogram shows layer A's rising line throughout, and a step in the lines where a freeze starts and where it lets go. Freezing and thawing make no click. |
| `03-pad-path.wav` | 11.0 s | The instrument, layers A to D on over four Sample sources (A a 100 Hz-4 kHz sweep, B a 330 Hz sine, C noise, D a 50-2000 Hz sweep, each at Position 0.25), one note held 0-10.5 s. The pad's handle goes from the centre to each corner and back, 1 s a leg, in 8 steps of 0.125 s: A (X 0, Y 0, 1.0-2.0 s), B (X 1, Y 0, 3.0-4.0 s), D (X 1, Y 1, 5.0-6.0 s), C (X 0, Y 1, 7.0-8.0 s), the centre at 9 s. Peak -5.3 dBFS. RMS -21.6 dBFS at the centre (0.5-1.0 s), -16.1 at corner B (3.5-4.0 s), -16.0 at corner D (5.5-6.0 s), -17.3 at corner C (7.5-8.0 s), -20.9 over the last second. | Each corner lets one layer through and the rest fall away: the sweep (A), the sine (B), the lower sweep (D), then noise (C). Moving to a corner is a crossfade, never a step; D1's pad law means the centre is quieter than a corner (about 5 dB here). The spectrogram's broadband bars are layer C's noise (7.0-8.0 s, with a fade in and out around it); the thin lines under it are the sine and the sweeps. |

## Host checklist

Do each item in **Ableton Live 12** and in **Bitwig Studio**, in both products. Use CLAP in Bitwig,
and VST3 and AU in Live. Note the host and format of anything that fails.

- [ ] The four views open, switch and close; the window resizes from 640 x 360 up; HiDPI.
- [ ] **Perform:** four lanes draw the four layers' grains in their colors, a Live lane scrolls and
  a Sample lane stands; the rectangle's four drags record automation; double-click freezes one lane;
  the pad's drag records Morph X and Y; the strip's controls; a file dropped on lane A.
- [ ] **The frame readout** at the densest setting reachable (four layers at Density 400, Size 2 s)
  is at or under 3 ms.
- [ ] **Edit:** every panel for every layer; a spread pair; a stepped control's menu; double-click
  resets.
- [ ] **Mod:** a chip dropped on a control is heard; a ring drag changes the depth; a factory route
  cleared and the macro goes quiet; the route survives save and reload.
- [ ] **FX:** Reverb Size sets on release without a click; Decay's "inf".
- [ ] **Undo and redo** from the buttons and the keys, across an editor close and open; the host's
  automation lane shows the undo as a change.
- [ ] **The theme switch,** and a second instance following it; the light theme's legibility.
- [ ] **The keyboard:** Tab through a view; arrows on a knob, on the pad and on the canvas.
- [ ] **The About box:** the version, the notices scroll, the license texts.
- [ ] The memory readout, the FX's loader strip and lanes B to D's drops, record, capture and splice
  marks (PR 2's, added to the list when PR 2 lands).
