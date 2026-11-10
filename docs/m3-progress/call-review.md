# M3 provisional-call review with the director (from 2026-11-09)

Run as a brainstorming session (the director's request): multiple-choice, flagged calls first.
A sub-project's gate passes only when its review is complete; then append its `gate passed` line to
`docs/m3-progress/program-ledger.md`.

## Sub-project 2, Expression
- E1 (MPE announcements): **confirmed**: write the MIDI settings.
- E9 (Voice steal order): **confirmed**: tails first, then heaviest; D4 applied per voice.
- E16 (note routing): **confirmed**: key range and channel/zone per layer.
- E17 (envelope curves): **confirmed**: one curve per envelope.
- Remaining calls (E2-E8, E10-E15, E18): **accepted as specified** (offered E5, E6/E18, E13 for reopening; none taken). Review complete 2026-11-09.

## Sub-project 3, Pitch-sync
- S2 (formant): **confirmed**: fixed formant; a Formant Track amount stays for later (layer index 0x39 stays free).
- S3 (Size at Clock Pitch): **confirmed**: Size reads as periods.
- S4 (Density at Clock Pitch): **confirmed**: Density is the refresh rate, draws held between refreshes.
- Remaining calls (S1, S5-S14): **accepted as specified** (offered S1, S5, S7 for reopening; none taken). Review complete 2026-11-09.

## Sub-project 3b, Playhead
- H1 (Scan on a moving ring): **confirmed**: adds to the ring's motion.
- H2 (Scan Speed's law): **confirmed**: cubic, through zero.
- H4 (Cloud restart): **confirmed**: first key after none.
- Remaining calls (H0, H3, H5-H12): **accepted as specified** (offered H3, H7, H11 for reopening; none taken).
- H13: **amend the spec**: tempo-locked scanning is sub-project 7's (Scan Sync), in M3, not M4's. The director moved tempo sync into M3 after H13 was written. To do on `m3-sp3b-playhead` when it resumes (no merged task touched: H13 was out of scope). Review complete 2026-11-09.

## Sub-project 4, Microtuning
- T2 (preset tuning): **confirmed**: the preset's when it carries one, else the instance's.
- T10 (libMTS): **confirmed**: run-time load only, 0BSD client vendored, nothing closed built, linked or shipped. Told the director this is precedent (Surge XT, Vital), not legal clearance. T10's "pin the repository" and T13's submodule wording are superseded by the ledger's vendoring ruling (Task 1).
- T16 (Pitch Quantize): **confirmed**: the whole pitch snaps.
- Remaining calls (T1, T3-T9, T11-T15): **accepted as specified** (offered T1, T6, T7 for reopening; none taken). Review complete 2026-11-09.

## Sub-project 8, Grain shapes
- G2 (reverse span): **confirmed**: the forward twin's span, from its far end.
- G7 (window under Reverse): **confirmed**: the window stays in output time.
- G9 (Skew's floor): **confirmed**: 1% of the grain, at least 16 samples a side.
- G6 (reversed grains' kernels): reopened by the director, then **confirmed**: full SIMD now (plan Tasks 2 and 3).
- Remaining calls (G1, G3-G5, G8, G10-G16): **accepted as specified** (offered G1, G4 for reopening; not taken). Review complete 2026-11-09.

## Sub-project 9, Per-voice filter
- F3 (order): **confirmed**: the voice filter, then the layer's bus filters.
- F5 (controls): **confirmed**: Type, Cutoff, Resonance, Key Track.
- F7 (factory routes): **confirmed**: none.
- Remaining calls (F1, F2, F4, F6, F8-F14): **accepted as specified** (offered F8, F10, F12 for reopening; none taken). Review complete 2026-11-09.

## Sub-project 7, Tempo sync (brainstorming, 2026-11-09)
- R3 (sync switches): **overruled**: one Sync switch per modulator per layer (LFO 1, LFO 2, Random 1, Random 2, Step), replacing the one Mod Sync ladder. IDs: the five unused layer indices 0x6B to 0x6F (3b's H12 leaves them free); spec and plan to amend.
- R2 (stopped): **confirmed**: run on at the last tempo; relock at play.
- R4 (divisions): **confirmed**: the rate knob snaps.
- R8, R9 (looper scope): **confirmed**: bar-quantized hold and resume; the rest is M4's.
- Remaining calls (R0, R1, R5-R7, R10-R15): **accepted as specified** (offered R5, R6, R10 for reopening; none taken).

## Sub-project 10, Scenes (brainstorming, 2026-11-09)
- N2 (what a scene holds): **confirmed**: base values only.
- N3 (recall): **confirmed**: writes the base values.
- N11 (presets): **confirmed**: a preset load replaces the bank.
- Remaining calls (N1, N4-N10, N12-N15): **accepted as specified** (offered N1, N8, N12 for reopening; none taken).

## Sub-project 11, Drive/crush/resonator (brainstorming, 2026-11-09)
- C3 (drive level): **confirmed**: level-matched at -12 dBFS.
- C8 (resonator key): **confirmed**: Fixed, Last Note, Held Notes, with Channel.
- C9 (resonator level): **confirmed**: unity noise power, capped at +18 dB.
- Remaining calls (C1, C2, C4-C7, C10-C15): **accepted as specified** (offered C1, C2, C4 for reopening; none taken).

## Sub-project 5, Views and presets (brainstorming, 2026-11-09; no spec yet)
- Scope: **the program row plus every handoff** from the other sub-projects (one complete pass; starts once what it presents has landed).
- Perform view: **grains tinted by voice** on the existing lanes; no per-note strips.
- New factory presets: **12 instrument, 6 FX**, appended.
- Edit view: **tabs inside Edit** (Sound, Voice, Color, Modulators as sketched).
- Scene buttons: **a strip in Perform**, between the lanes and the macros (pads 1 to 8, Scene B, Morph, Punch).
- Inactive controls: **gray but reachable**.
- Tuning: **a header button** beside the preset name, showing what's in effect and opening the scale list.
- Voice tints: **shades of the layer's color**.

## Sub-project 6, the Close (brainstorming, 2026-11-09; no spec yet)
- Listening: **a short pack as each sub-project lands**, then a shorter final gate.
- Logic Pro: **check and record**; doesn't block 0.3.0; resolves §22's question.
- Carry-over: **triage every row**: fix the cheap and real, keep with a named reason, or close as obsolete.

## Outcome
Gates passed for 2, 3, 3b, 4, 8 and 9 (m3-program-ledger.md). No call overruled; one amended (H13). Nothing restarts until the director says go: passing a gate is not the go.
