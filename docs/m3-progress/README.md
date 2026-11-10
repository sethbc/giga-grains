# M3 progress and resume notes

The one place M3's running state is kept, on `main`, so a new session can resume without hunting
through ignored folders or worktrees (the director, 2026-11-09). Start with [`RESUME.md`](RESUME.md).

| File | What it holds |
|---|---|
| [`RESUME.md`](RESUME.md) | The state of every sub-project and how to resume. Its "LATEST" section and table come first; older sections below it are history |
| [`program-ledger.md`](program-ledger.md) | The controller's append-only log: gates passed, the director's decisions, each sub-project's progress and `PAUSED` lines |
| [`call-review.md`](call-review.md) | The director's answers to every sub-project's provisional calls and brainstorming questions, 2026-11-09 |
| [`briefs/`](briefs/) | The standing instructions leads follow: `STANDARD-WORKFLOW.md` (binding), `lead-brief-common.md`, `resume-2026-11-09.md`, `pause-2026-11-09.md`, the throttle and slowdown notes, and the spec and plan briefs |
| [`ledgers/`](ledgers/) | A snapshot of each sub-project's own SDD ledger at the pause of 2026-11-09 |

The competitor research behind sub-projects 3b and 7 to 11 is in [`docs/research/m3/`](../research/m3/).

## Keeping it current

- **The live copies are these files**, in the main checkout (`/home/sethbc/src/giga-grains/docs/m3-progress/`).
  The controller and every lead write progress lines to `program-ledger.md` there; the controller
  commits them to `main` through a documents-only pull request (CI passes those in about a minute)
  at least at every pause, resume and sub-project merge. The old copies under the ignored
  `.superpowers/sdd/` are retired: each is now a one-line pointer here.
- **A lead's own SDD ledger** stays in its worktree (`<worktree>/.superpowers/sdd/<plan>/progress.md`,
  which git ignores) while it works. At each pause and at each sub-project's merge, the controller
  copies it into `ledgers/`.
- Paths in these files are repository-relative (`docs/m3-progress/...`), except worktree paths,
  which are absolute (`/home/sethbc/.local/share/giga-grains/worktrees/...`).
