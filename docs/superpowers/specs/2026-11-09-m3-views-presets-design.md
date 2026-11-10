# M3 sub-project 5: views and presets

| | |
|---|---|
| **Status** | Design from the director's brainstorming of 2026-11-09 (`.superpowers/sdd/m3-call-review.md`, sub-project 5). The director's decisions are marked **(the director)**; everything else is Claude's call, marked as such, for the builder to keep or argue against |
| **Program** | `docs/superpowers/specs/2026-11-07-m3-program-design.md` §8 row 5, widened by the director to take every handoff the other M3 sub-projects left it |
| **Builds** | After sub-projects 2, 3, 3b, 4, 7, 8, 9, 10 and 11 are on `main` **(the director)**, on a branch `m3-sp5-views` from `main` then |

## 1. Intent

M3 closes with every new feature reachable and playable from the editor, not only from a host's
generic panel. A player at a LinnStrument or a keyboard can see which notes are sounding, recall
scenes while playing, see and change the tuning, and start from presets that show each new
feature. M3 adds about 30 controls per layer and 20 globals; today's Edit view is one scrolling page
of panels, and it can't take them gracefully.

## 2. Goals, non-goals, anti-goals

**Goals:**
- Every M3 parameter placed where a player would look for it, reachable at 960 × 600 and 640 × 360.
- The Perform view shows notes apart within a layer and puts scenes under the player's hand.
- The tuning in effect is always visible.
- Controls that do nothing in the current state say so.
- 18 new factory presets that each give one or more new features a playable start.

**Non-goals:** per-note strips in Perform **(the director, declined)**; hiding inactive controls
**(the director, declined)**; a new top-level view; a scale editor; the GPU canvas; the TEMP-style
"release returns any control" gesture that sub-project 10 offered (Claude's call: a carry-over row,
since it is a new interaction model for every control).

**Anti-goals** (failure even if it works): a label elided or colliding at 640 × 360; a control
that moves when the state changes; an existing factory preset's position, sound or golden changed;
any audio-thread or engine change; the layer colors stop reading as A, B, C and D.

## 3. Constraints

- `AGENTS.md`'s GUI tests hold and grow with this work: every view in both products and both
  themes at 960 × 600 and 640 × 360, every control inside its view, every parameter reachable, the
  keyboard reaches every control, no elision or collision, the light theme at 4.5:1.
- Factory presets are appended after the 24 per product. Preset order freezes only at 1.0, but
  existing ones never move. Each new preset gets a golden in `tests/golden/presets/`, its click
  score under 15.
- Remote-controls pages are appended; existing pages keep their order and slots.
- `gg-snap` and `docs/listening/m2-views/snapshots/` show the new views, regenerated and looked at.
- The editor never touches the audio thread except through the existing paths (UI queue, snapshot,
  `uiCommand`).

## 4. Approaches (the director picked A)

- **A. Build after every sub-project has landed.** One pass over the real controls; no merge fights
  in `EditLayout`'s table; presets can use every feature. It waits on the slowest branch.
- B. Build the frame now (tabs, scene strip, header button), place controls later. The new shape
  sooner, but every branch landing meanwhile has to be re-placed into tabs.
- C. Start once 2, 3, 3b, 4, 8 and 9 land; 7, 10 and 11 land on top of the tabs. Earlier start;
  those three re-place their controls, and presets still wait for them.

## 5. The design

### 5.1 The Edit view gets tabs (the director)

The Edit view keeps its layer strips (A to D) on top and gains a row of tabs; each tab is a page of
panels for the selected layer that fits the window with little or no scrolling. The director chose
these four, as sketched:

```
A B C D strips
[ Sound ][ Voice ][ Color ][ Modulators ]
+--------------------------------------+
| Voice page:                          |
| Mode  Clock  Note Ch  Key Lo/Hi      |
| Scan Speed/Edge/Track   Pan          |
| Voice Filter  Cutoff  Res  KeyTrack  |
+--------------------------------------+
```

Claude's call for the membership, which the builder adjusts to fit:
- **Sound:** Source, Ring (with Loop Sync), Scheduler, Grain (with Reverse and Skew), Pitch (with
  Pitch Quantize), Space, the bus Filter panel.
- **Voice:** Mode, Clock, note routing (Note Channel, Key Low, Key High), Pan, Scan (Speed, Edge,
  Track, Sync), the voice filter (Voice Filter, Cutoff, Resonance, Key Track).
- **Color:** Drive, Drive Shape, Crush, Downsample. The global resonator stays in the FX view.
- **Modulators:** LFO 1 and 2, Env 1 and 2 (with their curves), Random 1 and 2, Step, each with its
  Sync switch (sub-project 7).

The Mod view shares the Edit view's panels today (`EditLayout.h`); it follows the same tabs. The
selected tab is kept per editor instance, as the view is. The keyboard reaches the tabs and every
control on each page.

### 5.2 The Perform view

- **The scene strip (the director):** one thin row between the lanes and the macros: pads 1 to 8,
  Scene B, Scene Morph and Scene Punch.

  ```
  | lanes A                            |
  | lanes B  C  D                      |
  +------------------------------------+
  |[1][2][3][4][5][6][7][8] B:[3] Morph-o- [Punch]|
  +------------------------------------+
  | pad  Size Density Pitch Spray ...  |
  ```

  A pad shows empty, filled and current. Pressing recalls; Claude's call for store and clear:
  Shift-click stores into that pad, a context menu offers Store, Clear and Rename. The recall from the
  editor pushes one whole-state undo entry (sub-project 10's N12). Scene names (sub-project 10 saves
  them) show as a pad's tooltip, and on the pad itself where it fits. Sub-project 10's FX-view
  Scenes panel stays, for Quantize, Glide and Store.
- **Voice shades (the director):** on Voice layers each voice's grains draw in its own shade of the
  layer's color: lighter and darker steps of the same hue, keyed by the voice's slot (0 to 15), so
  the lane still reads as its layer. Cloud layers draw as today. This needs the voice on the UI
  grain record: `GrainVis` (`core/include/gg/core/GrainQueue.h`) carries `layer` today and gains a
  voice field (Claude's call: a slot number, with a value for "Cloud"). The canvas bench's
  8,192-grain figure stays within INTENT §16's 3 ms.

### 5.3 The header's tuning button (the director)

Beside the preset name, a button shows what is in effect ("12-TET", "19-EDO", "MTS: <name>", or
"MTS connected (ignored)") and opens a menu: the loaded scale and mapping, recent files, Load scale
(.scl), Load keyboard mapping (.kbm), 12-TET, and MTS-ESP's status. It replaces sub-project 4's
Tuning… menu in the Midi panel (T12), which keeps the MTS-ESP switch. Save As gains a "save the
tuning with the preset" choice, on by default when a scale is loaded (sub-project 4's T2 note).

```
Giga Grains  < Init >  [19-EDO v]  Perform Edit Mod FX
```

### 5.4 Inactive controls gray out (the director)

A control whose parameter does nothing in the current state draws dimmed and stays editable, in
place. At least:
- on a Cloud layer: Clock, note routing's effect on voices, Scan Track, the voice filter's four,
  and every per-voice-only control;
- Voice Filter Off: Cutoff, Resonance, Key Track;
- Clock Free: nothing; Clock Pitch: Size shows periods (sub-project 3's `sizePeriods`);
- a stage at 0: Drive Shape with Drive at 0; the resonator's controls with Mix at 0;
- a synced rate shows its division while its Sync is on (sub-project 7's display).

The builder completes the list from each sub-project's spec with the same rule, and a GUI test
checks each condition grays its controls and keeps them reachable.

### 5.5 Remote-controls pages

Pages for the new parameters are appended, grouped as the Edit tabs are (Claude's call: a Voice
page per pair of layers, a Color page per pair of layers, a Scenes page and a Resonator page where
sub-projects 10 and 11 haven't already added them). Every page has at most eight slots and existing
pages don't move.

### 5.6 The new factory presets (the director: 12 instrument, 6 FX)

Appended as instrument 25 to 36 and FX 25 to 30, in `content/presets/`, each with a golden. Claude's
call for what they cover, so each new feature has a start:

| Feature | Instrument | FX |
|---|---|---|
| Voice and MPE expression (sub-projects 1, 2) | 3 | 1 |
| Pitch-sync (3) | 2 | 1 |
| Playhead scan (3b) | 1 | 1 |
| A tuning carried by the preset (4) | 1 | 0 |
| Tempo sync (7) | 1 | 1 |
| Reverse and Skew (8) | 1 | 0 |
| The voice filter (9) | 1 | 0 |
| A scene bank (10) | 1 | 1 |
| Drive, crush, resonator (11) | 1 | 1 |

**A content v3 release (the director):** the new presets get new CC0 source material, chosen for
sustained, expressive playing (bowed, blown, sung or struck sources that reward pressure and slides).
The release follows v2's path (`docs/superpowers/specs/` of M2 sub-project 5, §6.5): CC0 only, every
file's origin and license in `PROVENANCE.md`, a new release of `sethbc/giga-grains-content`, the
pin in `content/pin.txt` bumped (SHA-256), the installers' Content component and its tests
following. v3 is a superset of v2, so every existing preset and golden keeps its files. Claude's
call: the candidate files and their sources go to the director as a list before the release is
made, and this work starts early in the sub-project, since the presets wait on it.

A preset may show several features; the counts say which feature leads. The voice presets are the
playable defaults the program design asked for. Each tuned preset carries its `.scl` and `.kbm`;
each scene preset carries a full bank.

### 5.7 The builder check's answers

A reviewer read this as a builder would (2026-11-09). Answered, or settled as Claude's calls:
- **Voice shades against the 3 ms canvas budget:** the budget holds (INTENT §16). Spike the banded
  resolve with shades first; if 16 shades don't fit, use fewer steps (voices share a shade by slot
  modulo the step count) before anything else. Moving the budget needs the director.
- **Graying and the light theme's 4.5:1:** labels keep 4.5:1 in every state; the dimming applies to
  the control's arc, knob face and value text, so the existing contrast test stays as it is and
  gains a dimmed-state case.
- **The Mod view** shows the same tab bar as Edit, with its route list beside the page as today;
  `EditLayout` stays one table with a tab per panel, read by both views. The selected tab is one
  value per editor instance, shared by Edit and Mod.
- **The editor's paths already exist:** sub-project 4's T12 loads a scale off the audio thread and
  reports what is in effect, and sub-project 10's N15 gives Store, Clear and Recall as `uiCommand`s
  and a scene status in `UiSnapshot`. Build on them. If one is missing when this starts, stop and
  report: no audio-side plumbing is added here.
- **Recent scale files** live in the per-machine settings file (`GG_SETTINGS_FILE` in tests).
  Save As's tuning choice applies to preset files only.
- **Both products** show the Voice tab with every parameter that product has (the FX has Voice
  layers, note routing and MPE since sub-project 1); a parameter a product lacks isn't drawn, as
  today.
- **Preset tags:** the vocabulary gains the tags the new presets need (for example `voice`, `mpe`,
  `tuned`, `scenes`, `sync`), appended; existing tags keep their meaning.

## 6. Exit

- The GUI tests pass in both products, both themes and both sizes, with the new tabs, the strip,
  the button and the graying conditions.
- `gg-snap` produces every view with the tabs (its file list grows by tab), and the committed
  snapshots are regenerated and looked at.
- 18 new preset goldens, each catching or accounted for as the preset goldens are.
- The canvas bench at 8,192 grains with voice shades is within 3 ms on the reference core.
- A listening pack for the presets (`docs/listening/m3-views-presets/`), in the per-sub-project form.
- `scripts/check.sh`, the validators and CI green.

## 7. Left to the builder

The exact tab membership where a page doesn't fit; the shade steps (lightness values) that keep
4.5:1 against the lane; the pad's look; the remote pages' grouping; the presets' sounds within the
coverage table; whether the tab row needs short labels at 640 × 360. Each recorded as a ruling.
