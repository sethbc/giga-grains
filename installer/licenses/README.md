# License texts no checkout holds

`gg-products` (`cmake/products.cmake`) writes the license set every installer carries into
`build/<preset>/gg-products/licenses/` (spec §6.3). Most of its files come from the pinned
checkouts. These two MIT texts don't: the code they cover is vendored in sst-jucegui without its
license file (`THIRD_PARTY_NOTICES.md`, "MIT-licensed libraries"). Each is copied byte for byte
from upstream, at the commit below, and carries its project's own copyright line.

| File | Upstream | Copied from | Copyright line |
|---|---|---|---|
| `CMakeRC-LICENSE.txt` | https://github.com/vector-of-bool/cmrc, `LICENSE.txt` | tag `2.0.0` (commit `692890b61220c4889ad84a0bac076e9c4d3101c1`), the version sst-jucegui vendors; identical at `master` (`952ffddba731fc110bd50409e8d2b8a06abbd237`) | Copyright (c) 2017 vector-of-bool <vectorofbool@gmail.com> |
| `flowbite-icons-LICENSE.txt` | https://github.com/themesberg/flowbite-icons, `LICENSE` | tag `v1.5.0` (commit `4f85985690fdd553e029f7415d90d32142bb5452`), `main` when fetched (2026-10-29) | Copyright (c) 2024 Themesberg |

Fetched with:

```sh
curl -fsSL https://raw.githubusercontent.com/vector-of-bool/cmrc/2.0.0/LICENSE.txt \
  -o CMakeRC-LICENSE.txt
curl -fsSL https://raw.githubusercontent.com/themesberg/flowbite-icons/4f85985690fdd553e029f7415d90d32142bb5452/LICENSE \
  -o flowbite-icons-LICENSE.txt
```

Upstream's CMakeRC file has no final newline; it is kept as upstream has it.
