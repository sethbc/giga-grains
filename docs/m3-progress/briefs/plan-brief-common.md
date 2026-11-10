# Brief: write one M3 sub-project's implementation plan

> **Since 2026-11-08 (`STANDARD-WORKFLOW.md`, beside this file):** dispatch this only after the sub-project's gate has passed (brainstorming or the provisional-call review with the director). Add this constraint to the plan's Global Constraints: every implementer invokes `superpowers:test-driven-development` and reports RED and GREEN evidence.

Use the superpowers:writing-plans skill (invoke it with the Skill tool) to write the implementation plan for the spec named in your dispatch, in the same shape as sub-project 1's plan (`docs/superpowers/plans/2026-11-07-m3-voice.md` in your worktree: header, Global Constraints, Review Focus, numbered tasks with Files / Interfaces / steps / exact test names and assertions / commands / commit messages). The director asked for parallel progress without the director's input, so make plan-level decisions yourself and note them. Save to `docs/superpowers/plans/2026-11-07-m3-<topic>.md`, commit it on your branch (stage by name; `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`). Do not write code; do not dispatch subagents. Skip the skill's "execution handoff" question: execution will be subagent-driven.

Copy these Global Constraints into the plan verbatim, adding your spec's own:
- Audio thread: no allocation, locks, I/O, syscalls or unbounded loops; RTSan-clean; 0- and 1-sample blocks; `params.flush` trivial.
- Determinism: randomness only from `gg::rng` (seed, layer, stream, field, index); no `std::` distributions; output bit-identical at any host block size; layers summed A to D.
- At rest nothing moves: with the new feature at rest every existing golden keeps its WAV SHA-256. The at-rest check: `/home/sethbc/.local/share/giga-grains/worktrees/giga-grains-m3-voice/.superpowers/sdd/2026-11-07-m3-voice/check-wav-hashes.sh <worktree>` compares the 108 M2-era cases (last line `baseline cases: 108, changed: 0, gone: 0`); also confirm the 8 voice goldens (tests/golden/cases/*voice*) are unchanged unless the task changes voices on purpose.
- Stage files by name; never `git add -A` or `git commit -a` (the at-rest check rewrites expected files while it runs). Never commit regenerated expected files of existing cases (this Linux machine's metrics differ in the last digit).
- CLAP IDs append-only; your sub-project's index ranges as the spec says.
- `-Wall -Wextra -Werror`; GCC 13's diagnostics (CLAUDE.md). Test names ASCII only; tests parallel-safe.
- Format: `CLANG_FORMAT=$HOME/.local/share/giga-grains/format-venv/bin/clang-format scripts/format.sh`.
- New build trees: `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Models: implementers opus for design-heavy tasks, sonnet otherwise; reviewers sonnet at least; never Fable.
- Branch hygiene: this branch merges `m3-voice` (sub-project 1) as it moves; parallel sub-projects 2, 3, 4 edit `core/include/gg/core/ParamIds.h` lists and `Processor::updateVoices` — keep such edits small and self-contained to ease merges.
Reply with the plan's path, commit, task count and one line per task (under 25 lines).
