# The M2 listening gate

One sitting, one numbered checklist. It holds every open question from the six M2 listening
packs, the carry-over's gate rows, the views' 16 snapshots and the host checks for Ableton Live 12
and Bitwig Studio. The packs keep their tables and figures; this file only asks.

**How to answer.** Each item ends with its answer form: yes or no, or a pick. Reply with the
numbers and words, for example "6 revisit, 12 further, 22 edit: ...". **An item you leave unanswered
stands as accept**, and an accepted item closes its carry-over row. A verdict that isn't accept
becomes the fix wave's first tasks.

**About two hours.** The listening is about 70 minutes, the carry-over items 15 and the host
matrix about 45 (eight cells, five or six minutes each).

## Before you start

The renders aren't committed; the links below point at `build/release/listening/m2-<pack>/` under
the repository root, so render once per tree with each pack's own commands (the "Rendering the
files" section of [layers](../m2-layers/README.md), [engine](../m2-engine/README.md),
[sources](../m2-sources/README.md), [extremes](../m2-extremes/README.md),
[presets](../m2-presets/README.md) and [views](../m2-views/README.md)). All six were re-rendered on
`release` at `df1c3ae` for this gate, and each pack says so; sources' `03` is 130 s and takes the
longest. Each WAV has its spectrogram PNG beside it. The presets pack needs the content (the first
`gg-presets` build fetches it) and `ffmpeg` for the joined ladder.

For the host cells, install the `release` products (`cmake --build --preset release --target
gg-installer`, or the `gg-products` folder), both products, all formats.

## Layers

Pack: [m2-layers](../m2-layers/README.md). The files are 4 to 13 s.

**1. Does the layers' sum sound too quiet?** Play [01-four-layers](../../../build/release/listening/m2-layers/01-four-layers.wav),
[05-layer-on-off](../../../build/release/listening/m2-layers/05-layer-on-off.wav) and [14-macro-size-all-layers](../../../build/release/listening/m2-layers/14-macro-size-all-layers.wav). With the pad at its centre
the layers on share 1 between them (D1): four uncorrelated layers measure about 7 dB below layer A
alone in `01`, A dips about 4 dB when B joins in `05`, and `14` sits about 6 dB under. Nothing is
louder than M1 was. Answer: `keep` (amplitude sharing) or `power` (the pad should keep power, a
change for M3).

**2. Is Env's release too quiet to hear?** Play [08-envelope-notes](../../../build/release/listening/m2-layers/08-envelope-notes.wav). The instrument
spawns no grain after a note-off, so Env 1's release is heard only in the last grains and the
reverb's tail. Answer: `accept` or `change` (a grain stream through the release, after M2).

## Engine

Pack: [m2-engine](../m2-engine/README.md). Re-rendered at `df1c3ae`: `04`, `05` and `07` are
rewritten from the new renders.

**3. Does HQ earn its weight?** An HQ grain costs 6.8 Normal grains, so a Budget holds a seventh
as many. Play [01-three-tiers](../../../build/release/listening/m2-engine/01-three-tiers.wav) (Eco, Normal, HQ), [02-hq-transposition-ladder](../../../build/release/listening/m2-engine/02-hq-transposition-ladder.wav)
(A is HQ, B is Normal, alternating) and [03-slow-stretch](../../../build/release/listening/m2-engine/03-slow-stretch.wav) (Normal, then HQ at 6.25 s).
Listen for the cleaner top, the missing fold-back at each step and the missing image whistle.
Answer: `yes` or `no`.

**4. Does the steal threshold sound right under sustained overload?** A grain may be stolen
once it has sounded half its window (D4). Play [04-overload-ladder](../../../build/release/listening/m2-engine/04-overload-ladder.wav) (four HQ layers,
Budget 4,096, 1,024, 256, 4,096) and [05-steal-tails](../../../build/release/listening/m2-engine/05-steal-tails.wav) (one layer, Budget 256): the cloud
renews in waves about twice a second, and nothing clicks (the new fade is a raised cosine).
Answer: `keep`, `earlier` (renew faster) or `later` (hold grains longer, drop more).

**5. Is HQ's weak spot acceptable?** Its anti-aliasing is weakest just under an octave or two
up (39 to 41 dB less fold-back than Normal at +11 and +23, against 82 at exactly +12 and +24), and
its broadband floor at stretch 0.67 is about -99.5 dB. Play [02-hq-transposition-ladder](../../../build/release/listening/m2-engine/02-hq-transposition-ladder.wav)
(the +19 step at 8 to 12 s against +12 and +24) and [07-live-hq](../../../build/release/listening/m2-engine/07-live-hq.wav) (47 dB less fold-back
than Normal over 7.7 to 8.2 s). Answer: `accept` or `no`.

## Sources

Pack: [m2-sources](../m2-sources/README.md). The pack asks no ear question. Its one check, `08a`
and `08b` rendering identical bytes, passed on the re-render
(`c60c0a190f28f73c0eb264551b1c97dc94646fad7be78ef051ecb81376848e2e`). Its host checks are in the
matrix below (step S5) and in item 40.

## Extremes

Pack: [m2-extremes](../m2-extremes/README.md). Re-rendered at `df1c3ae`: the Spectral files (`03`,
`04`, `05`, `13`) moved by row 365 (DC at unity) and every figure still matches.

**6. Is the Peak limiter clean enough?** It clicks by the metric on hot material, from its
instant gain steps (zero lookahead, D5b kept the law, 2026-10-31). Play [12-limiter-modes](../../../build/release/listening/m2-extremes/12-limiter-modes.wav)
from 8 s (Mode Peak). Answer: `keep` or `revisit` (after M2).

**7. Is the Spectral kind's width wanted?** Each channel's phases are keyed apart, so centered
content comes out wide. Play [03-spectral-scan](../../../build/release/listening/m2-extremes/03-spectral-scan.wav), [04-spectral-freeze-fx](../../../build/release/listening/m2-extremes/04-spectral-freeze-fx.wav)
and [05-spectral-vs-grains](../../../build/release/listening/m2-extremes/05-spectral-vs-grains.wav). Answer: `keep` or `narrower` (a mono source stays nearer
the center).

**8. Is the gap on a switch to Spectral acceptable?** A layer switched to Spectral is near
silent for about 0.1 s, and longer with Spray, before the smear fades in. No render has it: in a
host, switch a layer's Source from Live (or Sample) to Spectral while it plays. Answer: `accept` or
`shorten`.

**9. Is the squash of a loud repeat at Feedback 100% musical?** The delay loop's soft
limiter pulls a loud repeat toward 0.5 over the first passes, then holds it (D7). Play
[11-delay-hold](../../../build/release/listening/m2-extremes/11-delay-hold.wav) (a tone, held 20 s within 0.1 dB), then in a host send a loud burst
through the delay at Feedback 100%, Damping 0. Answer: `accept` or `no`.

**10. Is the bend from Reverb Modulation's glide acceptable?** Modulation now glides 0 to 1 in
30 ms, so a held tail bends about 3 semitones for those 30 ms when Modulation jumps. No render has
it: in a host, Decay infinite, Damping 0, a held pad, Modulation 100% to 0% at once. Answer:
`accept` or `no`.

## Presets

Pack: [m2-presets](../m2-presets/README.md). Re-rendered at `df1c3ae`; its measured table was diffed
on `release` (row 398): 54 of 55 rows identical, one peak moved 0.01 dB. The 48 presets' renders
are in the pack's folder (`fx-NN-*.wav`, `inst-NN-*.wav`); rows 394 to 397 are in the carry-over
section below.

**11. Is there a preset you would cut or redo?** Play any of the 48; the table in the pack gives
each one's character. Answer: `none`, or the numbers (for example `fx-22, inst-15`).

**12. Should INIT start the instrument further into its sample?** Play [init-inst](../../../build/release/listening/m2-presets/init-inst.wav)
(peak -20.3 dBFS, RMS -39 dBFS: Position 0 is the violin's quiet bow start; the same spec at
Position 0.5 peaks at -4.9) and [init-fx](../../../build/release/listening/m2-presets/init-fx.wav). Answer: `keep` or `further` (D8's change).

**13. Does Infinite Regress stay bounded?** Play [fx-19-infinite-regress](../../../build/release/listening/m2-presets/fx-19-infinite-regress.wav) (Feedback
0.93; it builds over 8 s to -12 dBFS RMS). Answer: `yes` or `no`.

**14. Does the Mutate ladder stay in the family at 10% and leave it by 60%?** Play
[mutate-ladder](../../../build/release/listening/m2-presets/mutate-ladder.wav) (FX 01 Soft Focus, then Mutate at 10%, 30% and 60%, 4 s each). Each
bus filter's type is a stepped slot, so about 73% of 15% presses flip at least one of the eight
types with four layers on. Answer: `fine` or `lock` (lock the two filter types per layer).

## Views

Pack: [m2-views](../m2-views/README.md). Re-rendered at `df1c3ae`; every figure matched.

**15. Do the 16 snapshots look right?** The 960 x 600 set, both themes. Look for the four
lanes in the layer colors (A orange, B sky blue, C green, D pink), the pad's corners and handle, the
macro row, undo and redo and the theme switch in the header, and in the light theme which parts stay
dark by design (the lanes, D9) and whether everything else reads. In the Mod view the two
vertical scrollbars are the panel's and the route list's. Answer: `yes`, or the picture and what's
wrong.

| | Perform | Edit | Mod | FX |
|---|---|---|---|---|
| FX, dark | [fx-perform-dark](../m2-views/snapshots/fx-perform-dark-960x600.png) | [fx-edit-dark](../m2-views/snapshots/fx-edit-dark-960x600.png) | [fx-mod-dark](../m2-views/snapshots/fx-mod-dark-960x600.png) | [fx-fx-dark](../m2-views/snapshots/fx-fx-dark-960x600.png) |
| FX, light | [fx-perform-light](../m2-views/snapshots/fx-perform-light-960x600.png) | [fx-edit-light](../m2-views/snapshots/fx-edit-light-960x600.png) | [fx-mod-light](../m2-views/snapshots/fx-mod-light-960x600.png) | [fx-fx-light](../m2-views/snapshots/fx-fx-light-960x600.png) |
| Instrument, dark | [inst-perform-dark](../m2-views/snapshots/inst-perform-dark-960x600.png) | [inst-edit-dark](../m2-views/snapshots/inst-edit-dark-960x600.png) | [inst-mod-dark](../m2-views/snapshots/inst-mod-dark-960x600.png) | [inst-fx-dark](../m2-views/snapshots/inst-fx-dark-960x600.png) |
| Instrument, light | [inst-perform-light](../m2-views/snapshots/inst-perform-light-960x600.png) | [inst-edit-light](../m2-views/snapshots/inst-edit-light-960x600.png) | [inst-mod-light](../m2-views/snapshots/inst-mod-light-960x600.png) | [inst-fx-light](../m2-views/snapshots/inst-fx-light-960x600.png) |

**16. Do the three moves sound like what the views show?** Play [01-window-drag](../../../build/release/listening/m2-views/01-window-drag.wav)
(the rectangle's four drags), [02-lane-freeze](../../../build/release/listening/m2-views/02-lane-freeze.wav) (a lane frozen, then thawed) and
[03-pad-path](../../../build/release/listening/m2-views/03-pad-path.wav) (the pad around the corners). No click at any step; the centre sits about
5 dB under a corner (D1). Answer: `yes` or `no`.

**17. Is the pad's double-click as two undo entries acceptable?** A real double-click is two
presses, the first an ordinary click (VW-T10a), so one double-click needs two undos. In any editor,
double-click the pad, then undo. Answer: `accept` or `no`.

## Carry-over

**18. Row 352: does a Ring Length drag click?** A drag at about 120 Hz clicks on the ring (its
click score is 12.7 against 15; the output's is 0). In any cell of the matrix, play audio through
the FX with a running ring (a Live layer, Freeze off) and drag Ring Length in the Edit view's Ring
panel (or the host's generic editor, where it shows the parameter) up and down for ten seconds;
check that a long ring's lane still draws its peaks (rows 385 and 386). Answer: `no click` or
`click` (fix it).

Row 387, the new strings. Each is a text the views added; answer `ok` or give the replacement.

**19. "Load a sample"** (a lane menu's file chooser). Answer: `ok` or edit.

**20. "Save the capture as"** (the capture's file chooser: the lane's right-click menu, Save
capture as…). Answer: `ok` or edit.

**21. "can't save the capture"**, in a notice, with ": the file can't be written" or ": not
enough memory". Answer: `ok` or edit.

**22. The strip's short forms "Osc", "Resamp" and "Spec"** (Oscillator, Resample, Spectral),
drawn where a narrow strip's Source button can't fit the name. Answer: `ok` or edit, naming the
word.

**23. "<L>'s sample is loading: set <L> Source to Sample to play it"** (a drop, or a capture,
onto a Live layer: drop a file on a Live lane). Answer: `ok` or edit.

**24. "Captured into <L>"** (a capture into a Sample layer). Answer: `ok` or edit.

**25. "can't capture the ring: not enough memory"** (set a low memory cap and capture). Answer:
`ok` or edit.

Rows 394 to 397 (the presets):

**26. Row 394: does the gained content hiss under granulation?** The quiet sources were gained
up to +41.8 dB to a -1 dBFS peak, raising their noise floors. Play [inst-15-kalimba-steps](../../../build/release/listening/m2-presets/inst-15-kalimba-steps.wav)
first (the mbira's +27 dB hiss shows even after its 3.5 kHz low-pass), then [inst-04-low-tide-brass](../../../build/release/listening/m2-presets/inst-04-low-tide-brass.wav)
(trombone +41.8), [inst-08-earth-hum](../../../build/release/listening/m2-presets/inst-08-earth-hum.wav) (double bass +35.6), [inst-14-low-bow-bass](../../../build/release/listening/m2-presets/inst-14-low-bow-bass.wav),
[inst-18-clockwork-brass](../../../build/release/listening/m2-presets/inst-18-clockwork-brass.wav) (+32.3 and +29.7), [inst-17-tremolo-engine](../../../build/release/listening/m2-presets/inst-17-tremolo-engine.wav) (+31.4),
[inst-03-ember-field](../../../build/release/listening/m2-presets/inst-03-ember-field.wav) (tuba +29.7), [inst-11-gilded-section](../../../build/release/listening/m2-presets/inst-11-gilded-section.wav),
[inst-07-ocean-archive](../../../build/release/listening/m2-presets/inst-07-ocean-archive.wav) (ocean drum +46; also -21 dBFS RMS at Level's +12 dB maximum,
so quiet), [inst-10-felt-mallets](../../../build/release/listening/m2-presets/inst-10-felt-mallets.wav) (marimba +35), [inst-12-harp-lantern](../../../build/release/listening/m2-presets/inst-12-harp-lantern.wav),
[inst-02-glass-orchard](../../../build/release/listening/m2-presets/inst-02-glass-orchard.wav) and [inst-20-shatter-glass](../../../build/release/listening/m2-presets/inst-20-shatter-glass.wav) (wine glass +30.6),
[fx-04-undertow](../../../build/release/listening/m2-presets/fx-04-undertow.wav) and [fx-23-splinter-keys](../../../build/release/listening/m2-presets/fx-23-splinter-keys.wav) (cymbal roll +38.5). Answer:
`accept` or the presets to fix.

**27. Row 395: are Shard Storm's ticks on drums acceptable?** Short pitched-up grains on sharp
onsets tick at drum transients (its golden uses a sweep because its click score swung 8.5 to 22.7
by seed on `loop-48k.wav`). Play [fx-22-shard-storm](../../../build/release/listening/m2-presets/fx-22-shard-storm.wav), then put it on a drum loop in a
host. Answer: `accept` or `no`.

**28. Row 395: do 17 and 18 need to say more about the transport?** They sound like plain
clouds without a playing transport (their descriptions say so; the renders run at 120 BPM). Play
[fx-17-bar-climber](../../../build/release/listening/m2-presets/fx-17-bar-climber.wav) and [fx-18-dotted-echoes](../../../build/release/listening/m2-presets/fx-18-dotted-echoes.wav); 18 and 21 are quiet over
the sparse loop. Answer: `accept` or `reword`.

**29. Row 396: any tag word to strike or add?** The 29 words: focus `texture` `playable`
`rhythmic` `extreme`; character `pad` `drone` `freeze` `smear` `stretch` `glitch` `keys` `pluck`
`percussive` `voice` `strings` `winds` `brass` `metal` `noise` `dark` `bright` `wide` `mono` `reverb`
`dry`; use `midi` `live-input` `sample` `sidechain`. No preset uses `voice` (the content has no
voice) or `dry`, so the browser hides both; `sidechain` has one preset, `mono` two. A change moves
a golden and, from 1.0.0, the frozen order. Answer: `none`, or the words.

**30. Row 396: any name or tag to change?** "Horizon" is in both products' names (Event
Horizon, Bowed Horizon); 18 Dotted Echoes' fixed 375 ms is a dotted eighth only at 120 BPM (its
description says so); all 24 FX presets are tagged `live-input` though only 04 reads a content
sample. Answer: `none`, or the change.

**31. Row 397: does the dark browser's list ground look right?** It changed slightly with
row 313: the lane's dark became the header's ground. Open the preset browser in the dark theme in
any host, over the strip. Answer: `ok` or `no`.

## The host matrix

Live 12 with the VST3 and the AU, Bitwig Studio with the CLAP and the VST3; each cell is one
format with one product (FX or instrument), so eight cells. Run the script below in each. The
standalone apps are checked by the agents' tests. Launch each host from a terminal with
`GG_STRESS_GRAINS=8192` in its environment for step S7 (the canvas then draws 8,192 synthetic
grains); the variable changes nothing else.

**The script, per cell** (answer a cell `pass`, or `fail` with the steps and a few words):

- **S1, Scan and load.** The host's scan lists the plugin in this format; it loads on a track
   with no dialog.
- **S2, Presets.** The host's list shows the product's 24 factory presets, INIT first (the VST3
   program list, the AU factory presets, Bitwig's browser for the CLAP); load one (FX: 01 Soft Focus;
   instrument: 01 Bowed Horizon). The strip's name follows.
- **S3, Editor.** Open it in the dark theme and the light (the header's switch), and resize 960 x 600 down
   to 640 x 360 and back: nothing clipped, blank or stuck.
- **S4, Automation.** Automate one parameter (Position or Density); play: the editor's control follows
   the lane.
- **S5, Save and reopen.** Let a ring fill (the FX: play audio through it; the instrument: set a
   layer to Resample and play notes), turn Freeze on, save the project, close it and reopen it: the ring is held from the
   first sample.
- **S6, Sound.** Instrument: notes from a MIDI clip. FX: audio through it. Audible, with no click at
   start and stop.
- **S7, Frame time.** The header's readout at `GG_STRESS_GRAINS=8192`: at most 3 ms. Note the number.
- **S8, Thread pool.** Switch all four layers on with dense, long grains until the CPU meter climbs.
   Note whether the host gives the plugin a pool (the load spreading over threads) or not.
- **S9, The editor without attaching** (row 232). Close the editor and reopen it five times; in Bitwig
   also open and close the device's window with the editor never shown; delete the instance and
   insert it again. JUCE may shut down and restart inside the editor's destroy: look for a crash,
   a blank window or a lost theme.
- **S10, Live's AU cells only: the host's parameter display after a load.** Load a preset, then
    reopen a saved project, with the generic parameter view open: the values update at once
    (clap-wrapper `4585bc6` reports a values-only rescan as value changes).

**32. Live 12, VST3, Giga Grains FX.** Steps S1 to S9. Answer: `pass` or `fail ...`.

**33. Live 12, VST3, Giga Grains.** Steps S1 to S9. Answer: `pass` or `fail ...`.

**34. Live 12, AU, Giga Grains FX.** Steps S1 to S10. Answer: `pass` or `fail ...`.

**35. Live 12, AU, Giga Grains.** Steps S1 to S10. Answer: `pass` or `fail ...`.

**36. Bitwig, CLAP, Giga Grains FX.** Steps S1 to S9. Answer: `pass` or `fail ...`.

**37. Bitwig, CLAP, Giga Grains.** Steps S1 to S9. Answer: `pass` or `fail ...`.

**38. Bitwig, VST3, Giga Grains FX.** Steps S1 to S9. Answer: `pass` or `fail ...`.

**39. Bitwig, VST3, Giga Grains.** Steps S1 to S9. Answer: `pass` or `fail ...`.

**40. Once-only host checks.** The packs' longer host checklists stay in the packs as their
record; these are the ones worth a sitting, each in the host named. Answer `pass` or `fail ...`
for the group; skipped counts as accept.

- Bitwig: the thirteen remote-control pages move what their names say, including "Filter A" and
  "FX" ([layers](../m2-layers/README.md#host-checklist), [extremes](../m2-extremes/README.md#host-checklist)).
- Live: the instrument's sidechain feeds a layer set to Live, then Spectral, then back with no click
  (Layer B; the layers and extremes packs).
- Both: all four layers at HQ, Density 400 Hz, Size 500 ms, Budget 256, then automated between 256
  and 16,384: no click, dropout or silent layer ([engine](../m2-engine/README.md#host-checklist)).
- Both: Peak limiter at -6 dB on a hot four-layer patch never passes -6 dB on the host's meter; Smear 0
  against 1 on the CPU meter, the two figures noted ([extremes](../m2-extremes/README.md#host-checklist)).
- Both: a ring over 32 MB (about two minutes of stereo) saved and reopened from the captures folder;
  the cap's refusal message in the sample strip; a layer B sample in the FX restored
  ([sources](../m2-sources/README.md#host-checklist)).
- Both: an M1 session opens as it did and the instrument's Macro 7 now drives Feedback; a `midi`
  FX preset with a MIDI track routed to it; Save as and reopen keeps the name with no edited dot; a
  favorite survives a restart; Mutate shows as automation ([layers](../m2-layers/README.md#host-checklist), [presets](../m2-presets/README.md#host-checklist)).
- Both: the installer's content status line reads "Factory content v2 in ..."
  ([presets](../m2-presets/README.md#host-checklist)).
