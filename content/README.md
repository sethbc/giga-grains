# content

What the factory content needs in this repository (M2 sub-project 5's spec §6.5). No audio is
committed here.

- **`pin.txt`** pins the CC0 sample release in
  [sethbc/giga-grains-content](https://github.com/sethbc/giga-grains-content), three lines:
  `tag <tag>`, `url <the release asset's URL>` and `sha256 <its SHA-256>`. A new release is a new
  tag (`v3`, …), never a moved one, and keeps every file of the one before byte for byte; the
  content repository's `README.md` says how one is made.
- **The pinned release is `v2`** (`giga-grains-content-v2.zip`, 48,200,259 bytes): 51 FLAC files
  in seven categories, 28 recorded samples from the Versilian Community Sample Library (VCSL),
  14 strings and brass from VS Chamber Orchestra: Community Edition (VSCO 2: CE; added in `v2`)
  and 9 signals rendered by `gg-render`, with `PROVENANCE.md`, the CC0 `LICENSE` and a
  `README.md`. There are still no voices.
- **Fetching:** `scripts/fetch-content.cmake` downloads the zip, refuses (and deletes) one whose
  SHA-256 isn't the pin's, unpacks it into `<dir>/content/` and writes `<dir>/content/.gg-pin`
  last; a folder already at the pin costs no network. The `gg-content` target runs it into
  `build/<preset>/content-cache/` (the `content_fixture` test builds it), or, when
  `GG_CONTENT_DIR` names a folder fetched elsewhere, only checks that folder against the pin.
  `scripts/fetch-content.sh <dir>` is CI's wrapper; it prints the `-DGG_CONTENT_DIR=...` option
  last. See `cmake/content.cmake`.
