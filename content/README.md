# content

What the factory content needs in this repository (M2 sub-project 5's spec §6.5). No audio is
committed here.

- **`pin.txt`** pins the CC0 sample release in
  [sethbc/giga-grains-content](https://github.com/sethbc/giga-grains-content), three lines:
  `tag <tag>`, `url <the release asset's URL>` and `sha256 <its SHA-256>`. A new release is a new
  tag (`v2`, …), never a moved one; the content repository's `README.md` says how one is made.
- **Fetching:** `scripts/fetch-content.cmake` downloads the zip, refuses (and deletes) one whose
  SHA-256 isn't the pin's, unpacks it into `<dir>/content/` and writes `<dir>/content/.gg-pin`
  last; a folder already at the pin costs no network. The `gg-content` target runs it into
  `build/<preset>/content-cache/` (the `content_fixture` test builds it), or, when
  `GG_CONTENT_DIR` names a folder fetched elsewhere, only checks that folder against the pin.
  `scripts/fetch-content.sh <dir>` is CI's wrapper; it prints the `-DGG_CONTENT_DIR=...` option
  last. See `cmake/content.cmake`.
