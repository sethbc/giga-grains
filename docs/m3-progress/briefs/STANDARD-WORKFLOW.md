# M3 standard workflow (the director, 2026-11-08)

Binding on every M3 session (top controller, sub-project lead, subagent) from now on, including
every resume. Where an older brief, note or ledger says otherwise, this file wins. The director's
own later instructions win over this file.

**Nothing restarts on the strength of this file.** The director's go is still required before any
agent or build resumes (`docs/m3-progress/RESUME.md`, "Waiting on the director").

## The seven skills

Invoke each one with the Skill tool. Following its idea without invoking it does not count.

| Skill | Who | When | M3 specifics |
|---|---|---|---|
| `superpowers:brainstorming` | The top controller, in a session with the director (a subagent can't: it needs the director's answers) | Before any new spec or design change; see the gates below | No spec is written from `spec-brief-common.md`'s "provisional calls" mode any more |
| `superpowers:using-git-worktrees` | Lead or controller | At the start of a sub-project's build, a resume, and before any new task worktree | Follow its order: detect existing isolation (the sub-project worktrees pass, skip to setup), then a native tool, then the git fallback. Declared preferences: worktrees are always wanted (no consent question), and the fallback directory is `/home/sethbc/.local/share/giga-grains/worktrees/`, never a top-level folder in `~/src` (the director's personal source directory). Project setup is `scripts/worktree-setup.sh --configure dev` with `CMAKE_C_COMPILER_LAUNCHER=ccache CMAKE_CXX_COMPILER_LAUNCHER=ccache` exported |
| `superpowers:writing-plans` | A plan agent or the controller | After brainstorming, and for any plan amendment | `plan-brief-common.md`, unchanged in method |
| `superpowers:subagent-driven-development` | Each sub-project's lead | Every build | Resume from the ledger per the skill's `references/resuming.md`: a task with a `Task <N>: complete` line is done |
| `superpowers:test-driven-development` | Every implementer | Every task that changes code or behavior | The dispatch names the skill, whatever the task brief says. The report carries TDD evidence (the RED failure, then GREEN); the task reviewer treats missing evidence as a finding |
| `superpowers:requesting-code-review` | Reviewer dispatch | Each task and the final whole-branch review run through SDD (the final review uses this skill's `code-reviewer.md`). Invoke it directly for any change outside an SDD plan before it merges (row 403's fix, for one) | Reviewer floor sonnet, final review opus |
| `superpowers:finishing-a-development-branch` | Lead (top controller for sub-project 1) | Last step of every branch, after the clean final review | Standing answer to its menu, from the director: **option 2, push and open a pull request**, as a **draft**, base `main`. Then poll CI until green. The worktree stays. Merging into `main` stays with the top controller, under the director's "merging them as able to" |

## Where things live (the director, 2026-11-08)

- **Worktrees:** `/home/sethbc/.local/share/giga-grains/worktrees/`, moved there from `~/src` on
  2026-11-08 (`gg-m3t-t6.leftover` there is a one-log remnant, not a worktree). A moved tree's
  `build/` holds stale absolute paths: delete it and run `scripts/worktree-setup.sh --configure dev`
  when that sub-project resumes. Worktrees the native tool creates inside the repository, under
  `.claude/worktrees/`, are fine.
- **Older paths:** committed plans and specs on the M3 branches still name `/home/sethbc/src/gg-m3*`
  and `/home/sethbc/src/giga-grains-m3-voice`. Read those as the same names under the worktrees
  folder above.
- **The resume notes** (`docs/m3-progress/RESUME.md`, the program ledger, `docs/m3-progress/briefs/`): the main checkout's
  ignored `docs/m3-progress/`.
- **The at-rest check** is `scripts/check-at-rest.sh <worktree>`, committed with its baseline, once
  a branch has merged `main` with it. Until then, the old `check-wav-hashes.sh` in sub-project 1's
  plan workspace still works; keep that workspace until every M3 branch has merged it. Where a plan
  names `check-wav-hashes.sh`, read `scripts/check-at-rest.sh`. The output's last line is unchanged.

## The gates on resume

A lead may start or resume a sub-project only after its gate has passed. The top controller records
each pass by appending to `docs/m3-progress/program-ledger.md`:
`<sub-project>: gate passed YYYY-MM-DD (<brainstorming|provisional-call review>)`.
A lead checks for that line first and stops if it is missing.

| Sub-project | Gate | After it |
|---|---|---|
| 1 Voice (`m3-voice`, PR #37) | None: the director approved V1 to V4 and B10 by name | Row 403's debugging (`superpowers:systematic-debugging`), requesting-code-review for the fix, then finishing |
| 2 Expression, 3 Pitch-sync, 3b Playhead, 4 Microtuning, 8 Grain shapes, 9 Per-voice filter | Go through that spec's provisional calls with the director, the flagged ones first (`docs/m3-progress/RESUME.md`), before any more building. In-flight and uncommitted task work stays untouched until then | An overruled call amends the spec; writing-plans amends the remaining tasks; list any merged task the change touches. Then SDD resumes from the ledger |
| 7 Tempo sync, 10 Scenes, 11 Drive/crush/resonator | Full brainstorming with the director, with the existing spec and plan as input | writing-plans amends or confirms the plan on that branch, then SDD starts |
| 5 Views and presets, 6 Close, and any new sub-project | Full brainstorming with the director before any spec exists | writing-plans, then SDD |

## What stays as it was

- A lead still decides, and records as a ruling, what an approved spec and plan leave open during a
  build. That is the only remaining use of a provisional call. It never stands in for brainstorming,
  even while the director is away: work that hasn't passed its gate waits.
- Parallel implementers only in separate worktrees (SDD forbids two in one workspace).
- `slowdown-note.md` (one implementer at a time, sonnet, opus for design-heavy engine tasks and the
  final review) and `throttle-note.md` hold until the director lifts them. On re-reviews, SDD's
  `references/fix-loop.md` governs.
- CLAUDE.md's rules: no Fable, `model` named on every dispatch, stage files by name.
