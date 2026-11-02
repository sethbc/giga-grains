# Review B: the build, CI, release and process documents

**What I read:** this is the worktree `m2-close` at `0189dc9` (= `main`, 2026-11-02). I read
`CLAUDE.md`, all of it, and `.github/workflows/ci.yml`, all 1,304 lines. I also read
`.github/workflows/format.yml`, `CMakePresets.json`, the top-level `CMakeLists.txt` and
`tests/CMakeLists.txt` (its discovery, labels, `RUN_SERIAL`, fixtures and timeouts), plus
`docs/releasing.md` and `README.md`.

I read these scripts: `scripts/check.sh`, `worktree-setup.sh`, `fetch-content.sh`,
`fetch-validators.sh`, `fetch-sdks.sh`, `update-goldens.sh` (its header) and `format.sh`. I also
read `tools/gg-bench/baselines/README.md` and the three baseline JSONs (models parsed).

From INTENT I read §15 (the toolchain), §16 (the CI and report-only bullets), §19 and §20. From
§23 I read rows #44, #47, #57 and #60 to #62, rows #70 and #71 (for the PR states), and rows #73
to #76.

I also read these sources:
- `installer/macos/distribution.xml.in`, `installer/macos/make-installer.sh` (the Content
  choice) and `installer/windows/giga-grains.iss`;
- the carry-over numbering preamble in `docs/m2-carry-over.md`;
- the pinned `prepare-for-juce` action, through `gh api`;
- `gh pr list` (merged).

I counted:
- `tests/golden/cases/*.json`: 60;
- `tests/golden/presets/cases/*.json`: 48 (24 fx, 24 inst);
- the `[slow]` cases: 10 Catch2 cases plus `snap_cli`, so 11;
- the `tests/sweep/` `TEST_CASE`s: 30;
- the gg-bench case names: 21, which at 3 block sizes give 63 configurations.

Findings: 15 DRIFT, 0 CONFLICT (one item is partly UNSURE, marked). B-8 and B-13 each give two
options, because the fix is a judgment call, but neither is two decisions disagreeing.

---

## B-1. "Where we are" leaves out sub-project 6, and §20 still says "PR 1 built"

- **Class:** DRIFT
- **Sources:**
  - `CLAUDE.md:23-44` lists sub-projects 1, 3, 2, 4 and 5 as built. It says nothing about
    sub-project 6, the Views. The seed is half right: sub-project 5 *is* there ("PRs #17 and
    #25", line 39-40). Sub-project 6 is the one missing.
  - `INTENT.md:3255`, §23 #71, is titled "M2 sub-project 6 (Views), PRs 1 and 2". `gh pr list`
    shows #18 "Views (PR 1)" merged on 2026-10-31 and #24 "PR 2" merged on 2026-11-02.
  - `INTENT.md:3124`, §20's M2 row, says: "**6 Views** (PR 1 built; §23 #71)".
- **Resolution:**
  - In `CLAUDE.md`, after line 41's sub-project 5 sentence, add one sentence: "Sub-project 6,
    Views, is built (INTENT §23 #71; PRs #18 and #24): the Perform, Edit, Mod and FX views,
    four lanes with a pitch axis, the layer strip and XY pad, the Mod view's route path, undo,
    the two themes, the About box, `gg-snap` and the GPU spike."
  - Also add #71 to line 23's citation list, which reads "(INTENT §20, §23 #61, #64)".
  - In §20 (`INTENT.md:3124`), change "PR 1 built" to "PRs 1 and 2 built, PRs #18 and #24,
    merged".

## B-2. README's status is dated 2026-10-29 and says "next comes M2"

- **Class:** DRIFT
- **Sources:**
  - `README.md:8-9` reads: "**Status (2026-10-29):** M0 … and M1 … are done, and so are the
    installers and the release pipeline; next comes M2."
  - Against that: §20 (`INTENT.md:3124`) says "M2 Giga (in progress since 2026-10-29)", and six
    of its seven sub-projects are merged (B-1).
- **Resolution:** Re-date the status line to the M2 close. Say that M2 is in progress, with
  sub-projects 1 to 6 built and the close (sub-project 7) under way. Keep the M5 public-release
  sentence as it is.

## B-3. README's build instructions lag the submodule command and #76's Windows toolchain

- **Class:** DRIFT
- **Sources:**
  - `README.md:19` says: "fetch the dependencies with `git submodule update --init`". Against
    it:
    - `CLAUDE.md:133` says "`git submodule update --init --recursive` (what CI and
      `worktree-setup.sh` check out …)";
    - `ci.yml:298` checks out with `submodules: recursive`.
  - `README.md:20-21` says: "on Windows, Visual Studio 2022 with ClangCL: the `windows-clangcl`
    preset". Against it:
    - §23 #76 (`INTENT.md:3260`) says the preset "is now **Ninja with `clang-cl`** … A local
      Windows build now runs from an x64 developer shell";
    - `CLAUDE.md:129-131` adds "from an 'x64 Native Tools' developer shell with Visual Studio's
      `VC\Tools\Llvm\x64\bin` first on the PATH";
    - `CMakePresets.json:66`'s displayName agrees.
  - `README.md:21` says: "The first configure downloads the VST3 and AudioUnit SDKs". Against
    it, `CLAUDE.md` ("Setup") says the first configure also downloads RtAudio, on Windows and on
    Linux without the PipeWire/JACK headers, and that the first `ctest` fetches the content.
- **Resolution:**
  - Change the command to `git submodule update --init --recursive`.
  - Replace the Windows clause with: Visual Studio 2022 (or its Build Tools) with the C++ x64
    tools and the C++ Clang component, from an x64 Native Tools shell, with
    `VC\Tools\Llvm\x64\bin` first on the PATH.
  - Mention RtAudio and the content fetch.
  - The per-OS package lists in the Appendix are the input for a fuller "Building from source"
    section.

## B-4. ci.yml's benchmark comment says "sub-project 6 makes it blocking"

- **Class:** DRIFT
- **Sources:**
  - `ci.yml:713-715` reads: "a regression (exit 3) becomes a warning and the step exits 0, so
    the artifact still uploads; sub-project 6 makes it blocking."
  - §23 #60 (`INTENT.md:3244`) says: "turns blocking at 1.0 … not in sub-project 6 as §19, §20
    and #44 said". §23 #61 moves it to 1.0.0, after M5.
  - The file's own header disagrees with the line: `ci.yml:14-17` says "report-only until 1.0.0
    … The gate turns blocking at 1.0.0, after M5".
- **Resolution:** At `ci.yml:715`, replace "sub-project 6 makes it blocking" with "it turns
  blocking at 1.0.0, after M5 (INTENT §23 #60, #61)".

## B-5. The baselines README says "Through M1" and "Sub-project 6 makes it blocking"

- **Class:** DRIFT
- **Sources:**
  - `tools/gg-bench/baselines/README.md:5-7` reads: "Through M1 the verdict is report-only (spec
    ruling 3) … Sub-project 6 makes it blocking, with the published §16 headline."
  - The same file's lines 28-29 say "(the whole gate is, until 1.0.0)".
  - §23 #60 and #61 put the blocking gate at 1.0.0, after M5. So do `CLAUDE.md:1089-1094` and
    §19 (`INTENT.md:3043-3046`).
- **Resolution:** Lines 5-7 should read: "The verdict is report-only until 1.0.0 (INTENT §23
  #60, #61): a regression is a warning, never a failed job. The gate turns blocking at 1.0.0,
  after M5, with the published §16 headline."

## B-6. CLAUDE.md's "seen so far" list for Windows leaves out the Xeon 8370C, which has a baseline entry

- **Class:** DRIFT
- **Sources:**
  - `CLAUDE.md:1077-1078` reads "(seen so far: … Windows on EPYC 7763, 9V45 and 9V74)". Three
    lines later, `CLAUDE.md:1081` lists "Windows' EPYC 7763, EPYC 9V45, Xeon 8370C and EPYC
    9V74".
  - `tools/gg-bench/baselines/windows-clangcl.json` holds an entry for "Intel(R) Xeon(R)
    Platinum 8370C CPU @ 2.80GHz (x86_64)", made from 1 run at `630d740` with 30 ratios.
  - `baselines/README.md:10-11` says "Windows on EPYC 7763, 9V45, 9V74 and Intel Xeon 8370C".
- **Resolution:** At `CLAUDE.md:1077-1078`, change the Windows list to "Windows on EPYC 7763,
  9V45 and 9V74 and Intel Xeon 8370C".

## B-7. ci.yml's header promises that "every later check runs", but `ctest` has no guard

- **Class:** DRIFT (the document disagrees with the configuration)
- **Sources:**
  - `ci.yml:22-23` reads: "Once the build succeeds, every later check runs even if an earlier one
    failed, so one run reports them all".
  - The *Test (unit, plugin and golden)* step at `ci.yml:612-618` has no `if:`, so it takes
    GitHub's default `success()`.
  - It comes right after *Linux products need no libstdc++ newer than Ubuntu 22.04's*
    (`ci.yml:555-591`), which can fail (`exit "$status"`).
  - So on the Linux leg, a libstdc++ violation skips all of `ctest`, including the cross-platform
    event-log gate.
  - Every other later step guards itself: the snapshot, Rosetta, bench, products and validator
    steps all carry `!cancelled() && steps.build.outcome == 'success'` or
    `steps.products.outcome == 'success'`.
- **Resolution:**
  - Add `if: ${{ !cancelled() && steps.build.outcome == 'success' }}` to the Test step at
    `ci.yml:612`.
  - The installer step stays as it is: it correctly has no `!cancelled()`, so it still requires
    every step to have passed.

## B-8. The Rosetta golden loop runs the 60 cases but not the 48 preset goldens, and its commands differ from the CTests

- **Class:** DRIFT
- **Sources:**
  - `ci.yml:637-638` reads: "Under Rosetta 2 it runs the whole Catch2 suite, then every golden
    case with the same commands as the golden CTests". But `ci.yml:697` globs only
    `tests/golden/cases/*.json`, which holds 60 cases.
  - The 48 `golden_preset_*` CTests (`tests/golden/presets/cases/`, defined after
    `tests/CMakeLists.txt:601`) never run under Rosetta. Neither do their fixture
    (`presets_built`) or the content they need.
  - Its command passes no `--content-dir`. The golden CTest passes `--content-dir
    ${GG_GOLDEN_MEDIA_DIR}` (`tests/CMakeLists.txt:593`). `inst-four-samples` then resolves
    through `--media-dir` instead.
  - `CLAUDE.md:526-527` says "then the golden cases three `gg-render` processes at a time".
    `CLAUDE.md` also says "There are 60, and 48 preset goldens beside them: 108" and calls the
    preset goldens labels `golden`.
  - §23 #75 (`INTENT.md:3259`) says "no case is dropped". That is true of the 60, but the 48
    were never added.
  - No carry-over row records the gap. `grep -i rosetta docs/m2-carry-over.md` finds only rows
    234 and 356.
- **Resolution:**
  - Option 1, recommended: make the text match the loop.
    - Change `ci.yml:637-638` to "then the 60 cases of `tests/golden/cases/`, with the golden
      CTests' commands but without `--content-dir` (their media resolve through `--media-dir`)".
    - Add to `CLAUDE.md:527`: "(the 48 preset goldens run on the native arm64 ctest and the x86
      legs, not under Rosetta)".
    - Log a carry-over row for the slice's missing preset-golden coverage.
  - Option 2: extend the loop to the preset goldens. That needs the packed presets copied into
    `$out-media/presets/` (the native ctest's `presets_built` has already packed them into
    `build/mac-universal/golden-media/presets/`) and `--content-dir "$CONTENT_CACHE_DIR/content"`.
    The cost is 48 more renders on 3 cores, within the step's 20-minute limit, which is
    unmeasured.
  - This is the controller's call, and it should be reported.

## B-9. format.yml's header names the wrong source folders

- **Class:** DRIFT
- **Sources:**
  - `.github/workflows/format.yml:5-6` reads: "runs clang-format 23 … over engine core plugin ui
    tools tests clients".
  - `scripts/format.sh:17` has `source_dirs=(engine core media plugin ui tools tests clients
    libs/thirdparty-impl)`.
- **Resolution:** At `format.yml:6`, change the list to "engine core media plugin ui tools tests
  clients libs/thirdparty-impl", or to "the folders `scripts/format.sh` lists".

## B-10. The cmake snippet in "Tests run in parallel" leaves out the third, `[crawl48]`, discovery

- **Class:** DRIFT
- **Sources:**
  - `CLAUDE.md:566-568` shows two calls: `catch_discover_tests(... "~[serial]" ...)` and
    `catch_discover_tests(... "[serial]" ... PROPERTIES RUN_SERIAL TRUE)`.
  - `tests/CMakeLists.txt:198-205` actually has three:
    - `"~[serial]"`;
    - `"[serial]~[crawl48]"` with `RUN_SERIAL TRUE`;
    - `"[crawl48]"` with `RUN_SERIAL TRUE FIXTURES_REQUIRED presets_built`.
  - `CLAUDE.md:384` already describes the crawl48 case's fixture in prose.
- **Resolution:** Update the snippet to the three calls. Or keep the two and add one line under
  them: "(a case that also needs a fixture gets a discovery of its own, as `[crawl48]` does with
  `presets_built`)".

## B-11. The runbook says "the five choices", but the PKG has six

- **Class:** DRIFT
- **Sources:**
  - `docs/releasing.md:278` reads: "Check the five choices installed where the overview says."
  - `docs/releasing.md:34` reads: "the PKG (six choices: the CLAP, VST3 and AU …, the two apps …,
    and the Content …)".
  - The installer sources confirm six:
    - `installer/macos/distribution.xml.in:31-36` has five lines plus `@GG_CONTENT_LINE@`;
    - `installer/macos/make-installer.sh:40` says "it makes a sixth component, the Content
      choice".
- **Resolution:** At `docs/releasing.md:278`, change "five" to "six".

## B-12. §19's installer bullet omits the Content component, and its sweep count and slot count are stale

- **Class:** DRIFT (the timings part is UNSURE)
- **Sources:**
  - **The Content component.** `INTENT.md:3091` reads: "macOS: a PKG with one component per
    format (CLAP, VST3, AU, the two apps) inside a DMG". Against it:
    - §15 (`INTENT.md:~1582`) says "the installers' Content component (macOS, Windows and Linux;
      the PKG's is deselectable)";
    - `docs/releasing.md:34` says six choices;
    - §23 #70 is where the Content component was introduced.
  - **The sweep count.** `INTENT.md:3039-3040` reads: "the sweep harness … **built in
    sub-project 3**: 20 `[sweep]` cases in `tests/sweep/`, about 26.5 s in `dev` and 194 s under
    ASan". `tests/sweep/*.cpp` holds 30 `TEST_CASE`s (3 + 1 + 2 + 19 + 5), and
    `CLAUDE.md:344` says "30 cases".
  - **The timings.** UNSURE: `CLAUDE.md:348-349` and §19 both keep "26.5 s … 194 s". The only
    other source for those figures is `docs/superpowers/specs/2026-10-30-m2-engine-design.md:568`,
    "20 `[sweep]` cases …, about 26.5 s". So the timings were measured with 20 cases and are
    probably stale for 30. I did not re-measure them.
  - **The slot count.** `INTENT.md:3071` reads: "a `[ui]` test walks the 223 slots (with
    sub-project 3's Quality and Budget)". `core/include/gg/core/ParamIds.h:25` says "283 in all"
    (sub-projects 2 and 4 added 60). The UI family may own this one too.
- **Resolution:**
  - At `INTENT.md:3091`, change the parenthesis to "(CLAP, VST3, AU, the two apps and, since M2
    sub-project 5, the Content)".
  - At `INTENT.md:3039`, change "20" to "30" ("built in sub-project 3, extended since").
  - Either re-time the sweep label (`ctest --preset dev -L sweep`, and the same under ASan) and
    update both places, or mark the figures "with its first 20 cases".
  - At `INTENT.md:3071`, change "223 slots" to "283 slots".
  - These are §19 (Part II) edits, so each one needs a §23 entry. One combined "M2 close
    consistency pass" row is enough.

## B-13. The carry-over's number 233 names two rows

- **Class:** DRIFT (process record)
- **Sources:**
  - `docs/m2-carry-over.md:19-20` states the rule: "A row added from now on takes the next free
    number from 300 up, so no number names two rows." `docs/m2-carry-over.md:24-25` then says:
    "M2 sub-project 5 added rows 233 and 234, numbered before the rule above (no M1 row has
    either number)."
  - But M1 did have a row 233. §23 #60 (`INTENT.md:3244`) says: "The AUv2 parameter order is
    ascending parameter ID (sub-project 5's decision D2, #58, carry-over row 233) … (185 and 233
    closed)".
  - The same number also names M2's row. §23 #70 (`INTENT.md:3254`) says: "… `preset-discovery-load`,
    which run their own path check (carry-over row 233)". `CLAUDE.md:1181` says "(carry-over row
    233)" for the clap-validator Windows exclusion.
  - Rows are numbered by the line each stood on in the M1 file before it froze. The frozen
    `docs/m1-carry-over.md` has no "233" token, which is probably why the claim was made.
- **Resolution:**
  - Option 1, recommended and the lightest: keep both numbers and correct
    `docs/m2-carry-over.md:24-25` to say "(M1's row 233, the AUv2 order, closed at M1's close,
    §23 #60; these name the M2 rows)". In `CLAUDE.md:1181`, write "(M2 carry-over row 233)".
  - Option 2: renumber M2's 233 and 234 to the next free numbers, 388 and 389, and update every
    citer: `CLAUDE.md:1181`, §23 #70, and the presets spec lines 929 and 978. (`ci.yml:789` cites
    only the file, so it needs no change.) That costs edits to a decision-log row, so it needs
    director sign-off for Part II.

## B-14. CMakeLists.txt's signing comment omits the signed dry run and the unsigned nightly

- **Class:** DRIFT
- **Sources:**
  - `CMakeLists.txt:54-55` reads: "'-' signs ad hoc, locally and on every CI run but a tag's or
    the nightly's."
  - `ci.yml:455` and `ci.yml:539-541` show otherwise: the Developer ID is passed whenever
    `release_kind != 'none'` and `sign == 'true'`.
    - A dispatch with `installers: true` (`dry-run`) also signs with the real identity.
    - A nightly without the secrets signs ad hoc (`ci.yml:207-218`; `docs/releasing.md:26`,
      "signed when the secrets exist").
- **Resolution:** At `CMakeLists.txt:54-55`, write instead: "'-' signs ad hoc: locally, and on
  every CI run that builds no installer or has no signing secrets; a tag, the nightly or an
  installer dry run with the secrets passes the Developer ID."

## B-15. §16 contradicts itself about the four-layer cases' baseline entries, and §23 #73 cites §19 for caches

- **Class:** DRIFT
- **Sources:**
  - **The four-layer cases.** `INTENT.md:1694-1700` (§16, the sub-project 1 bullet) says of
    `engine-layers` and `engine-layers-full`: "their ratios 'not in the baseline'; their figures
    are measured at sub-project 7's idle run, which adds their baseline entries."
    - The same section disagrees at `INTENT.md:1734-1736`, the AVX2 bullet: "The x86 baselines
      were remade from the same runs, and the six report-only cases now have entries beside the
      four gated ones on those legs."
    - The committed `linux-gcc13.json` and `windows-clangcl.json` entries (`630d740`) hold both
      layer cases, 30 ratios each. `mac-universal.json` holds them on no entry.
  - **The cache citation.** §23 #73 (`INTENT.md:3257`) ends: "the caches are still saved by
    pushes to `main` (§19)". §19 (`INTENT.md:2808-3117`) says nothing about caches. The cache
    rules live in §23 #57 (3) and `CLAUDE.md` "CI caches".
- **Resolution:**
  - At `INTENT.md:1698-1700`, write: "… their ratios have entries on the x86 legs since the AVX2
    task (below) and none on `mac-universal`. Their idle-run figures are measured at sub-project
    7's idle run."
  - In #73, change "(§19)" to "(#57)". A decision-log row is history, so make this a pointer fix
    noted in the close's consistency row, or leave the row and record the misreference there.

---

## Appendix: the prerequisites CI installs, per OS (for a README "Building from source" section)

Line numbers are `ci.yml`'s unless a line says otherwise. The runners' preinstalled tools are
noted where the workflow relies on them without installing them.

### Linux (`linux-gcc13`, runner `ubuntu-22.04`, ci.yml:254-257)

1. **gcc-13 / g++-13** from the `ubuntu-toolchain-r/test` PPA (356-360):
   `sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test && sudo apt-get update`, then
   `sudo apt-get install -y gcc-13 g++-13`. The configure fails below GCC 13
   (`CMakeLists.txt:70-72`).
2. **`surge-synthesizer/sst-githubactions/prepare-for-juce@5bb92ec9d3401eba6a85f8edea98395b3b866e84`**
   (`# main`), with `os: Linux`, `gccversion: 13` (362-366). Its `action.yml` at that SHA,
   read through `gh api`, is a composite action that runs:
   - `sudo apt-get update`
   - `sudo apt install libfontconfig1-dev libasound2-dev libx11-dev libxcomposite-dev
     libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libfreetype6-dev
     libglu1-mesa-dev libjack-jackd2-dev`. It has no `-y`: the runner image assumes yes, but a
     README should say `apt install -y`.
   - `sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 13` (and the same for
     `g++`). It installs no compiler.
3. **`sudo apt-get install -y ninja-build xvfb libxi-dev`** (370-372). `libxi-dev` is there
   because JUCE 9's `juce_gui_basics` includes `<X11/extensions/XInput2.h>`. `xvfb` is only for
   `xvfb-run` under pluginval (803).
4. **`sudo apt-get install -y ccache`** (385-387). It is CI only, and optional locally.
5. **Relied on, preinstalled:** CMake 3.22 or newer, git, curl, unzip, `file`, `objdump`/`nm`
   (binutils; the libstdc++ check, 555-591).
6. A local build needs no other audio package: RtAudio is fetched at configure. Note
   `libs/CMakeLists.txt:357-358`: `JUCE_USE_CURL=0` and `JUCE_WEB_BROWSER=0`, so there is no
   libcurl or webkit2gtk.

### macOS (`mac-universal`, runner `macos-26`, an Apple M1 VM, ci.yml:245-248)

1. **prepare-for-juce** with `os: macOS` does nothing: its step is `run: echo "MacOS is just
   fine"`, and the `xcode-select` line is commented out. So the image's default Xcode provides
   AppleClang, the SDK, `codesign`, `lipo` and `auval`.
2. **`brew install ninja`** if `ninja` is missing (374-376), with `HOMEBREW_NO_AUTO_UPDATE=1`
   (259).
3. **`brew install ccache`** if missing (381-383). It is CI only.
4. **Rosetta 2**, for the x86_64 slice's tests only:
   `sudo softwareupdate --install-rosetta --agree-to-license` when `arch -x86_64 /usr/bin/true`
   fails (652-654).
5. **Relied on, preinstalled:** CMake 3.22 or newer, git, curl, unzip, openssl (signing only).
   The macOS deployment target is 11.0 (`CMakeLists.txt:13`).
6. **For RTSan locally** (not on the macOS leg): Homebrew `llvm`, as `GG_LLVM=/opt/homebrew/opt/llvm`
   (CLAUDE.md, `check.sh:155-158`).

### Windows (`windows-clangcl`, runner `windows-2022`, ci.yml:249-253)

1. **Visual Studio 2022** with the component `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`.
   Its bundled LLVM must have `VC\Tools\Llvm\x64\bin\clang-cl.exe`: the step throws otherwise
   (410-431), so the "C++ Clang tools for Windows" component is needed. The step then:
   - loads `vcvars64.bat` into the environment;
   - puts that LLVM first on `Path`;
   - sets `GG_WIN_LINKER` to `<VCToolsInstallDir>\bin\Hostx64\x64\link.exe`, passed as
     `-DCMAKE_LINKER` (533). That is needed only in Git Bash, where `/usr/bin/link` shadows it.
     A local "x64 Native Tools" shell needs none of this.
2. **Ninja:** `choco install -y ninja --no-progress` if `ninja` is missing (532). It comes with
   VS's CMake component, or install it with winget or choco locally.
3. **ccache 4.14.2**, CI only (391-402):
   `https://github.com/ccache/ccache/releases/download/v4.14.2/ccache-4.14.2-windows-x86_64.zip`,
   SHA-256 `6993ce645e53192cec592ee23186200b574ac4c59097ecde8d9cfd681b47ae84`, unzipped and put
   on `PATH`.
4. **Inno Setup 6**, for the installer EXE only: `find_program(GG_ISCC ISCC PATHS "C:/Program
   Files (x86)/Inno Setup 6")` (`cmake/products.cmake:170`). It is preinstalled on the image.
   Without it, `gg-installer` makes the zip alone.
5. **Git Bash** is every step's `shell: bash` (92-94). It provides curl, unzip, `sha256sum` and
   `cygpath`. **Relied on, preinstalled:** CMake 3.22 or newer, git, PowerShell 7 (`pwsh`, 412).
6. Steinberg's VST3 validator alone builds with `-G "Visual Studio 17 2022" -A x64` (870).

### Sanitizer jobs (asan × 6 shards, rtsan, runner `ubuntu-24.04`, ci.yml:992-1124)

- `sudo apt-get update && sudo apt-get install -y ninja-build ccache` (1049-1050).
- **LLVM 20.1.8**, LLVM's official release:
  `https://github.com/llvm/llvm-project/releases/download/llvmorg-20.1.8/LLVM-20.1.8-Linux-X64.tar.xz`,
  SHA-256 `1ead36b3dfcb774b57be530df42bec70ab2d239fbce9889447c7a29a4ddc1ae6` (1012-1013,
  1051-1058). It is extracted into `/opt/llvm-20.1.8` (`GG_LLVM`, 1015), and only these members:
  `bin/clang`, `bin/clang++`, `bin/clang-20`, `bin/llvm-symbolizer` and `lib/clang/20`
  (1060-1062). It is checked for `libclang_rt.rtsan.a` (1065).
- `CC` and `CXX` are set to that clang for the asan preset (1108-1109).
- No JUCE or X11 packages are installed: both presets set `GG_BUILD_EDITOR=OFF`.

### Format job (`format.yml`, runner `ubuntu-24.04`)

- `python3 -m venv`, then
  `pip install --only-binary :all: --require-hashes clang-format==23.1.3 --hash=sha256:58bf1804216501669832a39f2415e14284337438a062ff564729792adec664ac`
  (format.yml:48-56).
- Locally, `scripts/format.sh` takes any clang-format whose major version is 23: Homebrew LLVM's,
  `clang-format-23` or `$CLANG_FORMAT`.

### Fetched by scripts on every leg (no package manager)

- **SDKs** (`scripts/fetch-sdks.sh`, git clones at clap-wrapper's `base_sdks.cmake` pins):
  - vst3sdk `v3.8.0_build_66`;
  - AudioUnitSDK `AudioUnitSDK-1.1.0`;
  - rtaudio `6.0.1`;
  - rtmidi `6.0.0`;
  - wil `v1.0.240803.1`.

  The CLAP and AAX SDKs are skipped. Locally, clap-wrapper fetches these itself at the first
  configure.
- **Content** (`scripts/fetch-content.sh` → `fetch-content.cmake`): `content/pin.txt` pins tag
  `v2`,
  `https://github.com/sethbc/giga-grains-content/releases/download/v2/giga-grains-content-v2.zip`,
  SHA-256 `3b0b177a…07befcd`. Locally, the first `ctest` fetches it through `gg-content`.
- **Validators** (`scripts/fetch-validators.sh`, SHA-256 checked):
  - clap-validator `0.4.1`, build `0.4.1-127-g152b982`;
  - pluginval `v1.0.4`.

---

## Checked and found consistent

- **CLAUDE.md's sanitizer text against ci.yml:**
  - six ASan shards with `-I <shard>,,6`;
  - `-LE slow` only on a pull request or a push to `main`;
  - RTSan runs everything;
  - the `sanitizers (asan, rtsan)` aggregate passes only on `needs.sanitizer.result ==
    success`;
  - a timeout of 90;
  - one ASan shard (shard 1) and the RTSan job save the compiler cache (`matrix.shard <= 1`);
  - the LLVM archive is cached by version and SHA-256, saved by the RTSan job, and checked on a
    hit.
- **The Rosetta step:** three disjoint shards (`--order decl --shard-count 3`), then
  `[serial]`, then golden renders with `xargs -P 3`; `[slow]` left out on a pull request or a
  push to `main`; a 20-minute limit. These match `CLAUDE.md:525-530` and §23 #75. The golden
  scope is the exception (B-8).
- **The benchmark step:**
  - `--quick --rounds 1` on a pull request or a push to `main`, otherwise full length, with
    `--baseline` when the file exists;
  - exit 3 becomes a warning;
  - a 60-minute step and a 120-minute job;
  - an upload only on a run that isn't a pull request or a push to `main`;
  - the nightly cron `17 3 * * *`.

  All of this matches CLAUDE.md, §16 (`INTENT.md:1795-1797`) and §23 #73 and #75.
- **The caches** (SDK, content, ccache, LLVM):
  - their keys and their restore-on-every-run, save-only-on-`main` rule;
  - ccache also saved after a failed build (`!cancelled()`);
  - Windows ccache 4.14.2 with its SHA-256.

  These match CLAUDE.md "CI caches" and §23 #57 and #76.
- **The validators:** clap-validator excludes `preset-discovery-(crawl|descriptor-consistency|load)`
  on Windows only; pluginval runs at strictness 10, under `xvfb-run -a --skip-gui-tests` on
  Linux; auval runs the three identities with a 30 s registrar wait; Steinberg's validator
  builds from `VST3_SDK_ROOT`.
- **`GG_REQUIRE_AVX2=1`** is set on the non-macOS legs only (617).
- **The Linux libstdc++ ceiling:** GLIBCXX ≤ 3.4.30, CXXABI ≤ 1.3.13, no exported `std::`. This
  matches §15 and CLAUDE.md.
- **CMakePresets.json against CLAUDE.md's preset table:**
  - every binary directory is `build/<preset>`, with `windows-clangcl` → `build/windows`;
  - build types: Debug, Release, Debug, RelWithDebInfo, then Release for the three inherited or
    CI presets;
  - asan and rtsan set `GG_BUILD_EDITOR=OFF`;
  - rtsan takes its compilers from `$env{GG_LLVM}`;
  - `windows-clangcl` is Ninja with clang-cl;
  - the test presets run `jobs: 8`.
- **The top-level `CMakeLists.txt`:** version `0.2.0` with `GG_VERSION_PRERELEASE` `dev`, so
  the build reports `0.2.0-dev`; the options `GG_BUILD_PLUGINS`, `GG_BUILD_EDITOR`,
  `GG_SANITIZE`, `GG_USE_RTSAN` and `GG_COPY_PLUGIN_AFTER_BUILD`; the GCC-13 floor; the RTSan
  Clang ≥ 20 check that names `GG_LLVM`; and `gg-all`'s members.
- **Counts:** 60 golden cases plus 48 preset goldens make 108. There are 11 `slow` tests (media
  1, Space 4, core 2, render 1, engine/determinism 2, `snap_cli`), matching
  `CLAUDE.md:406-412`. `snap_cli` has labels `plugin;slow` and a timeout of 300.
  `installer_build_products` has `RUN_SERIAL`.
- **gg-bench:** 21 case names × 3 blocks give 63 configurations, matching `CLAUDE.md:1018`.
- **The baseline models:**
  - mac: the M1 VM, 12 ratios;
  - Windows: 7763, 9V45 and Xeon 8370C, 30 ratios each, and 9V74, 12 ratios, from the SSE era;
  - Linux: 7763 and 9V45, 30 ratios each;
  - `engine-live-long`, `-smear`, `-buses` and `-spectral` in no entry.

  These match `CLAUDE.md:1080-1088` and the baselines README. The README's model lists are
  right. The exception is CLAUDE.md's "seen so far" (B-6).
- **`scripts/check.sh`:**
  - its usage line;
  - it configures only without a `CMakeCache.txt`;
  - the Homebrew-LLVM default for rtsan;
  - `gg-all` for asan and rtsan;
  - `-L` passed through;
  - the false values for `GG_COPY_PLUGIN_AFTER_BUILD`, including the case-sensitive NOTFOUND;
  - its logs and exit codes.

  All of this matches CLAUDE.md.
- **`scripts/worktree-setup.sh`:** `--configure` checked before any clone, and the rtsan
  Homebrew default. **`scripts/update-goldens.sh`:** both case folders, `--content-dir`, and the
  `wav-sha256.txt` comparison. **`scripts/fetch-validators.sh`:** the pins match CLAUDE.md.
  **`scripts/format.sh`:** major version 23 and the binary's search order.
- **`docs/releasing.md` against the `plan` and `release` jobs:**
  - the four kinds and their triggers;
  - the seven secrets;
  - a partial set fails;
  - a release is always signed;
  - a published nightly must be signed;
  - the hotfix check against `origin/main` or `origin/release/*`;
  - `--verify-tag --draft` and a pre-release when the version has a suffix;
  - the file names of the four installers, the source tarball and SHA256SUMS;
  - the installer step's 70 minutes;
  - `GG_PUBLISH_NIGHTLY: "false"`;
  - a push to `release/*` runs no CI (`push: branches: [main]`).
- **The install locations:** the Windows Content component installs into
  `{commonappdata}\sethbc\Giga Grains`, and the macOS one into `/Library/Application
  Support/sethbc/Giga Grains`. Both match `docs/releasing.md:34-38`.
- **§23 #74's "every `slow` test still runs without a sanitizer on every build leg's ctest":**
  the native ctest step passes no `-LE`.
- **ci.yml's comments about prepare-for-juce** (352-355, 368: no compiler, no `libxi-dev`) match
  the action's source.
