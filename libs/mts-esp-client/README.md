# The MTS-ESP client (vendored)

Upstream: https://github.com/ODDSound/MTS-ESP, commit `f214739b8832e7f297cb9970d0c0efbf783f1462`.
License: 0BSD (`LICENSE`, copied unmodified).

These five files are copied byte for byte from that commit, and nothing else:

- `Client/libMTSClient.h` and `Client/libMTSClient.cpp`: the client a plugin uses to read a
  master's tuning. It loads ODDSound's libMTS with `dlopen` (`LoadLibrary` on Windows) at run
  time, when the user has installed it.
- `Master/libMTSMaster.h` and `Master/libMTSMaster.cpp`: the master API, built only for a test
  executable that registers a real master.
- `LICENSE`.

**libMTS, ODDSound's closed runtime library, is never vendored, built, linked or shipped.** The
upstream repository holds its binaries and installers, which is why this is not a submodule: they
must never sit in this AGPL tree. Nothing under upstream's `libMTS/` is here.

`cmake/MtsEspGuard.cmake` fails the configure if any of these files differs from the SHA-256
below or is missing (tests `mts_esp_guard_*`).

| File | SHA-256 |
|---|---|
| `LICENSE` | `481d4c320dc5c1a7f5b4604c8e9a08ac7710e73291b177c468ab69e0fe158837` |
| `Client/libMTSClient.h` | `5c89f524b41f732b955e092c35522eb007dcb3a23db7631e9f9f75f4ffcf958f` |
| `Client/libMTSClient.cpp` | `b854e7c2b657a660431839fb1c413522fa2877296404cee2a36db55d8813c4b3` |
| `Master/libMTSMaster.h` | `124caf7eb967c68c221cd0632b8ead09d143456ec3f72be304007daf5ebfb186` |
| `Master/libMTSMaster.cpp` | `5bad1805125c86b8e710113ff367823fe3615774734b82709cb88cff69293db2` |
