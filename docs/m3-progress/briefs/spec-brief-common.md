# Brief: write one M3 sub-project's spec (common to sub-projects 2, 3, 4)

> **RETIRED by the director, 2026-11-08. Do not dispatch from this brief.** New specs and design changes come from `superpowers:brainstorming` with the director (`STANDARD-WORKFLOW.md`, beside this file). Kept as the record of how the specs of sub-projects 2, 3, 3b, 4 and 7 to 11 were written.

You are writing the design spec for one sub-project of M3 (Playable) of giga-grains, a deterministic granular synth (C++23, CLAP-first plugins). The director has asked that all remaining M3 sub-projects proceed **in parallel and without the director's input** (2026-11-07: "keep building all of the sub-projects as parallel as possible and merging them as able to. progress as much as you can without input"). So where a spec would normally put a forced choice to the director, you make the call yourself, mark it **"Claude's provisional call"** with the alternative(s) declined and why, and move on. He reviews the calls later.

## Read first (in your worktree)
- `CLAUDE.md` (non-negotiables, build/test/golden conventions).
- `INTENT.md` Part I (never changes), and Part II §14 (engine), §15 (plugins), §18 (state/presets), §20, §23 #94 and later.
- The approved M3 program design: `docs/superpowers/specs/2026-11-07-m3-program-design.md` — its §1-§11, especially §2/§2a (the director's decisions and the approved P1-P8), §4 (the voice model), §6 (pitch-sync), §7 (microtuning), §8 (your sub-project's row: what it delivers and its exit), §9 (rules every sub-project keeps), §10 (what's left to your spec).
- Sub-project 1's spec, the base you build on: `docs/superpowers/specs/2026-11-07-m3-voice-design.md` (V1-V7, B1-B10, §4) and its plan `docs/superpowers/plans/2026-11-07-m3-voice.md`. Sub-project 1 is finishing on branch `m3-voice` (draft PR #37): its plugin task (CLAP note expressions, MIDI-MPE, NOTE_END, `clap_plugin_as_vst3`) and Voice-layer headroom (V7) are landing now.
- The code as it is on your branch (from `m3-voice`): `core/` (Processor, NoteTable, VoiceAllocator, ModMatrix, ParamIds), `engine/` (Engine, CloudScheduler, AmpEnvelope), `plugin/src/`, `tools/gg-render/`.
- The research notes from sub-project 1: `/tmp/claude-1000/-home-sethbc-src-giga-grains/b7b046d0-0ef6-4ef5-a912-4b2c26fccc5b/scratchpad/research/` (note-path.md, mpe-paths.md, voicemanager.md).

## Write
`docs/superpowers/specs/2026-11-07-m3-<topic>-design.md` (topic given in your dispatch), in the same shape as sub-project 1's spec: a status table (Draft, provisional calls, program, branch), Intent, Scope (in/out), Forced choices as Claude's provisional calls (numbered with your sub-project's letter, e.g. E1… for expression), Design (parameters with append-only CLAP IDs and the next free indices; state/patch-version impact; determinism; real-time safety; the at-rest rule: every existing golden's WAV hash unchanged when the new feature is at rest; gg-render spec keys; goldens to add; tests), INTENT changes (Part II, each with a §23 row), and what's left to the builder. Write it so a talented builder could plan from it without asking anyone. Keep it to what this sub-project delivers (program §8); note interactions with the other parallel sub-projects (2 expression, 3 pitch-sync, 4 tuning, 5 views/presets) so they don't collide: in particular, new parameters' CLAP IDs — sub-project 2 takes global indices from 0x25 upward and layer indices from 0x32 upward; sub-project 3 takes global 0x30 upward and layer 0x38 upward; sub-project 4 takes global 0x38 upward (no layer parameters unless needed: then 0x3C upward). State those exact indices in your spec so parallel branches never collide.

Then do a builder check yourself: re-read the spec as the builder, list the questions a builder would still need answered, and answer each in the spec (as a provisional call where it's a design choice).

Commit the spec on your branch (stage it by name; `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`). Don't write code. Don't dispatch subagents.

## Report
Reply with: the spec's path, its commit, the list of provisional calls (one line each), and any conflict you found with the program design, sub-project 1 or the other sub-projects (under 25 lines).
