# Listening pack: M2 sub-project 5 (Presets and content)

**Skeleton.** This pack is filled in by group 2 of the sub-project (the spec's §6.12): the 48
factory presets are voiced on wave 2's parameters and the content, after wave 2 has merged and the
branch is rebased. Group 1 (built) ships the preset system, INIT and two test presets per product,
so no render below exists yet, and the table lists what group 2 will render. The pack is rendered
for the record then and heard at M2's close (sub-project 7; the programme design's decision 1). It
has two parts:

- renders from `gg-render`, to hear offline, one per item of the spec's table (§11);
- a host checklist for Ableton Live 12 and Bitwig Studio, for the close's sitting.

The spec is `docs/superpowers/specs/2026-10-30-m2-presets-design.md`: the preset file §6.1,
loading and INIT §6.2, discovery and the wrapper's bridges §6.3, the content §6.4 and §6.5, the
factory presets §6.6, the browser §6.8 and Mutate §6.9 (the law is decision D4, the locks D3).

## Rendering the files

The renders aren't committed. The specs will be in `specs/` (group 2). Each is a render spec with
a `"preset"` key; the presets are packed from the sources under `content/presets/` by the
`gg-presets` target, and the samples they name come from the pinned content release. From the
repository root:

```sh
cmake --preset release && cmake --build --preset release --target gg-render gg-presets gg-content
r=build/release/tools/gg-render/gg-render
c=build/release/content-cache/content          # or a folder scripts/fetch-content.sh filled
P=build/release/content/Presets                # the packed presets
L=build/release/listening/m2-presets
$r --write-media "$L/media"                    # tone-44k1.wav and loop-48k.wav
for s in docs/listening/m2-presets/specs/*.json; do
  n=$(basename "$s" .json)
  $r "$s" --media-dir "$L/media" --content-dir "$c" -o "$L/$n.wav" --spectrogram "$L/$n.png" \
     --metrics "$L/$n.metrics.json" --events "$L/$n.events.jsonl"
done
```

A spec's `"preset"` path is resolved from the spec's folder or `--media-dir`; `--preset FILE` on
the command line sets or overrides it. The spec's seed is the render's (D6). Every render is
deterministic, so on one platform the same commit gives the same bytes.

## The files (planned: fifty renders, spec §11)

| File | Length | What it is | Listen for |
|---|---|---|---|
| `fx-NN-<slug>.wav` × 24 | 8 s | Each FX preset over the content's input phrase; the eight `midi` ones with two held notes from 3 s | The preset's character; the MIDI ones freezing and repitching on the notes (D7) |
| `inst-NN-<slug>.wav` × 24 | 8 s | Each instrument preset: a two-note phrase, then a chord | Playable from note 1; the focus the tags promise |
| `init-fx.wav`, `init-inst.wav` | 8 s | INIT in each product over the same material | The plain defaults (D8) |
| `mutate-ladder.wav` | 16 s | One texture preset, then Mutate at 10%, 30% and 60% (seed fixed, counter 1 to 3), 4 s each | Variations that stay in the family at 10% and leave it by 60% (D3, D4) |

**Open question for the director:** the content release `v1` has no voice samples (VCSL has none),
so the FX renders' "voice-and-strings phrase" can't be played as the spec words it. Proposed: a
strings-and-mallets phrase from `v1`, and the `voice` tag unused (spec §6.5, pending). Group 2
waits on the answer.

## Host checklist

For the close's sitting in Ableton Live 12 and Bitwig Studio:

- [ ] Install from the PKG and the EXE (and unpack the tarball); the browser's status line says
      "Factory content v1 in …".
- [ ] The 24 presets per product in the browser, in order, INIT first.
- [ ] The VST3 program list and its selector follow the browser, in Live and Bitwig.
- [ ] The AU factory presets list and load, in Live (and Logic, best effort).
- [ ] A `midi` FX preset on a track with a MIDI track routed to it.
- [ ] Save as…, then reopen the project: the preset's name comes back, with no edited dot until
      a value moves.
- [ ] A favorite survives a restart.
- [ ] Mutate recorded as automation (the lanes show the gestures).
- [ ] An M1 session reopens unchanged, with its name "Init".
