Machine load note from the top controller (load average 73 on 32 cores; tests are timing out and slowing everyone):
- `ctest -j 4`, not 8 or 12; run the full suite at most once per task (the implementer, just before committing); everyone else runs focused labels only.
- Run the at-rest check only for tasks that change DSP or rendering, not for docs/UI-only tasks.
- At most two implementers building at once per sub-project.
- Stop every background shell, `--watch` and monitor you start once you've read what you needed (TaskStop, or kill the process); never leave `gh pr checks --watch` running — poll once when you need CI state.
- **Never `pkill`, `killall` or kill by process name or pattern** (other sessions and other sub-projects run `ctest`, `cmake` and shells on this machine; a name-based kill stops their work). Kill only the PIDs you started, or use TaskStop on your own background tasks.
