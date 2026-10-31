# M2 sub-project 5: Presets and content — design

| | |
|---|---|
| **Milestone** | M2 Giga (INTENT §20), sub-project 5 of 7, wave 3 (merged first in its wave) |
| **Status** | **Approved by the programme manager 2026-10-30** against the programme design and INTENT, with two notes applied in place: this sub-project's INTENT §23 row takes the next free number at merge (sub-project 3 merges first), and under `media/` it adds the new header `gg/media/ContentPaths.h` alone, header-only, editing nothing else there until sub-project 2 merges and this branch rebases (§6.4). **The director's decisions D1–D9, 2026-10-30, by forced choice, relayed by the programme manager: every recommended option** (option 1 of each), recorded in §8 and applied in place. Coordination with sub-project 6 (§6.8): it keeps the header label's component ID `header.preset` and its bounds slot for this sub-project, which replaces that label's contents and moves nothing else in the header. **Group 1 built 2026-10-31** (Tasks 1–13 and 10b; INTENT §23 #70); group 2 (Tasks 14–18 and content v2) goes in a new branch and pull request. (Earlier: group 1 built 2026-10-30, Tasks 1 to 13 plus 10b, on `m2-presets`; PR #17, draft), **group 2 pending** (§6.12: wave 2's merge, then the voicing of the 48 presets). Amended in place, each marked **Amended**: §6.2, §6.3, §6.4, §6.5, §6.8, §6.9, §6.11, §10 and §12 |
| **Spec authority** | `INTENT.md`; the programme design (`docs/superpowers/specs/2026-10-29-m2-programme-design.md`), whose §6.5, §7.5 and §7.8 this document realizes and does not reopen |
| **Builds on** | Sub-project 1 (layers, the mod matrix, the patch at version 2, `gg-render`'s layer keys; INTENT §23 #65); the release pipeline's reserved Content component (release-pipeline spec §6.9); M1's state container (INTENT §18) and Perform view (§17) |
| **Starts from** | `main` at `ab9f4e2` (PR #15 merged) on branch `m2-presets`; `docs/m2-carry-over.md` with **11 open rows** |
| **Waits on** | Wave 2, for its second group of tasks only (§6.12): `MEDI` version 2's content-relative reference (sub-project 2) and wave 2's parameters, which the factory presets are voiced with |

## 1. Why this sub-project

Until now both products start at "Init" and stay there: a saved session is the only way a sound
survives, there is no factory sound to reach for, no sample ships with the instrument, and the
instrument is silent until the user finds a file. INTENT promises presets through one system,
CLAP preset discovery (§18), about 100 curated presets by v1.0 across the four focuses (§12),
MIDI-driven FX presets as a headline feature (§23 #25, #32), a small curated CC0 sample set kept
out of git history (§12), a browser with tags, favorites and INIT, and Mutate (§17). The
programme design places all of it here (§6.5, items I24 to I29), with the content pipeline (§7.8)
and the preset container (§7.5).

The sub-project delivers, in the programme design's words: the preset file format and CLAP preset
discovery and `preset-load`, with the VST3 and AU bridges checked and the identity tests extended to
the selector parameter; the factory presets, a floor of 40 across INTENT §3's focuses and at least 8
MIDI-driven FX presets, each voiced with the mod matrix and tagged; the CC0 sample set, its
repository and release asset, the fetch script, the installers' Content component filled, and the
plugins' content search path; `gg-render --preset`; Mutate with locks and an amount, on the main
thread; and the browser (tags, favorites, search, INIT) replacing the header's "Init".

**Its exit** (programme design §6.5): every factory preset loads in all four formats in Live and
Bitwig (met here by machine-checkable stand-ins, §10, and at the close's sitting, §11); each preset
has a golden render; the installers install the content and the plugins find it on all three
platforms; `gg-render --preset` renders a factory preset bit-identically to the plugin's state load;
the license set carries the content's CC0 notice.

## 2. Goals

- **One preset system.** A preset is a `GGST` container: the same bytes a state load reads, with
  the preset's name, tags and description inside its `PTCH`, and no `AUDI` (§6.1). The browser,
  the host's lists (through CLAP preset discovery and clap-wrapper's bridges) and
  `gg-render --preset` all read it the same way.
- **Both plugins advertise presets to every host:** the CLAP preset-discovery factory and the
  `preset-load` extension, which the pinned clap-wrapper already turns into a VST3 program list
  with a selector parameter at `0xC00000` and into AU factory presets (§6.3). No clap-wrapper bump.
- **Factory content that is found wherever the installer put it,** on all three platforms, and a
  user content folder beside it (§6.4).
- **A content pipeline that never touches this repository's history:** the samples in the public
  `giga-grains-content` repository under CC0-1.0, each with its provenance, released as a tagged
  zip that CI and `scripts/fetch-content.sh` fetch at a pinned tag and SHA-256 (§6.5).
- **At least 48 factory presets** (D1): 24 for each product, 8 of the FX's MIDI-driven, across the
  four focuses, each voiced with the mod matrix and tagged, each with a golden render.
- **Mutate** on the main thread, deterministic from the instance's seed, with per-parameter locks
  and an amount (§6.9).
- **The browser** in the header's preset slot: list, search, tag filters, favorites, INIT, save as
  a user preset, previous and next (§6.8).
- Every M1 and sub-project 1 golden keeps its WAV SHA-256: nothing here changes the audio path.

## 3. Non-goals and anti-goals

**Non-goals.**
- Undo and redo (I29's other half; sub-project 6), the Edit, Mod and FX views, and lock icons on
  their controls. Mutate's locks live in its own panel here; sub-project 6 may surface them on the
  Edit view's controls later.
- Per-layer media in the plugin's loader, `MEDI` version 2 and `AUDI` (sub-project 2). This
  sub-project adopts sub-project 2's content-relative reference once it merges (§6.12) and defines
  nothing of the manifest itself.
- New parameters. Wave 3 adds none (programme design §7.1).
- A preset "pack" format, downloadable soundpacks, user tags editing beyond save-time tags, preset
  ratings, cloud anything (INTENT §9).
- Host-side preset browsers' extras (CLAP preset discovery's extra-info, images, soundpack art).
- The About box (row 221, sub-project 6).
- Recordings of the director's (programme design decision 8).

**Anti-goals** (a failure even if everything works).
- Any byte of CC0 sample audio in this repository's history, or any sample without a CC0 statement
  and a recorded provenance.
- A preset load that allocates, locks or does I/O on the audio thread: loading is a main-thread
  state load; the audio thread takes the result as it takes a session restore today.
- Output that depends on where the content was found, or on whether the host offers a preset list.
- A golden that quietly depends on the network at test time without saying so: the content fixture
  is explicit (§10).
- The factory preset order changing by accident before 1.0.0 freezes it: the order is the file
  names' (§6.6), pinned by a test.

## 4. Constraints

- CLAUDE.md's non-negotiables and INTENT §18's frozen set: no CLAP parameter is added, so nothing
  frozen moves; the VST3 selector parameter is clap-wrapper's, at `0xC00000` or the first free ID
  above it, and our IDs stay below `0xB00000`.
- The state rules (programme design §7.5): the container stays at version 1; no new chunk and no
  patch version bump are this sub-project's (sub-project 2 alone adds chunks; sub-project 1 made
  M2's one bump). Preset metadata therefore lives inside `PTCH` (§6.1), where every reader back to
  M1 ignores it.
- The determinism rules: Mutate draws from the engine's counter RNG; no `std::` distribution.
- Licensing: content is CC0-1.0 with a statement per sample; the preset sources in this repository
  are AGPL-3.0-or-later like the code; the license set gains the CC0 text and
  `THIRD_PARTY_NOTICES.md` a factory-content section. Every dependency stays pinned; this
  sub-project adds none (no `flac` library: FLAC is encoded by hand when the content release is
  made, and decoded by dr_flac, already in gg-media).
- The programme's process (CLAUDE.md, "Working style"; programme design §9, §12): a draft PR,
  `scripts/check.sh` over `dev,asan,rtsan`, CI green, `scripts/update-goldens.sh` before and after
  with the hash list in the commit, the carry-over counted at start and end, the listening pack for
  the record only.
- Ownership (programme design §6.5): the preset system in `plugin/` and `core/` (discovery,
  `preset-load`, the container's reading), `scripts/fetch-content.sh` and the installers' Content
  component, the content repository, the browser's UI files, Mutate. Shared files
  (`GigaGrainsPlugin`, `Editor.cpp`, `tests/CMakeLists.txt`, INTENT) only by addition; the loader's
  candidate list gains one entry (the content folders) after sub-project 2 merges, and nothing of
  sub-project 2's is copied.

## 5. Approaches

**Where preset metadata lives.** Three were weighed.
- **Inside `PTCH`, as a `<preset>` element under `<patch>` (chosen).** `fromStateXml` reads
  `<params>`, `<engine>` and `<mod>` by name and ignores other children, and PatchBase reads only
  the root's attributes and `<params>`, so an M1 build and every M2 build load a preset's patch
  unchanged. No chunk, no version bump, no owner conflict with sub-project 2. The name rides in
  PatchBase's own `name` attribute, which the plugins have written as "Init" since M1.
- *A `PRST` chunk.* Clean, but a chunk is sub-project 2's to add (§7.5); asking for an exception
  buys nothing the element doesn't give.
- *A sidecar file beside the preset.* Then the browser and a host's "load from file" read
  different bytes from a state load, against §7.5's "the same bytes".

**Where the factory presets' sources live.** Two were weighed.
- **In this repository, as readable JSON sources compiled to `.ggpreset` at build time (chosen).**
  A preset is first-party work, reviewed like code, versioned with the parameters it names, and the
  golden tests need every preset at test time. The shipped file is the `GGST` container; the JSON is
  the authoring form, in `gg-render`'s existing vocabulary (`params`, `mod`, `samples` by display
  name), compiled by `gg-render --pack-preset`. A binary blob per preset in git would be unreviewable
  and would drift from the names it was authored against.
- *In the content repository, as `.ggpreset` files.* Then the content release and this repository
  version separately, a preset golden needs the network for its preset as well as its sample, and
  a parameter renamed here silently breaks a preset there.

**Where the samples live and how they reach a machine.** Fixed by the programme design (§7.8): the
public content repository, a tagged release zip, `scripts/fetch-content.sh`, the installers'
Content component. The spec's choices are the layout (§6.5), the pin's form and the fixture that
gives the tests the content (§10).

**How a host shows the presets.** Fixed by INTENT §18 (one preset system, CLAP preset discovery) and
by the pinned clap-wrapper, which indexes the plugin's own discovery provider in a process-wide
background crawl, publishes the result as a VST3 program list on the root unit with a selector
parameter and as AU factory presets, and loads through `preset-load` (§6.3). The spec adds nothing
to the wrapper and bumps nothing.

**Mutate's law.** Relative jitter (D4, recommended) against a blend toward a fresh random point:
§6.9 gives both; the director picks.

## 6. Design

### 6.1 The preset file (programme design §7.5)

A preset is a **`GGST` container**, exactly what `encodeState` writes (INTENT §18): `PTCH` and
`MEDI`, in that order, no `AUDI`, container version 1. The extension is **`.ggpreset`**. What makes
it a preset is inside `PTCH`:

```
<patch id="org.sethbc.giga-grains.patch" version="2" name="Glass Hold">
  <params>…</params><engine seed="0" /><mod>…</mod>
  <preset product="fx" author="Giga Grains" description="A held, glassy freeze that repitches to the keys"
          tags="texture freeze midi" />
</patch>
```

- `name` is PatchBase's root attribute, which `toStateXml` writes and `fromStateXml` now also
  returns (an added out-parameter; today the plugins write "Init" and read nothing).
- `<preset>` is new, an element under `<patch>` that every existing reader ignores (`fromStateXml`
  reads `<params>`, `<engine>` and `<mod>` by name; PatchBase reads the root's attributes and
  `<params>`). Its attributes: `product` (`fx` or `instrument`: the plugin the preset is for),
  `author`, `description` and `tags` (space-separated, lowercase ASCII with hyphens, the vocabulary
  of §6.7). A missing `<preset>` means "not a preset": a session state. A missing attribute is
  empty. The attributes' text is escaped as `MEDI`'s paths are (`&` as `&#x26;`).
- **A preset's `MEDI`** names factory samples by their content-relative path, in `MEDI` version 2's
  content-relative reference, which sub-project 2 defines (§6.12). Until it merges, the presets this
  sub-project's first group builds have an empty `MEDI` (the FX's Live layers need none). No
  `AUDI`: a preset never embeds audio.
- **The engine seed in a preset is 0** and is ignored on load (D6, recommended): the instance keeps
  its own, so two tracks with one preset make different clouds (§23 #59). `gg-render --preset`
  uses its spec's seed.
- **Reading.** `gg/core/Preset.h` adds `readPreset(bytes, mode, PresetInfo &, SessionState &)`: the
  container decoded by `decodeState` (all or nothing, the 64 MiB cap, the nesting pre-scan: nothing
  new), plus the `<preset>` element and the name read from `PTCH`; false when there is no
  `<preset>` or its `product` isn't `mode`'s. `presetInfoOf(bytes)` reads the metadata alone, for
  discovery and the browser, without building values: it parses `PTCH` for the root's `name` and
  the `<preset>` element only, under the same pre-scan, so a crawl of a folder of presets costs no
  parameter work.
- **Writing.** `encodePreset(info, state)` is `encodeState` with the element and the name in; the
  browser's "Save as…" and `gg-render --pack-preset` use it. A saved user preset carries the
  instance's `MEDI` as a session would (an absolute path and the sample-folder-relative path; the
  content-relative reference too, once the loader records it): a user preset made from a factory
  sample finds it again on another machine with the content installed.
- **The product check** is strict for presets (an FX preset doesn't load into the instrument, and
  the browser lists one product's presets only; D9, recommended), where a session state still
  loads across products as INTENT §18 says.

### 6.2 Loading a preset (`preset-load`, INTENT §18's CLAP features)

Both plugins implement `clap.preset-load/2`. `from_location(kind, location, load_key)`:
- `CLAP_PRESET_DISCOVERY_LOCATION_FILE`: `location` is a `.ggpreset` path; the file is read on the
  main thread (at most `kMaxStateBytes`, as a state is), `readPreset` checks it for this product,
  and the preset is applied through **the one state-load path** `stateLoad` uses today, factored into
  `applyState(SessionState, LoadKind)`: the values, the routes and (for a session) the seed stored;
  active, offered to the audio thread in the same hand-offs a restore uses (the values with the
  routes in the FX, with the sample in the instrument; the `loadedState_` snapshot); inactive, taken
  by `activate()`; then `CLAP_PARAM_RESCAN_VALUES`. Nothing about the audio thread changes.

  > **Amended (2026-10-30):** every preset load (the browser's, the host's `preset-load` and INIT)
  > calls the host's `state.mark_dirty`, and a browser load announces itself through
  > `preset_load.loaded`. The edited mark compares the base values with the loaded preset's and
  > nothing more is stored for it: a session saved after editing a preset reopens with the
  > preset's name and **no** edited mark, since the loaded values are then the saved ones. The
  > brief mandates this; it is noted, not a defect.

  > **Amended 2026-10-30 (the director's decision, D-Q2):** the edited mark **is saved**. A
  > state saved while the loaded preset is edited carries `edited="1"` on the PTCH root (a
  > session has no `<preset>` element, and one would make it a preset); a state saved unedited
  > carries nothing, so its bytes are as before (pinned in `tests/core/preset_tests.cpp`). No new
  > chunk, no container or patch version change: a reader that doesn't know the attribute ignores
  > it. A reopened session shows the preset's name **with** the mark until the next preset load
  > or save; a preset file never carries it. This replaces the amendment above.
- `CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN` with `load_key` `init`: **INIT**, the product's defaults
  (every parameter's default, the factory routes, the sample kept per D5), built in memory, so INIT
  exists with no content installed. The browser's INIT is this. (D8, recommended: INIT is a fresh
  instance's sound, bit for bit.)
- **What a preset leaves alone** (D5, recommended): the instance's seed (D6) and, when the preset's
  `MEDI` names no sample, the sample already loaded (the instrument's, and in wave 2 any layer's),
  so a settings-only preset plays on the user's own material. A preset that names a sample loads
  it as a restore does (D8 of M1 sub-project 3: before `from_location` returns), through the
  content search path (§6.4). The live rings are never state, so an FX preset keeps what the ring
  holds. Freeze arms as a state load arms it (ruling M1-ac).
- On success the plugin calls the host's `preset_load.loaded()` with the same identity, so a
  wrapper or host moves its selector or list to the preset; on failure `on_error()` with an OS
  error where there is one and a message ("not a Giga Grains FX preset", "can't read …"). The
  browser's own loads go through the same function, so a VST3 host's program display follows a
  browser pick.
- **The header's preset name** is the loaded preset's `name`, kept by the plugin as
  `presetName_` and written back into the state's `name` on save, so a reopened session shows the
  preset it came from. A base value that differs from the loaded preset's marks it edited: the
  header shows the name with a trailing dot (`Glass Hold •`), computed on the editor's 30 Hz tick
  from the 218 values (cheap). INIT shows "Init", as today.
- The FX's `is_for_preset` on its remote-controls pages stays false; nothing changes there.

### 6.3 Discovery: the factory, the provider and the wrapper's bridges (INTENT §18)

- **The factory.** `getFactory(CLAP_PRESET_DISCOVERY_FACTORY_ID)` (and the `_COMPAT` id) returns a
  factory with one provider, "Giga Grains presets", vendor `sethbc`. It lives in `gg-plugin-core`,
  so the headless tests drive it through the entry point as a host would.
- **The provider's `init`** declares:
  - one filetype: `Giga Grains preset`, extension `ggpreset`;
  - the factory presets folder, `<Content>/Presets`, flagged `IS_FACTORY_CONTENT`, for every
    content root of §6.4 that exists;
  - the user presets folder (§6.4), flagged `IS_USER_CONTENT`, created on the first save;

    > **Amended (2026-10-30), Task 7 ruling:** the provider's `init` creates the user presets
    > folder (its errors ignored) and **always declares it**, so a host that caches the
    > declarations sees the first save; "created on the first save" gave a host nothing to cache.
    > A content root's `Presets` folder is declared only when it exists.
  - one soundpack, `org.sethbc.giga-grains.factory`, "Giga Grains Factory", with the content
    release's version from `content-version.txt`;
  - one plugin location per product for INIT: `CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN`, location
    null, `get_metadata` answering load key `init` for `add_plugin_id` of that product.
  The provider has no instance: it reads the settings file (the user content folder) on its
  `init`, main thread, through `GG_SETTINGS_FILE` as the plugins do, so the tests point it at a
  scratch file.

    > **Amended (2026-10-30):** INIT is **one** discovered preset, `begin_preset("Init", "init")`
    > carrying both products' plugin ids through two `add_plugin_id` calls, at one PLUGIN location.
    > A second `begin_preset` for the other product deadlocks clap-validator's crawl. The content
    > release's version rides in the soundpack's description (CLAP's soundpack struct has no
    > version field).

    > **Amended (2026-10-30), the group 1 fix wave:** the user's content folder ("Content
    > folder…", §6.4's root 2) is declared as **user** content, "Content folder presets", with no
    > soundpack: only `GG_CONTENT_DIR` and the installs are factory content (M6). The provider
    > reads the favorites file at `init` and ORs `IS_FAVORITE` into a favorite file preset's flags
    > and INIT's (§6.4, I3). Its metadata and the browser's catalog agree on what a host's crawl
    > lists: every `.ggpreset` under a declared folder, its subfolders included, the extension
    > matched in any case (I2).
- **`get_metadata`** for a file reads `presetInfoOf` and reports: `begin_preset(name, load_key
  null)`, `add_plugin_id(clap, <the product's id>)`, `set_soundpack_id` when the file sits under a
  factory folder, `set_flags` (factory or user content), `add_creator(author)`, `set_description`,
  `set_timestamps` from the file's modification time, and each tag as a feature (`add_feature`).
  A file that isn't a preset reports `on_error` and no preset; a folder containing it is not an
  error.
- **The wrapper's bridges, as pinned** (clap-wrapper `4e7afc3`, `v0.17.0-15`): `Clap::PresetIndex`
  crawls the provider's locations once per library and plugin id, on a background thread, sorted by
  (location kind, location, load key); the VST3 wrapper publishes the complete index as a program
  list on the root unit with a `kIsProgramChange` selector at the first free ID from `0xC00000`,
  grown in place in `onIdle` when the crawl completes; the AU wrapper publishes it as
  `kAudioUnitProperty_FactoryPresets` (waiting up to 1 s for the crawl) and loads one through
  `NewFactoryPresetSet`. Both use `preset-load`, and `preset_loaded` moves the selector. **Consequences
  this spec accepts:** file locations sort before plugin locations, so INIT is last in a host's list
  (the browser lists it first); within the factory folder the order is the file names' (§6.6); the
  standalone has no host list and uses the browser.
- **The selector parameter and the identities.** No clap-wrapper change, so rows 87, 88 and 218 are
  not re-triggered by a bump; but the VST3 gains a parameter and the AU gains factory presets, so
  the identity and AU state tests run as if one had happened (§10), and a macOS `[plugin]` test
  reads the built VST3's controller parameter list and pins the selector: present at `0xC00000`,
  `kIsProgramChange`, `stepCount` the factory count minus one, and nothing else above the
  `0xB00000` block.

### 6.4 The content search path (programme design §7.8)

`gg/media/ContentPaths.h` (gg-media, JUCE-free, so `gg-render` and the plugins share it; **header-only and the one new file under `media/`,** by the programme manager's note: nothing existing there, `media/CMakeLists.txt` included, is edited before sub-project 2 merges):
- **The content roots,** in order; a content-relative reference resolves to the first root where
  the file exists:
  1. `GG_CONTENT_DIR`, when set and not empty (tests, `gg-render`, a developer tree);
  2. the user content folder from the settings file (`contentFolder`, a new attribute of
     `settings.xml`, empty by default, set from the browser's "Content folder…"), where a user
     keeps a third-party or self-made pack in the same layout;
  3. the per-user install: macOS `~/Library/Application Support/sethbc/Giga Grains/`, Windows
     `%LOCALAPPDATA%\sethbc\Giga Grains\`, Linux `$XDG_DATA_HOME/sethbc/Giga Grains/` (default
     `~/.local/share/sethbc/Giga Grains/`, the tarball README's folder);
  4. the system install: macOS `/Library/Application Support/sethbc/Giga Grains/` (the PKG's),
     Windows `%ProgramData%\sethbc\Giga Grains\` (the installer's `{commonappdata}`), Linux each
     of `$XDG_DATA_DIRS` (default `/usr/local/share:/usr/share`) plus `sethbc/Giga Grains/`.

  > **Amended (2026-10-30), the group 1 fix wave:** roots 1, 3 and 4 are factory content; root 2,
  > the user's content folder, holds a pack of the user's own, which the browser lists as such
  > (never deletable there) and the provider declares as user content (M6). **The tests' seam**
  > (§10's "environment overrides for the system folders"): `GG_SYSTEM_CONTENT_DIR`, read only
  > when set and not empty, replaces roots 3 and 4 with that one folder, so a test never sees
  > the machine's installed content (I5). Nothing ships setting it.
- **The user presets folder:** beside the settings file, `<settings folder>/Presets/` (macOS
  `~/Library/Application Support/Giga Grains/Presets/`, Windows `%APPDATA%\Giga Grains\Presets\`,
  Linux `$XDG_DATA_HOME/giga-grains/presets/`): where "Save as…" writes and the provider's user
  location.
- **Favorites** are per machine: `<settings folder>/favorites.xml`, one `<preset>` per favorite by
  its discovery identity (kind, location, load key), read and written on the main thread through
  the settings file's temporary-file-and-rename (ruling R16). The provider reports `IS_FAVORITE`
  for them, so hosts that show favorites agree with the browser.

  > **Amended (2026-10-30):** the file is `<settings folder>/favorites.txt`, **one key per line**
  > (the preset's identity as `presetKey` writes it), not XML. It is read and written on the main
  > thread through `writeReplacing`, the temporary-file-and-rename the catalog's save shares, and
  > a key holding a line break is refused.
- **The loader's candidates** (INTENT §18, "Finding a missing sample") gain, after sub-project 2
  merges, one entry: when the entry carries a content-relative reference, the content roots in
  order, after the absolute path and before the same-name search. Match by size then SHA-256 as the
  others do. `gg-render` never searches; it resolves content references against `--content-dir`
  alone and refuses a preset that names content it can't find (exit 2).

### 6.5 The content repository, its release and the fetch (programme design §7.8)

**The repository** `https://github.com/sethbc/giga-grains-content` (public, empty today; the
director's local clone is `~/src/giga-grains-content`), populated by this sub-project:

```
LICENSE                     CC0-1.0, the full text
README.md                   what this is, how a release is made, the layout
PROVENANCE.md               one entry per shipped file (below)
Samples/<Category>/<name>.flac
tools/                      the gg-render specs and the shell script that make the generated signals
```

- **Two sources** (programme design decision 8; D2 recommended): downloads under CC0, with the URL,
  the author, the license statement's location, the downloaded file's SHA-256 and the conversion
  command recorded before a file is admitted; and signals our own tools generate, recorded as the
  `gg-render` spec and command line that made them. **The download source proposed is the
  Versilian Community Sample Library (VCSL)**, CC0-1.0 on GitHub (`sgossner/VCSL`), which ships
  sustained and plucked instruments, mallets, voices and percussion in WAV at a pinned commit, so a
  file is fetched by raw URL at a commit SHA and its hash recorded. Freesound's CC0 pool needs an
  account for downloads, so it is not used by the agent; the director may add files from it by
  hand, with the same record (D2's third option).
- **A provenance entry:** the shipped path; the source (`download` or `generated`); for a
  download its URL (with the commit or version), author, license (`CC0-1.0`) and where the
  statement is, the download's SHA-256, and the command that trimmed, normalized and encoded it;
  for a generated file the spec and command; and the shipped file's SHA-256, size, rate, channels
  and length. A file without a CC0 statement is never admitted.
- **The files:** FLAC (lossless; dr_flac decodes it, so a `MEDI` fingerprint and a render are
  exact), 48 kHz or the source's rate, mono or stereo as the source, peak-normalized to −1 dBFS,
  trimmed to 2–20 s, encoded with `flac -8` (the command recorded). Budget 20–50 MB in all (INTENT
  §12); the plan's target is 30 to 40 files, about 30 MB. Chosen because they granulate well:
  sustained tones (bowed and blown instruments, voices, bowls), textures (bowed metal, breath,
  rain-like noise beds), a few plucked and struck sounds for the playable presets, and generated
  sweeps, noise beds and drones rendered by our own engine from those (the resample promise of
  INTENT §4, offline).
- **The release:** a tag `v1` with the asset `giga-grains-content-v1.zip` whose top level is the
  layout above (`Samples/`, `PROVENANCE.md`, `LICENSE`, `README.md`). A later release is `v2`,
  never a moved tag.

  > **Pending the director (2026-10-30):** the content v1 as released has **no Voices category**
  > (VCSL has none): its 37 files are Strings, Winds, Mallets, Plucked, Textures and the nine
  > generated. §11's "voice-and-strings phrase" therefore has no voice to play. The proposal is
  > option 1: a strings-and-mallets phrase from v1, and the `voice` tag goes unused until a
  > content release has a voice. Not decided; group 2 waits on the answer.
- **The pin** in this repository, `content/pin.txt`: the tag, the asset URL and the zip's SHA-256.
  `scripts/fetch-content.sh <dir>` downloads the asset to `<dir>/giga-grains-content-<tag>.zip`,
  checks the SHA-256 (refusing a mismatch), unpacks it to `<dir>/content/` and writes
  `<dir>/content/.gg-pin`; a `<dir>` already at the pin costs no network. CI caches `<dir>` keyed
  by `content/pin.txt` and the script, as the SDK cache is, saved on `main` alone. CMake's
  `gg-content` target runs the script into `build/<preset>/content-cache/` unless `GG_CONTENT_DIR`
  names an existing folder (CI passes the cache). The first `ctest` or `gg-products` of a tree
  therefore fetches once; `asan` and `rtsan` download nothing at configure, as today, and their
  content fixture fetches the same zip (§10).
- **This repository's `content/`:** `pin.txt`, `presets/Giga Grains/*.json` and
  `presets/Giga Grains FX/*.json` (the preset sources, §6.6), and `README.md`. No audio.

### 6.6 The factory presets and their build (programme design §6.5; D1, D7)

- **A preset source** is JSON in `gg-render`'s vocabulary:

  ```json
  {"product":"fx","name":"Glass Hold","author":"Giga Grains",
   "description":"A held, glassy freeze that repitches to the keys",
   "tags":["texture","freeze","midi"],
   "params":{"Size":-2.0,"Density":4.0,"B Source":1,"Layer B On":1,"Reverb Amount":0.35},
   "mod":[{"slot":13,"source":"LFO 1","target":"Position","targetLayer":"all","depth":0.1,
           "curve":"Bipolar"}],
   "samples":{"A":"Samples/Voices/alto-ah.flac"}}
  ```

  `params`, `mod` and `samples` are parsed by the render spec's own parser (`RenderSpec.cpp`), so a
  name the product displays is the only way to address a parameter, a wrong name fails the build,
  and a preset's `samples` are content-relative paths (resolved by `--content-dir` for a render,
  recorded in `MEDI` for the file). A parameter the source doesn't name takes its default; the
  routes are the factory twelve plus the source's.
- **The packer:** `gg-render --pack-preset <source.json> -o <out.ggpreset> [--content-dir DIR]`
  writes the container (§6.1). With `--content-dir` it checks that every sample exists and fills
  the entry's fingerprint, format, channels, rate and frames from the file (sub-project 2's
  content-relative reference; §6.12); without it, a source naming samples is an error. CMake's
  `gg-presets` target packs every source under `content/presets/` into
  `build/<preset>/content/Presets/<Product>/`, where `gg-products` stages it with the fetched
  samples as the Content component.
- **The file names fix the order** (INTENT §18: factory preset order freezes at 1.0.0): each
  source and its `.ggpreset` is `NN Name.json`, `NN` two digits from `01`, unique per product;
  clap-wrapper sorts file locations by path, so a host's program numbers follow `NN`. A test pins
  the names and their count, and a renumbering before 1.0.0 is a deliberate commit.
- **The floor and the split** (D1, recommended): **48 presets, 24 per product;** by focus, 16
  textures, 12 playable, 8 rhythmic (the Step sequencer and the transport's beat and bar phases;
  M4's sync scheduler isn't needed to be rhythmic), 12 extreme; **8 of the FX's are MIDI-driven**
  (note-freeze and note-repitch of the ring, §23 #32, #48), tagged `midi`, and voiced to sound
  without a note and to transform under one (D7, recommended). Every preset uses the mod matrix
  beyond the factory routes (at least one route of its own), and the instrument presets use the
  content's samples, most with two or more layers.
- **Voicing happens in the second group** (§6.12), on wave 2's parameters and samples; the first
  group ships INIT and two test presets per product under `tests/fixtures/presets/`, which the
  tests and the browser's scratch folders use.

### 6.7 Tags (programme design §11: "the tag vocabulary")

Lowercase ASCII with hyphens, stored in `<preset tags>` and reported as CLAP features. **Focus
tags**, one or more per preset: `texture`, `playable`, `rhythmic`, `extreme`. **Character tags:**
`pad`, `drone`, `freeze`, `smear`, `stretch`, `glitch`, `keys`, `pluck`, `percussive`, `voice`,
`strings`, `winds`, `metal`, `noise`, `dark`, `bright`, `wide`, `mono`, `reverb`, `dry`. **Use
tags:** `midi` (the FX's MIDI-driven presets), `live-input`, `sample`, `sidechain`. The browser's
filter shows the focus tags as four buttons and the rest as a list; a tag no factory preset uses
isn't shown. The director may strike or add words when the preset list is reviewed at the gate; the
vocabulary is a list in `gg/core/Preset.h`, and a source with a word outside it fails the build.

### 6.8 The browser (INTENT §17, "Presets and editing"; I25, I29)

Replaces the header's "Init" label in both products' editors. **Coordination with sub-project 6
(the programme manager, 2026-10-30):** the views sub-project keeps the label's component ID
`header.preset` and its bounds slot; this sub-project replaces that label's contents with the preset
strip and the browser's button, under the same ID and in the same slot, and moves nothing else in
the header. The slot is 90 px today; the strip fits it (the name elided in the middle) and grows
only if sub-project 6 widens the slot.
- **The header strip:** `‹` and `›` (previous and next in the browser's current list), the preset
  name (edited-dot per §6.2), which opens the browser. Keyboard: the strip's buttons take focus like
  the macros; `‹`/`›` on the arrow keys when focused.
- **The browser** is an overlay over the canvas (not a window: hosts and the standalone treat a
  plugin window differently; an overlay inside the editor is one code path), closed by its button,
  Escape or a pick:
  - a search field (substring match on name, description and tags, case-insensitive);
  - the four focus tags as toggles and the character and use tags as a wrapped list, AND-combined;
  - a Favorites toggle (only favorites), and a star on each row;
  - the list: INIT first, then the factory presets in their order (`NN`), then user presets, each
    row name, tags and a star; the loaded one highlighted; a pick loads it (one call into the
    plugin's `preset-load` path, §6.2) and keeps the browser open until Escape or its button, so a
    user can audition down the list with the arrow keys;
  - **Save as…:** a name field, the tag list to tick, an optional description; writes
    `<user presets>/<name>.ggpreset` (a name that would overwrite asks first) and reloads the list;
    "Delete" on a user preset, with a confirm; factory presets can't be deleted;
  - **Content folder…:** sets the settings' `contentFolder` (a folder chooser) and rescans;
  - a line at the bottom: the content release found ("Factory content v1 in
    /Library/Application Support/sethbc/Giga Grains") or "No factory content found: install it, or
    choose a Content folder".
- **The list is the plugin's,** built on the main thread by the browser's model
  (`PresetCatalog` in `plugin/`): it scans the same locations the provider declares, reads
  `presetInfoOf` per file, merges favorites, and rescans on open, after a save, and when a watched
  folder's modification time changes (polled at the editor's 30 Hz tick, cheap). It never waits on
  clap-wrapper's index. With 48 factory files a scan is a few milliseconds; a user folder of
  thousands is scanned on a background thread with the list filling in, the same shape as the
  loader's worker.

  > **Amended (2026-10-30):** there is no background worker. The catalog **caps a listing at 5,000
  > files per folder, applied before any file is read**, and the status line says how many more
  > were left out ("(N more files not listed)"). A folder past the cap is a limit the user is told
  > of, not a stall; a worker returns only if a real library needs more.
  >
  > **Amended (2026-10-30):** the **Mutate button** (`preset.mutate`) lives in the browser beside
  > "Save as…", not in the header strip, which keeps its 90 px `header.preset` slot and nothing
  > else (§6.9).
  >
  > **Amended (2026-10-30), the group 1 fix wave:**
  > - The catalog walks each folder's subfolders and matches `.ggpreset` in any case, sorted by
  >   the path relative to the folder in byte order, the 5,000 cap per folder applied before any
  >   read, as the hosts' crawl lists them (I2).
  > - The loaded row is the loaded **entry** (its catalog key), set by every load path and a
  >   save; a session, which names a preset but no file, is matched by name (I4).
  > - `‹`/`›` step through the list as it stands; the list is rescanned on opening, after a
  >   save, a delete and a content folder chosen, and when the 30 Hz poll sees a modification
  >   time change (a stat of the settings and favorites files and each watched folder; a change
  >   inside a subfolder alone shows at the next rescan). The strip's Left and Right arrow keys
  >   step when it has the focus, and the focus returns to it when the browser closes (M3, M4,
  >   T5).
  > - "Replace?" names the preset the existing file holds (two names can make one file), and
  >   Save as… returns a result (saved, exists, failed) rather than a message to parse (M2, T5).
  > - The status line names a content folder as "Content folder: <folder>", apart from the
  >   factory content's line (M6).
  > - The browser and the Mutate panel are **opaque** over the canvas and the macro row: nothing
  >   of the Perform view shows through (the director's screenshots, UI-1).
- **The editor's context** gains the calls the browser needs (`presetCatalog`, `loadPreset`,
  `savePreset`, `deletePreset`, `setFavorite`, `setContentFolder`, `presetName`, `presetEdited`,
  `mutate`), each a `std::function` the plugin fills, main thread, as the loader strip's are.
- **Technology:** sst-jucegui's controls and the dark style sheet, as the Perform view's; a
  `juce::ListBox` for the rows; the files `ui/src/PresetBrowser.{h,cpp}` and
  `ui/src/PresetStrip.{h,cpp}`. `Editor.cpp` changes only where the label was, so sub-project 6's
  Perform work rebases over a small diff.

### 6.9 Mutate (INTENT §17; I28; D3, D4)

- **The model,** `gg/core/Mutate.h`, pure and allocation-free: `mutate(const ParamValues &in,
  const LockSet &locks, float amount, uint64_t seed, uint32_t counter, Mode mode, ParamValues
  &out)`. For each unlocked slot it draws `u` from the counter RNG keyed (seed, 0, 0,
  `Field::Mutate`, counter × kParamCount + slot) and moves the value in the parameter's **CLAP
  domain** by the chosen law, then clamps to the range (`paramMeta`):
  - **continuous parameters,** relative jitter (D4, recommended): `v' = v + amount × range × (2u −
    1)`; a log-domain parameter is already its log value, so Size moves in octaves and Density in
    octaves as the macros do;
  - **stepped parameters** (Distribution's shape aside, these are enumerations and switches): a
    new step is drawn with probability `amount`, uniformly among the steps, else unchanged;
  - **the sixteen Step values** of a layer count as one parameter for the lock and draw per step.
  - *The alternative law* (D4, option 2): `v' = v + amount × (r − v)` with `r` a uniform draw over
    the range, a blend toward a fresh random point; at amount 1 the result is independent of where
    it started.
- **Locks** are a bitset over the 218 slots, kept by the plugin for its life (not state; a session
  reopens with the defaults). **Locked by default** (D3, recommended, "the structure stays, the
  sound moves"): Mix, Level and Level B to D, Layer A to D On, every Source, every Freeze, Macro
  Freeze and the seven other macros, Morph X and Y, Reverb Amount, Dry to Reverb, Step Length, and
  every parameter of a layer that is off. Everything else is unlocked: each on layer's Position,
  Size, Density, Distribution, Pitch, Spray, Stereo, Texture, Feedback (within its 0–0.95), Diffusion,
  Pitch Spread and Size Spread; the LFOs', envelopes', randoms' and Step's rates, shapes, smooths and
  steps; Space's Decay, Size, Damping, Pre-delay and Modulation; and wave 2's sound-shaping
  parameters by section once they exist (the plan lists them at the rebase). *Alternatives:* D3
  option 2 unlocks layer A's Cloud page alone (Position to Freeze, Texture, Diffusion); option 3
  unlocks everything but the macros.
- **The panel** (`ui/src/MutatePanel.{h,cpp}`), opened from a "Mutate" button beside the preset
  strip: an Amount knob (0–100%, default 15%), a **Mutate** button, and the lock list grouped by
  section with a lock toggle per row (and per layer section a "lock all" / "unlock all"). Each press
  applies one mutation: the plugin computes `out` on the main thread, then sends every changed slot
  as a host gesture (begin, value, end) through the editor queue, in batches small enough for its
  1,024 entries (the builder sizes the batch or grows the queue; a mutation touching 150 slots is
  450 entries), so hosts record it as automation and sub-project 6's undo will see ordinary
  gestures. The counter increments per press, so successive presses differ and a session's seed
  with the same presses reproduces the same walk; the counter isn't saved.
- Mutate never touches the mod routes, the sample, the seed or the locks; it is a parameter move.

> **Amended (2026-10-30), Mutate as built (Tasks 2, 10 and 10b):**
> - **The RNG key** is layer and stream `0xFFFFFFFF` with `Field::PositionJitter` and index
>   `counter × kParamCount + slot`; the stepped parameters' second draw (which step) uses stream
>   `0xFFFFFFFE`. No new `rng::Field`: this replaces `Field::Mutate` with (seed, 0, 0, …).
> - **Locks:** "Reset locks" restores the **structural** locks only (33 slots). The off-layer
>   locks are computed at each press from the values, so a layer switched on later is mutable at
>   once. In the panel a layer's 16 Step values are one row, locked together; the model's
>   `LockSet` stays per slot.
> - **The button** (`preset.mutate`) lives in the preset browser beside "Save as…", not beside the
>   strip: the strip keeps the 90 px `header.preset` slot (sub-project 6 owns the header). The
>   panel (`preset.mutatePanel`) opens from it.
> - **Sending the values.** The changed slots go to the host as begin/value/end gestures through
>   the UI queue in batches of at most 100 slots (300 entries, whole triplets), coalesced per
>   slot, each batch sent only after a host drain has begun since the previous one (the drain
>   witness; at most 600 of the queue's 1,024 entries wait at once). Any state or preset load
>   discards what waits, and a user's gesture on a slot still waiting wins. Nothing on the audio
>   thread changes. The same sender is exposed as `GigaGrainsPlugin::sendValues` and
>   `EditorContext::sendValues`, for any caller restoring many values at once; sub-project 6's
>   whole-state undo adopts it after both merge.

> **Amended 2026-10-30 (the director's decision, D-Q3):** **Mutate never moves Reverb Decay into
> infinity.** If Decay is below `kDecayInfinite` (the start of the ∞ zone), a press's new Decay
> is capped at the float just below it; if Decay is already in the zone, the press moves it by
> its usual law, into or out of the zone. The cap follows the existing draw (no new draw, no new
> `rng::Field`), in `gg/core/Mutate.h`, so `gg-render` and the plugin agree.
>
> **Amended (2026-10-30), the group 1 fix wave:** a preset or state load also drops the editor's
> values already queued (Mutate's sent batches, `sendValues`', the user's own): each queued
> change carries a load epoch, and the audio thread drops a value queued before the last load it
> applied, an integer compare (I1); gesture begins and ends still go to the host. A host that
> calls `on_main_thread` without draining is asked again only after a drain (M5). In the panel,
> each layer's "Lock all" and "Unlock all" have a row of their own under its heading, clear of
> the scroll bar (UI-2).

### 6.10 `gg-render --preset` (INTENT §19; I27)

- **Two spellings of one thing:** a spec key `"preset": "<path>"` (instrument and FX modes;
  relative to the spec's folder or `--media-dir`, like a file path) and the option `--preset
  <path>`, which sets or overrides the key. `--content-dir DIR` resolves the preset's
  content-relative samples (required when the preset names any; otherwise exit 2).
- **Order of application:** the product check (an FX preset in an instrument spec is bad input,
  exit 2), then the preset's values and routes replace the defaults and the factory routes, then
  the spec's `params`, `mod` and `samples` on top (a spec may override a preset's Position, say,
  or give layer B another sample), then `automation`, `notes`, `controls` and `transport` as today.
  The spec's `seed` is the render's seed (D6).
- **Bit identity with the plugin:** the preset's values reach `Processor::setBases`, its routes
  `setRoutes`, its samples `setSourceNow` through the same `gg-media` load-and-convert the loader
  uses, so a render of the FX at block N over an input equals the headless plugin's `process` after
  `preset-load` of the same file with the same input, sample for sample; a test does exactly that
  (§10).
- `gg-render --pack-preset` (§6.6) is the third mode beside `--write-media`; the usage line and
  the exit statuses are unchanged in meaning (2 for a bad source, a missing sample under
  `--content-dir`, or a bad name).

### 6.11 The installers' Content component (release-pipeline spec §6.9)

- **Staging:** `gg-products` gains `content/`: `Presets/<Product>/NN Name.ggpreset` from
  `gg-presets`, `Samples/` and `PROVENANCE.md`, `LICENSE` (as `LICENSE-CC0-1.0.txt`) and
  `README.md` from the fetched release, and `content-version.txt` (`giga-grains-content <tag>
  <zip sha256>`). It depends on `gg-content` and `gg-presets`; with `GG_CONTENT_DIR` set to a
  folder without the pinned release it fails loudly rather than ship a partial component.
- **macOS:** the reserved `org.sethbc.giga-grains.content.pkg` choice in `distribution.xml.in` is
  enabled, its component built by `make-installer.sh --content <dir>` with the install location
  `/Library/Application Support/sethbc/Giga Grains`; on by default, deselectable.
- **Windows:** the `.iss`'s reserved `content` component and its `[Files]` line, with
  `/DContent=<dir>`, to `{commonappdata}\sethbc\Giga Grains`; the zip carries `content/` beside
  the products and its readme says where to put it.
- **Linux:** the tarball carries `content/`; `README.md` says to copy it to
  `~/.local/share/sethbc/Giga Grains/` or `/usr/share/sethbc/Giga Grains/`.
- **The license set:** `CC0-1.0.txt` (from `installer/licenses/`, since no checkout holds it) joins
  the set on every platform; `check-license-set.cmake`'s expected list grows by one;
  `THIRD_PARTY_NOTICES.md` gains "Factory content", naming the content release, its license and a
  pointer to `PROVENANCE.md` (which the component carries), with the authors credited there. The
  installers' license panes are unchanged (the AGPL).
- **Tests** (`ctest -L installer`): `installer_content_staged` (the staged folder: both products'
  preset folders with the pinned count, every `samples` reference of every preset resolving to a
  file, every sample's SHA-256 matching `PROVENANCE.md`, the version file); the existing PKG, zip
  and tarball checks extended with the component's entries; `installer_license_set` with the CC0
  text.

> **Amended (2026-10-30), as built:** `gg-presets` names a preset by its whole file stem (`NN
> Name`, CMake's `NAME_WLE`), so a dotted name keeps its dot. `THIRD_PARTY_NOTICES.md`'s
> "Factory content" names the tag `content/pin.txt` pins rather than a version written in the
> notice. The checks are `installer_content_staged` (minimums of 0 presets and 0 `midi` in group
> 1, raised to 24 per product and 8 with group 2) and `installer_windows_iss_content` (it reads
> the `.iss`, so it runs on every OS), beside the extended PKG, zip and tarball tests and
> `installer_license_set`.

### 6.12 The two groups of work and the boundary with wave 2 (the brief; programme design §6)

**Group 1, now (needs wave 1 alone):** §6.1 (the element, reading and writing, `MEDI` empty),
§6.2, §6.3, §6.4 (without the loader's candidate), §6.5 (the repository, the release `v1`, the
pin, the fetch, the CI cache, `gg-content`), §6.6's packer and INIT, §6.7, §6.8, §6.9, §6.10
(presets without samples), §6.11 (the component with samples and INIT; the preset count check at
its floor of 0 until group 2), the tests of §10 that need no wave-2 code, the pack's README
skeleton, INTENT and CLAUDE.md for what is built.

**Group 2, after the programme manager says wave 2 has merged and this branch is rebased:**
- adopt sub-project 2's `MEDI` version 2 content-relative reference in `encodePreset`, the packer
  and `readPreset` (one attribute name; nothing of 2's is copied), and add the content roots to the
  loader's candidate list (§6.4);
- voice the 48 presets on the full parameter list (§6.6), their golden renders and the pack
  (§10, §11), Mutate's default lock list extended to wave 2's sections (§6.9), the preset count
  pinned at 48 and the `midi` count at 8;
- the final review and fix wave, the hash claim, the identity and AU runs, `BUILD READY`.

If wave 2's reference has a shape this design can't take (a reference that isn't a path under a
content root), this sub-project stops at that boundary and says so in its report.

## 7. Determinism and real-time safety

- Nothing in this sub-project runs on the audio thread except what already does: a preset load
  reaches it as a state load does, through the existing hand-offs; the `[plugin][rt]` tests cover
  `process` and `params.flush` after a `preset-load` as after a `stateLoad`.
- Mutate is main-thread arithmetic on a copy of the values; its randomness is the counter RNG
  keyed by the instance's seed, so a session's seed and a count of presses reproduce it.
- Discovery, the catalog scan, favorites and the settings are main thread, with the file writes
  through the temporary-file-and-rename path (R16). (*Amended 2026-10-30:* there is no catalog
  worker; §6.8's per-folder cap bounds a scan.)
- `gg-render --preset` changes no render path: it fills the same `setBases`, `setRoutes` and
  `setSourceNow` the spec's keys fill, so block-size invariance and the event logs hold as before.
- Every existing golden renders bit-identically: no DSP file is touched. `scripts/update-goldens.sh`
  before and after each group, with the hash list in the commit.

## 8. Decisions for the director

Each a forced choice; the recommended option is first, with one line on what each costs. Nothing
engineering-only is here (that is §9).

**The director's answers, 2026-10-30, relayed by the programme manager: every recommended option.**
D1 option 1, D2 option 1, D3 option 1, D4 option 1, D5 option 1, D6 option 1, D7 option 1, D8
option 1, D9 option 1. Each is marked below and applied in §6.

**D1. How many factory presets, and how they are spread** (§6.6). The floor is 40 and 8 MIDI-driven **Decided: option 1.**
FX presets.
1. **48: 24 per product, 8 of the FX's MIDI-driven; by focus 16 textures, 12 playable, 8 rhythmic,
   12 extreme.** *Cost:* 48 goldens and 48 pack renders; the rhythmic presets use the Step
   sequencer and the transport phases, not M4's sync, so they are "grooving" rather than locked.
2. *40: 20 per product, 8 MIDI-driven; 14 / 10 / 6 / 10.* *Cost:* the floor exactly; v1.0's
   "about 100" is further away.
3. *64: 32 per product, 12 MIDI-driven; 20 / 16 / 12 / 16.* *Cost:* a week's more voicing and
   listening for the close; a longer host list.

**D2. Where the CC0 samples come from** (§6.5; programme design decision 8 fixed "downloads with
recorded provenance and our own tools"). **Decided: option 1.**
1. **The Versilian Community Sample Library (CC0-1.0, GitHub, pinned by commit) for the recorded
   material, plus signals our tools generate.** *Cost:* one library's character across the
   recorded half (orchestral and folk instruments, voices, percussion); no field recordings.
2. *Generated signals only.* *Cost:* no acoustic material at all; the instrument presets sound
   synthetic, against INTENT §6's references.
3. *Option 1 plus files the director downloads by hand from Freesound's CC0 pool* (the agent can't
   log in), each handed over with its URL and author. *Cost:* the director's time before the
   second group can voice; the same provenance record applies.

**D3. What Mutate touches by default** (§6.9). **Decided: option 1.**
1. **The structure stays locked, the sound moves:** Mix, the Levels, Layer On, Source, Freeze, the
   macros, Morph X/Y, Reverb Amount, Dry to Reverb, Step Length and every off layer's parameters
   are locked; every other parameter of the on layers and Space's five voiced ones move. *Cost:*
   a mutation can move Position, Pitch and the envelopes together, so a small amount still changes
   a playable preset's feel; the user locks more.
2. *Layer A's Cloud page alone* (Position, Size, Density, Distribution, Pitch, Spray, Stereo,
   Texture, Diffusion). *Cost:* layers B to D, the modulators and Space never mutate unless
   unlocked by hand.
3. *Everything but the macros.* *Cost:* a press can switch layers and sources on and off and
   silence a preset with Mix; the amount knob is the only guard.

**D4. Mutate's law** (§6.9). **Decided: option 1.**
1. **Relative jitter:** each unlocked value moves by up to `amount × range` either way, in its CLAP
   domain. *Cost:* repeated presses random-walk; a large amount is needed to leave a preset's
   neighbourhood.
2. *Blend toward a fresh random point:* `v + amount × (random − v)`. *Cost:* small amounts drift
   every parameter toward the range's middle on average; at 100% the preset is forgotten.

**D5. What a preset without a sample does to the sample you have** (§6.2). **Decided: option 1.**
1. **Keeps it:** a settings-only preset plays on your own material; a preset that names a sample
   loads it. *Cost:* INIT doesn't unload a sample (a "Clear sample" stays the loader strip's job).
2. *Sets the state exactly:* no sample in the preset, no sample after it. *Cost:* every factory
   instrument preset must carry a sample, and a user's sample is gone after any preset pick.

**D6. Whose seed a loaded preset uses** (§6.1, §6.2). **Decided: option 1.**
1. **The instance keeps its seed;** the preset's is ignored. *Cost:* the same preset on two tracks
   makes two different clouds (as two fresh instances do today); a preset's render in the pack is
   one seed's.
2. *The preset's seed is taken,* as a session restore takes its seed. *Cost:* every instance of a
   preset is identical until a parameter moves, and a saved user preset pins the seed it was made
   with.

**D7. How the eight MIDI-driven FX presets rest** (§6.6). **Decided: option 1.**
1. **They sound without a note and transform under one:** the input is granulated as the preset
   says, and a held note freezes the ring and repitches the cloud to the keys. *Cost:* a user who
   never routes MIDI hears an ordinary preset and may not find the feature; the description and
   the `midi` tag say so.
2. *Dry until played:* Velocity routes to Mix, so the effect is heard only while notes are held.
   *Cost:* without MIDI routed the preset seems to do nothing.

**D8. What INIT is** (§6.2). **Decided: option 1.**
1. **A fresh instance, bit for bit:** every default, the factory routes, nothing else. *Cost:* INIT
   is dry and plain; the first good sound is one preset away.
2. *A voiced Init* (a little Diffusion and Reverb Amount, a slow LFO on Position). *Cost:* "Init"
   no longer means the defaults; `fx-sine` and the other default-valued goldens would describe a
   state no button reaches.

**D9. Which presets a product's browser lists** (§6.1, §6.8). **Decided: option 1.**
1. **Its own:** the FX lists FX presets, the instrument instrument presets; a preset file carries
   its product. *Cost:* a sound made in one product can't be picked from the other's list (a
   session state still loads across products, as today).
2. *Both, with a product filter.* *Cost:* a list twice as long, and an FX preset loaded into the
   instrument sets a Live source on every layer the FX had, which sounds like silence until a
   sample is loaded.

## 9. My calls

Engineering choices, each with its reason.

- **Preset metadata rides in `PTCH` as `<preset>`** (§5, §6.1): every existing reader ignores it,
  no chunk and no version bump are needed, and the state-change ownership rules stay whole.
- **The sources are JSON in this repository, compiled at build time** (§5, §6.6): reviewed like
  code, bound to the parameter names, and present for the goldens without the network. The content
  release holds samples and provenance only.
- **`gg-render` packs presets** (`--pack-preset`) rather than a new tool: the parser the source
  format needs is `RenderSpec.cpp`'s, and gg-render already has a non-render mode (`--write-media`).
- **FLAC in the content release:** lossless, decoded by dr_flac already in gg-media, about half the
  size of WAV inside a 20–50 MB budget; encoded by the `flac` command when the release is made, the
  command recorded in the provenance. No encoder enters the build.
- **The user content folder is one extra root in the same layout,** not a second kind of
  location: one resolver, one scan, and a self-made pack works like the factory's.
- **The browser builds its own catalog** rather than reading clap-wrapper's index: the CLAP build
  has no wrapper, the index is private to the wrapper, and the browser must list user saves at once.
- **INIT is a plugin location, not a file:** it must exist with no content installed; the cost is
  its last place in the wrapper's lists (file kind sorts first), accepted and recorded.
- **Favorites in a file of their own, by discovery identity:** the settings XML stays a flat
  attribute set, and a favorite survives a renamed sample folder.
- **The file names carry a two-digit prefix:** the only ordering clap-wrapper gives a folder is
  its paths', and the order freezes at 1.0.0, so it is made explicit now and pinned by a test.
- **Mutate's randomness is the counter RNG keyed by the instance's seed and a press counter:** the
  project's one RNG, reproducible per session, no `std::` distribution; the counter isn't state
  because a walk's history isn't worth a chunk.
- **Mutate applies through host gestures,** one per changed slot, in batches that fit the editor
  queue: hosts record it, sub-project 6's undo sees ordinary gestures, and the audio thread learns
  of it exactly as of a knob move.
- **Preset goldens live in `tests/golden/presets/`** with their own glob, outside the
  click-injection table (§10): 48 cases × 16 injected renders would add minutes to every test run
  for a detector already pinned on the engineered cases; each preset golden's expected click score
  is instead asserted under the limit once, by a cheap test that reads the expected JSON.
- **The content fixture is a test, not a configure step** (§6.5, §10): `golden_media`'s shape,
  idempotent and offline once fetched; the sanitizer presets keep downloading nothing at configure.
- **The settings file gains one attribute** (`contentFolder`), additive: an older build ignores it,
  a newer one reads an empty one as unset.
- **No new `gg-bench` case** (programme design §7.6 asks each spec to name its cases): nothing here
  runs on the audio thread that didn't; the preset scan and Mutate are main-thread and bounded by
  tests (a 48-file scan under 50 ms in the `dev` build), not benchmarked. `engine-sample` is run
  from `release` at the end for the regression check the brief asks for. (*Amended 2026-10-30:*
  the 48-file scan test arrives with Task 17, the factory presets, which are its 48 files.)
- **The VST3 selector is pinned by a raw-vtable test**, as `gg-vst3-class-ids` pins the class IDs:
  no SDK headers, no hosting module, and it fails the day a wrapper bump moves the selector.

## 10. Verification

**Unit tests** (Catch2; labels in brackets; every new case parallel-safe, writing under
`GG_TEST_OUTPUT_DIR` in a name of its own):
- `[core][preset]`: `encodePreset` / `readPreset` round trip (name, product, author, description,
  tags; values, routes); the element is ignored by `fromStateXml` and by a literal M1 (version-1)
  reader path; a state without `<preset>` isn't a preset; a wrong product fails; the tag vocabulary
  (every word in the list, an unknown word refused); `presetInfoOf` reads no values; the 64 MiB cap
  and the nesting pre-scan still apply; INIT's bytes equal `encodeState` of a fresh instance's
  values and the factory routes.
- `[core][mutate]`: determinism (same seed, counter, locks and amount give the same bytes);
  locked slots never move; amount 0 moves nothing; every result within range; stepped parameters
  take only steps; the law's distribution over 10,000 draws (mean near the start value for D4
  option 1; coverage of the range); a layer off locks its parameters by default; the default lock
  list pinned.
- `[media][content]`: the roots in order per platform (through environment overrides for the
  system folders in tests); `GG_CONTENT_DIR` first; the settings' `contentFolder` second; a
  content-relative reference resolving to the first root that has the file; the user presets and
  favorites paths; favorites round trip.
- `[plugin][preset]`: through the factory, both products: `preset-load` from a file (values,
  routes applied; the host's `loaded` called with the identity; `on_error` on a bad file, a missing
  file, a wrong product); INIT by load key; a preset without a sample keeps the instrument's sample
  (D5); the seed kept (D6); the header name and the edited mark; a save-as round trip to a scratch
  user folder; the catalog's list (INIT first, factory by `NN`, user after; search, tag and favorite
  filters); the discovery factory's provider declares the filetype, the locations and the soundpack
  under a stub indexer, and `get_metadata` reports each fixture preset's fields; a crawl of 48
  fixtures under 50 ms in `dev` (*amended 2026-10-30:* with Task 17, whose 48 factory presets are
  the fixtures).
- `[plugin][rt]`: the tripwire on `process` and `params.flush` after a `preset-load` while active.
- `[render]`: `--preset` and the `"preset"` key (one wins as specified), `--content-dir` required
  when the preset names content, the product check, the override order; `--pack-preset`'s
  errors (a bad name, a missing sample, a word outside the vocabulary) and its bytes equal to
  `encodePreset` of the parsed source.
- **Bit identity** (`[plugin][render]`): the FX, headless, after `preset-load` of a fixture preset,
  fed 4 s of the generated sweep at block 256, against `gg-render --preset` of the same spec:
  `std::memcmp` equal; the same for the instrument with a fixture preset naming a sample under
  `GG_CONTENT_DIR` (second group).
- **The VST3 selector and the AU presets** (macOS, with the editor, label `plugin`):
  `vst3_preset_selector` (§6.3) and `gg-au-state-tests` extended: `kAudioUnitProperty_FactoryPresets`
  lists the factory count with their names, `PresentPreset` of one loads it (a changed value read
  back), and the pinned row-87 faults re-checked (the name after `PresentPreset`; see §12).
  `identity_*` and `codesign_*` run unchanged.
- `[build]`: the preset sources' file names (`NN Name.json`, unique `NN`, both products), the
  count at its pinned value (48 at the end; the `midi` tag on 8 FX presets), each source parsing.

**Goldens.**
- **The content fixture:** `content_fixture` (label `golden`, `FIXTURES_SETUP content`) runs
  `scripts/fetch-content.sh build/<preset>/content-cache` unless `GG_CONTENT_DIR` is set; offline
  it passes when the pin is already there and fails with the script's message otherwise. CI's legs
  restore the content cache before it.
- **The preset goldens,** one per factory preset, `tests/golden/presets/cases/<product>-<NN-slug>.json`
  (expected files beside them in `expected/`), each a 6 s render with its own glob in
  `tests/CMakeLists.txt` (`golden_preset_<case>`, labels `golden` and `presets`, requiring
  `golden_media` and `content`), compared by metrics and event log as the 36 are: the FX presets
  over the generated `loop-48k.wav` or sweep (two notes for the `midi` ones), the instrument
  presets with a two-note phrase then a chord. `scripts/update-goldens.sh` renders them too (with
  `--content-dir`) and lists their hashes beside the 36. A cheap `[golden][presets]` test asserts
  every preset golden's expected click score is under the limit, in place of the click-injection
  table.
- **The 36 existing goldens keep their WAV SHA-256s** through both groups: the hash list in each
  commit that touches `tools/`, `core/` or `plugin/`.
- `golden_tamper_*` are unchanged.

**Installers:** `ctest -L installer` with the Content component (§6.11): staged content, the
PKG/DMG, zip and tarball entries, the license set with `CC0-1.0.txt`.

**Validators and CI:** clap-validator (which exercises preset discovery and `preset-load` when a
plugin offers them), pluginval, auval and Steinberg's validator on `release`; CI green on every leg
through the draft PR, with the content cache step added to `ci.yml` beside the SDK cache; the
`gg-bench --quick` smoke unchanged, and a full `engine-sample` run from `release` at the end.

> **Amended (2026-10-30):** CI's Windows leg runs clap-validator with `--exclude
> 'preset-discovery-(crawl|descriptor-consistency|load)'`. clap-validator 0.4.1-127 demands
> `/C:\...` for a FILE location, a form no Windows file function opens, while CLAP's
> `preset-discovery.h` asks for a native path; so a FILE location is a **native OS path on every
> platform**, and the three checks are excluded on Windows alone (carry-over row 233). macOS and
> Linux run all of them.

**The pre-commit gate:** `scripts/check.sh` over `dev,asan,rtsan`, its summary in the PR.

## 11. The listening pack: `docs/listening/m2-presets/`

Rendered for the record in the second group, heard at sub-project 7 (programme design decision 1).
A `README.md` in the M2 packs' form (how to render, with `--content-dir`; a table of files with
length, what it is and what to listen for; the host checklist) and `specs/`:

| File | What | Listen for |
|---|---|---|
| `fx-NN-<slug>.wav` × 24 | each FX preset over the content's voice-and-strings phrase as input, 8 s; the eight `midi` ones with two held notes from 3 s | the preset's character; the MIDI ones freezing and repitching on the notes (D7) |
| `inst-NN-<slug>.wav` × 24 | each instrument preset, a two-note phrase then a chord, 8 s | playable from note 1; the focus the tags promise |
| `init-fx.wav`, `init-inst.wav` | INIT in each product over the same material | the plain defaults (D8) |
| `mutate-ladder.wav` | one texture preset, then Mutate at 10%, 30% and 60% (seed fixed, counter 1 to 3), 4 s each | variations that stay in the family at 10%, leave it by 60% (D3, D4) |

Fifty renders, deterministic on one platform. The README's host checklist for the close's sitting
in Live and Bitwig: install from the PKG and the EXE (and unpack the tarball) and confirm the
browser says "Factory content v1 in …"; the 24 presets per product in the browser, in order;
the VST3 program list and its selector following the browser in Live and Bitwig; the AU factory
presets in Live (and Logic, best effort); a `midi` preset with a MIDI track routed; Save as… and
a reopen; a favorite surviving a restart; Mutate recorded as automation; an M1 session reopening
unchanged with its name "Init".

## 12. The carry-over

**11 open rows at this sub-project's start** (`docs/m2-carry-over.md`: 51, 85, 87, 88, 144, 148,
184, 217, 218, 221, 232).

**Closed here:** none planned. The programme design's §8 places none in sub-project 5; its
upstream rows (85, 87, 88, 218, 232) close only with a bump, and this sub-project bumps nothing
(§6.3).

**Touched files with open rows:** `tests/plugin/au_state_tests.cpp` (row 87's pins: the factory
presets make `PresentPreset` do more in the wrapper; the pins are re-run and, if the wrapper now
retains a name after a factory preset load, row 87 is narrowed or closed with the commit);
`plugin/src/editor/EditorPlugin.cpp` (row 232, which waits on an sst-clap-helpers bump, not a
touch). No "when touched" row names a file this sub-project edits.

**Expected at the end: 11 open rows,** no new rows. Every minor the final review raises is fixed in
the fix wave unless blocked outside our control or genuinely large (the policy).

> **Amended (2026-10-30):** group 1 closed with **row 87's pins unmoved** (Task 12 re-ran them:
> the name after `PresentPreset` is still "Program", and the saved ClassInfo state carries the
> preset's values), so row 87 is not narrowed. Two rows were added during the build, both
> blocked outside this sub-project's work: **233** (clap-validator and Windows preset locations,
> §10) and **234** (a one-off extra grain on an off layer, x86_64 under Rosetta). The final
> count is for the close.

## 13. INTENT changes (Part II, with one §23 row in the same change)

- **§12** is Part I and unchanged; its "kept out of git history (via LFS or a release asset)" is
  met by the release asset, which §15 and §18 record.
- **§15:** the content repository, its release and `scripts/fetch-content.sh`, the CI content
  cache; clap-wrapper's preset bridges as pinned (the index, the selector, the AU presets) and the
  consequences accepted (INIT last; file order); `CC0-1.0.txt` in the license set and the notices'
  factory-content section.
- **§17:** "Presets and editing" marked as built: the browser (list, search, tags, favorites, INIT,
  save as, previous/next), the header's preset name and edited mark, Mutate with its law (D4),
  locks (D3) and amount; undo/redo left to sub-project 6.
- **§18, "Presets":** the file (`GGST`, `.ggpreset`, `<preset>` in `PTCH`, no `AUDI`, `MEDI`'s
  content-relative reference), what a load keeps (D5, D6), INIT (D8), the per-product rule (D9);
  the discovery provider's locations and soundpack; the content roots and the user presets folder;
  favorites; the settings' `contentFolder`; the selector parameter at `0xC00000` as now observed and
  pinned; "Frozen from 1.0.0: factory preset order" gains "the `NN` prefix of the file names".
- **§19:** `gg-render --preset` and `--pack-preset`; the preset goldens and the content fixture;
  the bit-identity test; the selector and AU preset tests.
- **§20:** the M2 row's "factory presets and CC0 samples, including MIDI-driven FX presets;
  preset discovery" marked built with the count (D1).
- **§22:** no row.
- **§23, the next free row at merge** (sub-project 3 merges before this one, so the number is
  taken then): this sub-project's director decisions (D1–D9, 2026-10-30), the rulings made during
  the build, and the §9 calls that amend Part II (the element in `PTCH`, the sources in this
  repository, INIT as a plugin location, the file-name order).

Part I is unchanged.

## 14. Risks

| Risk | Mitigation |
|---|---|
| Sub-project 2's content-relative reference arrives in a shape the packer can't fill (not a path under a root) | The boundary rule (§6.12): stop and report; the first group needs none of it |
| clap-wrapper's crawl or selector misbehaves in a host (a mid-crawl list, a reloaded preset after a project load) | The wrapper's own guards (adopt-after-setState, complete-crawl publication) read and relied on; the close's host checklist; nothing of ours depends on the selector |
| Row 87's pins move with factory presets present | Re-run in the first group's AU test extension; narrow or close the row with the commit |
| The content fetch makes the tests need the network | One fixture, cached in CI and idempotent locally; the 36 goldens and every unit test run without it; `check.sh` fetches once per tree |
| VCSL's files are large multi-samples, and trimming by hand is slow | A script in the content repository's `tools/` does the trim, normalize and encode from a list, recorded as the command; 30–40 files |
| 48 presets voiced in one group on a parameter list that just landed | Each is a JSON source; the goldens and the pack render in one command; the listening is the close's |
| The browser's overlay fights sub-project 6's Perform rework in `Editor.cpp` | The change there is the label's slot alone; the browser is its own files; 5 merges first |
| A huge user preset folder stalls the editor | The catalog's scan moves to a worker past a threshold, the list fills in |

## 15. Left to the builder

- The exact `<preset>` escaping helper's reuse from `MEDI`'s, and whether `presetInfoOf` parses
  with tinyxml or a bounded scan (both must obey the 64-depth pre-scan).
- The catalog's worker threshold and the browser's row layout.
- The Mutate gesture batch size versus growing the editor queue.
- The VCSL file list and the generated signals' specs (the second group's voicing), within §6.5's
  budget and categories.
- The raw-vtable path to the VST3 controller's parameter list (IEditController's layout), or, if
  it proves brittle, a grep of Steinberg's validator output for the program list as the fallback,
  recorded in the spec's amendments.
